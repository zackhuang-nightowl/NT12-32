/***************************************************************************************
 *  storage_health.c — SMART / 只读 / 温度 / 坏道 采集
 *
 *  现状：只读检测走标准 sysfs（可靠）；SMART 明细需接平台（三选一，见文末 TODO），
 *  未接时返回 ok=1 但明细为 0/未知，不阻塞主流程。
 ***************************************************************************************/
#include "nvr_storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 从 /dev/sdX 取裸名 sdX */
static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static int sysfs_read_u(const char *name, const char *attr, unsigned long *out)
{
    char p[128], buf[64];
    snprintf(p, sizeof(p), "/sys/block/%s/%s", name, attr);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int ok = (fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);
    if (!ok) return -1;
    *out = strtoul(buf, NULL, 10);
    return 0;
}

int nvr_storage_read_health(const char *path, nvr_disk_health_t *out)
{
    memset(out, 0, sizeof(*out));
    out->temp_celsius = -1;          /* 未知 */
    const char *name = base_name(path);

    /* 1) 只读检测（内核标记）——可靠 */
    unsigned long ro = 0;
    if (sysfs_read_u(name, "ro", &ro) == 0)
        out->read_only = (ro != 0);

    /* 2) SMART 明细（温度/重映射/待映射/通电时长）——待接平台采集
     * TODO 三选一（取决于 na51090 BSP 带哪个）：
     *   a) libatasmart:  sk_disk_open(path) → sk_disk_smart_get_temperature/...
     *   b) ATA passthrough ioctl(HDIO_DRIVE_CMD / SG_IO) 直读 SMART 数据页
     *   c) 若 BSP 已挂 smartd，读其导出（如 /var/lib/smartmontools 或自定义）
     * 采集到后填：out->temp_celsius / reallocated / pending / power_on_hours
     */

    /* 3) 综合健康：只读 或 重映射/待映射超阈值 → 不健康 */
    out->ok = (!out->read_only
               && out->reallocated < 64
               && out->pending == 0) ? 1 : 0;
    return 0;
}
