/***************************************************************************************
 *  nvr_playback.c — 本机录像回放引擎(见 nvr_playback.h)。
 *  墙钟主时钟 + 多路 feeder; >4X 只解 I; backward=I 帧倒放; 宫格音频 enable[];
 *  PlaybackMsgNotify 按设备实际能力上报。
 ***************************************************************************************/
#include "nvr_playback.h"
#include "nvr_streaming.h"
#include "stream_nal.h"
#include "nvr_preview.h"
#include "nvr_gui_config.h"
#include "mhal_vdec.h"
#include "mhal_vout.h"
#include "mhal_aout.h"
#include "mhal_budget.h"
#include "rsdk_balance.h"
#include "rsdk_types.h"
#include "nvr_log.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define PB_MAX_SEGS  512
#define PB_MAX_FEED  16
#define PB_MAX_CH    32
#define PB_IKEY_MAX  4096

typedef struct { uint32_t wall; uint64_t pts; } pb_ikey_t;

typedef struct pb_feeder_ctx {
    struct nvr_playback *pb;
    int          chn0;
    int          cell_idx;
    int          want_stream;
    mhal_vdec_t *vdec;
    int          thread_ok;
} pb_feeder_ctx_t;

struct nvr_playback {
    nvr_playback_cfg_t cfg;
    pthread_mutex_t    lock;
    pthread_t          th[PB_MAX_FEED];
    pb_feeder_ctx_t    fctx[PB_MAX_FEED];
    int                nth;
    volatile int       running;
    volatile int       paused;
    volatile int       chn0;
    volatile int       want_stream;
    volatile int       speed_milli;  /* 125=0.125X … 8000=8X; frame→frame_step */
    volatile int       frame_step;   /* 1=逐帧:显示一帧后自动 pause */
    volatile int       i_only;       /* >4X 或 backward */
    uint32_t           start_wall;
    uint32_t           day_end_wall; /* 起播日本地 23:59:59,到则停 */
    int                backward;
    char               status[16], speed[16], direction[16];
    char               notify[160];
    volatile uint32_t  cur_wall;
    int                disp_mode;
    int                ch_list[NVR_PB_MAX_CELLS];
    int                ch_count;
    int                audio_en[NVR_PB_MAX_CELLS];
    int                audio_n;
    long               play_base_ms;
    uint32_t           play_base_wall;
    long               pause_at_ms;
    volatile int       alive_feeders;
    volatile uint32_t  max_decoded_wall;
    volatile uint32_t  min_decoded_wall;   /* 倒放解码到的最早 wall(倒放时间轴取此,随倒放递减) */
    volatile int       clock_reanchored;
    int                dec_open;
    int                dec_chlist[NVR_PB_MAX_CELLS];
    int                dec_n;
};

static int pb_grid_side(int mode)
{
    if (mode <= 1) return 1;
    if (mode == 4)  return 2;
    if (mode == 9)  return 3;
    if (mode == 16) return 4;
    if (mode == 25) return 5;
    if (mode == 36) return 6;
    int n = 2;
    while (n < 6 && n * n < mode) n++;
    return n;
}
static int pb_cell_count(int mode)
{
    if (mode <= 0) return 1;
    return mode;
}
static void pb_cell_rect(struct nvr_playback *pb, int cell_idx,
                         int *cx, int *cy, int *cw, int *ch)
{
    int W = 0, H = 0; mhal_vout_get_resolution(&W, &H);
    if (W <= 0 || H <= 0) { W = pb->cfg.hdmi_w > 0 ? pb->cfg.hdmi_w : 1920;
                            H = pb->cfg.hdmi_h > 0 ? pb->cfg.hdmi_h : 1080; }
    int rw = (pb->disp_mode == 0) ? W : W * 4 / 5;
    int rh = (pb->disp_mode == 0) ? H : H * 4 / 5;
    int n = pb_grid_side(pb->disp_mode);
    int cell_w = rw / n, cell_h = rh / n;
    *cx = (cell_idx % n) * cell_w;
    *cy = (cell_idx / n) * cell_h;
    *cw = cell_w;
    *ch = cell_h;
}

static long now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* 解析倍速 → milli(1000=1X); frame → *frame_out=1, milli=1000。 */
static int parse_speed(const char *s, int *frame_out)
{
    if (frame_out) *frame_out = 0;
    if (!s || !s[0]) return 1000;
    if (strcmp(s, "frame") == 0) { if (frame_out) *frame_out = 1; return 1000; }
    if (s[0] == '0' && s[1] == '.') {
        double v = atof(s);
        if (v > 0.0 && v <= 16.0) return (int)(v * 1000.0 + 0.5);
    }
    int n = atoi(s);
    if (n >= 1 && n <= 16) return n * 1000;
    return 1000;
}

static uint32_t day_end_of(uint32_t wall)
{
    time_t tt = (time_t)wall;
    struct tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59; tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}

static uint32_t pb_master_wall(const nvr_playback_t *pb)
{
    int sm = pb->speed_milli > 0 ? pb->speed_milli : 1000;
    if (pb->play_base_ms == 0) return pb->start_wall;
    long ref = pb->pause_at_ms ? pb->pause_at_ms : now_ms();
    long el = ref - pb->play_base_ms; if (el < 0) el = 0;
    /* wall += elapsed_ms * speed_milli / 1e6 */
    long delta = (el * (long)sm) / 1000000L;
    if (pb->backward) {
        if ((uint32_t)delta >= pb->play_base_wall) return 0;
        return pb->play_base_wall - (uint32_t)delta;
    }
    return pb->play_base_wall + (uint32_t)delta;
}

