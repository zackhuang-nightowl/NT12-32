/***************************************************************************************
 *  nvr_cmd_router.c — NVR 命令路由(表驱动)。见 nvr_cmd_router.h / nvr_cmd_internal.h。
 *
 *  分派次序(nvr_cmd_dispatch):
 *    1) 黑名单 = 本地路由表 g_nvr_cmd_table[]:命中 → 本地 handler 处理。
 *    2) 非黑名单 → 按 camera.backend 分流:
 *         backend==0 (NOP/kind0)     → 改写 args.channel=1(设备侧) → 透传 POST 设备 /APPJsonCmd
 *         backend!=0 (nopOnvif/onvif) → nop_app_dispatch(mappingonvif 前门 is_onvif→onvif_map_dispatch)
 *       通道找不到设备 → 501。
 *    3) 无 channel 且非黑名单 → 回落 nop_app_dispatch → 501。
 ***************************************************************************************/
#include "nvr_cmd_router.h"
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_log.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nvr_cmd_router {
    nvr_cmd_router_cfg_t cfg;
    nvr_cmd_ctx_t        ctx;      /* handler 上下文(由 cfg 装配) */
};

/* ------------------------- 转发到设备（NOP 透传） ------------------------- */
typedef struct { char *buf; size_t len; } mem_t;
static size_t on_write(void *p, size_t sz, size_t n, void *u)
{ size_t a = sz * n; mem_t *m = u; char *nb = realloc(m->buf, m->len + a + 1); if (!nb) return 0;
  m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0; return a; }

/* 用通道 IP + 凭据把 NOP JSON 原样 POST 到设备 /APPJsonCmd(NightOwl 从机相机接口)。
 * 转发失败(无 IP / 设备不可达 / 无应答) → 统一 501。user/pass 可空(默认/空密码)。 */
static char *forward_nop(const char *ip, int port, const char *user, const char *pass,
                         const char *json_in)
{
    if (!ip || !ip[0]) return nvr_resp_not_support();
    CURL *c = curl_easy_init();
    if (!c) return nvr_resp_not_support();
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/APPJsonCmd", ip, port);
    mem_t m = {0};
    struct curl_slist *hdr = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_in);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    if (user && user[0]) {                       /* 相机鉴权:admin + 通道口令 */
        char up[160]; snprintf(up, sizeof(up), "%s:%s", user, pass ? pass : "");
        curl_easy_setopt(c, CURLOPT_USERPWD, up);
        curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_DIGEST | CURLAUTH_BASIC));
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || !m.buf) { free(m.buf); return nvr_resp_not_support(); }
    return m.buf;   /* 设备原样应答 */
}

/* rtsp://<ip>[:port]/... 里取 host */
static void host_from_url(const char *url, char *ip, int cap)
{
    ip[0] = 0;
    const char *p = url ? strstr(url, "://") : NULL;
    if (!p) return;
    p += 3; int i = 0; while (p[i] && p[i] != ':' && p[i] != '/' && i < cap - 1) { ip[i] = p[i]; i++; } ip[i] = 0;
}

/* ------------------------- 回落 nopcore ------------------------- */
static char *fallback_nop(nvr_cmd_router_t *r, const char *json_in)
{
    if (r->cfg.nop) {
        char *nout = NULL;
        if (nop_app_dispatch(r->cfg.nop, json_in, &nout) == 0 && nout) {
            char *dup = strdup(nout);
            nop_app_free_response(nout);
            return dup ? dup : nvr_resp_err("oom");
        }
    }
    return nvr_resp_not_support();
}

