/***************************************************************************************
 *  Copyright (C) 2025-2026, Nightowl DG, all rights reserved.
 *
 *  rsdk_cloud.h — 录像「云存上传状态」内置 API（计划 §B5）
 *
 *  背景：云存(事件切片上传)的**上传状态**内置于 librsdk，作为权威来源；上传引擎
 *        (components/cloud_uploader) 只负责网络/TS/HTTP，通过本 API 推进状态并枚举待传。
 *
 *  存储：以 rsdk_meta 的 doc_type=RSDK_DOC_CLOUD JSON 文档为权威（meta.db 片外 SQLite，
 *        不触碰冻结的裸盘格式）。每个云存事件一行，按 event_id 更新。
 *        依赖 metadata=on（与 rsdk_pic 相同门控）。
 *
 *  状态：NONE/PENDING/UPLOADING/DONE/FAILED/LOST 对应
 *        不上传 / 未上传 / 上传中 / 上传完毕 / 上传失败 / 源被覆盖丢失。
 ***************************************************************************************/
#ifndef RSDK_CLOUD_H
#define RSDK_CLOUD_H

#include "rsdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* rsdk_cloud_state_t 现为盘上权威值,定义在 rsdk_types.h(事件槽 state 字段共用)。
 * 四态:NONE(0)/PENDING(1 未上传)/UPLOADING(2)/DONE(3 已上传)/RETRY(4 待重试)。 */

/* 一个云存事件 = 某通道一个 AI/门铃事件，绑定其 RecSegment。
 * event_id 由调用方铸造（见 rsdk_cloud_make_event_id）。
 * starttime = 事件起始 epoch 秒（= 上传 URL/tags 的 10 位 starttime，未埋通道前）。 */
typedef struct {
    uint64_t           event_id;
    int                chn;
    uint32_t           rectype;      /* RSDK_REC_HUMAN/FACE/VEHICLE/DOORBELL/... */
    uint32_t           starttime;
    uint32_t           end_time;     /* 0=未闭合 */
    rsdk_cloud_state_t state;
    uint32_t           seg_id;       /* 首段 seg_id */
    uint16_t           disk;         /* 首段所在盘 */
    uint64_t           start_chunk;  /* 首段起始 chunk（reclaim 映射用） */
    uint32_t           attempts;
    uint32_t           updated_ts;
    int32_t            last_err;
} rsdk_cloud_event_t;

/* 由 (chn, starttime, rectype, salt) 确定性铸造 64 位 event_id。
 * 布局：[chn:8][rectype:8][salt:16][starttime:32]。salt 用于同秒去碰撞。 */
RSDK_API uint64_t rsdk_cloud_make_event_id(int chn, uint32_t starttime, int rectype, uint16_t salt);

/* 注册/更新一个云存事件（按 event_id 幂等 upsert）。事件录像开始时调用。 */
RSDK_API rsdk_err_t rsdk_cloud_event_begin(void *meta, const rsdk_cloud_event_t *ev);

/* 把一个刚闭合的 RecSegment 绑定到已有云存事件（多段事件可多次调用）。 */
RSDK_API rsdk_err_t rsdk_cloud_event_add_seg(void *meta, uint64_t event_id,
                                             const rsdk_index_slot_t *seg);

/* 推进状态。转 UPLOADING 时 attempts++。err 为最近的 HTTP/curl/-1002.. 码（无则 0）。 */
RSDK_API rsdk_err_t rsdk_cloud_set_state(void *meta, uint64_t event_id,
                                         rsdk_cloud_state_t st, int32_t err);

/* 读单个事件当前状态。找不到返回 RSDK_E_*（非 RSDK_OK）。 */
RSDK_API rsdk_err_t rsdk_cloud_get(void *meta, uint64_t event_id, rsdk_cloud_event_t *out);

/* 枚举待上传：state ∈ {PENDING[, FAILED][, 过期 UPLOADING]}，按 starttime 升序，最多 cap 条。
 * 返回条数。上传器轮询此接口。 */
typedef struct {
    int      include_failed;     /* 1=也返回 FAILED（重试） */
    uint32_t stale_uploading_s;  /* >0=也返回早于此秒数的 UPLOADING（崩溃恢复） */
    int      chn;                /* -1=任意通道 */
} rsdk_cloud_poll_opt_t;
RSDK_API int rsdk_cloud_enumerate_pending(void *meta, const rsdk_cloud_poll_opt_t *opt,
                                          rsdk_cloud_event_t *out, int cap);

/* 覆盖回收钩子：当 (disk,chunk) 即将被覆盖时调用；把起始或任一段落在该 chunk、
 * 且未 DONE 的云存事件置为 LOST。返回被标记条数。接入 rsdk_rec_set_reclaim 回调。 */
RSDK_API int rsdk_cloud_on_reclaim(void *meta, uint16_t disk, uint64_t chunk);

#ifdef __cplusplus
}
#endif
#endif /* RSDK_CLOUD_H */
