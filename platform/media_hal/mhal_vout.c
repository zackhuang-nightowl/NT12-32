/***************************************************************************************
 *  mhal_vout.c — 视频输出/分屏（封装 hd_videoout）
 *
 *  参照 hdal 样例 playback_1div_to_4div.c 的 init/open/get-syscaps 流程。
 *  ⚠️ 仅在 na51090 BSP 交叉环境编译（依赖 hdal 库 + dts 内存池）。
 ***************************************************************************************/
#include "mhal_internal.h"
#include "nvr_log.h"
#include "nvr_display_modes.h"    /* HDMI 分辨率阶梯单一来源(热切降级) */
#include "vendor_videoout.h"      /* VENDOR_VIDEOOUT_PARAM_AUTO_CLEARWIN:空/停路窗口自动清黑 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>

mhal_disp_t g_disp;

/* g_disp 递归锁：见 mhal_internal.h。首次用时惰性初始化(mhal_vout_init 先于任何流线程)。 */
static pthread_mutex_t g_mhal_mtx;
static int g_mhal_mtx_ready;
static void mhal_lock_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_mhal_mtx, &a);
    pthread_mutexattr_destroy(&a);
    g_mhal_mtx_ready = 1;
}
void mhal_lock(void)   { if (!g_mhal_mtx_ready) mhal_lock_init(); pthread_mutex_lock(&g_mhal_mtx); }
void mhal_unlock(void) { if (g_mhal_mtx_ready) pthread_mutex_unlock(&g_mhal_mtx); }

/* 分屏几何：1/4/9/16 → 网格边长 1/2/3/4 */
/* 当前布局的有效格子数（0..count-1 为屏内格；>=count 的窗号无对应格，须隐藏而非越界下发）。 */
static int mhal_layout_cell_count(mhal_layout_t layout)
{
    switch (layout) {
        case MHAL_LAYOUT_1:  return 1;
        case MHAL_LAYOUT_4:  return 4;
        case MHAL_LAYOUT_8:  return 8;
        case MHAL_LAYOUT_9:  return 9;
        case MHAL_LAYOUT_16: return 16;
        case MHAL_LAYOUT_25: return 25;
        case MHAL_LAYOUT_36: return 36;
        default:             return 16;
    }
}

void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h)
{
    /* 8 宫格：特殊布局 1 大 + 7 小（4x4 网格里，大窗占左上 3x3，右列4小+底行3小=7）。 */
    if (layout == MHAL_LAYOUT_8) {
        int cw = disp_w / 4, ch = disp_h / 4;
        if (idx <= 0)      { *x = 0;        *y = 0;        *w = 3 * cw; *h = 3 * ch; }   /* 大窗 */
        else if (idx <= 4) { *x = 3 * cw;   *y = (idx - 1) * ch; *w = cw; *h = ch; }      /* 右列 4 小 */
        else               { *x = (idx - 5) * cw; *y = 3 * ch;   *w = cw; *h = ch; }      /* 底行 3 小 */
        return;
    }
    /* 方阵：1/4/9/16/25/36 → n = 1/2/3/4/5/6，每格 disp/n。 */
    int n = (layout == MHAL_LAYOUT_1)  ? 1 :
            (layout == MHAL_LAYOUT_4)  ? 2 :
            (layout == MHAL_LAYOUT_9)  ? 3 :
            (layout == MHAL_LAYOUT_16) ? 4 :
            (layout == MHAL_LAYOUT_25) ? 5 : 6;   /* 36 */
    int cw = disp_w / n, ch = disp_h / n;
    *x = (idx % n) * cw;
    *y = (idx / n) * ch;
    *w = cw;
    *h = ch;
}

/* 设 HDMI 输出模式(支持 3840x2160 / 1920x1080 / 1280x720)。成功返 0。
 * 业务逻辑:分辨率可选(按所接屏);由 mhal_vout_init/切换接口调用,失败方回落 1080p。 */
