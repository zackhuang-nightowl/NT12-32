/***************************************************************************************
 *  nvr_cmd_ota.c — ota 域:NVR 自升级 + 查服务器 + 通道固件下推。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_defaults.h"
#include "nvr_ota.h"
#include "nvr_ipc_ota.h"
#include "nvr_identity.h"
#include "nop_sdk/nop_error_str.h"
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static void ota_cfg(const nvr_cmd_ctx_t *c, char *base, size_t bcap,
                    char *env, size_t ecap)
{
    nvr_settings_get_str(c->settings, "ota.url_base", base, (int)bcap, NVR_URL_OTA_BASE);
    nvr_settings_get_str(c->settings, "ota.env", env, (int)ecap, NVR_URL_OTA_ENV);
}

static int build_nvr_url(const nvr_cmd_ctx_t *c, char *out, size_t cap)
{
    char base[256], env[64], prod[64], model[48];
    ota_cfg(c, base, sizeof(base), env, sizeof(env));
    nvr_settings_get_str(c->settings, "ota.nvr_product", prod, sizeof(prod),
                         NVR_URL_OTA_NVR_PRODUCT);
    nvr_identity_get_model(model, sizeof(model));
    if (!model[0])
        nvr_settings_get_str(c->settings, "system.model", model, sizeof(model), NVR_DEF_MODEL);
    return nvr_ota_build_check_url(out, cap, base, env, prod, model);
}

static int build_cam_url(const nvr_cmd_ctx_t *c, const char *model,
                         char *out, size_t cap)
{
    char base[256], env[64], prod[64];
    ota_cfg(c, base, sizeof(base), env, sizeof(env));
    nvr_settings_get_str(c->settings, "ota.cam_product", prod, sizeof(prod),
                         NVR_URL_OTA_CAM_PRODUCT);
    if (!model || !model[0]) return -1;
    return nvr_ota_build_check_url(out, cap, base, env, prod, model);
}

static char *resp_check(int q, const nvr_ota_meta_t *meta, const char *cur_raw,
                        const char *model)
{
    char srv[64], cur[64];
    cJSON *o;
    int newer;
    if (q == -1) return nvr_resp_err(NOP_ERRSTR_NETWORK);
    if (q != 0)  return nvr_resp_err(NOP_ERRSTR_SERVER);
    nvr_ota_ver_norm(meta->version, model, srv, sizeof(srv));
    nvr_ota_ver_norm(cur_raw, model, cur, sizeof(cur));
    newer = (srv[0] && nvr_ota_ver_cmp(srv, cur) > 0) ? 1 : 0;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "newFW", newer ? 1 : 0);
    if (meta->version[0])
        cJSON_AddStringToObject(o, "version", meta->version);
    if (meta->description[0])
        cJSON_AddStringToObject(o, "description", meta->description);
    return nvr_resp_content(o);
}

static void cam_live_fw(const nvr_cmd_ctx_t *c, int chn0, const nvr_channel_t *ch,
                        char *out, size_t cap)
{
    out[0] = 0;
    if (ch->firmware[0])
        snprintf(out, cap, "%s", ch->firmware);
    if (ch->backend != 0) return;
    {
        char *resp = nvr_chan_dev_post(c->cm, chn0, "getDeviceInfo", "{}");
        cJSON *root, *content;
        const char *fw;
        if (!resp) return;
        root = cJSON_Parse(resp);
        free(resp);
        content = root ? cJSON_GetObjectItem(root, "content") : NULL;
        fw = content ? cJSON_GetStringValue(cJSON_GetObjectItem(content, "firmwareVersion")) : NULL;
        if (fw && fw[0]) snprintf(out, cap, "%s", fw);
        if (root) cJSON_Delete(root);
    }
}

/* 0=ok 绑定且可查；否则返回已构造的 error 应答。 */
static char *chan_ready(const nvr_cmd_ctx_t *c, int ch1, nvr_channel_t *ch)
{
    int code;
    if (ch1 < 1 || !c->cm) return nvr_resp_err("invalid_param");
    if (nvr_chan_get(c->cm, ch1 - 1, ch) != 0)
        return nvr_resp_err(NOP_ERRSTR_CAM_NOT_BINDED);
    code = nvr_chan_status_code_of(c->cm, ch1 - 1);
    if (code == 0) return nvr_resp_err(NOP_ERRSTR_CAM_NOT_BINDED);
    if (code != 1 && code != 6) return nvr_resp_err(NOP_ERRSTR_CAM_DISCONNECTED);
    return NULL;
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

char *cmd_GUI_checkServerFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    char url[512], cur[64], model[48];
    nvr_ota_meta_t meta;
    const char *override = nvr_jstr(a, "url", NULL);
    int q;

    nvr_settings_get_str(c->settings, "system.fw_version", cur, sizeof(cur),
                         NVR_DEF_FW_VERSION);
    nvr_identity_get_model(model, sizeof(model));
    if (override && override[0]) {
        snprintf(url, sizeof(url), "%s", override);
    } else if (build_nvr_url(c, url, sizeof(url)) != 0) {
        return nvr_resp_err(NOP_ERRSTR_SERVER);
    }
    NVR_LOGI("ota", "checkServerFirmware %s (cur=%s)", url, cur);
    q = nvr_ota_query(url, &meta);
    return resp_check(q, &meta, cur, model);
}

