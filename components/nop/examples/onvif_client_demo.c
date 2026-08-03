/**
 * @file onvif_client_demo.c
 * @brief Exercises the full extracted ONVIF client: discover devices, then for
 *        each one open a handle and query device info, media profiles + stream/
 *        snapshot URIs, and PTZ availability.
 *
 * Built only when NOP_WITH_ONVIF=ON. Pure C — uses only nop_sdk/nop_onvif.h;
 * the vendored ONVIF C++ library stays hidden behind the adapter.
 *
 *   ./onvif_client_demo [discovery_seconds] [username] [password]
 */
#include "nop_sdk/nop_onvif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_username = "admin";
static const char *g_password = "admin";

static void inspect_device(const nop_onvif_device_info_t *found)
{
    nop_onvif_device_t            *device;
    nop_onvif_device_information_t  info;
    nop_onvif_datetime_t           when;
    int                            profile_count, ptz_nodes, i;

    device = nop_onvif_device_create(found->host, found->port, found->service_url, 0);
    if (!device)
        return;
    nop_onvif_device_set_auth(device, g_username, g_password);

    if (nop_onvif_get_device_information(device, &info) == 0)
        printf("    info:     %s / %s / fw %s / sn %s\n",
               info.manufacturer, info.model, info.firmware_version, info.serial_number);
    else
        printf("    info:     (unavailable — check credentials: %s)\n",
               nop_onvif_device_last_error(device));

    if (nop_onvif_get_system_datetime(device, &when) == 0)
        printf("    time:     %04d-%02d-%02d %02d:%02d:%02d UTC\n",
               when.year, when.month, when.day, when.hour, when.minute, when.second);

    profile_count = nop_onvif_get_profiles(device);
    printf("    profiles: %d\n", profile_count);
    for (i = 0; i < profile_count; i++) {
        nop_onvif_profile_t profile;
        char                snapshot[300];
        if (nop_onvif_get_profile(device, i, &profile) != 0)
            continue;
        printf("      [%d] %s (%s)\n", i, profile.name, profile.token);
        printf("          rtsp:     %s\n", profile.stream_uri[0] ? profile.stream_uri : "(n/a)");
        if (nop_onvif_get_snapshot_uri(device, profile.token, snapshot, sizeof(snapshot)) == 0)
            printf("          snapshot: %s\n", snapshot);
    }

    ptz_nodes = nop_onvif_ptz_get_node_count(device);
    printf("    ptz:      %s (%d node(s))\n", ptz_nodes > 0 ? "yes" : "no",
           ptz_nodes > 0 ? ptz_nodes : 0);

    nop_onvif_device_destroy(device);
}

static void on_device_found(const nop_onvif_device_info_t *device, void *user)
{
    int *index = (int *)user;
    printf("[%d] %s:%d  %s\n", ++(*index), device->host, device->port, device->service_url);
    printf("    endpoint: %s\n", device->endpoint_reference);
    if (device->scopes[0])
        printf("    scopes:   %s\n", device->scopes);
    inspect_device(device);
    printf("\n");
}

int main(int argc, char **argv)
{
    int seconds = (argc > 1) ? atoi(argv[1]) : 5;
    int count = 0;

    if (argc > 2) g_username = argv[2];
    if (argc > 3) g_password = argv[3];

    if (nop_onvif_global_init() != 0) {
        fprintf(stderr, "onvif init failed\n");
        return 1;
    }

    printf("Discovering ONVIF devices for %d seconds ...\n\n", seconds);
    nop_onvif_discover(NULL, seconds, on_device_found, &count);
    printf("Done — %d device(s) found.\n", count);

    nop_onvif_global_cleanup();
    return 0;
}
