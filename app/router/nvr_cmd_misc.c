/***************************************************************************************
 *  nvr_cmd_misc.c — 通道聚合/安全/云存统计等 LOCAL handler。
 *  getChannelsStatus 走 NVR 通道状态机;标记为暂不实现的 LOCAL 命令直接 501。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_chan_bind.h"
#include "nvr_dev_classify.h"
#include "nvr_onvif.h"
#include "nvr_crypto.h"
#include "nvr_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NVR_NOT_IMPL(name) \
char *cmd_##name(cJSON *a, const nvr_cmd_ctx_t *c) { (void)a; (void)c; return nvr_resp_not_support(); }

char *cmd_getChannelsStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    int cap = c->settings ? nvr_settings_get_int(c->settings, "system.capacity", NVR_DEF_CAPACITY) : NVR_DEF_CAPACITY;
    if (cap < 1) cap = NVR_DEF_CAPACITY;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "channels");
    for (int ch1 = 1; ch1 <= cap; ch1++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "channel", ch1);
        cJSON_AddNumberToObject(e, "status", c->cm ? nvr_chan_status_code_of(c->cm, ch1 - 1) : 0);
        cJSON_AddItemToArray(arr, e);
    }
    return nvr_resp_content(o);
}

NVR_NOT_IMPL(getChannelStats)
NVR_NOT_IMPL(getChannelLoading)

static void auth_all_same_ip(const nvr_cmd_ctx_t *c, const char *ip,
                             const char *random, const char *penh)
{
    nvr_channel_t list[NVR_MAX_CH];
    int n, i;
    if (!c || !c->cm || !ip || !ip[0]) return;
    n = nvr_chan_list(c->cm, list, NVR_MAX_CH);
    for (i = 0; i < n; i++)
        if (strcmp(list[i].onvif_ip, ip) == 0)
            nvr_chan_set_enh(c->cm, list[i].chn, random, penh);
}

char *cmd_getEnhancedSecurity(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    nvr_channel_t ch;
    char rnd[64] = {0};
    int rc;
    if (ch1 < 1) ch1 = 1;
    if (!c || !c->cm) return nvr_resp_not_support();
    if (nvr_chan_get(c->cm, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    if (ch.kind != NVR_DEV_KIND_NOP) return nvr_resp_not_support();
    rc = nvr_chan_enh_get(&ch, rnd, sizeof(rnd));
    if (rc == 1) {
        auth_all_same_ip(c, ch.onvif_ip, "", "");
        return nvr_resp_not_support();
    }
    if (rc != 0) return nvr_resp_err("failed");
    {
        nvr_channel_t cur;
        if (nvr_chan_get(c->cm, ch1 - 1, &cur) != 0) cur = ch;
        if (!rnd[0]) {
            if (cur.enh_random[0] || cur.pass[0])
                auth_all_same_ip(c, ch.onvif_ip, "", "");
        } else if (strcmp(cur.enh_random, rnd) != 0) {
            char penh[24];
            if (nvr_pw_from_random(rnd, penh, sizeof(penh)) == 16)
                auth_all_same_ip(c, ch.onvif_ip, rnd, penh);
        }
    }
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "random", rnd);
        return nvr_resp_content(o);
    }
}

char *cmd_setEnhancedSecurity(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    const char *random = nvr_jstr(a, "random", NULL);
    nvr_channel_t ch;
    char penh[24] = {0};
    int enable, rc;
    if (ch1 < 1) ch1 = 1;
    if (!c || !c->cm) return nvr_resp_not_support();
    if (nvr_chan_get(c->cm, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    if (ch.kind != NVR_DEV_KIND_NOP) return nvr_resp_not_support();
    enable = (random && random[0]) ? 1 : 0;
    rc = nvr_chan_enh_apply(&ch, enable, enable ? random : NULL, penh, sizeof(penh));
    if (rc == 1) return nvr_resp_not_support();
    if (rc != 0) return nvr_resp_err("failed");
    auth_all_same_ip(c, ch.onvif_ip, enable ? ch.enh_random : "", enable ? penh : "");
    NVR_LOGI("cmd", "ch%d setEnhancedSecurity %s", ch1, enable ? "on" : "off");
    return nvr_resp_ok();
}

/* GUI 激活：NVR 自己跑 GET/SET/再 GET，算 P_act，不使用界面下发的 password。 */
static int active_ensure_kind(const nvr_cmd_ctx_t *c, int chn0, nvr_channel_t *ch)
{
    char scopes[1024];
    if (!c || !c->cm || !ch) return -1;
    if (ch->kind == NVR_DEV_KIND_NOPONVIF) return 0;
    if (ch->kind == NVR_DEV_KIND_NOP) return -1;
    scopes[0] = 0;
    if (nvr_onvif_get_scopes(ch->onvif_ip, ch->onvif_port, "", "", scopes, (int)sizeof(scopes)) != 0)
        nvr_onvif_probe_scopes(ch->onvif_ip, scopes, (int)sizeof(scopes), NULL, 0, NULL);
    if (!scopes[0]) return -1;
    nvr_chan_apply_discovery(c->cm, chn0, scopes);
    if (nvr_chan_get(c->cm, chn0, ch) != 0) return -1;
    return (ch->kind == NVR_DEV_KIND_NOPONVIF) ? 0 : -1;
}

