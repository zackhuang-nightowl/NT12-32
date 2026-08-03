/**
 * @file ipc_demo.c
 * @brief Minimal IPC embed: register the stub HAL, create the app, and run a
 *        few NOP commands through the same dispatch path every transport uses.
 *
 * Replace hal_stub_register_all() with your firmware's real HAL tables
 * (hal_register(HAL_VIDEO, &my_video_if), ...) and the capabilities light up
 * automatically.
 */
#include "nop_sdk/nop_sdk.h"
#include "hal_stub.h"   /* demo-only convenience from ports/stub */

#include <stdio.h>
#include <string.h>

static void run(nop_app_t *app, const char *req)
{
    char *out = NULL;
    nop_app_dispatch(app, req, &out);
    printf("  >> %s\n  << %s\n\n", req, out ? out : "(null)");
    nop_app_free_response(out);
}

int main(void)
{
    nop_app_config_t cfg;
    nop_app_t       *app;

    printf("nop_sdk %s — IPC demo\n\n", nop_sdk_version());

    hal_stub_register_all();   /* firmware would register real HALs here */

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;
    app = nop_app_create(&cfg);
    if (!app) {
        fprintf(stderr, "failed to create app\n");
        return 1;
    }

    run(app, "{\"func\":\"getDeviceInfo\",\"args\":{}}");
    run(app, "{\"func\":\"X_NightOwl_getAPIVersion\",\"args\":{}}");
    run(app, "{\"func\":\"X_NightOwl_getDeviceCapabilities\",\"args\":{}}");
    run(app, "{\"func\":\"startLiveStream\",\"args\":{\"channel\":0,\"stream\":0}}");
    run(app, "{\"func\":\"ptzMove\",\"args\":{\"channel\":0,\"direction\":\"left\",\"speed\":40}}");
    run(app, "{\"func\":\"setChannelLightSwitch\",\"args\":{\"channel\":0,\"enable\":true}}");
    run(app, "{\"func\":\"X_NightOwl_getStorageInfo\",\"args\":{}}");
    run(app, "{\"func\":\"formatStorage\",\"args\":{\"value\":\"sdcard\"}}");
    run(app, "{\"func\":\"X_NightOwl_getChannelRecordingSwitch\",\"args\":{\"channel\":0}}");
    run(app, "{\"func\":\"X_NightOwl_getChannelContinuousRecordingSchedule\",\"args\":{\"channel\":0}}");
    run(app, "{\"func\":\"startSpeaker\",\"args\":{\"channel\":0,\"audioFormat\":{\"codec\":\"g711a\",\"sampleRate\":8000}}}");
    run(app, "{\"func\":\"checkFirmwareUpgradeStatus\",\"args\":{}}");
    run(app, "{\"func\":\"thisDoesNotExist\",\"args\":{}}");
    run(app, "{\"func\":\"batchCmd\",\"args\":{\"cmds\":["
             "{\"func\":\"getDeviceInfo\",\"args\":{}},"
             "{\"func\":\"X_NightOwl_getAPIVersion\",\"args\":{}}]}}");

    nop_app_destroy(app);
    return 0;
}