char *cmd_GUI_checkChannelServerFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    nvr_channel_t ch;
    char *err, url[512], cur[64];
    nvr_ota_meta_t meta;
    const char *override = nvr_jstr(a, "url", NULL);
    int q;

    err = chan_ready(c, ch1, &ch);
    if (err) return err;
    cam_live_fw(c, ch1 - 1, &ch, cur, sizeof(cur));
    if (override && override[0]) {
        snprintf(url, sizeof(url), "%s", override);
    } else if (build_cam_url(c, ch.model, url, sizeof(url)) != 0) {
        return nvr_resp_err(NOP_ERRSTR_SERVER);
    }
    NVR_LOGI("ota", "checkChannelServerFirmware ch%d model=%s %s",
             ch1, ch.model[0] ? ch.model : "?", url);
    q = nvr_ota_query(url, &meta);
    return resp_check(q, &meta, cur, ch.model);
}

static int usb_file(const char *storage, const char *name, char *out, size_t cap)
{
    const char *base = "/mnt/usb";
    struct stat st;
    if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\')) return -1;
    if (storage) {
        if (!strcmp(storage, "usb2")) base = "/mnt/usb2";
        else if (!strcmp(storage, "sdcard")) base = "/mnt/sdcard";
        else if (!strcmp(storage, "hdd") || !strcmp(storage, "hdd2")) return -1;
    }
    if (snprintf(out, cap, "%s/%s", base, name) >= (int)cap) return -1;
    if (stat(out, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return 0;
}

char *cmd_GUI_upgradeChannelFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    const char *fn = nvr_jstr(a, "FileName", NULL);
    const char *st = nvr_jstr(a, "storage", "usb");
    nvr_channel_t ch;
    nvr_ipc_ota_job_t job;
    char *err;
    int rc;

    if (!fn) return nvr_resp_err("invalid_param");
    err = chan_ready(c, ch1, &ch);
    if (err) return err;
    memset(&job, 0, sizeof(job));
    job.chn = ch1 - 1;
    job.ch = ch;
    job.cm = c->cm;
    job.nop = c->nop;
    job.automatic = 1;
    if (usb_file(st, fn, job.local_file, sizeof(job.local_file)) != 0)
        return nvr_resp_err("no_such_file");
    rc = nvr_ipc_ota_start(&job);
    if (rc == -2) return nvr_resp_err("upgrade_in_progress");
    if (rc != 0)  return nvr_resp_err("start_failed");
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_upgradeChannelFirmware(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    const char *url = nvr_jstr(a, "url", NULL);
    nvr_channel_t ch;
    nvr_ipc_ota_job_t job;
    char *err;
    int rc;

    err = chan_ready(c, ch1, &ch);
    if (err) return err;
    memset(&job, 0, sizeof(job));
    job.chn = ch1 - 1;
    job.ch = ch;
    job.cm = c->cm;
    job.nop = c->nop;
    job.automatic = nvr_jbool(a, "auto", 1);
    cam_live_fw(c, ch1 - 1, &ch, job.cur_ver, sizeof(job.cur_ver));
    if (url && url[0]) {
        snprintf(job.query_url, sizeof(job.query_url), "%s", url);
    } else if (build_cam_url(c, ch.model, job.query_url, sizeof(job.query_url)) != 0) {
        nvr_ipc_ota_set_status(ch1 - 1, 4, 0);
        return nvr_resp_ok();
    }
    rc = nvr_ipc_ota_start(&job);
    if (rc == -2) return nvr_resp_err("upgrade_in_progress");
    if (rc != 0) {
        nvr_ipc_ota_set_status(ch1 - 1, 2, 6);
        return nvr_resp_ok();
    }
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_checkChannelUpgradeStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    int err = 0, st;
    cJSON *o;
    (void)c;
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    st = nvr_ipc_ota_status(ch1 - 1, &err);
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "status", st);
    if (st == 2 && err > 0)
        cJSON_AddNumberToObject(o, "error", err);
    return nvr_resp_content(o);
}
