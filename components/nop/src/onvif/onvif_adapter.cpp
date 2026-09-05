/**
 * @file onvif_adapter.cpp
 * @brief Calling layer over the vendored Happytimesoft ONVIF client library.
 *        Compiled as C++ (the library is C++), exposes a pure C ABI
 *        (nop_sdk/nop_onvif.h). The vendored sources in third_party/onvif/ are
 *        used UNMODIFIED — this file only orchestrates their public API.
 *
 * Coverage: runtime, discovery/monitor, device handle, device service
 * (info/capabilities/date-time/reboot/reset), media (profiles/stream &
 * snapshot URIs/snapshot/video sources), PTZ (move/stop/presets/home),
 * imaging (get/set), events (pull-point), firmware/system maintenance.
 */
#include "nop_sdk/nop_onvif.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "nop_sdk/nop_log.h"
#include "nop_sdk/osal/osal.h"

/* Vendored ONVIF client library headers (third_party/onvif). */
extern "C" {
#include "sys_inc.h"
#include "sys_buf.h"
#include "util.h"
#include "sys_log.h"
#include "http_parse.h"
#include "onvif.h"
#include "onvif_ver.h"
#include "onvif_cm.h"
#include "onvif_req.h"
#include "onvif_res.h"
#include "onvif_cln.h"
#include "onvif_api.h"
#include "onvif_probe.h"
}

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifndef MAX_DEV_NUMS
#  define MAX_DEV_NUMS 64
#endif

struct nop_onvif_device {
    ONVIF_DEVICE dev;
};

static int           g_onvif_ready = 0;
static osal_mutex_t *g_onvif_mutex = NULL;

/* Discovery/monitor trampoline state (one probe/monitor at a time). */
static nop_onvif_discovery_cb g_user_callback = NULL;
static void                  *g_user_data     = NULL;
static int                    g_discovered    = 0;

/* ======================================================================== */
/* Runtime                                                                  */
/* ======================================================================== */

int nop_onvif_global_init(void)
{
    if (g_onvif_ready)
        return 0;

    network_init();
    /* 不调 log_init()：vendored logger 在本平台 log_init 路径会崩(原作者注)，
     * 且 NULL fp 时 log_print_ex 自动 no-op，保持安静。排查取流用 nvr_onvif.c 里
     * 的 [onvif] get_url 摘要日志即可。 */
    sys_buf_init(10 * MAX_DEV_NUMS);
    http_msg_buf_init(10 * MAX_DEV_NUMS);

    g_onvif_mutex = osal_mutex_create();
    g_onvif_ready = 1;
    NOP_LOGI("onvif: client runtime initialized (%s)", ONVIF_CLIENT_VERSION);
    return 0;
}

void nop_onvif_global_cleanup(void)
{
    if (!g_onvif_ready)
        return;
    osal_mutex_destroy(g_onvif_mutex);
    g_onvif_mutex = NULL;
    g_onvif_ready = 0;
}

/* ======================================================================== */
/* Discovery                                                                */
/* ======================================================================== */

static void copy_scopes(const DEVICE_BINFO *info, char *out, size_t out_size)
{
    size_t used = 0;
    out[0] = '\0';
    for (uint32 scope_index = 0; scope_index < info->sizeScopes; scope_index++) {
        const char *item = info->scopes[scope_index].ScopeItem;
        size_t      len = strlen(item);
        if (used + len + 2 >= out_size)
            break;
        if (used > 0)
            out[used++] = ' ';
        memcpy(out + used, item, len);
        used += len;
        out[used] = '\0';
    }
}

static void probe_trampoline(DEVICE_BINFO *p_res, int msgtype, void *pdata)
{
    (void)pdata;
    if (msgtype != PROBE_MSGTYPE_MATCH && msgtype != PROBE_MSGTYPE_HELLO)
        return;

    nop_onvif_device_info_t device;
    memset(&device, 0, sizeof(device));
    strncpy(device.endpoint_reference, p_res->EndpointReference, sizeof(device.endpoint_reference) - 1);
    strncpy(device.host, p_res->XAddr.host, sizeof(device.host) - 1);
    device.port = p_res->XAddr.port;
    strncpy(device.service_url, p_res->XAddr.url, sizeof(device.service_url) - 1);
    copy_scopes(p_res, device.scopes, sizeof(device.scopes));

    g_discovered++;
    if (g_user_callback)
        g_user_callback(&device, g_user_data);
}

int nop_onvif_discover(const char *local_ip, int seconds,
                       nop_onvif_discovery_cb callback, void *user)
{
    int found;

    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return -1;
    if (seconds <= 0)
        seconds = 5;

    osal_mutex_lock(g_onvif_mutex);
    g_user_callback = callback;
    g_user_data     = user;
    g_discovered    = 0;

    set_probe_cb(probe_trampoline, NULL);
    start_probe(local_ip, seconds);
#ifdef _WIN32
    Sleep((DWORD)seconds * 1000);
#else
    sleep((unsigned)seconds);
#endif
    stop_probe();

    found = g_discovered;
    g_user_callback = NULL;
    g_user_data     = NULL;
    osal_mutex_unlock(g_onvif_mutex);
    return found;
}

int nop_onvif_monitor_start(nop_onvif_discovery_cb callback, void *user)
{
    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return -1;
    g_user_callback = callback;
    g_user_data     = user;
    set_probe_cb(probe_trampoline, NULL);
    start_probe(NULL, 30);
    return 0;
}

void nop_onvif_monitor_stop(void)
{
    stop_probe();
    g_user_callback = NULL;
    g_user_data     = NULL;
}

/* ======================================================================== */
/* Device handle                                                            */
/* ======================================================================== */

nop_onvif_device_t *nop_onvif_device_create(const char *host, int port,
                                            const char *service_url, int https)
{
    nop_onvif_device_t *handle;
    if (!host)
        return NULL;
    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return NULL;

    handle = (nop_onvif_device_t *)calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;

    onvif_initDevice(&handle->dev, host, port > 0 ? port : 80, https ? 1 : 0);
    if (service_url && service_url[0]) {
        strncpy(handle->dev.binfo.XAddr.url, service_url,
                sizeof(handle->dev.binfo.XAddr.url) - 1);
        handle->dev.binfo.XAddr.url[sizeof(handle->dev.binfo.XAddr.url) - 1] = '\0';
    }
    onvif_SetAuthMethod(&handle->dev, AuthMethod_UsernameToken);
    /* 单次 SOAP 上限。Happytime 每条都新建 TCP+Connection:close；失败时 5s 会让 OSD/编码体感卡住。 */
    onvif_SetReqTimeout(&handle->dev, 3000);
    handle->dev.events.init_term_time = 60;
    return handle;
}

