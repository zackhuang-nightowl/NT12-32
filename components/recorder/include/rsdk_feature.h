/* Copyright (C) 2025-2026, Nightowl DG. RSDK 特性控制模块(设计 §13). */
#ifndef RSDK_FEATURE_H
#define RSDK_FEATURE_H
#include "rsdk_types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 由 tools/gen_config.py 从 rsdk_features.conf 生成; 缺省保守值见下 */
#if defined(__has_include)
#  if __has_include("rsdk_config.h")
#    include "rsdk_config.h"
#  endif
#endif
#ifndef RSDK_CFG_ENCRYPTION
#  define RSDK_CFG_ENCRYPTION 0
#endif
#ifndef RSDK_CFG_METADATA
#  define RSDK_CFG_METADATA 0
#endif
#ifndef RSDK_CFG_MULTIDISK_BALANCE
#  define RSDK_CFG_MULTIDISK_BALANCE 0
#endif
#ifndef RSDK_CFG_BACKUP_FMP4
#  define RSDK_CFG_BACKUP_FMP4 0
#endif
#ifndef RSDK_CFG_CHUNK_MIB
#  define RSDK_CFG_CHUNK_MIB 8
#endif
#ifndef RSDK_CFG_SLOTS_PER_CHUNK
#  define RSDK_CFG_SLOTS_PER_CHUNK 4
#endif
#ifndef RSDK_CFG_META_RATIO_PCT10
#  define RSDK_CFG_META_RATIO_PCT10 5   /* 0.5% x10 */
#endif
#ifndef RSDK_CFG_HDD_FULL
#  define RSDK_CFG_HDD_FULL 0           /* 0=overwrite 1=stop */
#endif

/* ===== 录像 O_DIRECT 写库(写库重构): 绕过 page cache, 每路聚合成大块顺序写裸盘 =====
 * 目标: 单盘稳定扛 64 路(32×主子)+ 事件, 录像不再靠 page cache 堆脏页/每秒数十次 fsync 拖垮整机。
 * 任一条件不满足(O_DIRECT 后端不支持/盘上布局不满足设备 logical_block 对齐)→ 自动回退旧缓冲写。 */
#ifndef RSDK_REC_ODIRECT
#  define RSDK_REC_ODIRECT 1                 /* 1=启用 O_DIRECT 数据句柄; 0=强制回退旧缓冲写 */
#endif
#ifndef RSDK_REC_STAGE_BYTES
#  define RSDK_REC_STAGE_BYTES (512*1024)    /* 每路聚合 buffer 目标大小(实际按 dio_align 取整) */
#endif
#ifndef RSDK_REC_FLUSH_MS
#  define RSDK_REC_FLUSH_MS 1500             /* 聚合未满时强制刷盘时限(ms) → 掉电丢失窗口上界 */
#endif
#ifndef RSDK_REC_DATASYNC_SEC
#  define RSDK_REC_DATASYNC_SEC 2            /* 数据 fd 周期 fdatasync 间隔(秒): 刷盘控制器 write cache */
#endif

typedef enum {
    RSDK_F_ENCRYPTION = 0, RSDK_F_METADATA, RSDK_F_MULTIDISK_BALANCE,
    RSDK_F_BACKUP_FMP4, RSDK_F__MAX
} rsdk_feature_id_t;

/* 编译期常量: 关闭特性可被死代码消除 */
RSDK_API int      rsdk_feature_on(rsdk_feature_id_t f);
RSDK_API uint32_t rsdk_feature_mask(void);   /* 打包成 RSDK_FEAT_* 位掩码, 写入 SuperBlock */

#ifdef __cplusplus
}
#endif
#endif
