/***************************************************************************************
 *  nvr_cmd_cloud.c — cloud 域 handler:云存总开关(cloud.switch KV)+ 每通道配置(cloud_channel 表)。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_log.h"
#include <string.h>

char *cmd_X_NightOwl_setCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    nvr_settings_set_int(c->settings, "cloud.switch", nvr_jbool(a, "value", 0));
    NVR_LOGI("router", "云存开关 → %d", nvr_jbool(a, "value", 0));
    return nvr_resp_ok();
}
char *cmd_X_NightOwl_getCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "value", nvr_settings_get_int(c->settings, "cloud.switch", 0));
    return nvr_resp_content(o);
}
char *cmd_setCloudRecordConfigs(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *chs = a ? cJSON_GetObjectItem(a, "channels") : NULL, *it;
    if (cJSON_IsArray(chs)) cJSON_ArrayForEach(it, chs) {
        nvr_cloud_ch_row_t row; memset(&row, 0, sizeof(row));
        row.chn = nvr_jint(it, "channel", -1);
        if (row.chn < 0) continue;
        row.enable = 1;
        snprintf(row.stream_type, sizeof(row.stream_type), "%s", nvr_jstr(it, "streamType", "main"));
        cJSON *tg = cJSON_GetObjectItem(it, "triggers"), *t; char buf[128] = {0}; int first = 1;
        if (cJSON_IsArray(tg)) cJSON_ArrayForEach(t, tg) {
            if (cJSON_IsString(t)) { if (!first) strncat(buf, ",", sizeof(buf)-strlen(buf)-1);
                strncat(buf, t->valuestring, sizeof(buf)-strlen(buf)-1); first = 0; } }
        snprintf(row.triggers, sizeof(row.triggers), "%s", buf);
        nvr_settings_cloud_ch_upsert(c->settings, &row);
    }
    NVR_LOGI("router", "云存配置已存");
    return nvr_resp_ok();
}
char *cmd_getCloudRecordConfigs(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; nvr_cloud_ch_row_t rows[64]; int n = nvr_settings_cloud_ch_list(c->settings, rows, 64);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", nvr_settings_get_int(c->settings, "storage.has_disk", 1) ? "async" : "sync");
    cJSON *arr = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "channel", rows[i].chn);
        cJSON_AddStringToObject(e, "streamType", rows[i].stream_type);
        cJSON *ta = cJSON_AddArrayToObject(e, "triggers");
        char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", rows[i].triggers);
        for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) cJSON_AddItemToArray(ta, cJSON_CreateString(p));
        cJSON_AddItemToArray(arr, e);
    }
    return nvr_resp_content(o);
}

char *cmd_getCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c)
{
    return cmd_X_NightOwl_getCloudRecordSwitch(a, c);
}
