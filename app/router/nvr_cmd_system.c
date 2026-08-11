/***************************************************************************************
 *  nvr_cmd_system.c — system 域 handler:设备名/信息/时区/重启/owner/远程访问。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "nvr_netime.h"
#include "nvr_tutk.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/reboot.h>

char *cmd_setName(cJSON *a, const nvr_cmd_ctx_t *c)
{
    nvr_settings_set_str(c->settings, "system.device_name", nvr_jstr(a, "name", ""));
    NVR_LOGI("router", "setName → %s", nvr_jstr(a, "name", ""));
    return nvr_resp_ok();
}
char *cmd_getName(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; char b[64];
    nvr_settings_get_str(c->settings, "system.device_name", b, sizeof(b), NVR_DEF_NAME);
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "name", b);
    return nvr_resp_content(o);
}

/* ---- 通道名(channels.json 持久化;默认 "Channel<N>",1..capacity)---- */
/* 列出所有通道名(getInputChannelNames):按容量 1..N 全列(有无真机都列)。 */
char *cmd_X_NightOwl_getInputChannelNames(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    int cap = c->settings ? nvr_settings_get_int(c->settings, "system.capacity", NVR_PERSIST_MAX_CH) : NVR_PERSIST_MAX_CH;
    if (cap < 1) cap = NVR_PERSIST_MAX_CH;
    if (cap > NVR_PERSIST_MAX_CH) cap = NVR_PERSIST_MAX_CH;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "names");
    for (int ch = 1; ch <= cap; ch++) {
        char nm[64];
        nvr_chan_persist_get_name(c->persist, ch, nm, sizeof(nm));   /* 未设→默认 ChannelN */
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", nm);
        cJSON_AddNumberToObject(e, "channel", ch);
        cJSON_AddItemToArray(arr, e);
    }
    return nvr_resp_content(o);
}
/* 单通道名(getInputChannelName)。 */
char *cmd_X_NightOwl_getInputChannelName(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch = nvr_jint(a, "channel", -1);
    if (ch < 1) return nvr_resp_err("invalid_param");
    char nm[64]; nvr_chan_persist_get_name(c->persist, ch, nm, sizeof(nm));
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "name", nm);
    return nvr_resp_content(o);
}
/* 设通道名(setInputChannelName)→ 存 channels.json。 */
char *cmd_X_NightOwl_setInputChannelName(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch = nvr_jint(a, "channel", -1);
    const char *name = nvr_jstr(a, "name", NULL);
    if (ch < 1 || !name) return nvr_resp_err("invalid_param");
    if (!c->persist || nvr_chan_persist_set_name(c->persist, ch, name) != 0)
        return nvr_resp_err("persist_failed");
    NVR_LOGI("router", "setInputChannelName ch%d=\"%s\"", ch, name);
    return nvr_resp_ok();
}
char *cmd_getDeviceInfo(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; char nm[64], sn[64], mdl[32];
    nvr_settings_get_str(c->settings, "system.device_name", nm, sizeof(nm), NVR_DEF_NAME);
    nvr_settings_get_str(c->settings, "system.sn", sn, sizeof(sn), "");   /* SN 由烧录分区读取,无编译默认 */
    nvr_settings_get_str(c->settings, "system.model", mdl, sizeof(mdl), NVR_DEF_MODEL);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", nm); cJSON_AddStringToObject(o, "sn", sn);
    cJSON_AddStringToObject(o, "model", mdl);
    cJSON_AddNumberToObject(o, "channels", nvr_settings_get_int(c->settings, "system.capacity", NVR_DEF_CAPACITY));
    return nvr_resp_content(o);
}
/* 设置系统时区(协议 X_NightOwl_setTimezone):
 *  - 送 tz_dst(POSIX DST 串) → 开夏令,按 tz_dst 生效(timezone 若也送则一并存作回显)。
 *  - 仅送 timezone("GMT ±H:MM") → 关夏令(清 tz_dst),按 GMT 偏移生效。
 * 存库后**即时安装**(合法 POSIX+TZif+/etc/localtime,GUI 与 nvr_app 两进程对齐),并**后台把
 * NVR 时间经 ONVIF 下发所有相机**。系统时钟仍 UTC;录像戳 epoch 不变(时区仅影响显示/排程)。 */
