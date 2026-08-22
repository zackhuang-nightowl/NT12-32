/***************************************************************************************
 *  uploader.c — 云存异步上传引擎。见 nvr_cloud_uploader.h / 计划 §B6 / docs cloudRec。
 *  轮询 rsdk_cloud 待传 → 取事件段 → 读帧 → TS 分片 → GET url → POST → 回写状态。
 ***************************************************************************************/
#include "nvr_cloud_uploader.h"
#include "nvr_record_policy.h"
#include "cloud_sync.h"
#include "uploader_internal.h"
#include "http_vsaas.h"
#include "ts_mux.h"
#include "rsdk_cloud.h"   /* 云存工作队列(meta.db,可丢) */
#include "rsdk_evtidx.h"  /* 云存态盘上权威=事件索引槽(查询源/可扫盘重建) */
#include "rsdk_balance.h" /* rsdk_group_count/dev */
#include "nvr_log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_PENDING 32

/* rectype → 云存 event_id 数字码（优先级见文档） */
static int event_id_code(uint32_t rectype)
{
    switch (rectype) {
        case RSDK_REC_DOORBELL: return 30312;
        case RSDK_REC_FACE:     return 30303;
        case RSDK_REC_HUMAN:    return 30302;
        case RSDK_REC_VEHICLE:  return 30305;
        default:                return 1;   /* pixelChange/motion */
    }
}
static const char *rectype_tag(uint32_t rectype)
{
    switch (rectype) {
        case RSDK_REC_DOORBELL: return "doorbellRing";
        case RSDK_REC_FACE:     return "face";
        case RSDK_REC_HUMAN:    return "human";
        case RSDK_REC_VEHICLE:  return "vehicle";
        default:                return "pixelChange";
    }
}

/* 置云存态:写工作队列(meta.db,可丢)+ 镜像到事件索引槽(盘上权威/查询源/可重建)。 */
static void cloud_set(nvr_cloud_uploader_t *up, uint64_t eid, int state, int32_t err)
{
#if RSDK_CFG_METADATA
    rsdk_cloud_set_state(up->cfg.meta, eid, (rsdk_cloud_state_t)state, err);
    if (up->cfg.group) {
        uint32_t now = (uint32_t)time(NULL);
        for (int di = 0; di < rsdk_group_count(up->cfg.group); di++) {
            rsdk_dev_t *d = rsdk_group_dev(up->cfg.group, di);
            if (d && rsdk_evtidx_patch_state(d, eid, state, err, now) == RSDK_OK) break;
        }
    }
#else
    (void)up; (void)eid; (void)state; (void)err;
#endif
}

