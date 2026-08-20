/***************************************************************************************
 *  stream_router.c — 每路裸流 → {硬解上屏(仅 decode_stream 那路), 录像(主/子分 writer)}（纯 C）
 *
 *  双流:每通道主+子两路常拉,各自 stream_route_video(p,...)。
 *    · 录像:主→writer_main、子→writer_sub(独立段索引);音频挂主流。
 *    · 显示:单个硬件解码器,只由 decode_stream(单宫格=主/多宫格=子)那一路喂;show_win<0 不解码。
 *  ⚠️ 解码一律 NA51090 硬件 VPU(不软解)。
 ***************************************************************************************/
#include "stream_internal.h"
#include "stream_nal.h"
#include "stream_hub.h"
#include "mhal_vout.h"
#include "nvr_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 远程 RTSP(P2PTunnel 映射);强符号在 cloud_tutk/nvr_rtsp_live.c */
__attribute__((weak)) void nvr_rtsp_live_feed(int chn, int stream, const uint8_t *data, int len,
                                               int codec, int is_key, uint32_t ts_ms)
{
    (void)chn; (void)stream; (void)data; (void)len; (void)codec; (void)is_key; (void)ts_ms;
}
__attribute__((weak)) void nvr_rtsp_live_feed_audio(int chn, const uint8_t *data, int len, uint32_t ts_ms)
{
    (void)chn; (void)data; (void)len; (void)ts_ms;
}

static stream_pull_t *pull_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
}

static rsdk_writer_t *writer_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? c->writer_sub : c->writer_main;
}

/* ---- 事件预录环(主/子各一,挂在 stream_pull_t) -------------------------- */

static void stream_pre_clear(stream_pull_t *p)
{
    int i;
    if (!p || !p->pre_frames) { if (p) { p->pre_count = 0; p->pre_head = 0; } return; }
    for (i = 0; i < p->pre_cap; i++) {
        free(p->pre_frames[i].data);
        p->pre_frames[i].data = NULL;
        p->pre_frames[i].len  = 0;
    }
    p->pre_count = 0;
    p->pre_head  = 0;
}

static void stream_pre_free(stream_pull_t *p)
{
    if (!p) return;
    stream_pre_clear(p);
    free(p->pre_frames);
    p->pre_frames = NULL;
    p->pre_cap = 0;
    p->pre_flushed = 0;
}

static void stream_pre_free_chan(stream_chan_t *c)
{
    if (!c) return;
    stream_pre_free(&c->pmain);
    stream_pre_free(&c->psub);
}

static int stream_pre_ensure(stream_pull_t *p, int pre_s)
{
    int need;
    if (!p || pre_s <= 0) { stream_pre_free(p); return 0; }
    need = pre_s * NVR_PRE_FPS_ASSUME;
    if (need > NVR_PRE_FRAMES_MAX) need = NVR_PRE_FRAMES_MAX;
    if (need < 1) need = 1;
    if (p->pre_frames && p->pre_cap == need) return 0;
    stream_pre_free(p);
    p->pre_frames = (stream_pre_frame_t *)calloc((size_t)need, sizeof(stream_pre_frame_t));
    if (!p->pre_frames) return -1;
    p->pre_cap = need;
    return 0;
}

static void stream_pre_push(stream_pull_t *p, const uint8_t *data, int len,
                            uint32_t ts, int is_key, int codec, int frame_type)
{
    stream_pre_frame_t *slot;
    uint8_t *copy;
    if (!p || !p->pre_frames || p->pre_cap <= 0 || !data || len <= 0) return;
    copy = (uint8_t *)malloc((size_t)len);
    if (!copy) return;
    memcpy(copy, data, (size_t)len);
    slot = &p->pre_frames[p->pre_head];
    if (p->pre_count == p->pre_cap) free(slot->data);
    else p->pre_count++;
    slot->data = copy;
    slot->len = (uint32_t)len;
    slot->ts = ts;
    slot->is_key = is_key ? 1 : 0;
    slot->codec = (uint8_t)codec;
    slot->frame_type = (uint8_t)frame_type;
    p->pre_head = (p->pre_head + 1) % p->pre_cap;
}

