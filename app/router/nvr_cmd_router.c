/***************************************************************************************
 *  nvr_cmd_router.c — NVR 命令路由。见 nvr_cmd_router.h。
 ***************************************************************************************/
#include "nvr_cmd_router.h"
#include "nvr_log.h"
#include "nvr_ota.h"
#include "nvr_cmd_display.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/reboot.h>

struct nvr_cmd_router {
    nvr_cmd_router_cfg_t cfg;
    int      listen_fd;
    int      port;
    volatile int running;
    pthread_t thr;
};

/* ------------------------- 应答构造 ------------------------- */
static char *resp_status(int code, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "statusCode", code);
    cJSON_AddStringToObject(r, "statusMsg", msg ? msg : (code == 200 ? "OK" : "ERR"));
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
static char *resp_content(cJSON *content)   /* 接管 content 所有权 */
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "statusCode", 200);
    cJSON_AddStringToObject(r, "statusMsg", "OK");
    cJSON_AddItemToObject(r, "content", content ? content : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
static const char *jstr(const cJSON *o, const char *k, const char *d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return (v && cJSON_IsString(v)) ? v->valuestring : d; }
static int jint(const cJSON *o, const char *k, int d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return (v && cJSON_IsNumber(v)) ? v->valueint : d; }
static int jbool(const cJSON *o, const char *k, int d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
  if (!v) return d; if (cJSON_IsBool(v)) return cJSON_IsTrue(v); if (cJSON_IsNumber(v)) return v->valueint != 0; return d; }

/* ------------------------- 转发到设备（NOP 透传） ------------------------- */
typedef struct { char *buf; size_t len; } mem_t;
static size_t on_write(void *p, size_t sz, size_t n, void *u)
{ size_t a = sz * n; mem_t *m = u; char *nb = realloc(m->buf, m->len + a + 1); if (!nb) return 0;
  m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0; return a; }

/* 用通道 IP + 凭据把 NOP JSON 原样 POST 到设备 /APPJsonCmd（NightOwl 从机相机接口）。
 * 转发失败(无 IP / 设备不可达 / 无应答) → 统一 501。user/pass 可空(默认/空密码)。 */
static char *forward_nop(const char *ip, int port, const char *user, const char *pass,
                         const char *json_in)
{
    if (!ip || !ip[0]) return resp_status(501, "not supported (no device)");
    CURL *c = curl_easy_init();
    if (!c) return resp_status(501, "not supported");
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/APPJsonCmd", ip, port);
    mem_t m = {0};
    struct curl_slist *hdr = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_in);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    if (user && user[0]) {                       /* 相机鉴权：admin + 通道口令(增强则为 P_enh) */
        char up[160]; snprintf(up, sizeof(up), "%s:%s", user, pass ? pass : "");
        curl_easy_setopt(c, CURLOPT_USERPWD, up);
        curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_DIGEST | CURLAUTH_BASIC));
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || !m.buf) { free(m.buf); return resp_status(501, "not supported (device unreachable)"); }
    return m.buf;   /* 设备原样应答 */
}

/* ------------------------- 命令分类 ------------------------- */
/* 面向通道的相机命令（带 channel，需发到设备）——前缀/名匹配 */
static int is_channel_cmd(const char *f)
{
    if (!f) return 0;
    if (strncmp(f, "ptz", 3) == 0) return 1;
    if (strstr(f, "ChannelLight") || strstr(f, "FloodLight") || strstr(f, "AudioAlert") ||
        strstr(f, "ChannelSensorConfig") || strstr(f, "ActivityZone") || strstr(f, "PanicSwitch") ||
        strstr(f, "ChannelIndicatorLight") || strstr(f, "VideoBrightness") || strstr(f, "VideoContrast") ||
        strstr(f, "VideoOrientation") || strstr(f, "VideoResolution") || strstr(f, "PtzTrack") ||
        strstr(f, "PtzPreset")) return 1;
    if (!strcmp(f, "snapshotChannel") || !strcmp(f, "startSpeaker") || !strcmp(f, "getSpeakerCapabilities") ||
        !strcmp(f, "getProfile")) return 1;
    if (strstr(f, "upgradeChannelFirmware") || strstr(f, "ChannelUpgradeStatus")) return 1;  /* IPC 固件下推→相机 */
    return 0;
}