void nop_onvif_device_set_auth(nop_onvif_device_t *device,
                               const char *username, const char *password)
{
    if (!device)
        return;
    if (!password || !password[0]) {
        onvif_SetAuthInfo(&device->dev, "", "");
        device->dev.needAuth = 0;   /* 无鉴权：不带 UsernameToken */
        return;
    }
    onvif_SetAuthInfo(&device->dev, username ? username : "admin", password);
    onvif_SetAuthMethod(&device->dev, AuthMethod_UsernameToken);
}

void nop_onvif_device_set_timeout(nop_onvif_device_t *device, int timeout_ms)
{
    if (device)
        onvif_SetReqTimeout(&device->dev, timeout_ms);
}

void nop_onvif_device_destroy(nop_onvif_device_t *device)
{
    if (!device)
        return;
    onvif_free_device(&device->dev);
    free(device);
}

const char *nop_onvif_device_last_error(nop_onvif_device_t *device)
{
    if (!device)
        return "";
    const char *err = onvif_GetErrString(&device->dev);
    return err ? err : "";
}

/* ======================================================================== */
/* One physical device = one handle (host:port). Built at connect.          */
/* ======================================================================== */

#define NOP_ONVIF_POOL_MAX 32
#define NOP_ONVIF_SRC_MAX  8
#define NOP_ONVIF_CACHE_OSD  8
#define NOP_ONVIF_CACHE_VENC 4
#define NOP_ONVIF_CACHE_MASK 8

typedef struct {
    nop_onvif_source_tokens_t tok;
    nop_onvif_dev_caps_t      caps;
    int  caps_ok;
    nop_onvif_ai_caps_t       ai;
    int  ai_ok;
    nop_onvif_osd_t  osd[NOP_ONVIF_CACHE_OSD];
    int  nosd;
    int  osd_ok;
    nop_onvif_venc_t venc[NOP_ONVIF_CACHE_VENC];
    int  nvenc;
    int  venc_ok;
    nop_onvif_mask_t mask[NOP_ONVIF_CACHE_MASK];
    int  nmask;
    int  mask_ok;
} nop_onvif_src_slot_t;

typedef struct {
    char host[128];
    int  port;
    int  https;
    char service_url[128];
    int  refcnt;
    int  endpoints_ready;
    int  profiles_ready;
    int  map_cache_ready;
    int  connected;
    nop_onvif_device_t *dev;
    osal_mutex_t       *lock;
    int  nsrc;
    nop_onvif_src_slot_t src[NOP_ONVIF_SRC_MAX];
    char media1_profile[100];
    char media2_profile[100];
    char main_uri[300];
    char sub_uri[300];
    char scopes[1024];
    int  scopes_ready;
} nop_onvif_pool_ent_t;

static nop_onvif_pool_ent_t g_devpool[NOP_ONVIF_POOL_MAX];
static osal_mutex_t        *g_devpool_lock;

static osal_mutex_t *pool_table_lock(void)
{
    if (!g_devpool_lock)
        g_devpool_lock = osal_mutex_create();
    return g_devpool_lock;
}

static int pool_port(int port)
{
    return port > 0 ? port : 80;
}

static nop_onvif_pool_ent_t *pool_find(const char *host, int port)
{
    int i, p = pool_port(port);
    if (!host || !host[0])
        return NULL;
    for (i = 0; i < NOP_ONVIF_POOL_MAX; i++) {
        if (g_devpool[i].dev && g_devpool[i].port == p &&
            strcmp(g_devpool[i].host, host) == 0)
            return &g_devpool[i];
    }
    return NULL;
}

static nop_onvif_pool_ent_t *pool_find_dev(const nop_onvif_device_t *dev)
{
    int i;
    if (!dev)
        return NULL;
    for (i = 0; i < NOP_ONVIF_POOL_MAX; i++)
        if (g_devpool[i].dev == dev)
            return &g_devpool[i];
    return NULL;
}

static nop_onvif_src_slot_t *slot_by_source(nop_onvif_pool_ent_t *e, const char *source_token)
{
    int i;
    if (!e || e->nsrc <= 0)
        return NULL;
    if (!source_token || !source_token[0])
        return &e->src[0];
    for (i = 0; i < e->nsrc; i++) {
        if (strcmp(e->src[i].tok.source_token, source_token) == 0)
            return &e->src[i];
    }
    return NULL;
}

static nop_onvif_src_slot_t *slot_by_vsc(nop_onvif_pool_ent_t *e, const char *vsc)
{
    int i;
    if (!e || !vsc || !vsc[0])
        return NULL;
    for (i = 0; i < e->nsrc; i++) {
        if (strcmp(e->src[i].tok.vsc_token, vsc) == 0)
            return &e->src[i];
    }
    return NULL;
}

static nop_onvif_src_slot_t *slot_by_cfg(nop_onvif_pool_ent_t *e, const char *cfg)
{
    int i;
    if (!e || !cfg || !cfg[0])
        return NULL;
    for (i = 0; i < e->nsrc; i++) {
        if (strcmp(e->src[i].tok.analytics_cfg, cfg) == 0)
            return &e->src[i];
    }
    return NULL;
}

static nop_onvif_src_slot_t *slot_by_key(nop_onvif_pool_ent_t *e, const char *key)
{
    nop_onvif_src_slot_t *s = slot_by_source(e, key);
    if (s)
        return s;
    s = slot_by_vsc(e, key);
    if (s)
        return s;
    return slot_by_cfg(e, key);
}

