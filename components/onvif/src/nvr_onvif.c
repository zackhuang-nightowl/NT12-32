/***************************************************************************************
 *  nvr_onvif.c — ② ONVIF glue，基于 nop_sdk 的 nop_onvif_* 客户端。
 ***************************************************************************************/
#include "nvr_onvif.h"
#include "nop_sdk/nop_onvif.h"
#include "nop_sdk/nop_onvif_ext.h"   /* media caps / analytics rules 映射能力集 */
#include "nop_sdk/nop_log.h"

#include <string.h>
#include <stdio.h>

#define NVR_ONVIF_LOG(...) NOP_LOGI(__VA_ARGS__)

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

/* 广播匹配：找到 host==want_ip 的设备，记下它真实的 device_service XAddr(host/port/path) */
typedef struct {
    const char *want_ip; int found;
    char host[128]; int port; char service_url[128]; char scopes[512];
} find_dev_t;

static void on_probe_match(const nop_onvif_device_info_t *d, void *user)
{
    find_dev_t *f = (find_dev_t *)user;
    if (f->found || !d->host[0]) return;
    if (strcmp(d->host, f->want_ip) != 0) return;
    f->found = 1;
    snprintf(f->host,        sizeof(f->host),        "%s", d->host);
    f->port = d->port;
    snprintf(f->service_url, sizeof(f->service_url), "%s", d->service_url);
    snprintf(f->scopes,      sizeof(f->scopes),      "%s", d->scopes);
}

/* 目标 IP → 广播用本机接口地址：PoE(198.18.<口>.x) → NVR 侧 198.18.<口>.100；否则默认接口(NULL) */
static void local_ip_for(const char *ip, char *out, size_t cap)
{
    int a, b, c, d; out[0] = 0;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 && a == 198 && b == 18)
        snprintf(out, cap, "198.18.%d.100", c);
}

/* 从若干 profile 里挑一个并取 RTSP 流 URL。media2=1 走 tr2，否则 trt。成功填 out 返 0。 */
static int pick_stream_uri(nop_onvif_device_t *dev, int media2, int n, int want_sub,
                           char *out, int out_size)
{
    if (n <= 0) return -1;
    nop_onvif_profile_t p;
    int idx = (want_sub && n > 1) ? 1 : 0;
    int gp = media2 ? nop_onvif_get_profile2(dev, idx, &p)
                    : nop_onvif_get_profile(dev, idx, &p);
    if (gp != 0) return -1;
    int sr = media2 ? nop_onvif_get_stream_uri2(dev, p.token, out, (size_t)out_size)
                    : nop_onvif_get_stream_uri(dev, p.token, NOP_ONVIF_TRANSPORT_RTSP, out, (size_t)out_size);
    if (sr == 0 && out[0]) return 0;
    if (p.stream_uri[0]) { snprintf(out, out_size, "%s", p.stream_uri); return 0; }
    return -1;
}

/* app 的取流钩子（强符号，覆盖 nvr_app.c 的弱兜底）。严格遵守 ONVIF 加设备流程：
 *   ① WS-Discovery 广播找到该 IP 的设备 → 拿真实 device_service XAddr(host/端口/路径)
 *   ② 据此构建 ONVIF_DEVICE（不猜 ip/port/path）
 *   ③ GetServices 取全部 service 链接（media1=ver10/media、media2=ver20/media…）
 *   ④ GetProfiles/GetStreamUri 严格发到各自 service 链接
 * 只一次尝试（有账号带鉴权，否则匿名）；上层限次+退避护 IPC 5 次错误上限。 */
int nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                      const char *stream, char *out, int out_size,
                      char *scopes_out, int scopes_cap)
{
    if (!ip || !out || out_size <= 0) return -1;
    if (nvr_onvif_init() != 0) return -1;
    out[0] = 0;
    if (scopes_out && scopes_cap > 0) scopes_out[0] = 0;
    (void)port;   /* 端口以广播结果为准，不用传入的猜测值 */

    /* ① 广播找设备，取真实 XAddr */
    char local_ip[64]; local_ip_for(ip, local_ip, sizeof(local_ip));
    find_dev_t f; memset(&f, 0, sizeof(f)); f.want_ip = ip;
    nop_onvif_discover(local_ip[0] ? local_ip : NULL, 2, on_probe_match, &f);
    /* 顺带回传 scopes(供通道分类 kind/mac);即使后续取流失败,分类信息也有用 */
    if (scopes_out && scopes_cap > 0 && f.found) snprintf(scopes_out, (size_t)scopes_cap, "%s", f.scopes);
    if (!f.found) {
        NVR_ONVIF_LOG("[onvif] get_url %s: 广播未发现设备(local=%s)",
                      ip, local_ip[0] ? local_ip : "default");
        return -1;
    }

    /* ② 用广播得到的 device_service XAddr 构建设备 */
    nop_onvif_device_t *dev = nop_onvif_device_create(
        f.host, f.port > 0 ? f.port : 80,
        f.service_url[0] ? f.service_url : "/onvif/device_service", 0);
    if (!dev) return -1;
    /* ONVIF 请求按 DB 账户密码鉴权:总带 WS-Security UsernameToken(用户名默认 admin,密码即使为空
     * 也带,空密码 digest 对空密码相机成立)。相机 media2/analytics 需鉴权,不带 token 会 401→能力空。 */
    nop_onvif_device_set_auth(dev, (user && user[0]) ? user : "admin", pass ? pass : "");

    /* ③ GetServices 取全部 service 链接 */
    int svc = nop_onvif_get_services(dev);
    int err = nop_onvif_last_error(dev);   /* -1连接 -4收超时 -6空 -7解析 */

    /* ④ media1(trt) 优先；无果再 media2(tr2, Profile T)。均按各自 service 链接。 */
    int want_sub = (stream && strcmp(stream, "sub") == 0);
    int n1 = nop_onvif_get_profiles(dev);
    int rc = pick_stream_uri(dev, 0, n1, want_sub, out, out_size);
    int n2 = -1;
    if (rc != 0) { out[0] = 0; n2 = nop_onvif_get_profiles2(dev);
                   rc = pick_stream_uri(dev, 1, n2, want_sub, out, out_size); }

    NVR_ONVIF_LOG("[onvif] get_url %s: disc(host=%s port=%d url=%s) services=%d(err=%d) media1=%d media2=%d -> %s",
                  ip, f.host, f.port, f.service_url, svc, err, n1, n2, rc == 0 ? out : "(FAIL)");
    nop_onvif_device_destroy(dev);
    return rc;
}

