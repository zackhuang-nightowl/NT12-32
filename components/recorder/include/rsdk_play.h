/* Copyright (C) 2025-2026, Nightowl DG. RSDK 检索/回放(设计 §6.2/§7). */
#ifndef RSDK_PLAY_H
#define RSDK_PLAY_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#include "rsdk_index.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_player rsdk_player_t;

/* 打开一个已命中的段(来自 rsdk_index_query) */
RSDK_API rsdk_err_t rsdk_play_open(rsdk_dev_t *d, const rsdk_index_slot_t *seg, rsdk_player_t **out);
/* 段内按 PTS 定位到最近不晚于 pts 的帧 */
RSDK_API rsdk_err_t rsdk_play_seek_pts(rsdk_player_t *p, uint64_t pts);
/* 段内按墙钟 epoch 定位到最近不晚于 wall 的关键帧(IDR)。优先读本段 RK_KEYIDX 表(大 chunk
 * 免顺扫);无表则顺扫。定位后 rsdk_play_next_frame 从该 IDR 起播。 */
RSDK_API rsdk_err_t rsdk_play_seek(rsdk_player_t *p, uint32_t wall);
/* 取下一帧: 读帧头→读负载→(按帧头 enc)解密→返回明文 Annex-B。
 * *data 指向内部缓冲(下次调用失效); 段尾返回 RSDK_E_NOTFOUND。 */
RSDK_API rsdk_err_t rsdk_play_next_frame(rsdk_player_t *p, rsdk_frame_hdr_t *hdr,
                                         const uint8_t **data, uint32_t *len);
RSDK_API void       rsdk_play_close(rsdk_player_t *p);

#ifdef __cplusplus
}
#endif
#endif