static void pool_fill_map_cache(nop_onvif_device_t *device, nop_onvif_pool_ent_t *e)
{
    nop_onvif_venc_t vencs[8];
    int i, nv;

    if (!device || !e || e->nsrc <= 0)
        return;
    if (!device->dev.ptz_node)
        nop_onvif_ptz_get_node_count(device);
    for (i = 0; i < e->nsrc; i++) {
        nop_onvif_src_slot_t *s = &e->src[i];
        nop_onvif_dev_caps_t  caps;
        nop_onvif_ai_caps_t   ai;
        nop_onvif_osd_t       osd[NOP_ONVIF_CACHE_OSD];
        nop_onvif_mask_t      mask[NOP_ONVIF_CACHE_MASK];
        int n;

        memset(&caps, 0, sizeof(caps));
        if (nop_onvif_get_device_caps(device, s->tok.source_token, &caps) == 0) {
            s->caps = caps;
            s->caps_ok = 1;
        }
        if (s->tok.analytics_cfg[0]) {
            memset(&ai, 0, sizeof(ai));
            if (nop_onvif_analytics_get_ai_caps(device, s->tok.analytics_cfg, &ai) == 0) {
                s->ai = ai;
                s->ai_ok = 1;
            }
        }
        if (s->tok.vsc_token[0]) {
            n = nop_onvif_media2_get_osds(device, osd, NOP_ONVIF_CACHE_OSD, s->tok.vsc_token);
            if (n >= 0) {
                int k;
                s->nosd = n > NOP_ONVIF_CACHE_OSD ? NOP_ONVIF_CACHE_OSD : n;
                for (k = 0; k < s->nosd; k++)
                    s->osd[k] = osd[k];
                s->osd_ok = 1;
            }
            n = nop_onvif_media2_get_masks(device, mask, NOP_ONVIF_CACHE_MASK, s->tok.vsc_token);
            if (n >= 0) {
                int k;
                s->nmask = n > NOP_ONVIF_CACHE_MASK ? NOP_ONVIF_CACHE_MASK : n;
                for (k = 0; k < s->nmask; k++)
                    s->mask[k] = mask[k];
                s->mask_ok = 1;
            }
        }
    }
    nv = nop_onvif_media2_get_vencs(device, vencs, 8);
    if (nv > 0)
        nop_onvif_device_venc_cache_ingest(device, vencs, nv);
    fprintf(stderr, "[onvif] map cache host=%s:%d nsrc=%d\n", e->host, e->port, e->nsrc);
}

nop_onvif_device_t *nop_onvif_device_retain(const char *host, int port,
                                            const char *service_url, int https)
{
    nop_onvif_pool_ent_t *e;
    nop_onvif_device_t   *d;
    int i, p = pool_port(port);
    osal_mutex_t *tl = pool_table_lock();

    if (!host || !host[0])
        return NULL;
    if (tl) osal_mutex_lock(tl);
    e = pool_find(host, p);
    if (e) {
        e->refcnt++;
        d = e->dev;
        if (tl) osal_mutex_unlock(tl);
        return d;
    }
    for (i = 0; i < NOP_ONVIF_POOL_MAX; i++) {
        if (g_devpool[i].dev)
            continue;
        d = nop_onvif_device_create(host, p,
                                    (service_url && service_url[0]) ? service_url
                                                                    : "/onvif/device_service",
                                    https);
        if (!d) {
            if (tl) osal_mutex_unlock(tl);
            return NULL;
        }
        memset(&g_devpool[i], 0, sizeof(g_devpool[i]));
        snprintf(g_devpool[i].host, sizeof(g_devpool[i].host), "%s", host);
        g_devpool[i].port = p;
        g_devpool[i].https = https ? 1 : 0;
        snprintf(g_devpool[i].service_url, sizeof(g_devpool[i].service_url), "%s",
                 (service_url && service_url[0]) ? service_url : "/onvif/device_service");
        g_devpool[i].refcnt = 1;
        g_devpool[i].dev = d;
        g_devpool[i].lock = osal_mutex_create();
        if (tl) osal_mutex_unlock(tl);
        return d;
    }
    if (tl) osal_mutex_unlock(tl);
    return NULL;
}

void nop_onvif_device_drop(const char *host, int port)
{
    nop_onvif_pool_ent_t *e;
    osal_mutex_t *tl = pool_table_lock();
    if (tl) osal_mutex_lock(tl);
    e = pool_find(host, port);
    if (e) {
        e->refcnt--;
        if (e->refcnt <= 0) {
            osal_mutex_t *lk = e->lock;
            nop_onvif_device_destroy(e->dev);
            memset(e, 0, sizeof(*e));
            if (lk) osal_mutex_destroy(lk);
        }
    }
    if (tl) osal_mutex_unlock(tl);
}

static int pool_pick_uri(nop_onvif_device_t *dev, int media2, int n, int want_sub,
                         const char *vsrc, char *out, size_t out_size)
{
    nop_onvif_profile_t p;
    int idx, i, first = -1, second = -1;
    if (n <= 0 || !out || out_size == 0)
        return -1;
    out[0] = 0;
    if (vsrc && vsrc[0]) {
        for (i = 0; i < n; i++) {
            int r = media2 ? nop_onvif_get_profile2(dev, i, &p)
                           : nop_onvif_get_profile(dev, i, &p);
            if (r != 0 || strcmp(p.source_token, vsrc) != 0)
                continue;
            if (first < 0) first = i;
            else { second = i; break; }
        }
        if (first < 0)
            return -1;
        idx = (want_sub && second >= 0) ? second : first;
    } else {
        idx = (want_sub && n > 1) ? 1 : 0;
    }
    if ((media2 ? nop_onvif_get_profile2(dev, idx, &p)
                : nop_onvif_get_profile(dev, idx, &p)) != 0)
        return -1;
    if (p.stream_uri[0]) {
        snprintf(out, (int)out_size, "%s", p.stream_uri);
        return 0;
    }
    return -1;
}

/* 轻量鉴权探针(试密码用):定位端点 + GetProfiles + **GetStreamUri** 双验。
 *   0=账密对(拿到取流 URL);-2=AUTH(401);-1=ERR(连不上/无 profile/取不到 URL)。
 * ★ 鉴权判定门必须是 GetStreamUri:有些设备 GetProfiles 宽松(弱/无鉴权也过),取流才强鉴权
 *   (如 nopOnvif 激活密码 P_act)——只判 GetProfiles 会选错凭据 → ONVIF 过了但 RTSP AUTHFAILED。
 * ★ set_auth 前置(有些设备 GetServices 也要鉴权)。**全程不缓存/不置 endpoints_ready/connected**:
 *   错密码不污染端点缓存;中标后由 nop_onvif_device_connect 用中标账密**干净完整重建**
 *   (取流 URL + scopes + 多源 = 验证完成后的"补充")。验证阶段只发 caps/services/profiles/streamUri。 */
