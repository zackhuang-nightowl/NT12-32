/***************************************************************************************
 *  nvr_onvif.c — ② ONVIF glue，基于 nop_sdk 的 nop_onvif_* 客户端。
 ***************************************************************************************/
#include "nvr_onvif.h"
#include "nop_sdk/nop_onvif.h"
#include "nop_sdk/nop_onvif_ext.h"   /* media caps / analytics rules 映射能力集 */
#include "nop_sdk/nop_log.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

#define NVR_ONVIF_LOG(...) NOP_LOGI(__VA_ARGS__)

static int g_inited = 0;

/* ================= 发现缓存(开机/周期"扫一次",按 IP 存 scopes) =================
 * 目的:每通道首解析原本各做一次 2s WS-Discovery probe,而 probe 被 g_onvif_mutex 全局串行
 * → 16 路开机要几十秒。改为**一次广播扫全部相机**缓存 scopes;各通道(含线程并发)解析时查缓存,
 * 命中即免 probe → 真正并发。缓存未命中才回落单路 probe(少见)。 */
#define NVR_DISC_MAX 64
typedef struct { char ip[64]; char scopes[1024]; char service_url[128]; int port; time_t ts; } disc_ent_t;
static disc_ent_t     g_disc[NVR_DISC_MAX];
static int            g_disc_n = 0;
static pthread_mutex_t g_disc_lock = PTHREAD_MUTEX_INITIALIZER;

static void disc_cache_put(const nop_onvif_device_info_t *d, void *user)
{
    (void)user;
    if (!d || !d->host[0]) return;
    pthread_mutex_lock(&g_disc_lock);
    int i, slot = -1;
    for (i = 0; i < g_disc_n; i++) if (strcmp(g_disc[i].ip, d->host) == 0) { slot = i; break; }
    if (slot < 0) { slot = (g_disc_n < NVR_DISC_MAX) ? g_disc_n++ : 0; memset(&g_disc[slot], 0, sizeof(g_disc[slot])); }
    snprintf(g_disc[slot].ip, sizeof(g_disc[slot].ip), "%s", d->host);
    if (d->scopes[0])      snprintf(g_disc[slot].scopes, sizeof(g_disc[slot].scopes), "%s", d->scopes);
    if (d->service_url[0]) snprintf(g_disc[slot].service_url, sizeof(g_disc[slot].service_url), "%s", d->service_url);
    if (d->port > 0)       g_disc[slot].port = d->port;
    g_disc[slot].ts = time(NULL);
    pthread_mutex_unlock(&g_disc_lock);
}

/* 一次广播(指定本机接口 local_ip;NULL=默认/LAN),把找到的所有相机 scopes 存入缓存。返回发现数。 */
int nvr_onvif_scan(const char *local_ip, int seconds)
{
    if (nvr_onvif_init() != 0) return -1;
    return nop_onvif_discover(local_ip, seconds > 0 ? seconds : 3, disc_cache_put, NULL);
}

/* 查缓存:命中且有 scopes → 回填并返回 0;未命中/无 scopes → -1(调用方回落 probe)。 */
int nvr_onvif_cached_scopes(const char *ip, char *scopes, int scopes_cap,
                            char *service_url, int svc_cap, int *port_io)
{
    if (!ip || !ip[0]) return -1;
    int rc = -1;
    pthread_mutex_lock(&g_disc_lock);
    for (int i = 0; i < g_disc_n; i++) {
        if (strcmp(g_disc[i].ip, ip) != 0) continue;
        if (g_disc[i].scopes[0]) {
            if (scopes && scopes_cap > 0)      snprintf(scopes, scopes_cap, "%s", g_disc[i].scopes);
            if (service_url && svc_cap > 0)    snprintf(service_url, svc_cap, "%s", g_disc[i].service_url);
            if (port_io && g_disc[i].port > 0) *port_io = g_disc[i].port;
            rc = 0;
        }
        break;
    }
    pthread_mutex_unlock(&g_disc_lock);
    return rc;
}

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
    char host[128]; int port; char service_url[128]; char scopes[1024];
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

/* 从若干 profile 里挑一个并取 RTSP 流 URL。media2=1 走 tr2，否则 trt。成功填 out 返 0。
 * vsrc_token 非空(多源设备)时:只在归属该 VideoSource 的 profile 里挑 主(第1个)/子(第2个)。 */
