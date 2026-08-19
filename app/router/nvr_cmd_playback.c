/***************************************************************************************
 *  nvr_cmd_playback.c — 本机回放控制 / 音频 / USB 备份 / 能力。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_playback.h"
#include "nvr_gui_config.h"
#include "nvr_rtsp_live.h"
#include "rsdk_backup.h"
#include "rsdk_types.h"
#include "nvr_log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <dirent.h>       /* GUI_getFileList: 列存储器根目录文件 */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#if defined(_WIN32)
#include <direct.h>
#define nvr_mkdir(p) _mkdir(p)
#else
#define nvr_mkdir(p) mkdir((p), 0755)
#endif

/* ============================ playbackControl / Mode / Caps ============================ */

char *cmd_GUI_playbackControl(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->pb) return nvr_resp_err("no_playback");
    const char *action = nvr_jstr(a, "action", NULL);
    if (!action) return nvr_resp_err("invalid_param");

    int ch1        = nvr_jint(a, "channel", 0);
    uint32_t start = (uint32_t)nvr_jint(a, "startTime", 0);
    const char *speed = nvr_jstr(a, "speed", NULL);
    const char *dir   = nvr_jstr(a, "direction", NULL);

    if (nvr_playback_control(c->pb, action, ch1, start, speed, dir) != 0)
        return nvr_resp_err("invalid_param");

    char status[16], sp[16], d[16], notify[160];
    uint32_t cur = 0;
    notify[0] = 0;
    nvr_playback_get_status(c->pb, status, sp, d, &cur, notify, (int)sizeof(notify));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "speed", sp);
    cJSON_AddStringToObject(o, "direction", d);
    cJSON_AddNumberToObject(o, "timestamp", cur);
    if (notify[0]) cJSON_AddStringToObject(o, "PlaybackMsgNotify", notify);
    return nvr_resp_content(o);
}

char *cmd_GUI_setPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->pb) return nvr_resp_err("no_playback");
    int dm = nvr_jint(a, "displayMode", 1);
    int list[NVR_PB_MAX_CELLS], n = 0;
    cJSON *chs = a ? cJSON_GetObjectItem(a, "channels") : NULL, *it;
    if (cJSON_IsArray(chs)) cJSON_ArrayForEach(it, chs) {
        if (n < NVR_PB_MAX_CELLS && cJSON_IsNumber(it)) list[n++] = (int)cJSON_GetNumberValue(it);
    }
    if (c->pv) nvr_preview_set_mode(c->pv, 0, 0);
    nvr_playback_set_mode(c->pb, dm, list, n);
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "result", "OK");
    return nvr_resp_content(o);
}

char *cmd_GUI_getPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; if (!c->pb) return nvr_resp_err("no_playback");
    int list[NVR_PB_MAX_CELLS], n = 0;
    int dm = nvr_playback_get_mode(c->pb, list, NVR_PB_MAX_CELLS, &n);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "displayMode", dm);
    cJSON *arr = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(list[i]));
    return nvr_resp_content(o);
}

/* getPlaybackCapabilities:只报本机实际有的协议/码流/已接相机通道。回放不支持 HLS。 */
char *cmd_getPlaybackCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    if (nvr_rtsp_live_port() > 0)
        cJSON_AddItemToArray(proto, cJSON_CreateString("rtsp-iotc-tunnel"));

    cJSON *st = cJSON_AddArrayToObject(o, "streamType");
    cJSON_AddItemToArray(st, cJSON_CreateString("video"));
    cJSON_AddItemToArray(st, cJSON_CreateString("subVideo"));
    cJSON_AddItemToArray(st, cJSON_CreateString("audio"));
    cJSON_AddItemToArray(st, cJSON_CreateString("audioAndVideo"));
    cJSON_AddItemToArray(st, cJSON_CreateString("audioAndSubVideo"));

    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    if (c->cm) {
        nvr_channel_t list[32];
        int n = nvr_chan_list(c->cm, list, 32);
        for (int i = 0; i < n; i++) {
            if (nvr_chan_status_code_of(c->cm, list[i].chn) == 0) continue;
            cJSON_AddItemToArray(chs, cJSON_CreateNumber(list[i].chn + 1));
        }
    }
    return nvr_resp_content(o);
}

/* ============================ PlaybackAudio ============================ */

char *cmd_GUI_getPlaybackAudio(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    if (!c->pb) return nvr_resp_err("no_playback");
    int en[NVR_PB_MAX_CELLS];
    int n = nvr_playback_get_audio(c->pb, en, NVR_PB_MAX_CELLS);
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "enable");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(en[i] ? 1 : 0));
    return nvr_resp_content(o);
}

