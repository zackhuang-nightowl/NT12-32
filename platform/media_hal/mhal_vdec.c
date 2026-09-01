/***************************************************************************************
 *  mhal_vdec.c — 视频硬解（封装 hd_videodec + videoproc 桥接到 videoout）
 *
 *  管线：hd_videodec_open/bind/set → hd_videoproc → hd_videoout；
 *        送帧 hd_videodec_send_list；取图 hd_videodec_pull_out_buf（抓拍用）。
 *  参照 hdal 样例 playback_1div_to_4div.c 的 open/bind/set_module_param/send 流程。
 *  ⚠️ 仅在 na51090 BSP 交叉环境编译。
 ***************************************************************************************/
#include <pthread.h>
#include "mhal_internal.h"
#include "mhal_budget.h"
#include "nvr_log.h"
#include "vendor_videodec.h"    /* VENDOR_VIDEODEC_PARAM_SUB_YUV_RATIO —— 8K 解码器下采样输出 */
#include "vendor_videoproc.h"   /* VENDOR_VIDEOPROC_SUB_RATIO_THLD —— videoproc 用 sub-yuv 的阈值 */
#include "vendor_common.h"      /* vendor_common_get_ddrid —— 按 dts 取各内存池实际所在 DDR 通道 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 解码维度宽超此值(8K 主码流 7680) → 启用 sub-yuv 下采样:解码器直接输出 1/2 小图,videoproc 只需
 * 缩小图(默认 VPE 输入/缩放器约 4K,直接喂 7680 处理不了、不出图)。4K 及以下不启用(直路更省)。 */
#define MHAL_SUBYUV_W_THLD 4096

/* 单实例最大帧尺寸（数据手册：分辨率 up to 8192×8192）；未知分辨率时的兜底上限 */
#define MHAL_DEC_MAX_W 8192
#define MHAL_DEC_MAX_H 8192
#define MHAL_DEC_DFLT_W 1920      /* w/h 未知时按此预分配（覆盖绝大多数主码流）*/
#define MHAL_DEC_DFLT_H 1088

void mhal_vdec_teardown(struct mhal_vdec *d);   /* 本文件后面定义(同批 reopen 回收用) */
static void vdec_budget_free_once(struct mhal_vdec *d);   /* prebuild reuse/hide 提前用,定义在后 */

/* ★ 解码内存池的 DDR 通道:dts(cfg_NVR_16CH)把 DISP_DEC_IN 放 DDR0、DISP_DEC_OUT 与
 * DISP_DEC_OUT_RATIO 放 DDR1(两者在 DDR0 为 0/禁用)。data_pool 的 ddr_id 若不按 dts 实际位置
 * 取,尤其 sub-yuv 的 RATIO 池默认落到 DDR0(那里没有 RATIO 池)→ 解码器产不出下采样图 → 无图。
 * 只解析一次并缓存(池布局启动即定)。 */
static int g_ddr_resolved = 0;
static HD_COMMON_MEM_DDR_ID g_ddr_dec_in, g_ddr_dec_out, g_ddr_dec_out_ratio;
static void resolve_dec_ddrid(void)
{
    if (g_ddr_resolved) return;
    g_ddr_dec_in = g_ddr_dec_out = g_ddr_dec_out_ratio = DDR_ID0;
    vendor_common_get_ddrid(HD_COMMON_MEM_DISP_DEC_IN_POOL,        COMMON_PCIE_CHIP_RC, &g_ddr_dec_in);
    vendor_common_get_ddrid(HD_COMMON_MEM_DISP_DEC_OUT_POOL,       COMMON_PCIE_CHIP_RC, &g_ddr_dec_out);
    vendor_common_get_ddrid(HD_COMMON_MEM_DISP_DEC_OUT_RATIO_POOL, COMMON_PCIE_CHIP_RC, &g_ddr_dec_out_ratio);
    g_ddr_resolved = 1;
    NVR_LOGI("vdec", "解码池 DDR: dec_in=%d dec_out=%d dec_out_ratio=%d(sub-yuv)",
             g_ddr_dec_in, g_ddr_dec_out, g_ddr_dec_out_ratio);
}

static int align16(int v) { return (v + 15) & ~15; }
/* H264/H265 硬解要求解码维度按 **64×64 对齐**(宏块/CTU)。否则内核报
 * "DST_BG_DIM(WxH) is not match alignment with H.264(64x64)" → vdodec_putjob fail、解不出帧。
 * 例:子码流 1280x720 的 720 非 64 倍数 → 必须补到 768。 */
static int align64(int v) { return (v + 63) & ~63; }

/* 主码流宽 > 阈值(8K 7680) → 该路需走 sub-yuv 下采样（解码器额外输出 1/2 小图）。d->w 已 64 对齐。 */
static int mhal_vdec_need_subyuv(const struct mhal_vdec *d)
{
    return d->w > MHAL_SUBYUV_W_THLD;
}

/* 配置一条解码路径的内存/编解码参数 —— **按实际分辨率动态分配** max_mem(维度 64 对齐)。
 * 8K 主码流(宽 > MHAL_SUBYUV_W_THLD)额外开 data_pool[1] 收 sub-yuv(下采样)输出，并设解码器
 * SUB_YUV_RATIO=2x：解码器直接吐 1/2 小图(7680→3840，≤VPE 4K 输入上限)供 videoproc 缩放。
 * 参照样例 playback_with_sub_ratio.c(CONFIG_SUB_RATIO 分支)。 */
