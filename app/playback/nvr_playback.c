/***************************************************************************************
 *  nvr_playback.c — 本机录像回放引擎(见 nvr_playback.h)。
 *  单通道全屏回放:rsdk_group_query 找段 → rsdk_group_play_next2 取解密 Annex-B 帧
 *  →(按 hdr.stream 过滤 + 跳音频 + I 帧起播)→ mhal_vdec_send 解码到全屏窗口。
 *  接管窗口0:play 前 nvr_preview_fullscreen + 关 live 解码;stop 后归还 live。
 ***************************************************************************************/
#include "nvr_playback.h"
#include "nvr_streaming.h"
#include "stream_nal.h"     /* 与 liveView 同一套 NAL 分类(参数集/关键帧判定) */
#include "nvr_preview.h"
#include "mhal_vdec.h"
#include "mhal_vout.h"
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
#define PB_MAX_FEED  16       /* 同屏最多回放通道数 */
#define PB_AU_MAX    (3*1024*1024)  /* access unit 组装缓冲上限(8K IDR ~300KB,留足) */
#define PB_MAX_CH    32       /* 通道数(黑屏时关全部 live 解码) */

/* 每通道回放线程上下文(feeder 只读,起播前填好,避免竞态)。
 * vdec 在起播时于 defer 批中**统一开好并绑格**(一次成图,避免逐路 commit 触发"can't extend"),
 * feeder 只负责喂帧;vdec 由 stop 时统一关闭。 */
typedef struct pb_feeder_ctx {
    struct nvr_playback *pb;
    int          chn0;        /* 该路回放通道(0-based) */
    int          cell_idx;    /* 在回放宫格中的格位(= 在 ch_list 中的序号) */
    int          want_stream; /* 本路码流(主/子) */
    mhal_vdec_t *vdec;        /* 预开的解码器(绑好本格;跨 seek 复用,由 close_decoders 关) */
    int          thread_ok;   /* feeder 线程已起(join 依据;解码器复用后 vdec 常驻不能当存活标志) */
} pb_feeder_ctx_t;

struct nvr_playback {
    nvr_playback_cfg_t cfg;
    pthread_mutex_t    lock;
    pthread_t          th[PB_MAX_FEED];       /* 每通道一个 feeder */
    pb_feeder_ctx_t    fctx[PB_MAX_FEED];
    int                nth;                    /* 活跃 feeder 数 */
    volatile int       running;      /* feeder 存活(全体共享) */
    volatile int       paused;
    volatile int       chn0;         /* 主回放通道(0-based),-1=无;供 enter/blackout */
    volatile int       want_stream;  /* NVR_STREAM_MAIN/SUB(超预算回退子) */
    volatile int       speed_num;    /* 1/2/4/8:倍速(节奏) */
    uint32_t           start_wall;   /* 起播 epoch(全通道共享起点) */
    int                backward;
    char               status[16], speed[16], direction[16];
    volatile uint32_t  cur_wall;     /* 当前回放位置(共享时钟算出,见 get_status) */
    int                disp_mode;    /* 回放布局(GUI_setPlaybackMode 记录) */
    int                ch_list[16];  /* 1-based 通道 */
    int                ch_count;
    /* --- 共享回放时钟(全通道同步 + 时间轴平滑,不再取各 feeder 最大 wall) --- */
    long               play_base_ms;   /* 起播/续播时 monotonic 基准 */
    uint32_t           play_base_wall; /* 该基准对应的录像 wall(=start_wall) */
    long               pause_at_ms;    /* 暂停时刻(0=未暂停);resume 补进 base */
    volatile int       alive_feeders;  /* 存活 feeder 数;归 0 = 放完/无录像 → 停(不空走时间轴) */
    volatile uint32_t  max_decoded_wall;/* 已解码到的最大 wall;时间轴 clamp 到此(无录像不前进) */
    volatile int       clock_reanchored;/* 首帧已把共享时钟锚到实际起播 wall(跳过空档,免空睡) */
    /* --- 解码器跨 seek 复用(避免每次拖时间轴都拆/重建 8K 解码器 → -1/送流失败churn) --- */
    int                dec_open;       /* 解码器已开(feeder 可反复起停复用) */
    int                dec_chlist[16]; /* 解码器当前所属 1-based 通道集 */
    int                dec_n;          /* 上同,个数;与请求不一致才重开 */
};

