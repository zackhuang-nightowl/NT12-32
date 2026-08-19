/***************************************************************************************
 *  nvr_cmd_misc.c — 通道聚合/安全/云存统计等 LOCAL handler。
 *  getChannelsStatus 走 NVR 通道状态机;其余回落 components/nop cap。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_chan_bind.h"
#include "nvr_dev_classify.h"
#include "nvr_onvif.h"
#include "nvr_crypto.h"
#include "nvr_log.h"
#include <string.h>

#define NVR_NOP(name) \
char *cmd_##name(cJSON *a, const nvr_cmd_ctx_t *c) { return nvr_cmd_nop_dispatch(a, c, #name); }

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

NVR_NOP(getChannelStats)
NVR_NOP(getChannelLoading)

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

NVR_NOP(getCurrentClouds)
NVR_NOP(getCloudStatusHistory)
NVR_NOP(getChannelCloudRecordStats)
NVR_NOP(getChannelCloudRecordStatsSwitch)
NVR_NOP(setChannelCloudRecordStatsSwitch)
NVR_NOP(getChannelRecordingContent)
NVR_NOP(getReportServer)
NVR_NOP(getEnvironment)
NVR_NOP(getLog)

#undef NVR_NOP
