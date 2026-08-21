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
        /* 版本兼容: 1..RSDK_FORMAT_VERSION 均识别为本系统盘(v1 只读兼容; 更高未知版本不认领, 避免误装配)。 */
        if (memcmp(sb.magic, RSDK_SB_MAGIC, 8) == 0 &&
            sb.version >= 1u && sb.version <= RSDK_FORMAT_VERSION) {
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
    /* ★池化:把**全部**本系统盘(OURS/ACTIVE)组成一个多盘负载均衡组。
     * 旧实现按 group_disk_index 去重——而每块盘各自单独格式化后 index 都是 0,会塌成一块盘,
     * 多盘均衡实际失效(对标行业"所有通道共享全部 HDD" + 分盘摊带宽)。这里改为收全部盘。
     * 按 path 稳定排序:组内数组下标确定;回放时段按下标定位盘(rsdk_group_query_stream 会重写下标)。 */
    const char *paths[NVR_MAX_DISKS]; int n = 0;
    for (int i = 0; i < s->ndisks && n < NVR_MAX_DISKS; i++) {
        nvr_disk_t *dk = &s->disks[i];
        if (dk->state == NVR_DISK_OURS || dk->state == NVR_DISK_ACTIVE)
            paths[n++] = dk->path;
    }
    if (n == 0) return RSDK_E_NOTFOUND;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (strcmp(paths[b], paths[a]) < 0) { const char *t = paths[a]; paths[a] = paths[b]; paths[b] = t; }

    rsdk_err_t rc = rsdk_group_open(paths, n, &s->group);
    if (rc == RSDK_OK) {
        for (int i = 0; i < s->ndisks; i++)
            if (s->disks[i].state == NVR_DISK_OURS) s->disks[i].state = NVR_DISK_ACTIVE;
        if (group_out) *group_out = s->group;
    }
    return rc;
}

/* 热插拔:把已扫描到、尚未入组的本系统盘(OURS)原地并入现有盘组(group 指针不变)。返回新并入盘数。 */
int nvr_storage_integrate(nvr_storage_t *s, rsdk_group_t *group)
{
    if (!s || !group) return 0;
    int added = 0;
    for (int i = 0; i < s->ndisks; i++) {
        nvr_disk_t *dk = &s->disks[i];
        if (dk->state != NVR_DISK_OURS && dk->state != NVR_DISK_ACTIVE) continue;
        if (rsdk_group_find_path(group, dk->path) >= 0) continue;      /* 已在组 */
        if (rsdk_group_add_disk(group, dk->path) == RSDK_OK) {
            dk->state = NVR_DISK_ACTIVE; added++;
        }
    }
    return added;
}

/* 掉盘:把该 path 对应盘在组内标记不健康(balance 选盘跳过),并置状态 OFFLINE。指针不变。 */
void nvr_storage_disk_offline(nvr_storage_t *s, rsdk_group_t *group, const char *path)
{
    if (!group || !path) return;
    int idx = rsdk_group_find_path(group, path);
    if (idx >= 0) rsdk_group_set_health(group, idx, 0);
    if (s) for (int i = 0; i < s->ndisks; i++)
        if (strcmp(s->disks[i].path, path) == 0) s->disks[i].state = NVR_DISK_OFFLINE;
}

rsdk_group_t *nvr_storage_group(nvr_storage_t *s) { return s->group; }
