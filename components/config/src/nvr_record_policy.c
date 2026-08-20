/***************************************************************************************
 *  nvr_record_policy.c — 录像/云存码流与触发策略
 ***************************************************************************************/
#include "nvr_record_policy.h"
#include <stdlib.h>
#include <strings.h>
#include <string.h>

void nvr_record_stream_mask(const char *stream_type, int *main_on, int *sub_on)
{
    int m = 0, s = 0;
    if (!stream_type || !stream_type[0]) {
        m = s = 1;
    } else if (!strcasecmp(stream_type, "both")) {
        m = s = 1;
    } else if (!strcasecmp(stream_type, "main")) {
        m = 1;
    } else if (!strcasecmp(stream_type, "sub")) {
        s = 1;
    } else if (!strcasecmp(stream_type, "disable")) {
        /* both off */
    } else {
        m = s = 1;
    }
    if (main_on) *main_on = m;
    if (sub_on)  *sub_on  = s;
}

int nvr_triggers_csv_has(const char *csv, const char *trigger)
{
    char tmp[128];
    if (!trigger || !trigger[0]) return 0;
    if (!csv || !csv[0]) return 0;
    snprintf(tmp, sizeof(tmp), "%s", csv);
    for (char *p = strtok(tmp, ","); p; p = strtok(NULL, ",")) {
        while (*p == ' ') p++;
        if (!strcasecmp(p, trigger)) return 1;
    }
    return 0;
}

int nvr_cloud_ch_upload_stream(nvr_settings_t *s, int chn0, const char *trigger, int *out_stream)
{
    nvr_cloud_ch_row_t cc;
    if (!s || !out_stream) return 0;
    if (!nvr_settings_get_int(s, "cloud.switch", 0)) return 0;
    if (nvr_settings_cloud_ch_get(s, chn0, &cc) != 0) return 0;
    if (!cc.enable) return 0;
    if (!strcasecmp(cc.stream_type, "disable")) return 0;
    if (trigger && cc.triggers[0] && !nvr_triggers_csv_has(cc.triggers, trigger))
        return 0;
    *out_stream = (!strcasecmp(cc.stream_type, "main")) ? 0 : 1;
    return 1;
}

int nvr_cloud_async_upload_allowed(nvr_settings_t *s, uint32_t start_epoch)
{
    char since[32];
    if (!s) return 1;
    if (nvr_settings_get_str(s, "cloud.async_upload_since", since, sizeof(since), "") <= 0 ||
        !since[0])
        return 1;
    char *end = NULL;
    unsigned long ts = strtoul(since, &end, 10);
    if (end == since || (end && *end && *end != ' '))
        return 1;
    return start_epoch >= (uint32_t)ts;
}