/* 首次上线探测:一次会话拿身份 + 主/子流 URI + PTZ 能力,并把 NVR 时间下发相机。 */
int nvr_onvif_probe(const char *ip, int port, const char *user, const char *pass, nvr_onvif_info_t *out)
{
    if (!ip || !out) return -1;
    if (nvr_onvif_init() != 0) return -1;
    memset(out, 0, sizeof(*out));
    (void)port;

    char local_ip[64]; local_ip_for(ip, local_ip, sizeof(local_ip));
    find_dev_t f; memset(&f, 0, sizeof(f)); f.want_ip = ip;
    nop_onvif_discover(local_ip[0] ? local_ip : NULL, 2, on_probe_match, &f);
    if (!f.found) { NVR_ONVIF_LOG("[onvif] probe %s: 广播未发现", ip); return -1; }
    snprintf(out->scopes, sizeof(out->scopes), "%s", f.scopes);

    nop_onvif_device_t *dev = nop_onvif_device_create(
        f.host, f.port > 0 ? f.port : 80,
        f.service_url[0] ? f.service_url : "/onvif/device_service", 0);
    if (!dev) return -1;
    /* ONVIF 请求按 DB 账户密码鉴权:总带 WS-Security UsernameToken(用户名默认 admin,密码即使为空
     * 也带,空密码 digest 对空密码相机成立)。相机 media2/analytics 需鉴权,不带 token 会 401→能力空。 */
    nop_onvif_device_set_auth(dev, (user && user[0]) ? user : "admin", pass ? pass : "");

    nop_onvif_device_information_t di;
    if (nop_onvif_get_device_information(dev, &di) == 0) {
        snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", di.manufacturer);
        snprintf(out->model,        sizeof(out->model),        "%s", di.model);
        snprintf(out->firmware,     sizeof(out->firmware),     "%s", di.firmware_version);
        snprintf(out->serial,       sizeof(out->serial),       "%s", di.serial_number);
    }
    nop_onvif_get_services(dev);
    nop_onvif_get_capabilities(dev);
    out->ptz = (nop_onvif_ptz_get_node_count(dev) > 0) ? 1 : 0;

    /* --- getDeviceCapabilities 能力集(NOPMappingONVIF.md):
     *   Media2 ConfigurationSet 标志 → mic/speaker/sensor/ptz;Analytics 规则 → motion/objectDetection;
     *   PTZ 子能力 → presets(nodes)/tours(patrol)/focus。 */
    nop_onvif_media_caps_t mc;
    if (nop_onvif_get_media_caps(dev, &mc) == 0) {
        out->cap_mic     = mc.mic;
        out->cap_speaker = (mc.audio_out && mc.audio_dec) ? 1 : 0;  /* AudioOutput + backchannel */
        out->cap_sensor  = mc.analytics;
        if (!out->ptz && mc.ptz) out->ptz = 1;
    }
    if (out->cap_sensor) {
        /* sensors[] 明细 = GetSupportedRules ∪ GetSupportedAnalyticsModules(能力发现,而非已配置实例)。 */
        nop_onvif_analytics_caps_t ac;
        if (nop_onvif_analytics_get_supported(dev, &ac) == 0) {
            out->cap_motion  = ac.motion;
            out->cap_objdet  = ac.objdet;
            out->obj_human   = ac.obj_human;
            out->obj_vehicle = ac.obj_vehicle;
            out->obj_animal  = ac.obj_animal;
            out->obj_face    = ac.obj_face;
            out->line_cross      = ac.line_cross;      out->line_max        = ac.line_max;
            out->line_max_points = ac.line_max_points; out->field_intrusion = ac.field_intrusion;
            out->field_max       = ac.field_max;       out->field_max_verts = ac.field_max_verts;
            /* objectDetection 探到但类别名未细分 → 给通用默认 human+vehicle(与参考样式一致)。 */
            if (out->cap_objdet && !out->obj_human && !out->obj_vehicle &&
                !out->obj_animal && !out->obj_face) { out->obj_human = 1; out->obj_vehicle = 1; }
        }
    }
    if (out->ptz) {
        nop_onvif_profile_t pf;
        if (nop_onvif_get_profile(dev, 0, &pf) == 0 && pf.token[0]) {
            nop_onvif_preset_t ps[4];
            if (nop_onvif_ptz_get_presets(dev, pf.token, ps, 4) > 0) out->ptz_presets = 1;
            nop_onvif_tour_t tr[2];
            if (nop_onvif_ptz_get_tours(dev, pf.token, tr, 2) > 0) out->ptz_tours = 1;
        }
        out->ptz_focus = 1;   /* 有 PTZ 一般含 Imaging 对焦(精确判定可加 GetImagingSettings) */
    }

    /* 主/子流 URI:media1 优先,空则 media2 */
    int n1 = nop_onvif_get_profiles(dev);
    int have_main = (pick_stream_uri(dev, 0, n1, 0, out->main_uri, sizeof(out->main_uri)) == 0);
    pick_stream_uri(dev, 0, n1, 1, out->sub_uri, sizeof(out->sub_uri));
    if (!have_main) { int n2 = nop_onvif_get_profiles2(dev);
        pick_stream_uri(dev, 1, n2, 0, out->main_uri, sizeof(out->main_uri));
        pick_stream_uri(dev, 1, n2, 1, out->sub_uri, sizeof(out->sub_uri)); }

    /* setTime 走 ONVIF:把 NVR 当前时间下发相机(所有设备统一) */
    out->time_set = (nop_onvif_set_system_datetime_now(dev) == 0) ? 1 : 0;

    NVR_ONVIF_LOG("[onvif] probe %s: model='%s' sn='%s' ptz=%d main=%s time_set=%d",
                  ip, out->model, out->serial, out->ptz, out->main_uri[0] ? "y" : "n", out->time_set);
    nop_onvif_device_destroy(dev);
    return 0;
}

int nvr_onvif_set_time_now(const char *ip, int port, const char *user, const char *pass)
{
    if (!ip) return -1;
    if (nvr_onvif_init() != 0) return -1;
    (void)port;
    char local_ip[64]; local_ip_for(ip, local_ip, sizeof(local_ip));
    find_dev_t f; memset(&f, 0, sizeof(f)); f.want_ip = ip;
    nop_onvif_discover(local_ip[0] ? local_ip : NULL, 2, on_probe_match, &f);
    if (!f.found) return -1;
    nop_onvif_device_t *dev = nop_onvif_device_create(
        f.host, f.port > 0 ? f.port : 80,
        f.service_url[0] ? f.service_url : "/onvif/device_service", 0);
    if (!dev) return -1;
    /* ONVIF 请求按 DB 账户密码鉴权:总带 WS-Security UsernameToken(用户名默认 admin,密码即使为空
     * 也带,空密码 digest 对空密码相机成立)。相机 media2/analytics 需鉴权,不带 token 会 401→能力空。 */
    nop_onvif_device_set_auth(dev, (user && user[0]) ? user : "admin", pass ? pass : "");
    int rc = nop_onvif_set_system_datetime_now(dev);
    nop_onvif_device_destroy(dev);
    return rc == 0 ? 0 : -1;
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