/* ------------------------- 本地命令：存/查设置库 ------------------------- */
/* 返回非 NULL=已本地处理；NULL=非本地命令 */
static char *handle_local(nvr_cmd_router_t *r, const char *func, cJSON *args)
{
    nvr_settings_t *s = r->cfg.settings;
    if (!func) return NULL;

    /* ---- 出图：显示指令（displayMode/映射/悬浮块/通道状态/能力）优先分流 ---- */
    {
        nvr_display_ctx_t dctx = { .pv = r->cfg.pv, .persist = r->cfg.persist, .cm = r->cfg.cm };
        char *dr = nvr_cmd_display_handle(func, args, &dctx);
        if (dr) return dr;
    }

    /* ---- 设备名 / 信息 ---- */
    if (!strcmp(func, "setName")) {
        nvr_settings_set_str(s, "system.device_name", jstr(args, "name", ""));
        NVR_LOGI("router", "setName → %s", jstr(args, "name", ""));
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "getName")) {
        char b[64]; nvr_settings_get_str(s, "system.device_name", b, sizeof(b), "NVR");
        cJSON *c = cJSON_CreateObject(); cJSON_AddStringToObject(c, "name", b); return resp_content(c);
    }
    if (!strcmp(func, "getDeviceInfo")) {
        char nm[64], sn[64], mdl[32];
        nvr_settings_get_str(s, "system.device_name", nm, sizeof(nm), "NVR");
        nvr_settings_get_str(s, "system.sn", sn, sizeof(sn), "");
        nvr_settings_get_str(s, "system.model", mdl, sizeof(mdl), "NT12-32");
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "name", nm); cJSON_AddStringToObject(c, "sn", sn);
        cJSON_AddStringToObject(c, "model", mdl);
        cJSON_AddNumberToObject(c, "channels", nvr_settings_get_int(s, "system.capacity", 32));
        return resp_content(c);
    }
    if (!strcmp(func, "X_NightOwl_setTimezone")) {
        nvr_settings_set_str(s, "system.timezone", jstr(args, "timezone", "UTC"));
        return resp_status(200, "OK");
    }

    /* ---- NOP owner / stoken ---- */
    if (!strcmp(func, "X_NightOwl_setOwner")) {
        nvr_owner_row_t ow; memset(&ow, 0, sizeof(ow));
        snprintf(ow.owner_id, sizeof(ow.owner_id), "%s", jstr(args, "owner_id", ""));
        snprintf(ow.username, sizeof(ow.username), "%s", jstr(args, "username", ""));
        snprintf(ow.stoken,   sizeof(ow.stoken),   "%s", jstr(args, "stoken", ""));
        nvr_settings_owner_set(s, &ow);
        NVR_LOGI("router", "setOwner 存储(stoken %d 字节)", (int)strlen(ow.stoken));
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "X_NightOwl_getOwner")) {
        nvr_owner_row_t ow; cJSON *c = cJSON_CreateObject();
        if (nvr_settings_owner_get(s, &ow) == 0) {
            cJSON_AddStringToObject(c, "owner_id", ow.owner_id);
            cJSON_AddStringToObject(c, "username", ow.username);
            cJSON_AddStringToObject(c, "stoken", ow.stoken);
        }
        return resp_content(c);
    }

    /* ---- 远程访问(BLE+P2P) 开关：仅本地 admin 可控；绑 NOP 账户后固定打开 ---- */
    if (!strcmp(func, "GUI_getRemoteAccessState")) {
        nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(s, &ow) == 0 && ow.owner_id[0]);
        cJSON *c = cJSON_CreateObject();
        /* enabled = 运行时手动开关值(本地 admin 态才有意义)；实际服务门控见 nvr_app */
        cJSON_AddBoolToObject(c, "enabled", nvr_settings_get_int(s, "service.remote_access", 0));
        cJSON_AddBoolToObject(c, "isBoundAws", bound);
        return resp_content(c);
    }
    if (!strcmp(func, "GUI_setRemoteAccessState")) {
        nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(s, &ow) == 0 && ow.owner_id[0]);
        cJSON *c = cJSON_CreateObject();
        if (bound) {
            cJSON_AddStringToObject(c, "result", "AlreadyBound");   /* 已绑账户，固定打开不可改 */
        } else {
            int en = jbool(args, "enable", 1);
            nvr_settings_set_int(s, "service.remote_access", en);   /* 触发订阅 → 启停 BLE+P2P */
            NVR_LOGI("router", "RemoteAccess → %d", en);
            cJSON_AddStringToObject(c, "result", "OK");
        }
        return resp_content(c);
    }

    /* ---- 云存开关 / 配置 ---- */
    if (!strcmp(func, "X_NightOwl_setCloudRecordSwitch")) {
        nvr_settings_set_int(s, "cloud.switch", jbool(args, "value", 0));
        NVR_LOGI("router", "云存开关 → %d", jbool(args, "value", 0));
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "X_NightOwl_getCloudRecordSwitch")) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddBoolToObject(c, "value", nvr_settings_get_int(s, "cloud.switch", 0));
        return resp_content(c);
    }
    if (!strcmp(func, "setCloudRecordConfigs")) {
        cJSON *chs = args ? cJSON_GetObjectItem(args, "channels") : NULL, *it;
        if (cJSON_IsArray(chs)) cJSON_ArrayForEach(it, chs) {
            nvr_cloud_ch_row_t row; memset(&row, 0, sizeof(row));
            row.chn = jint(it, "channel", -1);
            if (row.chn < 0) continue;
            snprintf(row.stream_type, sizeof(row.stream_type), "%s", jstr(it, "streamType", "main"));
            cJSON *tg = cJSON_GetObjectItem(it, "triggers"), *t; char buf[128] = {0}; int first = 1;
            if (cJSON_IsArray(tg)) cJSON_ArrayForEach(t, tg) {
                if (cJSON_IsString(t)) { if (!first) strncat(buf, ",", sizeof(buf)-strlen(buf)-1);
                    strncat(buf, t->valuestring, sizeof(buf)-strlen(buf)-1); first = 0; } }
            snprintf(row.triggers, sizeof(row.triggers), "%s", buf);
            nvr_settings_cloud_ch_upsert(s, &row);
        }
        NVR_LOGI("router", "云存配置已存");
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "getCloudRecordConfigs")) {
        nvr_cloud_ch_row_t rows[64]; int n = nvr_settings_cloud_ch_list(s, rows, 64);
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "mode", nvr_settings_get_int(s, "storage.has_disk", 1) ? "async" : "sync");
        cJSON *arr = cJSON_AddArrayToObject(c, "channels");
        for (int i = 0; i < n; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "channel", rows[i].chn);
            cJSON_AddStringToObject(o, "streamType", rows[i].stream_type);
            cJSON *ta = cJSON_AddArrayToObject(o, "triggers");
            char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", rows[i].triggers);
            for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) cJSON_AddItemToArray(ta, cJSON_CreateString(p));
            cJSON_AddItemToArray(arr, o);
        }
        return resp_content(c);
    }

    /* ---- 每通道录像触发 / 推送开关（KV 存储） ---- */
    if (!strcmp(func, "X_NightOwl_setChannelRecordingTriggers")) {
        int ch = jint(args, "channel", -1); if (ch < 0) return resp_status(400, "no channel");
        cJSON *tg = cJSON_GetObjectItem(args, "triggers"), *t; char buf[128] = {0}; int first = 1;
        if (cJSON_IsArray(tg)) cJSON_ArrayForEach(t, tg) { if (cJSON_IsString(t)) {
            if (!first) strncat(buf, ",", sizeof(buf)-strlen(buf)-1);
            strncat(buf, t->valuestring, sizeof(buf)-strlen(buf)-1); first = 0; } }
        char key[48]; snprintf(key, sizeof(key), "rec.ch%d.triggers", ch);
        nvr_settings_set_str(s, key, buf);
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "X_NightOwl_getChannelRecordingTriggers")) {
        int ch = jint(args, "channel", -1); char key[48], b[128];
        snprintf(key, sizeof(key), "rec.ch%d.triggers", ch);
        nvr_settings_get_str(s, key, b, sizeof(b), "human,face,vehicle");
        cJSON *c = cJSON_CreateObject(); cJSON *ta = cJSON_AddArrayToObject(c, "triggers");
        char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", b);
        for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) cJSON_AddItemToArray(ta, cJSON_CreateString(p));
        return resp_content(c);
    }
    if (!strcmp(func, "X_NightOwl_setChannelsPushNotificationSwitch")) {
        int ch = jint(args, "channel", -1); char key[48];
        snprintf(key, sizeof(key), "push.ch%d.switch", ch < 0 ? 0 : ch);
        nvr_settings_set_int(s, key, jbool(args, "value", 0));
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "X_NightOwl_getChannelsPushNotificationSwitch")) {
        int ch = jint(args, "channel", -1); char key[48];
        snprintf(key, sizeof(key), "push.ch%d.switch", ch < 0 ? 0 : ch);
        cJSON *c = cJSON_CreateObject();
        cJSON_AddBoolToObject(c, "value", nvr_settings_get_int(s, key, 0));
        return resp_content(c);
    }

    /* ---- 存储（接真实 storage 子系统） ---- */
    if (!strcmp(func, "X_NightOwl_getStorageInfo") || !strcmp(func, "getStorageInfo")) {
        cJSON *c = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(c, "storages");
        if (r->cfg.stg) {
            nvr_disk_t d[8]; int n = nvr_storage_list(r->cfg.stg, d, 8);
            for (int i = 0; i < n; i++) { cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "name", d[i].path);
                cJSON_AddNumberToObject(o, "totalSize", (double)(d[i].capacity_bytes / 1024 / 1024));
                cJSON_AddStringToObject(o, "status",
                    d[i].state == NVR_DISK_ACTIVE ? "ready" : (d[i].state == NVR_DISK_FAILED ? "error" : "idle"));
                cJSON_AddItemToArray(arr, o); }
        }
        return resp_content(c);
    }
    if (!strcmp(func, "formatStorage")) {
        if (!r->cfg.stg) return resp_status(501, "no storage");
        nvr_disk_t d[8]; int n = nvr_storage_list(r->cfg.stg, d, 8);
        NVR_LOGW("router", "formatStorage(%s)", jstr(args, "value", "hdd"));
        if (n > 0 && nvr_storage_format(r->cfg.stg, d[0].path, 0, 1) == RSDK_OK) return resp_status(200, "OK");
        return resp_status(500, "format failed");
    }
    if (!strcmp(func, "getAllDisksHealth")) {
        cJSON *c = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(c, "disks");
        if (r->cfg.stg) { nvr_disk_t d[8]; int n = nvr_storage_list(r->cfg.stg, d, 8);
            for (int i = 0; i < n; i++) { cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "name", d[i].path);
                cJSON_AddBoolToObject(o, "healthy", d[i].health.ok);
                cJSON_AddNumberToObject(o, "tempCelsius", d[i].health.temp_celsius);
                cJSON_AddItemToArray(arr, o); } }
        return resp_content(c);
    }
    if (!strcmp(func, "getCurrentStorage")) {
        char b[32]; nvr_settings_get_str(s, "storage.current", b, sizeof(b), "hdd");
        cJSON *c = cJSON_CreateObject(); cJSON_AddStringToObject(c, "value", b); return resp_content(c);
    }
    if (!strcmp(func, "setCurrentStorage")) {
        nvr_settings_set_str(s, "storage.current", jstr(args, "value", "hdd")); return resp_status(200, "OK");
    }

    /* ---- 录像开关（存设置库 KV） ---- */
    if (!strcmp(func, "X_NightOwl_setChannelRecordingSwitch")) {
        int ch = jint(args, "channel", -1); if (ch < 0) return resp_status(400, "no channel");
        char k[48]; snprintf(k, sizeof(k), "rec.ch%d.switch", ch);
        nvr_settings_set_int(s, k, jbool(args, "value", 1)); return resp_status(200, "OK");
    }
    if (!strcmp(func, "X_NightOwl_getChannelRecordingSwitch")) {
        int ch = jint(args, "channel", -1); char k[48]; snprintf(k, sizeof(k), "rec.ch%d.switch", ch);
        cJSON *c = cJSON_CreateObject(); cJSON_AddBoolToObject(c, "value", nvr_settings_get_int(s, k, 1));
        return resp_content(c);
    }

    /* ---- 设备能力聚合（device + 每通道） ---- */
    if (!strcmp(func, "X_NightOwl_getDeviceCapabilities")) {
        cJSON *c = cJSON_CreateObject();
        cJSON *dev = cJSON_AddObjectToObject(c, "device");
        cJSON *dcap = cJSON_AddArrayToObject(dev, "capabilities");
        cJSON_AddItemToArray(dcap, cJSON_CreateString("multiStorage"));
        cJSON_AddItemToArray(dcap, cJSON_CreateString("format"));
        cJSON_AddItemToArray(dcap, cJSON_CreateString("cloudRecording"));
        cJSON *chs = cJSON_AddArrayToObject(c, "channels");
        if (r->cfg.cm) { nvr_channel_t list[32]; int n = nvr_chan_list(r->cfg.cm, list, 32);
            for (int i = 0; i < n; i++) { cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "channel", list[i].chn);
                cJSON_AddStringToObject(o, "signal", "IPC");
                cJSON *cc = cJSON_AddArrayToObject(o, "capabilities");
                cJSON_AddItemToArray(cc, cJSON_CreateString("cloudRecording"));
                cJSON_AddItemToArray(chs, o); } }
        return resp_content(c);
    }

    /* ---- 重启 ---- */
    if (!strcmp(func, "reboot")) {
        NVR_LOGW("router", "收到 reboot"); sync(); reboot(RB_AUTOBOOT); return resp_status(200, "OK");
    }

    /* ---- OTA：NVR 自升级 ---- */
    if (!strcmp(func, "upgradeFirmware")) {
        const char *url = jstr(args, "url", NULL);
        if (!url) return resp_status(400, "no url");
        char staging[128], updater[128];
        nvr_settings_get_str(s, "ota.staging", staging, sizeof(staging), "/mnt/update.rom");
        nvr_settings_get_str(s, "ota.updater", updater, sizeof(updater), "");
        int rc = nvr_ota_start(url, staging, updater[0] ? updater : NULL);
        if (rc == -2) return resp_status(409, "upgrade already in progress");
        if (rc != 0)  return resp_status(500, "start failed");
        return resp_status(200, "OK");
    }
    if (!strcmp(func, "checkFirmwareUpgradeStatus")) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "progress", nvr_ota_progress());
        cJSON_AddStringToObject(c, "state", nvr_ota_state());
        return resp_content(c);
    }

    /* ---- 事件列表（接 rsdk 索引） ---- */
    if (!strcmp(func, "X_NightOwl_queryEventList")) {
        int ch = jint(args, "channel", -1);
        uint32_t t0 = (uint32_t)jint(args, "startTime", 0), t1 = (uint32_t)jint(args, "endTime", 0);
        if (t1 == 0) t1 = (uint32_t)time(NULL);
        cJSON *c = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(c, "events");
        if (r->cfg.group) { rsdk_index_slot_t sl[128]; int n = rsdk_group_query(r->cfg.group, t0, t1, ch, -1, sl, 128);
            for (int i = 0; i < n && i < 128; i++) { if (!(sl[i].flags & RSDK_SLOT_EVENT)) continue;
                cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "channel", sl[i].chn);
                cJSON_AddNumberToObject(o, "startTime", sl[i].start_time);
                cJSON_AddNumberToObject(o, "endTime", sl[i].end_time);
                cJSON_AddNumberToObject(o, "type", sl[i].rectype);
                cJSON_AddItemToArray(arr, o); } }
        return resp_content(c);
    }
    if (!strcmp(func, "X_NightOwl_queryEventCalendar")) {
        cJSON *c = cJSON_CreateObject(); cJSON_AddArrayToObject(c, "days");  /* 月聚合待细化 */
        return resp_content(c);
    }

    return NULL;   /* 非本地命令 */
}