/* 把本路预录环 flush 到对应 writer(从最早关键帧起)。 */
static void stream_pre_flush(stream_chan_t *c, stream_pull_t *p)
{
    rsdk_writer_t *w;
    int i, idx, oldest, started = 0;
    int *gated;
    if (!c || !p) return;
    w = writer_of(c, p->stream);
    gated = (p->stream == NVR_STREAM_SUB) ? &c->rec_gated_sub : &c->rec_gated_main;
    if (!w || !p->pre_frames || p->pre_count <= 0) {
        stream_pre_clear(p);
        return;
    }
    oldest = (p->pre_head - p->pre_count + p->pre_cap) % p->pre_cap;
    for (i = 0; i < p->pre_count; i++) {
        stream_pre_frame_t *b;
        rsdk_frame_t f;
        idx = (oldest + i) % p->pre_cap;
        b = &p->pre_frames[idx];
        if (!b->data || b->len == 0) continue;
        if (!started) {
            if (!b->is_key) continue;
            started = 1;
            p->rec_state = STREAM_REC_RECORDING;
            *gated = 1;
        }
        memset(&f, 0, sizeof(f));
        f.chn = (uint16_t)c->cfg.chn;
        f.stream = (uint8_t)p->stream;
        f.codec = b->codec;
        f.frame_type = b->frame_type;
        f.pts = (uint64_t)b->ts;
        f.wall_time = (uint64_t)time(NULL);
        f.data = b->data;
        f.len = b->len;
        rsdk_rec_write_frame(w, &f);
    }
    stream_pre_clear(p);
}

/* puller 线程:事件片段开始时补开 writer(owns writer,可 open)。 */
static void stream_event_writers_open(stream_chan_t *c)
{
    rsdk_group_t *grp;
    int rectype;
    if (!c) return;
    grp = c->grp;
    if (!grp) return;
    rectype = c->pend_event_rectype ? (int)c->pend_event_rectype : RSDK_REC_MOTION;
    if (!c->writer_main) {
        if (rsdk_rec_open_group_stream(grp, c->cfg.chn, rectype,
                                       NVR_STREAM_MAIN, &c->writer_main) != RSDK_OK)
            c->writer_main = NULL;
        else { c->rec_gated_main = 0; c->pmain.rec_state = STREAM_REC_WAIT_IDR; c->pmain.rec_last_gen = c->pmain.conn_gen; }
    }
    if (!c->writer_sub) {
        if (rsdk_rec_open_group_stream(grp, c->cfg.chn, rectype,
                                       NVR_STREAM_SUB, &c->writer_sub) != RSDK_OK)
            c->writer_sub = NULL;
        else { c->rec_gated_sub = 0; c->psub.rec_state = STREAM_REC_WAIT_IDR; c->psub.rec_last_gen = c->psub.conn_gen; }
    }
    stream_rec_mask_poke(c);
}

static void stream_apply_event_tags(stream_chan_t *c)
{
    uint64_t pend = c->pend_event_id;
    if (pend != c->applied_event_id) {
        if (c->writer_main) {
            rsdk_rec_set_event(c->writer_main, pend);
            if (pend)
                rsdk_rec_mark_event(c->writer_main, pend, c->pend_event_rectype,
                                    c->pend_event_start, c->pend_event_end,
                                    (uint32_t)(1u << (c->pend_event_rectype & 31)), 0);
        }
        if (c->writer_sub) rsdk_rec_set_event(c->writer_sub, pend);
        c->applied_event_id = pend;
    } else if (c->applied_event_id && c->pend_event_end &&
               (uint32_t)time(NULL) > c->pend_event_end) {
        if (c->writer_main) rsdk_rec_set_event(c->writer_main, 0);
        if (c->writer_sub)  rsdk_rec_set_event(c->writer_sub, 0);
        c->applied_event_id = 0; c->pend_event_id = 0;
        if (c->event_clip) {
            /* 事件窗结束:停写并关 writer,回到预录待命 */
            stream_close_writer(c);
            c->event_clip = 0;
            c->pmain.pre_flushed = 0;
            c->psub.pre_flushed = 0;
            stream_rec_mask_poke(c);
        }
    }
}

