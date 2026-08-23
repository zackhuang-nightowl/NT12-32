/***************************************************************************************
 *  Copyright (C) 2025-2026, Nightowl DG, all rights reserved.
 *
 *  RSDK 元数据子系统 —— 完整 JSON 文档存取(设计 §12 / 冻结 §5-6)。
 *  模型: 设备产出 → 上层转 JSON 结构体 → SDK 原样入库(写时不解析内容),
 *        只按调用方给的 ts/chn/event_id/doc_type/seg_ref 建索引。
 *  智能检索: 读取侧用 SQLite JSON1 json_extract; 高频字段建表达式索引。
 ***************************************************************************************/
#ifndef RSDK_META_H
#define RSDK_META_H

#include "rsdk_types.h"   /* rsdk_err_t / rsdk_segref_t / RSDK_API */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 文档类型标签(调用方给; SDK 不据此解析内容, 仅用于过滤) ---- */
enum rsdk_doc_type {
    RSDK_DOC_AI_EVENT = 1,   /* AI 事件(人/车/越界...)整体 */
    RSDK_DOC_AI_FRAME = 2,   /* 逐帧目标框/轨迹 */
    RSDK_DOC_LPR      = 3,   /* 车牌 */
    RSDK_DOC_FACE     = 4,   /* 人脸 */
    RSDK_DOC_ALARM    = 6,   /* 报警/联动 */
    RSDK_DOC_POS      = 7     /* POS 交易 */
    /* 抓拍(SNAP)/云存态(CLOUD)已迁事件索引槽(盘上权威),不再入 meta.db。 */
};

/* ---- 写入键: 全部由调用方提供, SDK 不从 JSON 里猜 ---- */
typedef struct {
    uint32_t      ts;         /* epoch 秒(主检索键, 必填) */
    uint16_t      ts_ms;      /* 毫秒(可 0) */
    int16_t       chn;        /* 通道(-1=无) */
    uint64_t      event_id;   /* 事件绑定(0=无) */
    uint32_t      doc_type;   /* enum rsdk_doc_type */
    rsdk_segref_t seg;        /* 视频定位(可全 0) */
} rsdk_meta_key_t;

/* ---- 检索条件 ---- */
typedef struct {
    uint32_t    t0, t1;       /* 时间范围[闭区间]; t1=0 表示到最新 */
    int16_t     chn;          /* -1=任意通道 */
    uint64_t    event_id;     /* 0=任意事件 */
    uint32_t    doc_type;     /* 0=任意类型 */
    const char *json_path;    /* 深查: JSON 路径(如 "$.objects[0].plate"); NULL=不深查 */
    const char *json_match;   /* 深查: 匹配值(支持 LIKE 通配 %); 与 json_path 配对 */
    int         limit;        /* 0=不限 */
} rsdk_meta_query_t;

/* ---- 检索结果(单条: 键 + 完整 JSON 原样) ---- */
typedef struct {
    uint64_t      id;         /* meta_doc.id */
    rsdk_meta_key_t key;
    const char   *json;       /* 完整 JSON 原样(以\0结尾); 由 SDK 持有, free_list 释放 */
    size_t        json_len;
} rsdk_metadoc_t;

typedef struct {
    rsdk_metadoc_t *docs;
    int             count;
} rsdk_metadoc_list_t;

/* ---- MetaRegion 盘上文档头(冻结 §6; packed 32B, 之后紧跟完整 JSON) ---- */
typedef struct __attribute__((packed)) {
    char     magic[4];        /* "MDOC" */
    uint8_t  enc;             /* bit0 AES-256-CTR | bit1 gzip */
    uint8_t  _rsv0[3];
    uint64_t event_id;
    uint32_t ts;
    uint32_t doc_type;
    uint32_t json_len;        /* 压缩前 JSON 字节 */
    uint32_t crc32;           /* 头+负载 CRC */
} rsdk_mdoc_hdr_t;

/* ============================ API ============================ */

/* 打开/建库(读 rsdk_features.conf 的 metadata.db_path + index_paths 建表与索引) */
RSDK_API rsdk_err_t rsdk_meta_open (const char *db_path, void **out_ctx);
RSDK_API void       rsdk_meta_close(void *ctx);

/* 写入一条完整 JSON 文档(原样存, 不解析)。doc_id 可为 NULL。 */
RSDK_API rsdk_err_t rsdk_meta_put  (void *ctx, const rsdk_meta_key_t *key,
                                    const char *json, size_t len, uint64_t *doc_id);

/* 按时间戳/通道/事件/类型 (+可选深查) 检索; 结果用 rsdk_meta_free_list 释放。 */
RSDK_API rsdk_err_t rsdk_meta_query(void *ctx, const rsdk_meta_query_t *q,
                                    rsdk_metadoc_list_t *out);

/* 取单条完整 JSON。 */
RSDK_API rsdk_err_t rsdk_meta_get  (void *ctx, uint64_t doc_id, rsdk_metadoc_t *out);

/* retention: 删除 ts < before 的文档(随视频覆盖调用; 返回删除条数)。 */
RSDK_API int        rsdk_meta_purge(void *ctx, uint32_t before);
/* retention(精确): 删除绑定到指定视频 chunk 的文档/抓拍(供 rec 覆盖回收回调调用)。 */
RSDK_API int        rsdk_meta_purge_chunk(void *ctx, int disk, uint64_t chunk);

RSDK_API void       rsdk_meta_free_list(rsdk_metadoc_list_t *lst);

/* 取内部 sqlite3*(ctx 即 db;供上层自跑 SQL)。 */
RSDK_API void      *rsdk_meta_db(void *ctx);

#ifdef __cplusplus
}
#endif
#endif /* RSDK_META_H */