static int set_hdmi_mode(HD_PATH_ID ctrl, int w, int h)
{
    HD_VIDEOOUT_MODE mode;
    memset(&mode, 0, sizeof(mode));
    mode.output_type = HD_COMMON_VIDEO_OUT_HDMI;
    /* ⚠️ input_dim 必须**显式**给合成画布尺寸,不能用 AUTO——AUTO 会退到 lcd_0(720x576),
     * 导致视频窗口越界报 lcd_0 boundary error、视频落到模拟口。见样例 display_with_change_mode.c。 */
    if      (w == 3840 && h == 2160) { mode.input_dim = HD_VIDEOOUT_IN_3840x2160; mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_3840X2160P30; }
    else if (w == 1280 && h == 720)  { mode.input_dim = HD_VIDEOOUT_IN_1280x720;  mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_1280X720P60; }
    else                             { mode.input_dim = HD_VIDEOOUT_IN_1920x1080; mode.output_mode.hdmi = HD_VIDEOOUT_HDMI_1920X1080P60; }
    return (hd_videoout_set(ctrl, HD_VIDEOOUT_PARAM_MODE, &mode) == HD_OK) ? 0 : -1;
}

/* fb 透明方案A(逐像素 alpha)：对各 fb 关掉 colorkey，使 ARGB 层按像素 alpha 与
 * 下方视频 plane 混合(UI 没画处 alpha=0 → 透出视频)。这也去掉"以绿为键色却没开
 * colorkey → 实心全绿"的现象。保留原 alpha_blend(ARGB 会忽略它、按像素 alpha)。 */
static void mhal_vout_setup_fb_alpha(void)
{
    int id;
    for (id = HD_FB0; id <= HD_FB2; id++) {
        HD_FB_ATTR fa;
        memset(&fa, 0, sizeof(fa));
        fa.fb_id = (HD_FB_ID)id;
        if (hd_videoout_get(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_FB_ATTR, &fa) != HD_OK)
            continue;
        fa.colorkey_en = 0;   /* 方案A：不用抠像，走 ARGB 像素 alpha */
        hd_videoout_set(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_FB_ATTR, &fa);
    }
}

/* 重建显示合成图（SDK playback_1div_to_4div stop_module→start_module 同款）：
 *   收集当前所有"在显"(vout_win>=0 且已开)通道的 dec/proc/vout 路径；
 *   ① 先 stop_list 上次起过的集合 —— **释放旧 VPD_graph_info_large 合成 graph**；
 *   ② 再 start_list 新集合 —— 一次成图。
 * 关键：stop 在 start 之前，任一时刻只需 1 块 large graph（该 cache reserve=1/can_extend=1，
 * 后备扩不出第二块）。→ 根治"边跑边逐路 hd_videoout_start 重建整屏合成、瞬时要 2 块"导致的
 * `ms_cache_alloc: Can't extend ms_cache_mem VPD_graph_info_large` / `vpd_set_graph fail -2`。
 * 通道开/关后调用；仅改窗口矩形/可见性走 IN_WIN_ATTR，不需重建（见 apply_window）。 */
/* 把视频层(videoout)清成稳定黑 —— 根治"空白格绿色随视频帧刷新而闪":
 * 空白区(无视频窗的格)若从不清,内容不定,LVGL 绿 colorkey 抠出来就跟着闪。
 * 用 HD_VIDEOOUT_PARAM_CLEAR_WIN(样例 liveview_with_clearwin.c)填黑。每次成图后调,
 * 覆盖整屏(视频窗随后由 start_list 盖回自己那格,空格保持稳定黑)。
 * USER_BLK 池未配置则优雅跳过(不影响出图)。 */