static int cfg_dec_path(struct mhal_vdec *d)
{
    int w = d->w > 0 ? align64(d->w) : align64(MHAL_DEC_DFLT_W);
    int h = d->h > 0 ? align64(d->h) : align64(MHAL_DEC_DFLT_H);
    /* ★ prebuild:每路 max_mem 至少按预留档 768×576 分配 → ≤预留的实际流(640x360/704x576 等主力)零重配直接喂。
     * 超预留的流(1280x720/1080x1080/4K 主)由 mhal_vdec_reconfig 先把 d->w/h 设为实际(>预留)再进本函数,
     * 自然按实际分配(只重配那一路)。 */
    if (w < MHAL_DEC_RSV_W) w = MHAL_DEC_RSV_W;
    if (h < MHAL_DEC_RSV_H) h = MHAL_DEC_RSV_H;
    if (w > MHAL_DEC_MAX_W) w = MHAL_DEC_MAX_W;
    if (h > MHAL_DEC_MAX_H) h = MHAL_DEC_MAX_H;
    d->cfg_w = w; d->cfg_h = h;   /* 记录已分配 max_mem 对齐尺寸(open 判定是否需重配) */

    int subyuv = mhal_vdec_need_subyuv(d);

    HD_VIDEODEC_PATH_CONFIG dec = {0};
    dec.max_mem.codec_type  = d->codec;
    dec.max_mem.frame_rate  = d->fps > 0 ? d->fps : 30;
    dec.max_mem.dim.w       = w;            /* 动态：单宫格 4K 就分 4K，子码流就分 640×360 */
    dec.max_mem.dim.h       = h;
    dec.max_mem.bs_counts   = 8;
    dec.max_mem.max_ref_num = 2;
    dec.max_mem.max_bitrate = 8 * 1024 * 1024;
    resolve_dec_ddrid();
    dec.max_mem.ddr_id      = g_ddr_dec_in;
    dec.data_pool[0].mode   = HD_VIDEODEC_POOL_ENABLE;   /* 主(全分辨率)输出池 */
    dec.data_pool[0].ddr_id = g_ddr_dec_out;
    dec.data_pool[0].counts = HD_VIDEODEC_SET_COUNT(3, 0);
    if (subyuv) {
        /* ★ 关键①:比特流窗口(单帧最大)。0=AUTO 由 max_bitrate(8Mbps)/fps 推 → ~50KB,远小于 8K
         * IDR(实测 ~567KB) → 送流 hd_videodec_send_list 时 VPD_PUT_COPY_MULTI_DIN 拷不进 DIN、失败,
         * 帧被丢、不出图。给 2MB 窗口装下 IDR。但 **DIN 池(部署 dtb 的 DISP_DEC_IN)只有 20.3MB**,
         * 4MB×bs_counts=8=32MB 会撑爆池→分配失败→无图。故 2MB×4=8MB,稳在池内(样例 transcode 给 1MB)。 */
        dec.max_mem.max_bs_size = 2 * 1024 * 1024;
        dec.max_mem.bs_counts   = 4;   /* 8K:少而大的比特流缓冲,4×2MB=8MB ≤ 20MB DIN 池 */
        /* sub-yuv(下采样)输出用 RATIO 池 —— 样例的 disp_dec_out_ratio 池(DDR1)。 */
        dec.data_pool[1].mode   = HD_VIDEODEC_POOL_ENABLE;
        dec.data_pool[1].ddr_id = g_ddr_dec_out_ratio;
        dec.data_pool[1].counts = HD_VIDEODEC_SET_COUNT(3, 0);
    } else if ((long)w * h > 1920L * 1080) {
        /* ★ 关键①(非 8K 的大主码流):4K / 12MP(如 4000×3000,宽 ≤4096 未触发 subyuv,VPE 可直吃)。
         * 单帧 IDR 极大(4000×3000 实测 ~455KB),远超 AUTO 比特流窗口(max_bitrate/fps ≈ 50KB)→ 送流时
         * VPD_PUT_COPY_MULTI_DIN 拷不进 DIN、**整批送流失败**,把**共享 DIN 池**堵死 → 同批别路一起丢帧变黑
         * (真机实证:4000×3000 主流单宫格解码时 ch23 被拖黑)。大主流**只在单宫格解**(并发路数少,DIN 不争用),
         * 给足 2MB 窗口装下 IDR;bs_counts=4 → 4×2MB=8MB ≤ DIN 池(部署 20.3MB)。小子流(宫格多路)保持 AUTO
         * 小窗守住池(已验证 24 路稳),不受影响。 */
        dec.max_mem.max_bs_size = 2 * 1024 * 1024;
        dec.max_mem.bs_counts   = 4;
    } else {
        /* ★ 普通路(≤1080p,含预留档 768×576 与 1280x720/1080x1080 离群子流):比特流窗口按分辨率给,
         * bs_counts 压到 3 以守住 **32 路** 的 DIN 池(部署 DISP_DEC_IN=20.3MB)。
         * 预留档 768×576 → 128KB×3=384KB/路 × 32 = ~12.3MB;离群路自适应重配时按更大档,留有余量。
         * (AUTO ~50KB 窗口对 704x576/1280x720 的 IDR(实测 64~100KB)会拷不进 DIN → 必须给足) */
        long px = (long)w * h;
        dec.max_mem.max_bs_size = (px > 1280L*720) ? (512*1024)
                                : (px > 768L*576)  ? (320*1024)
                                :                    (128*1024);
        dec.max_mem.bs_counts   = 3;
    }

    HD_RESULT ret = hd_videodec_set(d->dec_path, HD_VIDEODEC_PARAM_PATH_CONFIG, &dec);
    if (ret != HD_OK) return ret;

    if (subyuv) {
        /* 8K 直喂 VPE 处理不了(缩放器 ~4K 输入)：命解码器额外输出 1/2 小图。
         * 8192/2 = 4096 ≤ VPE 上限，故宽在 (4096,8192] 一律 2x 足矣。 */
        VENDOR_VIDEODEC_SUB_YUV sub_yuv = { .ratio = VENDOR_SUB_RATIO_2x };
        ret = vendor_videodec_set(d->dec_path, VENDOR_VIDEODEC_PARAM_SUB_YUV_RATIO, &sub_yuv);
        if (ret != HD_OK) {
            NVR_LOGE("vdec", "chn%d 设 SUB_YUV_RATIO(2x) 失败 %d(8K 下采样)", d->chn, ret);
            return ret;
        }
        NVR_LOGI("vdec", "chn%d 8K 主码流(%dx%d)：启用 sub-yuv 2x 下采样(解码器直吐 %dx%d 供 VPE)",
                 d->chn, w, h, w / 2, h / 2);
    }
    return ret;
}

