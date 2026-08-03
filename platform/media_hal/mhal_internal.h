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
} mhal_disp_t;

extern mhal_disp_t g_disp;

/* 布局：算窗口 idx 在显示区内的矩形 */
void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h);

#endif
