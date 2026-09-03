/***************************************************************************************
 *  nvr_cmd_system.c — system 域 handler:设备名/信息/时区/重启/owner/远程访问。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_identity.h"
#include "nvr_log.h"
#include "nvr_netime.h"
#include "nvr_ble.h"
#include "nvr_gui_notify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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
    (void)a; char nm[64], sn[64], mdl[32], fw[32], typ[32], mac[32] = "";
    nvr_settings_get_str(c->settings, "system.device_name", nm, sizeof(nm), NVR_DEF_NAME);
    nvr_identity_get_sn(sn, sizeof(sn));   /* SN 由数据分区 /User/OWLSerialNumber 读取,恒定 */
    nvr_settings_get_str(c->settings, "system.model", mdl, sizeof(mdl), NVR_DEF_MODEL);
    nvr_settings_get_str(c->settings, "system.fw_version", fw, sizeof(fw), NVR_DEF_FW_VERSION);
    nvr_settings_get_str(c->settings, "system.device_type", typ, sizeof(typ), NVR_DEF_DEVICE_TYPE);
    nvr_identity_get_mac("eth0", mac, sizeof(mac));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", typ);
    cJSON_AddStringToObject(o, "name", nm); cJSON_AddStringToObject(o, "sn", sn);
    cJSON_AddStringToObject(o, "model", mdl);
    cJSON_AddNumberToObject(o, "channels", nvr_settings_get_int(c->settings, "system.capacity", NVR_DEF_CAPACITY));
    /* APP(TUTK)契约字段:firmwareVersion/serialNumber/mac */
    cJSON_AddStringToObject(o, "firmwareVersion", fw);
    cJSON_AddStringToObject(o, "serialNumber", sn);
    cJSON_AddStringToObject(o, "mac", mac);
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
    int vrc;

    vrc = nvr_tz_validate_set(tz, dst);
    if (vrc == -1)
        return nvr_resp_err("invalid_tz_dst");
    if (vrc == -2)
        return nvr_resp_err("dst_not_supported");

    if (dst[0]) {
        nvr_settings_set_str(c->settings, "system.tz_dst", dst);
        if (tz[0]) nvr_settings_set_str(c->settings, "system.timezone", tz);
    } else {
        nvr_settings_set_str(c->settings, "system.tz_dst", "");                         /* 夏令关 */
        nvr_settings_set_str(c->settings, "system.timezone", tz[0] ? tz : NVR_DEF_TIMEZONE);
    }
    nvr_tz_install(c->settings);
    nvr_time_notify_changed(c->settings, "setTimezone");
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
/* App GET:与 set_datetime 同为 UTC 墙钟(gmtime),不是 cap 软时钟。 */
char *cmd_get_datetime(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    time_t now = time(NULL);
    struct tm g;
    gmtime_r(&now, &g);
    char date[16], clock_str[16];
    snprintf(date, sizeof(date), "%04d%02d%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
    snprintf(clock_str, sizeof(clock_str), "%02d%02d%02d", g.tm_hour, g.tm_min, g.tm_sec);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "date", date);
    cJSON_AddStringToObject(o, "time", clock_str);
    return nvr_resp_content(o);
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
    if (nvr_settings_owner_get(c->settings, &ow) != 0) memset(&ow, 0, sizeof(ow));
    const char *owner_id = nvr_jstr(a, "owner_id", "");
    const char *username = nvr_jstr(a, "username", "");
    const char *stoken   = nvr_jstr(a, "stoken", "");
    snprintf(ow.owner_id, sizeof(ow.owner_id), "%s", owner_id);
    snprintf(ow.username, sizeof(ow.username), "%s", username);
    if (stoken[0]) snprintf(ow.stoken, sizeof(ow.stoken), "%s", stoken);
    nvr_settings_owner_set(c->settings, &ow);

    /* ★ setOwner = APP/tutkagent 绑定路径(addDevice 由 APP 做,NVR 不实现;NVR 只记录
     * owner_id/username/stoken)。绑定到 NOP 账户后**删除本地 Admin 组/全部本地用户**
     * (与 GUI login_aws 绑定路径一致:本地 Admin 让位给 NOP 账户)。仅在有 owner_id 时删,
     * 避免空 setOwner 误删。 */
    if (owner_id[0] && c->settings) {
        nvr_user_row_t list[NVR_USER_MAX];
        int n = nvr_settings_user_list(c->settings, list, NVR_USER_MAX);
        for (int i = 0; i < n; i++)
            (void)nvr_settings_user_delete(c->settings, list[i].username);
        if (n > 0) NVR_LOGI("router", "setOwner: 绑定 NOP(%s) → 清本地用户 %d 个", owner_id, n);
    }

    /* 文档：setOwner 带 ownerId 触发 BLEKey 更新；回包带 BLEKey；下一连接才用新 key 加密 */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "func", "X_NightOwl_setOwner");
    if (owner_id[0]) {
        char blekey[20];
        if (nvr_ble_gen_key16(blekey, (int)sizeof(blekey)) == 0) {
            nvr_settings_set_str(c->settings, "ble.key", blekey);
            cJSON_AddStringToObject(o, "BLEKey", blekey);
            NVR_LOGI("router", "setOwner → BLEKey 已生成（下连生效）");
        }
    }
    NVR_LOGI("router", "setOwner 存储(stoken %d 字节)", (int)strlen(ow.stoken));
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_getOwner(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; nvr_owner_row_t ow; cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "func", "X_NightOwl_getOwner");
    if (nvr_settings_owner_get(c->settings, &ow) == 0) {
        cJSON_AddStringToObject(o, "owner_id", ow.owner_id);
        cJSON_AddStringToObject(o, "username", ow.username);
        cJSON_AddStringToObject(o, "stoken", ow.stoken);
    }
    /* BLEKey(NVR 蓝牙密码)← ble.key(nvr_settings.db);供 APP 重连取回蓝牙配对密码。仅非空时回。 */
    {
        char blekey[20] = "";
        nvr_settings_get_str(c->settings, "ble.key", blekey, sizeof(blekey), "");
        if (blekey[0]) cJSON_AddStringToObject(o, "BLEKey", blekey);
    }
    return nvr_resp_content(o);
}