int nop_onvif_device_try_auth(nop_onvif_device_t *device,
                              const char *username, const char *password)
{
    nop_onvif_pool_ent_t *e;
    int n1, n2;
    char uri[300];
    if (!device) return -1;
    e = pool_find_dev(device);
    if (e && e->lock) osal_mutex_lock(e->lock);
    nop_onvif_device_set_auth(device, (username && username[0]) ? username : "admin",
                              password ? password : "");   /* set_auth 前置 */
    device->dev.authFailed = 0;
    nop_onvif_get_capabilities(device);   /* 用本候选账密定位端点(不写 endpoints_ready,避免错密污染) */
    nop_onvif_get_services(device);
    n1 = nop_onvif_get_profiles(device);
    if (n1 <= 0 && device->dev.authFailed) { if (e && e->lock) osal_mutex_unlock(e->lock); return -2; }
    n2 = (n1 <= 0) ? nop_onvif_get_profiles2(device) : 1;
    if (n1 <= 0 && n2 <= 0) {
        int af = device->dev.authFailed;
        if (e && e->lock) osal_mutex_unlock(e->lock);
        return af ? -2 : -1;
    }
    /* ★ 真鉴权门:GetStreamUri 拿到 URL 才算账密对 */
    uri[0] = 0;
    if (n1 > 0) pool_pick_uri(device, 0, n1, 0, NULL, uri, sizeof(uri));
    if (!uri[0] && n2 > 0) pool_pick_uri(device, 1, n2, 0, NULL, uri, sizeof(uri));
    if (e && e->lock) osal_mutex_unlock(e->lock);
    if (!uri[0]) return device->dev.authFailed ? -2 : -1;
    return 0;
}

int nop_onvif_device_connect(nop_onvif_device_t *device,
                             const char *username, const char *password)
{
    nop_onvif_pool_ent_t *e;
    char toks[NOP_ONVIF_SRC_MAX][100];
    int ntok, i, n1, n2;

    if (!device)
        return -1;
    e = pool_find_dev(device);
    nop_onvif_device_set_auth(device, (username && username[0]) ? username : "admin",
                              password ? password : "");
    if (e && e->lock) osal_mutex_lock(e->lock);

    if (!e || !e->endpoints_ready) {
        nop_onvif_get_capabilities(device);
        nop_onvif_get_services(device);
        if (e) e->endpoints_ready = 1;
    }
    if (e && !e->scopes_ready) {
        char sc[1024];
        sc[0] = 0;
        if (nop_onvif_get_scopes(device, sc, sizeof(sc)) == 0 && sc[0]) {
            snprintf(e->scopes, sizeof(e->scopes), "%s", sc);
            e->scopes_ready = 1;
        }
    }
    if (!e || !e->profiles_ready) {
        n1 = nop_onvif_get_profiles(device);
        n2 = -1;
        if (n1 <= 0)
            n2 = nop_onvif_get_profiles2(device);
        if (n1 <= 0 && n2 <= 0) {
            if (e && e->lock) osal_mutex_unlock(e->lock);
            return device->dev.authFailed ? -2 : -1;
        }
        if (e) {
            nop_onvif_profile_t p;
            e->media1_profile[0] = 0;
            e->media2_profile[0] = 0;
            e->main_uri[0] = 0;
            e->sub_uri[0] = 0;
            if (n1 > 0 && nop_onvif_get_profile(device, 0, &p) == 0)
                snprintf(e->media1_profile, sizeof(e->media1_profile), "%s", p.token);
            if (n2 > 0 && nop_onvif_get_profile2(device, 0, &p) == 0)
                snprintf(e->media2_profile, sizeof(e->media2_profile), "%s", p.token);
            if (n1 > 0) {
                pool_pick_uri(device, 0, n1, 0, NULL, e->main_uri, sizeof(e->main_uri));
                pool_pick_uri(device, 0, n1, 1, NULL, e->sub_uri, sizeof(e->sub_uri));
            }
            if (!e->main_uri[0] && n2 > 0) {
                pool_pick_uri(device, 1, n2, 0, NULL, e->main_uri, sizeof(e->main_uri));
                pool_pick_uri(device, 1, n2, 1, NULL, e->sub_uri, sizeof(e->sub_uri));
            }
            ntok = nop_onvif_list_sources(device, toks, NOP_ONVIF_SRC_MAX);
            e->nsrc = 0;
            if (ntok < 0) ntok = 0;
            for (i = 0; i < ntok && e->nsrc < NOP_ONVIF_SRC_MAX; i++) {
                nop_onvif_source_tokens_t st;
                memset(&st, 0, sizeof(st));
                if (nop_onvif_resolve_source(device, toks[i], &st) == 0) {
                    e->src[e->nsrc].tok = st;
                    e->nsrc++;
                }
            }
            if (e->nsrc == 0) {
                nop_onvif_source_tokens_t st;
                if (nop_onvif_resolve_source(device, "", &st) == 0) {
                    e->src[e->nsrc].tok = st;
                    e->nsrc++;
                }
            }
            e->profiles_ready = 1;
        }
    }
    if (e && e->profiles_ready && !e->map_cache_ready && e->nsrc > 0) {
        pool_fill_map_cache(device, e);
        e->map_cache_ready = 1;
    }
    if (e) e->connected = 1;
    if (e && e->lock) osal_mutex_unlock(e->lock);
    return 0;
}

int nop_onvif_device_connected(const nop_onvif_device_t *device)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    return (e && e->connected) ? 1 : 0;
}

int nop_onvif_device_auth_failed(const nop_onvif_device_t *device)
{
    if (!device)
        return 0;
    return device->dev.authFailed ? 1 : 0;
}

void nop_onvif_device_lock(nop_onvif_device_t *device)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    if (e && e->lock) osal_mutex_lock(e->lock);
}

void nop_onvif_device_unlock(nop_onvif_device_t *device)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    if (e && e->lock) osal_mutex_unlock(e->lock);
}

int nop_onvif_device_cached_uri(nop_onvif_device_t *device, int want_sub,
                                const char *vsrc, char *out, size_t out_size)
{
    nop_onvif_pool_ent_t *e;
    nop_onvif_profile_t p;
    int n, i, first = -1, second = -1, idx;
    if (!device || !out || out_size == 0)
        return -1;
    out[0] = 0;
    e = pool_find_dev(device);
    if (e && e->profiles_ready && (!vsrc || !vsrc[0])) {
        const char *u = want_sub ? e->sub_uri : e->main_uri;
        if (u[0]) {
            snprintf(out, (int)out_size, "%s", u);
            return 0;
        }
    }
    n = 0;
    for (i = 0; nop_onvif_get_profile(device, i, &p) == 0; i++) n++;
    if (n > 0 && vsrc && vsrc[0]) {
        for (i = 0; i < n; i++) {
            if (nop_onvif_get_profile(device, i, &p) != 0) continue;
            if (strcmp(p.source_token, vsrc) != 0) continue;
            if (first < 0) first = i;
            else { second = i; break; }
        }
        if (first < 0) return -1;
        idx = (want_sub && second >= 0) ? second : first;
        if (nop_onvif_get_profile(device, idx, &p) == 0 && p.stream_uri[0]) {
            snprintf(out, (int)out_size, "%s", p.stream_uri);
            return 0;
        }
    }
    if (e && e->main_uri[0]) {
        snprintf(out, (int)out_size, "%s", want_sub && e->sub_uri[0] ? e->sub_uri : e->main_uri);
        return 0;
    }
    return -1;
}

