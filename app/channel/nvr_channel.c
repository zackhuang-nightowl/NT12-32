/***************************************************************************************
 *  nvr_channel.c — 通道管理 + 在线状态机。见 nvr_channel.h / 计划 §B1。
 ***************************************************************************************/
#include "nvr_channel.h"
#include "nvr_onvif.h"        /* nvr_onvif_get_url（弱兜底在 app/onvif 提供强符号） */
#include "nvr_dev_classify.h" /* nvr_dev_classify */
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    nvr_channel_t     d;          /* 通道描述（含 kind/backend/enabled） */
    int               in_use;
    nvr_chan_status_t status;
    int               backoff_s;
    time_t            next_retry;
    int               notified_online;
} slot_t;

struct nvr_chan_mgr {
    nvr_chan_mgr_cfg_t cfg;
    slot_t slots[NVR_MAX_CH];
};

const char *nvr_chan_status_name(nvr_chan_status_t s)
{
    switch (s) {
        case NVR_CHAN_EMPTY:      return "EMPTY";
        case NVR_CHAN_BOUND:      return "BOUND";
        case NVR_CHAN_CONNECTING: return "CONNECTING";
        case NVR_CHAN_ONLINE:     return "ONLINE";
        case NVR_CHAN_NOSIGNAL:   return "NOSIGNAL";
        case NVR_CHAN_FAIL:       return "FAIL";
        case NVR_CHAN_DISABLED:   return "DISABLED";
        default:                  return "?";
    }
}

static slot_t *slot_of(nvr_chan_mgr_t *m, int chn)
{
    if (chn < 0 || chn >= NVR_MAX_CH) return NULL;
    return &m->slots[chn];
}

int nvr_chan_mgr_init(const nvr_chan_mgr_cfg_t *cfg, nvr_chan_mgr_t **out)
{
    if (!cfg || !cfg->sm || !out) return -1;
    nvr_chan_mgr_t *m = calloc(1, sizeof(*m));
    if (!m) return -1;
    m->cfg = *cfg;
    if (m->cfg.reconnect_base_s <= 0) m->cfg.reconnect_base_s = 5;
    if (m->cfg.reconnect_max_s  <= 0) m->cfg.reconnect_max_s  = 30;
    *out = m;
    return 0;
}

void nvr_chan_mgr_deinit(nvr_chan_mgr_t *m) { if (m) free(m); }

/* 解析取流 URL：显式 url 优先；否则 onvif_auto+ip → nvr_onvif_get_url。成功写 cc.url。 */
static int resolve_url(const nvr_channel_t *d, char *url, int cap)
{
    if (d->url[0]) { snprintf(url, cap, "%s", d->url); return 0; }
    if (d->onvif_auto && d->onvif_ip[0]) {
        const char *st = (d->stream == NVR_STREAM_SUB) ? "sub" : "main";
        if (nvr_onvif_get_url(d->onvif_ip, d->onvif_port, d->user, d->pass, st, url, cap) == 0)
            return 0;
    }
    url[0] = 0;
    return -1;   /* 待发现/待取流 */
}

