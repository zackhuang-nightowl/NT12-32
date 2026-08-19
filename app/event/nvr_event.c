/***************************************************************************************
 *  nvr_event.c — 事件中枢。见 nvr_event.h / 计划 §B4。
 *
 *  ★ 作为 nop 事件脊柱(nop_hub)的**订阅者**:相机 ONVIF 事件与本地 AI 事件都经 nop_event_publish
 *  进 nop_hub → 本中枢 sink 统一处理:①每通道每类型状态位(供 GUI_longPolling 的 Motion/Human/Face/Car
 *  位图)②事件录像触发 ③预览图标。此前 ONVIF 事件直接进 nop_hub、绕过本中枢 → longPolling 恒 0
 *  且不触发事件录像;订阅后两者都通。
 ***************************************************************************************/
#include "nvr_event.h"
#include "nvr_streaming.h"   /* nvr_stream_set_event:事件录像落盘 */
#include "nvr_settings.h"
#include "nvr_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define EVT_MAX_CH 32
#define ICON_DECAY_S 5        /* 无事件多少秒后清状态位/图标 */
#define SNAP_Q_CAP   8
#define SNAP_JPEG_MAX (256u * 1024u)
#define META_Q_CAP   16

typedef struct {
    int      chn;
    uint64_t eid;
    uint32_t ts;
    uint8_t *jpeg;
    size_t   jpeg_len;
} snap_job_t;

typedef struct {
    int      kind;      /* 0=enable 1=pull */
    int      chn;
    uint64_t eid;
    uint32_t start;
} meta_job_t;

struct nvr_evt_hub {
    nvr_evt_cfg_t cfg;
    pthread_mutex_t lock;                 /* sink 在发布线程更新;longPolling 在命令线程读 */
    nop_event_subscription_t *sub;        /* nop_hub 订阅句柄 */
    struct { unsigned bits; time_t last; } icon[EVT_MAX_CH];
    snap_job_t      snap_q[SNAP_Q_CAP];
    int             snap_head, snap_tail, snap_n;
    pthread_cond_t  snap_cv;
    pthread_t       snap_tid;
    int             snap_started;
    volatile int    snap_run;
    meta_job_t      meta_q[META_Q_CAP];
    int             meta_head, meta_tail, meta_n;
    pthread_cond_t  meta_cv;
    pthread_t       meta_tid;
    int             meta_started;
    volatile int    meta_run;
};

/* 8012 事件中心数字 msgType → detect 类型(NightOwl 私有编号)。未知/0 → NOP_DETECT_TYPE_MAX。 */
nop_detect_type_t nvr_evt_detect_from_msgtype(uint32_t m)
{
    switch (m) {
        case 1:     return NOP_DETECT_MOTION;
        case 2:     return NOP_DETECT_HUMAN;
        case 3:     return NOP_DETECT_FACE;
        case 30305: return NOP_DETECT_VEHICLE;
        case 30316: return NOP_DETECT_ANIMAL;
        case 30317: return NOP_DETECT_PACKAGE;
        case 30312: return NOP_DETECT_DOORBELL_RING;
        case 30103: return NOP_DETECT_LINE_CROSS;
        case 30104: return NOP_DETECT_FIELD_INTRUSION;
        default:    return NOP_DETECT_TYPE_MAX;
    }
}

int nvr_evt_rectype_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:          return RSDK_REC_MOTION;
        case NOP_DETECT_PIXEL_CHANGE:    return RSDK_REC_MOTION;
        case NOP_DETECT_HUMAN:           return RSDK_REC_HUMAN;
        case NOP_DETECT_FACE:            return RSDK_REC_FACE;
        case NOP_DETECT_VEHICLE:         return RSDK_REC_VEHICLE;
        case NOP_DETECT_LINE_CROSS:      return RSDK_REC_LINECROSS;
        case NOP_DETECT_FIELD_INTRUSION: return RSDK_REC_INTRUSION;
        case NOP_DETECT_ANIMAL:          return RSDK_REC_ANIMAL;
        case NOP_DETECT_PACKAGE:         return RSDK_REC_PACKAGE;
        case NOP_DETECT_DOORBELL_RING:   return RSDK_REC_DOORBELL;
        default:                         return -1;
    }
}

