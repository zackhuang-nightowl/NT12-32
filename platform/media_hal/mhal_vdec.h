/**
 * mhal_vdec.h — 视频硬解抽象（封装 na51090 hd_videodec）
 *
 * 上层（components/streaming）只见本接口，不碰 hdal 私有 API。
 * 实现：mhal_vdec.c 对接 $BSP/code/hdal/include/hd_videodec.h
 * 参考：$BSP/code/hdal/samples/media_flow/playback_*.c
 *
 * ⚠️ 骨架 / 接口占位：函数体待对接 hdal。
 */
#ifndef MHAL_VDEC_H
#define MHAL_VDEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MHAL_CODEC_H264, MHAL_CODEC_H265 } mhal_codec_t;

typedef struct mhal_vdec mhal_vdec_t;

/* 打开失败返回码 */
#define MHAL_VDEC_EBUDGET (-2)   /* 解码吞吐预算超限：拒绝这一路（录像不受影响）*/

/* 每通道开一个解码器；bind_vout>=0 时解码结果直连指定 vout 分屏。
 * w/h/fps = 该路**实际分辨率/帧率**（按数据手册 746 Mpix/s 预算准入 + 动态分配 max_mem）；
 *   传 0 表示未知(不占预算、按上限分配)。超预算返回 MHAL_VDEC_EBUDGET，不开解码器。 */
int  mhal_vdec_open(int chn, mhal_codec_t codec, int w, int h, int fps,
                    int bind_vout_win, mhal_vdec_t **out);

/* 送一帧 Annex-B 裸流（来自 streaming 的 CRtspClient video_cb）；ts 为 90kHz PTS */
int  mhal_vdec_send(mhal_vdec_t *d, const uint8_t *annexb, uint32_t len, uint32_t ts);

/* 拉解码后 YUV（若不直连 vout，需要二次处理/抓拍时用） */
int  mhal_vdec_recv(mhal_vdec_t *d, void *yuv_buf, uint32_t *inout_len, uint32_t timeout_ms);

void mhal_vdec_close(mhal_vdec_t *d);

/* 当前活跃解码器数(供回放确认全局无残留 live 解码器竞争硬件)。 */
int  mhal_vdec_active_count(void);

/* 批量送流:多路解码器帧一次 send_list 送(按 SDK,避免逐路 send 并发竞争 → DEC_HW_TIMEOUT)。 */
int  mhal_vdec_send_multi(mhal_vdec_t **ds, const uint8_t **bufs,
                          const uint32_t *lens, const uint32_t *tss, int n);

#ifdef __cplusplus
}
#endif
#endif /* MHAL_VDEC_H */
