/***************************************************************************************
 *  mhal_vout.c — 视频输出/分屏（封装 hd_videoout）
 *
 *  参照 hdal 样例 playback_1div_to_4div.c 的 init/open/get-syscaps 流程。
 *  ⚠️ 仅在 na51090 BSP 交叉环境编译（依赖 hdal 库 + dts 内存池）。
 ***************************************************************************************/
#include "mhal_internal.h"
#include <stdio.h>
#include <string.h>

mhal_disp_t g_disp;

/* 分屏几何：1/4/9/16 → 网格边长 1/2/3/4 */
void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h)
{
    int n = (layout == MHAL_LAYOUT_1) ? 1 :
            (layout == MHAL_LAYOUT_4) ? 2 :
            (layout == MHAL_LAYOUT_9) ? 3 : 4;
    int cw = disp_w / n, ch = disp_h / n;
    *x = (idx % n) * cw;
    *y = (idx / n) * ch;
    *w = cw;
    *h = ch;
}

int mhal_vout_init(mhal_out_t out, int width, int height)
{
    HD_RESULT ret;
    memset(&g_disp, 0, sizeof(g_disp));
    g_disp.out    = out;
    g_disp.layout = MHAL_LAYOUT_16;

    /* 1) 平台公共初始化 + 各引擎 init（dec/proc/out）。
     * hd_common_init 的入参与 DRAM/chip 数相关，取值随板级 dts；见样例 §hd_common_init 注释。 */
    if ((ret = hd_common_init(0)) != HD_OK) { printf("hd_common_init fail %d\n", ret); return -1; }
    if ((ret = hd_videodec_init())  != HD_OK) return -1;
    if ((ret = hd_videoproc_init()) != HD_OK) return -1;
    if ((ret = hd_videoout_init())  != HD_OK) return -1;

    /* 2) 打开 videoout 设备控制路径（样例：hd_videoout_open(0, HD_VIDEOOUT_0_CTRL, &ctrl)） */
    if ((ret = hd_videoout_open(0, HD_VIDEOOUT_0_CTRL, &g_disp.ctrl_path)) != HD_OK) {
        printf("videoout ctrl open fail %d\n", ret);
        return -1;
    }

    /* 3) 取显示能力（input_dim = 面板/HDMI 分辨率）与 FB 格式 */
    if (hd_videoout_get(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_SYSCAPS, &g_disp.syscaps) != HD_OK) return -1;
    g_disp.fb_fmt.fb_id = HD_FB0;
    hd_videoout_get(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_FB_FMT, &g_disp.fb_fmt);

    g_disp.disp_w = width  > 0 ? width  : (int)g_disp.syscaps.input_dim.w;
    g_disp.disp_h = height > 0 ? height : (int)g_disp.syscaps.input_dim.h;

    /* 配置 HDMI 输出模式/时序：按 width/height 选 HD_VIDEOOUT_HDMI_ID，经
     * hd_videoout_set(ctrl_path, HD_VIDEOOUT_PARAM_MODE, &mode) 下发。
     * 字段/调用序列参照样例 display_with_change_mode.c / vcap_vi_scan.c：
     *   mode.output_type      = HD_COMMON_VIDEO_OUT_HDMI;
     *   mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_xxx;
     *   hd_videoout_set(ctrl_path, HD_VIDEOOUT_PARAM_MODE, &mode);
     * (HD_VIDEOOUT_MODE 定义见 hd_videoout.h：output_type + input_dim + output_mode 联合体) */
    {
        HD_VIDEOOUT_MODE mode;
        memset(&mode, 0, sizeof(mode));
        mode.output_type = HD_COMMON_VIDEO_OUT_HDMI;
        mode.input_dim    = HD_VIDEOOUT_IN_AUTO;
        if (g_disp.disp_w == 1280 && g_disp.disp_h == 720)
            mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_1280X720P60;
        else if (g_disp.disp_w == 1920 && g_disp.disp_h == 1080)
            mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_1920X1080P60;
        else
            mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_1920X1080P60; /* 就近/默认兜底 */

        if ((ret = hd_videoout_set(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_MODE, &mode)) != HD_OK)
            printf("videoout set mode fail %d (hdmi_id=%d)\n", ret, mode.output_mode.hdmi);
    }

    g_disp.inited = 1;
    return 0;
}

int mhal_vout_set_layout(mhal_layout_t layout)
{
    if (!g_disp.inited) return -1;
    g_disp.layout = layout;
    /* 对已开通道重算窗口矩形并下发（proc_out.rect + videoout IN_WIN_ATTR）。
     * 单窗口属性下发见 mhal_vdec.c 里 apply_window()（open 时用同一函数）。 */
    for (int i = 0; i < MHAL_MAX_CH; i++)
        if (g_disp.ch[i] && g_disp.ch[i]->vout_win >= 0)
            mhal_vout_bind(g_disp.ch[i]->vout_win, i);
    return 0;
}

/* 下发窗口矩形（videoproc OUT + videoout IN_WIN_ATTR），供 grid 绑定与任意矩形绑定共用 */
static void apply_window(struct mhal_vdec *d, int x, int y, int w, int h)
{
    /* videoproc 输出矩形（缩放到该窗口大小） */
    HD_VIDEOPROC_OUT po; memset(&po, 0, sizeof(po));
    po.rect.x = x; po.rect.y = y; po.rect.w = w; po.rect.h = h;
    po.bg.w = g_disp.disp_w; po.bg.h = g_disp.disp_h;
    po.pxlfmt = g_disp.fb_fmt.fmt;
    po.dir = HD_VIDEO_DIR_NONE;
    hd_videoproc_set(d->proc_path, HD_VIDEOPROC_PARAM_OUT, &po);

    /* videoout 输入窗口属性（位置 + 可见） */
    HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
    win.rect.x = x; win.rect.y = y; win.rect.w = w; win.rect.h = h;
    win.visible = 1;
    hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);
}

int mhal_vout_bind(int win_idx, int decoder_chn)
{
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) return -1;
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) return -1;

    int x, y, w, h;
    mhal_layout_rect(g_disp.layout, win_idx, g_disp.disp_w, g_disp.disp_h, &x, &y, &w, &h);

    apply_window(d, x, y, w, h);

    d->vout_win = win_idx;
    return 0;
}

int mhal_vout_bind_rect(int decoder_chn, int x, int y, int w, int h)
{
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) return -1;
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) return -1;

    apply_window(d, x, y, w, h);

    d->vout_win = -2; /* 自由矩形，非宫格窗口号 */
    return 0;
}

int mhal_vout_unbind(int decoder_chn)
{
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) return -1;
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) return -1;

    HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
    win.visible = 0;
    hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);

    d->vout_win = -1;
    return 0;
}

int mhal_vout_osd(int win_idx, const char *text)
{
    (void)win_idx; (void)text;
    /* TODO(板级): 通道名/时间/事件 OSD 叠加走 GFX/OSG 图层
     * （hd_gfx_* 或 videoout OSD 参数），见样例 display_with_osg.c / draw_lines_to_display.c。 */
    return 0;
}

void mhal_vout_deinit(mhal_out_t out)
{
    (void)out;
    if (!g_disp.inited) return;
    if (g_disp.ctrl_path) hd_videoout_close(g_disp.ctrl_path);
    hd_videoout_uninit();
    hd_videoproc_uninit();
    hd_videodec_uninit();
    hd_common_uninit();
    memset(&g_disp, 0, sizeof(g_disp));
}
