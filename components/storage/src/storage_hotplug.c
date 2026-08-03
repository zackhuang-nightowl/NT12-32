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