/* displayMode → mhal 布局(与 liveView mode_to_mhal 完全一致:含 8=1大+7小、6→9格 等)。 */
static mhal_layout_t pb_mode_to_layout(int mode)
{
    switch (mode) {
        case 0: case 1: return MHAL_LAYOUT_1;
        case 4:  return MHAL_LAYOUT_4;
        case 8:  return MHAL_LAYOUT_8;
        case 9:  return MHAL_LAYOUT_9;
        case 16: return MHAL_LAYOUT_16;
        case 25: return MHAL_LAYOUT_25;
        case 36: return MHAL_LAYOUT_36;
        default: return MHAL_LAYOUT_16;
    }
}
/* 回放视频区宫格:先按 HDMI 分辨率算视频区(mode0=全屏 W×H;其余=0.8W×0.8H@(0,0)),
 * 再用**与 liveView 同一套**布局(mhal_layout_rect)在该区域内算第 cell_idx 格的像素矩形。 */
static void pb_cell_rect(struct nvr_playback *pb, int cell_idx,
                         int *cx, int *cy, int *cw, int *ch)
{
    /* ★ 用**实际显示分辨率**(与 liveView 同源 g_disp)算视频区,不能用可能过时/缺省的 cfg.hdmi_*
     *   (否则按 1920 算的格落到 4K 屏上会缩成一小块)。 */
    int W = 0, H = 0; mhal_vout_get_resolution(&W, &H);
    if (W <= 0 || H <= 0) { W = pb->cfg.hdmi_w > 0 ? pb->cfg.hdmi_w : 1920;
                            H = pb->cfg.hdmi_h > 0 ? pb->cfg.hdmi_h : 1080; }
    int rw = (pb->disp_mode == 0) ? W : W * 4 / 5;   /* mode0 全屏,其余 0.8 视频区 */
    int rh = (pb->disp_mode == 0) ? H : H * 4 / 5;
    mhal_layout_rect(pb_mode_to_layout(pb->disp_mode), cell_idx, rw, rh, cx, cy, cw, ch);
}

static long now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
                          return ts.tv_sec*1000L + ts.tv_nsec/1000000L; }
static int parse_speed(const char *s){ if(!s||!s[0])return 1; int n=atoi(s); return (n>=1&&n<=16)?n:1; }

/* 探测某通道自 start_wall 起首个视频 I 帧的 codec(确认有可放录像 + 定 H264/H265)。
 * 返回 0 且 *codec 有效=有录像;<0=无录像/失败。只读若干帧即关,开销小。 */
static int pb_peek_codec(nvr_playback_t *pb, int chn0, int want_stream,
                         uint32_t start_wall, int *codec)
{
    rsdk_group_t *g = pb->cfg.group;
    rsdk_index_slot_t *segs = (rsdk_index_slot_t*)malloc(sizeof(rsdk_index_slot_t)*PB_MAX_SEGS);
    if(!segs) return -1;
    int nseg = rsdk_group_query(g, start_wall, start_wall + 24*3600, chn0,
                                RSDK_REC_CONTINUOUS, segs, PB_MAX_SEGS);
    if(nseg<=0){ free(segs); return -1; }
    rsdk_group_player_t *gp=NULL;
    if(rsdk_group_play_open(g, segs, nseg, &gp)!=RSDK_OK || !gp){ free(segs); return -1; }
    free(segs);
    int rc = -1;
    for(int guard=0; guard<4096; guard++){
        rsdk_frame_hdr_t h; const uint8_t *data=NULL; uint32_t len=0; int disk=0, gap=0;
        if(rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap)!=RSDK_OK) break;
        if(h.frame_type==RSDK_FRAME_AUDIO) continue;
        if((int)h.stream != want_stream) continue;
        if((uint32_t)h.wall_time < start_wall) continue;
        if(h.frame_type != RSDK_FRAME_I) continue;      /* 等首个 I 帧 */
        *codec = h.codec; rc = 0; break;
    }
    rsdk_group_play_close(gp);
    return rc;
}

