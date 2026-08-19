/***************************************************************************************
 *  nvr_cmd_storage.c — storage 域 handler:盘信息/格式化/健康/当前存储介质。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_log.h"
#include "rsdk_disk.h"
#include "nvr_streaming.h"  /* nvr_stream_mgr_set_group:格式化后补开 writer */
#include "nvr_storage.h"     /* nvr_storage_scan/assemble */
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>     /* getStorageInfo: 可移动盘(USB/SD)容量 */

/* 挂载点判定:/proc/mounts 里有该路径为挂载点则返回 1(区分"已挂 USB"与 rootfs 空目录)。 */
static int stg_is_mountpoint(const char *path)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char line[512], dev[160], mp[256]; int found = 0;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "%159s %255s", dev, mp) == 2 && strcmp(mp, path) == 0) { found = 1; break; }
    fclose(f);
    return found;
}

/* 追加可移动盘(USB/SD)到 storage list:约定挂载点 /mnt/usb、/mnt/usb2、/mnt/sdcard
 * (见 lib/udev/usb_auto_mount.sh)。GUI 升级/备份靠此发现 U 盘,再调 GUI_getFileList 列文件。 */
static void stg_add_removable(cJSON *arr)
{
    static const struct { const char *name, *path; } rm[] = {
        { "usb",    "/mnt/usb"    },
        { "usb2",   "/mnt/usb2"   },
        { "sdcard", "/mnt/sdcard" },
    };
    for (unsigned i = 0; i < sizeof(rm) / sizeof(rm[0]); i++) {
        struct statvfs vf;
        if (!stg_is_mountpoint(rm[i].path)) continue;
        if (statvfs(rm[i].path, &vf) != 0 || vf.f_blocks == 0) continue;
        /* 去重:RSDK 盘扫描(开机 USB 已插)可能已列同名但只看到裸块(freeSize 0)。
         * 已挂载文件系统的 statvfs 才是权威 → 删掉旧同名,用挂载值重加一条。 */
        int idx = 0; cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            cJSON *nm = cJSON_GetObjectItem(it, "name");
            if (nm && cJSON_IsString(nm) && strcmp(nm->valuestring, rm[i].name) == 0) {
                cJSON_DeleteItemFromArray(arr, idx); break;
            }
            idx++;
        }
        unsigned long bs = vf.f_frsize ? vf.f_frsize : vf.f_bsize;
        double total_mb = (double)((unsigned long long)vf.f_blocks * bs / 1024 / 1024);
        double free_mb  = (double)((unsigned long long)vf.f_bavail * bs / 1024 / 1024);
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", rm[i].name);
        cJSON_AddNumberToObject(e, "totalSize", total_mb);
        cJSON_AddNumberToObject(e, "freeSize",  free_mb);
        cJSON_AddStringToObject(e, "status", "in_use");
        cJSON_AddItemToArray(arr, e);
    }
}

/* 统一名(hdd/hdd2/sdcard/usb):按类别 + 实际检测计数。seq 计数器由调用方按类持有。 */
static void stg_name_of(const char *path, int *hdd_seq, int *usb_seq, char *out, size_t n)
{
    rsdk_disk_info_t info;
    rsdk_disk_class_t cls = RSDK_DISKCLASS_HDD;
    if (rsdk_disk_identify(path, &info) == RSDK_OK) cls = info.dclass;
    int seq = 0;
    if      (cls == RSDK_DISKCLASS_SDCARD) seq = 0;
    else if (cls == RSDK_DISKCLASS_USB)    seq = (*usb_seq)++;
    else                                   seq = (*hdd_seq)++;   /* HDD/SSD/未知 */
    rsdk_disk_unified_name(path, seq, out, n);
}

/* nvr_disk_state → 接口 status(in_use 表示当前录像用盘) */
static const char *stg_status_str(nvr_disk_state_t s, int in_use)
{
    switch (s) {
        case NVR_DISK_ACTIVE:  return in_use ? "in_use" : "ready";
        case NVR_DISK_FAILED:
        case NVR_DISK_OFFLINE:  return "error";
        default:                return "ready";   /* BLANK/FOREIGN/未知:已挂未用 */
    }
}

/* 按接口 name 找对应盘(同一套 hdd/hdd2/sdcard/usb 计数规则);找到返回 1 并填 *out。 */
static int stg_find_by_name(const nvr_cmd_ctx_t *c, const char *value, nvr_disk_t *out, int *count)
{
    if (count) *count = 0;
    if (!c->stg || !value) return 0;
    nvr_disk_t d[8]; int n = nvr_storage_list(c->stg, d, 8);
    if (count) *count = n;
    int hdd_seq = 0, usb_seq = 0;
    for (int i = 0; i < n; i++) {
        char name[16]; stg_name_of(d[i].path, &hdd_seq, &usb_seq, name, sizeof(name));
        if (strcmp(name, value) == 0) { if (out) *out = d[i]; return 1; }
    }
    return 0;
}

