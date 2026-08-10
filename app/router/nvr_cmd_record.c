/***************************************************************************************
 *  nvr_cmd_record.c — record 域 handler:每通道录像触发/开关(record_config)+ 推送开关(push_config)。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_streaming.h"   /* nvr_stream_set_record:开关立即驱动 recorder */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 取(无行则默认 record_on=1, stream main;触发默认**空**——本地录像不需触发,只云存用)。 */
static void record_get_or_default(const nvr_cmd_ctx_t *c, int chn, nvr_record_cfg_t *r)
{
    if (nvr_settings_record_get(c->settings, chn, r) != 0) {
        memset(r, 0, sizeof(*r)); r->chn = chn; r->record_on = 1;
        r->triggers[0] = 0;   /* 本地录像触发默认空(仅云存需要触发) */
        snprintf(r->stream_type, sizeof(r->stream_type), "main");
    }
}
/* triggers CSV → JSON 数组 */
static cJSON *triggers_to_arr(const char *csv)
{
    cJSON *ta = cJSON_CreateArray();
    char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", csv ? csv : "");
    for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) cJSON_AddItemToArray(ta, cJSON_CreateString(p));
    return ta;
}
/* triggers JSON 数组 → CSV */
static void arr_to_triggers(cJSON *args, char *buf, int cap)
{
    buf[0] = 0; cJSON *tg = args ? cJSON_GetObjectItem(args, "triggers") : NULL, *t; int first = 1;
    if (cJSON_IsArray(tg)) cJSON_ArrayForEach(t, tg) { if (cJSON_IsString(t)) {
        if (!first) strncat(buf, ",", (size_t)cap - strlen(buf) - 1);
        strncat(buf, t->valuestring, (size_t)cap - strlen(buf) - 1); first = 0; } }
}

/* channel 1-based → chn0=channel-1(内部,与 record_schedule/streaming 一致;统一 0-based 键)。 */
char *cmd_X_NightOwl_setChannelRecordingTriggers(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1); if (ch1 < 1) return nvr_resp_err("invalid_channel");
    int chn0 = ch1 - 1;
    nvr_record_cfg_t r; record_get_or_default(c, chn0, &r);
    arr_to_triggers(a, r.triggers, sizeof(r.triggers));
    nvr_settings_record_set(c->settings, &r);
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getChannelRecordingTriggers(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = nvr_jint(a, "channel", 1) - 1;
    nvr_record_cfg_t r; record_get_or_default(c, chn0, &r);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "triggers", triggers_to_arr(r.triggers));
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_setChannelRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1); if (ch1 < 1) return nvr_resp_err("invalid_channel");
    int chn0 = ch1 - 1;
    nvr_record_cfg_t r; record_get_or_default(c, chn0, &r);
    r.record_on = nvr_jbool(a, "value", 1);
    nvr_settings_record_set(c->settings, &r);
    return nvr_resp_ok();   /* 实际启停由主循环 rec_schedule_apply 合并(手动&&排程&&时段)下发 */
}
char *cmd_X_NightOwl_getChannelRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = nvr_jint(a, "channel", 1) - 1;
    nvr_record_cfg_t r; record_get_or_default(c, chn0, &r);
    cJSON *o = cJSON_CreateObject(); cJSON_AddBoolToObject(o, "value", r.record_on);
    return nvr_resp_content(o);
}
/* ============ 持续录像:定时录像总开关 + 周排程(NVR 本地实现;之前误透传给相机)============
 * channel 1-based → chn0=channel-1(内部)。开关立即驱动 recorder 开/关 writer(启用控制生效);
 * 排程 rules(秒级区间)存库,时段细化由录像调度 tick 评估(见 nvr_record_sched)。 */
char *cmd_X_NightOwl_setChannelContinuousScheduleRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1); if (ch1 < 1) return nvr_resp_err("invalid_channel");
    int chn0 = ch1 - 1, on = nvr_jbool(a, "value", 1);
    nvr_rec_schedule_t r; if (nvr_settings_rec_sched_get(c->settings, chn0, &r) != 0) { memset(&r,0,sizeof(r)); r.chn=chn0; }
    r.sched_on = on;
    nvr_settings_rec_sched_set(c->settings, &r);
    /* 不在此直接 set_record:由主循环 rec_schedule_apply 按(开关 && 时段)统一评估下发(≤5s 生效),
     * 单一 owner 避免与排程时段冲突。 */
    (void)chn0;
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getChannelContinuousScheduleRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = nvr_jint(a, "channel", 1) - 1;
    nvr_rec_schedule_t r; nvr_settings_rec_sched_get(c->settings, chn0, &r);   /* 默认 sched_on=1 */
    cJSON *o = cJSON_CreateObject(); cJSON_AddBoolToObject(o, "value", r.sched_on ? 1 : 0);
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_setChannelContinuousRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1); if (ch1 < 1) return nvr_resp_err("invalid_channel");
    int chn0 = ch1 - 1;
    cJSON *rules = a ? cJSON_GetObjectItem(a, "rules") : NULL;
    if (!cJSON_IsArray(rules)) return nvr_resp_err("invalid_param");
    nvr_rec_schedule_t r; if (nvr_settings_rec_sched_get(c->settings, chn0, &r) != 0) { memset(&r,0,sizeof(r)); r.chn=chn0; r.sched_on=1; }
    char *js = cJSON_PrintUnformatted(rules);
    if (js) { snprintf(r.rules, sizeof(r.rules), "%s", js); free(js); }
    nvr_settings_rec_sched_set(c->settings, &r);
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getChannelContinuousRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = nvr_jint(a, "channel", 1) - 1;
    nvr_rec_schedule_t r; nvr_settings_rec_sched_get(c->settings, chn0, &r);
    cJSON *rules = (r.rules[0]) ? cJSON_Parse(r.rules) : NULL;
    if (!rules || !cJSON_IsArray(rules)) {                 /* 无存储 → 默认 7×24 全天一条 rule */
        if (rules) cJSON_Delete(rules);
        rules = cJSON_CreateArray();
        cJSON *rule = cJSON_CreateObject();
        cJSON_AddStringToObject(rule, "id", "rule-id-1");
        cJSON *wd = cJSON_CreateArray();
        for (int d = 1; d <= 7; d++) cJSON_AddItemToArray(wd, cJSON_CreateNumber(d));
        cJSON_AddItemToObject(rule, "weekdays", wd);
        cJSON_AddStringToObject(rule, "startTime", "000000");
        cJSON_AddStringToObject(rule, "endTime", "235959");
        cJSON_AddItemToArray(rules, rule);
    }
    cJSON *o = cJSON_CreateObject(); cJSON_AddItemToObject(o, "rules", rules);
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_setChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch = nvr_jint(a, "channel", -1); if (ch < 0) ch = 0;
    nvr_push_cfg_t p; if (nvr_settings_push_get(c->settings, ch, &p) != 0) { memset(&p, 0, sizeof(p)); p.chn = ch; }
    p.switch_on = nvr_jbool(a, "value", 0);
    nvr_settings_push_set(c->settings, &p);
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch = nvr_jint(a, "channel", -1); if (ch < 0) ch = 0;
    nvr_push_cfg_t p; if (nvr_settings_push_get(c->settings, ch, &p) != 0) { memset(&p, 0, sizeof(p)); p.chn = ch; }
    cJSON *o = cJSON_CreateObject(); cJSON_AddBoolToObject(o, "value", p.switch_on);
    return nvr_resp_content(o);
}