/* feeder(每通道一个):读该通道段组帧 → 过滤 → 喂**预开**的解码器 ctx->vdec(已绑本格),按 pts 节奏。
 * 解码器由 spawn 在 defer 批中开好并绑格,feeder 不再 open/commit(避免逐路成图触发"can't extend")。
 * pb->running=0 退出;vdec 由 stop 统一关。arg = pb_feeder_ctx_t*。 */
static void *pb_feeder(void *arg)
{
    pb_feeder_ctx_t *ctx = (pb_feeder_ctx_t*)arg;
    nvr_playback_t  *pb  = ctx->pb;
    rsdk_group_t    *g   = pb->cfg.group;
    int chn0 = ctx->chn0;
    int cell = ctx->cell_idx;
    int want_stream = ctx->want_stream;
    uint32_t start_wall = pb->start_wall;
    mhal_vdec_t *vdec = ctx->vdec;
    if(!vdec) return NULL;

    rsdk_index_slot_t *segs = (rsdk_index_slot_t*)malloc(sizeof(rsdk_index_slot_t)*PB_MAX_SEGS);
    if(!segs) return NULL;
    int nseg = rsdk_group_query(g, start_wall, start_wall + 24*3600, chn0,
                                RSDK_REC_CONTINUOUS, segs, PB_MAX_SEGS);
    if(nseg<=0){ free(segs); return NULL; }
    rsdk_group_player_t *gp=NULL;
    if(rsdk_group_play_open(g, segs, nseg, &gp)!=RSDK_OK || !gp){ free(segs); return NULL; }
    free(segs);

    /* ★ 送解码与 liveView(stream_router.c)**完全同一套**,只是数据源是录像:
     *   ① nal_classify 分类每帧(纯参数集/关键帧/含参数);② 参数集持续缓存到 par;
     *   ③ 缺参数的关键帧 → 拼上缓存 par 再送(否则解码器 scan 不到头);④ 纯参数集帧只缓存不单独送;
     *   ⑤ 从关键帧起同步(synced,否则 P 帧参考链断花屏);⑥ 逐帧送,按 pts 共享时钟实时节奏。 */
    uint8_t *par = (uint8_t*)malloc(65536);
    if(!par){ rsdk_group_play_close(gp); __sync_sub_and_fetch(&pb->alive_feeders,1); return NULL; }
    int par_len=0, par_building=0, synced=0, trace=0;
    long base_ms=0; uint64_t base_pts=0, prev_pts=0;
    NVR_LOGI("pb","chn%d ▶回放 格%d %s @%u", chn0+1, cell,
             want_stream==NVR_STREAM_SUB?"子":"主", start_wall);

    while(pb->running){
        if(pb->paused){ usleep(40000); continue; }
        rsdk_frame_hdr_t h; const uint8_t *data=NULL; uint32_t len=0; int disk=0, gap=0;
        if(rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap)!=RSDK_OK) break;   /* 放完 */
        if(h.frame_type==RSDK_FRAME_AUDIO) continue;             /* 跳音频 */
        if((int)h.stream != want_stream)  continue;              /* 只放选中码流(主/子) */

        nal_class_t nc; nal_classify(data, (int)len, (h.codec==RSDK_CODEC_H265)?1:0, &nc);

        /* ① 参数集缓存(每帧,与是否解码无关):连续参数集 NAL 累积到 par。 */
        if(nc.is_param){
            if(!par_building){ par_len=0; par_building=1; }
            if(par_len + (int)len <= 65536){ memcpy(par+par_len, data, len); par_len += len; }
            continue;                                            /* 纯参数集帧:只缓存,不单独送 */
        }
        par_building = 0;

        if((uint32_t)h.wall_time < start_wall) continue;         /* 跳到起播点 */

        /* ② 缺参数的关键帧 → 拼上缓存 par(SPS/PPS/VPS)再送 */
        const uint8_t *sbuf=data; uint32_t slen=len; uint8_t *tmp=NULL;
        if(nc.is_key && !nc.has_param && par_len>0){
            tmp = (uint8_t*)malloc((size_t)par_len + len);
            if(tmp){ memcpy(tmp, par, par_len); memcpy(tmp+par_len, data, len); sbuf=tmp; slen=(uint32_t)par_len+len; }
        }

        int speed = pb->speed_num>0?pb->speed_num:1;
        /* ③ 同步门控:从关键帧起(P 帧参考链需先有 IDR) */
        if(!synced){
            if(!nc.is_key){ if(tmp) free(tmp); continue; }
            synced=1; base_pts=h.pts; prev_pts=h.pts;
            /* 实际起播 wall 可能晚于请求点(空档 rsdk 返回下段)→ 重锚共享时钟到实际位置,免空睡。 */
            if(!pb->clock_reanchored){
                pb->play_base_ms=now_ms(); pb->play_base_wall=(uint32_t)h.wall_time; pb->clock_reanchored=1;
                NVR_LOGI("pb","chn%d 共享时钟重锚 → 实际起播 wall=%u(请求 %u)", chn0+1, (uint32_t)h.wall_time, start_wall);
            }
            base_ms = pb->play_base_ms +
                      (long)(((int64_t)((uint32_t)h.wall_time - pb->play_base_wall))*1000/speed);
        }
        /* 段边界/空档(pts 回退或前跳>10s):重锚现在播 + 共享时钟推进到本帧 wall(跳过空档) */
        if(h.pts < prev_pts || (h.pts - prev_pts) > (uint64_t)90000*10){
            base_pts=h.pts; base_ms=now_ms();
            pb->play_base_ms=now_ms(); pb->play_base_wall=(uint32_t)h.wall_time;
        }
        prev_pts = h.pts;
        /* ④ pts 平滑实时节奏 */
        long target = base_ms + (long)(((int64_t)(h.pts - base_pts)/90)/speed);
        long dt = target - now_ms();
        while(dt > 0 && pb->running && !pb->paused){
            long s = dt > 200 ? 200 : dt; usleep((useconds_t)(s*1000)); dt = target - now_ms();
        }
        int sr = mhal_vdec_send(vdec, sbuf, slen, (uint32_t)h.pts);
        if(trace < 40){ NVR_LOGI("pbtr","chn%d SEND key=%d haspar=%d slen=%u ret=%d",
                         chn0+1, nc.is_key, nc.has_param, slen, sr); trace++; }
        if(tmp) free(tmp);
        if((uint32_t)h.wall_time > pb->max_decoded_wall) pb->max_decoded_wall = (uint32_t)h.wall_time;
    }
    free(par);
    rsdk_group_play_close(gp);
    __sync_sub_and_fetch(&pb->alive_feeders, 1);   /* 本路放完/退出 → 存活计数减一 */
    NVR_LOGI("pb","chn%d ■回放退出(格%d)", chn0+1, cell);
    return NULL;
}