int nop_onvif_device_cached_source(nop_onvif_device_t *device, const char *source_token,
                                   nop_onvif_source_tokens_t *out)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    int i;
    if (!device || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (!e || e->nsrc <= 0)
        return -1;
    if (!source_token || !source_token[0]) {
        *out = e->src[0].tok;
        return 0;
    }
    for (i = 0; i < e->nsrc; i++) {
        if (strcmp(e->src[i].tok.source_token, source_token) == 0) {
            *out = e->src[i].tok;
            return 0;
        }
    }
    return -1;
}

int nop_onvif_device_cached_nsrc(nop_onvif_device_t *device)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    return (e && e->nsrc > 0) ? e->nsrc : 0;
}

int nop_onvif_device_cached_source_at(nop_onvif_device_t *device, int index,
                                      nop_onvif_source_tokens_t *out)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    if (!device || !out || !e || index < 0 || index >= e->nsrc)
        return -1;
    *out = e->src[index].tok;
    return 0;
}

int nop_onvif_device_cached_caps(nop_onvif_device_t *device, const char *source_token,
                                 nop_onvif_dev_caps_t *out)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    if (!device || !out)
        return -1;
    s = slot_by_source(e, source_token);
    if (!s || !s->caps_ok)
        return -1;
    *out = s->caps;
    return 0;
}

int nop_onvif_device_caps_cache_put(nop_onvif_device_t *device, const char *source_token,
                                    const nop_onvif_dev_caps_t *caps)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    if (!device || !caps)
        return -1;
    s = slot_by_source(e, source_token);
    if (!s)
        return -1;
    s->caps = *caps;
    s->caps_ok = 1;
    return 0;
}

int nop_onvif_device_cached_ai(nop_onvif_device_t *device, const char *source_or_cfg,
                               nop_onvif_ai_caps_t *out)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    if (!device || !out)
        return -1;
    s = slot_by_key(e, source_or_cfg);
    if (!s || !s->ai_ok)
        return -1;
    *out = s->ai;
    return 0;
}

int nop_onvif_device_ai_cache_put(nop_onvif_device_t *device, const char *source_or_cfg,
                                  const nop_onvif_ai_caps_t *ai)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    if (!device || !ai)
        return -1;
    s = slot_by_key(e, source_or_cfg);
    if (!s)
        return -1;
    s->ai = *ai;
    s->ai_ok = 1;
    return 0;
}

int nop_onvif_device_cached_osds(nop_onvif_device_t *device, const char *key,
                                 nop_onvif_osd_t *out, int max)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int n, i;
    if (!device || !out || max <= 0)
        return -1;
    s = slot_by_key(e, key);
    if (!s || !s->osd_ok)
        return -1;
    n = s->nosd < max ? s->nosd : max;
    for (i = 0; i < n; i++)
        out[i] = s->osd[i];
    return n;
}

int nop_onvif_device_osd_cache_replace(nop_onvif_device_t *device, const char *key,
                                       const nop_onvif_osd_t *osds, int n)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int i;
    if (!device)
        return -1;
    s = slot_by_key(e, key);
    if (!s)
        return -1;
    if (n < 0) n = 0;
    if (n > NOP_ONVIF_CACHE_OSD) n = NOP_ONVIF_CACHE_OSD;
    s->nosd = n;
    for (i = 0; i < n; i++)
        s->osd[i] = osds[i];
    s->osd_ok = 1;
    return 0;
}

int nop_onvif_device_osd_cache_put(nop_onvif_device_t *device, const char *key,
                                   const nop_onvif_osd_t *osd)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int i;
    if (!device || !osd)
        return -1;
    s = slot_by_key(e, key);
    if (!s)
        return -1;
    if (!s->osd_ok) {
        s->nosd = 0;
        s->osd_ok = 1;
    }
    for (i = 0; i < s->nosd; i++) {
        if (osd->token[0] && strcmp(s->osd[i].token, osd->token) == 0) {
            s->osd[i] = *osd;
            return 0;
        }
    }
    if (s->nosd < NOP_ONVIF_CACHE_OSD) {
        s->osd[s->nosd++] = *osd;
        return 0;
    }
    return -1;
}

int nop_onvif_device_osd_cache_del(nop_onvif_device_t *device, const char *key,
                                   const char *osd_token)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int i;
    if (!device || !osd_token || !osd_token[0])
        return -1;
    s = slot_by_key(e, key);
    if (!s || !s->osd_ok)
        return -1;
    for (i = 0; i < s->nosd; i++) {
        if (strcmp(s->osd[i].token, osd_token) == 0) {
            int k;
            for (k = i; k < s->nosd - 1; k++)
                s->osd[k] = s->osd[k + 1];
            s->nosd--;
            return 0;
        }
    }
    return -1;
}

int nop_onvif_device_cached_vencs(nop_onvif_device_t *device, const char *source_token,
                                  nop_onvif_venc_t *out, int max)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int n, i;
    if (!device || !out || max <= 0)
        return -1;
    s = slot_by_source(e, source_token);
    if (!s || !s->venc_ok)
        return -1;
    n = s->nvenc < max ? s->nvenc : max;
    for (i = 0; i < n; i++)
        out[i] = s->venc[i];
    return n;
}

int nop_onvif_device_venc_cache_put(nop_onvif_device_t *device, const nop_onvif_venc_t *venc)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    int i, j;
    if (!device || !venc || !venc->token[0] || !e)
        return -1;
    for (i = 0; i < e->nsrc; i++) {
        nop_onvif_src_slot_t *s = &e->src[i];
        for (j = 0; j < s->nvenc; j++) {
            if (strcmp(s->venc[j].token, venc->token) == 0) {
                s->venc[j] = *venc;
                s->venc_ok = 1;
                return 0;
            }
        }
        if ((s->tok.main_venc[0] && strcmp(s->tok.main_venc, venc->token) == 0) ||
            (s->tok.sub_venc[0] && strcmp(s->tok.sub_venc, venc->token) == 0)) {
            if (s->nvenc < NOP_ONVIF_CACHE_VENC) {
                s->venc[s->nvenc++] = *venc;
                s->venc_ok = 1;
                return 0;
            }
        }
    }
    return -1;
}

