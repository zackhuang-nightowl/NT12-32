/* Copyright (C) 2025-2026, Nightowl DG. RSDK 扫描重建(自描述数据区 → 重建索引/事件骨架).
 * 用途:索引区/meta 丢失或损坏时,顺读数据区(帧头 rec_kind/rectype/event_id 自描述 + 内联标记),
 *      按 seg_id/event_id 聚合,重写 Index Slot;可选回填 meta 事件骨架(doc_type=AI_EVENT)。
 * 富 AI 细节不在录像里 → 不重建(由上层按实际回空)。 */
#ifndef RSDK_SCAN_H
#define RSDK_SCAN_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 扫描单盘数据区,重建索引槽;meta!=NULL(且 metadata=on)时回填事件骨架 AI_EVENT。
 * out_segs/out_events 可空,返回重建的段/事件数。成功返回 RSDK_OK。 */
RSDK_API rsdk_err_t rsdk_scan_rebuild(rsdk_dev_t *d, void *meta, int *out_segs, int *out_events);

#ifdef __cplusplus
}
#endif
#endif