/* 为 8K 路的 videoproc 设 sub-yuv 采用阈值：主图缩小比超 numer/denom 时改用解码器的 sub-yuv。
 * 8K 主图(7680)超 VPE 输入上限、无论何种宫格都必用 sub，故设 {6,5}(=1.2，任何下采样即触发，
 * 同样例)。仅对 sub-yuv 生效的 8K 路设置——4K 路无 sub 输出，若强制用 sub 反致无图。 */
static void cfg_proc_subyuv_thld(struct mhal_vdec *d)
{
    if (!d->proc_path || !mhal_vdec_need_subyuv(d)) return;
    VENDOR_VDOPROC_SUB_RATIO_THLD thld = { .numer = 6, .denom = 5 };
    HD_RESULT ret = vendor_videoproc_set(d->proc_path, VENDOR_VIDEOPROC_SUB_RATIO_THLD, &thld);
    if (ret != HD_OK)
        NVR_LOGE("vdec", "chn%d 设 videoproc SUB_RATIO_THLD 失败 %d(8K sub-yuv 采用阈值)", d->chn, ret);
}

/* ★ 开机预建全窗:一次性 open+bind+start N 路 dec/proc/vout 进**同一张**合成图(单 large graph),
 * 解码器按预留档 768×576 分配 max_mem,全部 visible=0。此后运行期加/删/切设备只逐窗 IN_WIN_ATTR
 * (visible/rect),永不再整图 stop_list/start_list → "一路问题只影响一路"。见官方 playback_1div_to_4div.c。 */
