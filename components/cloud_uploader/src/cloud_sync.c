/***************************************************************************************
 *  cloud_sync.c — 云存同步上传(无盘): 事件期间从拉流旁路取帧 → TS 分片实时 POST。
 *  事件结束调用 vsaas_update_tags 上报 duration(文档 sync 模式要求)。
 ***************************************************************************************/
#include "cloud_sync.h"
#include "uploader_internal.h"
#include "http_vsaas.h"
#include "ts_mux.h"
#include "rsdk_cloud.h"
#include "nvr_log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SYNC_MAX_SESS   8
#define SYNC_FRAME_Q    96
#define SYNC_FRAME_MAX  (256u * 1024u)

typedef struct sync_sess sync_sess_t;

static struct nvr_cloud_uploader *g_sync_up;

static int event_id_code(uint32_t rectype)
{
    switch (rectype) {
        case RSDK_REC_DOORBELL: return 30312;
        case RSDK_REC_FACE:     return 30303;
        case RSDK_REC_HUMAN:    return 30302;
        case RSDK_REC_VEHICLE:  return 30305;
        default:                return 1;
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

static sync_sess_t *sess_by_eid(struct nvr_cloud_uploader *up, uint64_t eid)
{
    for (int i = 0; i < SYNC_MAX_SESS; i++)
        if (up->sync[i].active && up->sync[i].event_id == eid) return &up->sync[i];
    return NULL;
}

static sync_sess_t *sess_alloc(struct nvr_cloud_uploader *up)
{
    for (int i = 0; i < SYNC_MAX_SESS; i++)
        if (!up->sync[i].active) return &up->sync[i];
    return NULL;
}

static void fq_clear(sync_sess_t *s)
{
    while (s->fq_n > 0) {
        sync_frame_t *f = &s->fq[s->fq_head];
        free(f->data);
        f->data = NULL;
        s->fq_head = (s->fq_head + 1) % SYNC_FRAME_Q;
        s->fq_n--;
    }
}

static void sess_free(sync_sess_t *s)
{
    if (!s) return;
    fq_clear(s);
    if (s->mux) { ts_mux_free(s->mux); s->mux = NULL; }
    memset(s, 0, sizeof(*s));
}

static int fq_push(sync_sess_t *s, const uint8_t *data, int len, uint64_t pts90k,
                   int codec, int is_key)
{
    if (!data || len <= 0 || len > (int)SYNC_FRAME_MAX) return -1;
    if (s->fq_n >= SYNC_FRAME_Q) {
        sync_frame_t *drop = &s->fq[s->fq_head];
        if (!drop->is_key) {
            free(drop->data);
            s->fq_head = (s->fq_head + 1) % SYNC_FRAME_Q;
            s->fq_n--;
        } else return -1;
    }
    sync_frame_t *f = &s->fq[s->fq_tail];
    f->data = (uint8_t *)malloc((size_t)len);
    if (!f->data) return -1;
    memcpy(f->data, data, (size_t)len);
    f->len = len;
    f->pts90k = pts90k;
    f->codec = codec;
    f->is_key = is_key;
    s->fq_tail = (s->fq_tail + 1) % SYNC_FRAME_Q;
    s->fq_n++;
    return 0;
}

static int fq_pop(sync_sess_t *s, sync_frame_t *out)
{
    if (s->fq_n <= 0) return 0;
    *out = s->fq[s->fq_head];
    s->fq[s->fq_head].data = NULL;
    s->fq_head = (s->fq_head + 1) % SYNC_FRAME_Q;
    s->fq_n--;
    return 1;
}

static int post_slice(struct nvr_cloud_uploader *up, sync_sess_t *s, int event_end,
                      uint64_t cur_pts)
{
    size_t tslen = 0;
    (void)ts_mux_data(s->mux, &tslen);
    if (tslen == 0) return 0;

    uint64_t fdur_ms = 0;
    if (s->have_first && cur_pts >= s->slice_base_pts)
        fdur_ms = (cur_pts - s->slice_base_pts) / 90;
    if (fdur_ms == 0) fdur_ms = (uint64_t)up->cfg.slice_ms;
    uint64_t fstart_ms = s->base_ms + (s->slice_base_pts - s->first_pts) / 90;

    const uint8_t *ts;
    size_t tl;
    ts = ts_mux_data(s->mux, &tl);
    if (vsaas_post_ts(s->url, ts, tl, fstart_ms, (uint32_t)fdur_ms, event_end) != 0)
        return -1;
    ts_mux_reset(s->mux);
    s->slice_base_pts = cur_pts;
    return 0;
}

static int finish_sess(struct nvr_cloud_uploader *up, sync_sess_t *s, const char *stoken)
{
#if RSDK_CFG_METADATA
    uint32_t dur;
    if (s->end_epoch > s->starttime)
        dur = s->end_epoch - s->starttime;
    else if (s->have_first && s->last_pts > s->first_pts)
        dur = (uint32_t)((s->last_pts - s->first_pts) / 90000);
    else
        dur = 1;
    if (dur > 300) dur = 300;

    char tags[160];
    snprintf(tags, sizeof(tags), "%s,starttimeOrg%u,duration%u", s->tags, s->orig_last, dur);
    if (vsaas_update_tags(up->cfg.stage, up->cfg.udid, stoken, s->st_embed, tags) != 0)
        NVR_LOGW("cloud", "ch%d sync update_tags 失败 eid=%llu", s->chn,
                 (unsigned long long)s->event_id);

    rsdk_cloud_set_state(up->cfg.meta, s->event_id, RSDK_CLOUD_DONE, 0);
    NVR_LOGI("cloud", "ch%d sync 上传完成 eid=%llu dur=%us", s->chn,
             (unsigned long long)s->event_id, dur);
#else
    (void)up; (void)s; (void)stoken;
#endif
    sess_free(s);
    return 0;
}

static int process_sess(struct nvr_cloud_uploader *up, sync_sess_t *s, const char *stoken)
{
    sync_frame_t f;
    int rc = 0;
    while (fq_pop(s, &f)) {
        if (!s->have_first) {
            s->have_first = 1;
            s->first_pts = f.pts90k;
            s->slice_base_pts = f.pts90k;
            s->h265 = (f.codec == 1);
            if (!s->mux) s->mux = ts_mux_new(s->h265);
        }
        s->last_pts = f.pts90k;
        ts_mux_write_video(s->mux, f.data, (size_t)f.len, f.pts90k, f.is_key);
        free(f.data);

        uint64_t cur_ms = (f.pts90k - s->slice_base_pts) / 90;
        if (cur_ms >= (uint64_t)up->cfg.slice_ms) {
            if (post_slice(up, s, 0, f.pts90k) != 0) { rc = -1; break; }
        }
    }

    if (rc == 0 && s->ending && s->fq_n == 0) {
        if (s->mux) {
            size_t tl = 0;
            ts_mux_data(s->mux, &tl);
            if (tl > 0 && post_slice(up, s, 1, s->last_pts ? s->last_pts : s->slice_base_pts) != 0)
                rc = -1;
        }
        if (rc == 0)
            finish_sess(up, s, stoken);
        else {
#if RSDK_CFG_METADATA
            rsdk_cloud_set_state(up->cfg.meta, s->event_id, RSDK_CLOUD_RETRY, -5);
#endif
            sess_free(s);
        }
    } else if (rc != 0) {
#if RSDK_CFG_METADATA
        rsdk_cloud_set_state(up->cfg.meta, s->event_id, RSDK_CLOUD_RETRY, -5);
#endif
        sess_free(s);
    }
    return rc;
}

void cloud_sync_attach(struct nvr_cloud_uploader *up) { g_sync_up = up; }
void cloud_sync_detach(void) { g_sync_up = NULL; }

int cloud_sync_mode(const struct nvr_cloud_uploader *up)
{
    return up && !up->cfg.group;
}

int cloud_sync_event_begin(struct nvr_cloud_uploader *up, int chn, uint64_t eid,
                           uint32_t starttime, uint32_t rectype, int rec_stream,
                           const char *stoken)
{
    if (!up || !eid || !stoken || !stoken[0] || up->cfg.group) return -1;

    pthread_mutex_lock(&up->lk);
    if (sess_by_eid(up, eid)) { pthread_mutex_unlock(&up->lk); return 0; }
    sync_sess_t *s = sess_alloc(up);
    if (!s) { pthread_mutex_unlock(&up->lk); return -1; }

    memset(s, 0, sizeof(*s));
    s->active = 1;
    s->event_id = eid;
    s->chn = chn;
    s->stream = rec_stream;
    s->starttime = starttime;
    s->rectype = rectype;
    s->orig_last = starttime % 10;
    s->st_embed = starttime - s->orig_last + (uint32_t)(chn % 10);
    s->base_ms = (uint64_t)s->st_embed * 1000;
    snprintf(s->tags, sizeof(s->tags), "%s,starttimeOrg%u",
             rectype_tag(rectype), s->orig_last);

    vsaas_url_t vu;
    if (vsaas_get_url(up->cfg.stage, up->cfg.udid, stoken, s->st_embed,
                      event_id_code(rectype), s->tags, &vu) != 0 ||
        !vu.http_ok) {
        NVR_LOGW("cloud", "ch%d sync GET url 失败", chn);
#if RSDK_CFG_METADATA
        rsdk_cloud_set_state(up->cfg.meta, eid, RSDK_CLOUD_RETRY, -3);
#endif
        sess_free(s);
        pthread_mutex_unlock(&up->lk);
        return -1;
    }
    if (vu.err_code == -1002 || vu.err_code == -1003 || vu.err_code == -1004) {
        nvr_cloud_uploader_force_off(up, vu.err_code);
        sess_free(s);
        pthread_mutex_unlock(&up->lk);
        return -1;
    }
    snprintf(s->url, sizeof(s->url), "%s", vu.url);
    s->url_ok = 1;
#if RSDK_CFG_METADATA
    rsdk_cloud_set_state(up->cfg.meta, eid, RSDK_CLOUD_UPLOADING, 0);
#endif
    NVR_LOGI("cloud", "ch%d sync 事件开始 eid=%llu stream=%d", chn,
             (unsigned long long)eid, rec_stream);
    pthread_mutex_unlock(&up->lk);
    return 0;
}

int cloud_sync_event_end(struct nvr_cloud_uploader *up, uint64_t eid, uint32_t end_epoch,
                         const char *stoken)
{
    (void)stoken;
    if (!up || !eid || up->cfg.group) return -1;
    pthread_mutex_lock(&up->lk);
    sync_sess_t *s = sess_by_eid(up, eid);
    if (s) { s->ending = 1; s->end_epoch = end_epoch; }
    pthread_mutex_unlock(&up->lk);
    return s ? 0 : -1;
}

void cloud_sync_drain(struct nvr_cloud_uploader *up, const char *stoken)
{
    if (!up || !stoken || !stoken[0] || up->cfg.group) return;
    pthread_mutex_lock(&up->lk);
    for (int i = 0; i < SYNC_MAX_SESS; i++)
        if (up->sync[i].active)
            process_sess(up, &up->sync[i], stoken);
    pthread_mutex_unlock(&up->lk);
}

void nvr_cloud_sync_feed(int chn, int stream, const uint8_t *data, int len,
                         int codec, int is_key, uint32_t ts_ms)
{
    struct nvr_cloud_uploader *up = g_sync_up;
    if (!up || !data || len <= 0 || up->cfg.group) return;

    pthread_mutex_lock(&up->lk);
    int on = up->sw_on && up->stoken[0];
    if (!on) { pthread_mutex_unlock(&up->lk); return; }

    uint64_t pts90k = (uint64_t)ts_ms * 90;
    for (int i = 0; i < SYNC_MAX_SESS; i++) {
        sync_sess_t *s = &up->sync[i];
        if (!s->active || s->ending) continue;
        if (s->chn != chn || s->stream != stream) continue;
        (void)fq_push(s, data, len, pts90k, codec, is_key);
    }
    pthread_mutex_unlock(&up->lk);
}