int nop_onvif_device_venc_cache_ingest(nop_onvif_device_t *device,
                                       const nop_onvif_venc_t *vencs, int n)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    int i, k;
    if (!device || !vencs || n < 0 || !e)
        return -1;
    for (i = 0; i < e->nsrc; i++) {
        nop_onvif_src_slot_t *s = &e->src[i];
        s->nvenc = 0;
        for (k = 0; k < n && s->nvenc < NOP_ONVIF_CACHE_VENC; k++) {
            if ((s->tok.main_venc[0] && strcmp(vencs[k].token, s->tok.main_venc) == 0) ||
                (s->tok.sub_venc[0] && strcmp(vencs[k].token, s->tok.sub_venc) == 0))
                s->venc[s->nvenc++] = vencs[k];
        }
        if (s->nvenc == 0 && i == 0) {
            for (k = 0; k < n && s->nvenc < NOP_ONVIF_CACHE_VENC; k++)
                s->venc[s->nvenc++] = vencs[k];
        }
        s->venc_ok = 1;
    }
    return 0;
}

int nop_onvif_device_cached_masks(nop_onvif_device_t *device, const char *key,
                                  nop_onvif_mask_t *out, int max)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int n, i;
    if (!device || !out || max <= 0)
        return -1;
    s = slot_by_key(e, key);
    if (!s || !s->mask_ok)
        return -1;
    n = s->nmask < max ? s->nmask : max;
    for (i = 0; i < n; i++)
        out[i] = s->mask[i];
    return n;
}

int nop_onvif_device_mask_cache_replace(nop_onvif_device_t *device, const char *key,
                                        const nop_onvif_mask_t *masks, int n)
{
    nop_onvif_pool_ent_t *e = pool_find_dev(device);
    nop_onvif_src_slot_t *s;
    int i;
    if (!device)
        return -1;
    s = slot_by_key(e, key);
    if (!s)
        return -1;
    if (n < 0) n = 0;
    if (n > NOP_ONVIF_CACHE_MASK) n = NOP_ONVIF_CACHE_MASK;
    s->nmask = n;
    for (i = 0; i < n; i++)
        s->mask[i] = masks[i];
    s->mask_ok = 1;
    return 0;
}

/* ======================================================================== */
/* Device service                                                           */
/* ======================================================================== */

int nop_onvif_get_device_information(nop_onvif_device_t *device,
                                     nop_onvif_device_information_t *out)
{
    if (!device || !out)
        return -1;
    if (!GetDeviceInformation(&device->dev))
        return -2;
    const onvif_DeviceInformation *info = &device->dev.DeviceInformation;
    memset(out, 0, sizeof(*out));
    strncpy(out->manufacturer,     info->Manufacturer,    sizeof(out->manufacturer) - 1);
    strncpy(out->model,            info->Model,           sizeof(out->model) - 1);
    strncpy(out->firmware_version, info->FirmwareVersion, sizeof(out->firmware_version) - 1);
    strncpy(out->serial_number,    info->SerialNumber,    sizeof(out->serial_number) - 1);
    strncpy(out->hardware_id,      info->HardwareId,      sizeof(out->hardware_id) - 1);
    return 0;
}

int nop_onvif_get_scopes(nop_onvif_device_t *device, char *out, size_t cap)
{
    nop_onvif_pool_ent_t *e;
    tds_GetScopes_REQ req;
    tds_GetScopes_RES res;
    size_t used = 0;
    uint32 i;

    if (!device || !out || cap < 2)
        return -1;
    out[0] = 0;
    e = pool_find_dev(device);
    if (e && e->scopes_ready && e->scopes[0]) {
        snprintf(out, (int)cap, "%s", e->scopes);
        return 0;
    }
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (!onvif_tds_GetScopes(&device->dev, &req, &res))
        return -2;
    for (i = 0; i < res.sizeScopes; i++) {
        const char *item = res.Scopes[i].ScopeItem;
        size_t len;
        if (!item || !item[0])
            continue;
        len = strlen(item);
        if (used + len + 2 >= cap)
            break;
        if (used > 0)
            out[used++] = ' ';
        memcpy(out + used, item, len);
        used += len;
        out[used] = '\0';
    }
    if (e && out[0]) {
        snprintf(e->scopes, sizeof(e->scopes), "%s", out);
        e->scopes_ready = 1;
    }
    return out[0] ? 0 : -3;
}

int nop_onvif_get_capabilities(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return GetCapabilities(&device->dev) ? 0 : -2;
}

/* 最近一次 SOAP 调用的底层错误码（ONVIF_ERR_*：-1 连接失败 -4 收超时 -6 空内容 -7 解析失败）。 */
int nop_onvif_last_error(nop_onvif_device_t *device)
{
    return device ? device->dev.errCode : -100;
}

int nop_onvif_get_services(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return GetServices(&device->dev) ? 0 : -2;
}

int nop_onvif_get_system_datetime(nop_onvif_device_t *device, nop_onvif_datetime_t *out)
{
    if (!device || !out)
        return -1;
    if (!GetSystemDateAndTime(&device->dev))
        return -2;
    const onvif_DateTime *dt = &device->dev.devTime;
    out->year   = dt->Date.Year;
    out->month  = dt->Date.Month;
    out->day    = dt->Date.Day;
    out->hour   = dt->Time.Hour;
    out->minute = dt->Time.Minute;
    out->second = dt->Time.Second;
    return 0;
}

int nop_onvif_set_system_datetime_now(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return SetSystemDateAndTime(&device->dev) ? 0 : -2;
}

int nop_onvif_set_system_datetime_cfg(nop_onvif_device_t *device,
                                      const char *tz_posix, int daylight_savings)
{
    tds_SetSystemDateAndTime_REQ req;
    time_t nowtime;
    struct tm gm;

    if (!device || !tz_posix || !tz_posix[0]) return -1;
    memset(&req, 0, sizeof(req));
    nowtime = time(NULL);
    gmtime_r(&nowtime, &gm);

    req.SystemDateTime.DateTimeType = SetDateTimeType_Manual;
    req.SystemDateTime.DaylightSavings = daylight_savings ? TRUE : FALSE;
    snprintf(req.SystemDateTime.TimeZone.TZ, sizeof(req.SystemDateTime.TimeZone.TZ),
             "%s", tz_posix);
    req.SystemDateTime.TimeZoneFlag = 1;
    req.UTCDateTimeFlag = 1;
    req.UTCDateTime.Date.Year   = gm.tm_year + 1900;
    req.UTCDateTime.Date.Month  = gm.tm_mon + 1;
    req.UTCDateTime.Date.Day    = gm.tm_mday;
    req.UTCDateTime.Time.Hour   = gm.tm_hour;
    req.UTCDateTime.Time.Minute = gm.tm_min;
    req.UTCDateTime.Time.Second = gm.tm_sec;

    return onvif_tds_SetSystemDateAndTime(&device->dev, &req, NULL) ? 0 : -2;
}