int mhal_vdec_prebuild(int n)
{
    if (!g_disp.inited) return -1;
    if (n <= 0 || n > MHAL_MAX_CH) n = MHAL_MAX_CH;
    mhal_lock();
    if (g_disp.prebuilt) { mhal_unlock(); return 0; }
    resolve_dec_ddrid();
    HD_PATH_ID dec[MHAL_MAX_CH], proc[MHAL_MAX_CH], vout[MHAL_MAX_CH];
    int m = 0;
    for (int chn = 0; chn < n; chn++) {
        struct mhal_vdec *d = calloc(1, sizeof(*d));
        if (!d) break;
        HD_RESULT ret; int step = 0;
        d->chn = chn;
        d->codec = HD_CODEC_TYPE_H265;   /* 默认 H265;实际流为 H264 → open 时自适应重配该路 */
        d->w = 0; d->h = 0; d->fps = 30; /* cfg_dec_path 按预留档 768×576 分配 */
        d->vout_win = -1; d->active = 0; d->budget_released = 1;
        step=1; if ((ret=hd_videodec_open(HD_VIDEODEC_IN(0,chn), HD_VIDEODEC_OUT(0,chn), &d->dec_path))!=HD_OK) goto pfail;
        step=2; if ((ret=hd_videoproc_open(HD_VIDEOPROC_IN(chn,0), HD_VIDEOPROC_OUT(chn,0), &d->proc_path))!=HD_OK) goto pfail;
        step=3; if ((ret=hd_videoout_open(HD_VIDEOOUT_IN(0,chn), HD_VIDEOOUT_OUT(0,0), &d->vout_path))!=HD_OK) goto pfail;
        step=4; if ((ret=hd_videodec_bind(HD_VIDEODEC_OUT(0,chn), HD_VIDEOPROC_IN(chn,0)))!=HD_OK) goto pfail;
        step=5; if ((ret=hd_videoproc_bind(HD_VIDEOPROC_OUT(chn,0), HD_VIDEOOUT_IN(0,chn)))!=HD_OK) goto pfail;
        step=6; if ((ret=cfg_dec_path(d))!=HD_OK) goto pfail;
        d->opened = 1;
        g_disp.ch[chn] = d;
        dec[m]=d->dec_path; proc[m]=d->proc_path; vout[m]=d->vout_path; m++;
        continue;
pfail:
        NVR_LOGE("mhal", "prebuild chn%d fail step%d ret=%d(池不足?)→ 预建到此共 %d 路", chn, step, ret, m);
        if (d->vout_path) hd_videoout_close(d->vout_path);
        if (d->proc_path) hd_videoproc_close(d->proc_path);
        if (d->dec_path)  hd_videodec_close(d->dec_path);
        free(d);
        break;
    }
    HD_RESULT r = HD_OK;
    if (m > 0 &&
        (r=hd_videodec_start_list(dec, m))==HD_OK &&
        (r=hd_videoproc_start_list(proc, m))==HD_OK &&
        (r=hd_videoout_start_list(vout, m))==HD_OK) {
        memcpy(g_disp.started_dec,  dec,  m*sizeof(HD_PATH_ID));
        memcpy(g_disp.started_proc, proc, m*sizeof(HD_PATH_ID));
        memcpy(g_disp.started_vout, vout, m*sizeof(HD_PATH_ID));
        g_disp.started_n = m;
        g_disp.commit_gen++;
        for (int chn = 0; chn < n; chn++) {                 /* 全部预建窗初始隐藏 */
            struct mhal_vdec *d = g_disp.ch[chn];
            if (!d) continue;
            HD_VIDEOOUT_WIN_ATTR wa; memset(&wa,0,sizeof(wa)); wa.visible = 0;
            hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &wa);
        }
        g_disp.prebuilt = 1;
        NVR_LOGI("mhal", "★ 预建全窗成功:%d 路 dec/proc/vout 常驻(单图);运行期加/删/切只逐窗 visible/rect,不整图重建", m);
    } else if (m > 0) {
        NVR_LOGE("mhal", "★ 预建 start_list 失败 %d(%d 路)→ 回退按需模式(未预建)。多半是 proc/vout 池装不下这么多路。", r, m);
        for (int chn = 0; chn < n; chn++) {                 /* 起图失败:全拆回退到原按需 open 模式 */
            struct mhal_vdec *d = g_disp.ch[chn];
            if (!d) continue;
            mhal_vdec_teardown(d);
            g_disp.ch[chn] = NULL;
        }
        g_disp.started_n = 0;
    }
    mhal_vout_clear_black();
    mhal_unlock();
    return g_disp.prebuilt ? 0 : -1;
}

/* ★ prebuild 下的"激活已建窗":不开路径、不整图重建。预算准入挪到此(Σ≤995);实际流 ≤ 已分配 max_mem
 * → 零重配直接喂;codec 变 或 尺寸超已分配 → 只重配这一路(stop dec→cfg→start dec,proc/vout 不动)。
 * 最后 apply_window(逐窗 visible/rect)。在 mhal_lock 内调用。 */
static int mhal_vdec_reuse(int chn, mhal_codec_t codec, int w, int h, int fps,
                           int win, mhal_vdec_t **out)
{
    struct mhal_vdec *d = g_disp.ch[chn];
    HD_VIDEO_CODEC hc = (codec == MHAL_CODEC_H265) ? HD_CODEC_TYPE_H265 : HD_CODEC_TYPE_H264;
    int nw = align64(w > 0 ? w : MHAL_DEC_RSV_W);
    int nh = align64(h > 0 ? h : MHAL_DEC_RSV_H);
    /* 预算:先释放旧(用旧 d->w/h)、再按新 reserve。超 995 → 拒该路(仅录不显),解码器保持隐藏。 */
    vdec_budget_free_once(d);
    if (mhal_budget_try_reserve(nw, nh, fps) != 0) {
        d->budget_released = 1; d->active = 0;
        NVR_LOGE("vdec", "chn%d 解码预算超限:需 %.1f/已用 %.1f/%.1f Mpix/s → 仅录不显", chn,
                 mhal_budget_cost(nw,nh,fps)/1e6, mhal_budget_used()/1e6, mhal_budget_total()/1e6);
        return MHAL_VDEC_EBUDGET;
    }
    d->budget_released = 0;
    int need = (hc != d->codec) || (nw > d->cfg_w) || (nh > d->cfg_h);
    d->codec = hc; d->w = nw; d->h = nh; d->fps = fps;
    if (need) {
        hd_videodec_stop(d->dec_path);
        HD_RESULT r = cfg_dec_path(d);           /* 按实际(可能 >预留)重分配 max_mem+比特流窗口 */
        cfg_proc_subyuv_thld(d);
        if (r == HD_OK) r = hd_videodec_start(d->dec_path);
        if (r != HD_OK) {
            NVR_LOGE("vdec", "chn%d 自适应重配 %dx%d 失败 %d", chn, nw, nh, r);
            vdec_budget_free_once(d); d->active = 0;
            return -1;
        }
        NVR_LOGI("vdec", "chn%d 自适应重配解码器 → %dx%d(超预留,只重建该路,不碰别路)", chn, nw, nh);
    }
    mhal_crop_apply_pending(d);
    d->active = 1; d->vout_win = win;
    *out = d;
    if (win >= 0) {
        mhal_vout_bind(win, chn);            /* apply_window:逐窗 proc OUT rect + vout IN_WIN_ATTR(visible=1,rect) */
    } else {
        HD_VIDEOOUT_WIN_ATTR wa; memset(&wa,0,sizeof(wa)); wa.visible = 0;
        hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &wa);
    }
    return 0;
}