static int pick_stream_uri(nop_onvif_device_t *dev, int media2, int n, int want_sub,
                           const char *vsrc_token, char *out, int out_size)
{
    if (n <= 0) return -1;
    nop_onvif_profile_t p;
    int idx;
    if (vsrc_token && vsrc_token[0]) {
        int first = -1, second = -1;
        for (int i = 0; i < n; i++) {
            int r = media2 ? nop_onvif_get_profile2(dev, i, &p)
                           : nop_onvif_get_profile(dev, i, &p);
            if (r != 0 || strcmp(p.source_token, vsrc_token) != 0) continue;
            if (first < 0) first = i; else { second = i; break; }
        }
        if (first < 0) return -1;                      /* 指定源无 profile */
        idx = (want_sub && second >= 0) ? second : first;
    } else {
        idx = (want_sub && n > 1) ? 1 : 0;             /* 单源:全局主/子 */
    }
    int gp = media2 ? nop_onvif_get_profile2(dev, idx, &p)
                    : nop_onvif_get_profile(dev, idx, &p);
    if (gp != 0) return -1;
    int sr = media2 ? nop_onvif_get_stream_uri2(dev, p.token, out, (size_t)out_size)
                    : nop_onvif_get_stream_uri(dev, p.token, NOP_ONVIF_TRANSPORT_RTSP, out, (size_t)out_size);
    if (sr == 0 && out[0]) return 0;
    if (p.stream_uri[0]) { snprintf(out, out_size, "%s", p.stream_uri); return 0; }
    return -1;
}

int nvr_onvif_connect(const char *ip, int port, const char *service_url,
                      const char *user, const char *pass)
{
    char path[128];
    if (!ip || !ip[0]) return -1;
    if (nvr_onvif_init() != 0) return -1;
    path[0] = 0;
    if (service_url && service_url[0])
        snprintf(path, sizeof(path), "%s", service_url);
    if (!path[0]) {
        /* 池里还没有 path 时才广播一次，拿真实 device_service */
        char local_ip[64]; local_ip_for(ip, local_ip, sizeof(local_ip));
        find_dev_t f; memset(&f, 0, sizeof(f)); f.want_ip = ip;
        nop_onvif_discover(local_ip[0] ? local_ip : NULL, 2, on_probe_match, &f);
        if (f.found) {
            if (f.port > 0) port = f.port;
            snprintf(path, sizeof(path), "%s", f.service_url[0] ? f.service_url : "/onvif/device_service");
        }
    }
    int p = port > 0 ? port : 80;
    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, p,
                                                      path[0] ? path : "/onvif/device_service", 0);
    if (!dev) return -1;
    if (nop_onvif_device_connected(dev)) {
        nop_onvif_device_set_auth(dev, user, pass);   /* 空密 = 关 digest */
        nop_onvif_device_drop(ip, p);   /* 已有连接持有者，撤掉这次多余 retain */
        return 0;
    }
    int rc = nop_onvif_device_connect(dev, user, pass);
    if (rc != 0)
        nop_onvif_device_drop(ip, p);
    NVR_ONVIF_LOG("[onvif] connect %s:%d path=%s -> %s", ip, p,
                  path[0] ? path : "/onvif/device_service", rc == 0 ? "ok" : "FAIL");
    return rc;
}

int nvr_onvif_probe_scopes(const char *ip, char *scopes, int scopes_cap,
                           char *service_url, int svc_cap, int *port_io)
{
    find_dev_t f;
    char local_ip[64];
    if (!ip || !ip[0]) return -1;
    if (nvr_onvif_init() != 0) return -1;
    if (scopes && scopes_cap > 0) scopes[0] = 0;
    if (service_url && svc_cap > 0) service_url[0] = 0;
    memset(&f, 0, sizeof(f));
    f.want_ip = ip;
    local_ip_for(ip, local_ip, sizeof(local_ip));
    nop_onvif_discover(local_ip[0] ? local_ip : NULL, 2, on_probe_match, &f);
    if (!f.found) return -1;
    if (scopes && scopes_cap > 0)
        snprintf(scopes, scopes_cap, "%s", f.scopes);
    if (service_url && svc_cap > 0)
        snprintf(service_url, svc_cap, "%s", f.service_url);
    if (port_io && f.port > 0) *port_io = f.port;
    return 0;
}

int nvr_onvif_get_scopes(const char *ip, int port, const char *user, const char *pass,
                         char *out, int cap)
{
    nop_onvif_device_t *dev;
    int p, soap_ok = 0;
    if (!ip || !out || cap <= 0) return -1;
    out[0] = 0;
    if (nvr_onvif_init() != 0) return -1;
    nvr_onvif_probe_scopes(ip, out, cap, NULL, 0, NULL);
    p = port > 0 ? port : 80;
    dev = nop_onvif_device_retain(ip, p, NULL, 0);
    if (dev) {
        if (!nop_onvif_device_connected(dev) && user && user[0]) {
            nvr_onvif_connect(ip, p, NULL, user, pass);
            nop_onvif_device_drop(ip, p);
            dev = nop_onvif_device_retain(ip, p, NULL, 0);
        }
        if (dev && nop_onvif_device_connected(dev)) {
            char soap[1024];
            soap[0] = 0;
            nop_onvif_device_lock(dev);
            if (nop_onvif_get_scopes(dev, soap, sizeof(soap)) == 0 && soap[0]) {
                snprintf(out, cap, "%s", soap);
                soap_ok = 1;
            }
            nop_onvif_device_unlock(dev);
        }
        if (dev) nop_onvif_device_drop(ip, p);
    }
    (void)soap_ok;
    return out[0] ? 0 : -1;
}

