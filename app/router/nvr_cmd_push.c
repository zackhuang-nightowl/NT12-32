/***************************************************************************************
 *  nvr_cmd_push.c — 推送配置接口（落 SQLite push_config）。见 pushNotification_standalone。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int push_cap(const nvr_cmd_ctx_t *c)
{
    int n = (c && c->settings)
        ? nvr_settings_get_int(c->settings, "system.capacity", NVR_DEF_CAPACITY)
        : NVR_DEF_CAPACITY;
    if (n < 1) n = NVR_DEF_CAPACITY;
    if (n > NVR_DEF_CAPACITY) n = NVR_DEF_CAPACITY;
    return n;
}

static int chn0_of(cJSON *a)
{
    int ch1 = nvr_jint(a, "channel", 1);
    return ch1 - 1;
}

static int valid_ch(const nvr_cmd_ctx_t *c, int chn0)
{
    return chn0 >= 0 && chn0 < push_cap(c);
}

static int valid_hhmm(const char *s)
{
    if (!s || (int)strlen(s) != 4) return 0;
    for (int i = 0; i < 4; i++) if (!isdigit((unsigned char)s[i])) return 0;
    int hh = (s[0] - '0') * 10 + (s[1] - '0');
    int mm = (s[2] - '0') * 10 + (s[3] - '0');
    return hh <= 23 && mm <= 59;
}

static int is_push_trigger(const char *s)
{
    static const char *k[] = {
        "pir", "pixelChange", "human", "face", "vehicle", "animal",
        "package", "doorbellRing", "lineCross", "fieldIntrusion", NULL
    };
    if (!s || !s[0]) return 0;
    for (int i = 0; k[i]; i++) if (strcmp(s, k[i]) == 0) return 1;
    return 0;
}

static int push_load(const nvr_cmd_ctx_t *c, int chn0, nvr_push_cfg_t *p)
{
    if (!c || !c->settings) return -1;
    return nvr_settings_push_get(c->settings, chn0, p);
}

static cJSON *triggers_arr(const char *csv)
{
    cJSON *a = cJSON_CreateArray();
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", csv ? csv : "");
    for (char *p = strtok(buf, ","); p; p = strtok(NULL, ",")) {
        while (*p == ' ') p++;
        if (*p) cJSON_AddItemToArray(a, cJSON_CreateString(p));
    }
    return a;
}

static int triggers_from_arr(cJSON *arr, char *out, int cap)
{
    out[0] = 0;
    if (!cJSON_IsArray(arr) || cap < 2) return -1;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsString(it) || !is_push_trigger(it->valuestring)) return -1;
        if (out[0]) strncat(out, ",", (size_t)cap - strlen(out) - 1);
        strncat(out, it->valuestring, (size_t)cap - strlen(out) - 1);
    }
    return 0;
}

char *cmd_X_NightOwl_getChannelPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    if (!valid_ch(c, chn0)) return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "value", p.switch_on);
    return nvr_resp_content(o);
}

char *cmd_X_NightOwl_setChannelPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    if (!valid_ch(c, chn0) || !nvr_jhas(a, "value") || !c || !c->settings)
        return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    p.chn = chn0;
    p.switch_on = nvr_jbool(a, "value", 0);
    if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_getChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    int cap = push_cap(c);
    cJSON *o = cJSON_CreateObject();
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < cap; i++) {
        nvr_push_cfg_t p;
        if (push_load(c, i, &p) != 0) return nvr_resp_err("no_config");
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "channel", i + 1);
        cJSON_AddBoolToObject(e, "value", p.switch_on);
        cJSON_AddItemToArray(chs, e);
    }
    return nvr_resp_content(o);
}

char *cmd_X_NightOwl_setChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *chs = a && cJSON_IsArray(a) ? a : (a ? cJSON_GetObjectItem(a, "channels") : NULL);
    if (!cJSON_IsArray(chs) || !c || !c->settings) return nvr_resp_err("invalid_param");
    cJSON *it;
    cJSON_ArrayForEach(it, chs) {
        int chn0 = nvr_jint(it, "channel", 0) - 1;
        if (!valid_ch(c, chn0) || !nvr_jhas(it, "value"))
            return nvr_resp_err("invalid_param");
        nvr_push_cfg_t p;
        if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
        p.chn = chn0;
        p.switch_on = nvr_jbool(it, "value", 0);
        if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    }
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_getChannelPushNotificationTriggers(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    if (!valid_ch(c, chn0)) return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "triggers", triggers_arr(p.triggers));
    return nvr_resp_content(o);
}

char *cmd_X_NightOwl_setChannelPushNotificationTriggers(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    cJSON *tr = a ? cJSON_GetObjectItem(a, "triggers") : NULL;
    if (!valid_ch(c, chn0) || !cJSON_IsArray(tr) || !c || !c->settings)
        return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    p.chn = chn0;
    if (triggers_from_arr(tr, p.triggers, (int)sizeof(p.triggers)) != 0)
        return nvr_resp_err("invalid_param");
    if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_getChannelPushNotificationDoNotDisturb(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    if (!valid_ch(c, chn0)) return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "isEnabled", p.dnd_enable);
    cJSON_AddStringToObject(o, "startTime", p.dnd_start);
    cJSON_AddStringToObject(o, "endTime", p.dnd_end);
    cJSON_AddStringToObject(o, "timeUnit", p.time_unit);
    return nvr_resp_content(o);
}

char *cmd_X_NightOwl_setChannelPushNotificationDoNotDisturb(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = chn0_of(a);
    if (!valid_ch(c, chn0) || !nvr_jhas(a, "enable") || !c || !c->settings)
        return nvr_resp_err("invalid_param");
    int en = nvr_jbool(a, "enable", 0);
    const char *st = nvr_jstr(a, "startTime", NULL);
    const char *et = nvr_jstr(a, "endTime", NULL);
    if (en && (!valid_hhmm(st) || !valid_hhmm(et)))
        return nvr_resp_err("invalid_param");
    nvr_push_cfg_t p;
    if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
    p.chn = chn0;
    p.dnd_enable = en;
    if (valid_hhmm(st)) snprintf(p.dnd_start, sizeof(p.dnd_start), "%s", st);
    if (valid_hhmm(et)) snprintf(p.dnd_end, sizeof(p.dnd_end), "%s", et);
    if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_setSnooze(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *chs = a ? cJSON_GetObjectItem(a, "channels") : NULL;
    if (!cJSON_IsArray(chs) || !c || !c->settings) return nvr_resp_err("invalid_param");
    time_t now = time(NULL);
    cJSON *it;
    cJSON_ArrayForEach(it, chs) {
        int chn0 = nvr_jint(it, "channel", 0) - 1;
        int end  = nvr_jint(it, "endTime", 0);
        if (!valid_ch(c, chn0)) return nvr_resp_err("invalid_param");
        if (end != 0 && end <= (int)now) continue;   /* 只能设未来；0=关 */
        nvr_push_cfg_t p;
        if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
        p.chn = chn0;
        p.snooze_end = end;
        if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    }
    return nvr_resp_ok();
}