void mhal_vout_clear_black(void)
{
    if (!g_disp.inited || !g_disp.ctrl_path) return;

    const UINT32 BUF_SZ = 1024 * 1024;
    /* USER_BLK 池在系统 DDR(DDR_ID0);直接用,免 vendor_common_get_ddrid 依赖。 */
    HD_COMMON_MEM_VB_BLK blk = hd_common_mem_get_block(HD_COMMON_MEM_USER_BLK, BUF_SZ, DDR_ID0);
    if (blk == HD_COMMON_MEM_VB_INVALID_BLK) { NVR_LOGE("mhal", "clearwin get_block fail"); return; }
    UINTPTR pa = hd_common_mem_blk2pa(blk);
    UINT8 *va = (UINT8 *)hd_common_mem_mmap(HD_COMMON_MEM_MEM_TYPE_NONCACHE, pa, BUF_SZ);
    if (!va) { hd_common_mem_release_block(blk); return; }

    HD_VIDEOOUT_CLEAR_WIN cw; memset(&cw, 0, sizeof(cw));
    cw.input_dim.w = (g_disp.disp_w > 1920) ? 512 : 256;
    cw.input_dim.h = (g_disp.disp_h > 1080) ? 272 : 136;
    cw.in_fmt = HD_VIDEO_PXLFMT_YUV422_ONE;
    cw.buf = va;
    /* 统一采用 SDK 样例 liveview_with_clearwin.c 的 WINCOLOR_BLACK=0x10801080:
     * uint32 LE 字节序 [80 10 80 10] = UYVY [U=0x80 Y=0x10 V=0x80 Y=0x10] → Y=16(视频黑,限幅
     * 范围下限)、U=V=128(中性)。之前用 0x00800080(Y=0)是"低于黑",部分屏渲染偏色/发绿。 */
    UINT32 pat = 0x10801080;
    for (UINT32 i = 0; i < (cw.input_dim.w * cw.input_dim.h / 2); i++)
        *(UINT32 *)(cw.buf + 4 * i) = pat;
    cw.output_rect.x = 0; cw.output_rect.y = 0;
    cw.output_rect.w = g_disp.disp_w; cw.output_rect.h = g_disp.disp_h;
    cw.mode = HD_ACTIVE_IMMEDIATELY;

    if (hd_videoout_set(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_CLEAR_WIN, &cw) != HD_OK)
        NVR_LOGE("mhal", "clearwin set fail");
    else
        NVR_LOGD("mhal", "clearwin: 视频层清黑 %dx%d", g_disp.disp_w, g_disp.disp_h);

    hd_common_mem_munmap(va, BUF_SZ);
    hd_common_mem_release_block(blk);
}

/* defer 期间关闭的解码器,路径已随 commit 的 stop_list 停 → 此处统一拆绑/关路径/归还预算/释放。 */
void mhal_vdec_teardown(struct mhal_vdec *d);   /* mhal_vdec.c 提供 */

void mhal_vout_defer_begin(void)
{
    mhal_lock();
    g_disp.defer++;
    mhal_unlock();
}

int mhal_vout_is_deferred(void) { return g_disp.defer > 0; }

void mhal_vout_defer_end(void)
{
    mhal_lock();
    if (g_disp.defer > 0) g_disp.defer--;
    int doit = (g_disp.defer == 0);
    mhal_unlock();
    if (doit) mhal_vout_commit();   /* 批量结束 → 一次成图(9格一起出、只闪一次) */
}

