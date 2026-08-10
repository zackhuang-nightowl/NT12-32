/***************************************************************************************
 *  nvr_playback.h — 本机(单机)录像回放引擎。
 *
 *  GUI 通过 GUI_playbackControl 驱动:NVR 用 rsdk_group_query 找段 → rsdk_group_play_* 取
 *  解密后 Annex-B 帧 → mhal_vdec_send 解码到显示窗(与 live 同一硬解+上屏路径,源换成录像)。
 *  单通道全屏(窗口0):play 时接管该窗(先关 live 解码),stop 时归还 live。
 *  录像段里 主/子/音频 交织(见 rsdk_rec) → 按 hdr.stream 过滤、跳音频、I 帧起播。
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

typedef struct {
    rsdk_group_t          *group;   /* 录像盘组(检索/回放) */
    struct nvr_stream_mgr *sm;      /* 释放/归还 live 显示 + 取解码尺寸 */
    struct nvr_preview    *pv;      /* 全屏布局切换 */
    int                    hdmi_w;  /* HDMI 分辨率(视频区按 0.8×W/0.8×H 从(0,0)摆放) */
    int                    hdmi_h;
} nvr_playback_cfg_t;

int  nvr_playback_create(const nvr_playback_cfg_t *cfg, nvr_playback_t **out);
void nvr_playback_destroy(nvr_playback_t *pb);

/* 回放控制。action: "play"/"start" | "pause" | "resume"/"play"(paused→resume) | "stop" | "seek"。
 * chn1 = 1-based 通道(<=0 则沿用当前);start_wall = 起播 UTC epoch(0=沿用);speed "1X"/"2X"/"4X"/"8X";
 * direction "forward"/"backward"(v1 仅正放,backward 记录不倒放)。返回 0。 */
int  nvr_playback_control(nvr_playback_t *pb, const char *action, int chn1,
                          uint32_t start_wall, const char *speed, const char *direction);

/* 取当前状态(供 GUI 应答)。status/speed/direction 缓冲各 >=16 字节;cur_wall 当前回放位置 epoch。 */
void nvr_playback_get_status(nvr_playback_t *pb, char *status, char *speed,
                             char *direction, uint32_t *cur_wall);

/* 进入回放模式(GUI_setPlaybackMode):接管窗口并**黑屏**(不显示 live),等 playbackControl 起播。
 * chn1 = 1-based 主放通道(<=0 沿用);同时记住布局供 get。 */
void nvr_playback_set_mode(nvr_playback_t *pb, int display_mode, const int *channels1, int n);
/* 取回放布局(GUI_getPlaybackMode)。channels_out 写 1-based 通道,返回 displayMode;n 写通道数。 */
int  nvr_playback_get_mode(nvr_playback_t *pb, int *channels_out, int cap, int *n);

/* 仅进入黑屏(不改布局记录);内部/兼容用。 */
void nvr_playback_enter(nvr_playback_t *pb, int chn1);

#ifdef __cplusplus
}
#endif
#endif /* NVR_PLAYBACK_H */