int mhal_vdec_open(int chn, mhal_codec_t codec, int w, int h, int fps,
                   int bind_vout_win, mhal_vdec_t **out)
{
    if (!g_disp.inited || chn < 0 || chn >= MHAL_MAX_CH) return -1;

    /* 帧尺寸上限校验（数据手册 8192×8192）*/
    if (w > MHAL_DEC_MAX_W || h > MHAL_DEC_MAX_H) {
        NVR_LOGE("vdec", "chn%d 分辨率 %dx%d 超单实例上限 %dx%d，拒绝解码",
                 chn, w, h, MHAL_DEC_MAX_W, MHAL_DEC_MAX_H);
        return MHAL_VDEC_EBUDGET;
    }

    /* ★ prebuild:预建全窗后,open = "激活已建窗"(自适应重配 + 逐窗 visible),不再开路径/不整图重建。 */
    if (g_disp.prebuilt && g_disp.ch[chn] && g_disp.ch[chn]->opened) {
        mhal_lock();
        int rc = mhal_vdec_reuse(chn, codec, w, h, fps, bind_vout_win, out);
        mhal_unlock();
        return rc;
    }

    /* ★ 吞吐预算准入：算上这一路是否超 746 Mpix/s；超了就**不解码最新这一路** */
    if (mhal_budget_try_reserve(w, h, fps) != 0) {
        NVR_LOGE("vdec", "chn%d 解码预算超限：需 %.1f Mpix/s，已用 %.1f/%.1f Mpix/s → 拒绝该路预览(录像不受影响)",
                 chn, mhal_budget_cost(w, h, fps) / 1e6,
                 mhal_budget_used() / 1e6, mhal_budget_total() / 1e6);
        return MHAL_VDEC_EBUDGET;
    }

    struct mhal_vdec *d = calloc(1, sizeof(*d));
    if (!d) { mhal_budget_release(w, h, fps); return -1; }
    mhal_lock();                 /* 串行化 g_disp 访问(多通道 rx 线程/预览并发) */
    d->chn      = chn;
    d->codec    = (codec == MHAL_CODEC_H265) ? HD_CODEC_TYPE_H265 : HD_CODEC_TYPE_H264;
    d->vout_win = bind_vout_win;
    /* 维度按 64 对齐存下 —— 硬解目标缓冲/DST_BG_DIM 要求 64×64(否则 720 之类非64倍数报
     * "not match alignment with H.264(64x64)"、解不出帧)。下游 cfg_dec_path/videoproc 全用对齐值。 */
    d->w = align64(w); d->h = align64(h); d->fps = fps;

    /* ★ 残留在显解码器清理(回放↔liveView 切换/上次 open 失败泄漏):本 chn 若仍有活动解码器挂在
     * g_disp.ch[chn](未经 mhal_vdec_close 摘除,仍 started 占着 IN/OUT+vout 窗),下面 hd_videodec_open/
     * videoout_open 复用同号会 ALREADY_OPEN(-25)/busy → 该窗黑屏、且失败路 g_disp.ch[chn]=NULL 会把它
     * 彻底泄漏(永占该窗,后续所有 open 卡死)。先停+摘 started+拆,再往下开新路径。 */
    struct mhal_vdec *cur = g_disp.ch[chn];
    if (cur && cur != d) {
        if (cur->dec_path)  hd_videodec_stop(cur->dec_path);
        if (cur->proc_path) hd_videoproc_stop(cur->proc_path);
        if (cur->vout_path) hd_videoout_stop(cur->vout_path);
        for (int k = 0; k < g_disp.started_n; k++) {
            if (g_disp.started_dec[k] == cur->dec_path) {
                g_disp.started_dec[k]  = g_disp.started_dec[g_disp.started_n - 1];
                g_disp.started_proc[k] = g_disp.started_proc[g_disp.started_n - 1];
                g_disp.started_vout[k] = g_disp.started_vout[g_disp.started_n - 1];
                g_disp.started_n--;
                break;
            }
        }
        g_disp.ch[chn] = NULL;
        mhal_vdec_teardown(cur);
        NVR_LOGI("mhal", "chn%d open:先拆残留在显解码器(回放/失败泄漏,避免 ALREADY_OPEN 黑屏)", chn);
    }

    /* ★ 同批内先关后开(切主/子码流 reopen 同一通道):旧解码器还挂在 pending_free(defer 期间未拆,
     * 路径仍占用),此时下面 hd_videodec_open 复用同一 IN/OUT 号会 ALREADY_OPEN(-25)。先就地停+拆旧:
     * 旧路径上次 commit 已 started → 停之、从 started 集合摘除、拆绑/关/释放,再往下开新路径。 */
    int reopen_freed = 0;
    for (int i = 0; i < g_disp.pending_n; i++) {
        struct mhal_vdec *old = g_disp.pending_free[i];
        if (!old || old->chn != chn) continue;
        if (old->dec_path)  hd_videodec_stop(old->dec_path);
        if (old->proc_path) hd_videoproc_stop(old->proc_path);
        if (old->vout_path) hd_videoout_stop(old->vout_path);
        for (int k = 0; k < g_disp.started_n; k++) {
            if (g_disp.started_dec[k] == old->dec_path) {
                g_disp.started_dec[k]  = g_disp.started_dec[g_disp.started_n - 1];
                g_disp.started_proc[k] = g_disp.started_proc[g_disp.started_n - 1];
                g_disp.started_vout[k] = g_disp.started_vout[g_disp.started_n - 1];
                g_disp.started_n--;
                break;
            }
        }
        mhal_vdec_teardown(old);
        g_disp.pending_free[i] = g_disp.pending_free[g_disp.pending_n - 1];
        g_disp.pending_n--;
        reopen_freed++;
        /* ★ swap-remove 把末尾元素填到了下标 i:必须回退重查, 否则漏检。
         * 且不能 break —— 高频 open/close 抖动下同一 chn 可能残留**多个**未排空实例,
         * 只拆第一个会让其余仍占着 IN(0,chn) → 下次 hd_videoout_open 报 ALREADY_OPEN(-25) → 级联黑屏。 */
        i--;
    }
    if (reopen_freed)
        NVR_LOGI("mhal", "chn%d 同批 reopen:拆旧解码器路径 x%d(避免 ALREADY_OPEN)", chn, reopen_freed);

    HD_RESULT ret; int step = 0;
    /* 1) 开 dec/proc/vout 三段路径（dev 0, id=chn） */
    step=1; if ((ret = hd_videodec_open(HD_VIDEODEC_IN(0, chn), HD_VIDEODEC_OUT(0, chn), &d->dec_path)) != HD_OK) goto fail;
    step=2; if ((ret = hd_videoproc_open(HD_VIDEOPROC_IN(chn, 0), HD_VIDEOPROC_OUT(chn, 0), &d->proc_path)) != HD_OK) goto fail;

    step=3; if (bind_vout_win >= 0 &&
        (ret = hd_videoout_open(HD_VIDEOOUT_IN(0, chn), HD_VIDEOOUT_OUT(0, 0), &d->vout_path)) != HD_OK) goto fail;

    /* 2) 绑定 dec→proc→(vout) */
    step=4; if ((ret = hd_videodec_bind(HD_VIDEODEC_OUT(0, chn), HD_VIDEOPROC_IN(chn, 0))) != HD_OK) goto fail;
    step=5; if (bind_vout_win >= 0 &&
        (ret = hd_videoproc_bind(HD_VIDEOPROC_OUT(chn, 0), HD_VIDEOOUT_IN(0, chn))) != HD_OK) goto fail;

    /* 3) 配置解码路径（内存/codec）；8K 路再给 videoproc 设 sub-yuv 采用阈值(令 VPE 收下采样小图) */
    step=6; if ((ret = cfg_dec_path(d)) != HD_OK) goto fail;
    cfg_proc_subyuv_thld(d);

    /* 4) 登记 + 应用分屏窗口矩形（proc_out.rect + videoout win） */
    g_disp.ch[chn] = d;
    if (bind_vout_win >= 0) mhal_vout_bind(bind_vout_win, chn);

    /* 5) 起流：
     *  - 在显通道(bind_vout_win>=0)：**不逐路单启** hd_videoout_start —— 边跑边单启会重建整屏
     *    合成 graph、旧 graph 未释放就要新的 → 瞬时 2 块 VPD_graph_info_large → "Can't extend"。
     *    置 opened 后走 mhal_vout_commit() 批量 stop_list→start_list，一次成图（只 1 块 large graph）。
     *  - 离屏通道(仅解码不上屏)：单启解码器即可（不碰显示合成 graph）。 */
    d->opened = 1;
    /* ZoomPan 可能在解码器 open 之前就到了：把暂存的 IN_CROP 写上，随后 start_list 带着裁剪起。 */
    mhal_crop_apply_pending(d);
    if (bind_vout_win >= 0) {
        /* ★ 在显路径:只登记(g_disp.ch[chn]=d 已在上面), 不在此各自成图。
         * defer 批处理由 defer_end 触发; 否则 request_commit → 唯一显示线程防抖后**合并成一次**整屏重建
         * (真机验证运行中只能整屏重建, 不能单路 start)。→ 消除 32 路异步各自重建的 Apply 洪水/黑闪。 */
        if (g_disp.defer == 0) mhal_vout_request_commit();
    } else {
        if ((ret = hd_videodec_start(d->dec_path)) != HD_OK) goto fail;   /* 离屏解码器:独立 start, 不碰显示合成图 */
    }

    *out = d;
    mhal_unlock();
    return 0;

fail:
    NVR_LOGE("mhal", "vdec_open chn%d fail %d (step%d: 1dec_open/2proc_open/3vout_open/4dec_bind/5proc_bind/6cfg)"
             " [vout_win=%d dim=%dx%d started=%d pending=%d]",
             chn, ret, step, bind_vout_win, d->w, d->h, g_disp.started_n, g_disp.pending_n);
    d->opened = 0;
    g_disp.ch[chn] = NULL;
    /* ★ 关键:关掉本次已开的 dec/proc/vout 路径。否则任一步失败后路径泄漏 → 同号(IN/OUT)下次
     * hd_videodec_open 复用即 ALREADY_OPEN(-25),一路失败级联到全部通道黑屏。 */
    hd_videodec_unbind(HD_VIDEODEC_OUT(0, chn));
    hd_videoproc_unbind(HD_VIDEOPROC_OUT(chn, 0));
    if (d->vout_path) hd_videoout_close(d->vout_path);
    if (d->proc_path) hd_videoproc_close(d->proc_path);
    if (d->dec_path)  hd_videodec_close(d->dec_path);
    if (g_disp.inited) mhal_vout_request_commit();  /* 摘除失败路，防抖线程重建其余在显窗口 */
    mhal_unlock();
    mhal_budget_release(d->w, d->h, d->fps);     /* 归还预算 */
    free(d);
    return -1;
}

