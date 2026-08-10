/* Copyright (C) 2025-2026, Nightowl DG. RSDK 设备信息/健康(设计 §4/§11). */
#ifndef RSDK_DISK_H
#define RSDK_DISK_H
#include "rsdk_types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 存储器类别(识别 hdd/sdcard/usb 等) */
typedef enum {
    RSDK_DISKCLASS_UNKNOWN = 0,
    RSDK_DISKCLASS_HDD,        /* 固定机械盘 */
    RSDK_DISKCLASS_SSD,        /* 固定固态盘 */
    RSDK_DISKCLASS_SDCARD,     /* SD 卡(mmcblk) */
    RSDK_DISKCLASS_USB,        /* 可移动 USB */
} rsdk_disk_class_t;

/* 身份 + 几何(一次性读) */
typedef struct {
    char     model[41];        /* ATA IDENTIFY words 27-46, 去尾空格; 取不到="" */
    char     serial[21];       /* words 10-19 */
    char     firmware[9];      /* words 23-26 */
    uint64_t capacity_bytes;   /* 块设备 BLKGETSIZE64; 普通文件 = 文件大小(=总容量) */
    uint32_t logical_sec;      /* BLKSSZGET, 典型 512; 文件兜底 512 */
    uint32_t physical_sec;     /* BLKPBSZGET, 典型 512/4096; 文件兜底 512 */
    int      is_ssd;           /* 1=SSD 0=HDD -1=未知 */
    int      is_removable;     /* 1=可移动(/sys removable, 如 USB) 0=固定 -1=未知 */
    rsdk_disk_class_t dclass;  /* 存储器类别:HDD/SSD/SDCARD/USB */
} rsdk_disk_info_t;
/* 类别 → 上报字符串前缀("hdd"/"sdcard"/"usb");未知返回 "hdd" */
RSDK_API const char *rsdk_disk_class_str(rsdk_disk_class_t c);
RSDK_API rsdk_err_t rsdk_disk_identify(const char *devpath, rsdk_disk_info_t *out);

/* 统一存储名(NOP 接口 name 字段):把 linux 设备名转成上报值。
 *   mmcblk* → "sdcard"; 可移动(USB) → "usb"[/usb2..]; 固定盘 → "hdd"[/hdd2..]。
 * hdd_seq/usb_seq:同类设备序号(0→hdd/usb, 1→hdd2/usb2, ..);sdcard 忽略序号。
 * (NAS 为网络挂载、非块设备,由上层给 "nas"。) */
RSDK_API void rsdk_disk_unified_name(const char *devpath, int seq, char *out, size_t n);

/* 健康(SMART), 固件按分钟级定时调用 */
typedef struct {
    int      healthy;          /* 1=PASS 0=FAIL -1=未知 */
    int      temp_c;           /* -1=未知 */
    uint64_t power_on_hours;   /* attr 9 */
    uint64_t reallocated;      /* attr 5 */
    uint64_t pending;          /* attr 197 */
    uint64_t offline_uncorr;   /* attr 198 */
    uint64_t crc_errors;       /* attr 199 */
} rsdk_smart_t;
RSDK_API rsdk_err_t rsdk_smart_read(const char *devpath, rsdk_smart_t *out);
RSDK_API int        rsdk_smart_ok  (const rsdk_smart_t *s);

/* ---- 完整 SMART 属性表(供 getAllDisksHealth 的 smart[] 明细) ---- */
#define RSDK_SMART_ATTR_MAX 30
typedef struct {
    uint8_t  id;               /* 属性 ID(如 5/9/194/197) */
    uint16_t flags;            /* status flags(word) */
    uint8_t  value;            /* 归一化当前值 */
    uint8_t  worst;            /* 归一化历史最差 */
    uint8_t  thresh;           /* 阈值(读 SMART thresholds 页; 取不到=0) */
    uint64_t raw;              /* 48-bit 原始值 */
} rsdk_smart_attr_t;
/* 读完整属性表到 out[](含 thresh)。返回填入条数;<0=出错(设备/权限)。 */
RSDK_API int         rsdk_smart_read_attrs(const char *devpath, rsdk_smart_attr_t *out, int cap);
/* 属性 ID → 标准名(如 5→"Reallocated Sectors Count");未知返回 "Unknown"。 */
RSDK_API const char *rsdk_smart_attr_name(uint8_t id);

/* RSDK 盘上状态(免 open, peek SuperBlock) */
typedef struct {
    int      formatted;        /* SB magic == RSDK_SB_MAGIC 且 CRC 通过 */
    uint64_t data_bytes;       /* 数据环**总容量** = data_chunks * chunk_bytes */
    uint64_t used_bytes;       /* **已使用**:未绕盘=write_ptr*chunk; 已绕盘=data_bytes */
    uint64_t free_bytes;       /* **空闲** = data_bytes - used_bytes(已绕盘=0) */
    int      wrapped;          /* seq_epoch>1 */
    uint32_t feature_mask;
    uint8_t  enc_algo;
} rsdk_disk_status_t;
RSDK_API rsdk_err_t rsdk_disk_probe(const char *devpath, rsdk_disk_status_t *out);

#ifdef __cplusplus
}
#endif
#endif