/* 请求的通道集是否与已开解码器一致(一致 → seek 复用解码器,不重建 8K 解码器)。 */
static int pb_chlist_same(nvr_playback_t *pb)
{
    if(!pb->dec_open || pb->dec_n != pb->ch_count) return 0;
    for(int i=0;i<pb->ch_count && i<16;i++) if(pb->dec_chlist[i] != pb->ch_list[i]) return 0;
    return 1;
}

/* 为 ch_list 每通道开解码器绑格(defer 批一次成图),**不启 feeder**。须已持锁。
 * - 解码预算与 liveView 同一套(746 Mpix/s):按列表顺序准入,累计超预算即**停止**后续(留黑)。
 * - 多宫格(disp_mode>1)默认用**子流**回放(与 live 多格一致、开销小);单格/全屏用主流。
 * - 无录像的通道跳过(该格留黑),不影响其它路。 */
static void pb_open_decoders_locked(nvr_playback_t *pb)
{
    pb->nth = 0;
    int nch = pb->ch_count > 0 ? pb->ch_count : 1;
    if(nch > PB_MAX_FEED) nch = PB_MAX_FEED;
    /* ★ 码流选择:只有 displayMode==0(全屏)放**主码流**;其余(1宫格及多宫格)都放**子码流**。 */
    int want_stream = (pb->disp_mode == 0) ? NVR_STREAM_MAIN : NVR_STREAM_SUB;
    pb->want_stream = want_stream;
    double budget = mhal_budget_total(), used = 0;

    /* ★ 全局单一使用者:开回放解码器前,活跃解码器应为 0(live 全停)。>0 = 有残留 live 解码器
     *   竞争硬件 → 4 路回放 + 残留 → DEC_HW_TIMEOUT。此处强制再关一遍所有 live 解码,并记数诊断。 */
    if(pb->cfg.sm) for(int i=0;i<PB_MAX_CH;i++) nvr_stream_set_display(pb->cfg.sm, i, -1);
    NVR_LOGI("pb","回放开解码前:活跃解码器 %d(应为0=live已全停)", mhal_vdec_active_count());

    mhal_vout_defer_begin();     /* 批量开解码器:一次 stop_list→start_list 成图(避免逐路"can't extend") */
    for(int i=0;i<nch;i++){
        int chn0 = (pb->ch_count>0 && pb->ch_list[i]>0) ? pb->ch_list[i]-1
                 : (pb->chn0>=0 ? pb->chn0 : i);
        if(chn0 < 0) continue;
        int codec = -1;
        if(pb_peek_codec(pb, chn0, want_stream, pb->start_wall, &codec) != 0){
            NVR_LOGW("pb","chn%d 起播 %u 无录像(该格留黑)", chn0+1, pb->start_wall);
            continue;
        }
        int w=0,ht=0,fps=0; nvr_stream_dim(pb->cfg.sm, chn0, want_stream, &w,&ht,&fps);
        double cost = mhal_budget_cost(w, ht, fps);
        if(cost > 0 && used + cost > budget){
            NVR_LOGW("pb","预算已满(%.0f/%.0f Mpix/s):通道 %d 及之后不回放(留黑)",
                     used/1e6, budget/1e6, chn0+1);
            break;
        }
        mhal_codec_t mc = (codec==RSDK_CODEC_H265)?MHAL_CODEC_H265:MHAL_CODEC_H264;
        NVR_LOGI("pb","chn%d 回放开解码 %s codec=%s %dx%d@%d 窗%d", chn0+1,
                 want_stream==NVR_STREAM_SUB?"子":"主", codec==RSDK_CODEC_H265?"H265":"H264", w, ht, fps, i);
        mhal_vdec_t *vd=NULL;
        /* ★ 用宫格窗号 i 开(bind_vout_win=i,vout_win≥0):commit 的 start_list 只收 vout_win≥0 的路,
         *   此刻就 bind_rect(会置 vout_win=-2)会被排除 → 解码器不启 → 送流 -33。故成图后再 bind_rect。 */
        int orc = mhal_vdec_open(chn0, mc, w, ht, fps, i, &vd);
        if(orc!=0 || !vd){ NVR_LOGE("pb","chn%d 开解码失败 %d(该格留黑)", chn0+1, orc); continue; }
        used += cost;
        pb_feeder_ctx_t *fc = &pb->fctx[pb->nth];
        fc->pb = pb; fc->chn0 = chn0; fc->cell_idx = i; fc->want_stream = want_stream;
        fc->vdec = vd; fc->thread_ok = 0;
        pb->nth++;
    }
    mhal_vout_defer_end();       /* 一次成图:所有回放解码器同时 start_list 启好(vout_win≥0 才收) */
    { int dw=0,dh=0; mhal_vout_get_resolution(&dw,&dh);
      NVR_LOGI("pb","回放宫格计算:屏 %dx%d mode=%d 视频区 %dx%d", dw,dh, pb->disp_mode,
               (pb->disp_mode==0)?dw:dw*4/5, (pb->disp_mode==0)?dh:dh*4/5); }
    for(int k=0;k<pb->nth;k++){  /* 成图后精确摆到本格(bind_rect,已在 started 集合) */
        int cx,cy,cw,chh; pb_cell_rect(pb, pb->fctx[k].cell_idx, &cx,&cy,&cw,&chh);
        NVR_LOGI("pb","chn%d 格%d → 矩形(%d,%d %dx%d)", pb->fctx[k].chn0+1, pb->fctx[k].cell_idx, cx,cy,cw,chh);
        mhal_vout_bind_rect(pb->fctx[k].chn0, cx, cy, cw, chh);
    }
    pb->dec_open = (pb->nth>0);
    pb->dec_n = pb->ch_count;
    for(int i=0;i<pb->ch_count && i<16;i++) pb->dec_chlist[i] = pb->ch_list[i];
}

