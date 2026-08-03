/***************************************************************************************
 *  nvr_streaming.h — ③ 拉流出图 管理层（C API）
 *
 *  每路 IPC = 一个 CRtspClient 拉 RTSP → video_cb 裸帧 →
 *      ├─ platform/media_hal(hd_videodec) 硬解上屏（预览）
 *      └─ components/recorder(rsdk_rec_write_frame) 旁路录像
 *
 *  上游拉流: third_party/happytime_onvif_rtsp (CRtspClient)
 *  解码上屏: platform/media_hal (mhal_vdec/mhal_vout)  ——不走 ffmpeg 软解
 *  录像:     components/recorder (rsdk_group_t 由 storage 装配)
 *  详见 README.md。
 ***************************************************************************************/
#ifndef NVR_STREAMING_H
#define NVR_STREAMING_H

#include <stdint.h>
#include "rsdk.h"          /* rsdk_group_t / rsdk_err_t */

#ifdef __cplusplus
extern "C" {
#endif

enum { NVR_CODEC_AUTO = -1, NVR_CODEC_H264 = 0, NVR_CODEC_H265 = 1 };
enum { NVR_STREAM_MAIN = 0, NVR_STREAM_SUB = 1 };

/* 单通道配置 */
typedef struct {
    int   chn;                 /* NVR 通道号 0..15 */
    char  url[256];            /* rtsp://198.18.N.100/main（onvif 模块取得） */
    char  user[64];
    char  pass[64];
    int   codec;               /* NVR_CODEC_AUTO / H264 / H265 */
    int   stream;              /* NVR_STREAM_MAIN / SUB */
    int   record;              /* 1=同时录像 */
    int   vout_win;            /* 预览分屏窗口索引；-1=只录不显 */
    int   over_tcp;            /* 1=RTP over TCP(NVR 场景更稳，默认建议 1) */
    int   enc_w, enc_h, fps;   /* 该码流实际分辨率/帧率（0=未知，供解码预算准入+动态分配）
                                *   一般由 ONVIF GetVideoEncoderConfiguration 填；未知时按码流类型估 */
} nvr_stream_chan_cfg_t;

/* 管理器配置 */
typedef struct {
    rsdk_group_t *group;       /* storage 装配好的盘组（录像目标）；NULL=全通道不录像 */
    int           conn_timeout;/* 连接超时秒，默认 5 */
    int           rx_timeout;  /* 无数据超时秒（触发重连），默认 10 */
} nvr_stream_mgr_cfg_t;

typedef struct nvr_stream_mgr nvr_stream_mgr_t;

/* 生命周期 */
rsdk_err_t nvr_stream_mgr_init  (const nvr_stream_mgr_cfg_t *cfg, nvr_stream_mgr_t **out);
void       nvr_stream_mgr_deinit(nvr_stream_mgr_t *m);

/* 通道管理 */
rsdk_err_t nvr_stream_add_channel(nvr_stream_mgr_t *m, const nvr_stream_chan_cfg_t *c);
rsdk_err_t nvr_stream_start      (nvr_stream_mgr_t *m, int chn);
rsdk_err_t nvr_stream_stop       (nvr_stream_mgr_t *m, int chn);
rsdk_err_t nvr_stream_start_all  (nvr_stream_mgr_t *m);
void       nvr_stream_stop_all   (nvr_stream_mgr_t *m);

/* 运行期：切主/子码流（换 url 重连）、开/关某通道录像 */
rsdk_err_t nvr_stream_switch_stream(nvr_stream_mgr_t *m, int chn, int stream, const char *url);
rsdk_err_t nvr_stream_set_record   (nvr_stream_mgr_t *m, int chn, int on);

/* 状态查询（供 UI/诊断） */
typedef enum { NVR_CH_IDLE, NVR_CH_CONNECTING, NVR_CH_PLAYING, NVR_CH_NOSIGNAL, NVR_CH_FAIL } nvr_ch_state_t;
nvr_ch_state_t nvr_stream_state(nvr_stream_mgr_t *m, int chn);

/* 该通道是否因解码预算超限被拒绝预览解码（1=只录不显，需 UI 提示"超出解码能力"）。 */
int nvr_stream_decode_denied(nvr_stream_mgr_t *m, int chn);

#ifdef __cplusplus
}
#endif
#endif /* NVR_STREAMING_H */