static void pb_wait_wall_fwd(nvr_playback_t *pb, uint32_t frame_wall)
{
    int sm = pb->speed_milli > 0 ? pb->speed_milli : 1000;
    while (pb->running && !pb->paused) {
        uint32_t mw = pb_master_wall(pb);
        if (frame_wall <= mw) return;
        uint32_t lag = frame_wall - mw;
        long sleep_ms = (long)lag * 1000000L / sm; /* wall_sec → ms at speed */
        if (sleep_ms > 200) sleep_ms = 200;
        if (sleep_ms < 5) sleep_ms = 5;
        usleep((useconds_t)(sleep_ms * 1000));
    }
}

static void pb_wait_wall_bwd(nvr_playback_t *pb, uint32_t frame_wall)
{
    int sm = pb->speed_milli > 0 ? pb->speed_milli : 1000;
    while (pb->running && !pb->paused) {
        uint32_t mw = pb_master_wall(pb);
        if (frame_wall >= mw) return;  /* 倒放:帧 wall 应 ≥ 主时钟(更早的帧已过) */
        uint32_t lag = mw - frame_wall;
        long sleep_ms = (long)lag * 1000000L / sm;
        if (sleep_ms > 200) sleep_ms = 200;
        if (sleep_ms < 5) sleep_ms = 5;
        usleep((useconds_t)(sleep_ms * 1000));
    }
}

static int pb_query_segs(nvr_playback_t *pb, int chn0, int want_stream,
                         uint32_t start_wall, uint32_t end_wall,
                         rsdk_index_slot_t *segs, int cap)
{
    rsdk_group_t *g = pb->cfg.group;
    if (end_wall <= start_wall) end_wall = start_wall + 24 * 3600;
    /* rectype=-1:连续∪事件段都纳入时间轴(纯事件录像的段 rectype≠CONTINUOUS,否则会漏播) */
    int n = rsdk_group_query_stream(g, start_wall, end_wall, chn0,
                                    -1, want_stream, segs, cap);
    if (n <= 0 && want_stream == NVR_STREAM_SUB)
        n = rsdk_group_query_stream(g, start_wall, end_wall, chn0,
                                    -1, -1, segs, cap);
    return n;
}

/* 盘上音频 → mhal_aout(硬解 AAC/G711 → 喇叭)。多格 enable 均可送。 */
static void pb_play_audio_frame(nvr_playback_t *pb, int cell,
                                const rsdk_frame_hdr_t *h,
                                const uint8_t *data, uint32_t len)
{
    if (!pb || !h || !data || len == 0) return;
    if (cell < 0 || cell >= pb->audio_n || !pb->audio_en[cell]) return;
    if (pb->paused || pb->backward) return;   /* 倒放/暂停不出声 */
    if (pb->speed_milli != 1000 || pb->frame_step) return; /* 仅 1X 正放送音 */

    mhal_acodec_t ac = MHAL_ACODEC_AAC;
    if (h->codec == RSDK_CODEC_AAC) ac = MHAL_ACODEC_AAC;
    /* 其它 codec 枚举暂按 AAC 试;G711 写入后扩展 */
    uint64_t ts_us = (uint64_t)h->wall_time * 1000000ULL;
    (void)mhal_aout_send(ac, 0, data, len, ts_us);
}

/* 从 MAIN 轨旁路取音频(子流回放时音轨在主流 writer)。返回打开的 player,失败 NULL。 */
static rsdk_group_player_t *pb_open_audio_player(nvr_playback_t *pb, int chn0, uint32_t start_wall)
{
    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * PB_MAX_SEGS);
    if (!segs) return NULL;
    int nseg = pb_query_segs(pb, chn0, NVR_STREAM_MAIN, start_wall,
                             pb->day_end_wall ? pb->day_end_wall + 1 : start_wall + 24 * 3600,
                             segs, PB_MAX_SEGS);
    rsdk_group_player_t *gp = NULL;
    if (nseg > 0)
        rsdk_group_play_open(pb->cfg.group, segs, nseg, &gp);
    free(segs);
    return gp;
}

/* 把 MAIN 音频 player 推到 master 附近并送出声。 */
static void pb_drain_audio_player(nvr_playback_t *pb, int cell, rsdk_group_player_t *gp_au)
{
    if (!gp_au || !pb) return;
    uint32_t mw = pb_master_wall(pb);
    for (int guard = 0; guard < 64; guard++) {
        rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0, gap = 0;
        if (rsdk_group_play_next2(gp_au, &h, &data, &len, &disk, &gap) != RSDK_OK) break;
        if (h.rec_kind != RSDK_RK_FRAME) continue;
        if ((uint32_t)h.wall_time + 1 < mw) continue;  /* 太旧跳过(追主时钟) */
        if ((uint32_t)h.wall_time > mw + 1) break;     /* 超前 → 下次再取 */
        if (h.frame_type == RSDK_FRAME_AUDIO)
            pb_play_audio_frame(pb, cell, &h, data, len);
    }
}

