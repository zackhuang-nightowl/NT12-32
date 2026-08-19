/***************************************************************************************
 *  nvr_preview.c — 预览分屏编排。见 nvr_preview.h / 计划 §B2。
 ***************************************************************************************/
#include "nvr_preview.h"
#include "nvr_defaults.h"
#include "mhal_vout.h"
#include "nvr_log.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>   /* usleep —— 出图就绪轮询 */

#define PV_MAX_WIN 36   /* 最大宫格数(6x6=36)；通道仍最多 32,多出的格为空 */
#define PV_MAX_CH  32

struct nvr_preview {
    nvr_preview_cfg_t cfg;
    pv_layout_t layout;
    int         page;                 /* 当前页（每页 win_count 个通道） */
    int         win_count;            /* 当前布局窗口数 */
    int         zoom_chn;             /* 单画面放大的通道；-1=无 */
    int         win2chn[PV_MAX_WIN];  /* 窗口→通道，-1=空 */

    /* --- 新 API：display_mode/page + 通道映射 + 悬浮块（计划 §Task4） --- */
    int         disp_w, disp_h;         /* HDMI 输出分辨率 */
    int         display_mode;           /* 0/1/4/9/16；0=未启用（已停解码） */
    int         display_page;           /* 1-based */
    int         map0[PV_MAX_CH];        /* 格序号(0-based) → 通道(0-based) */
    int         ext_n;                  /* 当前悬浮块数量 */
    nvr_pv_ext_t ext[PV_MAX_WIN];       /* 当前悬浮块列表 */

    int         new_api_active;         /* 首次调用 set_mode 后置 1（Important2 修复） */

    /* --- 数字变焦(ZoomPan)每通道状态；千分比 + ratio(100=1x)。掉电/重启回默认。 --- */
    struct { int enable, cx, cy, ratio; } zoom[PV_MAX_CH];

    pthread_mutex_t lock;                /* 递归锁：8089 路由线程 与 主循环线程 并发访问保护
                                          （Important1 修复；用递归锁因公有函数间存在嵌套调用） */
};

static void ext_place(nvr_preview_t *p, const nvr_pv_ext_t *e);

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
    p->disp_w = cfg->hdmi_w; p->disp_h = cfg->hdmi_h;
    for (int i = 0; i < PV_MAX_CH; i++) p->map0[i] = i;   /* 默认恒等映射 */
    for (int i = 0; i < PV_MAX_CH; i++) { p->zoom[i].enable = 1; p->zoom[i].cx = 500; p->zoom[i].cy = 500; p->zoom[i].ratio = 100; }
    p->display_mode = 0; p->display_page = 1; p->ext_n = 0;
    p->new_api_active = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&p->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    *out = p;
    return 0;
}

void nvr_preview_set_cm(nvr_preview_t *p, nvr_chan_mgr_t *cm) { if (p) p->cfg.cm = cm; }

void nvr_preview_deinit(nvr_preview_t *p) {
    if (!p) return;
    pthread_mutex_destroy(&p->lock);
    free(p);
}

/* 热切 HDMI 输出分辨率:切 mhal 输出模式(降级取生效值)→ 按新画布重排当前分屏 + 悬浮块。
 * 供 setSysDisplay 分辨率热切。eff_w/eff_h 回填实际生效(可能降级)。 */
int nvr_preview_set_hdmi(nvr_preview_t *p, int w, int h, int *eff_w, int *eff_h)
{
    if (!p) return -1;
    mhal_vout_set_resolution(w, h);              /* 切输出模式 + 清屏(降级同 init) */
    int ew = w, eh = h;
    mhal_vout_get_resolution(&ew, &eh);          /* 取实际生效(可能降级) */

    pthread_mutex_lock(&p->lock);
    p->disp_w = ew; p->disp_h = eh;
    int mode = p->display_mode, page = p->display_page;
    nvr_pv_ext_t saved[PV_MAX_WIN]; int sn = p->ext_n;
    for (int i = 0; i < sn && i < PV_MAX_WIN; i++) saved[i] = p->ext[i];
    pthread_mutex_unlock(&p->lock);

    if (mode > 0) nvr_preview_set_mode(p, mode, page);   /* 重排分屏(新画布) */
    if (sn > 0)   nvr_preview_set_ext(p, saved, sn);     /* 重排悬浮块(按新 disp_w/h 换算) */

    if (eff_w) *eff_w = ew;
    if (eff_h) *eff_h = eh;
    return 0;
}

