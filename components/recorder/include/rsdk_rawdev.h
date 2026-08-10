/* Copyright (C) 2025-2026, Nightowl DG. RSDK 裸设备抽象(裸盘/镜像文件统一). */
#ifndef RSDK_RAWDEV_H
#define RSDK_RAWDEV_H
#include "rsdk_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_rawdev rsdk_rawdev_t;

/* 打开 /dev/sdX 或普通镜像文件(自测用); 读回总扇区数 */
RSDK_API rsdk_err_t rsdk_rawdev_open (const char *path, rsdk_rawdev_t **out);
RSDK_API uint64_t   rsdk_rawdev_sectors(rsdk_rawdev_t *d);
RSDK_API uint32_t   rsdk_rawdev_logical_sec(rsdk_rawdev_t *d);
/* 按字节偏移读/写(内部保证 512 对齐访问块设备) */
RSDK_API rsdk_err_t rsdk_rawdev_pread (rsdk_rawdev_t *d, uint64_t off, void *buf, size_t n);
RSDK_API rsdk_err_t rsdk_rawdev_pwrite(rsdk_rawdev_t *d, uint64_t off, const void *buf, size_t n);
RSDK_API rsdk_err_t rsdk_rawdev_sync (rsdk_rawdev_t *d);
RSDK_API void       rsdk_rawdev_close(rsdk_rawdev_t *d);
/* 测试钩子: 令接下来 count 次落在 [off,off+len) 的 pwrite 返回 RSDK_E_IO(模拟坏扇区)。 */
RSDK_API void       rsdk_rawdev_fault_inject(rsdk_rawdev_t *d, uint64_t off, uint64_t len, int count);

#ifdef __cplusplus
}
#endif
#endif
