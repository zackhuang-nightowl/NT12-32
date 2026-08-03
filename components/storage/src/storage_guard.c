/***************************************************************************************
 *  storage_guard.c — 防 OS 误挂载裸盘（设计 §4）
 *
 *  裸盘无 FS，但内核/udev 可能自动挂载/fsck/探测。本模块：
 *    1) 运行期扫 /proc/mounts，发现录像盘被挂载则告警（可选自动 umount）。
 *    2) 提供 udev 规则模板（部署期投放）见 nvr-rawdisk.rules。
 ***************************************************************************************/
#include "storage_internal.h"
#include <stdio.h>
#include <string.h>

int nvr_storage_guard_check(nvr_storage_t *s)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char line[512];
    int mounted = 0;
    while (fgets(line, sizeof(line), f)) {
        /* /proc/mounts 行首是设备节点，如 "/dev/sda /mnt/x ext4 ..." */
        for (int i = 0; i < s->ndisks; i++) {
            /* 录像盘整盘或其分区被挂载都算异常 */
            if (strncmp(line, s->disks[i].path, strlen(s->disks[i].path)) == 0) {
                mounted++;
                /* 告警：录像盘不应被文件系统挂载 */
                stg_emit(s, NVR_STG_EVT_DISK_FAILED, &s->disks[i]);
                /* TODO(可选): umount(mountpoint) 强制卸载后再纳入 */
            }
        }
    }
    fclose(f);
    return mounted;
}
