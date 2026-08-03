/***************************************************************************************
 *  nvr_preview.h — 预览分屏编排（app 集成层，计划 §B2）
 *
 *  职责：唯一调用 mhal_vout_* 的模块。持有 通道→窗口 映射、布局/分页、OSD、状态图标、
 *        码流选择（多分屏子码流 / 单画面主码流）。
 *  说明：mhal 当前支持 1/4/9/16；6/12 在本层用 9/16 网格只绑 N 窗实现（不改平台）。
 ***************************************************************************************/
#ifndef NVR_PREVIEW_H
#define NVR_PREVIEW_H

#include "nvr_streaming.h"        /* nvr_stream_mgr_t / switch_stream */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PV_L1 = 1, PV_L4 = 4, PV_L6 = 6, PV_L9 = 9, PV_L12 = 12, PV_L16 = 16 } pv_layout_t;

/* 状态图标位（与 nvr_event 一致） */
#define PV_ICON_MOTION 0x1
#define PV_ICON_HUMAN  0x2
#define PV_ICON_FACE   0x4
#define PV_ICON_REC    0x8

typedef struct nvr_preview nvr_preview_t;

typedef struct {
    nvr_stream_mgr_t *sm;         /* borrowed：缩放/全屏切主码流 */
    int  osd_name;                /* OSD 显示通道名 */
    int  osd_datetime;            /* OSD 显示时间 */
} nvr_preview_cfg_t;

int  nvr_preview_init  (const nvr_preview_cfg_t *cfg, nvr_preview_t **out);
void nvr_preview_deinit(nvr_preview_t *p);

int  nvr_preview_set_layout(nvr_preview_t *p, pv_layout_t layout);
int  nvr_preview_page      (nvr_preview_t *p, int page);        /* 翻页（下一组通道） */
int  nvr_preview_map       (nvr_preview_t *p, int win, int chn);
int  nvr_preview_unmap     (nvr_preview_t *p, int win);

/* 通道上/下线（由 channel_mgr 回调经 app 转发）：映射到可见窗口 / 画无信号 OSD。 */
int  nvr_preview_on_channel_online (nvr_preview_t *p, int chn);
int  nvr_preview_on_channel_offline(nvr_preview_t *p, int chn);

/* 交互：全屏（→L1+主码流）、单画面放大。 */
int  nvr_preview_fullscreen (nvr_preview_t *p, int chn);
int  nvr_preview_single_zoom(nvr_preview_t *p, int chn, int on);

/* 状态图标（由 event hub 设置）。 */
void nvr_preview_set_icons(nvr_preview_t *p, int chn, unsigned icon_bits);

/* 周期：刷新时间 OSD。 */
void nvr_preview_tick(nvr_preview_t *p);

#ifdef __cplusplus
}
#endif
#endif /* NVR_PREVIEW_H */
