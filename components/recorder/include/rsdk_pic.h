/* Copyright (C) 2025-2026, Nightowl DG. RSDK 抓拍(PIC).
 * 事件触发时刻那帧 JPEG → 加密写 MetaRegion(环形) → 截图指针回填事件索引槽(权威,可扫盘重建)。
 * 取图只经事件槽 snap_off(不经 meta.db);仅在 metadata=on 时可用(复用 MetaRegion)。 */
#ifndef RSDK_PIC_H
#define RSDK_PIC_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 抓拍类型(对标逆向: P 目标抠图 / M 事件主图 / H 场景图 / 封面) */
typedef enum {
    RSDK_PIC_TARGET = 0,   /* P: 检测目标抠图(检索列表/推送小图) */
    RSDK_PIC_MAIN   = 1,   /* M: 事件主图(推送主配图) */
    RSDK_PIC_SCENE  = 2,   /* H: 场景大图 */
    RSDK_PIC_COVER  = 3    /* 时间轴/通道封面 */
} rsdk_pic_type_t;

/* 写入键: 事件触发时提供; event_id 必填(与事件绑定,截图指针回填该事件槽) */
typedef struct {
    int             chn;
    uint32_t        ts;         /* 抓拍 epoch */
    uint64_t        event_id;   /* 绑定事件(回填 snap_off 到该槽) */
    rsdk_pic_type_t type;
    uint16_t        w, h;
} rsdk_pic_key_t;

/* 盘上 blob 头(packed 40B, 之后紧跟 JPEG 字节) */
typedef struct __attribute__((packed)) {
    char     magic[4];         /* "PIC0" */
    uint8_t  type;             /* rsdk_pic_type_t */
    uint8_t  enc;              /* 0=明文 1=AES-256-CTR */
    uint16_t chn;
    uint64_t event_id;
    uint32_t ts;
    uint16_t w, h;
    uint32_t jpeg_len;
    uint32_t crc32;            /* 头+负载 CRC */
    uint8_t  _rsv[8];
} rsdk_pic_hdr_t;

/* 事件触发时刻抓拍: JPEG 加密写 MetaRegion,截图指针回填 event_id 对应事件槽。
 * 写入侧不解码 JPEG,原样存。pic_id(可空)返回 MetaRegion 绝对偏移。 */
RSDK_API rsdk_err_t rsdk_pic_write(rsdk_dev_t *d, const rsdk_pic_key_t *k,
                                   const void *jpeg, size_t len, uint64_t *pic_id);
/* 单张截图上限:20MP JPEG(高质量约 0.8B/px)→ ~16MiB。宏,实机调试后可调。 */
#ifndef RSDK_PIC_MAX_BYTES
#define RSDK_PIC_MAX_BYTES (16u << 20)
#endif

/* 按 MetaRegion 绝对偏移直接读截图(供事件槽 snap_off 取图)。
 * expect_event_id 校验环覆盖(0=不校验;*jpeg 需 free)。 */
RSDK_API rsdk_err_t rsdk_pic_read_blob(rsdk_dev_t *d, uint64_t off, uint64_t expect_event_id,
                                       void **jpeg, size_t *len);

#ifdef __cplusplus
}
#endif
#endif
