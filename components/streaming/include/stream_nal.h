/* stream_nal.h — Annex-B 裸流首个 VCL NAL 分类（判关键帧 / I·P）纯 C */
#ifndef STREAM_NAL_H
#define STREAM_NAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int is_key;      /* 1=关键帧(IDR/IRAP) / 含参数集的关键帧 AU / 纯参数集 → 录像可从此起 */
    int is_param;    /* 1=**纯**参数集帧(只有 VPS/SPS/PPS,无 VCL slice) → 硬解可扣留缓存 */
    int has_param;   /* 1=帧内含 SPS/PPS(可能同时有 slice=合并关键帧 AU,如某些相机) */
    int frame_type;  /* RSDK_FRAME_I=0 / RSDK_FRAME_P=1 */
} nal_class_t;

/* codec: 0=H264 1=H265。扫描 Annex-B(00 00 01 / 00 00 00 01)首个有意义 NAL 分类。 */
void nal_classify(const uint8_t *data, int len, int codec, nal_class_t *out);

/* H.264 连续性(配合 live 起播门控):从 SPS 取 log2_max_frame_num,从 VCL 取 frame_num。
 * 返回 1=相对 prev 发生 frame_num 跳变(参考链可能已断,应丢 P 等 IDR);0=连续/无法判定/IDR 重置。
 * *log2_io:0=未知,解析到 SPS 时写入; *prev_fn_io:-1=无前帧。 */
int nal_h264_frame_num_gap(const uint8_t *data, int len, int is_idr,
                           int *log2_io, int *prev_fn_io);

/* 从整帧 Annex-B 里找 SPS(H264 type7 / H265 type33)解析**真实编码分辨率**(含裁剪窗修正)。
 * codec:0=H264 1=H265。成功返回 1 并填 *w/*h(luma 采样,已去 crop);失败返回 0。
 * 调用方须做保护:失败时**不得**用猜测尺寸开解码器。 */
int nal_sps_dims(const uint8_t *data, int len, int codec, int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif
