/* Copyright (C) 2025-2026, Nightowl DG. RSDK 多盘负载均衡(设计 §4). */
#ifndef RSDK_BALANCE_H
#define RSDK_BALANCE_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#include "rsdk_index.h"   /* rsdk_seg_visit_fn(覆盖遍历回调) */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_group rsdk_group_t;

/* 打开盘组(单盘退化: n=1)。同组盘须 group_uuid 一致。 */
RSDK_API rsdk_err_t rsdk_group_open(const char *const *devpaths, int n, rsdk_group_t **out);
RSDK_API int        rsdk_group_count(rsdk_group_t *g);
RSDK_API rsdk_dev_t*rsdk_group_dev(rsdk_group_t *g, int i);
/* 为通道选一块盘写入(评分: 写带宽/待分配/覆盖压力 + 通道亲和, 设计 §4.2) */
RSDK_API rsdk_err_t rsdk_balance_pick(rsdk_group_t *g, int chn, rsdk_dev_t **picked);
/* SMART 健康刷新(固件按分钟级定时调用, 保持 SG_IO 离热选路径) */
RSDK_API rsdk_err_t rsdk_group_smart_refresh(rsdk_group_t *g);
/* 报告段字节数, 更新 EWMA 写带宽; 由录像层在段结束时调用 */
RSDK_API void       rsdk_balance_report(rsdk_group_t *g, rsdk_dev_t *dev, uint64_t bytes);
/* 测试钩子: 强制设置某盘健康状态(模拟故障盘; SMART 在 image 文件上返回 unknown→ok) */
RSDK_API void       rsdk_group_set_health(rsdk_group_t *g, int disk, int ok);
RSDK_API void       rsdk_group_close(rsdk_group_t *g);

/* 运行时把一块**已格式化**的盘原地加入盘组(★group 指针不变——避免关组重开导致各模块借用的
 * group 指针悬空;数组在锁内 realloc 追加)。已在组内→RSDK_OK;未格式化/外来→rsdk_dev_open 失败原样返回。 */
RSDK_API rsdk_err_t rsdk_group_add_disk(rsdk_group_t *g, const char *path);

/* 带外改盘(format 直写盘区)后原地重载整组,不换 group 指针(借用者立即见新态,免重启)。停写后调用。 */
RSDK_API rsdk_err_t rsdk_group_reload(rsdk_group_t *g);
/* 按 devpath 查组内下标(供热插拔判断是否已入组);无则返回 -1。 */
RSDK_API int        rsdk_group_find_path(rsdk_group_t *g, const char *path);

/* 盘组元数据锁(= 组内各盘共享的递归锁): 供 rec 层把"跨多个原语的复合操作"(段翻转、开/关 writer)
 * 做成原子。递归 → 可与内部各原语的自锁嵌套。数据区读写不走此锁。 */
RSDK_API void       rsdk_group_lock(rsdk_group_t *g);
RSDK_API void       rsdk_group_unlock(rsdk_group_t *g);

/* ---- 多盘回放(设计 §4/§7.4) ---- */
/* 跨盘检索: 归并盘组内所有盘的索引, 按 start_time 升序。
 * 返回的 slot.start_disk 被重写为「盘组内数组下标」, 供 group_play 定位到具体盘。 */
RSDK_API int rsdk_group_query(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                              int rectype, rsdk_index_slot_t *out, int cap);
/* 同 rsdk_group_query; stream>=0 只取该码流段(0主/1子); stream<0 不限。 */
RSDK_API int rsdk_group_query_stream(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                                      int rectype, int stream, rsdk_index_slot_t *out, int cap);
/* 跨盘覆盖遍历: 归并盘组内所有盘, 逐段回调, ★无 cap 截断(见 rsdk_index_foreach_stream)。
 * 日历/日内时间轴等只需覆盖统计的接口用此, 避免 query_stream 的 cap 截断漏段。返回命中段数。 */
RSDK_API int rsdk_group_foreach_stream(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                                       int rectype, int stream, rsdk_seg_visit_fn cb, void *user);

/* 跨盘连续回放器: 顺序播放一组(可跨盘)段, 到段尾自动切到下一段所在盘。 */
typedef struct rsdk_group_player rsdk_group_player_t;
RSDK_API rsdk_err_t rsdk_group_play_open(rsdk_group_t *g, const rsdk_index_slot_t *segs, int nseg,
                                         rsdk_group_player_t **out);
/* 取下一帧(解密后 Annex-B); *disk_out 回填当前帧来自哪块盘(数组下标)。段组尾返回 RSDK_E_NOTFOUND。 */
RSDK_API rsdk_err_t rsdk_group_play_next(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                         const uint8_t **data, uint32_t *len, int *disk_out);
/* 同 rsdk_group_play_next, 额外输出跨段间隙标志:
 * *gap_out=1 表示本帧是切到新段后的第一帧且新段与上段存在录制间隙(>2s), 否则 0.
 * gap_out 为 NULL 时与 rsdk_group_play_next 行为完全一致。 */
RSDK_API rsdk_err_t rsdk_group_play_next2(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                          const uint8_t **data, uint32_t *len,
                                          int *disk_out, int *gap_out);
RSDK_API rsdk_err_t rsdk_group_play_seek_pts(rsdk_group_player_t *p, uint64_t pts);
/* 按墙钟 epoch 定位:选覆盖 wall 的段 + 段内读 RK_KEYIDX 跳到 ≤wall 最近 IDR(大段回放必用)。 */
RSDK_API rsdk_err_t rsdk_group_play_seek(rsdk_group_player_t *p, uint32_t wall);
RSDK_API void       rsdk_group_play_close(rsdk_group_player_t *p);

#ifdef __cplusplus
}
#endif
#endif
