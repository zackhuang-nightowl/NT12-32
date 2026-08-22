/***************************************************************************************
 *  Copyright (C) 2025-2026, Nightowl DG, all rights reserved.
 *
 *  RSDK —— NT12-32 NVR 录像系统 SDK (总头)。
 *  裸盘 no-FS 直写 + 多盘负载均衡 + AES-256-CTR + JSON 元数据文档库。
 *
 *  NVR 固件用法:
 *      #include <rsdk.h>
 *      链接 librsdk.a (metadata=on 时另链 -lsqlite3)。
 *
 *  典型链路(垂直切片):
 *      rsdk_format(dev, &opt);                 // 首次: 按盘容量自动分配布局
 *      rsdk_dev_open(dev, &d);
 *      rsdk_rec_open(d, ch, RSDK_REC_CONTINUOUS, &w);
 *      rsdk_rec_write_frame(w, &frame);        // 写盘(可 AES-CTR 加密)
 *      rsdk_rec_close(w);                      // 闭合段 → 写索引
 *      rsdk_index_query(d, t0, t1, ch, -1, slots, N);
 *      rsdk_play_open(d, &slots[0], &p);
 *      rsdk_play_next_frame(p, &hdr, &data, &len);  // 解密后 Annex-B
 ***************************************************************************************/
#ifndef RSDK_H
#define RSDK_H

#define RSDK_VERSION_MAJOR 1
#define RSDK_VERSION_MINOR 0
#define RSDK_VERSION_PATCH 0
#define RSDK_VERSION_STR  "1.0.0"

#include "rsdk_types.h"
#include "rsdk_feature.h"
#include "rsdk_rawdev.h"
#include "rsdk_crypto.h"
#include "rsdk_storgedev.h"
#include "rsdk_disk.h"
#include "rsdk_index.h"
#include "rsdk_evtidx.h"
#include "rsdk_rec.h"
#include "rsdk_play.h"
#include "rsdk_balance.h"
#include "rsdk_backup.h"
#include "rsdk_scan.h"
#include "rsdk_repair.h"
#if RSDK_CFG_METADATA
#include "rsdk_meta.h"
#include "rsdk_pic.h"
#include "rsdk_cloud.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
RSDK_API const char *rsdk_version(void);
RSDK_API const char *rsdk_strerror(rsdk_err_t e);
#ifdef __cplusplus
}
#endif
#endif /* RSDK_H */