/* 读 /dev/urandom 填 buf；失败用 rand 回落 */
static void fill_rand(unsigned char *buf, int n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    int ok = 0;
    if (fd >= 0) {
        if (read(fd, buf, (size_t)n) == n) ok = 1;
        close(fd);
    }
    if (!ok) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (int i = 0; i < n; i++) buf[i] = (unsigned char)(rand() & 0xFF);
    }
}

/* 文档 updateP2PCredential：设备乱数生成 authKey(8) + avPassword(6 hex)，落库并热更新 TUTK */
char *cmd_X_NightOwl_updateP2PCredential(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    static const char *ALNUM =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    unsigned char rnd[14];
    fill_rand(rnd, (int)sizeof(rnd));
    char auth[NVR_TUTK_AUTH_KEY_LEN + 1];
    for (int i = 0; i < NVR_TUTK_AUTH_KEY_LEN; i++)
        auth[i] = ALNUM[rnd[i] % 62];
    auth[NVR_TUTK_AUTH_KEY_LEN] = 0;
    char av[8];
    static const char *HEX = "0123456789abcdef";
    for (int i = 0; i < 6; i++)
        av[i] = HEX[rnd[8 + i] % 16];
    av[6] = 0;

    /* 写回数据分区 tutkdata.json(IOTCKey + AVKey),持久且对齐 ODC。 */
    (void)c;
    (void)nvr_identity_set_tutk_creds(auth, av);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "func", "X_NightOwl_updateP2PCredential");
    cJSON_AddStringToObject(o, "authKey", auth);
    cJSON_AddStringToObject(o, "avPassword", av);
    NVR_LOGI("router", "updateP2PCredential auth=%.8s av=%s", auth, av);
    return nvr_resp_content(o);
}

/* BLE 登录：出厂 admin/admin；已绑定则 user=owner_id 且 BLEKey=ble.key */
char *cmd_X_NightOwl_loginUser(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *user = nvr_jstr(a, "user", NULL);
    const char *ble  = nvr_jstr(a, "BLEKey", NULL);
    if (!user || !ble) return nvr_resp_err("invalid_param");

    nvr_owner_row_t ow;
    memset(&ow, 0, sizeof(ow));
    if (c->settings) (void)nvr_settings_owner_get(c->settings, &ow);

    int ok = 0;
    if (ow.owner_id[0]) {
        char blekey[20];
        nvr_settings_get_str(c->settings, "ble.key", blekey, sizeof(blekey), "");
        if (strcmp(user, ow.owner_id) == 0 && blekey[0] && strcmp(ble, blekey) == 0)
            ok = 1;
    } else {
        if (strcmp(user, "admin") == 0 && strcmp(ble, "admin") == 0)
            ok = 1;
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "func", "X_NightOwl_loginUser");
    if (!ok) {
        cJSON_Delete(o);
        return nvr_resp_err("auth_failed");
    }
    nvr_gui_set_ui_unlocked(1);
    return nvr_resp_content(o);
}