static void stream_write_rec_frame(stream_chan_t *c, stream_pull_t *p,
                                   const uint8_t *data, int len, uint32_t ts,
                                   const nal_class_t *nc)
{
    rsdk_writer_t *wrec = writer_of(c, p->stream);
    rsdk_frame_t f;
    if (!wrec || !data || len <= 0 || !nc) return;

    if (p->rec_state == STREAM_REC_WAIT_IDR) {
        if (!nc->is_key) return;
        p->rec_state = STREAM_REC_RECORDING;
        if (p->stream == NVR_STREAM_SUB) c->rec_gated_sub = 1;
        else c->rec_gated_main = 1;
    }
    memset(&f, 0, sizeof(f));
    f.chn = (uint16_t)c->cfg.chn;
    f.stream = (uint8_t)p->stream;
    f.codec = (uint8_t)p->codec;
    f.frame_type = (uint8_t)nc->frame_type;
    f.pts = (uint64_t)ts;
    f.wall_time = (uint64_t)time(NULL);
    f.data = data;
    f.len = (uint32_t)len;
    rsdk_rec_write_frame(wrec, &f);
}

/* 录像索引 gap 标记(type_mask bit31 = 码流不连续/重连边界) */
#define STREAM_REC_GAP_TYPE_MASK  0x80000000u

static void stream_rec_mark_gap(stream_chan_t *c, stream_pull_t *p, const char *reason)
{
    rsdk_writer_t *wrec = writer_of(c, p->stream);
    if (!wrec) return;
    rsdk_rec_mark_event(wrec, 0, RSDK_REC_CONTINUOUS, (uint32_t)time(NULL), 0,
                          STREAM_REC_GAP_TYPE_MASK, 0);
    NVR_LOGW("record", "ch%d[%s] gap: %s gen=%u",
             c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
             reason ? reason : "disc", p->conn_gen);
}

/* 消费 RecordQueue → rsdk(同 puller 线程, owns writer) */
static void stream_record_drain(stream_chan_t *c, stream_pull_t *p, int budget)
{
    rsdk_writer_t *wrec;
    int n = 0;
    if (!c || !p) return;
    wrec = writer_of(c, p->stream);
    if (!wrec) {
        stream_record_q_flush(&p->rec_q);
        return;
    }
    if (p->rec_q.stall_logged && p->rec_q.count >= STREAM_RECORD_Q_HIGH_WM)
        NVR_LOGW("record", "ch%d[%s] RecordQueue 高水位 %d/%d (stall=%u)",
                 c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
                 p->rec_q.count, STREAM_RECORD_Q_CAP, p->rec_q.stall_cnt);

    while (p->rec_q.count > 0 && (budget <= 0 || n < budget)) {
        const stream_record_slot_t *s = stream_record_q_peek(&p->rec_q);
        nal_class_t nc;
        if (!s || !s->data || s->len == 0) { stream_record_q_pop(&p->rec_q); continue; }
        memset(&nc, 0, sizeof(nc));
        nc.is_key = s->is_key;
        nc.is_param = s->is_param;
        nc.frame_type = s->frame_type;
        stream_write_rec_frame(c, p, s->data, (int)s->len, s->ts, &nc);
        stream_record_q_pop(&p->rec_q);
        n++;
    }
}