char *cmd_X_NightOwl_getDeviceActive(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    nvr_channel_t ch;
    int st = 0, code;
    if (ch1 < 1 || !c || !c->cm) return nvr_resp_not_support();
    if (nvr_chan_get(c->cm, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    if (ch.kind == NVR_DEV_KIND_NOP) {
        char *fwd = nvr_chan_dev_post(c->cm, ch1 - 1, "X_NightOwl_getDeviceActive", NULL);
        return fwd ? fwd : nvr_resp_not_support();
    }
    if (active_ensure_kind(c, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    code = nvr_chan_get_device_active(&ch, &st);
    if (code == 501) return nvr_resp_not_support();
    if (code != 200) return nvr_resp_err("device_not_active");
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "status", st ? 1 : 0);
        return nvr_resp_content(o);
    }
}

char *cmd_X_NightOwl_setDeviceActive(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    nvr_channel_t ch;
    char pact[24];
    int ar;
    if (ch1 < 1 || !c || !c->cm) return nvr_resp_not_support();
    if (nvr_chan_get(c->cm, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    if (active_ensure_kind(c, ch1 - 1, &ch) != 0) return nvr_resp_not_support();
    ar = nvr_chan_try_activate(&ch, NULL, pact, sizeof(pact));
    if (ar == 1) return nvr_resp_not_support();
    if (ar != 0) return nvr_resp_err("device_not_active");
    nvr_chan_set_auth(c->cm, ch1 - 1, "admin", pact);
    NVR_LOGI("cmd", "ch%d GUI 激活成功，P_act 已入库", ch1);
    return nvr_resp_ok();
}

char *cmd_getCurrentClouds(cJSON *a, const nvr_cmd_ctx_t *c)
{
    char cur[32], avail[256], tmp[256];
    cJSON *o, *arr;
    (void)a;
    if (!c || !c->settings)
        return nvr_resp_not_support();
    nvr_settings_get_str(c->settings, "cloudServer.current", cur, sizeof(cur), "tutk");
    nvr_settings_get_str(c->settings, "cloudServer.available", avail, sizeof(avail), "tutk");
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "currentCloud", cur);
    arr = cJSON_AddArrayToObject(o, "availableClouds");
    snprintf(tmp, sizeof(tmp), "%s", avail);
    for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) {
        while (*p == ' ') p++;
        if (*p)
            cJSON_AddItemToArray(arr, cJSON_CreateString(p));
    }
    if (cJSON_GetArraySize(arr) == 0)
        cJSON_AddItemToArray(arr, cJSON_CreateString("tutk"));
    return nvr_resp_content(o);
}

static void wifi_fill(nvr_chan_mgr_t *cm, int chn0, char *net, int ncap, int *signal)
{
    char *fwd;
    if (net && ncap > 0) snprintf(net, (size_t)ncap, "Ethernet");
    if (signal) *signal = 0;
    if (!cm) return;
    fwd = nvr_chan_dev_post(cm, chn0, "getCurrentWifi", NULL);
    if (!fwd) return;
    {
        cJSON *j = cJSON_Parse(fwd);
        free(fwd);
        if (!j) return;
        cJSON *ct = cJSON_GetObjectItem(j, "content");
        if (cJSON_IsObject(ct)) {
            const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(ct, "ssid"));
            cJSON *sig = cJSON_GetObjectItem(ct, "signal");
            if (ssid && ssid[0] && net && ncap > 0)
                snprintf(net, (size_t)ncap, "%s", ssid);
            if (signal && cJSON_IsNumber(sig)) *signal = (int)sig->valuedouble;
        }
        cJSON_Delete(j);
    }
}

char *cmd_X_NightOwl_getChannelInfo(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    nvr_channel_t ch;
    char name[64] = "";
    char net[64];
    int signal = 0;
    const char *dtype = "standaloneIpCamera";
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    if (!c->cm || nvr_chan_get(c->cm, ch1 - 1, &ch) != 0 ||
        nvr_chan_status_code_of(c->cm, ch1 - 1) == 0)
        return nvr_resp_not_support();
    if (c->persist)
        nvr_chan_persist_get_name(c->persist, ch1, name, sizeof(name));
    if (!name[0]) snprintf(name, sizeof(name), "%s", ch.name[0] ? ch.name : "Camera");
    if (ch.poe_port > 0) {
        snprintf(net, sizeof(net), "Ethernet");
        signal = 0;
    } else {
        wifi_fill(c->cm, ch1 - 1, net, (int)sizeof(net), &signal);
        if (!net[0]) snprintf(net, sizeof(net), "WiFi Network");
    }
    /* ★ 每次**实时**向设备取型号/序列号/固件版本(相机升级成功与否靠此真取判定,故不缓存)。取到覆盖
     * ch 里旧值;取不到(离线/超时)回退 ch 已存值。NOP→getDeviceInfo / ONVIF→GetDeviceInformation(直连
     * 已存 service_url,不广播)。含网络往返 → 本命令走 hold=0(router)不阻塞其它 8089 命令。 */
    char manu[64] = "";
    {
        nvr_chan_devinfo_t di;
        if (c->cm && nvr_chan_query_device_info(c->cm, ch1 - 1, &di) == 0) {
            if (di.firmware[0]) snprintf(ch.firmware, sizeof(ch.firmware), "%s", di.firmware);
            if (di.serial[0])   snprintf(ch.serial,   sizeof(ch.serial),   "%s", di.serial);
            if (di.model[0])    snprintf(ch.model,    sizeof(ch.model),    "%s", di.model);
            if (di.manufacturer[0]) snprintf(manu, sizeof(manu), "%s", di.manufacturer);
        }
    }
    if (ch.model[0] && (strncmp(ch.model, "DB-", 3) == 0 || strstr(ch.model, "doorbell") ||
                        strstr(ch.model, "Doorbell")))
        dtype = "doorbell";
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddStringToObject(o, "firmwareVersion", ch.firmware[0] ? ch.firmware : "");
        cJSON_AddStringToObject(o, "serialNumber", ch.serial[0] ? ch.serial : "");
        cJSON_AddStringToObject(o, "model", ch.model[0] ? ch.model : "");
        cJSON_AddStringToObject(o, "mac", ch.mac[0] ? ch.mac : "");
        cJSON_AddStringToObject(o, "manufacturer",
                                manu[0] ? manu :
                                ((ch.mac[0] && (strncmp(ch.mac, "54:2b:57", 8) == 0 ||
                                                strncmp(ch.mac, "54:2B:57", 8) == 0))
                                 ? "NightOwl" : ""));
        cJSON_AddStringToObject(o, "type", dtype);
        /* W5: 只暴露 IPv4;含 ':' 的 IPv6 链路本地(fe80::)等非法/不可路由地址一律返回空,
         * 绝不把脏地址透给 GUI(W3/W7 已从源头拦截,这里再兜一层)。 */
        cJSON_AddStringToObject(o, "ip",
                                (ch.onvif_ip[0] && !strchr(ch.onvif_ip, ':')) ? ch.onvif_ip : "");
        cJSON_AddStringToObject(o, "storageType", "none");
        cJSON_AddStringToObject(o, "network", net);
        cJSON_AddNumberToObject(o, "signalStrength", signal);
        return nvr_resp_content(o);
    }
}

NVR_NOT_IMPL(getCloudStatusHistory)
NVR_NOT_IMPL(getChannelCloudRecordStats)
NVR_NOT_IMPL(getChannelCloudRecordStatsSwitch)
NVR_NOT_IMPL(setChannelCloudRecordStatsSwitch)
NVR_NOT_IMPL(getChannelRecordingContent)
NVR_NOT_IMPL(getReportServer)
NVR_NOT_IMPL(getEnvironment)
NVR_NOT_IMPL(getLog)

#undef NVR_NOT_IMPL