static int pb_peek_codec(nvr_playback_t *pb, int chn0, int want_stream,
                         uint32_t start_wall, int *codec)
{
    rsdk_group_t *g = pb->cfg.group;
    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * PB_MAX_SEGS);
    if (!segs) return -1;
    int nseg = pb_query_segs(pb, chn0, want_stream, start_wall, start_wall + 24 * 3600, segs, PB_MAX_SEGS);
    if (nseg <= 0) { free(segs); return -1; }
    rsdk_group_player_t *gp = NULL;
    if (rsdk_group_play_open(g, segs, nseg, &gp) != RSDK_OK || !gp) { free(segs); return -1; }
    free(segs);
    int rc = -1;
    for (int guard = 0; guard < 4096; guard++) {
        rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0, gap = 0;
        if (rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap) != RSDK_OK) break;
        if (h.rec_kind != RSDK_RK_FRAME) continue;
        if (h.frame_type == RSDK_FRAME_AUDIO) continue;
        if ((int)h.stream != want_stream) continue;
        if ((uint32_t)h.wall_time < start_wall) continue;
        if (h.frame_type != RSDK_FRAME_I) continue;
        *codec = h.codec; rc = 0; break;
    }
    rsdk_group_play_close(gp);
    return rc;
}

static void pb_set_notify(nvr_playback_t *pb, const char *msg)
{
    if (!msg || !msg[0]) { pb->notify[0] = 0; return; }
    snprintf(pb->notify, sizeof(pb->notify), "%s", msg);
}

/* 能力检查 → PlaybackMsgNotify;返回 0=可播,-1=notSupport。 */
static int pb_check_caps(nvr_playback_t *pb)
{
    int maxch = nvr_gui_config_max_playback_channels();
    int nch = pb->ch_count > 0 ? pb->ch_count : 1;
    if (nch > maxch) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Cannot support simultaneous playback more than %d channels.", maxch);
        pb_set_notify(pb, buf);
        snprintf(pb->status, sizeof(pb->status), "notSupport");
        return -1;
    }
    /* 多宫格用子流;仅 mode0 主流 → 不会超过 2 路主流。若强制多路主流则提示。 */
    if (pb->disp_mode == 0 && nch > 1) {
        pb_set_notify(pb, "Cannot support playback more than 2 main streams.");
        snprintf(pb->status, sizeof(pb->status), "notSupport");
        return -1;
    }
    if (pb->speed_milli > 8000) {
        pb_set_notify(pb, "Cannot support fast-forwarding greater than 8X.");
        pb->speed_milli = 8000;
        snprintf(pb->speed, sizeof(pb->speed), "8X");
    }
    /* 4K:预算装不下时在 open 阶段逐路跳过,这里对已知 4K 通道先提示 */
    for (int i = 0; i < nch && i < NVR_PB_MAX_CELLS; i++) {
        int chn0 = pb->ch_list[i] > 0 ? pb->ch_list[i] - 1 : -1;
        if (chn0 < 0) continue;
        int w = 0, h = 0, fps = 0;
        int st = (pb->disp_mode == 0) ? NVR_STREAM_MAIN : NVR_STREAM_SUB;
        nvr_stream_dim(pb->cfg.sm, chn0, st, &w, &h, &fps);
        if (w >= 3840 && h >= 2160) {
            double cost = mhal_budget_cost(w, h, fps > 0 ? fps : 15);
            if (cost > mhal_budget_total()) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Cannot support 4K decoding for Channel%d.", chn0 + 1);
                pb_set_notify(pb, buf);
                /* 不直接 notSupport 整场:其它路仍可播 */
            }
        }
    }
    return 0;
}

static int pb_send_vframe(nvr_playback_t *pb, pb_feeder_ctx_t *ctx, mhal_vdec_t *vdec,
                          const uint8_t *data, uint32_t len, const rsdk_frame_hdr_t *h,
                          uint8_t *par, int *par_len, int *par_building, int *synced,
                          uint64_t *prev_pts, int *trace)
{
    nal_class_t nc; nal_classify(data, (int)len, (h->codec == RSDK_CODEC_H265) ? 1 : 0, &nc);
    if (nc.is_param) {
        if (!*par_building) { *par_len = 0; *par_building = 1; }
        if (*par_len + (int)len <= 65536) { memcpy(par + *par_len, data, len); *par_len += (int)len; }
        return 0;
    }
    *par_building = 0;

    const uint8_t *sbuf = data; uint32_t slen = len; uint8_t *tmp = NULL;
    if (nc.is_key && !nc.has_param && *par_len > 0) {
        tmp = (uint8_t *)malloc((size_t)*par_len + len);
        if (tmp) {
            memcpy(tmp, par, (size_t)*par_len);
            memcpy(tmp + *par_len, data, len);
            sbuf = tmp; slen = (uint32_t)*par_len + len;
        }
    }
    if (!*synced) {
        if (!nc.is_key) { if (tmp) free(tmp); return 0; }
        *synced = 1; *prev_pts = h->pts;
        if (!pb->clock_reanchored) {
            pb->play_base_ms = now_ms();
            pb->play_base_wall = (uint32_t)h->wall_time;
            pb->clock_reanchored = 1;
        }
    }
    if (pb->i_only && !nc.is_key) { if (tmp) free(tmp); return 0; }

    if (pb->backward) pb_wait_wall_bwd(pb, (uint32_t)h->wall_time);
    else              pb_wait_wall_fwd(pb, (uint32_t)h->wall_time);

    if (!pb->backward && *prev_pts && h->pts >= *prev_pts &&
        (h->pts - *prev_pts) < (uint64_t)90000 * 2) {
        int sm = pb->speed_milli > 0 ? pb->speed_milli : 1000;
        long fine = (long)(((int64_t)(h->pts - *prev_pts) / 90) * 1000 / sm);
        if (fine > 0 && fine < 80 && pb->running && !pb->paused)
            usleep((useconds_t)(fine * 1000));
    }
    *prev_pts = h->pts;

    int sr = mhal_vdec_send(vdec, sbuf, slen, (uint32_t)h->pts);
    if (*trace < 20) {
        NVR_LOGI("pbtr", "chn%d SEND key=%d slen=%u ret=%d wall=%u",
                 ctx->chn0 + 1, nc.is_key, slen, sr, (uint32_t)h->wall_time);
        (*trace)++;
    }
    if (tmp) free(tmp);
    if ((uint32_t)h->wall_time > pb->max_decoded_wall) pb->max_decoded_wall = (uint32_t)h->wall_time;
    if (pb->min_decoded_wall == 0 || (uint32_t)h->wall_time < pb->min_decoded_wall)
        pb->min_decoded_wall = (uint32_t)h->wall_time;   /* 倒放递减边界 → 时间轴倒退 */

    if (pb->frame_step) {
        pb->paused = 1; pb->pause_at_ms = now_ms();
        snprintf(pb->status, sizeof(pb->status), "paused");
    }
    return 1;
}

