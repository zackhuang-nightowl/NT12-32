/**
 * @file onvif_server_demo.c
 * @brief Brings the device-side ONVIF server up from SDK-supplied device
 *        identity, so any ONVIF client/NVR (e.g. ONVIF Device Manager) can
 *        discover and inspect this device. Pure C — uses only
 *        nop_sdk/nop_onvif_server.h; the vendored ONVIF server stays hidden
 *        behind the adapter.
 *
 * Built only when NOP_WITH_ONVIF_SERVER=ON.
 *
 *   ./onvif_server_demo [seconds] [http_port]
 *
 * With no args it starts on port 8080, runs ~3s (enough for a smoke test /
 * one discovery probe), then stops. Pass a larger [seconds] to leave it up for
 * a real client to connect.
 */
#include "nop_sdk/nop_onvif_server.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Stand-in for firmware's real HAL_SYSTEM. The ONVIF server's DeviceInformation
 * is sourced from this via nop_onvif_server_config_from_hal(). */
static nop_status_t demo_get_device_info(void *ctx, hal_device_info_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    strcpy(out->type,             "standaloneIpCamera");
    strcpy(out->model,            "NOP-CAM-7000");
    strcpy(out->firmware_version, "NOP-FW-2.3.1");
    strcpy(out->serial_number,    "SN0A1B2C3D");
    out->maximum_channel_count = 1;
    out->current_channel_count = 1;
    return NOP_OK;
}
static const hal_system_if g_demo_system = { demo_get_device_info, NULL, NULL, NULL };
#ifdef _WIN32
#  include <windows.h>
#  define sleep_seconds(s) Sleep((s) * 1000)
#else
#  include <unistd.h>
#  define sleep_seconds(s) sleep(s)
#endif

int main(int argc, char **argv)
{
    int                       seconds   = (argc > 1) ? atoi(argv[1]) : 3;
    int                       http_port = (argc > 2) ? atoi(argv[2]) : 8080;
    nop_onvif_server_config_t config;
    int                       rc;

    printf("nop_sdk ONVIF server demo (vendored %s)\n\n",
           nop_onvif_server_version());

    /* Firmware registers its HAL; the ONVIF server reads identity from it. */
    hal_register(HAL_SYSTEM, &g_demo_system);

    nop_onvif_server_config_init(&config);   /* main + sub profiles preset */
    strcpy(config.manufacturer, "NightOwl");
    nop_onvif_server_config_from_hal(&config); /* model/fw/serial/hw ← HAL_SYSTEM */
    config.http_port = http_port;
    strcpy(config.profiles[0].stream_uri, "rtsp://127.0.0.1:554/live/main");
    strcpy(config.profiles[1].stream_uri, "rtsp://127.0.0.1:554/live/sub");
    config.has_ptz = 1;   /* advertise a PTZ node bound to the main profile */

    rc = nop_onvif_server_start(&config);
    if (rc != 0) {
        fprintf(stderr, "onvif_server_start failed (rc=%d)\n", rc);
        return 1;
    }

    printf("ONVIF server up:\n");
    printf("  service:  http://<device-ip>:%d/onvif/device_service\n", http_port);
    printf("  identity: %s / %s / fw %s / sn %s\n",
           config.manufacturer, config.model,
           config.firmware_version, config.serial_number);
    {
        int i;
        for (i = 0; i < config.profile_count; i++)
            printf("  profile%d: %-10s %dx%d  stream=%s\n", i,
                   config.profiles[i].name,
                   config.profiles[i].video_width, config.profiles[i].video_height,
                   config.profiles[i].stream_uri);
    }
    printf("  ptz:      %s\n", config.has_ptz ? "yes" : "no");
    printf("  auth:     %s (%s)\n", config.need_auth ? "required" : "open",
           config.username);
    printf("  running:  %s\n\n", nop_onvif_server_is_running() ? "yes" : "no");

    /* ONVIF-events bridge smoke: attach a detection hub, publish a few events
     * (each mapped to an ONVIF topic and queued to any subscribed consumer),
     * then detach before stopping. Verifies attach/publish/detach wiring. */
    {
        nop_event_hub_t *event_hub = nop_event_hub_create();
        if (event_hub && nop_onvif_server_attach_event_hub(event_hub) == 0) {
            nop_event_t event;
            memset(&event, 0, sizeof(event));
            event.channel = 0;
            event.type = NOP_DETECT_MOTION;   /* → CellMotionDetector/Motion */
            nop_event_publish(event_hub, &event);
            event.type = NOP_DETECT_HUMAN;    /* → MyRuleDetector/PeopleDetect */
            nop_event_publish(event_hub, &event);
            event.type = NOP_DETECT_VEHICLE;  /* → MyRuleDetector/VehicleDetect */
            nop_event_publish(event_hub, &event);
            printf("  events:   bridge attached, published motion+human+vehicle\n");
            nop_onvif_server_detach_event_hub();
        }
        nop_event_hub_destroy(event_hub);
    }

    printf("\nDiscoverable via WS-Discovery for %d second(s); "
           "point an ONVIF client at the service URL above.\n", seconds);
    sleep_seconds(seconds);

    nop_onvif_server_stop();
    printf("\nStopped. running=%s\n",
           nop_onvif_server_is_running() ? "yes" : "no");
    return 0;
}