/* detect → GUI_get/setChannelEventRecordingSchedule 的 sensor 名 */
static const char *sensor_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:
        case NOP_DETECT_PIXEL_CHANGE:    return "pixelChange";
        case NOP_DETECT_HUMAN:           return "human";
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return "face";
        case NOP_DETECT_VEHICLE:         return "vehicle";
        case NOP_DETECT_ANIMAL:          return "animal";
        case NOP_DETECT_PACKAGE:         return "package";
        case NOP_DETECT_DOORBELL_RING:   return "doorbellRing";
        case NOP_DETECT_LINE_CROSS:      return "lineCross";
        case NOP_DETECT_FIELD_INTRUSION: return "fieldIntrusion";
        default:                         return NULL;
    }
}

/* 事件类型 → GUI_longPolling 的四类状态位。motion/human/face/car 对应 GUI 四个位图。 */
static unsigned icon_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:
        case NOP_DETECT_PIXEL_CHANGE:    return NVR_ICON_MOTION;
        case NOP_DETECT_HUMAN:
        case NOP_DETECT_FIELD_INTRUSION:
        case NOP_DETECT_LINE_CROSS:      return NVR_ICON_HUMAN;   /* 人/越线/入侵 → 人形 */
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return NVR_ICON_FACE;
        case NOP_DETECT_VEHICLE:         return NVR_ICON_CAR;
        default:                         return 0;
    }
}

/* 统一事件处理(nop_hub 发布线程回调):置状态位 + 事件录像触发 + 预览图标。 */
static void evt_sink(void *sink_ctx, const nop_event_t *ev)
{
    nvr_evt_hub_t *h = (nvr_evt_hub_t *)sink_ctx;
    if (!h || !ev || ev->channel < 0 || ev->channel >= EVT_MAX_CH) return;
    int chn = ev->channel;
    unsigned bit = icon_of(ev->type);
    int rectype = nvr_evt_rectype_of(ev->type);

    unsigned newbits = 0;
    uint64_t eid = 0;
    if (bit) {
        pthread_mutex_lock(&h->lock);
        h->icon[chn].bits |= bit;
        h->icon[chn].last = time(NULL);
        newbits = h->icon[chn].bits;
        pthread_mutex_unlock(&h->lock);
        if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, chn, newbits);
    }
    if (rectype >= 0 && h->cfg.rs) {
        /* 事件录像周排程门控；无保存规则=不录(GET 仍可回 7×24 给 GUI) */
        const char *sensor = sensor_of(ev->type);
        int allow_rec = 1;
        if (h->cfg.settings && sensor) {
            time_t now = time(NULL);
            struct tm tmv;
            localtime_r(&now, &tmv);
            int wday = (tmv.tm_wday == 0) ? 7 : tmv.tm_wday;
            int sod  = tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec;
            if (!nvr_settings_schedule_allows(h->cfg.settings, chn, "record_event",
                                             sensor, wday, sod)) {
                NVR_LOGI("event", "ch%d sensor=%s 不在事件录像排程内，跳过落盘", chn, sensor);
                allow_rec = 0;
            }
        }
        if (allow_rec) {
        uint32_t ts = (uint32_t)(ev->timestamp_ms / 1000);
        int post_s = NVR_EVT_POST_RECORD_S;
        int pre_s  = 5;
        if (h->cfg.settings) {
            post_s = nvr_settings_record_post_s_get(h->cfg.settings, chn);
            pre_s  = nvr_settings_record_pre_s_get(h->cfg.settings, chn);
        }
        uint32_t start = (pre_s > 0 && ts > (uint32_t)pre_s) ? (ts - (uint32_t)pre_s) : ts;
        eid = nvr_rec_trigger_event(h->cfg.rs, chn, rectype, start, post_s);
        /* ★ 事件录像落盘:连续轨打标,或仅事件待命时开片段(预录 flush + 后录)。
         * 索引窗 [ts-pre, ts+post];puller 过 pend_event_end 自动清标签/关片段。 */
        if (eid && h->cfg.sm)
            nvr_stream_set_event(h->cfg.sm, chn, eid, rectype, start,
                                 ts + (uint32_t)post_s);
        }
    }
    /* 抓拍入队：不阻塞事件发布线程。失败/满队列丢弃，不影响录像/图标。 */
    if (h->cfg.on_snap) {
        uint32_t ts_sec = (uint32_t)(ev->timestamp_ms / 1000);
        uint64_t seid = eid;
        if (!seid) seid = ((uint64_t)chn << 32) | (uint64_t)ts_sec;
        snap_job_t job;
        memset(&job, 0, sizeof(job));
        job.chn = chn; job.eid = seid; job.ts = ts_sec;
        if (ev->jpeg && ev->jpeg_len > 0 && ev->jpeg_len <= SNAP_JPEG_MAX) {
            job.jpeg = (uint8_t *)malloc(ev->jpeg_len);
            if (job.jpeg) {
                memcpy(job.jpeg, ev->jpeg, ev->jpeg_len);
                job.jpeg_len = ev->jpeg_len;
            }
        }
        pthread_mutex_lock(&h->lock);
        if (h->snap_n < SNAP_Q_CAP) {
            h->snap_q[h->snap_tail] = job;
            h->snap_tail = (h->snap_tail + 1) % SNAP_Q_CAP;
            h->snap_n++;
            pthread_cond_signal(&h->snap_cv);
            job.jpeg = NULL;
        }
        pthread_mutex_unlock(&h->lock);
        free(job.jpeg);   /* 入队成功则已置 NULL */
    }
    NVR_LOGI("event", "ch%d AI事件 type=%d → rectype=%d bits=0x%x", chn, (int)ev->type, rectype, newbits);
}