/* 正放 feeder。 */
static void *pb_feeder_fwd(void *arg)
{
    pb_feeder_ctx_t *ctx = (pb_feeder_ctx_t *)arg;
    nvr_playback_t *pb = ctx->pb;
    rsdk_group_t *g = pb->cfg.group;
    int chn0 = ctx->chn0, cell = ctx->cell_idx, want_stream = ctx->want_stream;
    uint32_t start_wall = pb->start_wall;
    mhal_vdec_t *vdec = ctx->vdec;
    if (!vdec) return NULL;

    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * PB_MAX_SEGS);
    if (!segs) return NULL;
    int nseg = pb_query_segs(pb, chn0, want_stream, start_wall, pb->day_end_wall + 1, segs, PB_MAX_SEGS);
    if (nseg <= 0) { free(segs); return NULL; }
    rsdk_group_player_t *gp = NULL;
    if (rsdk_group_play_open(g, segs, nseg, &gp) != RSDK_OK || !gp) { free(segs); return NULL; }
    free(segs);

    uint8_t *par = (uint8_t *)malloc(65536);
    if (!par) { rsdk_group_play_close(gp); __sync_sub_and_fetch(&pb->alive_feeders, 1); return NULL; }
    int par_len = 0, par_building = 0, synced = 0, trace = 0;
    uint64_t prev_pts = 0;

    /* 子流视频时音轨在 MAIN writer → 旁路开 MAIN player 取 AAC(可中途 enable 再开) */
    rsdk_group_player_t *gp_au = NULL;

    NVR_LOGI("pb", "chn%d ▶回放 格%d %s @%u speed=%s%s", chn0 + 1, cell,
             want_stream == NVR_STREAM_SUB ? "子" : "主", start_wall, pb->speed,
             pb->i_only ? " I-only" : "");

    while (pb->running) {
        if (pb->paused) { usleep(40000); continue; }
        if (pb->day_end_wall && pb_master_wall(pb) >= pb->day_end_wall) {
            snprintf(pb->status, sizeof(pb->status), "stopped");
            break;
        }
        /* 中途打开音频:子流旁路 MAIN */
        if (!gp_au && want_stream != NVR_STREAM_MAIN &&
            cell < pb->audio_n && pb->audio_en[cell])
            gp_au = pb_open_audio_player(pb, chn0, pb_master_wall(pb));

        rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0, gap = 0;
        if (rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap) != RSDK_OK) break;
        if (h.rec_kind != RSDK_RK_FRAME) continue;

        /* 主流交织音轨:直接出声 */
        if (h.frame_type == RSDK_FRAME_AUDIO) {
            pb_play_audio_frame(pb, cell, &h, data, len);
            continue;
        }
        if ((int)h.stream != want_stream) continue;
        if ((uint32_t)h.wall_time < start_wall) continue;
        if (pb->day_end_wall && (uint32_t)h.wall_time > pb->day_end_wall) break;

        if (gap) pb_wait_wall_fwd(pb, (uint32_t)h.wall_time);
        {
            uint32_t mw = pb_master_wall(pb);
            if ((uint32_t)h.wall_time + 2 < mw && h.frame_type != RSDK_FRAME_I) continue;
        }
        pb_send_vframe(pb, ctx, vdec, data, len, &h, par, &par_len, &par_building,
                       &synced, &prev_pts, &trace);

        if (gp_au && cell < pb->audio_n && pb->audio_en[cell])
            pb_drain_audio_player(pb, cell, gp_au);
    }
    free(par);
    if (gp_au) rsdk_group_play_close(gp_au);
    rsdk_group_play_close(gp);
    __sync_sub_and_fetch(&pb->alive_feeders, 1);
    NVR_LOGI("pb", "chn%d ■回放退出(格%d)", chn0 + 1, cell);
    return NULL;
}