int nop_onvif_reboot(nop_onvif_device_t *device)
{
    if (!device) return -1;
    tds_SystemReboot_REQ req;
    tds_SystemReboot_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    return onvif_tds_SystemReboot(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_factory_reset(nop_onvif_device_t *device, int hard)
{
    if (!device) return -1;
    tds_SetSystemFactoryDefault_REQ req;
    memset(&req, 0, sizeof(req));
    req.FactoryDefault = hard ? FactoryDefaultType_Hard : FactoryDefaultType_Soft;
    return onvif_tds_SetSystemFactoryDefault(&device->dev, &req, NULL) ? 0 : -2;
}

/* ======================================================================== */
/* Media                                                                    */
/* ======================================================================== */

int nop_onvif_get_profiles(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!GetProfiles(&device->dev))
        return -2;
    /* Resolve RTSP URIs so cached profiles carry a usable stream address. */
    GetStreamUris(&device->dev, TransportProtocol_RTSP);
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next)
        count++;
    return count;
}

int nop_onvif_get_profile(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out)
{
    int i = 0;
    if (!device || !out || index < 0)
        return -1;
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next, i++) {
        if (i == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->token, p->token, sizeof(out->token) - 1);
            strncpy(out->name, p->name, sizeof(out->name) - 1);
            strncpy(out->stream_uri, p->stream_uri, sizeof(out->stream_uri) - 1);
            /* 该 profile 归属的物理视频源(多源区分):取 VideoSourceConfiguration.SourceToken */
            if (p->v_src_cfg)
                strncpy(out->source_token, p->v_src_cfg->Configuration.SourceToken,
                        sizeof(out->source_token) - 1);
            return 0;
        }
    }
    return -2;
}

int nop_onvif_get_stream_uri(nop_onvif_device_t *device, const char *profile_token,
                             nop_onvif_transport_t proto, char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    if (!GetStreamUris(&device->dev, (onvif_TransportProtocol)proto))
        return -2;
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
        if (strcmp(p->token, profile_token) == 0) {
            strncpy(out_uri, p->stream_uri, out_size - 1);
            out_uri[out_size - 1] = '\0';
            return 0;
        }
    }
    return -3;
}

/* ---- Media2 (ver20 / Profile T)：部分相机只在 media2 返回 profiles/stream uri ---- */
int nop_onvif_get_profiles2(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!tr2_GetProfiles(&device->dev))
        return -2;
    /* Media2 GetStreamUri Protocol=RTSP（RTP interleaved over TCP）；不用 RtspUnicast。 */
    tr2_GetStreamUris(&device->dev, "RTSP");
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next)
        count++;
    return count;
}

int nop_onvif_get_profile2(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out)
{
    int i = 0;
    if (!device || !out || index < 0)
        return -1;
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next, i++) {
        if (i == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->token, p->MediaProfile.token, sizeof(out->token) - 1);
            strncpy(out->name, p->MediaProfile.Name, sizeof(out->name) - 1);
            strncpy(out->stream_uri, p->MediaProfile.stream_uri, sizeof(out->stream_uri) - 1);
            return 0;
        }
    }
    return -2;
}

int nop_onvif_get_stream_uri2(nop_onvif_device_t *device, const char *profile_token,
                              char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    if (!tr2_GetStreamUris(&device->dev, "RTSP"))
        return -2;
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
        if (strcmp(p->MediaProfile.token, profile_token) == 0) {
            strncpy(out_uri, p->MediaProfile.stream_uri, out_size - 1);
            out_uri[out_size - 1] = '\0';
            return 0;
        }
    }
    return -3;
}

int nop_onvif_get_snapshot_uri(nop_onvif_device_t *device, const char *profile_token,
                               char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    trt_GetSnapshotUri_REQ req;
    trt_GetSnapshotUri_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_trt_GetSnapshotUri(&device->dev, &req, &res))
        return -2;
    strncpy(out_uri, res.Uri, out_size - 1);
    out_uri[out_size - 1] = '\0';
    return 0;
}

int nop_onvif_get_snapshot(nop_onvif_device_t *device, const char *profile_token,
                           unsigned char **out_buf, int *out_len)
{
    if (!device || !profile_token || !out_buf || !out_len)
        return -1;
    return GetSnapshot(&device->dev, profile_token, out_buf, out_len) ? 0 : -2;
}

void nop_onvif_free_buffer(void *buf)
{
    if (buf)
        FreeBuff(buf);
}

int nop_onvif_get_video_source_count(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (device->dev.v_src) {
        for (VideoSourceList *v = device->dev.v_src; v; v = v->next)
            count++;
        return count;
    }
    if (!GetVideoSources(&device->dev))
        return -2;
    for (VideoSourceList *v = device->dev.v_src; v; v = v->next)
        count++;
    return count;
}

/* ======================================================================== */
/* PTZ                                                                      */
/* ======================================================================== */

int nop_onvif_ptz_get_node_count(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!GetNodes(&device->dev))
        return -2;
    for (PTZNodeList *n = device->dev.ptz_node; n; n = n->next)
        count++;
    return count;
}

static void fill_ptz_speed(onvif_PTZSpeed *speed, float pan, float tilt, float zoom)
{
    memset(speed, 0, sizeof(*speed));
    speed->PanTiltFlag = 1;
    speed->PanTilt.x = pan;
    speed->PanTilt.y = tilt;
    speed->ZoomFlag = 1;
    speed->Zoom.x = zoom;
}

static void fill_ptz_vector(onvif_PTZVector *vector, float pan, float tilt, float zoom)
{
    memset(vector, 0, sizeof(*vector));
    vector->PanTiltFlag = 1;
    vector->PanTilt.x = pan;
    vector->PanTilt.y = tilt;
    vector->ZoomFlag = 1;
    vector->Zoom.x = zoom;
}

