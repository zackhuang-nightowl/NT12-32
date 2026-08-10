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
#include "nvr_channel.h"          /* nvr_chan_mgr_t / nvr_chan_set_stream（主/子码流按显示模式切） */

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
    nvr_chan_mgr_t   *cm;         /* borrowed：按显示模式切通道主/子码流(经 ONVIF 重解析) */
    int  osd_name;                /* OSD 显示通道名 */
    int  osd_datetime;            /* OSD 显示时间 */
    int  hdmi_w, hdmi_h;          /* HDMI 输出分辨率（悬浮块千分比换算基准） */
} nvr_preview_cfg_t;

/* 悬浮块（画中画/自由矩形）：chn0 为 0-based 通道号，x/y/w/h 为千分比(0..1000)，
 * stream 为 NVR_STREAM_MAIN/NVR_STREAM_SUB。 */
typedef struct {
    int chn0;
    int x, y, w, h;
    int stream;
} nvr_pv_ext_t;

int  nvr_preview_init  (const nvr_preview_cfg_t *cfg, nvr_preview_t **out);
/* 迟绑通道管理器(cm 在 preview_init 之后才创建)：用于按显示模式切通道主/子码流。 */
void nvr_preview_set_cm(nvr_preview_t *p, nvr_chan_mgr_t *cm);
void nvr_preview_deinit(nvr_preview_t *p);

/* 布局模式（mode∈{0,1,4,9,16}；0=停止解码/隐藏所有窗）+ 分页（page 1-based）。 */
int  nvr_preview_set_mode(nvr_preview_t *p, int display_mode, int display_page);
/* 通道映射表（1-based 输入，内部转 0-based）；不持久化，仅本次调用生效并按当前 mode 重排。 */
int  nvr_preview_set_mapping(nvr_preview_t *p, const int *map1based, int n);
/* 悬浮块列表（覆盖式设置；n=0 清空所有悬浮块）。 */
int  nvr_preview_set_ext(nvr_preview_t *p, const nvr_pv_ext_t *b, int n);

/* 阻塞等待:切宫格/切码流后,轮询直到**任一可见格出图**或超时(ms)。供接口层"等图出了再回复"
 * (LVGL 等待期间放加载动画)。返回 0=已出图,1=超时。 */
int  nvr_preview_wait_ready (nvr_preview_t *p, int timeout_ms);

int  nvr_preview_get_mode   (nvr_preview_t *p, int *mode, int *page);
int  nvr_preview_get_mapping(nvr_preview_t *p, int *out1based, int cap);
int  nvr_preview_get_ext    (nvr_preview_t *p, nvr_pv_ext_t *out, int cap);

/* 千分比→像素：clamp((v*span+500)/1000, 0, span)。纯函数，便于单测。 */
int  pv_thousandths_to_px(int v, int span);

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

/* 数字变焦(ZoomPan,X_NightOwl_setChannelZoomPan)。chn0 为 0-based 通道号。
 *  入:enable、CenterPointXY/FocusPointXY 千分比(0..1000)、ratio(100=1.00x,MAX 500)。
 *  出:实际生效 CenterPointXY + ratio(被夹取后回填);result 文案见下。
 *  裁剪 ROI = 源画面按 ratio 等比缩小、以 CenterPointXY 为中心并夹在帧内 → VPE 放大填满窗口。 */
int  nvr_preview_set_zoom(nvr_preview_t *p, int chn0, int enable,
                          int cx, int cy, int focusx, int focusy, int ratio,
                          int *out_cx, int *out_cy, int *out_ratio, const char **out_result);
/* 读回某通道当前变焦状态(掉电/重启回默认 500,500,100)。 */
int  nvr_preview_get_zoom(nvr_preview_t *p, int chn0,
                          int *enable, int *cx, int *cy, int *ratio);

/* 状态图标（由 event hub 设置）。 */
void nvr_preview_set_icons(nvr_preview_t *p, int chn, unsigned icon_bits);

/* 周期：刷新时间 OSD。 */
void nvr_preview_tick(nvr_preview_t *p);

#ifdef __cplusplus
}
#endif
#endif /* NVR_PREVIEW_H */
