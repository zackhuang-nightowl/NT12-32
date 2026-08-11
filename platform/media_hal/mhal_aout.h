/***************************************************************************************
 *  mhal_aout.h — 回放音频出声(封装 hd_audiodec + hd_audioout)
 *
 *  管线: AAC/G711 裸流 → hd_audiodec →(bind)→ hd_audioout(喇叭/HDMI)
 *  上层只见本接口;单实例(硬件通常一路扬声器)。多宫格 enable 时各 feeder 均可 send,
 *  由应用侧控制同时开几路(多路同时送会叠到同一解码器)。
 ***************************************************************************************/
#ifndef MHAL_AOUT_H
#define MHAL_AOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MHAL_ACODEC_AAC  = 0,
    MHAL_ACODEC_G711U,
    MHAL_ACODEC_G711A,
    MHAL_ACODEC_PCM
} mhal_acodec_t;

/* 打开/确保音频通路就绪(惰性:首次 send 或显式 open)。
 * sample_rate: Hz, 0→默认 16000。out_dev: 0=DAC/喇叭 1=ADDA 2=HDMI, <0 读环境变量。 */
int  mhal_aout_open(mhal_acodec_t codec, int sample_rate, int out_dev);
void mhal_aout_close(void);

/* 送一帧压缩音/PCM。ts_us 可为 0。成功 0。未 open 时自动按 codec/sr 打开。 */
int  mhal_aout_send(mhal_acodec_t codec, int sample_rate,
                    const uint8_t *data, uint32_t len, uint64_t ts_us);

/* 音量 0..100;未 open 返回 -1。 */
int  mhal_aout_set_volume(int vol);
int  mhal_aout_get_volume(void);

/* 是否已打开。 */
int  mhal_aout_is_open(void);

#ifdef __cplusplus
}
#endif
#endif /* MHAL_AOUT_H */