/* ------------------------- 顶层分派 ------------------------- */
char *nvr_cmd_dispatch(nvr_cmd_router_t *r, const char *json_in)
{
    if (!json_in) return resp_status(400, "empty");
    cJSON *req = cJSON_Parse(json_in);
    if (!req) return resp_status(400, "bad json");
    const char *func = jstr(req, "func", NULL);
    cJSON *args = cJSON_GetObjectItem(req, "args");

    char *out = NULL;

    /* 1) 本地命令：存/查设置库 */
    out = handle_local(r, func, args);
    if (out) { cJSON_Delete(req); return out; }

    /* 2) 面向通道的相机命令：找真实设备 IP → 透传/翻译 */
    int channel = jint(args, "channel", -1);
    if (channel >= 0 && is_channel_cmd(func)) {
        nvr_channel_t ch;
        if (!r->cfg.cm || nvr_chan_get(r->cfg.cm, channel, &ch) != 0) {
            cJSON_Delete(req);
            NVR_LOGW("router", "%s: 通道 %d 未找到设备 → 501", func, channel);
            return resp_status(501, "not supported (channel not found)");   /* 转不到且非本地 → 501 */
        }
        /* 解析真实设备 IP（onvif_ip 优先，否则从 url 取 host） */
        char ip[64]; snprintf(ip, sizeof(ip), "%s", ch.onvif_ip[0] ? ch.onvif_ip : "");
        if (!ip[0] && ch.url[0]) {   /* rtsp://<ip>[:port]/... 里取 host */
            const char *p = strstr(ch.url, "://");
            if (p) { p += 3; int i = 0; while (p[i] && p[i] != ':' && p[i] != '/' && i < 63) { ip[i] = p[i]; i++; } ip[i] = 0; }
        }
        /* NOP(0)/nopOnvif(1)=NightOwl 从机 → HTTP /APPJsonCmd 透传；通用 ONVIF(2) → 映射翻译 */
        if (ch.kind != 2) {
            NVR_LOGI("router", "%s → 透传 ch%d 设备 %s (%s)", func, channel, ip,
                     ch.kind == 0 ? "NOP" : "nopOnvif");
            out = forward_nop(ip, r->cfg.dev_nop_port, ch.user, ch.pass, json_in);
            cJSON_Delete(req);
            return out;
        }
        NVR_LOGI("router", "%s → ch%d 通用 ONVIF 翻译(映射层)", func, channel);
        /* 落到 nop_app_dispatch：cap 前门 → nop_onvif_map_dispatch → ONVIF SOAP 到相机 */
    }

    /* 3) 回落 nopcore 293 handler（含 ONVIF 映射前门） */
    cJSON_Delete(req);
    if (r->cfg.nop) {
        char *nout = NULL;
        if (nop_app_dispatch(r->cfg.nop, json_in, &nout) == 0 && nout) {
            char *dup = strdup(nout);
            nop_app_free_response(nout);
            return dup ? dup : resp_status(500, "oom");
        }
    }
    return resp_status(501, "not implemented");
}