/* 倒放:预扫 I 帧索引,按 pts seek 逆序解显。 */
static void *pb_feeder_bwd(void *arg)
{
    pb_feeder_ctx_t *ctx = (pb_feeder_ctx_t *)arg;
    nvr_playback_t *pb = ctx->pb;
    rsdk_group_t *g = pb->cfg.group;
    int chn0 = ctx->chn0, want_stream = ctx->want_stream;
    uint32_t start_wall = pb->start_wall;
    mhal_vdec_t *vdec = ctx->vdec;
    if (!vdec) return NULL;

    uint32_t t0 = start_wall > 3600 ? start_wall - 3600 : 0; /* 倒放预扫最多 1 小时 */
    if (pb->day_end_wall) {
        uint32_t day0 = pb->day_end_wall - 86399;
        if (t0 < day0) t0 = day0;
    }

    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * PB_MAX_SEGS);
    if (!segs) return NULL;
    int nseg = pb_query_segs(pb, chn0, want_stream, t0, start_wall + 1, segs, PB_MAX_SEGS);
    if (nseg <= 0) { free(segs); return NULL; }
    rsdk_group_player_t *gp = NULL;
    if (rsdk_group_play_open(g, segs, nseg, &gp) != RSDK_OK || !gp) { free(segs); return NULL; }

    /* par 提前分配,预扫时顺带抓一次参数集(SPS/PPS 恒定)。 */
    uint8_t *par = (uint8_t *)malloc(65536);
    int par_len = 0, par_building = 0, synced = 0, trace = 0;
    int par_captured = 0;
    uint64_t prev_pts = 0;

    pb_ikey_t *keys = (pb_ikey_t *)malloc(sizeof(pb_ikey_t) * PB_IKEY_MAX);
    int nk = 0;
    if (keys) {
        for (int guard = 0; guard < 500000 && nk < PB_IKEY_MAX; guard++) {
            rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0;
            if (rsdk_group_play_next(gp, &h, &data, &len, &disk) != RSDK_OK) break;
            if (h.rec_kind != RSDK_RK_FRAME) continue;
            if ((int)h.stream != want_stream) continue;
            /* ★ 崩溃根因修复:倒放只 seek 到 IDR、跳过其前的 SPS/PPS → 送无参数 IDR 进解码 =
             *   "scan first header error" 风暴 → nvr_app 崩。此处抓一次参数集,显示时前置到每个 IDR。 */
            if (!par_captured && par) {
                nal_class_t nc; nal_classify(data, (int)len, (h.codec == RSDK_CODEC_H265) ? 1 : 0, &nc);
                if (nc.is_param) { if (par_len + (int)len <= 65536) { memcpy(par + par_len, data, len); par_len += (int)len; } }
                else if (nc.is_key && par_len > 0) par_captured = 1;   /* 首个 IDR 前参数已齐 → 冻结 */
            }
            if (h.frame_type != RSDK_FRAME_I) continue;
            if ((uint32_t)h.wall_time < t0 || (uint32_t)h.wall_time > start_wall) continue;
            keys[nk].wall = (uint32_t)h.wall_time;
            keys[nk].pts = h.pts;
            nk++;
        }
    }
    rsdk_group_play_close(gp);
    NVR_LOGI("pb", "chn%d ◀倒放 扫到 %d 个 I 帧(参数集 %d 字节)", chn0 + 1, nk, par_len);

    for (int i = nk - 1; i >= 0 && pb->running; i--) {
        while (pb->paused && pb->running) usleep(40000);
        if (!pb->running) break;
        if (rsdk_group_play_open(g, segs, nseg, &gp) != RSDK_OK || !gp) break;
        rsdk_group_play_seek_pts(gp, keys[i].pts);
        rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0, gap = 0;
        int got = 0;
        for (int g2 = 0; g2 < 64; g2++) {
            if (rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap) != RSDK_OK) break;
            if (h.rec_kind != RSDK_RK_FRAME || h.frame_type != RSDK_FRAME_I) continue;
            if ((int)h.stream != want_stream) continue;
            got = 1; break;
        }
        if (got && par) {
            pb_send_vframe(pb, ctx, vdec, data, len, &h, par, &par_len, &par_building,
                           &synced, &prev_pts, &trace);
        }
        rsdk_group_play_close(gp); gp = NULL;
        if (pb->frame_step) break;
    }

    free(par); free(keys); free(segs);
    __sync_sub_and_fetch(&pb->alive_feeders, 1);
    NVR_LOGI("pb", "chn%d ■倒放退出", chn0 + 1);
    return NULL;
}

static void *pb_feeder(void *arg)
{
    pb_feeder_ctx_t *ctx = (pb_feeder_ctx_t *)arg;
    if (ctx->pb->backward) return pb_feeder_bwd(arg);
    return pb_feeder_fwd(arg);
}

static int pb_chlist_same(nvr_playback_t *pb)
{
    if (!pb->dec_open || pb->dec_n != pb->ch_count) return 0;
    for (int i = 0; i < pb->ch_count && i < NVR_PB_MAX_CELLS; i++)
        if (pb->dec_chlist[i] != pb->ch_list[i]) return 0;
    return 1;
}

