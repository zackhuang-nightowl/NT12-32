/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像写入(设计 §3/§7). */
#ifndef RSDK_REC_H
#define RSDK_REC_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#include "rsdk_balance.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_writer rsdk_writer_t;

/* 打开一路录像写入器(单盘, 一段连续录像 = 一个 RecSegment) */
RSDK_API rsdk_err_t rsdk_rec_open (rsdk_dev_t *d, int chn, int rectype, rsdk_writer_t **out);

/* 打开盘组录像写入器: 每换新段时按负载均衡选盘(设计 §4), 把一路录像分散到多盘。
 * 配合 rsdk_group_query/rsdk_group_play 做多盘写入+回放闭环。 */
RSDK_API rsdk_err_t rsdk_rec_open_group(rsdk_group_t *g, int chn, int rectype, rsdk_writer_t **out);
/* 指定码流开盘组写入器(0主/1子); 索引槽写入 slot.stream, 供按码流检索。 */
RSDK_API rsdk_err_t rsdk_rec_open_group_stream(rsdk_group_t *g, int chn, int rectype, int stream,
                                               rsdk_writer_t **out);
/* 写一帧前可改 stream(须在首帧前调用; 默认 0)。兼容旧 open 路径。 */
RSDK_API void       rsdk_rec_set_stream(rsdk_writer_t *w, int stream);
/* 写一帧(明文 Annex-B); 内部: 取/换chunk → 填帧头 → 按特性 AES-CTR 加密 → 顺序写 */
RSDK_API rsdk_err_t rsdk_rec_write_frame(rsdk_writer_t *w, const rsdk_frame_t *f);
/* 切换录像类型(常录↔事件), 会闭合当前段并开新段 */
RSDK_API rsdk_err_t rsdk_rec_change_type(rsdk_writer_t *w, int rectype);
/* 主动切段(同类型): 闭合当前段 + 开新段。供上层"定时切片"用——到达目标时长后, 在下一个 IDR 帧
 * 之前调用, 使新段从 IDR 起(回放段界无缝)。当前段无帧则为空操作。 */
RSDK_API rsdk_err_t rsdk_rec_rotate(rsdk_writer_t *w);
/* 当前段已写帧数(供上层判断"是否已有帧, 可安全切段") */
RSDK_API uint32_t   rsdk_rec_frame_count(rsdk_writer_t *w);
/* 把本盘缓冲的数据刷到介质(fdatasync 级): 供上层周期调用, 把掉电丢失窗口压到调用间隔内。
 * 不改 SB/SysTab(那是 rsdk_dev_flush 的职责), 只 fsync 裸设备。 */
RSDK_API rsdk_err_t rsdk_rec_datasync(rsdk_writer_t *w);
/* 闭合段并写入索引 */
RSDK_API rsdk_err_t rsdk_rec_close(rsdk_writer_t *w);
/* 当前段 id / chunk(供元数据/抓拍 seg_ref 绑定) */
RSDK_API uint32_t   rsdk_rec_seg_id(rsdk_writer_t *w);
RSDK_API uint64_t   rsdk_rec_cur_chunk(rsdk_writer_t *w);

/* 覆盖回收回调: overwrite 模式下某 chunk 被复用前触发(disk,chunk),
 * 供上层清理绑定到该视频 chunk 的元数据/抓拍(retention 联动)。 */
typedef void (*rsdk_reclaim_cb)(void *user, uint16_t disk, uint64_t chunk);
RSDK_API void rsdk_rec_set_reclaim(rsdk_writer_t *w, rsdk_reclaim_cb cb, void *user);
/* 事件标签:置后续帧头 event_id(连续轨命中事件时打标,便于扫描重建;传 0 清除)。 */
RSDK_API void rsdk_rec_set_event(rsdk_writer_t *w, uint64_t event_id);
/* 内联标记(自描述,供扫描重建):事件(复合 type_mask+精确时窗)/ 云存终态。 */
RSDK_API rsdk_err_t rsdk_rec_mark_event(rsdk_writer_t *w, uint64_t event_id, uint8_t rectype,
                                        uint32_t start, uint32_t end, uint32_t type_mask, uint32_t ref_seg);
RSDK_API rsdk_err_t rsdk_rec_mark_cloud(rsdk_writer_t *w, uint64_t event_id, uint8_t state, uint32_t ts);

#ifdef __cplusplus
}
#endif
#endif
