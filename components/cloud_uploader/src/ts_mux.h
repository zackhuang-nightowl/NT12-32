/* ts_mux.h — 极简 MPEG2-TS 封装（H264/H265 视频 + AAC/ADTS 音频）。
 * 产出内存 TS 缓冲，按切片复用。上真机按码流细节调优（PCR/连续性/PES 头）。 */
#ifndef TS_MUX_H
#define TS_MUX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ts_mux ts_mux_t;

ts_mux_t *ts_mux_new(int video_is_h265);
void      ts_mux_free(ts_mux_t *m);

/* 写一个视频 AU（Annex-B，含起始码）。pts90k=90kHz PTS；is_key=RAP/IDR。 */
int ts_mux_write_video(ts_mux_t *m, const uint8_t *annexb, size_t len, uint64_t pts90k, int is_key);
/* 写一个音频帧（ADTS AAC）。 */
int ts_mux_write_audio(ts_mux_t *m, const uint8_t *adts, size_t len, uint64_t pts90k);

/* 当前切片缓冲。 */
const uint8_t *ts_mux_data(ts_mux_t *m, size_t *len);
/* 清空切片缓冲（下一切片重新发 PAT/PMT）。 */
void ts_mux_reset(ts_mux_t *m);

#ifdef __cplusplus
}
#endif
#endif /* TS_MUX_H */