/* 处理一个待传事件：返回 0 成功(DONE)，<0 失败(RETRY)，-1000=强制关 */
static int process_event(nvr_cloud_uploader_t *up, const rsdk_cloud_event_t *ev, const char *stoken)
{
#if RSDK_CFG_METADATA
    cloud_set(up, ev->event_id, RSDK_CLOUD_UPLOADING, 0);

    int rec_stream = 1;
    if (!up->cfg.settings ||
        !nvr_cloud_ch_upload_stream(up->cfg.settings, ev->chn, rectype_tag(ev->rectype), &rec_stream)) {
        NVR_LOGI("cloud", "ch%d 事件跳过上传(云存未开/未启用/trigger/stream)", ev->chn);
        cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -10);
        return -1;
    }

    if (up->cfg.group && !nvr_cloud_async_upload_allowed(up->cfg.settings, ev->starttime)) {
        NVR_LOGI("cloud", "ch%d 事件跳过(早于 async_upload_since) start=%u", ev->chn, ev->starttime);
        cloud_set(up, ev->event_id, RSDK_CLOUD_NONE, 0);
        return -1;
    }

    /* 1) 定位事件段（连续轨按时窗；按 cloud_channel.stream_type 取主/子轨） */
    uint32_t t0 = ev->starttime;
    uint32_t t1 = ev->starttime + 300;            /* 上限一个合约窗口 */
    rsdk_index_slot_t segs[64];
    int nseg = rsdk_group_query_stream(up->cfg.group, t0, t1, ev->chn,
                                       RSDK_REC_CONTINUOUS, rec_stream, segs, 64);
    if (nseg <= 0) { cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -1); return -1; }

    rsdk_group_player_t *pl = NULL;
    if (rsdk_group_play_open(up->cfg.group, segs, nseg, &pl) != RSDK_OK || !pl) {
        cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -2); return -1;
    }

    /* 2) 多通道 starttime 埋通道 + tags */
    uint32_t orig_last = ev->starttime % 10;
    uint32_t st_embed  = ev->starttime - orig_last + (uint32_t)(ev->chn % 10);
    char tags[128];
    snprintf(tags, sizeof(tags), "%s,starttimeOrg%u", rectype_tag(ev->rectype), orig_last);

    /* 3) GET upload url（事件内复用） */
    NVR_LOGI("cloud", "上传事件 ch%d rectype=%u starttime=%u tags=%s", ev->chn, ev->rectype,
             ev->starttime, tags);
    vsaas_url_t vu;
    if (vsaas_get_url(up->cfg.stage, up->cfg.udid, stoken, st_embed,
                      event_id_code(ev->rectype), tags, &vu) != 0) {
        NVR_LOGW("cloud", "ch%d GET url 失败(网络?)", ev->chn);
        rsdk_group_play_close(pl);
        cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -3); return -1;
    }
    if (vu.err_code == -1002 || vu.err_code == -1003 || vu.err_code == -1004) {
        NVR_LOGE("cloud", "服务器拒绝 code=%d(设备/绑定/合约无效) → 关云存", vu.err_code);
        rsdk_group_play_close(pl);
        cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, vu.err_code);
        return -1000;   /* 上层 force_off */
    }
    if (!vu.http_ok) {
        rsdk_group_play_close(pl);
        cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -4); return -1;
    }

    /* 4) 读帧 → TS 分片 → POST（按 slice_ms 切片；末片 event_end=1） */
    int h265 = 0;   /* 首帧 codec 决定；简化默认 h264，实际按 hdr.codec 设 */
    ts_mux_t *mux = ts_mux_new(h265);
    uint64_t slice_base_pts = 0, first_pts = 0;
    int have_first = 0, slice_idx = 0, rc_all = 0;
    uint64_t base_ms = (uint64_t)st_embed * 1000;   /* filename 用埋通道后的时间为基 */

    rsdk_frame_hdr_t hdr;
    const uint8_t *data; uint32_t len; int disk;
    int eof = 0;
    while (!eof) {
        rsdk_err_t r = rsdk_group_play_next(pl, &hdr, &data, &len, &disk);
        if (r != RSDK_OK) { eof = 1; }
        else {
            if (!have_first) { have_first = 1; first_pts = hdr.pts; slice_base_pts = hdr.pts; h265 = (hdr.codec == 1); }
            int is_key = (hdr.frame_type == 0);   /* 约定 0=I */
            ts_mux_write_video(mux, data, len, hdr.pts, is_key);
        }

        /* 切片条件：累计时长 >= slice_ms（用 pts 差，90kHz），或 EOF */
        uint64_t cur_ms = have_first ? (hdr.pts - slice_base_pts) / 90 : 0;
        int slice_due = (cur_ms >= (uint64_t)up->cfg.slice_ms) || eof;
        size_t tslen = 0; ts_mux_data(mux, &tslen);
        if (slice_due && tslen > 0) {
            uint64_t fstart_ms = base_ms + (slice_base_pts - first_pts) / 90;
            uint32_t fdur_ms   = (uint32_t)((have_first ? (hdr.pts - slice_base_pts) : 0) / 90);
            if (fdur_ms == 0) fdur_ms = up->cfg.slice_ms;
            const uint8_t *ts; size_t tl; ts = ts_mux_data(mux, &tl);
            if (vsaas_post_ts(vu.url, ts, tl, fstart_ms, fdur_ms, eof ? 1 : 0) != 0) rc_all = -1;
            ts_mux_reset(mux);
            slice_base_pts = hdr.pts;
            slice_idx++;
        }
        if (eof) break;
    }

    ts_mux_free(mux);
    rsdk_group_play_close(pl);

    if (rc_all == 0) {
        NVR_LOGI("cloud", "ch%d 上传完成 (%d 切片)", ev->chn, slice_idx);
        cloud_set(up, ev->event_id, RSDK_CLOUD_DONE, 0); return 0;
    }
    NVR_LOGW("cloud", "ch%d 上传失败(切片 POST)", ev->chn);
    cloud_set(up, ev->event_id, RSDK_CLOUD_RETRY, -5);
    return -1;
