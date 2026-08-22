/***************************************************************************************
 *  nvr_record_sched.c — 录像调度/事件与云存协调。见 nvr_record_sched.h / 计划 §B3。
 *  注意：连续录像由 ③streaming 负责；本模块只做事件/云存登记 + 满盘策略 + 状态。
 ***************************************************************************************/
#include "nvr_record_sched.h"
#include "nvr_record_policy.h"
#include "nvr_defaults.h"   /* 出厂默认(NVR_DEF_POST_RECORD_S 等) */
#include "rsdk_cloud.h"   /* 云存工作队列(meta.db,可丢) */
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

uint64_t nvr_rec_trigger_event(nvr_rec_sched_t *r, int chn, int rectype, uint32_t start_epoch, int post_s)
{
    rec_ch_t *c = ch_of(r, chn);
    if (!c || !c->in_use) return 0;
    time_t now = time(NULL);
    if (start_epoch == 0) start_epoch = (uint32_t)now;
    int post = (post_s > 0) ? post_s : r->cfg.post_record_s;
    if (post <= 0) post = 10;

    /* 进行中的同类型事件 → 仅延长后录窗口 */
    if (c->evt_active && c->evt_rectype == rectype) {
        c->evt_until = now + post;
        return c->evt_id;
    }

    uint64_t eid = 0;
#if RSDK_CFG_METADATA
    int cloud_stream = 1;
    int cloud_ok = r->cfg.settings &&
        nvr_cloud_ch_upload_stream(r->cfg.settings, chn, rectype_trigger(rectype), &cloud_stream);
    if (cloud_ok) {
        int async = (r->cfg.group != NULL);
        int reg = 1;
        if (async && r->cfg.settings &&
            !nvr_cloud_async_upload_allowed(r->cfg.settings, start_epoch))
            reg = 0;
        if (reg) {
            eid = rsdk_cloud_make_event_id(chn, start_epoch, rectype, (uint16_t)(now & 0xFFFF));
            /* 登记云存事件（PENDING）——连续录像轨按时窗，start_chunk 未知置 0，上传器按时窗取片 */
            if (r->cfg.meta && !r->stopped) {
                rsdk_cloud_event_t ev; memset(&ev, 0, sizeof(ev));
                ev.event_id = eid; ev.chn = chn; ev.rectype = (uint32_t)rectype;
                ev.starttime = start_epoch; ev.state = RSDK_CLOUD_PENDING;
                rsdk_cloud_event_begin(r->cfg.meta, &ev);
                if (r->cfg.on_cloud_event)
                    r->cfg.on_cloud_event(r->cfg.cloud_user, chn, eid, start_epoch, rectype);
            }
        }
    } else {
        (void)cloud_stream;
    }
#else
    (void)start_epoch;
#endif
    c->evt_active = 1; c->evt_rectype = rectype; c->evt_id = eid;
    c->evt_start = start_epoch; c->evt_until = now + post;
    NVR_LOGI("rec", "ch%d 事件录像 rectype=%d event_id=%llu post=%ds%s", chn, rectype,
             (unsigned long long)eid, post, (r->cfg.meta && eid) ? " (已登记云存)" : "");
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
        if (c->evt_active && now >= c->evt_until) {
            uint64_t eid = c->evt_id;
            uint32_t start = c->evt_start;
#if RSDK_CFG_METADATA
            if (eid && r->cfg.meta) {
                rsdk_cloud_event_t ev;
                if (rsdk_cloud_get(r->cfg.meta, eid, &ev) == RSDK_OK) {
                    ev.end_time = (uint32_t)now;
                    rsdk_cloud_event_begin(r->cfg.meta, &ev);
                }
            }
#endif
            c->evt_active = 0; c->evt_id = 0;
            if (eid && r->cfg.on_event_end)
                r->cfg.on_event_end(r->cfg.end_user, i, eid, start);
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
