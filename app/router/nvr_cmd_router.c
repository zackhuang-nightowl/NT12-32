/***************************************************************************************
 *  nvr_cmd_router.c — NVR 命令路由(表驱动)。见 nvr_cmd_router.h / nvr_cmd_internal.h。
 *
 *  分派次序(nvr_cmd_dispatch):
 *    1) 黑名单 = 本地路由表 g_nvr_cmd_table[]:命中 → 本地 handler 处理。
 *    2) 非黑名单 → 按 camera 分流:
 *         backend==0 (NOP)           → 透传 POST 设备 /APPJsonCmd
 *           例外: getChannelActivityZoneTypes / get/setChannelTriggerActivityZone
 *                 先透传；失败 → mapping（CellMotion ModifyRules，写格子）
 *         kind==nopOnvif 且仅 nightowl_protocol 那几条(白灯/警笛/Panic/激活)
 *                                    → POST 发现口 /APPJsonCmd；其余 nopOnvif 命令仍走 ONVIF SOAP
 *         其余 backend!=0            → mapping → ONVIF SOAP
 *       通道找不到设备 → 501。
 *    3) 无 channel 且非黑名单 → 回落 nop_app_dispatch → 501。
 ***************************************************************************************/
#include "nvr_cmd_router.h"
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_identity.h"
#include "nvr_defaults.h"
#include "nvr_dev_classify.h"
#include "nvr_log.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct nvr_cmd_router {
    nvr_cmd_router_cfg_t cfg;
    nvr_cmd_ctx_t        ctx;      /* handler 上下文(由 cfg 装配) */
    /* 派发串行锁：8089 现为线程池(≤5 并发),但本地表 handler / nopcore 配置类
     * handler 操作的共享状态(通道表、settings 单一 sqlite 连接、nop 核心配置)
     * 均按单线程写就,不可重入。用本锁把这些"快"路径串行化 = 与改造前等价、零新增竞态;
     * 而两条"慢"网络后端(NOP 透传 curl 每次独立句柄可重入 / ONVIF 映射内部 be->lock 自串行)
     * 在下发前释放本锁并行执行——这正是消除"一路卡死全 8089"的关键。 */
    pthread_mutex_t      disp_lock;
};

/* curl 连接缓存：透传每次 easy_init/cleanup，不 share 就会每条命令新建 TCP。
 * CONNECT + DNS 跨句柄复用，同一相机后续命令走 keep-alive。 */
static CURLSH         *g_curl_share;
static pthread_mutex_t g_curl_share_mu = PTHREAD_MUTEX_INITIALIZER;

static void curl_share_lock(CURL *h, curl_lock_data d, curl_lock_access a, void *u)
{
    (void)h; (void)d; (void)a; (void)u;
    pthread_mutex_lock(&g_curl_share_mu);
}
static void curl_share_unlock(CURL *h, curl_lock_data d, void *u)
{
    (void)h; (void)d; (void)u;
    pthread_mutex_unlock(&g_curl_share_mu);
}

static long mono_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ------------------------- 转发到设备（NOP 透传） ------------------------- */
typedef struct { char *buf; size_t len; } mem_t;
static size_t on_write(void *p, size_t sz, size_t n, void *u)
{ size_t a = sz * n; mem_t *m = u; char *nb = realloc(m->buf, m->len + a + 1); if (!nb) return 0;
  m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0; return a; }

/* 发现口 = 命令口。空口令 = 无鉴权，不带 HTTP Digest;123456 视为真实口令(带鉴权)。 */
static int nop_http_need_auth(const char *user, const char *pass)
{
    if (!user || !user[0]) return 0;
    if (!pass || !pass[0]) return 0;
    return 1;
}

/* 用通道 IP + 凭据把 NOP JSON 原样 POST 到设备 /APPJsonCmd(NightOwl 从机相机接口)。
 * 转发失败(无 IP / 设备不可达 / 无应答) → 统一 501。user/pass 可空(默认/空密码)。 */
