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
typedef enum { MHAL_LAYOUT_1, MHAL_LAYOUT_4, MHAL_LAYOUT_9, MHAL_LAYOUT_16 } mhal_layout_t;

int  mhal_vout_init(mhal_out_t out, int width, int height);   /* 如 HDMI 3840x2160 */
int  mhal_vout_set_layout(mhal_layout_t layout);              /* 切分屏 */
int  mhal_vout_bind(int win_idx, int decoder_chn);            /* 某分屏窗口绑某解码通道 */
int  mhal_vout_bind_rect(int decoder_chn, int x, int y, int w, int h); /* 任意像素矩形 */
int  mhal_vout_unbind(int decoder_chn);                                /* 隐藏该通道窗口 */
int  mhal_vout_osd(int win_idx, const char *text);           /* 通道名/时间/事件 OSD 叠加 */
void mhal_vout_deinit(mhal_out_t out);

#ifdef __cplusplus
}
#endif
#endif /* MHAL_VOUT_H */