int mhal_vout_commit(void)
{
    mhal_lock();
    if (!g_disp.inited) { mhal_unlock(); return -1; }

    HD_PATH_ID dec[MHAL_MAX_CH], proc[MHAL_MAX_CH], vout[MHAL_MAX_CH];
    int n = 0, rc = 0;
    for (int i = 0; i < MHAL_MAX_CH; i++) {
        struct mhal_vdec *d = g_disp.ch[i];
        /* vout_win≥0=宫格窗; -2=自由矩形(bind_rect,回放 0.8 区/displayExt)。二者都在显,须进 start_list。 */
        if (d && d->opened && (d->vout_win >= 0 || d->vout_win == -2) &&
            d->vout_path && d->proc_path && d->dec_path) {
            dec[n] = d->dec_path; proc[n] = d->proc_path; vout[n] = d->vout_path;
            n++;
        }
    }

    /* ① 停旧集合（顺序与 SDK stop_module 一致：dec→proc→vout），释放旧 large graph */
    if (g_disp.started_n > 0) {
        hd_videodec_stop_list(g_disp.started_dec,  g_disp.started_n);
        hd_videoproc_stop_list(g_disp.started_proc, g_disp.started_n);
        hd_videoout_stop_list(g_disp.started_vout,  g_disp.started_n);
        g_disp.started_n = 0;
    }

    /* ①.5 释放 defer 期间关闭的解码器(其 dec/proc/vout 路径已随上面 stop_list 停,现拆绑/关/释放) */
    for (int i = 0; i < g_disp.pending_n; i++)
        if (g_disp.pending_free[i]) mhal_vdec_teardown(g_disp.pending_free[i]);
    g_disp.pending_n = 0;

    /* ② 一次性起新集合（SDK start_module 顺序：dec→proc→vout），只建 1 块 large graph */
    if (n > 0) {
        HD_RESULT ret;
        if ((ret = hd_videodec_start_list(dec, n)) != HD_OK)      { NVR_LOGE("mhal", "dec_start_list(%d) fail %d", n, ret); rc = -1; }
        else if ((ret = hd_videoproc_start_list(proc, n)) != HD_OK){ NVR_LOGE("mhal", "proc_start_list(%d) fail %d", n, ret); rc = -1; }
        else if ((ret = hd_videoout_start_list(vout, n)) != HD_OK) { NVR_LOGE("mhal", "vout_start_list(%d) fail %d", n, ret); rc = -1; }
        else {
            memcpy(g_disp.started_dec,  dec,  n * sizeof(HD_PATH_ID));
            memcpy(g_disp.started_proc, proc, n * sizeof(HD_PATH_ID));
            memcpy(g_disp.started_vout, vout, n * sizeof(HD_PATH_ID));
            g_disp.started_n = n;
            NVR_LOGD("mhal", "commit: %d 窗一次成图(start_list)", n);
            mhal_vout_clear_black();   /* 空白区清稳定黑(空格不闪) */
        }
    }
    mhal_unlock();
    return rc;
}

/* 请求分辨率并落地到 g_disp.disp_w/h:屏幕不支持则沿阶梯(NVR_DISPLAY_MODES,高→低)逐级下探到可用者。
 * ⚠️ disp_w/h 必须取实际生效值(越界会落 lcd_0 不上屏)。供 init 与 set_resolution 共用。 */
static void apply_hdmi_resolution(int width, int height)
{
    static const struct { int w, h; } ladder[] = {
#define X(w, h, label) { (w), (h) },
        NVR_DISPLAY_MODES(X)
#undef X
    };
    const unsigned nl = (unsigned)(sizeof(ladder) / sizeof(ladder[0]));
    int tw = width  > 0 ? width  : NVR_DISPLAY_DEFAULT_W;
    int th = height > 0 ? height : NVR_DISPLAY_DEFAULT_H;
    int ok = (set_hdmi_mode(g_disp.ctrl_path, tw, th) == 0);
    if (!ok) {
        long req_area = (long)tw * th;
        for (unsigned i = 0; i < nl; i++) {
            if ((long)ladder[i].w * ladder[i].h >= req_area) continue;
            if (set_hdmi_mode(g_disp.ctrl_path, ladder[i].w, ladder[i].h) == 0) {
                NVR_LOGW("mhal", "HDMI %dx%d 屏幕不支持,降级到 %dx%d", tw, th, ladder[i].w, ladder[i].h);
                tw = ladder[i].w; th = ladder[i].h; ok = 1; break;
            }
        }
    }
    if (!ok) { tw = NVR_DISPLAY_DEFAULT_W; th = NVR_DISPLAY_DEFAULT_H; set_hdmi_mode(g_disp.ctrl_path, tw, th); }
    g_disp.disp_w = tw; g_disp.disp_h = th;
    NVR_LOGD("mhal", "HDMI 输出实际 %dx%d", g_disp.disp_w, g_disp.disp_h);
}