static void pb_open_decoders_locked(nvr_playback_t *pb)
{
    pb->nth = 0;
    int nch = pb->ch_count > 0 ? pb->ch_count : 1;
    if (nch > PB_MAX_FEED) nch = PB_MAX_FEED;
    int want_stream = (pb->disp_mode == 0) ? NVR_STREAM_MAIN : NVR_STREAM_SUB;
    pb->want_stream = want_stream;
    double budget = mhal_budget_total(), used = 0;

    if (pb->cfg.sm) for (int i = 0; i < PB_MAX_CH; i++) nvr_stream_set_display(pb->cfg.sm, i, -1);
    NVR_LOGI("pb", "回放开解码前:活跃解码器 %d", mhal_vdec_active_count());

    mhal_vout_defer_begin();
    for (int i = 0; i < nch; i++) {
        int chn0 = (pb->ch_count > 0 && pb->ch_list[i] > 0) ? pb->ch_list[i] - 1
                 : (pb->chn0 >= 0 ? pb->chn0 : i);
        if (chn0 < 0) continue;
        int codec = -1;
        if (pb_peek_codec(pb, chn0, want_stream, pb->start_wall, &codec) != 0) {
            NVR_LOGW("pb", "chn%d 起播 %u 无录像(该格留黑)", chn0 + 1, pb->start_wall);
            continue;
        }
        int w = 0, ht = 0, fps = 0; nvr_stream_dim(pb->cfg.sm, chn0, want_stream, &w, &ht, &fps);
        double cost = mhal_budget_cost(w, ht, fps);
        if (cost > 0 && used + cost > budget) {
            char buf[128];
            if (w >= 3840)
                snprintf(buf, sizeof(buf), "Cannot support 4K decoding for Channel%d.", chn0 + 1);
            else
                snprintf(buf, sizeof(buf),
                         "Cannot support simultaneous playback more than %d channels.", pb->nth);
            pb_set_notify(pb, buf);
            NVR_LOGW("pb", "预算已满:通道 %d 及之后不回放", chn0 + 1);
            break;
        }
        mhal_codec_t mc = (codec == RSDK_CODEC_H265) ? MHAL_CODEC_H265 : MHAL_CODEC_H264;
        mhal_vdec_t *vd = NULL;
        int orc = mhal_vdec_open(chn0, mc, w, ht, fps, i, &vd);
        if (orc != 0 || !vd) { NVR_LOGE("pb", "chn%d 开解码失败 %d", chn0 + 1, orc); continue; }
        used += cost;
        pb_feeder_ctx_t *fc = &pb->fctx[pb->nth];
        fc->pb = pb; fc->chn0 = chn0; fc->cell_idx = i; fc->want_stream = want_stream;
        fc->vdec = vd; fc->thread_ok = 0;
        pb->nth++;
    }
    for (int k = 0; k < pb->nth; k++) {
        int cx, cy, cw, chh; pb_cell_rect(pb, pb->fctx[k].cell_idx, &cx, &cy, &cw, &chh);
        mhal_vout_bind_rect(pb->fctx[k].chn0, cx, cy, cw, chh);
    }
    mhal_vout_defer_end();
    pb->dec_open = (pb->nth > 0);
    pb->dec_n = pb->ch_count;
    for (int i = 0; i < pb->ch_count && i < NVR_PB_MAX_CELLS; i++) pb->dec_chlist[i] = pb->ch_list[i];
}

static void pb_start_feeders_locked(nvr_playback_t *pb)
{
    if (pb->nth <= 0) { pb->running = 0; snprintf(pb->status, sizeof(pb->status), "stopped"); return; }
    pb->running = 1;
    pb->alive_feeders = pb->nth;
    pb->max_decoded_wall = pb->start_wall;
    pb->min_decoded_wall = pb->start_wall;
    for (int k = 0; k < pb->nth; k++)
        pb->fctx[k].thread_ok = (pthread_create(&pb->th[k], NULL, pb_feeder, &pb->fctx[k]) == 0);
    snprintf(pb->status, sizeof(pb->status), "playing");
}

static void pb_stop_feeders_locked(nvr_playback_t *pb)
{
    pb->running = 0;
    for (int i = 0; i < pb->nth; i++)
        if (pb->fctx[i].thread_ok) { pthread_join(pb->th[i], NULL); pb->fctx[i].thread_ok = 0; }
}

static void pb_close_decoders_locked(nvr_playback_t *pb)
{
    pb_stop_feeders_locked(pb);
    if (pb->nth > 0) {
        mhal_vout_defer_begin();
        for (int i = 0; i < pb->nth; i++)
            if (pb->fctx[i].vdec) { mhal_vdec_close(pb->fctx[i].vdec); pb->fctx[i].vdec = NULL; }
        mhal_vout_defer_end();
    }
    pb->nth = 0; pb->dec_open = 0; pb->dec_n = 0;
}

static void pb_blackout_locked(nvr_playback_t *pb, int chn0)
{
    if (pb->cfg.sm) for (int i = 0; i < PB_MAX_CH; i++) nvr_stream_set_display(pb->cfg.sm, i, -1);
    mhal_vout_clear_black();
    if (chn0 >= 0) pb->chn0 = chn0;
}

static void pb_stop_locked(nvr_playback_t *pb)
{
    pb_close_decoders_locked(pb);
    mhal_vout_clear_black();
    mhal_aout_close();   /* 停回放时关喇叭通路 */
    pb->chn0 = -1;
    snprintf(pb->status, sizeof(pb->status), "stopped");
}