char *cmd_getSnooze(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    int cap = push_cap(c);
    cJSON *o = cJSON_CreateObject();
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < cap; i++) {
        nvr_push_cfg_t p;
        if (push_load(c, i, &p) != 0) return nvr_resp_err("no_config");
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "endTime", p.snooze_end);
        cJSON_AddNumberToObject(e, "channel", i + 1);
        cJSON_AddItemToArray(chs, e);
    }
    return nvr_resp_content(o);
}

char *cmd_setPushPhotoSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *chs = a ? cJSON_GetObjectItem(a, "channels") : NULL;
    if (!cJSON_IsArray(chs) || !c || !c->settings) return nvr_resp_err("invalid_param");
    cJSON *it;
    cJSON_ArrayForEach(it, chs) {
        int chn0 = nvr_jint(it, "channel", 0) - 1;
        if (!valid_ch(c, chn0)) return nvr_resp_err("invalid_param");
        nvr_push_cfg_t p;
        if (push_load(c, chn0, &p) != 0) return nvr_resp_err("no_config");
        p.chn = chn0;
        p.photo_on = nvr_jbool(it, "enable", 0);
        if (nvr_settings_push_set(c->settings, &p) != 0) return nvr_resp_err("persist_failed");
    }
    return nvr_resp_ok();
}

char *cmd_getPushPhotoSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    int cap = push_cap(c);
    cJSON *o = cJSON_CreateObject();
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < cap; i++) {
        nvr_push_cfg_t p;
        if (push_load(c, i, &p) != 0) return nvr_resp_err("no_config");
        cJSON *e = cJSON_CreateObject();
        cJSON_AddBoolToObject(e, "enable", p.photo_on);
        cJSON_AddNumberToObject(e, "channel", i + 1);
        cJSON_AddItemToArray(chs, e);
    }
    return nvr_resp_content(o);
}
