/***************************************************************************************
 *  nvr_rtsp_srv.h — C shim over nop::NopRtspServer (OnvifClientLibrary).
 *
 *  隧道直播的 RTSP 服务端现由 SDK 标准的 nop::NopRtspServer 承担:它用挂钟媒体时钟
 *  (get_rtp_timestamp(90000)) 给每帧打时戳并作为节拍器 → 修掉手搓服务端"没有媒体
 *  时钟、发得比放得快导致冻结"的老 bug。本 shim 把 C++ 单例引擎包成 C 接口,供
 *  nvr_rtsp_live.c(C 编译单元)调用。整个进程只有一个引擎实例(SDK 语义)。
 ***************************************************************************************/
#ifndef NVR_RTSP_SRV_H
#define NVR_RTSP_SRV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 RTSP 服务端引擎(进程内单例)。先调用 rtspLibInit()(幂等)。
 * 返回实际监听端口(成功即入参 port;失败返回 0)。 */
int  nvr_srv_start(int port);

/* 停止引擎并释放资源。 */
void nvr_srv_stop(void);

/* 引擎是否已启动。 */
int  nvr_srv_is_running(void);

/* 注册/初始化一个直播 slot,使 push_video/push_audio(slot,...) 有目标。
 *   slot     : [0, NOP_RTSP_MAX_STREAMS) 的槽位号 → URL "live{slot+1}"
 *   vcodec   : 0=H264 1=H265
 *   w,h,fps  : 视频维度(挂钟计时,fps 非关键;可传 0 交由默认)
 *   bitrate  : kb/s(信息性)
 *   want_audio: 非 0 时附带音频轨
 *   acodec   : NopAudioCodec(如 4=AAC)
 *   asr,ach  : 采样率 / 声道数
 * 返回 0 成功,-1 失败。 */
int  nvr_srv_add_stream(int slot, int vcodec, int w, int h, double fps, int bitrate,
                        int want_audio, int acodec, int asr, int ach);

/* 注入一个已编码视频访问单元(Annex-B,含起始码)。不带时戳——服务端打挂钟时戳并节拍。 */
int  nvr_srv_push_video(int slot, const uint8_t *data, int len);

/* 同上,但带该帧真实 RTP 时戳(90kHz)。回放用:让出流时间轴反映录像时间而非挂钟,
 * 修"时间轴不按录像顺序叠加"。ts 由回放读线程按帧 pts 增量累加(单调、1×)。 */
int  nvr_srv_push_video_ts(int slot, const uint8_t *data, int len, uint32_t ts);

/* 注入一个已编码音频帧。nbsamples 为该帧采样数(AAC≈1024)。 */
int  nvr_srv_push_audio(int slot, const uint8_t *data, int len, int nbsamples);

/* 释放该 slot:归还 add_stream 占用的 CLiveVideo/CLiveAudio 引用(引用计数归零 →
 * m_bInited 复位),以便该 slot 复用时按新分辨率重新初始化。 */
void nvr_srv_free_stream(int slot);

/* 注册回放 URL 解析器(库侧实现,onvifclient 提供)。App 拉 rtsp://.../playback/<startTime>
 * (仅时间戳,不含通道);库侧 rtsp_parse_live_url 命中 playback/ 前缀时调用此 resolver 把
 * startTime 反查回 slot 号(== 通道号)。resolver 返回 slot,或 <0 表示未知/未就绪。 */
void rtsp_set_pb_slot_resolver(int (*fn)(unsigned int start_ts));

/* 注册回放控制回调(库侧处理 SET_PARAMETER 时调用):op 0=seek(定位到 utc)、1=play(恢复)。
 * chn=会话 v_index(==slot)。库侧据此让回放读线程重定位/暂停/恢复。 */
void rtsp_set_pb_ctrl_hook(int (*fn)(int chn, int op, unsigned int utc));
/* 诊断:把 SET_PARAMETER body 交给回调(→ 分类 DEBUG 日志,NVR_LOG_CATS 未开时静默)。 */
void rtsp_set_pb_diag_hook(void (*fn)(int is_live, int ctx_len, const char *body));

#ifdef __cplusplus
}
#endif
#endif /* NVR_RTSP_SRV_H */