int nop_onvif_ptz_continuous_move(nop_onvif_device_t *device, const char *profile_token,
                                  float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_ContinuousMove_REQ req;
    ptz_ContinuousMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_speed(&req.Velocity, pan, tilt, zoom);
    return onvif_ptz_ContinuousMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_stop(nop_onvif_device_t *device, const char *profile_token,
                       int stop_pan_tilt, int stop_zoom)
{
    if (!device || !profile_token) return -1;
    ptz_Stop_REQ req;
    ptz_Stop_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    req.PanTiltFlag = 1; req.PanTilt = stop_pan_tilt ? TRUE : FALSE;
    req.ZoomFlag = 1;    req.Zoom = stop_zoom ? TRUE : FALSE;
    return onvif_ptz_Stop(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_relative_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_RelativeMove_REQ req;
    ptz_RelativeMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_vector(&req.Translation, pan, tilt, zoom);
    return onvif_ptz_RelativeMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_absolute_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_AbsoluteMove_REQ req;
    ptz_AbsoluteMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_vector(&req.Position, pan, tilt, zoom);
    return onvif_ptz_AbsoluteMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_home(nop_onvif_device_t *device, const char *profile_token)
{
    if (!device || !profile_token) return -1;
    ptz_GotoHomePosition_REQ req;
    ptz_GotoHomePosition_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    return onvif_ptz_GotoHomePosition(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_home_speed(nop_onvif_device_t *device,
                                  const char *profile_token, float speed)
{
    if (!device || !profile_token) return -1;
    ptz_GotoHomePosition_REQ req;
    ptz_GotoHomePosition_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (speed > 0.0f) {
        if (speed > 1.0f) speed = 1.0f;
        req.SpeedFlag       = 1;
        req.Speed.PanTiltFlag = 1;
        req.Speed.PanTilt.x = speed;
        req.Speed.PanTilt.y = speed;
        req.Speed.ZoomFlag  = 1;
        req.Speed.Zoom.x    = speed;
    }
    return onvif_ptz_GotoHomePosition(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_preset(nop_onvif_device_t *device, const char *profile_token,
                              const char *preset_token)
{
    if (!device || !profile_token || !preset_token) return -1;
    ptz_GotoPreset_REQ req;
    ptz_GotoPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetToken, preset_token, sizeof(req.PresetToken) - 1);
    return onvif_ptz_GotoPreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_set_preset(nop_onvif_device_t *device, const char *profile_token,
                             const char *preset_name)
{
    if (!device || !profile_token) return -1;
    ptz_SetPreset_REQ req;
    ptz_SetPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (preset_name && preset_name[0]) {
        req.PresetNameFlag = 1;
        strncpy(req.PresetName, preset_name, sizeof(req.PresetName) - 1);
    }
    return onvif_ptz_SetPreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_remove_preset(nop_onvif_device_t *device, const char *profile_token,
                                const char *preset_token)
{
    if (!device || !profile_token || !preset_token) return -1;
    ptz_RemovePreset_REQ req;
    ptz_RemovePreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetToken, preset_token, sizeof(req.PresetToken) - 1);
    return onvif_ptz_RemovePreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_get_presets(nop_onvif_device_t *device, const char *profile_token,
                              nop_onvif_preset_t *out_array, int max)
{
    if (!device || !profile_token || !out_array || max <= 0)
        return -1;
    ptz_GetPresets_REQ req;
    ptz_GetPresets_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_ptz_GetPresets(&device->dev, &req, &res))
        return -2;

    int count = 0;
    for (PTZPresetList *node = res.PTZPresets; node && count < max; node = node->next) {
        memset(&out_array[count], 0, sizeof(out_array[count]));
        strncpy(out_array[count].token, node->PTZPreset.token, sizeof(out_array[count].token) - 1);
        strncpy(out_array[count].name, node->PTZPreset.Name, sizeof(out_array[count].name) - 1);
        count++;
    }
    onvif_free_PTZPresets(&res.PTZPresets);
    return count;
}

/* ======================================================================== */
/* Imaging                                                                  */
/* ======================================================================== */

int nop_onvif_get_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   nop_onvif_imaging_t *out)
{
    if (!device || !video_source_token || !out)
        return -1;
    img_GetImagingSettings_REQ req;
    img_GetImagingSettings_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);
    if (!onvif_img_GetImagingSettings(&device->dev, &req, &res))
        return -2;

    const onvif_ImagingSettings *s = &res.ImagingSettings;
    memset(out, 0, sizeof(*out));
    out->has_brightness       = s->BrightnessFlag;       out->brightness       = s->Brightness;
    out->has_contrast         = s->ContrastFlag;         out->contrast         = s->Contrast;
    out->has_color_saturation = s->ColorSaturationFlag;  out->color_saturation = s->ColorSaturation;
    out->has_sharpness        = s->SharpnessFlag;        out->sharpness        = s->Sharpness;
    return 0;
}

int nop_onvif_set_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   const nop_onvif_imaging_t *in)
{
    if (!device || !video_source_token || !in)
        return -1;
    img_SetImagingSettings_REQ req;
    img_SetImagingSettings_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);

    onvif_ImagingSettings *s = &req.ImagingSettings;
    if (in->has_brightness)       { s->BrightnessFlag = 1;      s->Brightness = in->brightness; }
    if (in->has_contrast)         { s->ContrastFlag = 1;        s->Contrast = in->contrast; }
    if (in->has_color_saturation) { s->ColorSaturationFlag = 1; s->ColorSaturation = in->color_saturation; }
    if (in->has_sharpness)        { s->SharpnessFlag = 1;       s->Sharpness = in->sharpness; }
    return onvif_img_SetImagingSettings(&device->dev, &req, &res) ? 0 : -2;
}

/* ======================================================================== */
/* Events (pull-point)                                                      */
/* ======================================================================== */

int nop_onvif_events_create_pullpoint(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return CreatePullPointSubscription(&device->dev) ? 0 : -2;
}

int nop_onvif_events_pull(nop_onvif_device_t *device, int timeout_s, int max_messages)
{
    if (!device) return -1;
    if (timeout_s <= 0)   timeout_s = 1;
    if (max_messages <= 0) max_messages = 10;

    tev_PullMessages_RES res;
    memset(&res, 0, sizeof(res));
    if (!PullMessages(&device->dev, timeout_s, max_messages, &res))
        return -2;

    int count = 0;
    for (NotificationMessageList *m = res.NotifyMessages; m; m = m->next)
        count++;
    onvif_free_NotificationMessages(&res.NotifyMessages);
    return count;
}

int nop_onvif_events_unsubscribe(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return Unsubscribe(&device->dev) ? 0 : -2;
}

/* ======================================================================== */
/* Firmware / system maintenance                                            */
/* ======================================================================== */

int nop_onvif_firmware_upgrade(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return FirmwareUpgrade(&device->dev, filename) ? 0 : -2;
}

int nop_onvif_system_backup(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return SystemBackup(&device->dev, filename) ? 0 : -2;
}

int nop_onvif_system_restore(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return SystemRestore(&device->dev, filename) ? 0 : -2;
}
