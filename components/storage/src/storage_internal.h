/* storage_internal.h — storage 模块内部共享定义（不对外） */
#ifndef NVR_STORAGE_INTERNAL_H
#define NVR_STORAGE_INTERNAL_H

#include "nvr_storage.h"

#define NVR_MAX_DISKS 8

struct nvr_storage {
    nvr_storage_cfg_t cfg;
    nvr_disk_t        disks[NVR_MAX_DISKS];
    int               ndisks;
    rsdk_group_t     *group;          /* 装配后的盘组 */
    int               hotplug_fd;     /* netlink uevent socket, -1=未开 */
};

/* 子模块内部入口 */
int  stg_hotplug_open (struct nvr_storage *s);   /* storage_hotplug.c */
void stg_hotplug_poll (struct nvr_storage *s);
void stg_hotplug_close(struct nvr_storage *s);
void stg_emit(struct nvr_storage *s, nvr_stg_evt_t e, const nvr_disk_t *d); /* storage_mgr.c */

#endif