/* 启 feeder 喂帧(解码器须已开好)。须已持锁。 */
static void pb_start_feeders_locked(nvr_playback_t *pb)
{
    if(pb->nth<=0){ pb->running=0; snprintf(pb->status,sizeof(pb->status),"stopped"); return; }
    pb->running = 1;
    pb->alive_feeders = pb->nth;              /* 存活计数;归 0 = 全放完 → 停 */
    pb->max_decoded_wall = pb->start_wall;    /* 时间轴 clamp 起点 */
    for(int k=0;k<pb->nth;k++)
        pb->fctx[k].thread_ok = (pthread_create(&pb->th[k], NULL, pb_feeder, &pb->fctx[k]) == 0);
    snprintf(pb->status,sizeof(pb->status),"playing");
}

/* 停并 join 所有 feeder(**解码器留着**供 seek 复用)。须已持锁。 */
static void pb_stop_feeders_locked(nvr_playback_t *pb)
{
    pb->running = 0;
    for(int i=0;i<pb->nth;i++)
        if(pb->fctx[i].thread_ok){ pthread_join(pb->th[i], NULL); pb->fctx[i].thread_ok = 0; }
}

/* 停 feeder + defer 批统一关解码器(一次成图收窗)。须已持锁。 */
static void pb_close_decoders_locked(nvr_playback_t *pb)
{
    pb_stop_feeders_locked(pb);
    if(pb->nth>0){
        mhal_vout_defer_begin();
        for(int i=0;i<pb->nth;i++)
            if(pb->fctx[i].vdec){ mhal_vdec_close(pb->fctx[i].vdec); pb->fctx[i].vdec=NULL; }
        mhal_vout_defer_end();
    }
    pb->nth = 0; pb->dec_open = 0; pb->dec_n = 0;
}

