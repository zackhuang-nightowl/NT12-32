/* stream_nal.h — Annex-B 裸流首个 VCL NAL 分类（判关键帧 / I·P）纯 C */
#ifndef STREAM_NAL_H
#define STREAM_NAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int is_key;      /* 1=关键帧(IDR/IRAP) 或 参数集(VPS/SPS/PPS) → 录像可从此起 */
    int is_param;    /* 1=纯参数集 NAL */
    int frame_type;  /* RSDK_FRAME_I=0 / RSDK_FRAME_P=1 */
} nal_class_t;

/* codec: 0=H264 1=H265。扫描 Annex-B(00 00 01 / 00 00 00 01)首个有意义 NAL 分类。 */
void nal_classify(const uint8_t *data, int len, int codec, nal_class_t *out);

#ifdef __cplusplus
}
#endif
#endif
