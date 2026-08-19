/***************************************************************************************
 *  nvr_cmd_record.c — record 域 handler:每通道录像触发/开关(record_config)+ 推送开关(push_config)。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_streaming.h"   /* nvr_stream_set_record:开关立即驱动 recorder */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 取(无行则默认 record_on=1 给 GET；真正录像只在 SET 落库后由 rec_schedule_apply 开)。 */
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
/* ============ 持续录像:定时录像总开关 + 周排程(NVR 本地)
 * GET 无库行 → 回开启 + 7×24(不写库)。SET 落库后 rec_schedule_apply 才真正开录。 */
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
    if (!cJSON_IsArray(rules)) {   /* 读DB:无行/非法 → 空数组(不再伪造 7×24 全满;出厂已由 DB 播种) */
        if (rules) cJSON_Delete(rules);
        rules = cJSON_CreateArray();
    }
    cJSON *o = cJSON_CreateObject(); cJSON_AddItemToObject(o, "rules", rules);
    return nvr_resp_content(o);
}

/* ============ 事件录像周排程(按 sensor；存 schedule 表 domain=record_event) ============ */
#define NVR_EVT_SCHED_DOMAIN "record_event"
#define NVR_EVT_SCHED_MAX    14

static void weekdays_csv_to_arr(const char *csv, cJSON *arr)
{
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%s", csv ? csv : "");
    for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) {
        int d = atoi(p);
        if (d >= 1 && d <= 7) cJSON_AddItemToArray(arr, cJSON_CreateNumber(d));
    }
}

static void weekdays_arr_to_csv(cJSON *arr, char *buf, int cap)
{
    buf[0] = 0;
    if (!cJSON_IsArray(arr) || cap < 2) return;
    int first = 1;
    cJSON *d;
    cJSON_ArrayForEach(d, arr) {
        int v = (int)cJSON_GetNumberValue(d);
        if (v < 1 || v > 7) continue;
        char num[4];
        snprintf(num, sizeof(num), "%d", v);
        if (!first) strncat(buf, ",", (size_t)cap - strlen(buf) - 1);
        strncat(buf, num, (size_t)cap - strlen(buf) - 1);
        first = 0;
    }
}

char *cmd_GUI_getChannelEventRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    const char *sensor = nvr_jstr(a, "sensor", NULL);
    if (ch1 < 1 || !sensor || !sensor[0]) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;

    nvr_schedule_row_t rows[NVR_EVT_SCHED_MAX];
    int n = (c && c->settings)
        ? nvr_settings_schedule_list(c->settings, chn0, NVR_EVT_SCHED_DOMAIN, sensor,
                                     rows, NVR_EVT_SCHED_MAX)
        : 0;

    /* 读DB:无行 → 空数组(不再伪造 7×24 全满;出厂已由 DB 播种 record_event 排程)。 */
    cJSON *rules = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *rule = cJSON_CreateObject();
        cJSON_AddStringToObject(rule, "id",
                                rows[i].rule_id[0] ? rows[i].rule_id : "rule-id-1");
        cJSON *wd = cJSON_CreateArray();
        weekdays_csv_to_arr(rows[i].weekdays, wd);
        cJSON_AddItemToObject(rule, "weekdays", wd);
        cJSON_AddStringToObject(rule, "startTime",
                                rows[i].start_hms[0] ? rows[i].start_hms : "000000");
        cJSON_AddStringToObject(rule, "endTime",
                                rows[i].end_hms[0] ? rows[i].end_hms : "235959");
        cJSON_AddItemToArray(rules, rule);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "rules", rules);
    return nvr_resp_content(o);
}

char *cmd_GUI_setChannelEventRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    const char *sensor = nvr_jstr(a, "sensor", NULL);
    cJSON *rules = a ? cJSON_GetObjectItem(a, "rules") : NULL;
    if (ch1 < 1 || !sensor || !sensor[0] || !cJSON_IsArray(rules))
        return nvr_resp_err("invalid_param");
    if (!c || !c->settings) return nvr_resp_err("invalid_param");

    int chn0 = ch1 - 1;
    int nrule = cJSON_GetArraySize(rules);
    if (nrule > NVR_EVT_SCHED_MAX) return nvr_resp_err("invalid_param");

    nvr_schedule_row_t rows[NVR_EVT_SCHED_MAX];
    memset(rows, 0, sizeof(rows));
    int n = 0;
    cJSON *rule;
    cJSON_ArrayForEach(rule, rules) {
        if (n >= NVR_EVT_SCHED_MAX) break;
        if (!cJSON_IsObject(rule)) return nvr_resp_err("invalid_param");
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(rule, "id"));
        const char *st = cJSON_GetStringValue(cJSON_GetObjectItem(rule, "startTime"));
        const char *en = cJSON_GetStringValue(cJSON_GetObjectItem(rule, "endTime"));
        cJSON *wd = cJSON_GetObjectItem(rule, "weekdays");
        if (!st || !en || (int)strlen(st) != 6 || (int)strlen(en) != 6 || !cJSON_IsArray(wd))
            return nvr_resp_err("invalid_param");
        nvr_schedule_row_t *r = &rows[n];
        r->chn = chn0;
        snprintf(r->domain, sizeof(r->domain), "%s", NVR_EVT_SCHED_DOMAIN);
        snprintf(r->sensor, sizeof(r->sensor), "%s", sensor);
        snprintf(r->rule_id, sizeof(r->rule_id), "%s", id && id[0] ? id : "rule-id-1");
        /* 同 id 多条时加后缀，满足 PRIMARY KEY(chn,domain,sensor,rule_id) */
        if (n > 0) {
            char uniq[32];
            snprintf(uniq, sizeof(uniq), "%s-%d", r->rule_id, n + 1);
            snprintf(r->rule_id, sizeof(r->rule_id), "%s", uniq);
        }
        weekdays_arr_to_csv(wd, r->weekdays, (int)sizeof(r->weekdays));
        if (!r->weekdays[0]) return nvr_resp_err("invalid_param");
        snprintf(r->start_hms, sizeof(r->start_hms), "%s", st);
        snprintf(r->end_hms, sizeof(r->end_hms), "%s", en);
        n++;
    }

    if (nvr_settings_schedule_replace(c->settings, chn0, NVR_EVT_SCHED_DOMAIN, sensor, rows, n) != 0)
        return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

/* ============ 事件后录秒数(Post Record) ============ */
char *cmd_getChannelRecordingTime(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    int v = (c && c->settings) ? nvr_settings_record_post_s_get(c->settings, chn0) : 10;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "value", v);
    return nvr_resp_content(o);
}

char *cmd_setChannelRecordingTime(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    if (ch1 < 1 || !nvr_jhas(a, "value") || !c || !c->settings)
        return nvr_resp_err("invalid_param");
    int sec = nvr_jint(a, "value", 10);
    if (nvr_settings_record_post_s_set(c->settings, ch1 - 1, sec) != 0)
        return nvr_resp_err("invalid_param");
    return nvr_resp_ok();
}