static char *forward_nop(const char *ip, int port, const char *user, const char *pass,
                         const char *json_in)
{
    if (!ip || !ip[0]) return nvr_resp_not_support();
    CURL *c = curl_easy_init();
    if (!c) return nvr_resp_not_support();
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/APPJsonCmd", ip, port > 0 ? port : 80);
    mem_t m = {0};
    struct curl_slist *hdr = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_in);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, (long)NVR_DEF_CMD_CONNECT_S);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)NVR_DEF_CMD_TIMEOUT_S);
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    /* 用后即关,不进连接池:避免相机关闭空闲连接后滞留 CLOSE_WAIT(见 share 注释)。 */
    curl_easy_setopt(c, CURLOPT_FORBID_REUSE, 1L);
    if (g_curl_share)
        curl_easy_setopt(c, CURLOPT_SHARE, g_curl_share);
    if (nop_http_need_auth(user, pass)) {
        char up[160]; snprintf(up, sizeof(up), "%s:%s", user, pass);
        curl_easy_setopt(c, CURLOPT_USERPWD, up);
        curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_DIGEST | CURLAUTH_BASIC));
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || !m.buf) { free(m.buf); return nvr_resp_not_support(); }
    return m.buf;   /* 设备原样应答 */
}

static int nop_resp_ok(const char *json)
{
    cJSON *o;
    int code;
    if (!json || !json[0]) return 0;
    o = cJSON_Parse(json);
    if (!o) return 0;
    code = nvr_jint(o, "statusCode", 0);
    cJSON_Delete(o);
    return code == 200;
}