/* ★ send_list 序列化锁:多路回放各自线程送流时,hd_videodec_send_list **不可并发**
 *   (SDK 用单线程一次送多路);并发会 DEC_HW_TIMEOUT。此锁保证任一时刻只有一个 send。 */
static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;

int mhal_vdec_send(mhal_vdec_t *d, const uint8_t *annexb, uint32_t len, uint32_t ts)
{
    return mhal_vdec_send_ex(d, annexb, len, ts, 200 /*阻塞等 FIFO*/);
}

int mhal_vdec_send_ex(mhal_vdec_t *d, const uint8_t *annexb, uint32_t len, uint32_t ts, int wait_ms)
{
    if (!d || !d->opened || !annexb || len == 0) return -1;
    if (wait_ms < 0) wait_ms = 0;

    /* 单帧走 send_list（样例验证过的送流路径） */
    HD_VIDEODEC_SEND_LIST item;
    memset(&item, 0, sizeof(item));
    item.path_id            = d->dec_path;
    item.user_bs.sign       = MAKEFOURCC('V', 'S', 'T', 'M');
    item.user_bs.p_bs_buf   = (CHAR *)annexb;      /* Annex-B 裸帧（来自 streaming video_cb） */
    item.user_bs.bs_buf_size= len;
    item.user_bs.timestamp  = (UINT64)ts * 1000;   /* 90kHz→us 的换算按需精化 */

    pthread_mutex_lock(&g_send_lock);
    HD_RESULT ret = hd_videodec_send_list(&item, 1, (UINT32)wait_ms);
    pthread_mutex_unlock(&g_send_lock);
    if (ret >= 0 && item.user_bs.retval >= 0)
        return 0;

    /* 送不进:>4MB(超解码窗口)=**硬错误**;否则(帧本身合法却入不了队)判为 **FIFO 满/超时**。
     *   非阻塞 live 送(wait_ms 小)→ 返回 MHAL_VDEC_EBUSY 供上层限时延丢帧追帧,不刷屏诊断;
     *   阻塞送(关键帧/bootstrap, wait_ms 大)失败仍按硬错误处理(触发 RESYNC),并节流记诊断。 */
    if (len > (4u << 20)) {
        NVR_LOGE("vdec", "chn%d 送流失败(帧>4MB超窗口) ret=%d retval=%d len=%u",
                 d->chn, ret, item.user_bs.retval, len);
        return -1;
    }
    if (wait_ms >= 100) {
        static int fail_cnt[MHAL_MAX_CH];
        int c = (d->chn >= 0 && d->chn < MHAL_MAX_CH) ? d->chn : 0;
        if (fail_cnt[c]++ < 30 || fail_cnt[c] % 50 == 0)
            NVR_LOGE("vdec", "chn%d 送流失败 ret=%d retval=%d len=%u(第%d次)",
                     d->chn, ret, item.user_bs.retval, len, fail_cnt[c]);
        return -1;
    }
    return MHAL_VDEC_EBUSY;
}