#else
    (void)up; (void)ev; (void)stoken; return -1;
#endif
}

static void *worker_main(void *arg)
{
    nvr_cloud_uploader_t *up = arg;
    while (up->running) {
        pthread_mutex_lock(&up->lk);
        int on = up->sw_on;
        char stoken[256]; snprintf(stoken, sizeof(stoken), "%s", up->stoken);
        pthread_mutex_unlock(&up->lk);

        if (on && stoken[0]) {
            if (cloud_sync_mode(up))
                cloud_sync_drain(up, stoken);
#if RSDK_CFG_METADATA
            else if (up->cfg.group) {
                rsdk_cloud_event_t pend[MAX_PENDING];
                rsdk_cloud_poll_opt_t opt = { .include_failed = 1, .stale_uploading_s = 300, .chn = -1 };
                int n = rsdk_cloud_enumerate_pending(up->cfg.meta, &opt, pend, MAX_PENDING);
                if (n > 0) NVR_LOGI("cloud", "待上传队列 %d 条", n);
                for (int i = 0; i < n && up->running; i++) {
                    int rc = process_event(up, &pend[i], stoken);
                    if (rc == -1000) {                       /* 服务器拒绝 → 强制关 */
                        nvr_cloud_uploader_force_off(up, pend[i].last_err);
                        break;
                    }
                }
                /* 二次:从事件索引槽(盘上权威)补捞 PENDING/RETRY,覆盖 meta.db 丢失+扫盘重建后的事件
                 * (重建把"上传中"→"待重试")。按 event_id 去重,已在上面处理过的跳过。 */
                rsdk_evt_slot_t *ss = (rsdk_evt_slot_t *)calloc(256, sizeof(rsdk_evt_slot_t));
                if (ss) {
                    for (int di = 0; di < rsdk_group_count(up->cfg.group) && up->running; di++) {
                        rsdk_dev_t *d = rsdk_group_dev(up->cfg.group, di);
                        if (!d) continue;
                        int m = rsdk_evtidx_query(d, 0, 0xFFFFFFFFu, -1, -1, ss, 256);
                        for (int i = 0; i < m && up->running; i++) {
                            if (ss[i].state != RSDK_CLOUD_PENDING && ss[i].state != RSDK_CLOUD_RETRY) continue;
                            int seen = 0;
                            for (int k = 0; k < n; k++)
                                if (pend[k].event_id == ss[i].event_id) { seen = 1; break; }
                            if (seen) continue;
                            rsdk_cloud_event_t ev; memset(&ev, 0, sizeof(ev));
                            ev.event_id = ss[i].event_id; ev.chn = ss[i].chn;
                            ev.rectype = ss[i].rectype; ev.starttime = ss[i].start_time;
                            ev.state = ss[i].state;
                            if (process_event(up, &ev, stoken) == -1000) {
                                nvr_cloud_uploader_force_off(up, ev.last_err);
                                di = rsdk_group_count(up->cfg.group);   /* 退出外层 */
                                break;
                            }
                        }
                    }
                    free(ss);
                }
            }
#endif
        }

        int wait_s = cloud_sync_mode(up) ? 1 : up->cfg.poll_interval_s;
        for (int s = 0; s < wait_s && up->running; s++) sleep(1);
    }
    return NULL;
}