int mhal_vout_init(mhal_out_t out, int width, int height)
{
    HD_RESULT ret;
    mhal_lock_init();                 /* 单线程启动期先建好递归锁 */
    memset(&g_disp, 0, sizeof(g_disp));
    g_disp.out    = out;
    g_disp.layout = MHAL_LAYOUT_16;

    /* 1) 平台公共初始化 + 各引擎 init（dec/proc/out）。
     * hd_common_init 的入参与 DRAM/chip 数相关，取值随板级 dts；见样例 §hd_common_init 注释。 */
    if ((ret = hd_common_init(0)) != HD_OK) { NVR_LOGE("mhal", "hd_common_init fail %d", ret); return -1; }
    if ((ret = hd_videodec_init())  != HD_OK) return -1;
    if ((ret = hd_videoproc_init()) != HD_OK) return -1;
    if ((ret = hd_videoout_init())  != HD_OK) return -1;

    /* 2) 打开 videoout 设备控制路径（样例：hd_videoout_open(0, HD_VIDEOOUT_0_CTRL, &ctrl)） */
    if ((ret = hd_videoout_open(0, HD_VIDEOOUT_0_CTRL, &g_disp.ctrl_path)) != HD_OK) {
        NVR_LOGE("mhal", "videoout ctrl open fail %d", ret);
        return -1;
    }

    /* 3) 取显示能力（input_dim = 面板/HDMI 分辨率）与 FB 格式 */
    if (hd_videoout_get(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_SYSCAPS, &g_disp.syscaps) != HD_OK) return -1;
    g_disp.fb_fmt.fb_id = HD_FB0;
    hd_videoout_get(g_disp.ctrl_path, HD_VIDEOOUT_PARAM_FB_FMT, &g_disp.fb_fmt);

    /* HDMI 输出分辨率：config 给的(4K/1080p 可选,按所接屏)；设失败 → 回落 1920x1080。
     * ⚠️ disp_w/disp_h 必须取**实际生效**分辨率——之前 config 写 4K 但屏是 1080p，
     * 设 4K 失败后仍按 3840 算窗口 → 越界到 lcd_0(720x576) → 不上屏。现在回落后按 1080p 算。 */
    /* HDMI 输出分辨率:按请求;屏幕不支持沿阶梯降级(取实际生效值)。 */
    apply_hdmi_resolution(width, height);

    g_disp.inited = 1;
    /* ★ 开机默认黑(LVGL 走 ARGB1555 逐像素 alpha 挖透明,视频层无信号时默认是绿 → 必须由 NVR 兜黑):
     *  ① setup_fb_alpha —— 关 OSD FB colorkey、走 ARGB1555 逐像素 alpha(与 GUI 一致)。
     *  ② AUTO_CLEARWIN(SDK liveview_with_clearwin.c 方式)—— 开启后驱动**自动**把"未起/已停 vo 路"
     *     的窗口区域清成黑,持续生效:开机无通道、无设备、通道掉线,对应区域都默认黑(核心兜底)。
     *  ③ clear_black —— 再显式挂一帧视频黑(WINCOLOR_BLACK),保证开机瞬间即黑不闪绿。 */
    mhal_vout_setup_fb_alpha();
    mhal_vout_enable_auto_clearwin();
    mhal_vout_push_black_bg();     /* ★ 挂全屏黑帧到专用背景窗口:开机/无设备立即黑(核心) */
    mhal_vout_clear_black();
    return 0;
}

/* ★ 开机默认黑的核心:向一个**专用全屏背景 vo 窗口**直接 push 一帧黑 YUV(SDK user_videoout.c
 * 的 hd_videoout_push_in_buf 方式,无需 dec/proc/start_list 即刻上屏)。
 * 背景窗口用远离通道(0..31)的 IN(0,63),置最低层 HD_LAYER1 → 后开的通道窗口自然盖在其上。
 * 一帧常驻:开机无通道/无设备时整屏是这帧黑;通道上图后盖住,空处仍是黑(替代"露绿")。 */
#define MHAL_BG_WIN_ID 63
static HD_PATH_ID           g_bg_path = 0;
static HD_COMMON_MEM_VB_BLK g_bg_blk  = HD_COMMON_MEM_VB_INVALID_BLK;