/* 把一个描述加入 streaming（若能拿到 URL 就 add，拿不到则置 BOUND 等待 tick 解析） */
static int install_slot(nvr_chan_mgr_t *m, const nvr_channel_t *d)
{
    slot_t *s = slot_of(m, d->chn);
    if (!s) return -1;
    s->d = *d;
    s->in_use = 1;
    s->backoff_s = m->cfg.reconnect_base_s;
    s->next_retry = 0;
    s->notified_online = 0;

    if (d->enabled == 0) { s->status = NVR_CHAN_DISABLED; return 0; }

    nvr_stream_chan_cfg_t cc; memset(&cc, 0, sizeof(cc));
    cc.chn = d->chn; cc.codec = d->codec; cc.stream = d->stream;
    cc.record = d->record; cc.vout_win = d->vout_win; cc.over_tcp = 1;
    snprintf(cc.user, sizeof(cc.user), "%s", d->user);
    snprintf(cc.pass, sizeof(cc.pass), "%s", d->pass);

    if (resolve_url(d, cc.url, sizeof(cc.url)) == 0) {
        nvr_stream_add_channel(m->cfg.sm, &cc);
        s->status = NVR_CHAN_BOUND;
        NVR_LOGI("chan", "ch%d 绑定 %s (kind=%d record=%d win=%d)", d->chn,
                 cc.url[0] ? cc.url : "(?)", d->kind, cc.record, cc.vout_win);
    } else {
        s->status = NVR_CHAN_BOUND;   /* 待 tick 再解析 URL 后加入 */
        NVR_LOGW("chan", "ch%d 无 URL(待 ONVIF/发现 %s), 暂挂起", d->chn,
                 d->onvif_ip[0] ? d->onvif_ip : "");
    }
    return d->chn;
}

int nvr_chan_load_config(nvr_chan_mgr_t *m, const nvr_config_t *cfg)
{
    if (!m || !cfg) return -1;
    int n = 0;
    for (int i = 0; i < cfg->nch; i++) {
        const nvr_channel_t *e = &cfg->ch[i];
        if (e->chn < 0 || e->chn >= NVR_MAX_CH) continue;
        nvr_channel_t d = *e;
        if (d.enabled == 0 && e->enabled == 0) d.enabled = 0; else d.enabled = 1;
        if (install_slot(m, &d) >= 0) n++;
    }
    return n;
}

int nvr_chan_add(nvr_chan_mgr_t *m, const nvr_channel_t *desc)
{
    if (!m || !desc) return -1;
    slot_t *s = slot_of(m, desc->chn);
    if (!s) return -1;
    if (s->in_use) nvr_chan_remove(m, desc->chn);
    return install_slot(m, desc);
}

int nvr_chan_remove(nvr_chan_mgr_t *m, int chn)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) return -1;
    nvr_stream_stop(m->cfg.sm, chn);
    if (s->notified_online && m->cfg.on_offline) m->cfg.on_offline(m->cfg.user, chn);
    memset(s, 0, sizeof(*s));
    return 0;
}

int nvr_chan_bind_poe(nvr_chan_mgr_t *m, int poe_port, const char *ip,
                      const char *user, const char *pass)
{
    if (!m || poe_port < 1 || poe_port > NVR_POE_PORTS) return -1;
    nvr_channel_t d; memset(&d, 0, sizeof(d));
    d.chn = poe_port - 1;                 /* PoE 口 P → 通道 P-1 */
    d.enabled = 1; d.record = 1;
    d.poe_port = poe_port;
    d.onvif_auto = 1; d.onvif_port = 80;
    d.stream = NVR_STREAM_MAIN; d.codec = NVR_CODEC_AUTO;
    d.vout_win = d.chn;
    snprintf(d.name, sizeof(d.name), "Camera %d", poe_port);
    snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%s", ip ? ip : "");
    snprintf(d.user, sizeof(d.user), "%s", user ? user : "admin");
    snprintf(d.pass, sizeof(d.pass), "%s", pass ? pass : "");
    NVR_LOGI("chan", "PoE 口%d 即插即用绑定 → ch%d (%s)", poe_port, d.chn, d.onvif_ip);
    return nvr_chan_add(m, &d);
}

int nvr_chan_apply_discovery(nvr_chan_mgr_t *m, int chn, const char *scopes)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) return -1;
    nvr_dev_class_t c;
    if (nvr_dev_classify(scopes, &c) != 0) return -1;
    s->d.kind    = (int)c.kind;
    s->d.backend = (int)c.backend;
    NVR_LOGI("chan", "ch%d 分类=%s backend=%s%s%s", chn, nvr_dev_kind_name(c.kind),
             c.backend == NVR_BACKEND_NOP ? "NOP透传" : "ONVIF翻译",
             c.mac[0] ? " mac=" : "", c.mac);
    return 0;
}