char *cmd_X_NightOwl_setTimezone(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *tz  = nvr_jstr(a, "timezone", "");
    const char *dst = nvr_jstr(a, "tz_dst",   "");
    if (dst[0]) {
        nvr_settings_set_str(c->settings, "system.tz_dst", dst);
        if (tz[0]) nvr_settings_set_str(c->settings, "system.timezone", tz);
    } else {
        nvr_settings_set_str(c->settings, "system.tz_dst", "");                         /* 夏令关 */
        nvr_settings_set_str(c->settings, "system.timezone", tz[0] ? tz : NVR_DEF_TIMEZONE);
    }
    nvr_tz_install(c->settings);          /* 即时生效(不等 60s tick) */
    nvr_time_push_cameras(c->settings);   /* 相机 ONVIF 授时(后台) */
    NVR_LOGI("router", "setTimezone tz='%s' dst='%s'", tz, dst);
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getTimezone(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; char tz[64], dst[80];
    nvr_settings_get_str(c->settings, "system.timezone", tz,  sizeof(tz),  NVR_DEF_TIMEZONE);
    nvr_settings_get_str(c->settings, "system.tz_dst",   dst, sizeof(dst), "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "timezone", tz);
    if (dst[0]) cJSON_AddStringToObject(o, "tz_dst", dst);
    return nvr_resp_content(o);
}
/* 手动设置系统时间(协议 set_datetime):date=yyyymmdd / time=hhmmss,**线协议为 UTC**
 * (与 SDK civil_to_epoch/get_datetime 的 gmtime 语义一致)。支持只给 date / 只给 time / 两者。
 * 真正 settimeofday 设 OS 时钟 + 写 RTC + 给所有相机 ONVIF 授时(NVR 为主时间)。
 * 之前 SDK 的 handle_set_datetime 只 pin 了软件 epoch,不动 OS 钟 → 录像戳/排程/显示仍错;此处修正。 */
char *cmd_set_datetime(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *date = nvr_jstr(a, "date", "");
    const char *tmv  = nvr_jstr(a, "time", "");
    if (!date[0] && !tmv[0]) return nvr_resp_err("invalid_param");

    time_t now = time(NULL); struct tm g; gmtime_r(&now, &g);   /* 以当前 UTC 为基准,支持部分字段 */
    if (date[0]) {
        int Y, Mo, D;
        if (strlen(date) != 8 || sscanf(date, "%4d%2d%2d", &Y, &Mo, &D) != 3 ||
            Mo < 1 || Mo > 12 || D < 1 || D > 31) return nvr_resp_err("invalid_param");
        g.tm_year = Y - 1900; g.tm_mon = Mo - 1; g.tm_mday = D;
    }
    if (tmv[0]) {
        int H, Mi, S;
        if (strlen(tmv) != 6 || sscanf(tmv, "%2d%2d%2d", &H, &Mi, &S) != 3 ||
            H > 23 || Mi > 59 || S > 59) return nvr_resp_err("invalid_param");
        g.tm_hour = H; g.tm_min = Mi; g.tm_sec = S;
    }
    g.tm_isdst = 0;
    time_t epoch = timegm(&g);                                  /* 按 UTC 解析 */
    if (epoch == (time_t)-1) return nvr_resp_err("invalid_param");
    if (nvr_time_set_clock(c->settings, (long long)epoch) != 0) return nvr_resp_err("set_failed");
    NVR_LOGI("router", "set_datetime date='%s' time='%s' → epoch=%lld", date, tmv, (long long)epoch);
    return nvr_resp_ok();
}
/* 自动授时开关(协议 X_NightOwl_setTimeSyncSwitch):enable=true→自动 NTP;false→手动(不跑 NTP,
 * 不覆盖手动设的时间)。之前只存 SDK 内存变量、没连 NTP;此处落库并真正门控 NTP。 */
char *cmd_X_NightOwl_setTimeSyncSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int en = nvr_jbool(a, "enable", 1);
    int prev = nvr_settings_get_int(c->settings, "system.time_sync", 1);
    nvr_settings_set_int(c->settings, "system.time_sync", en);
    if (en && !prev) nvr_time_resync(c->settings);             /* 关→开:立即再同步 */
    NVR_LOGI("router", "setTimeSyncSwitch enable=%d", en);
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getTimeSyncSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enable", nvr_settings_get_int(c->settings, "system.time_sync", 1));
    return nvr_resp_content(o);
}
char *cmd_reboot(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    NVR_LOGW("router", "收到 reboot"); sync(); reboot(RB_AUTOBOOT);
    return nvr_resp_ok();
}
/* 文件拷贝(恢复出厂复位 GUI_CONFIG 用)。 */
static void copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"); if (!in) { NVR_LOGW("router", "默认 %s 缺失,GUI_CONFIG 未复位", src); return; }
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return; }
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
}
/* 恢复出厂:清空设置表(保留 schema_version/seeded)+ 复位界面配置(删 gui_cfg.ini + 覆盖 GUI_CONFIG.json)+ 重启。 */
char *cmd_X_NightOwl_resetToFactorySettings(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    NVR_LOGW("router", "恢复出厂:清空设置表 + 复位界面配置 + 重启");
    if (c->settings) nvr_settings_factory_reset(c->settings);
    const char *ini = getenv("NVR_GUI_CFG_INI");        if (!ini || !ini[0]) ini = "/mnt/custom/gui_cfg.ini";
    const char *cfg = getenv("NVR_GUI_CONFIG");         if (!cfg || !cfg[0]) cfg = "/mnt/custom/GUI_CONFIG.json";
    const char *def = getenv("NVR_GUI_CONFIG_DEFAULT"); if (!def || !def[0]) def = "/dvr/config/defaults/GUI_CONFIG.json";
    unlink(ini);                 /* 删 gui_cfg.ini(LVGL 会重建) */
    copy_file(def, cfg);         /* 复位 GUI_CONFIG.json(LVGL 不重建,用自带默认覆盖) */
    sync(); reboot(RB_AUTOBOOT);
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_setOwner(cJSON *a, const nvr_cmd_ctx_t *c)
{
    nvr_owner_row_t ow;
    if (nvr_settings_owner_get(c->settings, &ow) != 0) memset(&ow, 0, sizeof(ow));  /* 读改写:保留 email/phone/cloud 等 */
    snprintf(ow.owner_id, sizeof(ow.owner_id), "%s", nvr_jstr(a, "owner_id", ""));
    snprintf(ow.username, sizeof(ow.username), "%s", nvr_jstr(a, "username", ""));
    snprintf(ow.stoken,   sizeof(ow.stoken),   "%s", nvr_jstr(a, "stoken", ""));
    nvr_settings_owner_set(c->settings, &ow);
    NVR_LOGI("router", "setOwner 存储(stoken %d 字节)", (int)strlen(ow.stoken));
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getOwner(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; nvr_owner_row_t ow; cJSON *o = cJSON_CreateObject();
    if (nvr_settings_owner_get(c->settings, &ow) == 0) {
        cJSON_AddStringToObject(o, "owner_id", ow.owner_id);
        cJSON_AddStringToObject(o, "username", ow.username);
        cJSON_AddStringToObject(o, "stoken", ow.stoken);
    }
    return nvr_resp_content(o);
}

/* 远程访问(BLE+P2P)开关:仅本地 admin 可控;绑 NOP 账户后固定打开 */
char *cmd_GUI_getRemoteAccessState(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(c->settings, &ow) == 0 && ow.owner_id[0]);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", nvr_settings_get_int(c->settings, "service.remote_access", 0));
    cJSON_AddBoolToObject(o, "isBoundAws", bound);
    return nvr_resp_content(o);
}
char *cmd_GUI_setRemoteAccessState(cJSON *a, const nvr_cmd_ctx_t *c)
{
    nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(c->settings, &ow) == 0 && ow.owner_id[0]);
    cJSON *o = cJSON_CreateObject();
    if (bound) {
        cJSON_AddStringToObject(o, "result", "AlreadyBound");
    } else {
        int en = nvr_jbool(a, "enable", 1);
        nvr_settings_set_int(c->settings, "service.remote_access", en);
        NVR_LOGI("router", "RemoteAccess → %d", en);
        cJSON_AddStringToObject(o, "result", "OK");
    }
    return nvr_resp_content(o);
}

