/***************************************************************************************
 *  Copyright (C) 2025-2026, Nightowl DG, all rights reserved.
 *  RSDK 共享类型 + 盘上结构(与 盘上格式冻结_v1.md 逐字节一致; packed 小端)。
 ***************************************************************************************/
#ifndef RSDK_TYPES_H
#define RSDK_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RSDK_API
#define RSDK_API
#endif

#define RSDK_SEC              512u
#define RSDK_SB_MAGIC         "RSDK01\0\0"
#define RSDK_FRAME_MAGIC      "rsdkfrm\0"
#define RSDK_FRAME_ALIGN      128u
#define RSDK_FORMAT_VERSION   2u   /* v2: 帧载荷 CRC(复用冗余 iv_nonce 尾4字节, 偏移不变); v1 只读兼容 */
#define RSDK_DEFAULT_SN       "NT12-32"   /* 无工厂区 SN 时的默认(format/open/rekey 须一致) */

/* ---- 返回码 ---- */
typedef enum {
    RSDK_OK = 0,
    RSDK_E_IO       = -1,
    RSDK_E_PARAM    = -2,
    RSDK_E_NOTFOUND = -3,
    RSDK_E_CORRUPT  = -4,
    RSDK_E_NOSPACE  = -5,
    RSDK_E_DB       = -6,
    RSDK_E_CRYPTO   = -7,
    RSDK_E_FORMAT   = -8,
    RSDK_E_BUSY     = -9,
    RSDK_E_SECTORSIZE = -10
} rsdk_err_t;

/* ---- 特性位(= SuperBlock.feature_mask; 冻结 §4) ---- */
enum {
    RSDK_FEAT_ENCRYPTION      = 0x01,
    RSDK_FEAT_METADATA        = 0x02,
    RSDK_FEAT_MULTIDISK_BAL   = 0x04,
    RSDK_FEAT_BACKUP_FMP4     = 0x08
};

/* ---- 录像/事件类型(冻结 §3.2) ---- */
enum rsdk_rectype {
    RSDK_REC_CONTINUOUS = 0, RSDK_REC_MOTION = 1, RSDK_REC_HUMAN = 2,
    RSDK_REC_FACE = 3, RSDK_REC_VEHICLE = 4, RSDK_REC_LINECROSS = 5,
    RSDK_REC_INTRUSION = 6, RSDK_REC_ANIMAL = 7, RSDK_REC_PACKAGE = 8,
    RSDK_REC_DOORBELL = 9
};

/* ---- 编解码 ---- */
enum { RSDK_CODEC_H264 = 0, RSDK_CODEC_H265 = 1, RSDK_CODEC_AAC = 2 };

/* 满盘策略 */
enum { RSDK_HDDFULL_OVERWRITE = 0, RSDK_HDDFULL_STOP = 1 };
enum { RSDK_FRAME_I = 0, RSDK_FRAME_P = 1, RSDK_FRAME_B = 2, RSDK_FRAME_AUDIO = 3 };

/* ==================== 盘上结构(packed) ==================== */

/* SuperBlock 512B (冻结 §1) */
typedef struct __attribute__((packed)) {
    char     magic[8];
    uint32_t version;
    uint32_t sb_crc32;
    uint8_t  disk_uuid[16];
    uint8_t  group_uuid[16];
    uint16_t group_disk_index;
    uint16_t group_disk_count;
    uint64_t total_sectors;
    uint64_t rsv_start_sec;
    uint64_t data_start_sec;
    uint64_t chunk_sectors;
    uint64_t chunk_count;
    uint64_t write_ptr_chunk;
    uint64_t seq_epoch;
    uint32_t feature_mask;
    uint32_t _rsvf;
    uint64_t meta_start_chunk;
    uint64_t meta_chunk_count;
    uint8_t  enc_algo;            /* 0=none 1=AES-256-CTR */
    uint8_t  enc_flags;
    uint8_t  hdd_full;            /* 满盘策略: 0=overwrite(循环覆盖) 1=stop(停录) */
    uint8_t  _rsv0[5];
    uint8_t  kdf_salt[16];
    uint8_t  wrapped_dek[48];
    uint8_t  dek_kcv[8];
    uint8_t  _pad[300];
} rsdk_superblock_t;

/* SysTab (定长, ≤4096B; 冻结 §2.2 精简版) */
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* 'STAB' = 0x42415453 */
    uint32_t sys_version;
    uint32_t systab_size;
    uint32_t crc32;
    uint64_t format_time;
    uint64_t bitmap_start_sec;
    uint64_t bitmap_sectors;
    uint64_t index_start_sec;
    uint64_t index_sectors;
    uint32_t index_slot_size;    /* =64 */
    uint32_t index_slot_count;
    uint32_t index_next;         /* 下一个可写索引槽(环形) */
    uint32_t _rsvi;
    uint64_t meta_next_off;      /* MetaRegion 追加分配器写指针(区内字节偏移, 环形) */
    uint64_t meta_bytes;         /* MetaRegion 大小(字节; 0=未启用) */
    struct { uint64_t cur_chunk; uint32_t cur_off; uint32_t seg_seq; } chn[32];
    uint8_t  _rsv[4096 - 88 - 32*16];   /* 精确补足到 4096(冻结: ≤4096) */
} rsdk_systab_t;