/* 进入回放模式:接管全屏窗口并**黑屏**(关 live 解码 + 清黑),不显示 LiveView。须已持锁。 */
static void pb_blackout_locked(nvr_playback_t *pb, int chn0)
{
    /* 停**所有**通道的 live 解码 → 整屏让位给回放;清黑(默认先黑,play 才喂录像)。
     * ★ 回放期间是**独占**模式:live 不再解码(preview 已由 setPlaybackMode 置 display_mode=0,
     *   通道上线也不会重开 live),回 live 只由 setDeviceDisplayMode 触发。 */
    if(pb->cfg.sm) for(int i=0;i<PB_MAX_CH;i++) nvr_stream_set_display(pb->cfg.sm, i, -1);
    mhal_vout_clear_black();
    if(chn0>=0) pb->chn0 = chn0;
}

/* 停回放(停 feeder + 关解码器)+ 黑屏(**留在回放模式**;回 live 只由 setDeviceDisplayMode 触发)。须已持锁。 */
static void pb_stop_locked(nvr_playback_t *pb)
{
    pb_close_decoders_locked(pb);
    mhal_vout_clear_black();
    pb->chn0 = -1;
    snprintf(pb->status,sizeof(pb->status),"stopped");
}

/* 进入回放模式黑屏(供 GUI_setPlaybackMode:一进回放就黑,不显示 live)。 */
void nvr_playback_enter(nvr_playback_t *pb, int chn1)
{
    if(!pb) return;
    int chn0 = (chn1>0)? chn1-1 : (pb->chn0>=0?pb->chn0:0);
    pthread_mutex_lock(&pb->lock);
    pb_close_decoders_locked(pb);
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status,sizeof(pb->status),"stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_create(const nvr_playback_cfg_t *cfg, nvr_playback_t **out)
{
    if(!cfg||!out) return -1;
    nvr_playback_t *pb = (nvr_playback_t*)calloc(1,sizeof(*pb));
    if(!pb) return -1;
    pb->cfg = *cfg; pb->chn0=-1; pb->want_stream=NVR_STREAM_MAIN; pb->speed_num=1;
    snprintf(pb->status,sizeof(pb->status),"stopped");
    snprintf(pb->speed,sizeof(pb->speed),"1X");
    snprintf(pb->direction,sizeof(pb->direction),"forward");
    pthread_mutex_init(&pb->lock,NULL);
    *out = pb; return 0;
}

void nvr_playback_destroy(nvr_playback_t *pb)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock); pb_stop_locked(pb); pthread_mutex_unlock(&pb->lock);
    pthread_mutex_destroy(&pb->lock); free(pb);
}