/* getStorageInfo 与 X_NightOwl_getStorageInfo 共用 */
char *cmd_getStorageInfo(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; cJSON *o = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(o, "list");
    if (c->stg) {
        nvr_disk_t d[8]; int n = nvr_storage_list(c->stg, d, 8);
        int hdd_seq = 0, usb_seq = 0;
        for (int i = 0; i < n; i++) {
            char name[16]; stg_name_of(d[i].path, &hdd_seq, &usb_seq, name, sizeof(name));
            /* 容量(每盘各自):已格式化用 RSDK 数据环总/空闲;未格式化用物理容量、空闲 0 */
            rsdk_disk_status_t st;
            uint64_t total_mb = d[i].capacity_bytes / 1024 / 1024, free_mb = 0;
            if (rsdk_disk_probe(d[i].path, &st) == RSDK_OK && st.formatted) {
                total_mb = st.data_bytes / 1024 / 1024;
                free_mb  = st.free_bytes / 1024 / 1024;
            }
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "name", name);
            cJSON_AddNumberToObject(e, "totalSize", (double)total_mb);
            cJSON_AddNumberToObject(e, "freeSize",  (double)free_mb);
            cJSON_AddStringToObject(e, "status", stg_status_str(d[i].state, i == 0));
            cJSON_AddItemToArray(arr, e);
        }
    }
    stg_add_removable(arr);   /* 追加已挂载的 USB/SD(供 GUI 升级/备份发现可移动盘) */
    return nvr_resp_content(o);
}
char *cmd_formatStorage(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->stg) return nvr_resp_err("no_storage");
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value) return nvr_resp_err("invalid_param");
    nvr_disk_t disk;
    if (!stg_find_by_name(c, value, &disk, NULL)) return nvr_resp_err("no_such_storage");
    NVR_LOGW("router", "formatStorage(%s) → %s", value, disk.path);
    /* ★ 先暂停所有写盘并关闭 writer(盘静默)——否则录像 writer 正写着被格式化的盘,
     * 既冲突(格式化不干净),格式化后又因旧 writer 还开着导致 set_group 补开 0 路(录像坏)。 */
    uint32_t was = c->sm ? nvr_stream_mgr_pause_recording(c->sm) : 0;
    rsdk_err_t frc = nvr_storage_format(c->stg, disk.path, 0, 1);
    /* ★ 无论成败都要恢复写盘:重扫(盘状态转 OURS)→ 重组装盘组 → 恢复录像通道并在新组补开 writer。
     * 开机盘未格式化时 assemble 失败、group=NULL、录像禁用;此处补上,ch 立即开始写盘。 */
    nvr_storage_scan(c->stg);
    rsdk_group_t *g = NULL;
    if (c->sm && nvr_storage_assemble(c->stg, &g) == RSDK_OK && g) {
        nvr_stream_mgr_resume_recording(c->sm, g, was);
        NVR_LOGW("router", "格式化后重组装盘组成功 → 录像已恢复(免重启)");
    } else {
        NVR_LOGW("router", "盘组重组装失败(可能格式化失败/多盘缺盘/SATA异常),录像待重启");
    }
    if (frc != RSDK_OK) return nvr_resp_err("format_failed");
    return nvr_resp_ok();
}
char *cmd_getAllDisksHealth(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; cJSON *o = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(o, "list");
    if (c->stg) {
        nvr_disk_t d[8]; int n = nvr_storage_list(c->stg, d, 8);
        int hdd_seq = 0, usb_seq = 0;
        for (int i = 0; i < n; i++) {
            char name[16]; stg_name_of(d[i].path, &hdd_seq, &usb_seq, name, sizeof(name));
            rsdk_smart_t sm; rsdk_smart_read(d[i].path, &sm);
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "name", name);
            cJSON_AddStringToObject(e, "status", rsdk_smart_ok(&sm) ? "good" : "bad");
            /* smart[] 完整属性表:id|name|flags|value|worst|thresh|raw|status */
            rsdk_smart_attr_t at[RSDK_SMART_ATTR_MAX];
            int m = rsdk_smart_read_attrs(d[i].path, at, RSDK_SMART_ATTR_MAX);
            cJSON *sa = cJSON_AddArrayToObject(e, "smart");
            for (int k = 0; k < m; k++) {
                const char *ok = (at[k].thresh && at[k].value <= at[k].thresh) ? "Warning" : "OK";
                char line[160];
                snprintf(line, sizeof(line), "%d|%s|0x%04X|%d|%d|%02X|%llu|%s",
                         at[k].id, rsdk_smart_attr_name(at[k].id), at[k].flags,
                         at[k].value, at[k].worst, at[k].thresh, (unsigned long long)at[k].raw, ok);
                cJSON_AddItemToArray(sa, cJSON_CreateString(line));
            }
            cJSON_AddItemToArray(arr, e);
        }
    }
    return nvr_resp_content(o);
}
char *cmd_getCurrentStorage(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; char b[32]; nvr_settings_get_str(c->settings, "storage.current", b, sizeof(b), "");
    if (!b[0] && c->stg) {                 /* 未设 → 默认首个检测到的盘 */
        nvr_disk_t d[8]; int n = nvr_storage_list(c->stg, d, 8);
        if (n > 0) { int hs = 0, us = 0; stg_name_of(d[0].path, &hs, &us, b, sizeof(b)); }
    }
    if (!b[0]) snprintf(b, sizeof(b), "hdd");
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "value", b);
    return nvr_resp_content(o);
}
char *cmd_setCurrentStorage(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value) return nvr_resp_err("invalid_param");
    int count = 0;                         /* 有盘时校验 name 存在;无盘时容许(配置先于硬件) */
    if (!stg_find_by_name(c, value, NULL, &count) && count > 0)
        return nvr_resp_err("no_such_storage");
    nvr_settings_set_str(c->settings, "storage.current", value);
    return nvr_resp_ok();
}

char *cmd_GUI_getHddConfig(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char mode[32];
    nvr_settings_get_str(c->settings, "storage.hdd_full", mode, sizeof(mode), "overwrite");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "overWrite", strcmp(mode, "overwrite") == 0);
    return nvr_resp_content(o);
}

char *cmd_GUI_setHddConfig(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!nvr_jhas(a, "overWrite")) return nvr_resp_err("invalid_param");
    nvr_settings_set_str(c->settings, "storage.hdd_full",
                         nvr_jbool(a, "overWrite", 1) ? "overwrite" : "stop");
    return nvr_resp_ok();
}
