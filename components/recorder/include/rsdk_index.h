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

/* 开机自检: 批量顺序扫索引区, 统计 有效/未封口(OPEN)/损坏(带flag但CRC不过) 槽数(供修复分类)。
 * 无锁 + 逐槽 CRC, 与 query 同安全模型。out 参数可空。 */
RSDK_API void rsdk_index_scan_stats(rsdk_dev_t *d, uint32_t *valid, uint32_t *open, uint32_t *corrupt);

/* 枚举所有 OPEN(未封口)有效槽的完整内容(供 Tier1 封口修复)。写入 out[](cap 个), 返回个数。
 * OPEN 槽数 = 崩溃/掉电时正在写的段, 通常 ≤ 通道数×码流数, cap 给 64 足够。 */
RSDK_API int rsdk_index_list_open(rsdk_dev_t *d, rsdk_index_slot_t *out, int cap);

/* 覆盖遍历回调: 逐个命中段回调 (chn0, start_time, end_time)。end_time 可能=0xFFFFFFFF(段未闭合),
 * 由调用方按"录到现在"归一。返回非 0 可提前终止遍历。 */
typedef int (*rsdk_seg_visit_fn)(void *user, int chn0, uint32_t start_time, uint32_t end_time);
/* 遍历 [t0,t1]∩chn∩rectype∩stream 的**所有**命中段并逐个回调。
 * ★与 rsdk_index_query_stream 的区别: 无 cap 截断——后者按物理槽序凑满 cap 就停, 超出的段被静默
 *   丢弃, 导致"日历/进度条只显示前一小段、比盘上实际录像少"。日历/时间轴等只需覆盖统计(不需要拿到
 *   每个段本身)的接口应改用本函数。chn<0 不限通道; rectype<0 不限; stream<0 不限。返回命中段数。 */
RSDK_API int rsdk_index_foreach_stream(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                                       int rectype, int stream, rsdk_seg_visit_fn cb, void *user);
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
