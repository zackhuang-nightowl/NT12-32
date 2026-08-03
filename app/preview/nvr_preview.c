/***************************************************************************************
 *  nvr_preview.c — 预览分屏编排。见 nvr_preview.h / 计划 §B2。
 ***************************************************************************************/
#include "nvr_preview.h"
#include "mhal_vout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PV_MAX_WIN 16
#define PV_MAX_CH  32

struct nvr_preview {
    nvr_preview_cfg_t cfg;
    pv_layout_t layout;
    int         page;                 /* 当前页（每页 win_count 个通道） */
    int         win_count;            /* 当前布局窗口数 */
    int         zoom_chn;             /* 单画面放大的通道；-1=无 */
    int         win2chn[PV_MAX_WIN];  /* 窗口→通道，-1=空 */
    unsigned    icons[PV_MAX_CH];     /* 每通道图标位 */
    char        name[PV_MAX_CH][40];  /* 通道名（OSD） */
};

/* pv_layout → mhal_layout（6→9 网格, 12→16 网格，只绑 N 窗） */
static mhal_layout_t to_mhal(pv_layout_t l, int *win_count)
{
    switch (l) {
        case PV_L1:  *win_count = 1;  return MHAL_LAYOUT_1;
        case PV_L4:  *win_count = 4;  return MHAL_LAYOUT_4;
        case PV_L6:  *win_count = 6;  return MHAL_LAYOUT_9;   /* 9 网格绑 6 */
        case PV_L9:  *win_count = 9;  return MHAL_LAYOUT_9;
        case PV_L12: *win_count = 12; return MHAL_LAYOUT_16;  /* 16 网格绑 12 */
        case PV_L16:
        default:     *win_count = 16; return MHAL_LAYOUT_16;
    }
}

int nvr_preview_init(const nvr_preview_cfg_t *cfg, nvr_preview_t **out)
{
    if (!cfg || !out) return -1;
    nvr_preview_t *p = calloc(1, sizeof(*p));
    if (!p) return -1;
    p->cfg = *cfg;
    p->zoom_chn = -1;
    for (int i = 0; i < PV_MAX_WIN; i++) p->win2chn[i] = -1;
    p->layout = PV_L16; p->win_count = 16;
    *out = p;
    return 0;
}

void nvr_preview_deinit(nvr_preview_t *p) { if (p) free(p); }

static void compose_osd(nvr_preview_t *p, int win)
{
    int chn = p->win2chn[win];
    if (chn < 0) return;
    char line[96]; line[0] = 0;
    if (p->cfg.osd_name && p->name[chn][0])
        snprintf(line, sizeof(line), "%s", p->name[chn]);
    /* 图标以文本标记附加（真实 OSD 由 mhal/GFX 叠加，具体样式上真机再调） */
    unsigned ic = p->icons[chn];
    if (ic) {
        char tag[24]; snprintf(tag, sizeof(tag), " [%s%s%s%s]",
            (ic & PV_ICON_MOTION) ? "M" : "", (ic & PV_ICON_HUMAN) ? "H" : "",
            (ic & PV_ICON_FACE)   ? "F" : "", (ic & PV_ICON_REC)   ? "R" : "");
        strncat(line, tag, sizeof(line) - strlen(line) - 1);
    }
    mhal_vout_osd(win, line);
}

int nvr_preview_set_layout(nvr_preview_t *p, pv_layout_t layout)
{
    if (!p) return -1;
    int wc = 0;
    mhal_layout_t ml = to_mhal(layout, &wc);
    if (mhal_vout_set_layout(ml) != 0) return -1;
    p->layout = layout; p->win_count = wc;
    /* 清空并按当前页重排（简单策略：窗 i ← 通道 page*win_count + i） */
    for (int w = 0; w < PV_MAX_WIN; w++) p->win2chn[w] = -1;
    for (int w = 0; w < wc; w++) {
        int chn = p->page * wc + w;
        if (chn < PV_MAX_CH) { p->win2chn[w] = chn; mhal_vout_bind(w, chn); compose_osd(p, w); }
    }
    return 0;
}

int nvr_preview_page(nvr_preview_t *p, int page)
{
    if (!p || page < 0) return -1;
    p->page = page;
    return nvr_preview_set_layout(p, p->layout);
}

int nvr_preview_map(nvr_preview_t *p, int win, int chn)
{
    if (!p || win < 0 || win >= p->win_count || chn < 0 || chn >= PV_MAX_CH) return -1;
    p->win2chn[win] = chn;
    mhal_vout_bind(win, chn);
    compose_osd(p, win);
    return 0;
}

int nvr_preview_unmap(nvr_preview_t *p, int win)
{
    if (!p || win < 0 || win >= PV_MAX_WIN) return -1;
    p->win2chn[win] = -1;
    mhal_vout_osd(win, "");
    return 0;
}

static int win_of_chn(nvr_preview_t *p, int chn)
{
    for (int w = 0; w < p->win_count; w++) if (p->win2chn[w] == chn) return w;
    return -1;
}

int nvr_preview_on_channel_online(nvr_preview_t *p, int chn)
{
    if (!p || chn < 0 || chn >= PV_MAX_CH) return -1;
    int w = win_of_chn(p, chn);
    if (w < 0) {
        /* 若该通道属于当前页范围但未绑定，绑到其应在窗口 */
        int win = chn - p->page * p->win_count;
        if (win >= 0 && win < p->win_count) { p->win2chn[win] = chn; mhal_vout_bind(win, chn); w = win; }
    }
    if (w >= 0) compose_osd(p, w);
    return 0;
}

int nvr_preview_on_channel_offline(nvr_preview_t *p, int chn)
{
    if (!p) return -1;
    int w = win_of_chn(p, chn);
    if (w >= 0) mhal_vout_osd(w, "NO SIGNAL");
    return 0;
}

int nvr_preview_fullscreen(nvr_preview_t *p, int chn)
{
    if (!p || chn < 0) return -1;
    nvr_preview_set_layout(p, PV_L1);
    p->win2chn[0] = chn; mhal_vout_bind(0, chn); compose_osd(p, 0);
    /* 单画面 → 切主码流 */
    if (p->cfg.sm) nvr_stream_switch_stream(p->cfg.sm, chn, NVR_STREAM_MAIN, NULL);
    p->zoom_chn = chn;
    return 0;
}

int nvr_preview_single_zoom(nvr_preview_t *p, int chn, int on)
{
    if (!p) return -1;
    if (on) return nvr_preview_fullscreen(p, chn);
    /* 退出放大 → 回默认布局 + 子码流 */
    if (p->cfg.sm && p->zoom_chn >= 0)
        nvr_stream_switch_stream(p->cfg.sm, p->zoom_chn, NVR_STREAM_SUB, NULL);
    p->zoom_chn = -1;
    return nvr_preview_set_layout(p, PV_L16);
}

void nvr_preview_set_icons(nvr_preview_t *p, int chn, unsigned icon_bits)
{
    if (!p || chn < 0 || chn >= PV_MAX_CH) return;
    p->icons[chn] = icon_bits;
    int w = win_of_chn(p, chn);
    if (w >= 0) compose_osd(p, w);
}

void nvr_preview_tick(nvr_preview_t *p)
{
    if (!p || !p->cfg.osd_datetime) return;
    /* 时间 OSD：叠加到第 0 窗（真机可每窗叠角标，样式上真机再调） */
    time_t t = time(NULL);
    struct tm tmv; localtime_r(&t, &tmv);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    mhal_vout_osd(0, ts);
}
