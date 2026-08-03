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