void mhal_vout_push_black_bg(void)
{
    if (!g_disp.inited) return;
    int w = g_disp.disp_w > 0 ? g_disp.disp_w : 1920;
    int h = g_disp.disp_h > 0 ? g_disp.disp_h : 1080;
    HD_RESULT ret;

    /* 1) 首次:开背景 vo 路 + 全屏窗口 + 置最低层 */
    if (!g_bg_path) {
        if ((ret = hd_videoout_open(HD_VIDEOOUT_IN(0, MHAL_BG_WIN_ID),
                                    HD_VIDEOOUT_OUT(0, 0), &g_bg_path)) != HD_OK) {
            NVR_LOGW("mhal", "背景 vo 路 open 失败 %d(跳过默认黑背景)", ret);
            g_bg_path = 0; return;
        }
        HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
        win.rect.x = 0; win.rect.y = 0; win.rect.w = w; win.rect.h = h;
        win.visible = 1;
        hd_videoout_set(g_bg_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);
        VENDOR_VIDEOOUT_WIN_LAYER_ATTR la; memset(&la, 0, sizeof(la));
        la.layer = HD_LAYER1;   /* 最低层(背景),通道窗口在其上 */
        vendor_videoout_set(g_bg_path, VENDOR_VIDEOOUT_PARAM_WIN_LAYER_ATTR, &la);
    }

    /* 2) 分配全屏黑 YUV422(UYVY)缓冲并 push 一帧(常驻显示,不释放该 block) */
    UINT32 sz = (UINT32)w * h * 2;
    if (g_bg_blk == HD_COMMON_MEM_VB_INVALID_BLK) {
        g_bg_blk = hd_common_mem_get_block(HD_COMMON_MEM_USER_BLK, sz, DDR_ID0);
        if (g_bg_blk == HD_COMMON_MEM_VB_INVALID_BLK) {
            NVR_LOGW("mhal", "背景黑帧 get_block 失败(跳过)"); return;
        }
    }
    UINTPTR pa = hd_common_mem_blk2pa(g_bg_blk);
    UINT8 *va = (UINT8 *)hd_common_mem_mmap(HD_COMMON_MEM_MEM_TYPE_NONCACHE, pa, sz);
    if (!va) { NVR_LOGW("mhal", "背景黑帧 mmap 失败"); return; }
    for (UINT32 i = 0; i < sz / 4; i++) *(UINT32 *)(va + 4 * i) = 0x10801080;  /* WINCOLOR_BLACK */
    hd_common_mem_munmap(va, sz);

    HD_VIDEO_FRAME f; memset(&f, 0, sizeof(f));
    f.sign        = MAKEFOURCC('V', 'F', 'R', 'M');
    f.ddr_id      = DDR_ID0;
    f.pxlfmt      = HD_VIDEO_PXLFMT_YUV422_ONE;
    f.dim.w       = w;         f.dim.h       = h;
    f.pw[0]       = w;         f.ph[0]       = h;
    f.loff[0]     = (UINT32)w * 2;                 /* UYVY 行跨距 */
    f.phy_addr[0] = pa;
    f.blk         = g_bg_blk;
    if ((ret = hd_videoout_push_in_buf(g_bg_path, &f, NULL, 0)) != HD_OK)
        NVR_LOGW("mhal", "背景黑帧 push_in_buf 失败 %d", ret);
    else
        NVR_LOGI("mhal", "背景黑帧已上屏(IN63 全屏 %dx%d,最低层):开机/无设备默认黑", w, h);
}

/* SDK 统一方式:开启 videoout AUTO_CLEARWIN —— 驱动自动把未起/已停的 vo 路窗口区清黑。
 * 一次开启持续生效:无视频/无设备/掉线的区域都默认黑(不再露出视频层绿底)。 */