char *cmd_GUI_setPlaybackAudio(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->pb) return nvr_resp_err("no_playback");
    cJSON *en = a ? cJSON_GetObjectItem(a, "enable") : NULL;
    if (!cJSON_IsArray(en)) return nvr_resp_err("invalid_param");
    int vals[NVR_PB_MAX_CELLS], n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, en) {
        if (n >= NVR_PB_MAX_CELLS) break;
        if (cJSON_IsNumber(it)) vals[n++] = ((int)cJSON_GetNumberValue(it) != 0) ? 1 : 0;
        else if (cJSON_IsBool(it)) vals[n++] = cJSON_IsTrue(it) ? 1 : 0;
        else vals[n++] = 0;
    }
    nvr_playback_set_audio(c->pb, vals, n);
    return nvr_resp_ok();
}

/* ============================ USB Channel Backup ============================ */

typedef struct {
    int      startTime, endTime;
    char     tags[64];
} pb_bak_slice_t;

typedef struct {
    volatile int running;
    volatile int abort;
    volatile int percent;       /* -1 error, 0..100 */
    char         info[128];
    pthread_t    th;
    int          th_ok;
    rsdk_group_t *group;
    int          chn0;
    int          stream;        /* 0 main / 1 sub */
    char         storage[16];
    char         out_dir[256];
    pb_bak_slice_t *slices;
    int          nslices;
    char         dates[16][12];
    int          ndates;
    uint32_t     t0, t1;
} pb_bak_job_t;

static pb_bak_job_t g_bak;

static int bak_resolve_dir(const char *storage, char *out, int cap)
{
    /* 设备约定:/mnt/usb 第一块,/mnt/usb2 第二块;sdcard → /mnt/sdcard */
    const char *base = "/mnt/usb";
    if (storage) {
        if (!strcmp(storage, "usb2")) base = "/mnt/usb2";
        else if (!strcmp(storage, "sdcard")) base = "/mnt/sdcard";
        else if (!strcmp(storage, "hdd") || !strcmp(storage, "hdd2"))
            return -1; /* 裸盘不可直接写文件 */
    }
    snprintf(out, (size_t)cap, "%s", base);
    struct stat st;
    if (stat(out, &st) != 0 || !S_ISDIR(st.st_mode)) return -2;
    return 0;
}

static void *bak_thread(void *arg)
{
    pb_bak_job_t *j = (pb_bak_job_t *)arg;
    j->percent = 0; j->info[0] = 0;
    if (!j->group || j->nslices <= 0) {
        j->percent = -1; snprintf(j->info, sizeof(j->info), "No backup list");
        j->running = 0; return NULL;
    }
    rsdk_export_opt_t opt; memset(&opt, 0, sizeof(opt));
    opt.fmt = RSDK_EXPORT_MP4;

    for (int i = 0; i < j->nslices && !j->abort; i++) {
        pb_bak_slice_t *s = &j->slices[i];
        uint32_t t0 = (uint32_t)s->startTime;
        uint32_t t1 = s->endTime > s->startTime ? (uint32_t)s->endTime : t0 + 30;
        char path[320];
        snprintf(path, sizeof(path), "%s/ch%d_%u_%u.mp4", j->out_dir, j->chn0 + 1, t0, t1);
        NVR_LOGI("bak", "export %s", path);
        rsdk_err_t rc = rsdk_backup_export(j->group, t0, t1, j->chn0, &opt, path);
        if (j->abort) break;
        if (rc != RSDK_OK) {
            j->percent = -1;
            if (rc == RSDK_E_NOSPACE)
                snprintf(j->info, sizeof(j->info), "Not enough space for backup");
            else if (rc == RSDK_E_IO)
                snprintf(j->info, sizeof(j->info), "Lost USB Device error");
            else
                snprintf(j->info, sizeof(j->info), "Backup failed (%d)", (int)rc);
            j->running = 0; return NULL;
        }
        j->percent = ((i + 1) * 100) / j->nslices;
    }
    if (j->abort) {
        j->percent = -1;
        snprintf(j->info, sizeof(j->info), "Abort By User");
    } else {
        j->percent = 100; j->info[0] = 0;
    }
    j->running = 0;
    return NULL;
}