/* ------------------------- 顶层分派 ------------------------- */
char *nvr_cmd_dispatch(nvr_cmd_router_t *r, const char *json_in)
{
    if (!json_in) return nvr_resp_err("empty_request");
    cJSON *req = cJSON_Parse(json_in);
    if (!req) return nvr_resp_err("bad_json");
    const char *func = nvr_jstr(req, "func", NULL);
    cJSON *args = cJSON_GetObjectItem(req, "args");

    /* 1) 黑名单 = 本地路由表:命中即 NVR 本地处理 */
    nvr_cmd_fn fn = nvr_cmd_table_lookup(func);
    if (fn) {
        char *out = fn(args, &r->ctx);
        cJSON_Delete(req);
        /* handler 只做实际业务:返回 NULL = 未处理/未实现 → 路由统一回 501 notSupport。 */
        return out ? out : nvr_resp_not_support();
    }

    /* 2) 非黑名单 → 有 channel:查 camera,按 backend 分流 */
    int channel = nvr_jint(args, "channel", -1);   /* 协议 1-based */
    if (channel >= 1 && r->cfg.cm) {
        nvr_channel_t ch;
        if (nvr_chan_get(r->cfg.cm, channel - 1, &ch) != 0) {
            NVR_LOGW("router", "%s: 通道 %d 未找到设备 → 501", func ? func : "?", channel);
            cJSON_Delete(req);
            return nvr_resp_not_support();
        }
        if (ch.backend == 0) {
            /* NOP 透传:改写 args.channel=设备侧(默认 1),重序列化后 POST 到真实设备 */
            char ip[64]; snprintf(ip, sizeof(ip), "%s", ch.onvif_ip[0] ? ch.onvif_ip : "");
            if (!ip[0]) host_from_url(ch.url, ip, sizeof(ip));
            cJSON *ca = cJSON_GetObjectItem(req, "args");
            if (ca && cJSON_GetObjectItem(ca, "channel"))
                cJSON_SetIntValue(cJSON_GetObjectItem(ca, "channel"), 1);
            char *rewritten = cJSON_PrintUnformatted(req);
            NVR_LOGI("router", "%s → 透传 ch%d 设备 %s", func ? func : "?", channel, ip);
            char *out = forward_nop(ip, r->cfg.dev_nop_port, ch.user, ch.pass,
                                    rewritten ? rewritten : json_in);
            free(rewritten);
            cJSON_Delete(req);
            return out;
        }
        /* nopOnvif/onvif → mappingonvif(nop_app_dispatch 内的 is_onvif 前门) */
        NVR_LOGI("router", "%s → ch%d mappingonvif", func ? func : "?", channel);
        cJSON_Delete(req);
        return fallback_nop(r, json_in);
    }

    /* 3) 无 channel 且非黑名单 → 回落 nopcore */
    cJSON_Delete(req);
    return fallback_nop(r, json_in);
}

/* ------------------------- 生命周期 ------------------------- */
/* 本路由不自建监听:作为唯一 8089 入口(nop_http_server)的请求处理器,
 * 由外部把请求体交给 nvr_cmd_dispatch。 */
int nvr_cmd_router_start(const nvr_cmd_router_cfg_t *cfg, nvr_cmd_router_t **out)
{
    if (!cfg || !out) return -1;
    nvr_cmd_router_t *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->cfg = *cfg;
    if (r->cfg.dev_nop_port <= 0) r->cfg.dev_nop_port = 8089;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* 装配 handler 上下文 */
    r->ctx.settings = cfg->settings;
    r->ctx.cm       = cfg->cm;
    r->ctx.stg      = cfg->stg;
    r->ctx.sm       = cfg->sm;
    r->ctx.group    = cfg->group;
    r->ctx.nop      = cfg->nop;
    r->ctx.pv       = cfg->pv;
    r->ctx.pb       = cfg->pb;
    r->ctx.eh       = cfg->eh;
    r->ctx.persist  = cfg->persist;
    r->ctx.dev_nop_port = r->cfg.dev_nop_port;
    if (cfg->settings)
        nvr_settings_get_str(cfg->settings, "system.sn", r->ctx.nvr_sn, sizeof(r->ctx.nvr_sn), "");

    NVR_LOGI("router", "命令路由就绪(表驱动 %d 项;作为 8089 入口处理器)", g_nvr_cmd_table_len);
    *out = r;
    return 0;
}

void nvr_cmd_router_stop(nvr_cmd_router_t *r)
{
    if (!r) return;
    curl_global_cleanup();
    free(r);
}
