/* Copyright (C) 2025-2026, Nightowl DG. RSDK 盘注册/格式化/超级块(设计 §2, 冻结 §1). */
#ifndef RSDK_STORGEDEV_H
#define RSDK_STORGEDEV_H
#include "rsdk_types.h"
#include "rsdk_rawdev.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_dev rsdk_dev_t;

typedef struct {
    const char *sn;          /* 设备SN(派生KEK; NULL=用默认) */
    uint32_t    chunk_mib;   /* 0=auto(按盘容量) */
    uint32_t    slots_per_chunk;
    uint32_t    feature_mask;/* 0=取 rsdk_feature_mask() */
    double      meta_ratio_pct;
    uint32_t    format_time; /* epoch; 0=调用方后填 */
    uint8_t     hdd_full;    /* 0=overwrite 1=stop; opt=NULL 时取 RSDK_CFG_HDD_FULL */
} rsdk_format_opt_t;

typedef struct {
    uint64_t total_sectors, chunk_count, chunk_sectors;
    uint64_t data_start_sec, free_chunks, meta_chunk_count;
    uint32_t feature_mask; uint8_t enc_algo; uint8_t hdd_full;
} rsdk_dev_info_t;

/* 打开(若未格式化返回 RSDK_E_FORMAT, 需先 rsdk_format) */
RSDK_API rsdk_err_t rsdk_dev_open (const char *path, rsdk_dev_t **out);
RSDK_API rsdk_err_t rsdk_format   (const char *path, const rsdk_format_opt_t *opt);
RSDK_API rsdk_err_t rsdk_dev_info (rsdk_dev_t *d, rsdk_dev_info_t *info);
RSDK_API void       rsdk_dev_close(rsdk_dev_t *d);
/* 密钥轮换(KEK 级): 换新盐重新派生 KEK 并重封装同一 DEK, 数据无需重加密即失效旧封装。 */
RSDK_API rsdk_err_t rsdk_dev_rekey(rsdk_dev_t *d);

/* 内部句柄访问(供 rec/index/play 复用) */
RSDK_API rsdk_rawdev_t     *rsdk_dev_raw(rsdk_dev_t *d);
RSDK_API rsdk_superblock_t *rsdk_dev_sb (rsdk_dev_t *d);
RSDK_API rsdk_systab_t     *rsdk_dev_systab(rsdk_dev_t *d);
RSDK_API rsdk_err_t         rsdk_dev_flush(rsdk_dev_t *d);  /* 回写 SB+SysTab */
RSDK_API struct rsdk_crypto *rsdk_dev_crypto(rsdk_dev_t *d); /* NULL 表示明文 */
/* 分配下一个数据 chunk(环形覆盖); 返回 chunk 下标 + 盘内字节偏移 */
RSDK_API rsdk_err_t rsdk_dev_alloc_chunk(rsdk_dev_t *d, uint64_t *chunk, uint64_t *byte_off);
/* 在 MetaRegion 追加分配 size 字节(环形); 返回盘内绝对字节偏移。metadata=off 时返回 E_NOSPACE。
 * 供元数据 Tier-2 大文档 与 抓拍 PIC blob 共用。 */
RSDK_API rsdk_err_t rsdk_dev_meta_alloc(rsdk_dev_t *d, uint64_t size, uint64_t *abs_off);
RSDK_API uint16_t   rsdk_dev_index(rsdk_dev_t *d);   /* 本盘在盘组内序号 */
RSDK_API int        rsdk_dev_is_wrapped(rsdk_dev_t *d); /* 是否已绕盘至少一圈(覆盖模式) */

#ifdef __cplusplus
}
#endif
#endif
