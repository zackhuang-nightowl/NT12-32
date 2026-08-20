/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像索引(设计 §6, 冻结 §3). */
#ifndef RSDK_INDEX_H
#define RSDK_INDEX_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 追加/更新一条段索引槽(按 seg_id 覆盖); 环形写入索引区 */
RSDK_API rsdk_err_t rsdk_index_write(rsdk_dev_t *d, const rsdk_index_slot_t *slot);
/* 按时间范围+通道+类型检索; 命中槽写入 out[](cap 个), 返回命中数。stream 不限。 */
RSDK_API int  rsdk_index_query(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                               int rectype, rsdk_index_slot_t *out, int cap);
/* 同 rsdk_index_query; stream>=0 时只命中 slot.stream==stream; stream<0 不限(旧盘/月历)。 */
RSDK_API int  rsdk_index_query_stream(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                                      int rectype, int stream, rsdk_index_slot_t *out, int cap);
/* 取最早可用日期(时间轴左界) */
RSDK_API rsdk_err_t rsdk_index_earliest(rsdk_dev_t *d, uint32_t *epoch);
/* 覆盖回收: 某 chunk 即将被覆盖前, 作废其上所有段索引槽(检索不再命中已覆盖数据)。返回作废数。
 * 有 chunk→slot 加速表时 O(1); 无表回退全索引扫描。 */
RSDK_API int rsdk_index_invalidate_chunk(rsdk_dev_t *d, uint64_t chunk);

/* 开盘时顺序扫一次索引, 建立 chunk→slot 加速表(供 O(1) 回收/upsert/真实容量)。
 * 由 rsdk_dev_open 调用。分配失败则不建表(各处回退全扫描, 仍正确)。 */
RSDK_API void rsdk_index_load_map(rsdk_dev_t *d);

#ifdef __cplusplus
}
#endif
#endif
