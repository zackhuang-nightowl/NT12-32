/***************************************************************************************
 *  mhal_vdec.c — 视频硬解（封装 hd_videodec + videoproc 桥接到 videoout）
 *
 *  管线：hd_videodec_open/bind/set → hd_videoproc → hd_videoout；
 *        送帧 hd_videodec_send_list；取图 hd_videodec_pull_out_buf（抓拍用）。
 *  参照 hdal 样例 playback_1div_to_4div.c 的 open/bind/set_module_param/send 流程。
 *  ⚠️ 仅在 na51090 BSP 交叉环境编译。
 ***************************************************************************************/
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
    if (w > MHAL_DEC_MAX_W) w = MHAL_DEC_MAX_W;
    if (h > MHAL_DEC_MAX_H) h = MHAL_DEC_MAX_H;

    int subyuv = mhal_vdec_need_subyuv(d);

    HD_VIDEODEC_PATH_CONFIG dec = {0};
    dec.max_mem.codec_type  = d->codec;
    dec.max_mem.frame_rate  = d->fps > 0 ? d->fps : 30;
    dec.max_mem.dim.w       = w;            /* 动态：单宫格 4K 就分 4K，子码流就分 640×360 */
    dec.max_mem.dim.h       = h;
    dec.max_mem.bs_counts   = 8;
    dec.max_mem.max_ref_num = 2;
    dec.max_mem.max_bitrate = 8 * 1024 * 1024;
    /* TODO(板级): dec.max_mem.ddr_id 与 data_pool[].ddr_id 应经
     * vendor_common_get_ddrid(HD_COMMON_MEM_DISP_DEC_*_POOL, ...) 取（见样例 get_all_id），
     * 并按 dts 配置的内存池划分。此处用默认池。 */
    dec.data_pool[0].mode   = HD_VIDEODEC_POOL_ENABLE;   /* 主(全分辨率)输出池 */
    dec.data_pool[0].counts = HD_VIDEODEC_SET_COUNT(3, 0);
    if (subyuv) {
        /* ★ 关键①:比特流窗口(单帧最大)。0=AUTO 由 max_bitrate(8Mbps)/fps 推 → ~50KB,远小于 8K
         * IDR(实测 ~567KB) → 送流 hd_videodec_send_list 时 VPD_PUT_COPY_MULTI_DIN 拷不进 DIN、失败,
         * 帧被丢、不出图。给 2MB 窗口装下 IDR。但 **DIN 池(部署 dtb 的 DISP_DEC_IN)只有 20.3MB**,
         * 4MB×bs_counts=8=32MB 会撑爆池→分配失败→无图。故 2MB×4=8MB,稳在池内(样例 transcode 给 1MB)。 */
        dec.max_mem.max_bs_size = 2 * 1024 * 1024;
        dec.max_mem.bs_counts   = 4;   /* 8K:少而大的比特流缓冲,4×2MB=8MB ≤ 20MB DIN 池 */
        /* ★ 关键②:按 dts 实际位置给各池 ddr_id(比特流→DEC_IN/DDR0,全分辨率→DEC_OUT/DDR1,
         * sub-yuv→RATIO/DDR1)。否则 data_pool[1] 默认 DDR0——那里没有 RATIO 池——sub-yuv 无处可写,
         * 解码器产不出下采样图 → 送流成功却无图。样例 playback_with_sub_ratio 正是这样取 ddr_id 的。 */
        resolve_dec_ddrid();
        dec.max_mem.ddr_id      = g_ddr_dec_in;
        dec.data_pool[0].ddr_id = g_ddr_dec_out;
        /* sub-yuv(下采样)输出用 RATIO 池 —— 样例的 disp_dec_out_ratio 池(DDR1)。 */
        dec.data_pool[1].mode   = HD_VIDEODEC_POOL_ENABLE;
        dec.data_pool[1].ddr_id = g_ddr_dec_out_ratio;
        dec.data_pool[1].counts = HD_VIDEODEC_SET_COUNT(3, 0);
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

    /* ★ 同批内先关后开(切主/子码流 reopen 同一通道):旧解码器还挂在 pending_free(defer 期间未拆,
     * 路径仍占用),此时下面 hd_videodec_open 复用同一 IN/OUT 号会 ALREADY_OPEN(-25)。先就地停+拆旧:
     * 旧路径上次 commit 已 started → 停之、从 started 集合摘除、拆绑/关/释放,再往下开新路径。 */
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
        NVR_LOGI("mhal", "chn%d 同批 reopen:先拆旧解码器路径(避免 ALREADY_OPEN)", chn);
        break;
    }

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
    if (bind_vout_win >= 0) {
        /* ★ 批量提交中(defer>0):只登记,不各自成图;由 mhal_vout_defer_end 一次 start_list 起全部。
         * 否则(单独开)立即成图。 */
        if (g_disp.defer == 0) {
            if (mhal_vout_commit() != 0) { ret = -1; goto fail; }
        }
    } else {
        if ((ret = hd_videodec_start(d->dec_path)) != HD_OK) goto fail;
    }

    *out = d;
    mhal_unlock();
    return 0;