int nvr_playback_control(nvr_playback_t *pb, const char *action, int chn1,
                         uint32_t start_wall, const char *speed, const char *direction)
{
    if(!pb||!action) return -1;
    pthread_mutex_lock(&pb->lock);
    if(speed&&speed[0])    { snprintf(pb->speed,sizeof(pb->speed),"%s",speed); pb->speed_num=parse_speed(speed); }
    if(direction&&direction[0]){ snprintf(pb->direction,sizeof(pb->direction),"%s",direction);
                                 pb->backward = (strcmp(direction,"backward")==0); }

    int is_pause = (strcmp(action,"pause")==0);
    int is_stop  = (strcmp(action,"stop")==0);
    int is_play  = (strcmp(action,"play")==0) || (strcmp(action,"start")==0) || (strcmp(action,"seek")==0);
    /* resume(从暂停继续):显式 resume,或 play 但已在放且暂停中且未带新起点(不重开文件,仅继续) */
    int is_resume= (strcmp(action,"resume")==0) ||
                   (is_play && pb->nth>0 && pb->paused && start_wall==0);

    if(is_pause){
        if(pb->nth>0 && !pb->paused){ pb->paused=1; pb->pause_at_ms=now_ms();  /* 冻结共享时钟 */
            snprintf(pb->status,sizeof(pb->status),"paused"); }
    } else if(is_stop){
        pb_stop_locked(pb);
    } else if(is_resume){
        if(pb->paused){ if(pb->pause_at_ms){ pb->play_base_ms += now_ms()-pb->pause_at_ms; pb->pause_at_ms=0; }
                        pb->paused=0; }
        snprintf(pb->status,sizeof(pb->status),"playing");
    } else if(is_play){  /* play / start / seek → 起播/换起点:ch_list 每通道各放各格 */
        int chn0 = (chn1>0)? chn1-1 : (pb->ch_count>0 ? pb->ch_list[0]-1 : pb->chn0);
        uint32_t sw = (start_wall>0)? start_wall : pb->start_wall;
        if(sw==0 || (chn0<0 && pb->ch_count<=0)){ pthread_mutex_unlock(&pb->lock); return -1; }
        pb_stop_feeders_locked(pb);              /* 停 feeder(解码器留着) */
        pb->start_wall = sw;                     /* 供 peek/feeder 起点 */
        if(!pb_chlist_same(pb)){                 /* 通道集变了才重建 8K 解码器(否则 seek 复用,免 -1/churn) */
            pb_close_decoders_locked(pb);
            pb_blackout_locked(pb, chn0);
            pb_open_decoders_locked(pb);
        }
        /* ★ 每次 play 都按当前 disp_mode 重绑每格矩形(即使复用解码器):切布局后画面必落到新宫格,
         *   不残留旧位置(liveView 按序填满,回放也须每次刷新格位)。 */
        for(int k=0;k<pb->nth;k++){
            int cx,cy,cw,chh; pb_cell_rect(pb, pb->fctx[k].cell_idx, &cx,&cy,&cw,&chh);
            mhal_vout_bind_rect(pb->fctx[k].chn0, cx, cy, cw, chh);
        }
        pb->want_stream = (pb->disp_mode==0)?NVR_STREAM_MAIN:NVR_STREAM_SUB;
        pb->paused = 0; pb->pause_at_ms = 0;
        pb->play_base_ms = now_ms(); pb->play_base_wall = sw;   /* 共享时钟基准(首出帧可能重锚到实际位置) */
        pb->clock_reanchored = 0;
        pb->cur_wall = sw;
        pb_start_feeders_locked(pb);             /* 拉 feeder 喂帧(复用/新建的解码器) */
    }
    /* status / 未知 action:仅回当前状态(下面统一取),不动引擎 —— 关键:status 轮询不得重起播! */
    pthread_mutex_unlock(&pb->lock);
    return 0;
}