int nvr_preview_set_layout(nvr_preview_t *p, pv_layout_t layout)
{
    if (!p) return -1;
    pthread_mutex_lock(&p->lock);
    int wc = 0;
    mhal_layout_t ml = to_mhal(layout, &wc);
    if (mhal_vout_set_layout(ml) != 0) { pthread_mutex_unlock(&p->lock); return -1; }
    p->layout = layout; p->win_count = wc;
    /* 清空并按当前页重排（简单策略：窗 i ← 通道 page*win_count + i） */
    for (int w = 0; w < PV_MAX_WIN; w++) p->win2chn[w] = -1;
    for (int w = 0; w < wc; w++) {
        int chn = p->page * wc + w;
        if (chn < PV_MAX_CH) { p->win2chn[w] = chn; nvr_stream_set_display(p->cfg.sm, chn, w); }
    }
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_page(nvr_preview_t *p, int page)
{
    if (!p || page < 0) return -1;
    pthread_mutex_lock(&p->lock);
    p->page = page;
    int rc = nvr_preview_set_layout(p, p->layout);
    pthread_mutex_unlock(&p->lock);
    return rc;
}

int nvr_preview_map(nvr_preview_t *p, int win, int chn)
{
    if (!p || win < 0 || chn < 0 || chn >= PV_MAX_CH) return -1;
    pthread_mutex_lock(&p->lock);
    if (win >= p->win_count) { pthread_mutex_unlock(&p->lock); return -1; }
    p->win2chn[win] = chn;
    nvr_stream_set_display(p->cfg.sm, chn, win);
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_unmap(nvr_preview_t *p, int win)
{
    if (!p || win < 0 || win >= PV_MAX_WIN) return -1;
    pthread_mutex_lock(&p->lock);
    p->win2chn[win] = -1;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

/* display_mode>0（新 set_mode/set_mapping 已激活）时，通道应在的窗口由 map0 决定
 * （格 k ← map0[(display_page-1)*win_count+k]），不再是旧 page*win_count+chn 公式
 * ——两条路径共享 win2chn/win_count（见 new_api_active，Important2 修复）。返回该
 * 通道在当前页应在的窗口，未命中(不在当前页)返回 -1。 */
static int win_of_chn_by_map(nvr_preview_t *p, int chn)
{
    int base = (p->display_page - 1) * p->win_count;
    for (int k = 0; k < p->win_count && k < PV_MAX_WIN; k++) {
        int idx = base + k;
        if (idx >= 0 && idx < PV_MAX_CH && p->map0[idx] == chn) return k;
    }
    return -1;
}

int nvr_preview_on_channel_online(nvr_preview_t *p, int chn)
{
    if (!p || chn < 0 || chn >= PV_MAX_CH) return -1;
    pthread_mutex_lock(&p->lock);
    /* ★ 通道上线 → 若它落在当前页可见格,开解码上屏。**无条件**按 map 重算并 set_display:
     * 不能用 win_of_chn 早退——set_mode 可能已预填 win2chn(那时通道还没 add 到 streaming、
     * set_display 是 no-op),真正的解码要等这里(上线后 slot 已存在)才开得起来。set_display 幂等。 */
    int w = -1;
    if (p->new_api_active) {
        /* 悬浮块优先：宫格已关时只在指定矩形出该通道。 */
        for (int i = 0; i < p->ext_n; i++) {
            if (p->ext[i].chn0 == chn) {
                if (p->cfg.cm) nvr_chan_set_stream(p->cfg.cm, chn, p->ext[i].stream);
                nvr_stream_set_display(p->cfg.sm, chn, 0);
                ext_place(p, &p->ext[i]);
                pthread_mutex_unlock(&p->lock);
                return 0;
            }
        }
        /* display_mode>0：从 map0 算该通道应在的窗口;==0(已离开 LiveView)则保持隐藏。 */
        if (p->display_mode > 0) {
            w = win_of_chn_by_map(p, chn);
            if (w >= 0) { p->win2chn[w] = chn; nvr_stream_set_display(p->cfg.sm, chn, w); }
        }
    } else {
        /* 旧 set_layout 路径（从未调用过 set_mode）：兼容旧 page*win_count+chn 公式 */
        int win = chn - p->page * p->win_count;
        if (win >= 0 && win < p->win_count) { p->win2chn[win] = chn; nvr_stream_set_display(p->cfg.sm, chn, win); w = win; }
    }
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_on_channel_offline(nvr_preview_t *p, int chn)
{
    (void)p; (void)chn;
    /* 掉线不叠 NO SIGNAL：GUI 经 longPolling ChannelStatusNotify 自绘 */
    return 0;
}

int nvr_preview_fullscreen(nvr_preview_t *p, int chn)
{
    if (!p || chn < 0) return -1;
    pthread_mutex_lock(&p->lock);
    nvr_preview_set_layout(p, PV_L1);
    p->win2chn[0] = chn; nvr_stream_set_display(p->cfg.sm, chn, 0);
    /* 单画面 → 切主码流 */
    if (p->cfg.cm) nvr_chan_set_stream(p->cfg.cm, chn, NVR_STREAM_MAIN);   /* 单画面=主码流 */
    p->zoom_chn = chn;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_single_zoom(nvr_preview_t *p, int chn, int on)
{
    if (!p) return -1;
    pthread_mutex_lock(&p->lock);
    if (on) { int rc = nvr_preview_fullscreen(p, chn); pthread_mutex_unlock(&p->lock); return rc; }
    /* 退出放大 → 回默认布局 + 子码流 */
    if (p->cfg.cm && p->zoom_chn >= 0)
        nvr_chan_set_stream(p->cfg.cm, p->zoom_chn, NVR_STREAM_SUB);   /* 退出放大=子码流 */
    p->zoom_chn = -1;
    int rc = nvr_preview_set_layout(p, PV_L16);
    pthread_mutex_unlock(&p->lock);
    return rc;
}

/* ---- 数字变焦(ZoomPan) ---- */
#define PV_ZOOM_MIN NVR_DEF_ZOOM_MIN   /* 100 = 1.00x 原画 */
#define PV_ZOOM_MAX NVR_DEF_ZOOM_MAX   /* 1000；≥此报 Exceeds zoom capabilities */

static int clamp_pm(int v)
{
    if (v < 0) return 0;
    if (v > 1000) return 1000;
    return v;
}

/* 原画千分比下的 ROI 边长：1000 × (100 / ZoomRatio)，宽高相同 → 保持原画宽高比。 */
static int zoom_roi_pm(int ratio)
{
    if (ratio < PV_ZOOM_MIN) ratio = PV_ZOOM_MIN;
    return (int)((1000L * PV_ZOOM_MIN + ratio / 2) / ratio);
}

static void zoom_roi_from_center(int cx, int cy, int side, int *x, int *y)
{
    int xx = cx - side / 2, yy = cy - side / 2;
    if (xx < 0) xx = 0;
    if (yy < 0) yy = 0;
    if (xx > 1000 - side) xx = 1000 - side;
    if (yy > 1000 - side) yy = 1000 - side;
    *x = xx; *y = yy;
}

int nvr_preview_set_zoom(nvr_preview_t *p, int chn0, int enable,
                         int cx, int cy, int focusx, int focusy, int ratio,
                         int *out_cx, int *out_cy, int *out_ratio, const char **out_result)
{
    if (!p || chn0 < 0 || chn0 >= PV_MAX_CH) return -1;
    pthread_mutex_lock(&p->lock);

    cx = clamp_pm(cx); cy = clamp_pm(cy);
    focusx = clamp_pm(focusx); focusy = clamp_pm(focusy);

    const char *res = "OK";
    if (!enable) {                 /* 关闭 → 文档默认 500,500,100 */
        cx = 500; cy = 500; ratio = PV_ZOOM_MIN;
        mhal_vout_set_crop(chn0, 0, 0, 1000, 1000);
        /* 退出数字变焦:恢复该视图应有的码流(单画面=主/多宫格=子)。 */
        if (p->cfg.cm)
            nvr_chan_set_stream(p->cfg.cm, chn0,
                                (p->display_mode == 1) ? NVR_STREAM_MAIN : NVR_STREAM_SUB);
    } else {
        /* ★ 数字变焦只在**主码流**上裁减(子码流分辨率低且该 proc 路不支持 IN_CROP,返 -10)。
         * 先把该通道解码切到主码流(高清、可裁),裁剪坐标是千分比,mhal 按主码流实际分辨率换算。
         * 解码器重开后由 mhal_crop_apply_pending 把暂存的 ROI 按主码流分辨率落下。 */
        if (p->cfg.cm)
            nvr_chan_set_stream(p->cfg.cm, chn0, NVR_STREAM_MAIN);
        if (ratio > PV_ZOOM_MAX) { ratio = PV_ZOOM_MAX; res = "Exceeds zoom capabilities"; }
        else if (ratio < PV_ZOOM_MIN) ratio = PV_ZOOM_MIN;

        int side = zoom_roi_pm(ratio);   /* 200 → 500；120 → 833；1000 → 100 */
        int x, y;

        /* Focus≠画面中心：焦点在「当前已放大画面」上，先映回原画再摆新 ROI（文档 Comment 5/7） */
        if (focusx != 500 || focusy != 500) {
            int pr = p->zoom[chn0].ratio;
            if (pr < PV_ZOOM_MIN) pr = PV_ZOOM_MIN;
            int ps = zoom_roi_pm(pr);
            int px, py;
            zoom_roi_from_center(p->zoom[chn0].cx, p->zoom[chn0].cy, ps, &px, &py);
            int orig_fx = px + (int)((long)ps * focusx / 1000);
            int orig_fy = py + (int)((long)ps * focusy / 1000);
            x = orig_fx - (int)((long)side * focusx / 1000);
            y = orig_fy - (int)((long)side * focusy / 1000);
        } else {
            x = cx - side / 2;
            y = cy - side / 2;
        }

        int clamped = 0;
        if (x < 0)            { x = 0; clamped = 1; }
        if (x > 1000 - side)  { x = 1000 - side; clamped = 1; }
        if (y < 0)            { y = 0; clamped = 1; }
        if (y > 1000 - side)  { y = 1000 - side; clamped = 1; }
        cx = x + side / 2; cy = y + side / 2;
        if (clamped && res[0] == 'O') res = "Exceeds the zoom range";
        NVR_LOGI("preview", "chn%d ZoomPan enable=%d ratio=%d ROI=%d,%d %dx%d (show %d/1000 of frame)",
                 chn0, enable, ratio, x, y, side, side, side);
        mhal_vout_set_crop(chn0, x, y, side, side);
    }

    p->zoom[chn0].enable = enable ? 1 : 0;
    p->zoom[chn0].cx = cx; p->zoom[chn0].cy = cy; p->zoom[chn0].ratio = ratio;

    if (out_cx) *out_cx = cx;
    if (out_cy) *out_cy = cy;
    if (out_ratio) *out_ratio = ratio;
    if (out_result) *out_result = res;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_get_zoom(nvr_preview_t *p, int chn0, int *enable, int *cx, int *cy, int *ratio)
{
    if (!p || chn0 < 0 || chn0 >= PV_MAX_CH) return -1;
    pthread_mutex_lock(&p->lock);
    if (enable) *enable = p->zoom[chn0].enable;
    if (cx)     *cx     = p->zoom[chn0].cx;
    if (cy)     *cy     = p->zoom[chn0].cy;
    if (ratio)  *ratio  = p->zoom[chn0].ratio;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

/* ============================================================================
 * 新 API（计划 §Task4）：display_mode/page 切换、通道映射、悬浮块、千分比换算
 * ==========================================================================*/

int pv_thousandths_to_px(int v, int span)
{
    long r = ((long)v * span + 500) / 1000;
    if (r < 0) r = 0;
    if (r > span) r = span;
    return (int)r;
}

static mhal_layout_t mode_to_mhal(int mode, int *win_count)
{
    switch (mode) {
        case 1:  *win_count = 1;  return MHAL_LAYOUT_1;
        case 4:  *win_count = 4;  return MHAL_LAYOUT_4;
        case 8:  *win_count = 8;  return MHAL_LAYOUT_8;   /* 1大+7小 */
        case 9:  *win_count = 9;  return MHAL_LAYOUT_9;
        case 25: *win_count = 25; return MHAL_LAYOUT_25;  /* 5x5 */
        case 36: *win_count = 36; return MHAL_LAYOUT_36;  /* 6x6 */
        case 16: *win_count = 16; return MHAL_LAYOUT_16;
        default: *win_count = 16; return MHAL_LAYOUT_16;
    }
}

int nvr_preview_set_mode(nvr_preview_t *p, int mode, int page)
{
    if (!p) return -1;
    pthread_mutex_lock(&p->lock);

    /* 首次调用即视为新 API 已激活（无论 mode 是否为 0）。
     * 用于 on_channel_online/offline 区分新/旧路径（Important2 修复）。 */
    p->new_api_active = 1;

    if (mode == 0) {
        /* 离开宫格 LiveView：只关宫格解码。悬浮块由 set_ext 另管，不在这里清。 */
        for (int c = 0; c < PV_MAX_CH; c++) {
            int in_ext = 0;
            for (int i = 0; i < p->ext_n; i++)
                if (p->ext[i].chn0 == c) { in_ext = 1; break; }
            if (!in_ext) nvr_stream_set_display(p->cfg.sm, c, -1);
        }
        for (int w = 0; w < PV_MAX_WIN; w++) p->win2chn[w] = -1;
        p->display_mode = 0;
        p->display_page = (page > 0 ? page : p->display_page);
        pthread_mutex_unlock(&p->lock);
        return 0;
    }

    /* 回宫格：先清悬浮块，避免旧自由矩形残留 */
    for (int i = 0; i < p->ext_n; i++) mhal_vout_unbind(p->ext[i].chn0);
    p->ext_n = 0;

    int wc = 0;
    mhal_layout_t ml = mode_to_mhal(mode, &wc);
    if (mhal_vout_set_layout(ml) != 0) { pthread_mutex_unlock(&p->lock); return -1; }

    p->display_mode = mode;
    p->display_page = (page > 0 ? page : 1);
    p->win_count = wc;

    /* 先算出本页可见集(格 w ← map0[base+w]) */
    int base = (p->display_page - 1) * wc;
    int vis[PV_MAX_CH]; for (int c = 0; c < PV_MAX_CH; c++) vis[c] = 0;
    for (int w = 0; w < PV_MAX_WIN; w++) p->win2chn[w] = -1;
    for (int w = 0; w < wc; w++) {
        int idx = base + w;
        int chn = (idx >= 0 && idx < PV_MAX_CH) ? p->map0[idx] : -1;
        p->win2chn[w] = chn;
        if (chn >= 0) vis[chn] = 1;
    }
    /* ★ 解码门控(监控通用做法):拉流对所有录像通道常开,解码(硬件VPU)只给可见格。
     *   先关"本页不可见"的解码 → 释放 VPU 预算,再开可见的(避免换页时预算瞬时叠加被拒)。
     *   nvr_stream_set_display 只切解码+绑窗,RTSP 会话不动 → 切布局/翻页无重连开销。 */
    for (int c = 0; c < PV_MAX_CH; c++)
        if (!vis[c]) nvr_stream_set_display(p->cfg.sm, c, -1);
    /* 先定各可见通道的解码码流(单宫格=主/多宫格=子),再开解码 → 直接按目标码流开一次(避免双开)。 */
    int st = (mode == 1) ? NVR_STREAM_MAIN : NVR_STREAM_SUB;
    for (int w = 0; w < wc; w++)
        if (p->win2chn[w] >= 0 && p->cfg.cm)
            nvr_chan_set_stream(p->cfg.cm, p->win2chn[w], st);
    for (int w = 0; w < wc; w++)
        if (p->win2chn[w] >= 0)
            nvr_stream_set_display(p->cfg.sm, p->win2chn[w], w);   /* 可见:按 decode_stream 开解码(内部各自成图 + 喂缓存关键帧秒出) */
    mhal_vout_commit();   /* 兜底再成图一次(确保窗口矩形/可见性生效) */

    pthread_mutex_unlock(&p->lock);
    return 0;
}

/* 阻塞等待:轮询直到**任一可见格已出图**(解码器喂到帧)或超时。切宫格/切码流接口用此"等图再回复"。
 * 不持锁轮询(先快照可见通道),避免阻塞其它 preview 操作。多宫格只要有一格出图即返回。 */
int nvr_preview_wait_ready(nvr_preview_t *p, int timeout_ms)
{
    if (!p || !p->cfg.sm) return -1;
    int vis[PV_MAX_WIN]; int nvis = 0;
    pthread_mutex_lock(&p->lock);
    for (int w = 0; w < p->win_count && w < PV_MAX_WIN; w++)
        if (p->win2chn[w] >= 0) vis[nvis++] = p->win2chn[w];
    for (int i = 0; i < p->ext_n && nvis < PV_MAX_WIN; i++) {
        int chn = p->ext[i].chn0, dup = 0;
        for (int k = 0; k < nvis; k++) if (vis[k] == chn) { dup = 1; break; }
        if (!dup && chn >= 0) vis[nvis++] = chn;
    }
    pthread_mutex_unlock(&p->lock);
    if (nvis == 0) return 0;                 /* 无可见格 → 立即返回 */

    if (timeout_ms <= 0) timeout_ms = NVR_DEF_WAIT_READY_MS;
    if (timeout_ms > NVR_DEF_WAIT_READY_MS) timeout_ms = NVR_DEF_WAIT_READY_MS;/* 上限 */
    /* "喂到帧"只是送进解码器,真正上屏还要解码+合成一段时间。就绪后再沉降一小段(封顶不超总超时)。 */
    const int settle_ms = NVR_DEF_SETTLE_MS;
    int waited = 0, step = 20;
    while (waited < timeout_ms) {
        for (int i = 0; i < nvis; i++)
            if (nvr_stream_display_ready(p->cfg.sm, vis[i])) {
                int extra = settle_ms;
                if (waited + extra > timeout_ms) extra = timeout_ms - waited;   /* 不超总超时 */
                if (extra > 0) usleep((useconds_t)extra * 1000);                /* 沉降:等真上屏 */
                return 0;
            }
        usleep((useconds_t)step * 1000);
        waited += step;
    }
    return 1;   /* 超时(最多 2s) */
}

int nvr_preview_set_mapping(nvr_preview_t *p, const int *map1based, int n)
{
    if (!p || !map1based) return -1;
    pthread_mutex_lock(&p->lock);
    for (int i = 0; i < n && i < PV_MAX_CH; i++) p->map0[i] = map1based[i] - 1; /* 1→0 based */
    /* 若当前已启用某分屏模式，按新映射立即重排 */
    int rc = 0;
    if (p->display_mode > 0) rc = nvr_preview_set_mode(p, p->display_mode, p->display_page);
    pthread_mutex_unlock(&p->lock);
    return rc;
}

/* 把一块悬浮窗绑到屏幕矩形。解码器可能尚未 open（bind_rect 失败则等上线/wait_ready 后再绑）。 */
static void ext_place(nvr_preview_t *p, const nvr_pv_ext_t *e)
{
    int x = pv_thousandths_to_px(e->x, p->disp_w);
    int y = pv_thousandths_to_px(e->y, p->disp_h);
    int w = pv_thousandths_to_px(e->w, p->disp_w);
    int h = pv_thousandths_to_px(e->h, p->disp_h);
    mhal_vout_bind_rect(e->chn0, x, y, w, h);
}

int nvr_preview_set_ext(nvr_preview_t *p, const nvr_pv_ext_t *b, int n)
{
    if (!p) return -1;
    if (n < 0) n = 0;
    if (n > 0 && !b) return -1;
    if (n > PV_MAX_WIN) return -2;

    pthread_mutex_lock(&p->lock);

    /* 清旧块：解绑 + 关解码（拉流/录像不动） */
    for (int i = 0; i < p->ext_n; i++) {
        int chn = p->ext[i].chn0;
        mhal_vout_unbind(chn);
        nvr_stream_set_display(p->cfg.sm, chn, -1);
    }
    p->ext_n = 0;

    if (n == 0) {
        mhal_vout_commit();
        pthread_mutex_unlock(&p->lock);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (b[i].chn0 < 0 || b[i].chn0 >= PV_MAX_CH) {
            pthread_mutex_unlock(&p->lock);
            return -1;
        }
        p->ext[p->ext_n++] = b[i];
        if (p->cfg.cm) nvr_chan_set_stream(p->cfg.cm, b[i].chn0, b[i].stream);
        /* win=0 只为把门控打开(vout 路径随 vdec_open 建好)，真正位置由 bind_rect 定。 */
        nvr_stream_set_display(p->cfg.sm, b[i].chn0, 0);
        ext_place(p, &p->ext[p->ext_n - 1]);
    }
    pthread_mutex_unlock(&p->lock);

    /* 等解码器起来后再绑一次矩形（set_display 在 puller 线程兑现）。 */
    nvr_preview_wait_ready(p, NVR_DEF_WAIT_READY_MS);
    pthread_mutex_lock(&p->lock);
    for (int i = 0; i < p->ext_n; i++) ext_place(p, &p->ext[i]);
    mhal_vout_commit();
    int denied = 0;
    for (int i = 0; i < p->ext_n; i++)
        if (nvr_stream_decode_denied(p->cfg.sm, p->ext[i].chn0)) denied = 1;
    pthread_mutex_unlock(&p->lock);
    return denied ? -2 : 0;
}

int nvr_preview_get_mode(nvr_preview_t *p, int *mode, int *page)
{
    if (!p) return -1;
    pthread_mutex_lock(&p->lock);
    if (mode) *mode = p->display_mode;
    if (page) *page = p->display_page ? p->display_page : 1;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int nvr_preview_get_mapping(nvr_preview_t *p, int *out1based, int cap)
{
    if (!p || !out1based) return 0;
    pthread_mutex_lock(&p->lock);
    int n = 0;
    for (int i = 0; i < PV_MAX_CH && n < cap; i++) out1based[n++] = p->map0[i] + 1;
    pthread_mutex_unlock(&p->lock);
    return n;
}

int nvr_preview_get_ext(nvr_preview_t *p, nvr_pv_ext_t *out, int cap)
{
    if (!p || !out) return 0;
    pthread_mutex_lock(&p->lock);
    int n = 0;
    for (int i = 0; i < p->ext_n && n < cap; i++) out[n++] = p->ext[i];
    pthread_mutex_unlock(&p->lock);
    return n;
}
