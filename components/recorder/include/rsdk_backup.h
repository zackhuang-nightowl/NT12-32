/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像导出(设计 §7.6).
 * 目前仅 MP4; 框架留扩展(格式枚举 + muxer 接口), 后续可加 fMP4/MKV 等。 */
#ifndef RSDK_BACKUP_H
#define RSDK_BACKUP_H
#include "rsdk_types.h"
#include "rsdk_storgedev.h"
#include "rsdk_index.h"
#include "rsdk_balance.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 导出格式(可扩展; 目前仅 MP4 已实现) */
typedef enum {
    RSDK_EXPORT_MP4  = 0,
    RSDK_EXPORT_FMP4 = 1,   /* fragmented MP4(流式/DASH) */
    /* 预留: RSDK_EXPORT_MKV, RSDK_EXPORT_AVI ... */
    RSDK_EXPORT__MAX
} rsdk_export_fmt_t;

typedef struct {
    int      fmt;         /* rsdk_export_fmt_t; 非 MP4 暂返回 RSDK_E_PARAM */
    uint32_t width, height; /* 显示尺寸; 0 → 默认 3840x2160 */
    uint32_t timescale;   /* 媒体时基; 0 → 90000 */
    uint32_t fps;         /* 0 → 从帧 pts 推算 */
} rsdk_export_opt_t;

/* ---- muxer 扩展接口: 新增封装格式 = 实现本接口 + rsdk_backup_register ---- */
typedef struct {
    const char *name;
    int         fmt;                                   /* rsdk_export_fmt_t */
    void      *(*open)  (const char *path, const rsdk_export_opt_t *opt);
    /* annexb: 一个访问单元(可含多 NAL); codec: RSDK_CODEC_*; keyframe: 是否关键帧 */
    int        (*add)   (void *ctx, const uint8_t *annexb, uint32_t len,
                         uint64_t pts, int keyframe, int codec);
    int        (*finish)(void *ctx);                   /* 收尾并关闭; 返回 0=OK */
} rsdk_muxer_t;

RSDK_API rsdk_err_t rsdk_backup_register(const rsdk_muxer_t *mux);  /* 注册新格式 */

/* ---- 导出 API ---- */
/* 导出一个时间范围+通道(跨盘归并, 自动解密)为文件 */
RSDK_API rsdk_err_t rsdk_backup_export(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                                       const rsdk_export_opt_t *opt, const char *out_path);
/* 同 export,stream=0主/1子;stream<0 不限。无该码流段则 RSDK_E_NOTFOUND。 */
RSDK_API rsdk_err_t rsdk_backup_export_stream(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                                              int stream, const rsdk_export_opt_t *opt,
                                              const char *out_path);
/* 导出单个已命中段(单盘) */
RSDK_API rsdk_err_t rsdk_backup_export_seg(rsdk_dev_t *d, const rsdk_index_slot_t *seg,
                                           const rsdk_export_opt_t *opt, const char *out_path);

#ifdef __cplusplus
}
#endif
#endif