void nvr_playback_set_mode(nvr_playback_t *pb, int display_mode, const int *channels1, int n)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock);
    pb->disp_mode = display_mode;
    if(n>16) n=16; if(n<0) n=0;
    pb->ch_count = n;
    for(int i=0;i<n;i++) pb->ch_list[i] = channels1?channels1[i]:0;
    /* 通道/布局变了 → 关旧解码器(下次 play 按新宫格重开)+ 黑屏接管;主放通道=列表首个 */
    pb_close_decoders_locked(pb);
    int chn0 = (n>0 && pb->ch_list[0]>0)? pb->ch_list[0]-1 : (pb->chn0>=0?pb->chn0:0);
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status,sizeof(pb->status),"stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_get_mode(nvr_playback_t *pb, int *channels_out, int cap, int *n)
{
    if(!pb) return 4;
    pthread_mutex_lock(&pb->lock);
    int dm = pb->disp_mode>0?pb->disp_mode:1;
    int cnt = pb->ch_count>0?pb->ch_count:1;
    if(cnt>cap) cnt=cap;
    for(int i=0;i<cnt;i++) channels_out[i] = pb->ch_count>0?pb->ch_list[i]:(i+1);
    if(n) *n=cnt;
    pthread_mutex_unlock(&pb->lock);
    return dm;
}

void nvr_playback_get_status(nvr_playback_t *pb, char *status, char *speed,
                             char *direction, uint32_t *cur_wall)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock);
    /* 所有 feeder 已退出(放完/无录像)→ 报 stopped(即使 pb->status 还写着 playing) */
    int ended = (pb->nth>0 && pb->alive_feeders<=0 && !pb->paused);
    if(status)    snprintf(status,16,"%s", ended ? "stopped" : pb->status);
    if(speed)     snprintf(speed,16,"%s",pb->speed);
    if(direction) snprintf(direction,16,"%s",pb->direction);
    if(cur_wall){
        /* ★ 时间轴 = **共享时钟**(平滑、不跳)但 clamp 到**已解码位置** max_decoded:
         *   - 连续有图:共享时钟 ≤ 已解码 → 取共享时钟(平滑);
         *   - 空档/放完:已解码停住 → 时间轴停在那(不空走 = 无录像不前进,修"无录像播放")。 */
        uint32_t cur;
        int speed_n = pb->speed_num>0?pb->speed_num:1;
        if(pb->nth<=0 || pb->play_base_ms==0) cur = pb->start_wall;
        else {
            long ref = pb->pause_at_ms ? pb->pause_at_ms : now_ms();
            long el = ref - pb->play_base_ms; if(el<0) el=0;
            uint32_t sc = pb->play_base_wall + (uint32_t)((el/1000)*speed_n);   /* 共享时钟 */
            uint32_t md = pb->max_decoded_wall;
            cur = (sc < md) ? sc : md;                                          /* clamp 到已解码 */
        }
        *cur_wall = cur;
    }
    pthread_mutex_unlock(&pb->lock);
}