void nvr_onvif_disconnect(const char *ip, int port)
{
    if (ip && ip[0])
        nop_onvif_device_drop(ip, port > 0 ? port : 80);
}

/* 取流：借连接时建好的 handle，用已缓存 URI。未连接则先 connect。不再每次 GetServices。 */
int nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                      const char *stream, char *out, int out_size,
                      char *scopes_out, int scopes_cap, const char *vsrc_token)
{
    if (!ip || !out || out_size <= 0) return -1;
    if (nvr_onvif_init() != 0) return -1;
    out[0] = 0;
    if (scopes_out && scopes_cap > 0) scopes_out[0] = 0;

    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, port > 0 ? port : 80, NULL, 0);
    if (!dev) return -1;
    if (!nop_onvif_device_connected(dev)) {
        if (nvr_onvif_connect(ip, port, NULL, user, pass) != 0) {
            nop_onvif_device_drop(ip, port > 0 ? port : 80);
            return -1;
        }
    } else {
        nop_onvif_device_set_auth(dev, user, pass);   /* 空密关 digest */
    }

    int want_sub = (stream && strcmp(stream, "sub") == 0);
    nop_onvif_device_lock(dev);
    int rc = nop_onvif_device_cached_uri(dev, want_sub, vsrc_token, out, (size_t)out_size);
    if (rc != 0)
        rc = pick_stream_uri(dev, 0, 8, want_sub, vsrc_token, out, out_size);
    if (rc != 0)
        rc = pick_stream_uri(dev, 1, 8, want_sub, vsrc_token, out, out_size);
    nop_onvif_device_unlock(dev);
    if (scopes_out && scopes_cap > 0) {
        char soap[1024];
        soap[0] = 0;
        nop_onvif_device_lock(dev);
        if (nop_onvif_get_scopes(dev, soap, sizeof(soap)) == 0 && soap[0])
            snprintf(scopes_out, scopes_cap, "%s", soap);
        nop_onvif_device_unlock(dev);
    }
    nop_onvif_device_drop(ip, port > 0 ? port : 80);   /* 配对本次 retain；连接仍由通道持有 */

    NVR_ONVIF_LOG("[onvif] get_url %s:%d %s%s%s -> %s",
                  ip, port > 0 ? port : 80, want_sub ? "sub" : "main",
                  (vsrc_token && vsrc_token[0]) ? " src=" : "",
                  (vsrc_token && vsrc_token[0]) ? vsrc_token : "",
                  rc == 0 ? out : "(FAIL)");
    return rc;
}

int nvr_onvif_get_mac(const char *ip, int port, const char *user, const char *pass,
                      char *out, int cap)
{
    int p, rc;
    nop_onvif_device_t *dev;
    if (!ip || !out || cap <= 0) return -1;
    out[0] = 0;
    if (nvr_onvif_init() != 0) return -1;
    p = port > 0 ? port : 80;
    dev = nop_onvif_device_retain(ip, p, NULL, 0);
    if (!dev) return -1;
    if (!nop_onvif_device_connected(dev)) {
        if (nvr_onvif_connect(ip, p, NULL, user, pass) != 0) {
            nop_onvif_device_drop(ip, p);
            return -1;
        }
    }
    nop_onvif_device_lock(dev);
    rc = nop_onvif_get_network_mac(dev, ip, out, cap);
    nop_onvif_device_unlock(dev);
    nop_onvif_device_drop(ip, p);
    NVR_ONVIF_LOG("[onvif] GetNetworkInterfaces mac %s:%d -> %s",
                  ip, p, rc == 0 ? out : "(FAIL)");
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

    if (nvr_onvif_connect(ip, f.port > 0 ? f.port : 80,
                          f.service_url[0] ? f.service_url : NULL, user, pass) != 0)
        return -1;
    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, f.port > 0 ? f.port : 80, NULL, 0);
    if (!dev) return -1;
    nop_onvif_device_lock(dev);

    nop_onvif_device_information_t di;
    if (nop_onvif_get_device_information(dev, &di) == 0) {
        snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", di.manufacturer);
        snprintf(out->model,        sizeof(out->model),        "%s", di.model);
        snprintf(out->firmware,     sizeof(out->firmware),     "%s", di.firmware_version);
        snprintf(out->serial,       sizeof(out->serial),       "%s", di.serial_number);
    }
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

    nop_onvif_device_cached_uri(dev, 0, NULL, out->main_uri, sizeof(out->main_uri));
    nop_onvif_device_cached_uri(dev, 1, NULL, out->sub_uri, sizeof(out->sub_uri));

    /* setTime 走 ONVIF:把 NVR 当前时间下发相机(所有设备统一) */
    out->time_set = (nop_onvif_set_system_datetime_now(dev) == 0) ? 1 : 0;

    NVR_ONVIF_LOG("[onvif] probe %s: model='%s' sn='%s' ptz=%d main=%s time_set=%d",
                  ip, out->model, out->serial, out->ptz, out->main_uri[0] ? "y" : "n", out->time_set);
    nop_onvif_device_unlock(dev);
    nop_onvif_device_drop(ip, f.port > 0 ? f.port : 80);
    return 0;
}