char *cmd_GUI_ChannelBackupFiles(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->group) return nvr_resp_err("no_storage");
    if (g_bak.running) return nvr_resp_err("backup_busy");

    int ch1 = nvr_jint(a, "channel", 0);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    const char *stype = nvr_jstr(a, "streamType", "main");
    const char *storage = nvr_jstr(a, "storage", "usb");
    cJSON *list = a ? cJSON_GetObjectItem(a, "list") : NULL;
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) <= 0) return nvr_resp_err("invalid_param");

    char dir[256];
    int dr = bak_resolve_dir(storage, dir, (int)sizeof(dir));
    if (dr == -1) return nvr_resp_err("invalid_storage");
    if (dr == -2) {
        /* 无挂载点时仍接受任务,线程里会因写失败报 Lost USB */
        snprintf(dir, sizeof(dir), "/tmp/nvr_backup");
        nvr_mkdir(dir);
        NVR_LOGW("bak", "storage %s 未挂载,回落 %s", storage ? storage : "usb", dir);
    }

    if (g_bak.th_ok) { pthread_join(g_bak.th, NULL); g_bak.th_ok = 0; }
    free(g_bak.slices); g_bak.slices = NULL;

    int ns = cJSON_GetArraySize(list);
    pb_bak_slice_t *sl = (pb_bak_slice_t *)calloc((size_t)ns, sizeof(*sl));
    if (!sl) return nvr_resp_err("oom");
    for (int i = 0; i < ns; i++) {
        cJSON *it = cJSON_GetArrayItem(list, i);
        sl[i].startTime = nvr_jint(it, "startTime", 0);
        sl[i].endTime   = nvr_jint(it, "endTime", sl[i].startTime + 30);
        cJSON *tags = cJSON_GetObjectItem(it, "tags");
        if (cJSON_IsArray(tags) && cJSON_GetArraySize(tags) > 0) {
            cJSON *t0 = cJSON_GetArrayItem(tags, 0);
            if (cJSON_IsString(t0)) snprintf(sl[i].tags, sizeof(sl[i].tags), "%s", t0->valuestring);
        }
    }

    memset(&g_bak, 0, sizeof(g_bak));
    g_bak.group = c->group;
    g_bak.chn0 = ch1 - 1;
    g_bak.stream = (stype && strcmp(stype, "sub") == 0) ? 1 : 0;
    snprintf(g_bak.storage, sizeof(g_bak.storage), "%s", storage ? storage : "usb");
    snprintf(g_bak.out_dir, sizeof(g_bak.out_dir), "%s", dir);
    g_bak.slices = sl; g_bak.nslices = ns;
    g_bak.ndates = 0; g_bak.t0 = 0; g_bak.t1 = 0;
    if (ns > 0) {
        g_bak.t0 = (uint32_t)sl[0].startTime;
        g_bak.t1 = (uint32_t)sl[ns - 1].endTime;
    }
    g_bak.running = 1; g_bak.percent = 0; g_bak.abort = 0;
    if (pthread_create(&g_bak.th, NULL, bak_thread, &g_bak) != 0) {
        free(sl); g_bak.slices = NULL; g_bak.running = 0;
        return nvr_resp_err("thread_failed");
    }
    g_bak.th_ok = 1;
    return nvr_resp_ok();
}

char *cmd_GUI_GetChannelBackupStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "percent", g_bak.percent);
    if (g_bak.info[0]) cJSON_AddStringToObject(o, "info", g_bak.info);
    return nvr_resp_content(o);
}

static uint32_t bak_ymd_epoch(const char *ymd, int end_of_day)
{
    int y = 0, mo = 0, d = 0;
    struct tm tm;
    if (!ymd || strlen(ymd) != 8 || sscanf(ymd, "%4d%2d%2d", &y, &mo, &d) != 3)
        return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    if (end_of_day) { tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59; }
    tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}

