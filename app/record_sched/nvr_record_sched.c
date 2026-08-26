/***************************************************************************************
 *  nvr_record_sched.c — 录像调度/事件与云存协调。见 nvr_record_sched.h / 计划 §B3。
 *  注意：连续录像由 ③streaming 负责；本模块只做事件/云存登记 + 满盘策略 + 状态。
 ***************************************************************************************/
#include "nvr_record_sched.h"
#include "nvr_record_policy.h"
#include "nvr_defaults.h"   /* 出厂默认(NVR_DEF_POST_RECORD_S 等) */
#include "rsdk_evtidx.h"   /* event_id 铸造(盘上事件权威) */
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REC_MAX_CH 32

typedef struct {
    int      in_use;
    int      codec;
    int      evt_active;       /* 有进行中的事件时窗 */
    int      evt_rectype;
    uint8_t  evt_cloud;        /* 本事件需云存(供延长/结束沿用) */
    uint64_t evt_id;
    uint32_t evt_start;        /* 事件起始 epoch */
    time_t   evt_until;        /* 后录截止 */
} rec_ch_t;

struct nvr_rec_sched {
    nvr_rec_sched_cfg_t cfg;
    rec_ch_t ch[REC_MAX_CH];
    int      stopped;
};

int nvr_rec_sched_init(const nvr_rec_sched_cfg_t *cfg, nvr_rec_sched_t **out)
{
    if (!cfg || !out) return -1;
    nvr_rec_sched_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->cfg = *cfg;
    if (r->cfg.post_record_s <= 0) r->cfg.post_record_s = NVR_DEF_POST_RECORD_S;
    *out = r;
    return 0;
}

void nvr_rec_sched_deinit(nvr_rec_sched_t *r) { if (r) free(r); }

static rec_ch_t *ch_of(nvr_rec_sched_t *r, int chn)
{
    if (!r || chn < 0 || chn >= REC_MAX_CH) return NULL;
    return &r->ch[chn];
}

int nvr_rec_channel_up(nvr_rec_sched_t *r, int chn, int codec)
{
    rec_ch_t *c = ch_of(r, chn);
    if (!c) return -1;
    c->in_use = 1; c->codec = codec;
    return 0;
}

int nvr_rec_channel_down(nvr_rec_sched_t *r, int chn)
{
    rec_ch_t *c = ch_of(r, chn);
    if (!c) return -1;
    c->in_use = 0; c->evt_active = 0;
    return 0;
}

static const char *rectype_trigger(int rectype)
{
    switch (rectype) {
        case RSDK_REC_DOORBELL: return "doorbellRing";
        case RSDK_REC_FACE:     return "face";
        case RSDK_REC_HUMAN:    return "human";
        case RSDK_REC_VEHICLE:  return "vehicle";
        default:                return "pixelChange";
    }
}