int nvr_cloud_uploader_start(const nvr_cloud_uploader_cfg_t *cfg, nvr_cloud_uploader_t **out)
{
    if (!cfg || !cfg->meta || !cfg->udid || !out) return -1;
    nvr_cloud_uploader_t *up = calloc(1, sizeof(*up));
    if (!up) return -1;
    up->cfg = *cfg;
    if (up->cfg.poll_interval_s <= 0) up->cfg.poll_interval_s = NVR_CLOUD_DEF_POLL_S;
    if (up->cfg.slice_ms <= 0)        up->cfg.slice_ms = NVR_CLOUD_DEF_SLICE_MS;
    if (up->cfg.worker_count <= 0)    up->cfg.worker_count = 1;
    if (cfg->stoken) snprintf(up->stoken, sizeof(up->stoken), "%s", cfg->stoken);
    pthread_mutex_init(&up->lk, NULL);
    vsaas_http_init();
    up->running = 1;
    if (pthread_create(&up->thr, NULL, worker_main, up) != 0) {
        up->running = 0; pthread_mutex_destroy(&up->lk); vsaas_http_cleanup(); free(up); return -1;
    }
    cloud_sync_attach(up);
    *out = up;
    return 0;
}

void nvr_cloud_uploader_stop(nvr_cloud_uploader_t *up)
{
    if (!up) return;
    up->running = 0;
    pthread_join(up->thr, NULL);
    cloud_sync_detach();
    pthread_mutex_destroy(&up->lk);
    vsaas_http_cleanup();
    free(up);
}

void nvr_cloud_uploader_set_group(nvr_cloud_uploader_t *up, rsdk_group_t *group)
{
    if (!up) return;
    pthread_mutex_lock(&up->lk);
    up->cfg.group = group;
    pthread_mutex_unlock(&up->lk);
}

int nvr_cloud_uploader_sync_mode(const nvr_cloud_uploader_t *up)
{
    return cloud_sync_mode(up);
}

int nvr_cloud_sync_event_begin(nvr_cloud_uploader_t *up, int chn, uint64_t eid,
                               uint32_t starttime, uint32_t rectype, int rec_stream)
{
    if (!up) return -1;
    char stoken[256];
    pthread_mutex_lock(&up->lk);
    snprintf(stoken, sizeof(stoken), "%s", up->stoken);
    pthread_mutex_unlock(&up->lk);
    return cloud_sync_event_begin(up, chn, eid, starttime, rectype, rec_stream, stoken);
}

int nvr_cloud_sync_event_end(nvr_cloud_uploader_t *up, uint64_t eid, uint32_t end_epoch)
{
    if (!up) return -1;
    char stoken[256];
    pthread_mutex_lock(&up->lk);
    snprintf(stoken, sizeof(stoken), "%s", up->stoken);
    pthread_mutex_unlock(&up->lk);
    return cloud_sync_event_end(up, eid, end_epoch, stoken);
}

void nvr_cloud_uploader_set_switch(nvr_cloud_uploader_t *up, int on)
{
    if (!up) return;
    pthread_mutex_lock(&up->lk); up->sw_on = on ? 1 : 0; pthread_mutex_unlock(&up->lk);
}
void nvr_cloud_uploader_set_stoken(nvr_cloud_uploader_t *up, const char *stoken)
{
    if (!up) return;
    pthread_mutex_lock(&up->lk);
    snprintf(up->stoken, sizeof(up->stoken), "%s", stoken ? stoken : "");
    pthread_mutex_unlock(&up->lk);
}
void nvr_cloud_uploader_force_off(nvr_cloud_uploader_t *up, int reason_code)
{
    if (!up) return;
    printf("[cloud] 服务器拒绝(code=%d): 强制关闭云存开关\n", reason_code);
    nvr_cloud_uploader_set_switch(up, 0);
    /* 上层应据此把设置库 cloud.switch 置 0，使 NOP get* 返回 false */
}
int nvr_cloud_uploader_online(nvr_cloud_uploader_t *up)
{
    if (!up) return 0;
    pthread_mutex_lock(&up->lk); int ok = up->sw_on && up->stoken[0]; pthread_mutex_unlock(&up->lk);
    return ok;
}