int nvr_onvif_set_time_now(const char *ip, int port, const char *user, const char *pass,
                           const nvr_onvif_time_cfg_t *tz_cfg)
{
    if (!ip) return -1;
    if (nvr_onvif_init() != 0) return -1;
    if (nvr_onvif_connect(ip, port, NULL, user, pass) != 0) return -1;
    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, port > 0 ? port : 80, NULL, 0);
    if (!dev) return -1;
    nop_onvif_device_lock(dev);
    int rc;
    if (tz_cfg && tz_cfg->tz_posix[0])
        rc = nop_onvif_set_system_datetime_cfg(dev, tz_cfg->tz_posix, tz_cfg->daylight);
    else
        rc = nop_onvif_set_system_datetime_now(dev);
    nop_onvif_device_unlock(dev);
    nop_onvif_device_drop(ip, port > 0 ? port : 80);
    return rc == 0 ? 0 : -1;
}

/* 多源按 VideoSourceToken 挑主 profile；单源用 index 0。 */
static int pick_profile_token(nop_onvif_device_t *dev, const char *vsrc_token,
                              char *tok, size_t tok_cap)
{
    nop_onvif_profile_t p;
    int n = nop_onvif_get_profiles(dev);
    if (n < 0) n = 0;
    if (n == 0) {
        n = nop_onvif_get_profiles2(dev);
        for (int i = 0; i < n; i++) {
            if (nop_onvif_get_profile2(dev, i, &p) != 0) continue;
            if (vsrc_token && vsrc_token[0] && strcmp(p.source_token, vsrc_token) != 0)
                continue;
            snprintf(tok, tok_cap, "%s", p.token);
            return tok[0] ? 0 : -1;
        }
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (nop_onvif_get_profile(dev, i, &p) != 0) continue;
        if (vsrc_token && vsrc_token[0] && strcmp(p.source_token, vsrc_token) != 0)
            continue;
        snprintf(tok, tok_cap, "%s", p.token);
        return tok[0] ? 0 : -1;
    }
    if (nop_onvif_get_profile(dev, 0, &p) == 0 && p.token[0]) {
        snprintf(tok, tok_cap, "%s", p.token);
        return 0;
    }
    return -1;
}

int nvr_onvif_get_snapshot(const char *ip, int port, const char *user, const char *pass,
                           const char *vsrc_token, unsigned char **out, int *out_len)
{
    if (!ip || !out || !out_len) return -1;
    *out = NULL; *out_len = 0;
    if (nvr_onvif_init() != 0) return -1;
    int p = port > 0 ? port : 80;
    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, p, NULL, 0);
    if (!dev) return -1;
    if (!nop_onvif_device_connected(dev)) {
        if (nvr_onvif_connect(ip, p, NULL, user, pass) != 0) {
            nop_onvif_device_drop(ip, p);
            return -1;
        }
    } else {
        nop_onvif_device_set_auth(dev, user, pass);
    }
    nop_onvif_device_set_timeout(dev, 4000);
    nop_onvif_device_lock(dev);
    char token[100]; token[0] = 0;
    int rc = pick_profile_token(dev, vsrc_token, token, sizeof(token));
    if (rc == 0)
        rc = nop_onvif_get_snapshot(dev, token, out, out_len);
    nop_onvif_device_unlock(dev);
    nop_onvif_device_drop(ip, p);
    if (rc != 0 || !out[0] || *out_len <= 0) {
        if (*out) { nop_onvif_free_buffer(*out); *out = NULL; }
        *out_len = 0;
        return -1;
    }
    return 0;
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
    snprintf(cam.service_url, sizeof(cam.service_url), "%s", d->service_url);
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