fail:
    NVR_LOGE("mhal", "vdec_open chn%d fail %d (step%d: 1dec_open/2proc_open/3vout_open/4dec_bind/5proc_bind/6cfg)", chn, ret, step);
    d->opened = 0;
    g_disp.ch[chn] = NULL;
    /* ★ 关键:关掉本次已开的 dec/proc/vout 路径。否则任一步失败后路径泄漏 → 同号(IN/OUT)下次
     * hd_videodec_open 复用即 ALREADY_OPEN(-25),一路失败级联到全部通道黑屏。 */
    hd_videodec_unbind(HD_VIDEODEC_OUT(0, chn));
    hd_videoproc_unbind(HD_VIDEOPROC_OUT(chn, 0));
    if (d->vout_path) hd_videoout_close(d->vout_path);
    if (d->proc_path) hd_videoproc_close(d->proc_path);
    if (d->dec_path)  hd_videodec_close(d->dec_path);
    if (g_disp.inited) mhal_vout_commit();      /* 摘除失败路，重建其余在显窗口(递归锁) */
    mhal_unlock();
    mhal_budget_release(d->w, d->h, d->fps);     /* 归还预算 */
    free(d);
    return -1;
}

int mhal_vdec_send(mhal_vdec_t *d, const uint8_t *annexb, uint32_t len, uint32_t ts)
{
    if (!d || !d->opened || !annexb || len == 0) return -1;

    /* 单帧走 send_list（样例验证过的送流路径） */
    HD_VIDEODEC_SEND_LIST item;
    memset(&item, 0, sizeof(item));
    item.path_id            = d->dec_path;
    item.user_bs.sign       = MAKEFOURCC('V', 'S', 'T', 'M');
    item.user_bs.p_bs_buf   = (CHAR *)annexb;      /* Annex-B 裸帧（来自 streaming video_cb） */
    item.user_bs.bs_buf_size= len;
    item.user_bs.timestamp  = (UINT64)ts * 1000;   /* 90kHz→us 的换算按需精化 */

    HD_RESULT ret = hd_videodec_send_list(&item, 1, 200 /*wait_ms*/);
    if (ret < 0 || item.user_bs.retval < 0) {
        /* 送流失败诊断:按通道计数(节流)。看是否 ch16(8K)大帧/超窗口/DIN 满 → 定位丢图根因。 */
        static int fail_cnt[MHAL_MAX_CH];
        int c = (d->chn >= 0 && d->chn < MHAL_MAX_CH) ? d->chn : 0;
        if (fail_cnt[c]++ < 30 || fail_cnt[c] % 50 == 0)
            NVR_LOGE("vdec", "chn%d 送流失败 ret=%d retval=%d len=%u(第%d次) %s",
                     d->chn, ret, item.user_bs.retval, len, fail_cnt[c],
                     len > (4u<<20) ? "★帧>4MB超窗口" : "");
        return -1;
    }
    return 0;
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
void mhal_vdec_teardown(struct mhal_vdec *d)
{
    if (!d) return;
    int chn = d->chn;
    hd_videodec_unbind(HD_VIDEODEC_OUT(0, chn));
    hd_videoproc_unbind(HD_VIDEOPROC_OUT(chn, 0));
    if (d->vout_path) hd_videoout_close(d->vout_path);
    if (d->proc_path) hd_videoproc_close(d->proc_path);
    if (d->dec_path)  hd_videodec_close(d->dec_path);
    mhal_budget_release(d->w, d->h, d->fps);
    free(d);
}

void mhal_vdec_close(mhal_vdec_t *d)
{
    if (!d) return;
    mhal_lock();
    int chn = d->chn;
    /* ★ 是否"在显路径"(需走 commit 的 stop_list 停 dec+proc+vout 后再拆,否则 proc/vout 未停就 close
     *   → 下次 open 同通道 proc 报 ALREADY_OPEN(-25)/失败)。vout_win:≥0=宫格窗、-2=自由矩形(bind_rect,
     *   回放各格用),二者都在屏上且都在 started 集合,必须走 commit 收;-1=已隐藏(unbind)才算非在显。 */
    int was_display = (d->opened && (d->vout_win >= 0 || d->vout_win == -2));

    /* 先从在显集合摘除本路。 */
    if (chn >= 0 && chn < MHAL_MAX_CH) g_disp.ch[chn] = NULL;

    /* ★ 批量提交中(defer>0)且是在显路径:不各自 commit,挂到 pending,由 defer_end 的一次 commit
     * 统一 stop_list(含本路)后再 teardown。一次切宫格只重成图 1 次。 */
    if (g_disp.defer > 0 && was_display) {
        if (g_disp.pending_n < MHAL_MAX_CH) g_disp.pending_free[g_disp.pending_n++] = d;
        else { /* 兜底:pending 满则就地(先 commit 停)处理 */ mhal_vout_commit(); mhal_vdec_teardown(d); }
        mhal_unlock();
        return;
    }

    if (was_display) {
        mhal_vout_commit();                         /* 停旧集合(含本路)+起其余;本路路径随之停 */
        mhal_vdec_teardown(d);
    } else if (d->opened) {
        hd_videodec_stop(d->dec_path);              /* 离屏解码器：独立停 */
        mhal_vdec_teardown(d);
    } else {
        mhal_vdec_teardown(d);
    }
    mhal_unlock();
}
