/***************************************************************************************
 *  nvr_onvif.c — ② ONVIF glue，基于 nop_sdk 的 nop_onvif_* 客户端。
 ***************************************************************************************/
#include "nvr_onvif.h"
#include "nop_sdk/nop_onvif.h"

#include <string.h>
#include <stdio.h>

static int g_inited = 0;

int nvr_onvif_init(void)
{
    if (g_inited) return 0;
    if (nop_onvif_global_init() != 0) return -1;
    g_inited = 1;
    return 0;
}

void nvr_onvif_cleanup(void)
{
    if (!g_inited) return;
    nop_onvif_global_cleanup();
    g_inited = 0;
}

/* app 的取流钩子（强符号，覆盖 nvr_app.c 的弱兜底） */
int nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                      const char *stream, char *out, int out_size)
{
    if (!ip || !out || out_size <= 0) return -1;
    if (nvr_onvif_init() != 0) return -1;

    nop_onvif_device_t *dev = nop_onvif_device_create(ip, port > 0 ? port : 80,
                                                      "/onvif/device_service", 0);
    if (!dev) return -1;
    if (user && user[0]) nop_onvif_device_set_auth(dev, user, pass ? pass : "");

    int rc = -1;
    int n = nop_onvif_get_profiles(dev);
    if (n > 0) {
        /* main → profile[0]，sub → profile[1]（若存在） */
        int idx = (stream && strcmp(stream, "sub") == 0 && n > 1) ? 1 : 0;
        nop_onvif_profile_t p;
        if (nop_onvif_get_profile(dev, idx, &p) == 0) {
            if (nop_onvif_get_stream_uri(dev, p.token, NOP_ONVIF_TRANSPORT_RTSP,
                                         out, (size_t)out_size) == 0 && out[0]) {
                rc = 0;
            } else if (p.stream_uri[0]) {           /* 回退：profiles 已带 uri */
                snprintf(out, out_size, "%s", p.stream_uri);
                rc = 0;
            }
        }
    }
    nop_onvif_device_destroy(dev);
    return rc;
}

/* 发现：把 nop 的 device_info 翻成 nvr_onvif_cam_t 回调 */
typedef struct { nvr_onvif_found_cb cb; void *user; } disc_ctx_t;

static void on_found(const nop_onvif_device_info_t *d, void *user)
{
    disc_ctx_t *c = (disc_ctx_t *)user;
    if (!c->cb) return;
    nvr_onvif_cam_t cam;
    memset(&cam, 0, sizeof(cam));
    snprintf(cam.host,   sizeof(cam.host),   "%s", d->host);
    cam.port = d->port;
    snprintf(cam.uuid,   sizeof(cam.uuid),   "%s", d->endpoint_reference);
    snprintf(cam.scopes, sizeof(cam.scopes), "%s", d->scopes);
    c->cb(&cam, c->user);
}

int nvr_onvif_discover(const char *local_ip, int seconds, nvr_onvif_found_cb cb, void *user)
{
    if (nvr_onvif_init() != 0) return -1;
    disc_ctx_t ctx = { cb, user };
    return nop_onvif_discover(local_ip, seconds > 0 ? seconds : 2, on_found, &ctx);
}
