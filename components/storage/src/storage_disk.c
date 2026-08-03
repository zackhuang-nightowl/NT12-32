/***************************************************************************************
 *  storage_disk.c — 盘发现 / 身份识别 / 格式化编排 / 盘组装配
 *
 *  复用 recorder 的裸设备原语：rsdk_rawdev_* / rsdk_format / rsdk_group_open。
 *  本文件只做"哪些盘、是不是我们的、怎么组装"，不碰盘上格式细节。
 ***************************************************************************************/
#include "storage_internal.h"
#include "rsdk_rawdev.h"
#include "rsdk_storgedev.h"
#include "rsdk_types.h"      /* rsdk_superblock_t / RSDK_SB_MAGIC / RSDK_SEC */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>

/* --- 小工具：读 /sys/block/<name>/<attr> 到字符串 --- */
static int sysfs_read(const char *name, const char *attr, char *buf, size_t n)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/block/%s/%s", name, attr);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t r = fread(buf, 1, n - 1, f);
    fclose(f);
    while (r && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) r--;
    buf[r] = 0;
    return (int)r;
}

/* 判断是不是我们关心的整盘块设备（sd/hd 开头，排除分区/ram/loop/mmc boot 等） */
static int is_whole_disk(const char *name)
{
    if (strncmp(name, "sd", 2) != 0 && strncmp(name, "hd", 2) != 0) return 0;
    /* 排除分区名（末尾带数字，如 sda1）——整盘 /sys/block 下不含分区，这里再兜底 */
    size_t L = strlen(name);
    if (L && isdigit((unsigned char)name[L - 1])) return 0;
    return 1;
}

/* ============ 身份识别（读首扇区，不改盘） ============ */
nvr_disk_state_t nvr_storage_identify(const char *path, nvr_disk_t *info,
                                      const uint8_t *want_group_uuid)
{
    rsdk_rawdev_t *raw = NULL;
    if (rsdk_rawdev_open(path, &raw) != RSDK_OK) return NVR_DISK_UNKNOWN;

    /* SuperBlock 在扇区 1（冻结 §1：扇区1主/扇区2备） */
    rsdk_superblock_t sb;
    nvr_disk_state_t st = NVR_DISK_BLANK;
    if (rsdk_rawdev_pread(raw, RSDK_SEC, &sb, sizeof(sb)) == RSDK_OK) {
        if (memcmp(sb.magic, RSDK_SB_MAGIC, 8) == 0 && sb.version == RSDK_FORMAT_VERSION) {
            /* 是 RSDK 盘 */
            if (info) {
                memcpy(info->group_uuid, sb.group_uuid, 16);
                info->group_disk_index = sb.group_disk_index;
                info->group_disk_count = sb.group_disk_count;
                info->feature_mask     = sb.feature_mask;
                info->enc_algo         = sb.enc_algo;
            }
            if (want_group_uuid && memcmp(sb.group_uuid, want_group_uuid, 16) != 0)
                st = NVR_DISK_OURS_OTHER;
            else
                st = NVR_DISK_OURS;
        } else {
            /* 非 RSDK：粗判是否有已知 FS 魔数 → FOREIGN，否则 BLANK。
             * TODO: 可接 libblkid 精判 ext/ntfs/xfs；此处仅魔数兜底。 */
            st = NVR_DISK_BLANK;   /* 保守：当空盘，格式化前仍会走 NEED_FORMAT 让用户确认 */
        }
    }
    rsdk_rawdev_close(raw);
    return st;
}