/* ------------------------- 极简 HTTP 服务端 ------------------------- */
static int read_line(int fd, char *buf, int cap)
{
    int n = 0; char c;
    while (n < cap - 1) { int rc = recv(fd, &c, 1, 0); if (rc <= 0) return (n > 0) ? n : -1;
        buf[n++] = c; if (c == '\n') break; }
    buf[n] = 0; return n;
}

static void handle_conn(nvr_cmd_router_t *r, int fd)
{
    char line[1024]; int content_len = 0;
    /* 请求行 + 头部 */
    if (read_line(fd, line, sizeof(line)) <= 0) { close(fd); return; }
    while (read_line(fd, line, sizeof(line)) > 0) {
        if (line[0] == '\r' || line[0] == '\n') break;   /* 头结束 */
        if (strncasecmp(line, "Content-Length:", 15) == 0) content_len = atoi(line + 15);
    }
    /* 读 body */
    char *body = NULL;
    if (content_len > 0 && content_len < 1024 * 1024) {
        body = malloc(content_len + 1); int got = 0;
        while (got < content_len) { int rc = recv(fd, body + got, content_len - got, 0); if (rc <= 0) break; got += rc; }
        if (body) body[got] = 0;
    }
    char *resp = nvr_cmd_dispatch(r, body ? body : "{}");
    free(body);

    size_t rlen = resp ? strlen(resp) : 0;
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", rlen);
    send(fd, hdr, hn, MSG_NOSIGNAL);
    if (resp) send(fd, resp, rlen, MSG_NOSIGNAL);
    free(resp);
    close(fd);
}

