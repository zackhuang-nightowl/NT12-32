/**
 * mhal_vout.h — 视频输出/分屏抽象（封装 na51090 hd_videoout）
 *
 * 上层（app/preview）编排分屏布局，本层落到 HDMI/CVBS。
 * 实现：mhal_vout.c 对接 $BSP/code/hdal/include/hd_videoout.h
 * 参考：$BSP/code/hdal/samples/media_flow/display_with_splice_screen.c
 *
 * ⚠️ 骨架 / 接口占位。
 */
#ifndef MHAL_VOUT_H
#define MHAL_VOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MHAL_OUT_HDMI, MHAL_OUT_CVBS, MHAL_OUT_VGA } mhal_out_t;

/* 布局：1/4/9/16 分屏 */
/* 设备支持的宫格：1/4/8/9/16/25/36(8=1大+7小,其余 n×n)。宫格数由 LVGL 下发的 displayMode 决定。 */
typedef enum { MHAL_LAYOUT_1, MHAL_LAYOUT_4, MHAL_LAYOUT_8, MHAL_LAYOUT_9,
               MHAL_LAYOUT_16, MHAL_LAYOUT_25, MHAL_LAYOUT_36 } mhal_layout_t;

/* ★ 最大分屏页(枚举末项)。窗口几何/最大格数一律由它推导, 不写死 36。
 * 实际可用格数受通道数 MHAL_MAX_CH 限(见 mhal_internal.h): 36 格页里最多 MHAL_MAX_CH 格有画面, 余格恒空。 */
#define MHAL_LAYOUT_MAX  MHAL_LAYOUT_36
int  mhal_layout_cell_count_of(mhal_layout_t layout);   /* 某布局的格数(1/4/8/9/16/25/36) */

int  mhal_vout_init(mhal_out_t out, int width, int height);   /* 如 HDMI 3840x2160 */
void mhal_vout_get_resolution(int *w, int *h);
/** DRM connector status: true if cable plugged (reads /sys/class/drm/.../status). */
void mhal_vout_get_cable_connect(int *hdmi, int *vga);
int  mhal_vout_set_resolution(int w, int h);
int  mhal_vout_set_layout(mhal_layout_t layout);              /* 切分屏 */
int  mhal_vout_commit(void);                                 /* 立即重建显示合成图(整屏 stop_list→start_list) */
/* ★ 请求一次防抖合并的重建: 上层切布局/映射/悬浮块后调它, 由唯一显示线程在静默后**只重建一次**,
 * 与各 puller 异步开/关解码器的重建合并 → 不再每次各自重建(消除 Apply 洪水/整屏黑闪)。 */
void mhal_vout_request_commit(void);
/* ★ 批量提交:begin/end 之间的解码器开/关不各自重成图,由 end 一次成图(9格一起出、只闪一次)。
 * 必须成对调用(可嵌套计数)。切宫格 set_mode 用它包住所有 open/close。 */
void mhal_vout_defer_begin(void);
void mhal_vout_defer_end(void);
int  mhal_vout_is_deferred(void);   /* 1=当前在批量提交中(解码器已开但未 start) */
int  mhal_vout_bind(int win_idx, int decoder_chn);            /* 某分屏窗口绑某解码通道 */
int  mhal_vout_bind_rect(int decoder_chn, int x, int y, int w, int h); /* 任意像素矩形 */
/* 按布局算第 idx 格在 disp_w×disp_h 区域内的矩形(liveView 宫格;含 8=1大+7小)。
 * 回放不用此接口:在 0.8 视频区自行均分(见 nvr_playback pb_cell_rect)。 */
void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h);
int  mhal_vout_unbind(int decoder_chn);                                /* 隐藏该通道窗口 */
void mhal_vout_clear_black(void);                                      /* 整屏(视频层)清稳定黑;回放进入时黑屏用 */
void mhal_vout_enable_auto_clearwin(void);                            /* 开启 AUTO_CLEARWIN:空/停 vo 路窗口自动黑(SDK 方式) */
void mhal_vout_push_black_bg(void);                                   /* 向专用背景窗口 push 全屏黑帧:开机/无设备默认黑 */
/* 数字变焦:对某解码通道设**输入裁剪 ROI**(源画面千分比 0..1000),VPE 把 ROI 放大填满窗口。
 * x_pm/y_pm=0 且 w_pm/h_pm=1000 → 全画面(取消变焦)。返回 0 成功、-1 该通道未在解码。 */
int  mhal_vout_set_crop(int decoder_chn, int x_pm, int y_pm, int w_pm, int h_pm);
void mhal_vout_deinit(mhal_out_t out);

#ifdef __cplusplus
}
#endif
#endif /* MHAL_VOUT_H */
