/* Copyright (C) 2025-2026, Nightowl DG. RSDK 多盘负载均衡(设计 §4). */
#ifndef RSDK_BALANCE_H
#define RSDK_BALANCE_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_group rsdk_group_t;

/* 打开盘组(单盘退化: n=1)。同组盘须 group_uuid 一致。 */
RSDK_API rsdk_err_t rsdk_group_open(const char *const *devpaths, int n, rsdk_group_t **out);
RSDK_API int        rsdk_group_count(rsdk_group_t *g);
RSDK_API rsdk_dev_t*rsdk_group_dev(rsdk_group_t *g, int i);
/* 为通道选一块盘写入(评分: 写带宽/待分配/覆盖压力 + 通道亲和) */
RSDK_API rsdk_err_t rsdk_balance_pick(rsdk_group_t *g, int chn, rsdk_dev_t **picked);
RSDK_API void       rsdk_group_close(rsdk_group_t *g);

/* ---- 多盘回放(设计 §4/§7.4) ---- */
/* 跨盘检索: 归并盘组内所有盘的索引, 按 start_time 升序。
 * 返回的 slot.start_disk 被重写为「盘组内数组下标」, 供 group_play 定位到具体盘。 */
RSDK_API int rsdk_group_query(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                              int rectype, rsdk_index_slot_t *out, int cap);

/* 跨盘连续回放器: 顺序播放一组(可跨盘)段, 到段尾自动切到下一段所在盘。 */
typedef struct rsdk_group_player rsdk_group_player_t;
RSDK_API rsdk_err_t rsdk_group_play_open(rsdk_group_t *g, const rsdk_index_slot_t *segs, int nseg,
                                         rsdk_group_player_t **out);
/* 取下一帧(解密后 Annex-B); *disk_out 回填当前帧来自哪块盘(数组下标)。段组尾返回 RSDK_E_NOTFOUND。 */
RSDK_API rsdk_err_t rsdk_group_play_next(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                         const uint8_t **data, uint32_t *len, int *disk_out);
RSDK_API rsdk_err_t rsdk_group_play_seek_pts(rsdk_group_player_t *p, uint64_t pts);
RSDK_API void       rsdk_group_play_close(rsdk_group_player_t *p);

#ifdef __cplusplus
}
#endif
#endif