static int stream_record_enqueue(stream_chan_t *c, stream_pull_t *p,
                                 const uint8_t *data, int len, uint32_t ts,
                                 const nal_class_t *nc)
{
    int rc;
    if (!data || len <= 0 || !nc || nc->is_param) return 0;
    rc = stream_record_q_push(&p->rec_q, data, (uint32_t)len, ts,
                              nc->is_key, nc->is_param, (uint8_t)nc->frame_type,
                              (uint8_t)p->codec);
    if (rc == 0) return 0;
    stream_record_drain(c, p, 0);
    rc = stream_record_q_push(&p->rec_q, data, (uint32_t)len, ts,
                              nc->is_key, nc->is_param, (uint8_t)nc->frame_type,
                              (uint8_t)p->codec);
    if (rc != 0)
        NVR_LOGE("record", "ch%d[%s] RecordQueue 满且 drain 后仍无法入队 len=%d",
                 c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主", len);
    return rc;
}

/* Recorder: 感知 conn_gen / disc(不丢帧, 只打 gap 标记) */
static void stream_rec_notify_disc(stream_chan_t *c, stream_pull_t *p)
{
    if (p->conn_gen != p->rec_last_gen) {
        if (p->rec_state == STREAM_REC_RECORDING)
            stream_rec_mark_gap(c, p, "generation_changed");
        p->rec_last_gen = p->conn_gen;
    }
    if (p->disc_mark) {
        if (p->rec_state == STREAM_REC_RECORDING)
            stream_rec_mark_gap(c, p, "continuity_disc");
        p->disc_mark = 0;
    }
}

/* 解码缓冲维度估算(供 max_mem 分配 + 预算准入)。按 stream 区分:子 720p / 主 8K 上限。
 * enc_w/h 已知(ONVIF profile 回填)则精确。理想应回填 enc_w/h(待补)。 */
static void resolve_dim(const nvr_stream_chan_cfg_t *cfg, int stream, int *w, int *h, int *fps)
{
    if (cfg->enc_w > 0 && cfg->enc_h > 0) { *w = cfg->enc_w; *h = cfg->enc_h; }
    else if (stream == NVR_STREAM_SUB)    { *w = 1280; *h = 720;  }   /* 子码流上限 720p */
    else                                  { *w = 7680; *h = 2176; }   /* 主码流上限 8K(7680×2160) */
    *fps = cfg->fps > 0 ? cfg->fps : 20;
}

/* 开解码器绑到窗口 win(>=0),用 **decode_stream 那一路** 的 codec/分辨率。解码=上屏。
 * 只对可见格通道调用;超解码预算则拒绝(录像照常)。 */
/* 供回放引擎:取某通道某码流的解码尺寸(main 录像回放用)。 */
void stream_chan_get_dim(stream_chan_t *c, int stream, int *w, int *h, int *fps)
{
    if (c) resolve_dim(&c->cfg, stream, w, h, fps);
}

void stream_decode_open(stream_chan_t *c, int win)
{
    if (!c || win < 0 || c->vdec) return;         /* 已在解码则不重复开 */
    stream_pull_t *p = pull_of(c, c->decode_stream);
    if (!p->connected || p->codec < 0) return;    /* 该路还没连上/codec 未定 → 等连上再开 */
    c->decode_denied = 0;
    mhal_codec_t mc = (p->codec == RSDK_CODEC_H265) ? MHAL_CODEC_H265 : MHAL_CODEC_H264;
    int w, h, fps; resolve_dim(&c->cfg, c->decode_stream, &w, &h, &fps);
    int rc = mhal_vdec_open(c->cfg.chn, mc, w, h, fps, win, &c->vdec);
    if (rc == MHAL_VDEC_EBUDGET) {
        c->vdec = NULL; c->decode_denied = 1;
        NVR_LOGE("stream", "chn%d 超出解码能力(预算超限) → 仅录像不预览", c->cfg.chn);
    } else if (rc != 0) {
        c->vdec = NULL;
    }
    c->fed_since_open = 0;
    stream_live_q_flush(&c->live_q);
    c->live_state = c->vdec ? STREAM_LIVE_WAIT_IDR : STREAM_LIVE_IDLE;
    c->live_gen = p->conn_gen;
    /* 解码重开也清本路 RTP/frame_num 连续态,避免拿关窗前的 seq/fn 误判缺口 */
    p->rtp_seq_valid = 0;
    p->h264_prev_fn = -1;
    p->disc_mark = 0;
    if (c->vdec) NVR_LOGI("stream", "chn%d ▶开解码 → 格%d (%s码流) state=WAIT_IDR",
                          c->cfg.chn, win, c->decode_stream == NVR_STREAM_SUB ? "子" : "主");
    /* ★ 喂缓存 bootstrap 秒出图(不置 SYNCED:与后续实时 P 不连续,须等实时完整关键 AU)。 */
    if (!mhal_vout_is_deferred()) stream_feed_keyframe(c);
}

/* 给解码器喂 decode_stream 那路的缓存关键帧(解码器须已 start)→ 不等下个 IDR、瞬时出图。 */
void stream_feed_keyframe(stream_chan_t *c)
{
    if (!c || !c->vdec || c->fed_since_open > 0) return;
    stream_pull_t *p = (c->decode_stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
    if (p->kf_len > 0) {
        if (mhal_vdec_send(c->vdec, p->kf, (uint32_t)p->kf_len, p->kf_ts) == 0) {
            c->fed_since_open++;
            NVR_LOGI("stream", "chn%d 喂 bootstrap 关键帧(%s)→ 即时出图(仍 WAIT_IDR)",
                     c->cfg.chn, c->decode_stream == NVR_STREAM_SUB ? "子" : "主");
        }
    }
}

/* 关解码器(隐藏)。只停解码,两路拉流+录像不动。 */
void stream_decode_close(stream_chan_t *c)
{
    if (!c || !c->vdec) return;
    mhal_vdec_t *v = c->vdec;
    c->vdec = NULL;
    c->live_state = STREAM_LIVE_IDLE;
    stream_live_q_flush(&c->live_q);
    mhal_vdec_close(v);
    c->decode_denied = 0;
    c->pmain.par_len = c->pmain.par_building = 0;
    c->psub.par_len  = c->psub.par_building  = 0;
    NVR_LOGI("stream", "chn%d ■关解码 (隐藏,拉流/录像照常)", c->cfg.chn);
}

/* Live:不连续 → RESYNC + 清空队列 + 注入 bootstrap(不等下一 IDR)。 */
static void stream_live_inject_bootstrap(stream_chan_t *c)
{
    stream_pull_t *p;
    if (!c || !c->vdec) return;
    p = pull_of(c, c->decode_stream);
    if (p->kf_len <= 0) return;
    if (mhal_vdec_send(c->vdec, p->kf, (uint32_t)p->kf_len, p->kf_ts) == 0)
        NVR_LOGI("live", "chn%d RESYNC 注入 bootstrap IDR len=%d", c->cfg.chn, p->kf_len);
}

static void stream_live_enter_resync(stream_chan_t *c, const char *why)
{
    if (!c) return;
    if (c->live_state == STREAM_LIVE_IDLE) return;
    if (c->live_state != STREAM_LIVE_RESYNC)
        NVR_LOGW("live", "chn%d %s → RESYNC", c->cfg.chn, why ? why : "disc");
    c->live_state = STREAM_LIVE_RESYNC;
    stream_live_q_flush(&c->live_q);
    stream_live_inject_bootstrap(c);
}

/* 从 LiveQueue 按状态机送 VDEC:WAIT_IDR/RESYNC 只收完整关键 AU,成功后 SYNCED 连续喂。 */
static void stream_live_drain(stream_chan_t *c)
{
    if (!c || !c->vdec) return;
    while (c->live_q.count > 0) {
        const stream_live_slot_t *s = stream_live_q_peek(&c->live_q);
        if (!s || !s->data || s->len == 0) { stream_live_q_pop(&c->live_q); continue; }

        if (c->live_state == STREAM_LIVE_WAIT_IDR || c->live_state == STREAM_LIVE_RESYNC) {
            if (!s->is_key) { stream_live_q_pop(&c->live_q); continue; }
            if (mhal_vdec_send(c->vdec, s->data, s->len, s->ts) != 0) break;
            c->live_state = STREAM_LIVE_SYNCED;
            if (c->fed_since_open < 1000000) c->fed_since_open++;
            NVR_LOGI("live", "chn%d 完整关键 AU 起播 → SYNCED (len=%u)", c->cfg.chn, (unsigned)s->len);
            stream_live_q_pop(&c->live_q);
            continue;
        }
        if (c->live_state == STREAM_LIVE_SYNCED) {
            if (mhal_vdec_send(c->vdec, s->data, s->len, s->ts) != 0) {
                stream_live_enter_resync(c, "vdec_send_fail");
                break;
            }
            if (c->fed_since_open < 1000000) c->fed_since_open++;
            stream_live_q_pop(&c->live_q);
            continue;
        }
        /* IDLE:不应入队 */
        stream_live_q_pop(&c->live_q);
    }
}

void stream_live_signal_resync(stream_chan_t *c, stream_pull_t *p, const char *why)
{
    if (!c || !p) return;
    if (p->stream != c->decode_stream) return;
    if (c->live_state == STREAM_LIVE_IDLE) return;
    c->live_gen = p->conn_gen;
    stream_live_enter_resync(c, why);
}

/* 只开录像 writer(主+子各一)。供运行时"格式化后重组装盘组"时对已连通道补开。幂等。 */
void stream_open_writer(stream_chan_t *c, rsdk_group_t *grp)
{
    if (!c || !c->cfg.record || !grp) return;
    if (!c->writer_main) {
        rsdk_err_t rc = rsdk_rec_open_group_stream(grp, c->cfg.chn, RSDK_REC_CONTINUOUS,
                                                   NVR_STREAM_MAIN, &c->writer_main);
        if (rc != RSDK_OK) { c->writer_main = NULL; NVR_LOGE("stream", "chn%d 补开主流 writer 失败 %d", c->cfg.chn, rc); }
        else { c->rec_gated_main = 0; c->pmain.rec_state = STREAM_REC_WAIT_IDR; c->pmain.rec_last_gen = c->pmain.conn_gen; }
    }
    if (!c->writer_sub) {
        rsdk_err_t rc = rsdk_rec_open_group_stream(grp, c->cfg.chn, RSDK_REC_CONTINUOUS,
                                                   NVR_STREAM_SUB, &c->writer_sub);
        if (rc != RSDK_OK) { c->writer_sub = NULL; NVR_LOGE("stream", "chn%d 补开子流 writer 失败 %d", c->cfg.chn, rc); }
        else { c->rec_gated_sub = 0; c->psub.rec_state = STREAM_REC_WAIT_IDR; c->psub.rec_last_gen = c->psub.conn_gen; }
    }
    if (c->writer_main || c->writer_sub)
        NVR_LOGI("stream", "chn%d 盘组就绪→补开录像 writer(主%s 子%s)", c->cfg.chn,
                 c->writer_main ? "开" : "无", c->writer_sub ? "开" : "无");
    stream_rec_mask_poke(c);
}

/* 运行时关录像 writer(录像开关关闭时立即停录,拉流/解码不动)。 */
void stream_close_writer(stream_chan_t *c)
{
    if (!c) return;
    if (!c->writer_main && !c->writer_sub) { c->rec_gated_main = 0; c->rec_gated_sub = 0; return; }
    stream_record_drain(c, &c->pmain, 0);
    stream_record_drain(c, &c->psub, 0);
    if (c->writer_main) { rsdk_rec_close(c->writer_main); c->writer_main = NULL; }
    if (c->writer_sub)  { rsdk_rec_close(c->writer_sub);  c->writer_sub  = NULL; }
    stream_record_q_flush(&c->pmain.rec_q);
    stream_record_q_flush(&c->psub.rec_q);
    c->pmain.rec_state = c->psub.rec_state = STREAM_REC_WAIT_IDR;
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    NVR_LOGI("stream", "chn%d 录像开关关→停录(关双 writer)", c->cfg.chn);
    stream_rec_mask_poke(c);
}

/* 开主/子双 writer。可见则同时开解码。 */
rsdk_err_t stream_router_open(stream_chan_t *c, rsdk_group_t *grp)
{
    if (c->cfg.record && grp) stream_open_writer(c, grp);
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    if (c->show_win >= 0) c->decode_dirty = 1;   /* 交由 puller 首帧在自己线程内开解码(统一,免竞态) */
    return RSDK_OK;
}

/* 一路码流来一帧。Frame Hub: CI → HUB → { BS旁路, RQ, LQ }。 */
void stream_route_video(stream_pull_t *p, const uint8_t *data, int len, uint32_t ts, uint16_t seq)
{
    int had_disc;
    uint64_t mono_ms;
    if (!p || !p->owner || !data || len <= 0) return;
    stream_chan_t *c = p->owner;

    /* ★ 兑现待处理的解码状态变更(show_win/decode_stream 变了)。只在**喂解码器那一路**的 puller
     * 线程里 open/close 解码器 → 与本函数下方 mhal_vdec_send 同线程,彻底消除跨线程 use-after-free。 */
    if (c->decode_dirty && p->stream == c->decode_stream) {
        c->decode_dirty = 0;
        if (c->vdec) stream_decode_close(c);
        if (c->show_win >= 0) stream_decode_open(c, c->show_win);
    }

    nal_class_t nc;
    nal_classify(data, len, p->codec, &nc);
    mono_ms = stream_hub_mono_ms();

    /* --- Continuity Inspect: 只 mark disc / 供 gen 比对, 不在此跳帧。 --- */
    if (!nc.is_param) {
        if (p->rtp_seq_valid) {
            uint16_t delta = (uint16_t)(seq - p->last_rtp_seq);
            int max_pkts = len / 700 + 3;
            if (nc.is_key) max_pkts = len / 700 + 16;
            else if (max_pkts < 2) max_pkts = 2;
            if (delta == 0 || delta > (uint16_t)max_pkts) {
                p->disc_mark = 1;
                p->rtp_gap_cnt++;
                if (p->rtp_gap_cnt <= 20 || (p->rtp_gap_cnt % 50) == 0)
                    NVR_LOGW("rtsp-gap", "ch%d[%s] RTP seq 异常 prev=%u cur=%u delta=%u len=%d (第%u次) → mark disc",
                             c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
                             (unsigned)p->last_rtp_seq, (unsigned)seq, (unsigned)delta,
                             len, p->rtp_gap_cnt);
            }
        }
        p->last_rtp_seq = seq;
        p->rtp_seq_valid = 1;

        if (p->codec != RSDK_CODEC_H265) {
            int prev_fn = p->h264_prev_fn;
            if (nal_h264_frame_num_gap(data, len, nc.is_key ? 1 : 0,
                                       &p->h264_log2_fn, &p->h264_prev_fn)) {
                p->disc_mark = 1;
                p->fn_gap_cnt++;
                if (p->fn_gap_cnt <= 20 || (p->fn_gap_cnt % 50) == 0)
                    NVR_LOGW("rtsp-gap", "ch%d[%s] frame_num 跳变 prev=%d now=%d (第%u次) → mark disc",
                             c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
                             prev_fn, p->h264_prev_fn, p->fn_gap_cnt);
            }
        }
    }

    if (p->stream == NVR_STREAM_MAIN && !nc.is_param && nc.is_key) {
        NVR_LOGI("router", "ch%d[主] IDR@f%u len=%d 距上个IDR=%u帧(GOP)",
                 c->cfg.chn, p->vframes, len, p->vframes > p->last_idr_f ? p->vframes - p->last_idr_f : 0);
        p->last_idr_f = p->vframes;
    }

    /* --- Bootstrap 旁路缓存(每路 channel+stream 独立): 参数集 + 完整 IDR AU。 --- */
    const uint8_t *sbuf = data; int slen = len; uint8_t *tmp = NULL;
    if (nc.is_param) {
        if (!p->par_building) { p->par_len = 0; p->par_building = 1; }
        if (p->par_len + len <= (int)sizeof(p->par)) {
            memcpy(p->par + p->par_len, data, len); p->par_len += len;
        }
    } else {
        p->par_building = 0;
        if (nc.is_key && !nc.has_param && p->par_len > 0) {
            tmp = (uint8_t *)malloc((size_t)(p->par_len + len));
            if (tmp) {
                memcpy(tmp, p->par, (size_t)p->par_len);
                memcpy(tmp + p->par_len, data, (size_t)len);
                sbuf = tmp; slen = p->par_len + len;
            }
        }
        if (nc.is_key && slen > 0) {
            if (p->kf_cap < slen) {
                uint8_t *nk = (uint8_t *)realloc(p->kf, (size_t)slen);
                if (nk) { p->kf = nk; p->kf_cap = slen; }
            }
            if (p->kf && p->kf_cap >= slen) {
                memcpy(p->kf, sbuf, (size_t)slen);
                p->kf_len = slen; p->kf_ts = ts;
            }
        }
    }

    /* --- HUB fanout: disc/gen → Recorder; RecordQueue / LiveQueue --- */
    had_disc = p->disc_mark;
    if (p->conn_gen != p->rec_last_gen || had_disc)
        stream_rec_notify_disc(c, p);

    /* --- Record 路径: RQ → drain → rsdk(不丢帧; WAIT_IDR→RECORDING) --- */
    if (c->cfg.record) {
        stream_apply_event_tags(c);
        if (!nc.is_param)
            (void)stream_record_enqueue(c, p, data, len, ts, &nc);
    } else if (c->event_arm) {
        if (!c->event_clip && p->stream == NVR_STREAM_MAIN &&
            (c->writer_main || c->writer_sub))
            stream_close_writer(c);
        if (c->event_clip) {
            if (p->stream == NVR_STREAM_MAIN) {
                if (!c->writer_main && !c->writer_sub) stream_event_writers_open(c);
                stream_apply_event_tags(c);
            }
            if (writer_of(c, p->stream)) {
                if (!p->pre_flushed) {
                    stream_pre_flush(c, p);
                    p->pre_flushed = 1;
                }
                if (!nc.is_param)
                    (void)stream_record_enqueue(c, p, data, len, ts, &nc);
            }
        } else if (!nc.is_param && c->pre_record_s > 0) {
            if (stream_pre_ensure(p, c->pre_record_s) == 0)
                stream_pre_push(p, data, len, ts, nc.is_key, p->codec, nc.frame_type);
        }
    } else if (!c->event_clip && (c->pmain.pre_frames || c->psub.pre_frames)) {
        stream_pre_free_chan(c);
    }

    /* --- Live 路径: LQ(双阈值) → SM → VDEC --- */
    if (c->vdec && p->stream == c->decode_stream && !nc.is_param) {
        if (p->conn_gen != c->live_gen) {
            c->live_gen = p->conn_gen;
            stream_live_enter_resync(c, "conn_gen");
        } else if (had_disc) {
            stream_live_enter_resync(c, "disc_mark");
        }
        if (c->live_state != STREAM_LIVE_IDLE) {
            int dropped = stream_live_q_push(&c->live_q, sbuf, (uint32_t)slen, ts, nc.is_key, mono_ms);
            if (dropped) {
                stream_live_enter_resync(c, "live_q_overflow");
                (void)stream_live_q_push(&c->live_q, sbuf, (uint32_t)slen, ts, nc.is_key, mono_ms);
            }
            stream_live_drain(c);
        }
    }

    stream_record_drain(c, p, 4);

    if (tmp) free(tmp);

    nvr_rtsp_live_feed(c->cfg.chn, p->stream, data, len, p->codec, nc.is_key, ts);
}

void stream_route_audio(stream_pull_t *p, const uint8_t *data, int len, uint32_t ts)
{
    if (!p || !p->owner || p->stream != NVR_STREAM_MAIN) return;
    stream_chan_t *c = p->owner;
    if (data && len > 0)
        nvr_rtsp_live_feed_audio(c->cfg.chn, data, len, ts);
    /* 音频录像:挂主流 writer,stream=2/codec=AAC。仅主路带音频。 */
    int writing = c->cfg.record || c->event_clip;
    if (!writing || !c->writer_main || !c->rec_gated_main || !data || len <= 0) return;
    rsdk_frame_t f;
    memset(&f, 0, sizeof(f));
    f.chn        = (uint16_t)c->cfg.chn;
    f.stream     = 2;
    f.codec      = RSDK_CODEC_AAC;
    f.frame_type = RSDK_FRAME_AUDIO;
    f.pts        = (uint64_t)ts;
    f.wall_time  = (uint64_t)time(NULL);
    f.data       = data;
    f.len        = (uint32_t)len;
    rsdk_rec_write_frame(c->writer_main, &f);
}

void stream_router_close(stream_chan_t *c)
{
    if (!c) return;
    stream_record_drain(c, &c->pmain, 0);
    stream_record_drain(c, &c->psub, 0);
    if (c->writer_main) { rsdk_rec_close(c->writer_main); c->writer_main = NULL; }
    if (c->writer_sub)  { rsdk_rec_close(c->writer_sub);  c->writer_sub  = NULL; }
    stream_record_q_flush(&c->pmain.rec_q);
    stream_record_q_flush(&c->psub.rec_q);
    if (c->vdec)   { mhal_vdec_close(c->vdec);   c->vdec = NULL; }
    c->live_state = STREAM_LIVE_IDLE;
    stream_live_q_flush(&c->live_q);
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    c->event_clip = 0;
    c->pmain.pre_flushed = 0;
    c->psub.pre_flushed = 0;
    stream_pre_free_chan(c);
    if (c->pmain.kf) { free(c->pmain.kf); c->pmain.kf = NULL; c->pmain.kf_len = c->pmain.kf_cap = 0; }
    if (c->psub.kf)  { free(c->psub.kf);  c->psub.kf  = NULL; c->psub.kf_len  = c->psub.kf_cap  = 0; }
}
