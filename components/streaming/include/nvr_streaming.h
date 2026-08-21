/***************************************************************************************
 *  nvr_streaming.h — ③ 拉流出图 管理层（C API）
 *
 *  每路 IPC = 一个 CRtspClient 拉 RTSP → video_cb 裸帧 →
 *      ├─ platform/media_hal(hd_videodec) 硬解上屏（预览）
 *      └─ components/recorder(rsdk_rec_write_frame) 旁路录像
 *
 *  上游拉流: SDK/OnvifClientLibrary (nop::NopRtspClient facade over CRtspClient)
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
    int   over_tcp;            /* 保留字段;拉流层强制 RTP over TCP=1(录像不丢包) */
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
/* 运行期更新主/子录像掩码(与 set_record 独立;排程/设置变更时下发)。 */
rsdk_err_t nvr_stream_set_record_mask(nvr_stream_mgr_t *m, int chn, int main_on, int sub_on);
/* 仅事件待命:arm=1 时主流进预录环,事件触发后 flush+写盘至 post 窗结束;与连续录像互斥写盘。
 * pre_s=预录秒数(0=无预录,仍可事件开录)。 */
rsdk_err_t nvr_stream_set_event_arm(nvr_stream_mgr_t *m, int chn, int arm, int pre_s);
/* 事件标记:置某通道当前录像段的事件(event_id/rectype/时窗)。event_id=0 清除。线程安全(仅置标志,
 * 由 puller 线程应用到 writer)。供事件(8012/ONVIF)→事件录像落盘。
 * 若 event_arm:同时启动事件片段写盘(含预录 flush)。 */
rsdk_err_t nvr_stream_set_event    (nvr_stream_mgr_t *m, int chn, uint64_t event_id,
                                    int rectype, uint32_t start, uint32_t end);
/* 运行时更新录像盘组 + 对录像通道补开 writer(格式化后重组装盘组、免重启启用录像)。 */
rsdk_err_t nvr_stream_mgr_set_group(nvr_stream_mgr_t *m, rsdk_group_t *group);

/* 格式化编排:pause 暂停所有写盘并关闭 writer(盘静默,供安全格式化),返回原录像通道位图;
 * resume 恢复原录像通道并换新盘组补开 writer。命令线程调用,内部沿用无锁 record=0 契约。 */
uint32_t nvr_stream_mgr_pause_recording (nvr_stream_mgr_t *m);
void     nvr_stream_mgr_resume_recording(nvr_stream_mgr_t *m, rsdk_group_t *group, uint32_t was);
/* 录像中通道位图(bit chn=该通道正在写盘)。供 GUI_longPolling 的 RecordStatus。 */
uint32_t   nvr_stream_recording_mask(nvr_stream_mgr_t *m);
/* 录像位图变化时唤醒 GUI_longPolling(由 app 注入 nvr_chan_poke_longpoll)。 */
void       nvr_stream_set_lp_poke(nvr_stream_mgr_t *m, void (*poke)(void *user), void *user);
/* 回放:取某通道某码流(NVR_STREAM_MAIN/SUB)的解码尺寸。无该通道回退 1080p。返回 0/. */
int        nvr_stream_dim(nvr_stream_mgr_t *m, int chn, int stream, int *w, int *h, int *fps);

/* 显示门控(preview 驱动)：设通道当前显示目标格。win>=0=在该格显示(开解码即上屏),win<0=隐藏(关解码)。
 * 拉流与录像不受影响——切布局/翻页只改解码目标,RTSP 会话不动;不可见通道不占硬件解码预算。 */
rsdk_err_t nvr_stream_set_display  (nvr_stream_mgr_t *m, int chn, int win);

/* ★ 双流:channel 层解析出某码流(NVR_STREAM_MAIN/SUB)的取流 URL → 提供给 streaming,该路 puller
 * 立即起(主+子两路常拉:主录像+单宫格显示、子录像+多宫格显示)。 */
rsdk_err_t nvr_stream_set_url      (nvr_stream_mgr_t *m, int chn, int stream, const char *url);

/* ★ 切换喂解码器的码流(单宫格=主/多宫格=子)。两路都在拉 → 瞬时切换、不重连。 */
rsdk_err_t nvr_stream_set_decode_stream(nvr_stream_mgr_t *m, int chn, int stream);

/* 状态查询（供 UI/诊断） */
typedef enum { NVR_CH_IDLE, NVR_CH_CONNECTING, NVR_CH_PLAYING, NVR_CH_NOSIGNAL, NVR_CH_FAIL } nvr_ch_state_t;
nvr_ch_state_t nvr_stream_state(nvr_stream_mgr_t *m, int chn);

/* 该通道是否因解码预算超限被拒绝预览解码（1=只录不显，需 UI 提示"超出解码能力"）。 */
int nvr_stream_decode_denied(nvr_stream_mgr_t *m, int chn);

/* 该通道当前是否已"出图"(解码器已开且喂到帧)。供切宫格/切码流接口阻塞回复直到有格出图。 */
int nvr_stream_display_ready(nvr_stream_mgr_t *m, int chn);

/* 给该通道喂缓存关键帧(批量提交结束、解码器 start 后调)→ 不等下个 IDR、秒出图。 */
rsdk_err_t nvr_stream_feed_keyframe(nvr_stream_mgr_t *m, int chn);

#ifdef __cplusplus
}
#endif
#endif /* NVR_STREAMING_H */