static void *snap_worker(void *arg)
{
    nvr_evt_hub_t *h = arg;
    while (h->snap_run) {
        snap_job_t job;
        pthread_mutex_lock(&h->lock);
        while (h->snap_n == 0 && h->snap_run)
            pthread_cond_wait(&h->snap_cv, &h->lock);
        if (!h->snap_run && h->snap_n == 0) {
            pthread_mutex_unlock(&h->lock);
            break;
        }
        job = h->snap_q[h->snap_head];
        h->snap_head = (h->snap_head + 1) % SNAP_Q_CAP;
        h->snap_n--;
        pthread_mutex_unlock(&h->lock);
        if (h->cfg.on_snap)
            h->cfg.on_snap(h->cfg.snap_user, job.chn, job.eid, job.ts,
                           job.jpeg, job.jpeg_len);
        free(job.jpeg);
    }
    return NULL;
}

static void meta_enqueue(nvr_evt_hub_t *h, const meta_job_t *job)
{
    if (!h || !job) return;
    pthread_mutex_lock(&h->lock);
    if (h->meta_n < META_Q_CAP) {
        h->meta_q[h->meta_tail] = *job;
        h->meta_tail = (h->meta_tail + 1) % META_Q_CAP;
        h->meta_n++;
        pthread_cond_signal(&h->meta_cv);
    }
    pthread_mutex_unlock(&h->lock);
}

static void *meta_worker(void *arg)
{
    nvr_evt_hub_t *h = arg;
    while (h->meta_run) {
        meta_job_t job;
        pthread_mutex_lock(&h->lock);
        while (h->meta_n == 0 && h->meta_run)
            pthread_cond_wait(&h->meta_cv, &h->lock);
        if (!h->meta_run && h->meta_n == 0) {
            pthread_mutex_unlock(&h->lock);
            break;
        }
        job = h->meta_q[h->meta_head];
        h->meta_head = (h->meta_head + 1) % META_Q_CAP;
        h->meta_n--;
        pthread_mutex_unlock(&h->lock);
        if (job.kind == 0) {
            if (h->cfg.on_meta_enable)
                h->cfg.on_meta_enable(h->cfg.meta_user, job.chn);
        } else if (h->cfg.on_meta_pull) {
            h->cfg.on_meta_pull(h->cfg.meta_user, job.chn, job.eid, job.start);
        }
    }
    return NULL;
}

int nvr_evt_init(const nvr_evt_cfg_t *cfg, nvr_evt_hub_t **out)
{
    if (!cfg || !out) return -1;
    nvr_evt_hub_t *h = calloc(1, sizeof(*h));
    if (!h) return -1;
    h->cfg = *cfg;
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->snap_cv, NULL);
    pthread_cond_init(&h->meta_cv, NULL);
    h->snap_run = 1;
    h->snap_started = (pthread_create(&h->snap_tid, NULL, snap_worker, h) == 0);
    if (!h->snap_started) h->snap_run = 0;
    h->meta_run = 1;
    h->meta_started = (pthread_create(&h->meta_tid, NULL, meta_worker, h) == 0);
    if (!h->meta_started) h->meta_run = 0;
    /* 订阅事件脊柱:相机 ONVIF + 本地 AI 事件都经此回调统一处理。 */
    if (cfg->nop_hub) h->sub = nop_event_subscribe(cfg->nop_hub, evt_sink, h);
    *out = h;
    return 0;
}

void nvr_evt_set_snap(nvr_evt_hub_t *h,
                      void (*on_snap)(void *user, int chn, uint64_t event_id, uint32_t ts,
                                      const uint8_t *inline_jpeg, size_t inline_len),
                      void *snap_user)
{
    if (!h) return;
    h->cfg.on_snap = on_snap;
    h->cfg.snap_user = snap_user;
}

