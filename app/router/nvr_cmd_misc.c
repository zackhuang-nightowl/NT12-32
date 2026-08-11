/***************************************************************************************
 *  nvr_cmd_misc.c — 通道聚合/安全/云存统计等 LOCAL handler。
 *  getChannelsStatus 走 NVR 通道状态机;其余回落 components/nop cap。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"

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
NVR_NOP(getEnhancedSecurity)
NVR_NOP(setEnhancedSecurity)
NVR_NOP(X_NightOwl_getDeviceActive)
NVR_NOP(X_NightOwl_setDeviceActive)
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