/* App/BLE:仅已登录(GUI 会话 / loginUser / notifyLoginSuccess / 向导解锁)才 200;否则 403。 */
char *cmd_X_NightOwl_unlock(cJSON *a, const nvr_cmd_ctx_t *c)
{
    nvr_owner_row_t ow;
    int allowed = nvr_gui_ui_unlocked();
    (void)a;
    memset(&ow, 0, sizeof(ow));
    if (!allowed && c && c->settings &&
        nvr_settings_owner_get(c->settings, &ow) == 0 && ow.owner_id[0])
        allowed = 1;   /* App 已绑定 owner,P2P 会话视为已登录 */
    if (!allowed)
        return nvr_resp_status(403, "Forbidden");
    nvr_gui_set_ui_unlocked(1);
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "func", "X_NightOwl_unlock");
        return nvr_resp_content(o);
    }
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
    (void)a; (void)c;
    char key[NVR_TUTK_AUTH_KEY_LEN + 4];
    nvr_identity_get_tutk_creds(key, sizeof(key), NULL, 0);   /* IOTCKey ← /User/OWL/tutkdata.json */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", key);
    return nvr_resp_content(o);
}

char *cmd_setIotcAuthKey(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value || !tutk_auth_key_valid(value))
        return nvr_resp_err("invalid_auth_key");
    /* 写回数据分区(持久,重启不丢);仅改 IOTCKey,保留 AvPassword。 */
    if (nvr_identity_set_tutk_creds(value, NULL) != 0)
        return nvr_resp_err("persist_failed");
    NVR_LOGI("router", "setIotcAuthKey → %.8s", value);
    return nvr_resp_ok();
}

char *cmd_setAvPassword(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value || !value[0] || strlen(value) > 32)
        return nvr_resp_err("invalid_param");
    if (nvr_identity_set_tutk_creds(NULL, value) != 0)
        return nvr_resp_err("persist_failed");
    NVR_LOGI("router", "setAvPassword");
    return nvr_resp_ok();
}

char *cmd_setIotcUID(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    const char *value = nvr_jstr(a, "value", NULL);
    if (!value || !value[0] || strlen(value) > 63)
        return nvr_resp_err("invalid_param");
    if (nvr_identity_set_uid(value) != 0)
        return nvr_resp_err("persist_failed");
    NVR_LOGI("router", "setIotcUID → %s", value);
    return nvr_resp_ok();
}

char *cmd_notifyLoginSuccess(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    nvr_gui_set_ui_unlocked(1);
    NVR_LOGI("router", "notifyLoginSuccess");
    return nvr_resp_ok();
}

char *cmd_notifySessionCount(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    NVR_LOGI("router", "notifySessionCount value=%s", nvr_jstr(a, "value", ""));
    return nvr_resp_ok();
}

char *cmd_getNotificationSetting(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return nvr_resp_not_support();   /* agent 启动引导:501 = 无内置推送配置 */
}

/* ---- ODC TUTK agent(AVAPIs_Server_CLI)启动引导:cgi 会向 :6061 拉取这些值 ---- */
char *cmd_getIotcUID(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    char uid[64] = "";
    nvr_identity_get_uid(uid, sizeof(uid));                   /* ← /User/tutk_agent_udid */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", uid);
    return nvr_resp_content(o);
}

char *cmd_getAvPassword(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    char av[16] = "";
    nvr_identity_get_tutk_creds(NULL, 0, av, sizeof(av));     /* AvPassword ← /User/OWL/tutkdata.json(默认888888) */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", av[0] ? av : "888888");
    return nvr_resp_content(o);
}

char *cmd_getAvAccount(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    char acct[64] = "";
    nvr_identity_get_av_account(acct, sizeof(acct));    /* AvAccount ← /User/OWL/tutkdata.json(默认 admin) */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", acct[0] ? acct : "admin");
    return nvr_resp_content(o);
}

char *cmd_getProfile(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    const char *paths[] = { "/tmp/tutk_profile.txt", "/dvr/tutk_cloud_agent/profile.txt" };
    FILE *fp = NULL;
    for (int i = 0; i < 2 && !fp; i++) fp = fopen(paths[i], "rb");
    if (!fp) return nvr_resp_not_support();
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > 65536) { fclose(fp); return nvr_resp_not_support(); }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return nvr_resp_err("oom"); }
    size_t rd = fread(buf, 1, (size_t)n, fp); fclose(fp);
    buf[rd] = '\0';
    cJSON *prof = cJSON_Parse(buf);
    free(buf);
    if (!prof) return nvr_resp_not_support();
    return nvr_resp_content(prof);   /* content = profile.txt 内容 */
}

char *cmd_GUI_getUID(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char uid[64], sn[64], mac[32];
    nvr_identity_get_uid(uid, sizeof(uid));
    nvr_identity_get_sn(sn, sizeof(sn));
    nvr_identity_get_mac("eth0", mac, sizeof(mac));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", uid);
    cJSON_AddStringToObject(o, "serial", sn);
    cJSON_AddStringToObject(o, "mac_address", mac);
    return nvr_resp_content(o);
}

char *cmd_GUI_getSystemLog(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return nvr_resp_not_support();
}
