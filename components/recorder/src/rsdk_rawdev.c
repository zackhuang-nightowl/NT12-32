/* Copyright (C) 2025-2026, Nightowl DG. RSDK 裸设备(裸盘/镜像文件统一). */
#define _GNU_SOURCE
#include "rsdk_rawdev.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

struct rsdk_rawdev { int fd; uint64_t sectors; uint32_t logical_sec;
    int direct; uint32_t dio_align;                  /* O_DIRECT 句柄 + 对齐单位(=logical_sec,动态) */
    uint64_t fault_off, fault_end; int fault_left; };   /* fault_*: 测试注入坏扇区 */

rsdk_err_t rsdk_rawdev_open_ex(const char *path, int direct, rsdk_rawdev_t **out)
{
    if (!path || !out) return RSDK_E_PARAM;
    int fd = open(path, O_RDWR | O_CLOEXEC);          /* 先普通打开, fstat 后再按需加 O_DIRECT */
    if (fd < 0) return RSDK_E_IO;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return RSDK_E_IO; }
    uint64_t bytes = 0;
    uint32_t logical_sec = 512;
    int is_blk = S_ISBLK(st.st_mode);
    if (is_blk) {
        if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) { close(fd); return RSDK_E_IO; }
        int ls = 0; if (ioctl(fd, BLKSSZGET, &ls) == 0 && ls > 0) logical_sec = (uint32_t)ls;
    } else {
        bytes = (uint64_t)st.st_size;
    }
    int use_direct = 0;
    /* 仅对**块设备**启用 O_DIRECT:镜像文件(自测)保持缓冲, 避免文件系统块对齐差异导致直写失败。
     * fcntl 加 O_DIRECT 失败(设备/内核不支持)→ open 失败, 调用方回退缓冲。对齐单位取 BLKSSZGET 动态值。
     * 测试逃生门 RSDK_DIO_FORCE_FILE:允许在支持 O_DIRECT 的 ext4 常规文件上启用(主机验证聚合写用),
     * 生产不设该环境变量 → 行为不变(常规文件仍走缓冲)。 */
#ifdef O_DIRECT
    int allow_dio = is_blk || (getenv("RSDK_DIO_FORCE_FILE") != NULL);
    if (direct && allow_dio) {
        int fl = fcntl(fd, F_GETFL);
        if (fl < 0 || fcntl(fd, F_SETFL, fl | O_DIRECT) < 0) { close(fd); return RSDK_E_IO; }
        use_direct = 1;
    }
#endif
    rsdk_rawdev_t *d = calloc(1, sizeof(*d));
    if (!d) { close(fd); return RSDK_E_IO; }
    d->fd = fd; d->sectors = bytes / RSDK_SEC; d->logical_sec = logical_sec;
    d->direct = use_direct;
    d->dio_align = use_direct ? logical_sec : 0;      /* = 设备 logical_block_size(动态, 不写死512) */
    *out = d;
    return RSDK_OK;
}

rsdk_err_t rsdk_rawdev_open(const char *path, rsdk_rawdev_t **out)
{
    return rsdk_rawdev_open_ex(path, 0, out);
}

uint32_t rsdk_rawdev_dio_align(rsdk_rawdev_t *d)  { return d ? d->dio_align : 0; }
uint32_t rsdk_rawdev_logical_sec(rsdk_rawdev_t *d) { return d ? d->logical_sec : 512; }

uint64_t rsdk_rawdev_sectors(rsdk_rawdev_t *d) { return d ? d->sectors : 0; }

rsdk_err_t rsdk_rawdev_pread(rsdk_rawdev_t *d, uint64_t off, void *buf, size_t n)
{
    if (!d) return RSDK_E_PARAM;
    uint8_t *p = buf; size_t done = 0;
    while (done < n) {
        ssize_t r = pread(d->fd, p + done, n - done, (off_t)(off + done));
        if (r < 0) return RSDK_E_IO;
        if (r == 0) { memset(p + done, 0, n - done); break; } /* 超出末尾按0 */
        done += (size_t)r;
    }
    return RSDK_OK;
}

rsdk_err_t rsdk_rawdev_pwrite(rsdk_rawdev_t *d, uint64_t off, const void *buf, size_t n)
{
    if (!d) return RSDK_E_PARAM;
    if (d->fault_left > 0 && off < d->fault_end && off + n > d->fault_off) {
        d->fault_left--; return RSDK_E_IO;      /* 注入: 模拟坏扇区写失败 */
    }
    const uint8_t *p = buf; size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(d->fd, p + done, n - done, (off_t)(off + done));
        if (r < 0) return RSDK_E_IO;
        done += (size_t)r;
    }
    return RSDK_OK;
}

rsdk_err_t rsdk_rawdev_sync(rsdk_rawdev_t *d) { return d && fsync(d->fd) == 0 ? RSDK_OK : RSDK_E_IO; }

rsdk_err_t rsdk_rawdev_datasync(rsdk_rawdev_t *d) { return d && fdatasync(d->fd) == 0 ? RSDK_OK : RSDK_E_IO; }

void rsdk_rawdev_fault_inject(rsdk_rawdev_t *d, uint64_t off, uint64_t len, int count) {
    if (!d) return;
    d->fault_off = off; d->fault_end = off + len; d->fault_left = count;
}

void rsdk_rawdev_close(rsdk_rawdev_t *d) { if (d) { fsync(d->fd); close(d->fd); free(d); } }
