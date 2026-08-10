/***************************************************************************************
 *  mhal_internal.h — media_hal 内部共享状态（不对外）
 *
 *  管线（参照 hdal 样例 playback_1div_to_4div.c）：
 *     videodec ──bind──► videoproc(VPE) ──bind──► videoout(HDMI/CVBS)
 *  每通道一条 dec→proc→vout 路径；vout 设备与分屏布局全局管理。
 ***************************************************************************************/
#ifndef MHAL_INTERNAL_H
#define MHAL_INTERNAL_H

#include "hdal.h"                 /* hd_* 全量 API（BSP: code/hdal/include） */
#include "mhal_vdec.h"
#include "mhal_vout.h"

#define MHAL_MAX_CH 32                   /* 32 路（分屏预览最多 32 解码器）*/

/* 单通道解码路径 */
struct mhal_vdec {
    int          chn;
    int          opened;
    HD_VIDEO_CODEC codec;                /* HD_CODEC_TYPE_H264 / H265 (枚举 typedef 名为 HD_VIDEO_CODEC) */
    int          vout_win;               /* 绑定的分屏窗口；-1=不上屏 */
    int          w, h, fps;              /* 实际分辨率/帧率（预算准入 + max_mem 动态分配）*/
    HD_PATH_ID   dec_path;               /* videodec */
    HD_PATH_ID   proc_path;              /* videoproc(VPE) */
    HD_PATH_ID   vout_path;              /* videoout 输入路径 */
    int          crop_on;                /* 数字变焦:IN_CROP 是否已从 OFF 切到 ON(切模式需 start，只切一次) */
};

/* 全局显示设备状态 */
typedef struct {
    int                 inited;
    mhal_out_t          out;             /* HDMI/CVBS/VGA */
    HD_PATH_ID          ctrl_path;       /* videoout 设备控制路径 */
    HD_VIDEOOUT_SYSCAPS syscaps;         /* 含 input_dim（显示分辨率） */
    HD_FB_FMT           fb_fmt;
    int                 disp_w, disp_h;
    mhal_layout_t       layout;          /* 当前分屏 */
    struct mhal_vdec   *ch[MHAL_MAX_CH]; /* 已开的解码通道（按 chn 索引） */

    /* 已 start_list 的显示集合快照（SDK 一次成图流程：stop_list 旧集合 → start_list 新集合，
     * stop 在 start 前 → 任一时刻只需 1 块 VPD_graph_info_large(reserve=1)，
     * 根治边跑边单启触发的 "Can't extend"。见 mhal_vout_commit / SDK stop_module+start_module）。 */
    HD_PATH_ID          started_dec[MHAL_MAX_CH];
    HD_PATH_ID          started_proc[MHAL_MAX_CH];
    HD_PATH_ID          started_vout[MHAL_MAX_CH];
    int                 started_n;

    /* ★ 批量提交:defer>0 时,vdec_open/close 不各自 commit(只改 g_disp.ch/挂 pending),
     * 由 mhal_vout_defer_end 一次 commit(停旧集合→释放 pending→起新集合)。一次切宫格只重成图 1 次,
     * 9 个格一起出、只闪一次(而非一个一个各闪)。 */
    int                 defer;
    struct mhal_vdec   *pending_free[MHAL_MAX_CH];  /* defer 期间关闭、待 commit 后统一释放的解码器 */
    int                 pending_n;
} mhal_disp_t;

/* 重建显示合成图：把当前所有在显通道 dec/proc/vout 路径一次 stop_list→start_list。
 * 通道开/关（增删窗口路径）后调用；纯改窗口矩形/可见性不需要（走 IN_WIN_ATTR，不重启）。 */
int mhal_vout_commit(void);

/* g_disp 全局锁（递归）：多线程访问 —— 预览(8089路由线程) / 各通道 RTSP rx 线程(开关解码器)
 * / 主循环，都会碰 g_disp/videoout。公有入口加锁串行化，避免并发 commit 撕裂 started 集合。
 * 递归锁：open 内部会再调 bind/commit 等已加锁入口。init 时创建。 */
void mhal_lock(void);
void mhal_unlock(void);

extern mhal_disp_t g_disp;

/* 布局：算窗口 idx 在显示区内的矩形 */
void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h);

#endif