static int activity_zone_func(const char *func)
{
    return func &&
           (strcmp(func, "X_NightOwl_getChannelActivityZoneTypes") == 0 ||
            strcmp(func, "X_NightOwl_getChannelTriggerActivityZone") == 0 ||
            strcmp(func, "X_NightOwl_setChannelTriggerActivityZone") == 0);
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

/* 映射(ONVIF)路径按 0-based 回显 content.channel;GUI 协议是 1-based。把响应里的
 * channel 改回请求的 1-based(仅动 channel 字段,不碰其它内容)。返回新串(释放旧串);
 * 无 content.channel 或解析失败则原样返回。透传路径不经过此处(设备应答原样返回)。 */
static char *restore_resp_channel(char *out, int channel_1based)
{
    if (!out) return out;
    cJSON *root = cJSON_Parse(out);
    if (!root) return out;
    cJSON *content = cJSON_GetObjectItem(root, "content");
    cJSON *cf = content ? cJSON_GetObjectItem(content, "channel") : NULL;
    if (cJSON_IsNumber(cf)) {
        cJSON_SetIntValue(cf, channel_1based);
        char *fixed = cJSON_PrintUnformatted(root);
        if (fixed) { free(out); out = fixed; }
    }
    cJSON_Delete(root);
    return out;
}

/* ------------------------- 顶层分派 ------------------------- */
char *nvr_cmd_dispatch(nvr_cmd_router_t *r, const char *json_in)
{
    if (!json_in) return nvr_resp_err("empty_request");
    cJSON *req = cJSON_Parse(json_in);   /* cJSON 无全局态,解析可无锁并行 */
    if (!req) return nvr_resp_err("bad_json");
    const char *func = nvr_jstr(req, "func", NULL);
    cJSON *args = cJSON_GetObjectItem(req, "args");

    /* 快路径共享状态不可重入 → 进锁串行(见 struct 注释);两条慢网络后端在下发前解锁并行。 */
    pthread_mutex_lock(&r->disp_lock);

    /* 1) 黑名单 = 本地路由表:命中即 NVR 本地处理 */
    nvr_cmd_fn fn = nvr_cmd_table_lookup(func);
    if (fn) {
        /* GUI_longPolling 可挂起数秒等状态变化:先放 disp_lock,避免卡住其它 8089 本地命令。 */
        int lp = (func && strcmp(func, "GUI_longPolling") == 0);
        int ota_chk = (func && (strcmp(func, "GUI_checkServerFirmware") == 0 ||
                                strcmp(func, "GUI_checkChannelServerFirmware") == 0));
        int snap = (func && strcmp(func, "snapshotChannel") == 0);
        /* getChannelInfo 现每次向设备实时取版本(NOP getDeviceInfo / ONVIF GetDeviceInformation),含网络
         * 往返 → 先放 disp_lock,避免这几百ms~数秒卡住其它 8089 命令(GUI 逐通道查设备信息时尤甚)。 */
        int chinfo = (func && strcmp(func, "X_NightOwl_getChannelInfo") == 0);
        int hold = !(lp || ota_chk || snap || chinfo);
        if (!hold) pthread_mutex_unlock(&r->disp_lock);
        char *out = fn(args, &r->ctx);
        if (hold) pthread_mutex_unlock(&r->disp_lock);
        cJSON_Delete(req);
        /* handler 只做实际业务:返回 NULL = 未处理/未实现 → 路由统一回 501 NOT_SUPPORT。 */
        return out ? out : nvr_resp_not_support();
    }

    /* 2) 非黑名单 → 有 channel:查 camera,按 backend/kind 分流 */
    int channel = nvr_jint(args, "channel", -1);   /* 协议 1-based */
    if (channel >= 1 && r->cfg.cm) {
        nvr_channel_t ch;
        if (nvr_chan_get(r->cfg.cm, channel - 1, &ch) != 0) {   /* ch 为值拷贝 */
            pthread_mutex_unlock(&r->disp_lock);
            NVR_LOGW("router", "%s: 通道 %d 未找到设备 → 501", func ? func : "?", channel);
            cJSON_Delete(req);
            return nvr_resp_not_support();
        }
        int noponvif_priv = (ch.kind == NVR_DEV_KIND_NOPONVIF &&
                             nvr_chan_noponvif_priv_func(func));
        if (ch.backend == 0 || noponvif_priv) {
            /* NOP 全量透传; nopOnvif 仅白名单那几条走 /APPJsonCmd,其余仍 mapping SOAP */
            char ip[64]; snprintf(ip, sizeof(ip), "%s", ch.onvif_ip[0] ? ch.onvif_ip : "");
            if (!ip[0]) host_from_url(ch.url, ip, sizeof(ip));
            cJSON *ca = cJSON_GetObjectItem(req, "args");
            if (noponvif_priv && func &&
                (strcmp(func, "X_NightOwl_getPanicSwitch") == 0 ||
                 strcmp(func, "X_NightOwl_setPanicSwitch") == 0)) {
                /* 设备侧 Panic 无 channel;NVR 只靠 channel 选机,转发时剥掉。 */
                if (ca) cJSON_DeleteItemFromObject(ca, "channel");
            } else if (ca && cJSON_GetObjectItem(ca, "channel")) {
                int dev_ch = ch.dev_chn > 0 ? ch.dev_chn : 1;
                cJSON_SetIntValue(cJSON_GetObjectItem(ca, "channel"), dev_ch);
            }
            char *rewritten = cJSON_PrintUnformatted(req);
            int port = ch.onvif_port > 0 ? ch.onvif_port : 80;
            NVR_LOGI("router", "%s → %s ch%d 设备 %s:%d",
                     func ? func : "?", noponvif_priv ? "nopOnvif私有NOP" : "透传",
                     channel, ip, port);
            /* 慢速 curl(最长 NVR_DEF_CMD_TIMEOUT_S):此后只用栈上局部(ip/ch 值拷贝/rewritten),
             * curl 每次独立句柄可重入 → 先解锁并行,避免拖住其它 8089 命令。 */
            pthread_mutex_unlock(&r->disp_lock);
            {
                long t0 = mono_ms();
                char *out = forward_nop(ip, port, ch.user, ch.pass,
                                        rewritten ? rewritten : json_in);
                NVR_LOGI("router", "%s 透传 ch%d %s:%d %ldms",
                         func ? func : "?", channel, ip, port, mono_ms() - t0);
                /* Motion 活动区域：NOP 透传失败则改回 NVR 0-based channel，走 CellMotion mapping。 */
                if (activity_zone_func(func) && !nop_resp_ok(out)) {
                    NVR_LOGI("router", "%s 透传失败 → mapping 回落 ch%d",
                             func, channel);
                    free(out);
                    free(rewritten);
                    if (ca && cJSON_GetObjectItem(ca, "channel"))
                        cJSON_SetIntValue(cJSON_GetObjectItem(ca, "channel"), channel - 1);
                    rewritten = cJSON_PrintUnformatted(req);
                    t0 = mono_ms();
                    out = fallback_nop(r, rewritten ? rewritten : json_in);
                    NVR_LOGI("router", "%s mapping-fallback ch%d %ldms",
                             func, channel, mono_ms() - t0);
                }
                /* 透传下发设备时 channel 已改成 dev_ch(见上,通常 1);设备按自己的 channel 回显,
                 * 这里把响应 channel 改回请求的 NVR 1-based(仅 channel,不动其它内容)。 */
                out = restore_resp_channel(out, channel);
                free(rewritten);
                cJSON_Delete(req);
                return out;
            }
        }
        /* nopOnvif/onvif → mappingonvif(nop_app_dispatch 内的 is_onvif 前门)。
         * 映射注册表/会话按 0-based(d->chn)建键匹配 → 丢给 nop 前把协议 1-based channel
         * 改写成 0-based,否则 is_onvif/session 会错位到 channel+1 的设备(off-by-one)。 */
        cJSON *ca = cJSON_GetObjectItem(req, "args");
        if (ca && cJSON_GetObjectItem(ca, "channel"))
            cJSON_SetIntValue(cJSON_GetObjectItem(ca, "channel"), channel - 1);
        char *rewritten = cJSON_PrintUnformatted(req);
        NVR_LOGI("router", "%s → ch%d mappingonvif(0-based=%d)", func ? func : "?", channel, channel - 1);
        /* 慢速 ONVIF wire(最长 5~10s):ONVIF 映射内部 be->lock 自串行且不触碰
         * 本地 settings/通道表 → 先解锁并行执行。 */
        pthread_mutex_unlock(&r->disp_lock);
        {
            long t0 = mono_ms();
            char *out = fallback_nop(r, rewritten ? rewritten : json_in);
            /* 映射内部按 0-based 回显 channel → 转回请求的 1-based(仅 channel,不动内容)。 */
            out = restore_resp_channel(out, channel);
            NVR_LOGI("router", "%s mapping ch%d %ldms",
                     func ? func : "?", channel, mono_ms() - t0);
            free(rewritten);
            cJSON_Delete(req);
            return out;
        }
    }

    /* 3) 无 channel 且非黑名单 → 回落 nopcore(设备级配置,可能写 settings → 持锁执行) */
    char *out = fallback_nop(r, json_in);
    pthread_mutex_unlock(&r->disp_lock);
    cJSON_Delete(req);
    return out;
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
    pthread_mutex_init(&r->disp_lock, NULL);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_curl_share = curl_share_init();
    if (g_curl_share) {
        /* 只共享 DNS 缓存(便宜、无副作用)。★ 不再共享连接池(CURL_LOCK_DATA_CONNECT):
         * 相机侧会主动关闭空闲连接,被 curl 连接池缓存的那条即滞留在 CLOSE_WAIT,长期累积
         * 耗尽 fd/连接(实测 8089 出现多条 →CLOSE_WAIT 泄漏)。NVR 转发相机命令频次低,每次
         * 短连接(配合 FORBID_REUSE 用后即关)更干净,握手成本在局域网可忽略。 */
        curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(g_curl_share, CURLSHOPT_LOCKFUNC, curl_share_lock);
        curl_share_setopt(g_curl_share, CURLSHOPT_UNLOCKFUNC, curl_share_unlock);
    }

    /* 装配 handler 上下文 */
    r->ctx.settings = cfg->settings;
    r->ctx.cm       = cfg->cm;
    r->ctx.stg      = cfg->stg;
    r->ctx.sm       = cfg->sm;
    r->ctx.group    = cfg->group;
    r->ctx.meta     = cfg->meta;
    r->ctx.nop      = cfg->nop;
    r->ctx.pv       = cfg->pv;
    r->ctx.pb       = cfg->pb;
    r->ctx.eh       = cfg->eh;
    r->ctx.persist  = cfg->persist;
    r->ctx.talk     = cfg->talk;
    r->ctx.disp_user         = cfg->disp_user;
    r->ctx.on_set_resolution = cfg->on_set_resolution;
    r->ctx.dev_nop_port = r->cfg.dev_nop_port;
    r->ctx.disp_lock = &r->disp_lock;   /* handler 阻塞等待前后 CMD_UNBLOCK/REBLOCK 用 */
    nvr_identity_get_sn(r->ctx.nvr_sn, sizeof(r->ctx.nvr_sn));  /* SN ← /User/OWLSerialNumber(恒定) */

    NVR_LOGI("router", "命令路由就绪(表驱动 %d 项;作为 8089 入口处理器)", g_nvr_cmd_table_len);
    *out = r;
    return 0;
}

void nvr_cmd_router_expand_sources(nvr_cmd_router_t *r, const char *ip)
{
    if (!r || !ip || !ip[0]) return;
    nvr_lan_expand_sources(&r->ctx, ip);   /* 用 router 自持 ctx(长生命)后台展开,不阻塞、不碰 disp_lock */
}

void nvr_cmd_router_stop(nvr_cmd_router_t *r)
{
    if (!r) return;
    if (g_curl_share) {
        curl_share_cleanup(g_curl_share);
        g_curl_share = NULL;
    }
    curl_global_cleanup();
    pthread_mutex_destroy(&r->disp_lock);
    free(r);
}