void nvr_evt_set_meta(nvr_evt_hub_t *h,
                      void (*on_enable)(void *user, int chn),
                      void (*on_pull)(void *user, int chn, uint64_t event_id, uint32_t start_ts),
                      void *meta_user)
{
    if (!h) return;
    h->cfg.on_meta_enable = on_enable;
    h->cfg.on_meta_pull = on_pull;
    h->cfg.meta_user = meta_user;
}

void nvr_evt_queue_meta_enable(nvr_evt_hub_t *h, int chn)
{
    meta_job_t j;
    memset(&j, 0, sizeof(j));
    j.kind = 0; j.chn = chn;
    meta_enqueue(h, &j);
}

void nvr_evt_queue_meta_pull(nvr_evt_hub_t *h, int chn, uint64_t event_id, uint32_t start_ts)
{
    meta_job_t j;
    memset(&j, 0, sizeof(j));
    j.kind = 1; j.chn = chn; j.eid = event_id; j.start = start_ts;
    meta_enqueue(h, &j);
}

void nvr_evt_deinit(nvr_evt_hub_t *h)
{
    if (!h) return;
    if (h->cfg.nop_hub && h->sub) nop_event_unsubscribe(h->cfg.nop_hub, h->sub);
    h->snap_run = 0;
    pthread_cond_signal(&h->snap_cv);
    if (h->snap_started) pthread_join(h->snap_tid, NULL);
    h->meta_run = 0;
    pthread_cond_signal(&h->meta_cv);
    if (h->meta_started) pthread_join(h->meta_tid, NULL);
    while (h->snap_n > 0) {
        free(h->snap_q[h->snap_head].jpeg);
        h->snap_head = (h->snap_head + 1) % SNAP_Q_CAP;
        h->snap_n--;
    }
    pthread_cond_destroy(&h->snap_cv);
    pthread_cond_destroy(&h->meta_cv);
    pthread_mutex_destroy(&h->lock);
    free(h);
}

/* 本地(app 侧)产生事件:发布到脊柱 → evt_sink 统一处理(状态位/录像/图标)。 */
int nvr_evt_ingest(nvr_evt_hub_t *h, int chn, nop_detect_type_t type, uint64_t ts_ms)
{
    if (!h || chn < 0 || chn >= EVT_MAX_CH) return -1;
    if (h->cfg.nop_hub) {
        nop_event_t ev; memset(&ev, 0, sizeof(ev));
        ev.channel = chn; ev.type = type; ev.timestamp_ms = ts_ms;
        nop_event_publish(h->cfg.nop_hub, &ev);   /* → evt_sink */
    } else {
        evt_sink(h, &(nop_event_t){ .channel = chn, .type = type, .timestamp_ms = ts_ms });
    }
    return 0;
}

/* GUI_longPolling 用:四类状态的**每通道位图**(bit chn=通道 chn+1 近 ICON_DECAY_S 秒内有该类事件)。 */
void nvr_evt_masks(nvr_evt_hub_t *h, uint32_t *motion, uint32_t *human, uint32_t *face, uint32_t *car)
{
    uint32_t m = 0, hu = 0, f = 0, c = 0;
    if (h) {
        pthread_mutex_lock(&h->lock);
        for (int i = 0; i < EVT_MAX_CH; i++) {
            unsigned b = h->icon[i].bits;
            if (b & NVR_ICON_MOTION) m  |= (1u << i);
            if (b & NVR_ICON_HUMAN)  hu |= (1u << i);
            if (b & NVR_ICON_FACE)   f  |= (1u << i);
            if (b & NVR_ICON_CAR)    c  |= (1u << i);
        }
        pthread_mutex_unlock(&h->lock);
    }
    if (motion) *motion = m; if (human) *human = hu; if (face) *face = f; if (car) *car = c;
}

void nvr_evt_tick(nvr_evt_hub_t *h)
{
    if (!h) return;
    time_t now = time(NULL);
    pthread_mutex_lock(&h->lock);
    for (int i = 0; i < EVT_MAX_CH; i++) {
        if (h->icon[i].bits && now - h->icon[i].last >= ICON_DECAY_S) {
            h->icon[i].bits = 0;
            pthread_mutex_unlock(&h->lock);
            if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, i, 0);
            pthread_mutex_lock(&h->lock);
        }
    }
    pthread_mutex_unlock(&h->lock);
}