void mhal_vout_enable_auto_clearwin(void)
{
    if (!g_disp.inited || !g_disp.ctrl_path) return;
    VENDOR_VIDEOOUT_AUTO_CLEARWIN acw;
    memset(&acw, 0, sizeof(acw));
    acw.enable = 1;
    if (vendor_videoout_set(g_disp.ctrl_path, VENDOR_VIDEOOUT_PARAM_AUTO_CLEARWIN, &acw) != HD_OK)
        NVR_LOGW("mhal", "AUTO_CLEARWIN 开启失败(vendor_videoout_set)");
    else
        NVR_LOGI("mhal", "AUTO_CLEARWIN 开启:空/停 vo 路窗口默认黑");
}

void mhal_vout_get_resolution(int *w, int *h)
{
    if (w) *w = g_disp.disp_w;
    if (h) *h = g_disp.disp_h;
}

/* 运行期热切 HDMI 输出分辨率(setSysDisplay)。只改输出模式 + 画布尺寸 + 清屏;窗口重排由
 * 上层 preview(nvr_preview_set_hdmi)负责。屏幕不支持按阶梯降级,生效值经 get 读回。 */
int mhal_vout_set_resolution(int w, int h)
{
    mhal_lock();
    if (!g_disp.inited) { mhal_unlock(); return -1; }
    apply_hdmi_resolution(w, h);
    mhal_vout_clear_black();
    mhal_unlock();
    return 0;
}

int mhal_vout_set_layout(mhal_layout_t layout)
{
    mhal_lock();
    if (!g_disp.inited) { mhal_unlock(); return -1; }
    g_disp.layout = layout;
    /* 对已开通道重算窗口矩形并下发（proc_out.rect + videoout IN_WIN_ATTR）。
     * 单窗口属性下发见 mhal_vdec.c 里 apply_window()（open 时用同一函数）。 */
    for (int i = 0; i < MHAL_MAX_CH; i++)
        if (g_disp.ch[i] && g_disp.ch[i]->vout_win >= 0)
            mhal_vout_bind(g_disp.ch[i]->vout_win, i);
    mhal_vout_clear_black();   /* 切布局把整屏清稳定黑,清掉旧布局残留(如全屏帧),再由各窗刷新 */
    mhal_unlock();
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
    HD_RESULT rp = hd_videoproc_set(d->proc_path, HD_VIDEOPROC_PARAM_OUT, &po);

    /* videoout 输入窗口属性（位置 + 可见） */
    HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
    win.rect.x = x; win.rect.y = y; win.rect.w = w; win.rect.h = h;
    win.visible = 1;
    HD_RESULT rw = hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);
    NVR_LOGD("mhal", "apply_window chn%d win=[%d,%d %dx%d] vis=1 proc_set=%d vout_set=%d bg=%dx%d",
             d->chn, x, y, w, h, rp, rw, g_disp.disp_w, g_disp.disp_h);
}

int mhal_vout_bind(int win_idx, int decoder_chn)
{
    mhal_lock();
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) { mhal_unlock(); return -1; }
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) { mhal_unlock(); return -1; }

    /* ★ 越界守卫:win_idx 超出当前布局格数(如 9宫格里 vout_win=10)→ mhal_layout_rect 会算出
     * 屏外矩形(y+h>屏高),videoout 报 boundary error 并使整批 start_list 失败 → 全屏黑。
     * 此时该通道在当前布局/页无可见格:隐藏其窗(仍在解码,供录像/切大布局或翻页时恢复),
     * 记下期望窗号 d->vout_win 以便布局变大后由 set_layout 重绑显示。 */
    if (win_idx < 0 || win_idx >= mhal_layout_cell_count(g_disp.layout)) {
        HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
        win.visible = 0;
        hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);
        d->vout_win = win_idx;
        NVR_LOGD("mhal", "bind chn%d win%d 超出布局格数 → 隐藏(不越界下发)", decoder_chn, win_idx);
        mhal_unlock();
        return 0;
    }

    int x, y, w, h;
    mhal_layout_rect(g_disp.layout, win_idx, g_disp.disp_w, g_disp.disp_h, &x, &y, &w, &h);

    apply_window(d, x, y, w, h);

    d->vout_win = win_idx;
    mhal_unlock();
    return 0;
}

