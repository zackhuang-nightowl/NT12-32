/**
 * @file test_config.c
 * @brief Device provisioning config: parse JSON (identity / capability level /
 *        OTA / TUTK), then apply the capability level to an app and confirm
 *        gating follows the file.
 */
#include "nop_sdk/nop_config.h"
#include "nop_sdk/nop_sdk.h"

#include <stdio.h>
#include <string.h>

static const char *CONFIG_JSON =
    "{"
    "  \"device\": {\"manufacturer\":\"NightOwl\",\"model\":\"NOP-NVR-8CH\","
    "               \"firmwareVersion\":\"2.3.1\",\"serialNumber\":\"SN0A1B2C\","
    "               \"type\":\"videoRecorder\",\"maxChannelCount\":8},"
    "  \"httpPort\": 8089,"
    "  \"capabilities\": [\"device\",\"system\",\"ptz\",\"storage\"],"
    "  \"detection\": {\"supportedTypes\": [\"human\",\"face\",\"gunShot\"]},"
    "  \"ota\": {\"company\":\"NightOwl\",\"product\":\"NVR\",\"model\":\"WNVR-BTWN8\","
    "            \"urlBase\":\"https://kota.example/ota\",\"autoUpdate\":true},"
    "  \"tutk\": {\"enable\":true,\"uid\":\"ABCD1234EFGH5678IJKL\","
    "             \"authKey\":\"A1B2C3D4\",\"licenseKey\":\"LIC-XYZ\"}"
    "}";

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(void)
{
    nop_device_config_t cfg;
    nop_app_config_t    app_cfg;
    nop_app_t          *app;
    char               *out = NULL;

    nop_device_config_init(&cfg);
    if (nop_device_config_load_json(CONFIG_JSON, strlen(CONFIG_JSON), &cfg) != NOP_OK)
        return fail("load_json");

    /* identity */
    if (strcmp(cfg.model, "NOP-NVR-8CH") != 0)        return fail("model");
    if (strcmp(cfg.type, "videoRecorder") != 0)       return fail("type");
    if (cfg.max_channel_count != 8)                   return fail("maxChannelCount");
    if (cfg.http_port != 8089)                        return fail("httpPort");
    /* capability level */
    if (cfg.capability_count != 4)                    return fail("capability_count");
    /* OTA */
    if (strcmp(cfg.ota.model, "WNVR-BTWN8") != 0)     return fail("ota.model");
    if (!cfg.ota.auto_update)                         return fail("ota.autoUpdate");
    /* TUTK */
    if (!cfg.tutk.enable)                             return fail("tutk.enable");
    if (strcmp(cfg.tutk.uid, "ABCD1234EFGH5678IJKL") != 0) return fail("tutk.uid");
    /* detection types (enum-mapped) */
    if (cfg.detection_type_count != 3)               return fail("detection count");
    if (cfg.detection_types[0] != NOP_DETECT_HUMAN)  return fail("detection[0]");
    if (cfg.detection_types[2] != NOP_DETECT_GUN_SHOT) return fail("detection[2]");

    /* apply capability level (exclusive): ptz/storage on, light off */
    memset(&app_cfg, 0, sizeof(app_cfg));
    app_cfg.role = NOP_ROLE_IPC;
    app_cfg.auto_caps = 0;
    app = nop_app_create(&app_cfg);
    if (!app)
        return fail("app create");
    if (nop_device_config_apply_capabilities(app, &cfg, 1) != NOP_OK)
        return fail("apply caps");

    /* ptzMove is gated by CAP_PTZ (configured) -> not a 501 capability gate.
     * Without HAL it returns 501 from the handler, so check a configured cap
     * via a command whose cap is OFF instead: light was NOT configured. */
    nop_app_dispatch(app, "{\"func\":\"setChannelLightSwitch\",\"args\":{\"channel\":0,\"enable\":true}}", &out);
    if (!out || !strstr(out, "501"))
        return fail("unconfigured cap (light) should gate 501");
    nop_app_free_response(out);

    nop_app_destroy(app);
    printf("test_config: OK\n");
    return 0;
}
