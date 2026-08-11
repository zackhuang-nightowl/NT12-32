/***************************************************************************************
 *  nvr_playback.h — 本机(单机)录像回放引擎。
 *
 *  GUI 通过 GUI_playbackControl 驱动:NVR 用 rsdk_group_query 找段 → rsdk_group_play_* 取
 *  解密后 Annex-B 帧 → mhal_vdec_send 解码到显示窗(与 live 同一硬解+上屏路径,源换成录像)。
 *  单通道全屏(窗口0):play 时接管该窗(先关 live 解码),stop 时归还 live。
 *  录像段里 主/子/音频 交织(见 rsdk_rec) → 按 hdr.stream 过滤;音频按宫格 enable[] 出声。
 *  不涉及 RTSP/P2P(远程回放见 docs/待办_TUTK_P2P_远程回放.md)。
 ***************************************************************************************/
#ifndef NVR_PLAYBACK_H
#define NVR_PLAYBACK_H

#include "rsdk.h"      /* rsdk_group_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_playback nvr_playback_t;
struct nvr_stream_mgr;
struct nvr_preview;

#define NVR_PB_MAX_CELLS 36

typedef struct {
    rsdk_group_t          *group;   /* 录像盘组(检索/回放) */
    struct nvr_stream_mgr *sm;      /* 释放/归还 live 显示 + 取解码尺寸 */
    struct nvr_preview    *pv;      /* 全屏布局切换 */
    int                    hdmi_w;  /* HDMI 分辨率(回放非0模式:视频区 0.8×W/0.8×H 从(0,0)均分) */
    int                    hdmi_h;
} nvr_playback_cfg_t;

int  nvr_playback_create(const nvr_playback_cfg_t *cfg, nvr_playback_t **out);
void nvr_playback_destroy(nvr_playback_t *pb);

/* 回放控制。action: "play"/"start" | "pause" | "resume" | "stop" | "seek" | "status"。
 * chn1 = 1-based 通道(<=0 则沿用当前);start_wall = 起播 UTC epoch(0=沿用/resume);
 * speed: frame/0.125X/0.25X/0.5X/1X/2X/4X/8X; direction forward/backward。
 * 返回 0;能力超限时 status 可为 notSupport,并通过 get_status 取 PlaybackMsgNotify。 */
int  nvr_playback_control(nvr_playback_t *pb, const char *action, int chn1,
                          uint32_t start_wall, const char *speed, const char *direction);

/* 取当前状态。notify 可为 NULL;非空时写 PlaybackMsgNotify(无提示则空串)。 */
void nvr_playback_get_status(nvr_playback_t *pb, char *status, char *speed,
                             char *direction, uint32_t *cur_wall,
                             char *notify, int notify_cap);

/* 进入回放模式(GUI_setPlaybackMode):接管窗口并**黑屏**,并按格数伸缩音频 enable[]。 */
void nvr_playback_set_mode(nvr_playback_t *pb, int display_mode, const int *channels1, int n);
int  nvr_playback_get_mode(nvr_playback_t *pb, int *channels_out, int cap, int *n);

/* 宫格音频使能(基于格位,非 camera)。set 时按接口原样接收多路 1。 */
void nvr_playback_set_audio(nvr_playback_t *pb, const int *enable, int n);
int  nvr_playback_get_audio(nvr_playback_t *pb, int *enable_out, int cap);

void nvr_playback_enter(nvr_playback_t *pb, int chn1);

#ifdef __cplusplus
}
#endif
#endif /* NVR_PLAYBACK_H */