/* ---- PoE/LAN 自动发现绑定 ---- */
typedef struct { nvr_chan_mgr_t *m; int bound; } disc_ctx_t;

static int chn_by_host(nvr_chan_mgr_t *m, const char *host)
{
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use && strcmp(m->slots[i].d.onvif_ip, host) == 0) return i;
    return -1;
}
static int free_ip_chn(nvr_chan_mgr_t *m)   /* 数字通道位: NVR_IP_CH_BASE 起找空位 */
{
    for (int i = NVR_IP_CH_BASE; i < NVR_MAX_CH; i++)
        if (!m->slots[i].in_use) return i;
    return -1;
}

static void on_discovered(const nvr_onvif_cam_t *cam, void *user)
{
    disc_ctx_t *ctx = user;
    nvr_chan_mgr_t *m = ctx->m;
    if (!cam->host[0]) return;

    nvr_dev_class_t cls; nvr_dev_classify(cam->scopes, &cls);

    /* 已绑定同 IP → 只更新分类 */
    int chn = chn_by_host(m, cam->host);
    if (chn >= 0) { m->slots[chn].d.kind = (int)cls.kind; m->slots[chn].d.backend = (int)cls.backend; return; }

    /* 通道分配：198.18.<口>.100 → PoE 口 => 通道 口-1；否则数字通道空位 */
    int a, b, cc, dd;
    if (sscanf(cam->host, "%d.%d.%d.%d", &a, &b, &cc, &dd) == 4 && a == 198 && b == 18 &&
        cc >= 1 && cc <= NVR_POE_PORTS)
        chn = cc - 1;
    else
        chn = free_ip_chn(m);
    if (chn < 0 || m->slots[chn].in_use) return;   /* 无空位 / 已占 */

    nvr_channel_t d; memset(&d, 0, sizeof(d));
    d.chn = chn; d.enabled = 1; d.record = 1;
    d.poe_port = (chn < NVR_POE_PORTS) ? chn + 1 : 0;
    d.onvif_auto = 1; d.onvif_port = cam->port > 0 ? cam->port : 80;
    d.stream = NVR_STREAM_MAIN; d.codec = NVR_CODEC_AUTO; d.vout_win = chn;
    d.kind = (int)cls.kind; d.backend = (int)cls.backend;
    snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%s", cam->host);
    snprintf(d.user, sizeof(d.user), "admin");    /* 默认；nopOnvif 激活后用 P_act */
    snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1);

    NVR_LOGI("chan", "发现相机 %s (%s) → 绑定 ch%d", cam->host, nvr_dev_kind_name(cls.kind), chn);
    if (nvr_chan_add(m, &d) >= 0) ctx->bound++;
}

int nvr_chan_run_discovery(nvr_chan_mgr_t *m, const char *local_ip, int seconds)
{
    if (!m) return -1;
    disc_ctx_t ctx = { m, 0 };
    nvr_onvif_discover(local_ip, seconds > 0 ? seconds : 2, on_discovered, &ctx);  /* 拉入强 onvif 符号 */
    return ctx.bound;
}

int nvr_chan_start_all(nvr_chan_mgr_t *m)
{
    if (!m) return -1;
    /* streaming 侧 add 过的通道统一起流；未解析 URL 的由 tick 后续加入 */
    return (nvr_stream_start_all(m->cfg.sm) == RSDK_OK) ? 0 : -1;
}

void nvr_chan_stop_all(nvr_chan_mgr_t *m)
{
    if (!m) return;
    nvr_stream_stop_all(m->cfg.sm);
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use) m->slots[i].status = NVR_CHAN_BOUND;
}