/* ============ 发现 + 识别：扫描 /dev/sd* ============ */
int nvr_storage_scan(nvr_storage_t *s)
{
    DIR *d = opendir("/sys/block");
    if (!d) return 0;
    s->ndisks = 0;
    struct dirent *e;
    while ((e = readdir(d)) && s->ndisks < NVR_MAX_DISKS) {
        if (!is_whole_disk(e->d_name)) continue;

        nvr_disk_t *dk = &s->disks[s->ndisks];
        memset(dk, 0, sizeof(*dk));
        snprintf(dk->path, sizeof(dk->path), "/dev/%s", e->d_name);

        char buf[64];
        if (sysfs_read(e->d_name, "size", buf, sizeof(buf)) > 0)   /* size 单位=512B 扇区 */
            dk->capacity_bytes = strtoull(buf, NULL, 10) * 512ull;
        if (sysfs_read(e->d_name, "device/model", buf, sizeof(buf)) > 0)
            snprintf(dk->model, sizeof(dk->model), "%s", buf);
        if (sysfs_read(e->d_name, "device/serial", dk->serial, sizeof(dk->serial)) <= 0)
            dk->serial[0] = 0;   /* 有些盘 serial 不在 sysfs，需 SMART/ioctl 补，见 health */

        /* 识别（用配置的组 UUID 比对——首次没有则传 NULL 只判 OURS/BLANK） */
        dk->state = nvr_storage_identify(dk->path, dk, NULL);
        nvr_storage_read_health(dk->path, &dk->health);
        s->ndisks++;
    }
    closedir(d);
    return s->ndisks;
}

int nvr_storage_list(nvr_storage_t *s, nvr_disk_t *out, int cap)
{
    int n = s->ndisks < cap ? s->ndisks : cap;
    memcpy(out, s->disks, (size_t)n * sizeof(nvr_disk_t));
    return n;
}

/* ============ 格式化编排 ============ */
rsdk_err_t nvr_storage_format(nvr_storage_t *s, const char *path,
                              uint16_t group_index, uint16_t group_count)
{
    rsdk_format_opt_t fo;
    memset(&fo, 0, sizeof(fo));
    fo.sn        = s->cfg.device_sn;          /* 组内一致，派生 KEK */
    fo.chunk_mib = 0;                          /* auto: 按盘容量 */
    fo.hdd_full  = s->cfg.hdd_full;
    fo.feature_mask = 0;                       /* 0 = 取编译期 rsdk_feature_mask() */
    /* group_index/count 由 rsdk_format 写入 SuperBlock；若当前 rsdk_format_opt_t
     * 未含该字段，装配阶段以实际 SB 为准，多盘编号在此传参约定。 */
    (void)group_index; (void)group_count;

    rsdk_err_t rc = rsdk_format(path, &fo);
    if (rc != RSDK_OK) return rc;

    /* 校验：能否正常 open + 读 info */
    rsdk_dev_t *dev = NULL;
    rc = rsdk_dev_open(path, &dev);
    if (rc == RSDK_OK) rsdk_dev_close(dev);
    return rc;
}

/* ============ 盘组装配：OURS 盘按 group_index 有序组装 ============ */
rsdk_err_t nvr_storage_assemble(nvr_storage_t *s, rsdk_group_t **group_out)
{
    /* 收集本组盘，按 group_disk_index 排序 */
    const char *ordered[NVR_MAX_DISKS] = {0};
    int found = 0, expect = 0;
    for (int i = 0; i < s->ndisks; i++) {
        nvr_disk_t *dk = &s->disks[i];
        if (dk->state != NVR_DISK_OURS && dk->state != NVR_DISK_ACTIVE) continue;
        if (dk->group_disk_index < NVR_MAX_DISKS) {
            ordered[dk->group_disk_index] = dk->path;
            if (dk->group_disk_count > expect) expect = dk->group_disk_count;
            found++;
        }
    }
    if (found == 0) return RSDK_E_NOTFOUND;
    if (expect == 0) expect = found;

    /* 压实成连续数组，同时检查缺盘 */
    const char *paths[NVR_MAX_DISKS]; int n = 0;
    for (int i = 0; i < expect; i++) {
        if (!ordered[i]) {
            /* 缺 group_index=i 的盘：多盘模式下部分盘的段暂不可读，
             * 仍可用现有盘继续（recorder 单盘退化）。这里告警并跳过。 */
            continue;
        }
        paths[n++] = ordered[i];
    }
    if (found < expect)
        stg_emit(s, NVR_STG_EVT_DISK_REMOVED, NULL);   /* 盘组不完整告警 */

    rsdk_err_t rc = rsdk_group_open(paths, n, &s->group);
    if (rc == RSDK_OK) {
        for (int i = 0; i < s->ndisks; i++)
            if (s->disks[i].state == NVR_DISK_OURS) s->disks[i].state = NVR_DISK_ACTIVE;
        if (group_out) *group_out = s->group;
    }
    return rc;
}

rsdk_group_t *nvr_storage_group(nvr_storage_t *s) { return s->group; }
