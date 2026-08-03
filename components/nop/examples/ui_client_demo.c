/**
 * @file ui_client_demo.c
 * @brief Models the on-NVR LVGL UI process driving the NOP backend as a client.
 *
 * Shows both client channels against the same nop_app server:
 *   1) loopback   — UI and backend in one process (or host test)
 *   2) unix socket — UI as a SEPARATE process over /tmp/nop_ui_demo.sock
 *      (here the backend server thread + client live in one process for the
 *       demo, but the socket path is exactly what a separate process uses)
 */
#include "nop_sdk/nop_sdk.h"
#include "nop_sdk/nop_client.h"
#include "nop_sdk/nop_transport_loopback.h"
#include "nop_sdk/nop_transport_unix.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>

static void ui_call(nop_client_t *client, const char *func, const char *args)
{
    int   status = 0;
    char *content = NULL;
    nop_status_t rc = nop_client_call_status(client, func, args, &status, &content);
    if (rc != NOP_OK) {
        printf("  %-34s transport error (%d)\n", func, (int)rc);
        return;
    }
    printf("  %-34s status=%d content=%s\n", func, status, content ? content : "{}");
    nop_client_free_response(content);
}

static void run_session(nop_client_t *client, const char *label)
{
    printf("[%s]\n", label);
    ui_call(client, "getDeviceInfo", NULL);
    ui_call(client, "X_NightOwl_getDeviceCapabilities", NULL);
    ui_call(client, "getDeviceDisplayMode", NULL);
    ui_call(client, "ptzMove", "{\"channel\":0,\"direction\":\"up\",\"speed\":30}");
    ui_call(client, "getChannelFloodLightSwitch", "{\"channel\":0}");
    printf("\n");
}

int main(void)
{
    nop_app_config_t      cfg;
    nop_app_t            *app;
    nop_client_channel_if channel;
    nop_client_t         *client;
    const char           *sock = "/tmp/nop_ui_demo.sock";
    nop_unix_server_t    *server;

    /* --- NVR backend (NOP server) --- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;
    hal_stub_register_all();
    app = nop_app_create(&cfg);
    if (!app) { fprintf(stderr, "app create failed\n"); return 1; }

    printf("NOP client demo — UI process driving the NVR backend\n\n");

    /* 1) loopback (same process) */
    nop_channel_loopback_init(&channel, app);
    client = nop_client_create(&channel);
    run_session(client, "loopback channel");
    nop_client_destroy(client);

    /* 2) unix socket (the separate-process path) */
    server = nop_unix_server_start(sock, app);
    if (!server) { fprintf(stderr, "unix server start failed\n"); nop_app_destroy(app); return 1; }
    if (nop_channel_unix_connect(&channel, sock) != 0) {
        fprintf(stderr, "unix connect failed\n");
        nop_unix_server_stop(server); nop_app_destroy(app); return 1;
    }
    client = nop_client_create(&channel);
    run_session(client, "unix socket channel");
    nop_client_destroy(client);
    nop_channel_unix_close(&channel);
    nop_unix_server_stop(server);

    nop_app_destroy(app);
    return 0;
}