void nvr_playback_enter(nvr_playback_t *pb, int chn1)
{
    if (!pb) return;
    int chn0 = (chn1 > 0) ? chn1 - 1 : (pb->chn0 >= 0 ? pb->chn0 : 0);
    pthread_mutex_lock(&pb->lock);
    pb_close_decoders_locked(pb);
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status, sizeof(pb->status), "stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_create(const nvr_playback_cfg_t *cfg, nvr_playback_t **out)
{
    if (!cfg || !out) return -1;
    nvr_playback_t *pb = (nvr_playback_t *)calloc(1, sizeof(*pb));
    if (!pb) return -1;
    pb->cfg = *cfg; pb->chn0 = -1; pb->want_stream = NVR_STREAM_MAIN;
    pb->speed_milli = 1000; pb->audio_n = 4;
    snprintf(pb->status, sizeof(pb->status), "stopped");
    snprintf(pb->speed, sizeof(pb->speed), "1X");
    snprintf(pb->direction, sizeof(pb->direction), "forward");
    pthread_mutex_init(&pb->lock, NULL);
    *out = pb; return 0;
}

void nvr_playback_destroy(nvr_playback_t *pb)
{
    if (!pb) return;
    pthread_mutex_lock(&pb->lock); pb_stop_locked(pb); pthread_mutex_unlock(&pb->lock);
    pthread_mutex_destroy(&pb->lock); free(pb);
}

int nvr_playback_control(nvr_playback_t *pb, const char *action, int chn1,
                         uint32_t start_wall, const char *speed, const char *direction)
{
    if (!pb || !action) return -1;
    pthread_mutex_lock(&pb->lock);
    pb_set_notify(pb, NULL);

    int old_bwd = pb->backward;
    int frame = 0;
    if (speed && speed[0]) {
        snprintf(pb->speed, sizeof(pb->speed), "%s", speed);
        pb->speed_milli = parse_speed(speed, &frame);
        pb->frame_step = frame;
        pb->i_only = (pb->speed_milli > 4000) || pb->backward;
    }
    if (direction && direction[0]) {
        snprintf(pb->direction, sizeof(pb->direction), "%s", direction);
        pb->backward = (strcmp(direction, "backward") == 0);
        pb->i_only = (pb->speed_milli > 4000) || pb->backward;
    }

    int is_pause = (strcmp(action, "pause") == 0);
    int is_stop  = (strcmp(action, "stop") == 0);
    int is_play  = (strcmp(action, "play") == 0) || (strcmp(action, "start") == 0) ||
                   (strcmp(action, "seek") == 0);
    int is_resume = (strcmp(action, "resume") == 0) ||
                    (is_play && pb->nth > 0 && pb->paused && start_wall == 0 &&
                     !pb->frame_step);

    /* 同 startTime 仅改 speed/direction:不重开(doc);方向翻转则重启 feeder */
    int same_seek = is_play && start_wall > 0 && start_wall == pb->start_wall &&
                    pb->nth > 0 && (pb->running || pb->paused);

    /* ★ 方向翻转(正↔倒)最优先:不管走哪个 action 分支,都从**当前播放位置**按新方向重启
     * feeder + 重锚时钟/解码边界。否则会出现"direction=backward 已设,但实际仍是正放 feeder
     * 在跑、时间轴不动"(resume/同 seek 无翻转/caps 早退等路径都会漏掉重启)。 */
    int dir_flipped = (direction && direction[0] && old_bwd != pb->backward &&
                       pb->nth > 0 && (pb->running || pb->paused) && !is_stop);

    if (dir_flipped) {
        uint32_t curpos = pb_master_wall(pb);
        if (curpos == 0) curpos = pb->start_wall;
        pb_stop_feeders_locked(pb);
        pb->start_wall = curpos;
        pb->day_end_wall = day_end_of(curpos);
        pb->i_only = (pb->speed_milli > 4000) || pb->backward;
        pb->play_base_ms = now_ms(); pb->play_base_wall = curpos;
        pb->clock_reanchored = 0; pb->paused = 0; pb->pause_at_ms = 0;
        pb->cur_wall = curpos;
        pb->max_decoded_wall = curpos; pb->min_decoded_wall = curpos;
        pb_start_feeders_locked(pb);
        snprintf(pb->status, sizeof(pb->status), "playing");
    } else if (is_pause) {
        if (pb->nth > 0 && !pb->paused) {
            pb->paused = 1; pb->pause_at_ms = now_ms();
            snprintf(pb->status, sizeof(pb->status), "paused");
        }
    } else if (is_stop) {
        pb_stop_locked(pb);
    } else if (same_seek) {
        if (pb->paused) {
            if (pb->pause_at_ms) { pb->play_base_ms += now_ms() - pb->pause_at_ms; pb->pause_at_ms = 0; }
            pb->paused = 0;
        }
        if (old_bwd != pb->backward) {
            pb_stop_feeders_locked(pb);
            pb->play_base_ms = now_ms(); pb->play_base_wall = pb->start_wall;
            pb->clock_reanchored = 0; pb->paused = 0; pb->pause_at_ms = 0;
            pb_start_feeders_locked(pb);
        }
        snprintf(pb->status, sizeof(pb->status), "playing");
    } else if (is_resume) {
        if (pb->paused) {
            if (pb->pause_at_ms) { pb->play_base_ms += now_ms() - pb->pause_at_ms; pb->pause_at_ms = 0; }
            pb->paused = 0;
        }
        snprintf(pb->status, sizeof(pb->status), "playing");
    } else if (is_play) {
        if (pb_check_caps(pb) != 0) { pthread_mutex_unlock(&pb->lock); return 0; }
        int chn0 = (chn1 > 0) ? chn1 - 1 : (pb->ch_count > 0 ? pb->ch_list[0] - 1 : pb->chn0);
        uint32_t sw = (start_wall > 0) ? start_wall : pb->start_wall;
        if (sw == 0 || (chn0 < 0 && pb->ch_count <= 0)) { pthread_mutex_unlock(&pb->lock); return -1; }
        pb_stop_feeders_locked(pb);
        pb->start_wall = sw;
        pb->day_end_wall = day_end_of(sw);
        if (!pb_chlist_same(pb)) {
            pb_close_decoders_locked(pb);
            pb_blackout_locked(pb, chn0);
            pb_open_decoders_locked(pb);
        }
        for (int k = 0; k < pb->nth; k++) {
            int cx, cy, cw, chh; pb_cell_rect(pb, pb->fctx[k].cell_idx, &cx, &cy, &cw, &chh);
            mhal_vout_bind_rect(pb->fctx[k].chn0, cx, cy, cw, chh);
        }
        pb->want_stream = (pb->disp_mode == 0) ? NVR_STREAM_MAIN : NVR_STREAM_SUB;
        pb->paused = 0; pb->pause_at_ms = 0;
        pb->play_base_ms = now_ms(); pb->play_base_wall = sw;
        pb->clock_reanchored = 0;
        pb->cur_wall = sw;
        pb->i_only = (pb->speed_milli > 4000) || pb->backward;
        pb_start_feeders_locked(pb);
    }
    pthread_mutex_unlock(&pb->lock);
    return 0;
}

void nvr_playback_set_mode(nvr_playback_t *pb, int display_mode, const int *channels1, int n)
{
    if (!pb) return;
    pthread_mutex_lock(&pb->lock);
    pb->disp_mode = display_mode;
    if (n > NVR_PB_MAX_CELLS) n = NVR_PB_MAX_CELLS; if (n < 0) n = 0;
    pb->ch_count = n;
    for (int i = 0; i < n; i++) pb->ch_list[i] = channels1 ? channels1[i] : 0;
    /* 音频 enable[] 按格数伸缩:增加保旧,减少丢尾 */
    int cells = pb_cell_count(display_mode == 0 ? 1 : display_mode);
    if (cells > NVR_PB_MAX_CELLS) cells = NVR_PB_MAX_CELLS;
    if (cells > pb->audio_n) {
        for (int i = pb->audio_n; i < cells; i++) pb->audio_en[i] = 0;
    }
    pb->audio_n = cells;

    pb_close_decoders_locked(pb);
    int chn0 = (n > 0 && pb->ch_list[0] > 0) ? pb->ch_list[0] - 1 : (pb->chn0 >= 0 ? pb->chn0 : 0);
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status, sizeof(pb->status), "stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_get_mode(nvr_playback_t *pb, int *channels_out, int cap, int *n)
{
    if (!pb) return 4;
    pthread_mutex_lock(&pb->lock);
    int dm = pb->disp_mode > 0 ? pb->disp_mode : 1;
    int cnt = pb->ch_count > 0 ? pb->ch_count : 1;
    if (cnt > cap) cnt = cap;
    for (int i = 0; i < cnt; i++) channels_out[i] = pb->ch_count > 0 ? pb->ch_list[i] : (i + 1);
    if (n) *n = cnt;
    pthread_mutex_unlock(&pb->lock);
    return dm;
}

void nvr_playback_set_audio(nvr_playback_t *pb, const int *enable, int n)
{
    if (!pb || !enable) return;
    pthread_mutex_lock(&pb->lock);
    if (n > NVR_PB_MAX_CELLS) n = NVR_PB_MAX_CELLS;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) pb->audio_en[i] = enable[i] ? 1 : 0;
    pb->audio_n = n;
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_get_audio(nvr_playback_t *pb, int *enable_out, int cap)
{
    if (!pb || !enable_out || cap <= 0) return 0;
    pthread_mutex_lock(&pb->lock);
    int n = pb->audio_n;
    if (n > cap) n = cap;
    if (n <= 0) { /* 默认按当前布局格数全静音 */
        n = pb_cell_count(pb->disp_mode == 0 ? 1 : (pb->disp_mode > 0 ? pb->disp_mode : 4));
        if (n > cap) n = cap;
        for (int i = 0; i < n; i++) enable_out[i] = 0;
    } else {
        for (int i = 0; i < n; i++) enable_out[i] = pb->audio_en[i] ? 1 : 0;
    }
    pthread_mutex_unlock(&pb->lock);
    return n;
}

void nvr_playback_get_status(nvr_playback_t *pb, char *status, char *speed,
                             char *direction, uint32_t *cur_wall,
                             char *notify, int notify_cap)
{
    if (!pb) return;
    pthread_mutex_lock(&pb->lock);
    int ended = (pb->nth > 0 && pb->alive_feeders <= 0 && !pb->paused);
    if (status)    snprintf(status, 16, "%s", ended ? "stopped" : pb->status);
    if (speed)     snprintf(speed, 16, "%s", pb->speed);
    if (direction) snprintf(direction, 16, "%s", pb->direction);
    if (notify && notify_cap > 0) {
        if (pb->notify[0]) snprintf(notify, (size_t)notify_cap, "%s", pb->notify);
        else notify[0] = 0;
    }
    if (cur_wall) {
        uint32_t cur;
        if (pb->nth <= 0 || pb->play_base_ms == 0) cur = pb->start_wall;
        else {
            uint32_t sc = pb_master_wall(pb);
            uint32_t md = pb->backward ? pb->min_decoded_wall : pb->max_decoded_wall;
            if (pb->backward) cur = (sc > md) ? sc : md; /* 倒放:主时钟与最早解码边界取较新(不早于实解) */
            else cur = (sc < md) ? sc : md;
        }
        *cur_wall = cur;
    }
    pthread_mutex_unlock(&pb->lock);
}