/* ★ 批量送流(按 SDK playback_1div_to_4div:多路解码器的帧**一次** send_list 送,避免逐路分别 send
 *   并发竞争硬件 → DEC_HW_TIMEOUT)。ds/bufs/lens/tss 各 n 个,一一对应;返回 0/负。 */
int mhal_vdec_send_multi(mhal_vdec_t **ds, const uint8_t **bufs,
                         const uint32_t *lens, const uint32_t *tss, int n)
{
    if (n <= 0) return -1;
    HD_VIDEODEC_SEND_LIST items[MHAL_MAX_CH];
    int m = 0;
    for (int i = 0; i < n && m < MHAL_MAX_CH; i++) {
        if (!ds[i] || !ds[i]->opened || !bufs[i] || lens[i] == 0) continue;
        memset(&items[m], 0, sizeof(items[m]));
        items[m].path_id             = ds[i]->dec_path;
        items[m].user_bs.sign        = MAKEFOURCC('V', 'S', 'T', 'M');
        items[m].user_bs.p_bs_buf    = (CHAR *)bufs[i];
        items[m].user_bs.bs_buf_size = lens[i];
        items[m].user_bs.timestamp   = (UINT64)tss[i] * 1000;
        m++;
    }
    if (m == 0) return 0;
    HD_RESULT ret = hd_videodec_send_list(items, m, 200 /*wait_ms*/);
    return (ret < 0) ? -1 : 0;
}

/* 当前活跃解码器数(g_disp.ch[] 非空)。供回放确认"全局单一使用者"(无残留 live 解码器竞争 HW)。 */
int mhal_vdec_active_count(void)
{
    int n = 0;
    for (int i = 0; i < MHAL_MAX_CH; i++) if (g_disp.ch[i]) n++;
    return n;
}