/* ---- 功能列表 / 自动重启 / TUTK ---- */
static int tutk_auth_key_valid(const char *v)
{
    if (!v) return 0;
    size_t n = strlen(v);
    if (n != NVR_TUTK_AUTH_KEY_LEN) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((v[i] >= '0' && v[i] <= '9') ||
              (v[i] >= 'A' && v[i] <= 'Z') ||
              (v[i] >= 'a' && v[i] <= 'z')))
            return 0;
    return 1;
}

static const char *const g_feature_list[] = {
    "General", "Encode", "Record", "Alarm", "Network",
    "NetService", "OutputSettings", "Account", "RS232"
};

char *cmd_GUI_getFeatureList(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "Restore");
    for (size_t i = 0; i < sizeof(g_feature_list) / sizeof(g_feature_list[0]); i++)
        cJSON_AddItemToArray(arr, cJSON_CreateString(g_feature_list[i]));
    return nvr_resp_content(o);
}

char *cmd_GUI_getAutoRebootSetting(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "Enable", nvr_settings_get_int(c->settings, "system.auto_reboot.enable", 0));
    cJSON_AddNumberToObject(o, "WhichDay", nvr_settings_get_int(c->settings, "system.auto_reboot.day", 0));
    cJSON_AddNumberToObject(o, "WhichTime", nvr_settings_get_int(c->settings, "system.auto_reboot.time", 0));
    return nvr_resp_content(o);
}