/* 映射 streaming 状态 → app 状态，并驱动上线/掉线通知与重连 */
static void tick_slot(nvr_chan_mgr_t *m, slot_t *s, time_t now)
{
    if (s->status == NVR_CHAN_DISABLED) return;

    /* URL 尚未解析（待发现）：尝试解析后加入 streaming */
    if (s->d.url[0] == 0 && s->d.onvif_auto && s->d.onvif_ip[0]) {
        char url[256];
        if (resolve_url(&s->d, url, sizeof(url)) == 0) {
            nvr_stream_chan_cfg_t cc; memset(&cc, 0, sizeof(cc));
            cc.chn = s->d.chn; cc.codec = s->d.codec; cc.stream = s->d.stream;
            cc.record = s->d.record; cc.vout_win = s->d.vout_win; cc.over_tcp = 1;
            snprintf(cc.url,  sizeof(cc.url),  "%s", url);
            snprintf(cc.user, sizeof(cc.user), "%s", s->d.user);
            snprintf(cc.pass, sizeof(cc.pass), "%s", s->d.pass);
            snprintf(s->d.url, sizeof(s->d.url), "%s", url);
            nvr_stream_add_channel(m->cfg.sm, &cc);
            nvr_stream_start(m->cfg.sm, s->d.chn);
        }
    }

    nvr_ch_state_t st = nvr_stream_state(m->cfg.sm, s->d.chn);
    switch (st) {
        case NVR_CH_PLAYING:
            s->status = NVR_CHAN_ONLINE;
            s->backoff_s = m->cfg.reconnect_base_s;
            if (!s->notified_online) {
                s->notified_online = 1;
                NVR_LOGI("chan", "ch%d ONLINE 出图", s->d.chn);
                if (m->cfg.on_online) m->cfg.on_online(m->cfg.user, s->d.chn);
            }
            break;
        case NVR_CH_CONNECTING:
            s->status = NVR_CHAN_CONNECTING;
            break;
        case NVR_CH_NOSIGNAL:
        case NVR_CH_FAIL:
            s->status = (st == NVR_CH_NOSIGNAL) ? NVR_CHAN_NOSIGNAL : NVR_CHAN_FAIL;
            if (s->notified_online) {
                s->notified_online = 0;
                NVR_LOGW("chan", "ch%d 掉线(%s)", s->d.chn,
                         (st == NVR_CH_NOSIGNAL) ? "NOSIGNAL" : "FAIL");
                if (m->cfg.on_offline) m->cfg.on_offline(m->cfg.user, s->d.chn);
            }
            if (now >= s->next_retry) {           /* 退避重连 */
                NVR_LOGI("chan", "ch%d 重连(退避 %ds)", s->d.chn, s->backoff_s);
                nvr_stream_stop(m->cfg.sm, s->d.chn);
                nvr_stream_start(m->cfg.sm, s->d.chn);
                s->next_retry = now + s->backoff_s;
                s->backoff_s = (s->backoff_s * 2 > m->cfg.reconnect_max_s)
                             ? m->cfg.reconnect_max_s : s->backoff_s * 2;
            }
            break;
        default:
            break;
    }
}

void nvr_chan_tick(nvr_chan_mgr_t *m)
{
    if (!m) return;
    time_t now = time(NULL);
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use) tick_slot(m, &m->slots[i], now);
}

int nvr_chan_get(nvr_chan_mgr_t *m, int chn, nvr_channel_t *out)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use || !out) return -1;
    *out = s->d;
    return 0;
}

int nvr_chan_list(nvr_chan_mgr_t *m, nvr_channel_t *out, int cap)
{
    if (!m || !out) return -1;
    int n = 0;
    for (int i = 0; i < NVR_MAX_CH && n < cap; i++)
        if (m->slots[i].in_use) out[n++] = m->slots[i].d;
    return n;
}

nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t *m, int chn)
{
    slot_t *s = slot_of(m, chn);
    return (s && s->in_use) ? s->status : NVR_CHAN_EMPTY;
}