/* App:按通道+日期备份到 USB。与 GUI_ChannelBackupFiles 共用 g_bak。 */
char *cmd_startRecordingBackup(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->group) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR");
        return nvr_resp_content(o);
    }
    if (g_bak.running) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR_BUSY");
        return nvr_resp_content(o);
    }
    int ch1 = nvr_jint(a, "channel", 0);
    cJSON *dates = a ? cJSON_GetObjectItem(a, "date") : NULL;
    if (ch1 < 1 || !cJSON_IsArray(dates) || cJSON_GetArraySize(dates) <= 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR");
        return nvr_resp_content(o);
    }
    char dir[256];
    int dr = bak_resolve_dir("usb", dir, (int)sizeof(dir));
    if (dr == -1) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR_FILESYS");
        return nvr_resp_content(o);
    }
    if (dr == -2) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR_NO_DISK");
        return nvr_resp_content(o);
    }

    int ns = cJSON_GetArraySize(dates);
    if (ns > 16) ns = 16;
    pb_bak_slice_t *sl = (pb_bak_slice_t *)calloc((size_t)ns, sizeof(*sl));
    if (!sl) return nvr_resp_err("oom");
    if (g_bak.th_ok) { pthread_join(g_bak.th, NULL); g_bak.th_ok = 0; }
    free(g_bak.slices); g_bak.slices = NULL;
    memset(&g_bak, 0, sizeof(g_bak));
    g_bak.ndates = 0;
    for (int i = 0; i < ns; i++) {
        cJSON *it = cJSON_GetArrayItem(dates, i);
        const char *ymd = cJSON_IsString(it) ? it->valuestring : NULL;
        uint32_t t0 = bak_ymd_epoch(ymd, 0);
        uint32_t t1 = bak_ymd_epoch(ymd, 1);
        if (!t0 || !t1) { free(sl); return nvr_resp_err("invalid_param"); }
        sl[i].startTime = (int)t0;
        sl[i].endTime = (int)t1;
        if (ymd && g_bak.ndates < 16) {
            snprintf(g_bak.dates[g_bak.ndates], sizeof(g_bak.dates[0]), "%s", ymd);
            g_bak.ndates++;
        }
    }
    g_bak.group = c->group;
    g_bak.chn0 = ch1 - 1;
    g_bak.stream = 0;
    snprintf(g_bak.storage, sizeof(g_bak.storage), "usb");
    snprintf(g_bak.out_dir, sizeof(g_bak.out_dir), "%s", dir);
    g_bak.slices = sl; g_bak.nslices = ns;
    g_bak.t0 = (uint32_t)sl[0].startTime;
    g_bak.t1 = (uint32_t)sl[ns - 1].endTime;
    g_bak.running = 1; g_bak.percent = 0; g_bak.abort = 0;
    if (pthread_create(&g_bak.th, NULL, bak_thread, &g_bak) != 0) {
        free(sl); g_bak.slices = NULL; g_bak.running = 0;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "err", "ERR");
        return nvr_resp_content(o);
    }
    g_bak.th_ok = 1;
    return nvr_resp_ok();
}

char *cmd_getRecordingBackupProgress(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "channel", g_bak.chn0 + 1);
    int pct = g_bak.percent;
    if (pct < 0) pct = 0;
    cJSON_AddNumberToObject(o, "percentage", pct);
    cJSON *arr = cJSON_AddArrayToObject(o, "date");
    for (int i = 0; i < g_bak.ndates; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateString(g_bak.dates[i]));
    cJSON_AddNumberToObject(o, "startTime", (double)g_bak.t0);
    cJSON_AddNumberToObject(o, "endTime", g_bak.running ? -1 : (double)g_bak.t1);
    char usb[256];
    if (!g_bak.th_ok && !g_bak.running && g_bak.nslices <= 0)
        cJSON_AddStringToObject(o, "err", "ERR_NO_TASK");
    else if (bak_resolve_dir("usb", usb, (int)sizeof(usb)) == -2)
        cJSON_AddStringToObject(o, "err", "ERR_NO_DISK");
    else if (g_bak.percent < 0)
        cJSON_AddStringToObject(o, "err", "ERR");
    return nvr_resp_content(o);
}

char *cmd_GUI_StopChannelBackup(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    if (g_bak.running) {
        g_bak.abort = 1;
        if (g_bak.th_ok) { pthread_join(g_bak.th, NULL); g_bak.th_ok = 0; }
        g_bak.running = 0;
        g_bak.percent = -1;
        snprintf(g_bak.info, sizeof(g_bak.info), "Abort By User");
    }
    return nvr_resp_ok();
}

/* ============================ 其它暂回落 / 桩 ============================ */

/* GUI_getFileList:列指定存储器**根目录**下的文件名(FW 升级前找 upgradeFile_*.bin 等)。
 * 见 nop_api_doc/System/GUI_getFileList.txt:args.storage(默认 usb;usb/usb2/sdcard/hdd/hdd2),
 * 返回 content.FileList=[文件名...]。usb/usb2/sdcard 有文件系统可列;hdd/hdd2 是裸盘(直写录像,
 * 无 fs)→ 空表。只列根目录、只列普通文件(不含子目录/隐藏项)。 */
char *cmd_GUI_getFileList(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    const char *storage = nvr_jstr(a, "storage", "usb");
    char dir[64];
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "FileList");
    if (bak_resolve_dir(storage, dir, sizeof(dir)) == 0) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] == '.') continue;              /* 跳过 . .. 及隐藏 */
                char full[320];
                snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
                struct stat st;
                if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) /* 仅根目录下的普通文件 */
                    cJSON_AddItemToArray(arr, cJSON_CreateString(e->d_name));
            }
            closedir(d);
            NVR_LOGI("file", "getFileList storage=%s dir=%s -> %d 文件",
                     storage, dir, cJSON_GetArraySize(arr));
        } else {
            NVR_LOGW("file", "getFileList: %s(%s) 打不开——未挂载?", dir, storage);
        }
    } else {
        NVR_LOGW("file", "getFileList: storage=%s 无可列文件系统(裸盘/未挂载)", storage);
    }
    return nvr_resp_content(o);   /* {statusCode:200, content:{FileList:[...]}} */
}
