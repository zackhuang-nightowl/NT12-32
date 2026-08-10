/***************************************************************************************
 *  nvr_event.h — 事件中枢（app 集成层，计划 §B4）
 *
 *  职责：接收相机 AI 事件 → 归一化 → nop_event_publish（复用 nop 事件脊柱，自动扇出到
 *        8012-server/ONVIF 桥/longPolling/推送）→ 本地扇出（事件录像触发 + 预览状态图标）。
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

/* 预览状态图标位（与 preview 一致） */
#define NVR_ICON_MOTION 0x1
#define NVR_ICON_HUMAN  0x2
#define NVR_ICON_FACE   0x4
#define NVR_ICON_REC    0x8
#define NVR_ICON_CAR    0x10

typedef struct nvr_evt_hub nvr_evt_hub_t;

typedef struct {
    nop_event_hub_t *nop_hub;      /* 共享事件脊柱（app 创建并传入） */
    nvr_rec_sched_t *rs;           /* borrowed：事件录像触发 */
    void *user;
    void (*on_icon)(void *user, int chn, unsigned icon_bits); /* 预览图标（可 NULL） */
} nvr_evt_cfg_t;

int  nvr_evt_init  (const nvr_evt_cfg_t *cfg, nvr_evt_hub_t **out);
void nvr_evt_deinit(nvr_evt_hub_t *h);

/* 归一化的入站事件（来自 8012 客户端回调 / ONVIF PullMessages 桥）：
 *   发布到 nop_hub（扇出 NOP 侧）+ 触发事件录像 + 置预览图标。 */
int  nvr_evt_ingest(nvr_evt_hub_t *h, int chn, nop_detect_type_t type, uint64_t ts_ms);

/* GUI_longPolling 用:motion/human/face/car 四类的每通道位图(bit chn=通道 chn+1 近期有该类事件)。 */
void nvr_evt_masks(nvr_evt_hub_t *h, uint32_t *motion, uint32_t *human, uint32_t *face, uint32_t *car);

/* 周期：图标衰减（一段时间无事件后清 motion/human/face 图标）。 */
void nvr_evt_tick(nvr_evt_hub_t *h);

/* nop_detect_type_t → RSDK_REC_*（供测试/复用）。返回 -1 表示不触发录像。 */
int  nvr_evt_rectype_of(nop_detect_type_t type);
/* 8012 事件中心数字 msgType → detect 类型;未知/0 → NOP_DETECT_TYPE_MAX。 */
nop_detect_type_t nvr_evt_detect_from_msgtype(uint32_t msg8012);

#ifdef __cplusplus
}
#endif
#endif /* NVR_EVENT_H */
