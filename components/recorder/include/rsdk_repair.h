/* Copyright (C) 2025-2026, Nightowl DG. RSDK 开机自检 + 分级修复(索引 / meta)。
 *
 * 目的:开机装配盘组后、对外提供查询前,主动发现"盘上索引 / meta 与实际录像不一致"并修复,
 *      使后续接口(日历 / 时间轴 / 回放)直接查得到数据,而不是查空或等待惰性重建。
 *
 * 判定(启发式,借开机那次全索引扫描,零额外持久状态):
 *   段=chunk ⇒ 健康索引里 有效槽数 应 ≈ 已写 chunk 数(未回绕=write_ptr_chunk;回绕=data_chunks)。
 *   - 有效+OPEN 槽数 << 应有段数 且 数据区确有帧            → 索引丢失/严重落后 → 全盘重建(Tier2,后台)
 *   - 损坏槽占比 > 25%                                      → 索引不可信         → 全盘重建(Tier2,后台)
 *   - 仅有未封口(OPEN)段 / 少量坏槽                        → 轻量封口           → Tier1(同步)
 *   - 其余(含"从没录过")                                   → 健康,不动
 *   meta 空但数据区有录像 → 也走后台数据扫描(顺带回填事件骨架;索引幂等 upsert)。
 *
 * 分级:Tier1 在装配内、录像启动前同步完成(快,保证常见掉电场景立即可查);
 *      Tier2 罕见(索引丢失/大损),后台线程扫描,期间查询返回当前索引已有内容,可查进度。
 * 所有索引改写走 rsdk_index_write 的 seg_id upsert(持盘组锁),与实时录像 / 覆盖回收串行,不冲突。
 */
#ifndef RSDK_REPAIR_H
#define RSDK_REPAIR_H
#include "rsdk_types.h"
#include "rsdk_balance.h"   /* rsdk_group_t */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RSDK_REPAIR_IDLE = 0,   /* 无需后台重建 / 未启动 */
    RSDK_REPAIR_RUNNING,    /* 后台重建进行中 */
    RSDK_REPAIR_DONE,       /* 后台重建完成 */
    RSDK_REPAIR_FAILED
} rsdk_repair_status_t;

/* 开机自检 + 分级修复。同步做 Tier1(封口未闭合段);需全盘重建 / meta 需回填时起后台线程后立即返回。
 * meta 可为 NULL(metadata=off)。g 须已 rsdk_group_open。幂等,可重复调用。返回 RSDK_OK=编排成功。 */
RSDK_API rsdk_err_t rsdk_group_check_and_repair(rsdk_group_t *g, void *meta);

/* 查后台重建状态 + 进度(percent 0..100,可空)。无后台任务返回 IDLE。 */
RSDK_API rsdk_repair_status_t rsdk_group_repair_progress(rsdk_group_t *g, int *percent);

/* 请求停止并回收后台重建线程(优雅关机 / 卸盘前调用,避免线程在 group 关闭后仍写盘)。 */
RSDK_API void rsdk_group_repair_stop(rsdk_group_t *g);

#ifdef __cplusplus
}
#endif
#endif
