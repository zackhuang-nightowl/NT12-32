/***************************************************************************************
 *  Copyright (C) 2025-2026, Nightowl DG, all rights reserved.
 *
 *  rsdk_evtidx.h — 事件索引区(盘上事件权威)。
 *
 *  事件的「音视频定位 / 截图指针 / 云存态」全部落在事件索引区(128B 事件槽,
 *  紧邻 64B 段索引区的相邻数组)。查询只读事件区(不依赖 meta.db);索引丢失由
 *  rsdk_scan_rebuild 顺读数据区自描述记录 + 顺扫 MetaRegion PIC0 头重建。
 *
 *  与段索引一致的环形语义:upsert by event_id(回扫命中同 id 覆盖,否则环形 append);
 *  chunk 被覆盖回收时按 av_chunk 作废对应事件槽。
 *
 *  依赖:metadata=on(格式化时才留事件区;evtidx_sectors=0 时所有写入返回 E_NOSPACE)。
 ***************************************************************************************/
#ifndef RSDK_EVTIDX_H
#define RSDK_EVTIDX_H

#include "rsdk_types.h"
#include "rsdk_storgedev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由 (chn, starttime, rectype, salt) 确定性铸造 64 位 event_id(事件唯一 id)。
 * 布局:[chn:8][rectype:8][salt:16][starttime:32]。salt 用于同秒去碰撞。 */
RSDK_API uint64_t rsdk_evtidx_make_event_id(int chn, uint32_t starttime, int rectype, uint16_t salt);

/* upsert 一条事件槽(按 event_id)。s->crc32 由本函数计算,调用方不必填。 */
RSDK_API rsdk_err_t rsdk_evtidx_write(rsdk_dev_t *d, const rsdk_evt_slot_t *s);

/* 读单条事件槽(按 event_id)。找不到返回 RSDK_E_NOTFOUND。 */
RSDK_API rsdk_err_t rsdk_evtidx_get(rsdk_dev_t *d, uint64_t event_id, rsdk_evt_slot_t *out);

/* 按时间区间[t0,t1] + 通道 + 云存态过滤,时间升序返回。
 * chn<0=任意通道;state<0=任意态;t1=0 视为到最新。返回条数(≤cap)。 */
RSDK_API int rsdk_evtidx_query(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                               int state, rsdk_evt_slot_t *out, int cap);

/* 回填截图指针到事件槽(抓拍落 MetaRegion 后调用),并置 RSDK_EVT_HAS_SNAP。 */
RSDK_API rsdk_err_t rsdk_evtidx_patch_snap(rsdk_dev_t *d, uint64_t event_id,
                                           uint16_t disk, uint64_t off,
                                           uint32_t len, uint32_t ts);

/* 推进云存态。转 UPLOADING 时 attempts++;记录 last_err/updated。 */
RSDK_API rsdk_err_t rsdk_evtidx_patch_state(rsdk_dev_t *d, uint64_t event_id,
                                            int state, int32_t err, uint32_t now);

/* 覆盖回收:作废起始 chunk 落在指定 chunk 的事件槽。返回作废条数。 */
RSDK_API int rsdk_evtidx_invalidate_chunk(rsdk_dev_t *d, uint64_t chunk);

#ifdef __cplusplus
}
#endif
#endif /* RSDK_EVTIDX_H */
