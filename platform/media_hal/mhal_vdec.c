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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 单实例最大帧尺寸（数据手册：分辨率 up to 8192×8192）；未知分辨率时的兜底上限 */
#define MHAL_DEC_MAX_W 8192
#define MHAL_DEC_MAX_H 8192
#define MHAL_DEC_DFLT_W 1920      /* w/h 未知时按此预分配（覆盖绝大多数主码流）*/
#define MHAL_DEC_DFLT_H 1088

static int align16(int v) { return (v + 15) & ~15; }

/* 配置一条解码路径的内存/编解码参数 —— **按实际分辨率动态分配** max_mem */
static int cfg_dec_path(struct mhal_vdec *d)
{
    int w = d->w > 0 ? align16(d->w) : MHAL_DEC_DFLT_W;
    int h = d->h > 0 ? align16(d->h) : MHAL_DEC_DFLT_H;
    if (w > MHAL_DEC_MAX_W) w = MHAL_DEC_MAX_W;
    if (h > MHAL_DEC_MAX_H) h = MHAL_DEC_MAX_H;

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
    dec.data_pool[0].mode   = HD_VIDEODEC_POOL_ENABLE;
    dec.data_pool[0].counts = HD_VIDEODEC_SET_COUNT(3, 0);
    return hd_videodec_set(d->dec_path, HD_VIDEODEC_PARAM_PATH_CONFIG, &dec);
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
    d->chn      = chn;
    d->codec    = (codec == MHAL_CODEC_H265) ? HD_CODEC_TYPE_H265 : HD_CODEC_TYPE_H264;
    d->vout_win = bind_vout_win;
    d->w = w; d->h = h; d->fps = fps;

    HD_RESULT ret;
    /* 1) 开 dec/proc/vout 三段路径（dev 0, id=chn） */
    if ((ret = hd_videodec_open(HD_VIDEODEC_IN(0, chn), HD_VIDEODEC_OUT(0, chn), &d->dec_path)) != HD_OK) goto fail;
    if ((ret = hd_videoproc_open(HD_VIDEOPROC_IN(chn, 0), HD_VIDEOPROC_OUT(chn, 0), &d->proc_path)) != HD_OK) goto fail;
    if (bind_vout_win >= 0 &&
        (ret = hd_videoout_open(HD_VIDEOOUT_IN(0, chn), HD_VIDEOOUT_OUT(0, 0), &d->vout_path)) != HD_OK) goto fail;

    /* 2) 绑定 dec→proc→(vout) */
    if ((ret = hd_videodec_bind(HD_VIDEODEC_OUT(0, chn), HD_VIDEOPROC_IN(chn, 0))) != HD_OK) goto fail;
    if (bind_vout_win >= 0 &&
        (ret = hd_videoproc_bind(HD_VIDEOPROC_OUT(chn, 0), HD_VIDEOOUT_IN(0, chn))) != HD_OK) goto fail;

    /* 3) 配置解码路径（内存/codec） */
    if ((ret = cfg_dec_path(d)) != HD_OK) goto fail;

    /* 4) 登记 + 应用分屏窗口矩形（proc_out.rect + videoout win） */
    g_disp.ch[chn] = d;
    if (bind_vout_win >= 0) mhal_vout_bind(bind_vout_win, chn);

    /* 5) 起流 */
    if ((ret = hd_videodec_start(d->dec_path)) != HD_OK) goto fail;
    if (bind_vout_win >= 0) {
        hd_videoproc_start(d->proc_path);
        hd_videoout_start(d->vout_path);
    }

    d->opened = 1;
    *out = d;
    return 0;

fail:
    printf("mhal_vdec_open chn%d fail %d\n", chn, ret);
    g_disp.ch[chn] = NULL;
    mhal_budget_release(d->w, d->h, d->fps);   /* 归还预算 */
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
    return (ret < 0) ? -1 : 0;
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

void mhal_vdec_close(mhal_vdec_t *d)
{
    if (!d) return;
    if (d->opened) {
        hd_videodec_stop(d->dec_path);
        if (d->vout_win >= 0) { hd_videoout_stop(d->vout_path); hd_videoproc_stop(d->proc_path); }
        hd_videodec_unbind(HD_VIDEODEC_OUT(0, d->chn));
        if (d->vout_win >= 0) hd_videoproc_unbind(HD_VIDEOPROC_OUT(d->chn, 0));
    }
    if (d->vout_path) hd_videoout_close(d->vout_path);
    if (d->proc_path) hd_videoproc_close(d->proc_path);
    if (d->dec_path)  hd_videodec_close(d->dec_path);
    if (d->chn >= 0 && d->chn < MHAL_MAX_CH) g_disp.ch[d->chn] = NULL;
    mhal_budget_release(d->w, d->h, d->fps);   /* 归还吞吐预算 */
    free(d);
}