/* 记录种类(数据区自描述):帧 + 4 类内联标记。marker 专有数据放 64B 头后的 payload。 */
enum rsdk_rec_kind {
    RSDK_RK_FRAME = 0,      /* 音视频帧 */
    RSDK_RK_SEG_OPEN = 1,   /* 段起(payload: rsdk_mk_seg_open_t) */
    RSDK_RK_SEG_CLOSE = 2,  /* 段止(payload: rsdk_mk_seg_close_t) */
    RSDK_RK_EVENT = 3,      /* 事件标记(payload: rsdk_mk_event_t) */
    RSDK_RK_CLOUD_STATE = 4 /* 云存终态(payload: rsdk_mk_cloud_t) */
};

/* FrameRecord 头 64B (冻结 §2;偏移不变——原保留字节命名为 rec_kind/rectype/event_id) */
typedef struct __attribute__((packed)) {
    char     magic[8];           /* "rsdkfrm\0" */
    uint16_t chn;
    uint8_t  stream;             /* 0主/1子/2音 */
    uint8_t  codec;
    uint8_t  frame_type;
    uint8_t  enc;                /* 0=明文 1=AES-256-CTR */
    uint8_t  rec_kind;           /* 0x0E: rsdk_rec_kind(帧/标记判别符) */
    uint8_t  rectype;            /* 0x0F: 所属段类型(0 连续/事件类型枚举) */
    uint32_t payload_len;        /* FRAME=负载字节;marker=marker payload 字节 */
    uint32_t seg_id;
    uint32_t frame_seq;
    uint64_t pts;
    uint64_t wall_time;          /* 掉索引自解析用 */
    uint8_t  iv_nonce[4];        /* 0x2C: v1 兼容占位(解密不依赖它, 用 seg_id+frame_seq 派生) */
    uint32_t payload_crc32;      /* 0x30: v2 明文载荷 CRC32(FRAME 有效; marker=0)。v1 盘此处为旧 nonce
                                  * 尾字节, 仅当盘 version>=2 时校验。被 hdr_crc32 覆盖保护。 */
    uint32_t hdr_crc32;
    uint64_t event_id;           /* 0x38: 0=无;连续帧被事件命中则打该 id;事件段帧自带 */
} rsdk_frame_hdr_t;

/* marker payload(紧跟 64B 头;payload_len=对应结构大小) */
typedef struct __attribute__((packed)) { uint8_t mode; uint8_t _r[3]; } rsdk_mk_seg_open_t;   /* mode 0 continuous/1 event */
typedef struct __attribute__((packed)) { uint32_t frame_count; uint64_t total_bytes; uint32_t seg_crc; } rsdk_mk_seg_close_t;
typedef struct __attribute__((packed)) { uint32_t event_start; uint32_t event_end; uint32_t type_mask; uint32_t ref_seg_id; } rsdk_mk_event_t; /* event_end 0=进行中;type_mask 复合类型位集 */
typedef struct __attribute__((packed)) { uint8_t state; uint8_t _r[3]; } rsdk_mk_cloud_t;      /* 0不传/1未传/2上传中/3已传/4失败 */

/* Index Slot 64B (冻结 §3;_pad 首字节命名为 stream) */
enum { RSDK_SLOT_VALID = 0x01, RSDK_SLOT_EVENT = 0x02, RSDK_SLOT_OPEN = 0x04 };
typedef struct __attribute__((packed)) {
    uint32_t seg_id;
    uint16_t chn;
    uint8_t  rectype;
    uint8_t  flags;
    uint32_t start_time;
    uint32_t end_time;           /* 0xFFFFFFFF=未闭合 */
    uint32_t frame_count;
    uint64_t total_bytes;
    uint16_t start_disk;
    uint16_t end_disk;
    uint64_t start_chunk;
    uint32_t start_off;
    uint64_t end_chunk;
    uint32_t end_off;
    uint32_t crc32;
    uint8_t  stream;             /* 0x3C: 0主/1子/2音(段按 chn,stream 区分) */
    uint8_t  _pad[3];
} rsdk_index_slot_t;

#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(rsdk_frame_hdr_t)  == 64, "FrameRecord 头必须 64B");
_Static_assert(sizeof(rsdk_index_slot_t) == 64, "Index Slot 必须 64B");
#endif
#endif

/* 视频定位引用 */
typedef struct { uint16_t disk; uint64_t chunk; uint32_t off; uint64_t pts; } rsdk_segref_t;

/* 一帧(应用层) */
typedef struct {
    uint16_t chn; uint8_t stream, codec, frame_type;
    uint64_t pts, wall_time;
    const uint8_t *data; uint32_t len;   /* 写入: 明文 Annex-B */
} rsdk_frame_t;

#ifdef __cplusplus
}
#endif
#endif /* RSDK_TYPES_H */