int mhal_vdec_recv(mhal_vdec_t *d, void *yuv_buf, uint32_t *inout_len, uint32_t timeout_ms)
{
    /* 抓拍/二次处理用：拉解码后 YUV。预览走 bind 直连 vout，无需 recv。 */
    if (!d || !d->opened) return -1;
    HD_VIDEO_FRAME frame;
    memset(&frame, 0, sizeof(frame));
    HD_RESULT ret = hd_videodec_pull_out_buf(d->dec_path, &frame, (INT32)timeout_ms);
    if (ret != HD_OK) return -1;
    /* TODO(板级): mmap frame.phy_addr[0] → 拷到 yuv_buf（按 dim/pxlfmt 计算大小），
     * 再 hd_videodec_release_out_buf。见样例 pull_decout_and_scale_jpg。 */
    (void)yuv_buf; (void)inout_len;
    hd_videodec_release_out_buf(d->dec_path, &frame);
    return 0;
}

/* 拆绑/关路径/归还预算/释放。★ 前提:本路 dec/proc/vout 路径**已停**(离屏路径独立停,或
 * 在显路径已随一次 stop_list 停)。不做 commit、不动 g_disp.ch(调用方已摘)。commit 的 pending
 * 释放、以及非批量 close 都走这里。在 mhal_lock 保护下调用。 */
/* 归还解码预算一次(幂等):关闭即还,避免在显解码器 defer 期间(未拆)仍占额 → 回放/新预览误判超预算。 */
static void vdec_budget_free_once(struct mhal_vdec *d)
{
    if (d && !d->budget_released) {
        mhal_budget_release(d->w, d->h, d->fps);
        d->budget_released = 1;
    }
}

void mhal_vdec_teardown(struct mhal_vdec *d)
{
    if (!d) return;
    int chn = d->chn;
    hd_videodec_unbind(HD_VIDEODEC_OUT(0, chn));
    hd_videoproc_unbind(HD_VIDEOPROC_OUT(chn, 0));
    if (d->vout_path) hd_videoout_close(d->vout_path);
    if (d->proc_path) hd_videoproc_close(d->proc_path);
    if (d->dec_path)  hd_videodec_close(d->dec_path);
    vdec_budget_free_once(d);   /* 幂等:close 已还则不重复 */
    free(d);
}

void mhal_vdec_close(mhal_vdec_t *d)
{
    if (!d) return;
    mhal_lock();
    /* ★ prebuild:关 = 隐藏该窗(visible=0)+ 归还预算,**保留**预建路径(解码器常驻,不 teardown、不整图重建)。
     * 下次 open 同 chn 直接复用 g_disp.ch[chn]。→ 加/删设备只碰这一路,别路解码器从不重启。 */
    if (g_disp.prebuilt && d->chn >= 0 && d->chn < MHAL_MAX_CH && g_disp.ch[d->chn] == d) {
        vdec_budget_free_once(d);
        HD_VIDEOOUT_WIN_ATTR wa; memset(&wa, 0, sizeof(wa)); wa.visible = 0;
        hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &wa);
        d->active = 0; d->vout_win = -1;
        NVR_LOGI("mhal", "chn%d ■隐藏(prebuild:窗visible=0,解码器常驻,不拆不重建)", d->chn);
        mhal_unlock();
        return;
    }
    int chn = d->chn;
    /* ★ 是否"在显路径"(需走 commit 的 stop_list 停 dec+proc+vout 后再拆,否则 proc/vout 未停就 close
     *   → 下次 open 同通道 proc 报 ALREADY_OPEN(-25)/失败)。vout_win:≥0=宫格窗、-2=自由矩形(bind_rect,
     *   回放各格用),二者都在屏上且都在 started 集合,必须走 commit 收;-1=已隐藏(unbind)才算非在显。 */
    int was_display = (d->opened && (d->vout_win >= 0 || d->vout_win == -2));

    /* 先从在显集合摘除本路。 */
    if (chn >= 0 && chn < MHAL_MAX_CH) g_disp.ch[chn] = NULL;

    /* ★ 预算立即归还(不等 defer 后的 teardown):否则回放/新预览在旧路尚未拆时误判"已用"超预算被拒
     * (整体 746 Mpix/s 全局池;真机:切到多宫格子码流回放却报超预算=旧 liveView 4K 未还额)。teardown 幂等。 */
    vdec_budget_free_once(d);

    /* ★ 在显路径关闭:不在此同步 commit/teardown。挂到 pending_free, request_commit → 唯一显示线程
     * 防抖后一次 stop_list(含本路)→ start_list(不含本路)→ 释放 pending(teardown)。defer 与非 defer 同一路径。
     * (真机验证运行中只能整屏重建; 合并避免"每关一路各重建一次"的黑闪。) */
    if (was_display) {
        if (g_disp.pending_n < MHAL_MAX_CH) {
            g_disp.pending_free[g_disp.pending_n++] = d;
            mhal_vout_request_commit();
        } else {                                    /* pending 满兜底: 就地同步重建+释放 */
            mhal_vout_commit();
            mhal_vdec_teardown(d);
        }
        mhal_unlock();
        return;
    }

    if (d->opened) {
        hd_videodec_stop(d->dec_path);              /* 离屏解码器：独立停 */
        mhal_vdec_teardown(d);
    } else {
        mhal_vdec_teardown(d);
    }
    mhal_unlock();
}
