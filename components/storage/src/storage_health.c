/***************************************************************************************
 *  storage_health.c — 盘健康：sysfs 只读 + rsdk SMART（ATA passthrough）
 ***************************************************************************************/
#include "nvr_storage.h"
#include "rsdk_disk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    out->temp_celsius = -1;
    if (!path || !path[0]) return -1;

    const char *name = base_name(path);
    unsigned long ro = 0;
    if (sysfs_read_u(name, "ro", &ro) == 0)
        out->read_only = (ro != 0);

    rsdk_smart_t sm;
    if (rsdk_smart_read(path, &sm) == RSDK_OK) {
        if (sm.temp_c >= 0) out->temp_celsius = sm.temp_c;
        out->reallocated    = (uint32_t)sm.reallocated;
        out->pending        = (uint32_t)sm.pending;
        out->power_on_hours = (uint32_t)sm.power_on_hours;
        out->ok = rsdk_smart_ok(&sm) && !out->read_only;
    } else {
        out->ok = !out->read_only;
    }
    return 0;
}