int mhal_vout_bind_rect(int decoder_chn, int x, int y, int w, int h)
{
    mhal_lock();
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) { mhal_unlock(); return -1; }
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) { mhal_unlock(); return -1; }

    apply_window(d, x, y, w, h);

    d->vout_win = -2; /* 自由矩形，非宫格窗口号 */
    mhal_unlock();
    return 0;
}

int mhal_vout_unbind(int decoder_chn)
{
    mhal_lock();
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) { mhal_unlock(); return -1; }
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d) { mhal_unlock(); return -1; }

    HD_VIDEOOUT_WIN_ATTR win; memset(&win, 0, sizeof(win));
    win.visible = 0;
    hd_videoout_set(d->vout_path, HD_VIDEOOUT_PARAM_IN_WIN_ATTR, &win);

    d->vout_win = -1;
    mhal_unlock();
    return 0;
}

/* 数字变焦:把解码源(d->w×d->h)的一块 ROI 送 VPE 放大填满窗口。ROI 用源画面千分比给,
 * 转像素后 4 对齐并夹在帧内。全画面(x=0,y=0,w=h=1000)= 取消变焦。
 * 机制:videoproc 输入裁剪 HD_VIDEOPROC_PARAM_IN_CROP —— VPE 只取 ROI 再缩放到 OUT 窗口。 */
static int align4_down(int v) { return v & ~3; }
static int align4_up(int v)   { return (v + 3) & ~3; }

int mhal_vout_set_crop(int decoder_chn, int x_pm, int y_pm, int w_pm, int h_pm)
{
    mhal_lock();
    if (!g_disp.inited || decoder_chn < 0 || decoder_chn >= MHAL_MAX_CH) { mhal_unlock(); return -1; }
    struct mhal_vdec *d = g_disp.ch[decoder_chn];
    if (!d || !d->opened || !d->proc_path) { mhal_unlock(); return -1; }

    /* 千分比 → 源像素(d->w/h 已 64 对齐);裁剪窗 4 对齐并夹在帧内 */
    int sw = d->w, sh = d->h;
    int cw = align4_up((int)((long)w_pm * sw / 1000));
    int ch = align4_up((int)((long)h_pm * sh / 1000));
    int cx = align4_down((int)((long)x_pm * sw / 1000));
    int cy = align4_down((int)((long)y_pm * sh / 1000));
    if (cw <= 0 || cw > sw) cw = sw;
    if (ch <= 0 || ch > sh) ch = sh;
    if (cx < 0) cx = 0; if (cx > sw - cw) cx = sw - cw;
    if (cy < 0) cy = 0; if (cy > sh - ch) cy = sh - ch;

    HD_VIDEOPROC_CROP crop; memset(&crop, 0, sizeof(crop));
    crop.mode = HD_CROP_ON;                 /* 恒 ON;取消变焦=全帧 rect,不切模式(免重启) */
    crop.win.coord.w = 0; crop.win.coord.h = 0;   /* {0,0}=像素坐标 */
    crop.win.rect.x = cx; crop.win.rect.y = cy;
    crop.win.rect.w = cw; crop.win.rect.h = ch;
    HD_RESULT ret = hd_videoproc_set(d->proc_path, HD_VIDEOPROC_PARAM_IN_CROP, &crop);
    if (ret != HD_OK) {
        NVR_LOGE("mhal", "chn%d 设输入裁剪失败 %d (ROI %d,%d %dx%d / 源 %dx%d)",
                 decoder_chn, ret, cx, cy, cw, ch, sw, sh);
        mhal_unlock();
        return -1;
    }
    /* 首次 OFF→ON 切了 crop 模式 → 按 HDAL 要求重启该 videoproc 路径(之后仅改 rect 无需重启)。 */
    if (!d->crop_on) { hd_videoproc_start(d->proc_path); d->crop_on = 1; }
    NVR_LOGI("mhal", "chn%d 数字变焦 ROI=%d,%d %dx%d (源 %dx%d)", decoder_chn, cx, cy, cw, ch, sw, sh);
    mhal_unlock();
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
