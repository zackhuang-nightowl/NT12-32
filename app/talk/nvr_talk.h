/***************************************************************************************
 *  nvr_talk.h — 双向对讲（App MIC → 相机喇叭）。本机只听 127.0.0.1:7000，
 *  外网由 TUTK agent 映射 iotc-tunnel:7000。相机侧按设备分流：
 *    backend==0 (NOP)  → TCP 相机:7000 送裸音频
 *    backend!=0        → ONVIF RTSP backchannel
 *  startSpeaker 回 tcp://iotc-tunnel:7000/speaker（talk 2.4；口与 profile 7000）。
 ***************************************************************************************/
#ifndef NVR_TALK_H
#define NVR_TALK_H

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_TALK_PORT 7000

typedef struct nvr_talk nvr_talk_t;

int  nvr_talk_init(int listen_port, nvr_talk_t **out);
void nvr_talk_deinit(nvr_talk_t *t);

/* chn0 内部 0-based。codec: pcm/g711u/g711a。url_out 填 tcp://iotc-tunnel:port/speaker */
int  nvr_talk_start(nvr_talk_t *t, int chn0, int backend,
                    const char *ip, int onvif_port,
                    const char *user, const char *pass, const char *vsrc,
                    const char *codec, char *url_out, int url_cap);
int  nvr_talk_stop (nvr_talk_t *t, int chn0);

/* 401 Random / 402 清 digest 时回写通道。ud 通常是 nvr_app，chn0 内部 0-based。 */
typedef void (*nvr_talk_enh_fn)(void *ud, int chn0, const char *random, const char *penh);
void nvr_talk_set_enh_cb(nvr_talk_t *t, nvr_talk_enh_fn fn, void *ud);

#ifdef __cplusplus
}
#endif
#endif /* NVR_TALK_H */
