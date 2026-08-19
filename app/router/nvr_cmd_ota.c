/***************************************************************************************
 *  nvr_cmd_ota.c — ota 域 handler:NVR 自升级 + 进度查询。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_ota.h"
#include <string.h>

static int arg_force(cJSON *a)
{
    if (nvr_jbool(a, "force", 0) || nvr_jbool(a, "forceUpgrade", 0)) return 1;
    if (nvr_jint(a, "force", 0) || nvr_jint(a, "forceUpgrade", 0)) return 1;
    const char *s = nvr_jstr(a, "force", NULL);
    if (!s) s = nvr_jstr(a, "forceUpgrade", NULL);
    if (s && (s[0] == '1' || !strcmp(s, "true") || !strcmp(s, "TRUE") ||
              !strcmp(s, "yes")))
        return 1;
    return 0;
}

char *cmd_upgradeFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *src = nvr_jstr(a, "url", NULL);
    if (!src) src = nvr_jstr(a, "FileName", NULL);
    if (!src) src = nvr_jstr(a, "fileName", NULL);
    if (!src) return nvr_resp_err("missing_url");

    char staging[128], updater[128], cur[64];
    nvr_settings_get_str(c->settings, "ota.staging", staging, sizeof(staging),
                         NVR_DEF_OTA_STAGING);
    nvr_settings_get_str(c->settings, "ota.updater", updater, sizeof(updater),
                         NVR_DEF_OTA_UPDATER);
    nvr_settings_get_str(c->settings, "system.fw_version", cur, sizeof(cur),
                         NVR_DEF_FW_VERSION);

    const char *md5 = nvr_jstr(a, "md5", NULL);
    if (!md5) md5 = nvr_jstr(a, "MD5", NULL);
    const char *ver = nvr_jstr(a, "version", NULL);
    if (!ver) ver = nvr_jstr(a, "firmwareVersion", NULL);

    int rc = nvr_ota_start(src, staging, updater,
                           md5, cur, ver, arg_force(a));
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
