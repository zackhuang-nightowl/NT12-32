/* Copyright (C) 2025-2026, Nightowl DG. RSDK 盘注册/格式化/超级块(设计 §2, 冻结 §1). */
#ifndef RSDK_STORGEDEV_H
#define RSDK_STORGEDEV_H
#include "rsdk_types.h"
#include "rsdk_rawdev.h"
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_dev rsdk_dev_t;

/* ★ 元数据递归锁(冻结格式外新增): 保护本盘 sb/st/index/badmap 内存态与其落盘。
 * rec/index 层跨多个原语的复合操作(如段翻转)用它把整段变更做成原子。数据区 pread/pwrite 不走此锁。
 * 入盘组后 rsdk_group_open 用 rsdk_dev_bind_lock 让整组共享一把锁(避免多盘 writer 迁移的锁序问题)。 */
RSDK_API void rsdk_dev_lock(rsdk_dev_t *d);
RSDK_API void rsdk_dev_unlock(rsdk_dev_t *d);
RSDK_API void rsdk_dev_bind_lock(rsdk_dev_t *d, pthread_mutex_t *shared);

/* ---- chunk→slot 内存加速表(纯内存, 覆盖回收 O(1) / upsert O(1) / 真实容量; 调用方须持 dev 锁) ---- */
#define RSDK_MAP_NONE 0xFFFFFFFFu
RSDK_API uint32_t rsdk_dev_map_get(rsdk_dev_t *d, uint64_t chunk);       /* 空/越界/无表→RSDK_MAP_NONE */
RSDK_API void     rsdk_dev_map_set(rsdk_dev_t *d, uint64_t chunk, uint32_t slot); /* 维护 used_chunks */
RSDK_API int      rsdk_dev_map_ready(rsdk_dev_t *d);                     /* 1=有表 0=回退全扫描 */
RSDK_API int      rsdk_dev_map_alloc(rsdk_dev_t *d);                     /* 供 index 层开盘建表 */

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

typedef struct {
    uint32_t chunk_mib;          /* 选定档: opt 指定值, 或 8/16/32 */
    uint64_t chunk_sectors;
    uint64_t chunk_count;        /* 总 chunk(含 meta) */
    uint64_t data_start_sec;
    uint64_t bitmap_sectors;
    uint64_t index_sectors;
    uint64_t evtidx_sectors;     /* 事件索引区扇区数(metadata=off 时 0) */
    uint64_t meta_chunk_count;
    uint64_t data_chunk_count;   /* = chunk_count - meta_chunk_count */
    uint32_t index_slot_count;
    uint32_t evtidx_slot_count;  /* 事件槽总数 */
} rsdk_layout_t;

/* 扇区大小谓词: 512 → 1(支持); 其它(如 4096/4Kn) → 0(不支持) */
RSDK_API int rsdk_sector_supported(uint32_t logical_sec);

/* 纯函数(无 I/O): 由容量+参数推导布局。total_sectors=512B单位盘扇区数;
 * chunk_mib_req=0 时按容量自动选档; slots=每chunk索引槽因子(0→RSDK_CFG_SLOTS_PER_CHUNK);
 * feature_mask 决定是否留 MetaRegion; meta_ratio_pct=meta 占比(<=0→配置默认)。
 * 容量过小无法布局(<64MiB 或算得 data_chunk_count==0)返回 RSDK_E_NOSPACE。 */
RSDK_API rsdk_err_t rsdk_plan_layout(uint64_t total_sectors, uint32_t chunk_mib_req,
                                     uint32_t slots, uint32_t feature_mask,
                                     double meta_ratio_pct, rsdk_layout_t *out);

/* 免 open 探测: 读主 SuperBlock 并校验 magic+CRC(供 rsdk_disk_probe / 上层枚举) */
RSDK_API rsdk_err_t rsdk_peek_superblock(const char *devpath, rsdk_superblock_t *sb);

/* 打开(若未格式化返回 RSDK_E_FORMAT, 需先 rsdk_format) */
RSDK_API rsdk_err_t rsdk_dev_open (const char *path, rsdk_dev_t **out);
/* 带外改盘(如 format 直写盘区)后原地重载运行态,不换 dev 指针(借用者立即见新态,免重启)。停写后调用。 */
RSDK_API rsdk_err_t rsdk_dev_reload(rsdk_dev_t *d);
RSDK_API rsdk_err_t rsdk_format   (const char *path, const rsdk_format_opt_t *opt);
RSDK_API rsdk_err_t rsdk_dev_info (rsdk_dev_t *d, rsdk_dev_info_t *info);
RSDK_API void       rsdk_dev_close(rsdk_dev_t *d);
/* 密钥轮换(KEK 级): 换新盐重新派生 KEK 并重封装同一 DEK, 数据无需重加密即失效旧封装。 */
RSDK_API rsdk_err_t rsdk_dev_rekey(rsdk_dev_t *d);

/* 内部句柄访问(供 rec/index/play 复用) */
RSDK_API rsdk_rawdev_t     *rsdk_dev_raw(rsdk_dev_t *d);
RSDK_API rsdk_superblock_t *rsdk_dev_sb (rsdk_dev_t *d);
RSDK_API rsdk_systab_t     *rsdk_dev_systab(rsdk_dev_t *d);
RSDK_API uint8_t           *rsdk_dev_evtidx_cache(rsdk_dev_t *d);  /* 事件区内存镜像(NULL=直读盘) */
RSDK_API void               rsdk_dev_evtidx_reload(rsdk_dev_t *d); /* 带外改盘后从盘重载镜像(一致化) */
RSDK_API rsdk_err_t         rsdk_dev_flush(rsdk_dev_t *d);  /* 回写 SB+SysTab */
RSDK_API struct rsdk_crypto *rsdk_dev_crypto(rsdk_dev_t *d); /* NULL 表示明文 */
/* 分配下一个数据 chunk(环形覆盖); 返回 chunk 下标 + 盘内字节偏移 */
RSDK_API rsdk_err_t rsdk_dev_alloc_chunk(rsdk_dev_t *d, uint64_t *chunk, uint64_t *byte_off);
/* 在 MetaRegion 追加分配 size 字节(环形); 返回盘内绝对字节偏移。metadata=off 时返回 E_NOSPACE。
 * 供元数据 Tier-2 大文档 与 抓拍 PIC blob 共用。 */
RSDK_API rsdk_err_t rsdk_dev_meta_alloc(rsdk_dev_t *d, uint64_t size, uint64_t *abs_off);
RSDK_API uint16_t   rsdk_dev_index(rsdk_dev_t *d);   /* 本盘在盘组内序号 */
RSDK_API uint32_t   rsdk_dev_version(rsdk_dev_t *d); /* 盘上格式版本(>=2 回放校验载荷 CRC) */
RSDK_API int        rsdk_dev_is_wrapped(rsdk_dev_t *d); /* 是否已绕盘至少一圈(覆盖模式) */

/* 坏 chunk 处理(设计 §4.3 / 坏块): 标记/查询/计数。位图落 bitmap 区(主+备+CRC), 分配自动跳坏。 */
RSDK_API rsdk_err_t rsdk_dev_mark_bad_chunk(rsdk_dev_t *d, uint64_t chunk);
RSDK_API int        rsdk_dev_is_bad_chunk (rsdk_dev_t *d, uint64_t chunk);
RSDK_API uint64_t   rsdk_dev_bad_chunk_count(rsdk_dev_t *d);

#ifdef __cplusplus
}
#endif
#endif