static void *server_main(void *arg)
{
    nvr_cmd_router_t *r = arg;
    while (r->running) {
        struct sockaddr_in ca; socklen_t cl = sizeof(ca);
        int fd = accept(r->listen_fd, (struct sockaddr *)&ca, &cl);
        if (fd < 0) { if (r->running) usleep(100000); continue; }
        handle_conn(r, fd);     /* 单连接串行；界面本地单客户端足够 */
    }
    return NULL;
}

int nvr_cmd_router_start(const nvr_cmd_router_cfg_t *cfg, nvr_cmd_router_t **out)
{
    if (!cfg || !out) return -1;
    nvr_cmd_router_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->cfg = *cfg;
    r->port = cfg->port > 0 ? cfg->port : 8089;
    if (r->cfg.dev_nop_port <= 0) r->cfg.dev_nop_port = 8089;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    r->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (r->listen_fd < 0) { free(r); return -1; }
    int yes = 1; setsockopt(r->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)r->port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 只听 127.0.0.1 */
    if (bind(r->listen_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(r->listen_fd, 8) != 0) {
        NVR_LOGE("router", "监听 %d 失败: %s", r->port, strerror(errno));
        close(r->listen_fd); free(r); return -1;
    }
    r->running = 1;
    if (pthread_create(&r->thr, NULL, server_main, r) != 0) {
        r->running = 0; close(r->listen_fd); free(r); return -1;
    }
    NVR_LOGI("router", "命令路由监听 127.0.0.1:%d (界面 /APPJsonCmd)", r->port);
    *out = r;
    return 0;
}

void nvr_cmd_router_stop(nvr_cmd_router_t *r)
{
    if (!r) return;
    r->running = 0;
    if (r->listen_fd >= 0) { shutdown(r->listen_fd, SHUT_RDWR); close(r->listen_fd); }
    pthread_join(r->thr, NULL);
    curl_global_cleanup();
    free(r);
}

int nvr_cmd_router_port(nvr_cmd_router_t *r) { return r ? r->port : 0; }
