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
#define RSDK_FORMAT_VERSION   1u
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
    RSDK_E_BUSY     = -9
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

/* FrameRecord 头 64B (冻结 §2) */
typedef struct __attribute__((packed)) {
    char     magic[8];           /* "rsdkfrm\0" */
    uint16_t chn;
    uint8_t  stream;
    uint8_t  codec;
    uint8_t  frame_type;
    uint8_t  enc;                /* 0=明文 1=AES-256-CTR */
    uint16_t _rsv0;
    uint32_t payload_len;
    uint32_t seg_id;
    uint32_t frame_seq;
    uint64_t pts;
    uint64_t wall_time;
    uint8_t  iv_nonce[8];
    uint32_t hdr_crc32;
    uint8_t  _rsvh[8];
} rsdk_frame_hdr_t;

/* Index Slot 64B (冻结 §3) */
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
    uint8_t  _pad[4];
} rsdk_index_slot_t;

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
