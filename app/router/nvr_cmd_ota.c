/***************************************************************************************
 *  nvr_cmd_ota.c — ota 域 handler:NVR 自升级 + 进度查询。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_ota.h"

char *cmd_upgradeFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *url = nvr_jstr(a, "url", NULL);
    if (!url) return nvr_resp_err("missing_url");
    char staging[128], updater[128];
    nvr_settings_get_str(c->settings, "ota.staging", staging, sizeof(staging), NVR_DEF_OTA_STAGING);
    nvr_settings_get_str(c->settings, "ota.updater", updater, sizeof(updater), "");
    int rc = nvr_ota_start(url, staging, updater[0] ? updater : NULL);
    if (rc == -2) return nvr_resp_err("upgrade_in_progress");
    if (rc != 0)  return nvr_resp_err("start_failed");
    return nvr_resp_ok();
}
char *cmd_checkFirmwareUpgradeStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "progress", nvr_ota_progress());
    cJSON_AddStringToObject(o, "state", nvr_ota_state());
    return nvr_resp_content(o);
}