uint64_t nvr_rec_trigger_event(nvr_rec_sched_t *r, int chn, int rectype, uint32_t start_epoch,
                               int post_s, int *cloud_out,
                               uint32_t *win_start_out, uint32_t *win_end_out)
{
    if (cloud_out) *cloud_out = 0;
    if (win_start_out) *win_start_out = start_epoch;
    if (win_end_out) *win_end_out = 0;
    rec_ch_t *c = ch_of(r, chn);
    if (!c || !c->in_use) return 0;
    time_t now = time(NULL);
    if (start_epoch == 0) start_epoch = (uint32_t)now;
    int post = (post_s > 0) ? post_s : r->cfg.post_record_s;
    if (post <= 0) post = NVR_DEF_POST_RECORD_S;

    /* 续录:进行中同类型事件且未达 10min 上限 → 沿用同 event_id、延长后录窗口(起点固定=首次触发)。 */
    if (c->evt_active && c->evt_rectype == rectype &&
        ((uint32_t)now - c->evt_start) < (uint32_t)NVR_DEF_EVENT_MAX_S) {
        c->evt_until = now + post;
        if (cloud_out) *cloud_out = c->evt_cloud;
        if (win_start_out) *win_start_out = c->evt_start;
        if (win_end_out)   *win_end_out   = (uint32_t)c->evt_until;
        return c->evt_id;
    }

    /* 需要开新事件(首个/换类型/达10min上限切割):先闭合进行中的旧事件(写真实结束=now)。 */
    if (c->evt_active && c->evt_id) {
        if (r->cfg.on_event_end)
            r->cfg.on_event_end(r->cfg.end_user, chn, c->evt_id, c->evt_start, (uint32_t)now);
        c->evt_active = 0; c->evt_id = 0; c->evt_cloud = 0;
    }

    /* 每个事件都铸造唯一 event_id(盘上事件槽由录像器建;不再写 meta.db)。 */
    uint64_t eid = rsdk_evtidx_make_event_id(chn, start_epoch, rectype, (uint16_t)(now & 0xFFFF));
    int cloud = 0;
#if RSDK_CFG_METADATA
    int cloud_stream = 1;
    int cloud_ok = r->cfg.settings &&
        nvr_cloud_ch_upload_stream(r->cfg.settings, chn, rectype_trigger(rectype), &cloud_stream);
    if (cloud_ok && !r->stopped) {
        int async = (r->cfg.group != NULL);
        cloud = 1;
        if (async && r->cfg.settings &&
            !nvr_cloud_async_upload_allowed(r->cfg.settings, start_epoch))
            cloud = 0;    /* 早于 async_upload_since:不上传(仅本地录像) */
        (void)cloud_stream;
        /* sync 模式(无盘组):即时开上传会话 */
        if (cloud && r->cfg.on_cloud_event)
            r->cfg.on_cloud_event(r->cfg.cloud_user, chn, eid, start_epoch, rectype);
    }
#endif
    c->evt_active = 1; c->evt_rectype = rectype; c->evt_id = eid; c->evt_cloud = (uint8_t)cloud;
    c->evt_start = start_epoch; c->evt_until = now + post;
    if (cloud_out) *cloud_out = cloud;
    if (win_start_out) *win_start_out = start_epoch;
    if (win_end_out)   *win_end_out   = (uint32_t)c->evt_until;
    NVR_LOGI("rec", "ch%d 事件录像 rectype=%d event_id=%llu post=%ds%s", chn, rectype,
             (unsigned long long)eid, post, cloud ? " (云存待传)" : "");
    return eid;
}

void nvr_rec_on_storage_evt(nvr_rec_sched_t *r, nvr_stg_evt_t e)
{
    if (!r) return;
    if (e == NVR_STG_EVT_FULL && r->cfg.hdd_full_policy == RSDK_HDDFULL_STOP) {
        r->stopped = 1;
        printf("[rec] 盘满(STOP 策略): 录像停止（连续录像应由 app 关 streaming record）\n");
    }
}

void nvr_rec_tick(nvr_rec_sched_t *r)
{
    if (!r) return;
    time_t now = time(NULL);
    for (int i = 0; i < REC_MAX_CH; i++) {
        rec_ch_t *c = &r->ch[i];
        /* 后录到期,或达 10min 上限 → 闭合事件(带真实结束墙钟)。 */
        int expired = (uint32_t)now >= (uint32_t)c->evt_until;
        int capped   = ((uint32_t)now - c->evt_start) >= (uint32_t)NVR_DEF_EVENT_MAX_S;
        if (c->evt_active && (expired || capped)) {
            uint64_t eid = c->evt_id;
            uint32_t start = c->evt_start;
            uint32_t end = capped ? (uint32_t)now : (uint32_t)c->evt_until;   /* 真实结束 */
            c->evt_active = 0; c->evt_id = 0; c->evt_cloud = 0;
            /* 事件槽 end_time 由 on_event_end→录像器 rsdk_rec_end_event 写真实值(盘上权威)。 */
            if (eid && r->cfg.on_event_end)
                r->cfg.on_event_end(r->cfg.end_user, i, eid, start, end);
        }
    }
}

int nvr_rec_is_recording(nvr_rec_sched_t *r, int chn)
{
    rec_ch_t *c = ch_of(r, chn);
    if (!c || !c->in_use) return 0;
    return (r->cfg.group != NULL) && !r->stopped;   /* 连续录像由 streaming 保证 */
}

int nvr_rec_stopped(nvr_rec_sched_t *r) { return r ? r->stopped : 0; }
