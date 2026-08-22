/***************************************************************************************
 *  nvr_rtsp_live.h — 本机 RTSP(TUTK 隧道 iotc-tunnel:8554)
 *
 *  直播 + 远程回放共用一口。App 经 P2PTunnel 映射后走标准 RTSP
 *  (OPTIONS/DESCRIBE/SETUP/PLAY/PAUSE/TEARDOWN/SET_PARAMETER)。
 *  每个 RTP 带 AVTECH 拓展头:playbackStatus + playbackTimestamp。
 ***************************************************************************************/
#ifndef NVR_RTSP_LIVE_H
#define NVR_RTSP_LIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rsdk_group;

int  nvr_rtsp_live_start(int port);
void nvr_rtsp_live_stop(void);
/* 实际监听端口;未启动返回 0。URL 必须用此值,禁止写死。 */
int  nvr_rtsp_live_port(void);

/* (chn,stream) 当前占用的 slot(== chn),未分配返回 -1。 */
int  nvr_rtsp_live_slot_of(int chn, int stream);
/* 该通道最近观测到的视频 codec:0=H264 1=H265 -1=未知。URL 扩展名 .264/.265 用。 */
int  nvr_rtsp_live_codec_of(int chn);
/* 释放某通道(主+子)占用的 live slot;stopLiveStream 带 channel 时用。 */
void nvr_rtsp_live_release(int chn);

/* 热插拔后盘组指针变化时重绑;NULL=无盘(回放只推空白帧)。 */
void nvr_rtsp_live_set_group(struct rsdk_group *group);

/* startLiveStream 预置通道/码流(0主/1子)及是否推视频/音频。chn=-1 取消。 */
void nvr_rtsp_live_select(int chn);
void nvr_rtsp_live_select_ex(int chn, int stream);
void nvr_rtsp_live_select_media(int chn, int stream, int want_video, int want_audio);

/* streaming 旁路:Annex-B 视频。stream:0主/1子。codec:0=H264 1=H265。 */
void nvr_rtsp_live_feed(int chn, int stream, const uint8_t *data, int len,
                        int codec, int is_key, uint32_t ts_ms);
/* 主流音频(AAC,可带 ADTS)→ 正在看该路 live 且 streamType 含 audio 的会话。 */
void nvr_rtsp_live_feed_audio(int chn, const uint8_t *data, int len, uint32_t ts_ms);

/* startPlayback:登记通道/码流/是否音频/起点。duration=重叠段剩余秒(无段=0)。
 * 无论有无录像都返回 0,App 随后 DESCRIBE /playback/<startTime>。 */
int  nvr_rtsp_pb_prepare(int chn, uint32_t start_utc, int stream, int want_audio, int want_video,
                         uint32_t *duration_out);
void nvr_rtsp_pb_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_RTSP_LIVE_H */
