/***************************************************************************************
 *  nvr_event.h — 事件中枢（app 集成层，计划 §B4）
 *
 *  职责：接收相机 AI 事件 → 归一化 → nop_event_publish（复用 nop 事件脊柱，自动扇出到
 *        8012-server/ONVIF 桥/longPolling/推送）→ 本地扇出（事件录像触发）。
 *        事件图标由 GUI 根据 longPolling 位图自绘，固件不叠 HDMI OSD。
 *  云存：事件录像触发时由 record_sched 登记云存状态；上传器按状态择机上传（无需本模块直连）。
 ***************************************************************************************/
#ifndef NVR_EVENT_H
#define NVR_EVENT_H

#include <stdint.h>
#include "nop_sdk/nop_event.h"          /* nop_event_hub_t / nop_detect_type_t */
#include "nvr_record_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/* longPolling 事件位图用（GUI 自绘 OSD） */
#define NVR_ICON_MOTION 0x1
#define NVR_ICON_HUMAN  0x2
#define NVR_ICON_FACE   0x4
#define NVR_ICON_REC    0x8
#define NVR_ICON_CAR    0x10

typedef struct nvr_evt_hub nvr_evt_hub_t;

/* 事件段落盘窗口(秒):mark_event 记录 [start, start+此] 的事件段末。 */
#define NVR_EVT_POST_RECORD_S 30

typedef struct {
    nop_event_hub_t *nop_hub;      /* 共享事件脊柱（app 创建并传入） */
    nvr_rec_sched_t *rs;           /* borrowed：事件录像触发 */
    struct nvr_stream_mgr *sm;     /* borrowed：事件录像落盘(nvr_stream_set_event 标记 writer) */
    struct nvr_settings *settings; /* borrowed：事件录像周排程门控；可 NULL=不门控 */
    void *user;
    void (*on_icon)(void *user, int chn, unsigned icon_bits); /* 可选；GUI 走 longPolling，一般不接 */
    /* 事件抓拍：在录像/图标之后由独立线程调用，失败不影响原事件路径。
     * inline_jpeg 仅在回调期间有效(已拷贝)；无图则 len=0，由编排层 ONVIF GetSnapshot。 */
    void *snap_user;
    void (*on_snap)(void *snap_user, int chn, uint64_t event_id, uint32_t ts,
                    const uint8_t *inline_jpeg, size_t inline_len);
    /* NOP EventExtInfo：编排层 POST 相机 / 写入 meta.db。可空=不取。 */
    void *meta_user;
    void (*on_meta_enable)(void *meta_user, int chn);
    void (*on_meta_pull)(void *meta_user, int chn, uint64_t event_id, uint32_t start_ts);
    /* 推送：事件线程只入队；worker 读事件图再上传。可空=不推。 */
    void *push_user;
    void (*on_push)(void *push_user, int chn, uint64_t event_id, uint32_t ts,
                    nop_detect_type_t type);
} nvr_evt_cfg_t;

int  nvr_evt_init  (const nvr_evt_cfg_t *cfg, nvr_evt_hub_t **out);
void nvr_evt_deinit(nvr_evt_hub_t *h);
/* 迟绑抓拍回调（通道管理器就绪后由 nvr_app 注入；可空=不抓拍）。 */
void nvr_evt_set_snap(nvr_evt_hub_t *h,
                      void (*on_snap)(void *user, int chn, uint64_t event_id, uint32_t ts,
                                      const uint8_t *inline_jpeg, size_t inline_len),
                      void *snap_user);
void nvr_evt_set_meta(nvr_evt_hub_t *h,
                      void (*on_enable)(void *user, int chn),
                      void (*on_pull)(void *user, int chn, uint64_t event_id, uint32_t start_ts),
                      void *meta_user);
void nvr_evt_set_push(nvr_evt_hub_t *h,
                      void (*on_push)(void *user, int chn, uint64_t event_id, uint32_t ts,
                                      nop_detect_type_t type),
                      void *push_user);
/* 上线后打开相机 EventExtInfo 缓存；后录结束取该事件 metaData。满队列丢弃。 */
void nvr_evt_queue_meta_enable(nvr_evt_hub_t *h, int chn);
void nvr_evt_queue_meta_pull(nvr_evt_hub_t *h, int chn, uint64_t event_id, uint32_t start_ts);

/* 归一化的入站事件（来自 8012 客户端回调 / ONVIF PullMessages 桥）：
 *   发布到 nop_hub（扇出 NOP 侧）+ 触发事件录像 + 更新 longPolling 位图。 */
int  nvr_evt_ingest(nvr_evt_hub_t *h, int chn, nop_detect_type_t type, uint64_t ts_ms);

/* GUI_longPolling 用:motion/human/face/car 四类的每通道位图(bit chn=通道 chn+1 近期有该类事件)。 */
void nvr_evt_masks(nvr_evt_hub_t *h, uint32_t *motion, uint32_t *human, uint32_t *face, uint32_t *car);

/* 周期：图标衰减（一段时间无事件后清 motion/human/face 图标）。 */
void nvr_evt_tick(nvr_evt_hub_t *h);

/* 注入 longPolling 唤醒(事件到达/图标衰减时)。 */
void nvr_evt_set_longpoll_poke(nvr_evt_hub_t *h, void (*poke)(void *user), void *user);

/* nop_detect_type_t → RSDK_REC_*（供测试/复用）。返回 -1 表示不触发录像。 */
int  nvr_evt_rectype_of(nop_detect_type_t type);
/* 8012 事件中心数字 msgType → detect 类型;未知/0 → NOP_DETECT_TYPE_MAX。 */
nop_detect_type_t nvr_evt_detect_from_msgtype(uint32_t msg8012);

#ifdef __cplusplus
}
#endif
#endif /* NVR_EVENT_H */
