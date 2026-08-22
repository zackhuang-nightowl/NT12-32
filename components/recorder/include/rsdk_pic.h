/* Copyright (C) 2025-2026, Nightowl DG. RSDK 抓拍(PIC, 对标 NK_JFSMD_PIC_Write).
 * 事件触发的 JPEG 抓拍, 用于事件推送(App/Email)的配图。
 * blob 存 MetaRegion(可加密), 索引进 meta_doc(doc_type=SNAP), 与事件绑定。
 * 仅在 metadata=on 时可用(复用 MetaRegion 与元数据索引表)。 */
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

/* 写入键: 事件触发时提供; event_id 必填(与事件绑定) */
typedef struct {
    int             chn;
    uint32_t        ts;         /* 抓拍 epoch */
    uint64_t        event_id;   /* 绑定事件(推送用) */
    rsdk_pic_type_t type;
    uint16_t        w, h;
    rsdk_segref_t   seg;        /* 对应录像时刻(点图跳视频) */
} rsdk_pic_key_t;

/* 检索结果 */
typedef struct {
    uint64_t pic_id;            /* = meta_doc.id */
    uint32_t ts; int chn; int type;
    uint16_t disk; uint64_t off; uint32_t len;  /* blob 位置 */
} rsdk_pic_ref_t;

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

/* meta: 由 rsdk_meta_open 返回的句柄(sqlite)。写入侧不解码 JPEG, 原样存。 */
RSDK_API rsdk_err_t rsdk_pic_write(rsdk_dev_t *d, void *meta, const rsdk_pic_key_t *k,
                                   const void *jpeg, size_t len, uint64_t *pic_id);
/* 单张截图上限:20MP JPEG(高质量约 0.8B/px)→ ~16MiB。宏,实机调试后可调。 */
#ifndef RSDK_PIC_MAX_BYTES
#define RSDK_PIC_MAX_BYTES (16u << 20)
#endif

/* 读回并解密 JPEG(*jpeg 需 free) */
RSDK_API rsdk_err_t rsdk_pic_read (rsdk_dev_t *d, void *meta, uint64_t pic_id,
                                   void **jpeg, size_t *len);
/* 按 MetaRegion 绝对偏移直接读截图(供事件槽 snap_off 取图,不经 meta.db)。
 * expect_event_id 校验环覆盖(0=不校验;*jpeg 需 free)。 */
RSDK_API rsdk_err_t rsdk_pic_read_blob(rsdk_dev_t *d, uint64_t off, uint64_t expect_event_id,
                                       void **jpeg, size_t *len);
/* 列出某事件的抓拍(type<0=全部) */
RSDK_API int        rsdk_pic_list_event(void *meta, uint64_t event_id, int type,
                                        rsdk_pic_ref_t *out, int cap);
/* 事件推送取图: 直接拿该事件指定类型(默认 MAIN)的 JPEG(已解密, *jpeg 需 free) */
RSDK_API rsdk_err_t rsdk_pic_get_for_event(rsdk_dev_t *d, void *meta, uint64_t event_id,
                                           int type, void **jpeg, size_t *len);

#ifdef __cplusplus
}
#endif
#endif
