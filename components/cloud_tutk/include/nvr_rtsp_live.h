/***************************************************************************************
 *  nvr_rtsp_live.h — 本地 RTSP 服务(供 P2PTunnel 映射 remote→554 推 live 子码流)
 ***************************************************************************************/
#ifndef NVR_RTSP_LIVE_H
#define NVR_RTSP_LIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  nvr_rtsp_live_start(int port);
void nvr_rtsp_live_stop(void);

/* streaming 旁路:子码流 Annex-B 帧 → RTP(有客户端 PLAY 时才发)。codec:0=H264 1=H265 */
void nvr_rtsp_live_feed(int chn, const uint8_t *data, int len, int codec, int is_key, uint32_t ts_ms);

#ifdef __cplusplus
}
#endif
#endif /* NVR_RTSP_LIVE_H */