char *cmd_GUI_setAutoRebootSetting(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!nvr_jhas(a, "Enable") || !nvr_jhas(a, "WhichDay") || !nvr_jhas(a, "WhichTime"))
        return nvr_resp_err("invalid_param");
    nvr_settings_set_int(c->settings, "system.auto_reboot.enable", nvr_jbool(a, "Enable", 0));
    nvr_settings_set_int(c->settings, "system.auto_reboot.day", nvr_jint(a, "WhichDay", 0));
    nvr_settings_set_int(c->settings, "system.auto_reboot.time", nvr_jint(a, "WhichTime", 0));
    return nvr_resp_ok();
}

char *cmd_getIotcAuthKey(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char key[NVR_TUTK_AUTH_KEY_LEN + 4];
    nvr_settings_get_str(c->settings, "tutk.authkey", key, sizeof(key), "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", key);
    return nvr_resp_content(o);
}

char *cmd_setIotcAuthKey(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value || !tutk_auth_key_valid(value))
        return nvr_resp_err("invalid_auth_key");
    if (!c->settings || nvr_settings_set_str(c->settings, "tutk.authkey", value) != 0)
        return nvr_resp_err("persist_failed");
    NVR_LOGI("router", "setIotcAuthKey → %.8s", value);
    return nvr_resp_ok();
}

char *cmd_GUI_getUID(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char uid[64], sn[64], mac[32];
    nvr_settings_get_str(c->settings, "tutk.uid", uid, sizeof(uid), "");
    nvr_settings_get_str(c->settings, "system.sn", sn, sizeof(sn), "");
    nvr_settings_get_str(c->settings, "system.mac", mac, sizeof(mac), "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", uid);
    cJSON_AddStringToObject(o, "serial", sn);
    cJSON_AddStringToObject(o, "mac_address", mac);
    return nvr_resp_content(o);
}

char *cmd_GUI_getSystemLog(cJSON *a, const nvr_cmd_ctx_t *c)
{
    return nvr_cmd_nop_dispatch(a, c, "GUI_getSystemLog");
}
