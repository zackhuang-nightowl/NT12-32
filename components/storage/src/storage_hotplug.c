/***************************************************************************************
 *  storage_hotplug.c — SATA 热插拔监听（netlink KOBJECT_UEVENT）
 *
 *  监听内核 uevent，捕获 block 设备 add/remove，转成 storage 事件。
 *  非阻塞：由 nvr_storage_tick() 周期 poll。
 ***************************************************************************************/
#include "storage_internal.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <linux/netlink.h>

int stg_hotplug_open(struct nvr_storage *s)
{
    struct sockaddr_nl nls;
    memset(&nls, 0, sizeof(nls));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid    = 0;                 /* 内核多播 */
    nls.nl_groups = 1;                 /* NETLINK_KOBJECT_UEVENT 组 1 */

    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) { s->hotplug_fd = -1; return -1; }
    if (bind(fd, (struct sockaddr *)&nls, sizeof(nls)) < 0) {
        close(fd); s->hotplug_fd = -1; return -1;
    }
    s->hotplug_fd = fd;
    return 0;
}

/* 可移动介质(USB)判定:读 /sys/block/<base>/removable==1。读不到(uevent 早于 sysfs 就绪的竞态)
 * 按非可移动处理——交由扫描层 nvr_storage_scan 的 removable 过滤兜底,USB 不会漏进盘组。 */
static int dev_is_removable(const char *devname)
{
    if (!devname) return 0;
    const char *base = strrchr(devname, '/');
    base = base ? base + 1 : devname;
    char path[128];
    snprintf(path, sizeof(path), "/sys/block/%s/removable", base);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}

/* uevent 报文是一串 '\0' 分隔的 KEY=VAL；解析出 ACTION 与 DEVNAME/SUBSYSTEM */
static void parse_uevent(struct nvr_storage *s, const char *buf, int len)
{
    const char *action = NULL, *devname = NULL, *subsystem = NULL, *devtype = NULL;
    for (int i = 0; i < len; ) {
        const char *line = buf + i;
        int l = (int)strlen(line);
        if (l == 0) { i++; continue; }
        if      (!strncmp(line, "ACTION=", 7))    action    = line + 7;
        else if (!strncmp(line, "DEVNAME=", 8))   devname   = line + 8;
        else if (!strncmp(line, "SUBSYSTEM=", 10))subsystem = line + 10;
        else if (!strncmp(line, "DEVTYPE=", 8))   devtype   = line + 8;
        i += l + 1;
    }
    if (!action || !subsystem || strcmp(subsystem, "block") != 0) return;
    if (devtype && strcmp(devtype, "disk") != 0) return;      /* 只关心整盘 */

    if (!strcmp(action, "add")) {
        /* ★ 可移动盘(USB/读卡器)不属录像存储:直接忽略,不触发重扫/入组。U 盘的文件访问由 udev
         * 自动挂载(/mnt/usb)提供,与录像盘组无关。否则插 U 盘会白扫一遍(I/O 冲击=插入卡顿)、
         * 把在录 HDD 的 ACTIVE 态重置、甚至把曾格式化成 RSDK 的 U 盘并进盘组(拔出即 D 态锁死)。 */
        if (dev_is_removable(devname)) return;
        /* 新盘插入 → 触发一次重扫（由 mgr 决定识别/格式化/纳入） */
        stg_emit(s, NVR_STG_EVT_DISK_ADDED, NULL);
    } else if (!strcmp(action, "remove")) {
        /* 掉盘 → 标记对应盘 OFFLINE，写入自动转其余盘 */
        nvr_disk_t *hit = NULL;
        if (devname) {
            for (int k = 0; k < s->ndisks; k++)
                if (strstr(s->disks[k].path, devname)) {
                    s->disks[k].state = NVR_DISK_OFFLINE;
                    hit = &s->disks[k];
                    break;
                }
        }
        stg_emit(s, NVR_STG_EVT_DISK_REMOVED, hit);
    }
}

void stg_hotplug_poll(struct nvr_storage *s)
{
    if (s->hotplug_fd < 0) return;
    char buf[4096];
    for (;;) {
        int n = (int)recv(s->hotplug_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        buf[n] = 0;
        parse_uevent(s, buf, n);
    }
}

void stg_hotplug_close(struct nvr_storage *s)
{
    if (s->hotplug_fd >= 0) { close(s->hotplug_fd); s->hotplug_fd = -1; }
}
