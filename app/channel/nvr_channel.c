/***************************************************************************************
 *  nvr_channel.c — 通道管理 + 在线状态机。见 nvr_channel.h / 计划 §B1。
 ***************************************************************************************/
#include "nvr_channel.h"
#include "nvr_defaults.h"     /* PoE 内网段宏 NVR_POE_NET_A/B */
#include "nvr_onvif.h"        /* nvr_onvif_get_url（弱兜底在 app/onvif 提供强符号） */
#include "nvr_dev_classify.h" /* nvr_dev_classify */
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ifaddrs.h>       /* getifaddrs —— 取 eth0 IP 作 LAN 发现的多播源 */
#include <arpa/inet.h>     /* inet_ntop */
#include <netinet/in.h>    /* sockaddr_in */
#include <curl/curl.h>     /* NOP 设备 getDeviceCapabilities 直取(POST 8089/APPJsonCmd) */
#include "cJSON.h"

/* NOP 设备默认命令端口(NightOwl 从机 8089/APPJsonCmd)。 */
#ifndef NVR_DEV_NOP_PORT
#define NVR_DEV_NOP_PORT 8089
#endif

/* curl 收集应答体 */
typedef struct { char *buf; size_t len; } chan_http_buf_t;
static size_t chan_http_sink(void *p, size_t sz, size_t nm, void *u)
{
    size_t n = sz * nm; chan_http_buf_t *b = (chan_http_buf_t *)u;
    char *nb = realloc(b->buf, b->len + n + 1); if (!nb) return 0;
    b->buf = nb; memcpy(b->buf + b->len, p, n); b->len += n; b->buf[b->len] = 0;
    return n;
}
/* 向 NOP 设备 POST 一条 JSON 命令,返回 malloc 应答体(调用方 free);失败返回 NULL。 */
static char *chan_nop_post(const char *ip, int port, const char *json_in)
{
    CURL *c = curl_easy_init(); if (!c) return NULL;
    char url[128]; snprintf(url, sizeof(url), "http://%s:%d/APPJsonCmd", ip, port > 0 ? port : NVR_DEV_NOP_PORT);
    chan_http_buf_t m = {0};
    struct curl_slist *hdr = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_in);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, chan_http_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdr); curl_easy_cleanup(c);
    if (rc != CURLE_OK) { free(m.buf); return NULL; }
    return m.buf;
}

typedef struct {
    nvr_channel_t     d;          /* 通道描述（含 kind/backend/enabled） */
    int               in_use;
    nvr_chan_status_t status;
    int               backoff_s;
    time_t            next_retry;
    int               notified_online;
    nvr_chan_substate_t sub;      /* 鉴权失败/超预算/待激活/休眠/固件升级子状态（0-7 码用） */
    int               url_tries;  /* 已发起的取流URL解析次数（护 IPC：错够即停，见 tick_slot） */
    time_t            url_next;    /* 下次允许解析的时间（退避） */
    time_t            recall_next; /* 下次允许"LAN 找回"重发现的时间（已添加 LAN 设备连不上时；30s 退避） */
    char              url_main[256]; /* 已解析的主码流取流 URL 缓存（切主/子码流免重新 ONVIF 解析→秒切） */
    char              url_sub[256];  /* 已解析的子码流取流 URL 缓存 */
    int               caps_probed; /* 首次上线已按设备探测并写 camera_capability(只探一次) */
    int               poe_present;  /* PoE 口:ONVIF 广播发现到相机在场(替代写死 .1 的 ARP 门控) */
    time_t            poe_disc_next;/* PoE 口下次允许 ONVIF 广播发现的时间(退避) */
} slot_t;

/* IPC 保护：多数 NightOwl IPC 连续鉴权失败 5 次即触发保护/重启。取流 URL 解析走 ONVIF
 * SOAP（携带凭据），失败即一次鉴权错误。故对每个通道限制解析次数并退避，凭据不对时
 * 绝不反复轰相机。达上限后停到 setLanDevice 改凭据（重装 slot 会清零计数）。 */
#define NVR_URL_MAX_TRIES 2

struct nvr_chan_mgr {
    nvr_chan_mgr_cfg_t cfg;
    slot_t slots[NVR_MAX_CH];
    unsigned notify_mask;   /* 状态变化位图(bit=chn)：上线/掉线置位,GUI_longPolling drain→ChannelStatusNotify */
    int      poe_scan_cursor; /* PoE 发现轮询游标(公平轮扫所有未出图 PoE 口,避免只扫前几口) */
};

/* 取点分十进制 IPv4 的第 3 段(198.18.<seg>.x → seg);失败返回 -1。 */
static int ip_seg3(const char *ip)
{
    int a, b, c, d;
    if (ip && sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) return c;
    return -1;
}

/* 读并清状态变化位图。供 GUI_longPolling 返回 ChannelStatusNotify（gui 据此重拉 getChannelStatus 出图）。 */
unsigned nvr_chan_drain_notify(nvr_chan_mgr_t *m)
{
    if (!m) return 0;
    unsigned v = m->notify_mask;
    m->notify_mask = 0;
    return v;
}

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

/* app 通道 FSM → 状态码用的粗粒度连接态 */
static nvr_conn_t conn_of(nvr_chan_status_t st)
{
    switch (st) {
        case NVR_CHAN_ONLINE:   return NVR_CONN_ONLINE;
        case NVR_CHAN_EMPTY:
        case NVR_CHAN_DISABLED: return NVR_CONN_NOCAM;
        default:                return NVR_CONN_CONNECTING;   /* BOUND/CONNECTING/NOSIGNAL/FAIL */
    }
}

void nvr_chan_set_substate(nvr_chan_mgr_t *m, int chn, const nvr_chan_substate_t *sub)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !sub) return;   /* 越界/空指针忽略 */
    s->sub = *sub;
}

/* 该通道是否有"真实设备"在场:经 ONVIF 发现拿到 mac(→ persist_camera 落库)即为真机。
 * 空 PoE 口(配置预建但没插相机)始终 mac 空、从未上线 → 非真机。已上线者一定是真机(即便某些
 * 非标相机 scopes 无 mac,也按在场处理)。 */
static int slot_has_device(const slot_t *s)
{
    if (!s || !s->in_use) return 0;
    return s->d.mac[0] != 0 || s->status == NVR_CHAN_ONLINE;
}

int nvr_chan_status_code_of(nvr_chan_mgr_t *m, int chn)
{
    slot_t *s = slot_of(m, chn);
    /* ★ 无真实设备(空 PoE 口/未发现,DB 无记录)→ 状态 0。不再把空口的 ONVIF 探测失败误报为
     * 连接中(3)/鉴权失败(4)。状态按实际设备记录:有真机才反映其在线/连接/子状态。 */
    if (!slot_has_device(s)) return 0;
    nvr_chan_status_t st = nvr_chan_status(m, chn);
    return nvr_chan_status_code(conn_of(st), &s->sub);
}

char *nvr_chan_dev_post(nvr_chan_mgr_t *m, int chn, const char *func, const char *args_json)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use || s->d.backend != 0) return NULL;   /* 仅 NOP 设备透传 */
    if (!s->d.onvif_ip[0]) return NULL;
    /* 设备侧 channel 固定 1(单相机);若调用方给了 args 就带上,否则只带 channel。 */
    char body[640];
    if (args_json && args_json[0])
        snprintf(body, sizeof(body), "{\"func\":\"%s\",\"args\":%s}", func ? func : "", args_json);
    else
        snprintf(body, sizeof(body), "{\"func\":\"%s\",\"args\":{\"channel\":1}}", func ? func : "");
    return chan_nop_post(s->d.onvif_ip, NVR_DEV_NOP_PORT, body);
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
static int resolve_url(const nvr_channel_t *d, char *url, int cap, char *scopes, int scap)
{
    if (scopes && scap > 0) scopes[0] = 0;
    if (d->url[0]) { snprintf(url, cap, "%s", d->url); return 0; }
    if (d->onvif_auto && d->onvif_ip[0]) {
        const char *st = (d->stream == NVR_STREAM_SUB) ? "sub" : "main";
        if (nvr_onvif_get_url(d->onvif_ip, d->onvif_port, d->user, d->pass, st, url, cap, scopes, scap) == 0)
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
    s->url_tries = 0;                     /* 新装/改凭据 → 重置取流解析预算 */
    s->url_next  = 0;
    s->caps_probed = 0;                   /* 新装/改凭据 → 下次上线重探能力 */
    memset(&s->sub, 0, sizeof(s->sub));   /* 清掉复用 slot 号残留的上一占用者子状态 */

    if (d->enabled == 0) { s->status = NVR_CHAN_DISABLED; return 0; }

    nvr_stream_chan_cfg_t cc; memset(&cc, 0, sizeof(cc));
    cc.chn = d->chn; cc.codec = d->codec; cc.stream = d->stream;
    cc.record = d->record; cc.vout_win = d->vout_win; cc.over_tcp = 1;
    snprintf(cc.user, sizeof(cc.user), "%s", d->user);
    snprintf(cc.pass, sizeof(cc.pass), "%s", d->pass);

    /* ⚠️ 只对**显式 url**（配置直给 rtsp://）在此同步加入——那是常数时间。
     * onvif_auto(无 url) 的 URL 解析要走 ONVIF 广播(~2s/路),**绝不在这里同步做**，
     * 否则 load_config 逐路阻塞几十秒、8089 迟迟起不来(GUI 连不上)。改为置 BOUND，
     * 由 tick 后台限速解析(先监听、后台连接)。 */
    /* 双流:先注册通道(streaming 建槽),主/子 URL 后续由 tick 解析后经 nvr_stream_set_url 分别提供。 */
    nvr_stream_add_channel(m->cfg.sm, &cc);
    s->status = NVR_CHAN_BOUND;
    if (d->url[0]) {
        /* 显式 url(配置直给 rtsp://):当作 d->stream 那一路直接提供(常数时间) */
        nvr_stream_set_url(m->cfg.sm, d->chn, d->stream, d->url);
        NVR_LOGI("chan", "ch%d 绑定 %s (kind=%d record=%d win=%d)", d->chn,
                 d->url, d->kind, cc.record, cc.vout_win);
    } else {
        NVR_LOGW("chan", "ch%d 待 ONVIF 后台解析主+子(%s)", d->chn,
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

/* 按 IP 从内核 ARP 表(/proc/net/arp)匹配物理 MAC。ARP 表只含已通信过的 IP(NVR 连相机后即有),
 * 是设备真实 MAC 的权威来源(ONVIF scope 里的 mac 可能缺失/伪造)。找到返 0。 */
static int arp_mac_of_ip(const char *ip, char *out, size_t cap)
{
    if (out && cap) out[0] = 0;
    if (!ip || !ip[0] || !out) return -1;
    FILE *f = fopen("/proc/net/arp", "r");
    if (!f) return -1;
    char line[256];
    if (fgets(line, sizeof(line), f)) { /* 跳表头 */ }
    int rc = -1;
    while (fgets(line, sizeof(line), f)) {
        char aip[64], hw[16], fl[16], mac[32], mask[16], dev[48];
        if (sscanf(line, "%63s %15s %15s %31s %15s %47s", aip, hw, fl, mac, mask, dev) >= 4) {
            if (strcmp(aip, ip) == 0 && strcmp(mac, "00:00:00:00:00:00") != 0) {
                snprintf(out, cap, "%s", mac); rc = 0; break;
            }
        }
    }
    fclose(f);
    return rc;
}

/* 取接口(如 eth0)的 IPv4,作 WS-Discovery 的多播源(选接口:eth0 查 LAN)。找到返 0。 */
static int iface_ipv4(const char *ifname, char *out, size_t cap)
{
    if (out && cap) out[0] = 0;
    if (!ifname || !out) return -1;
    struct ifaddrs *ifa = NULL, *p;
    if (getifaddrs(&ifa) != 0) return -1;
    int rc = -1;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(p->ifa_name, ifname) != 0) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, out, cap)) rc = 0;
        break;
    }
    freeifaddrs(ifa);
    return rc;
}

/* ---- 设备落库(camera 表)。写表门槛:必须有真实 ip + mac。 ----
 * ★ MAC 权威性:优先用 ARP 表(按 IP 匹配的物理 mac)覆盖 ONVIF scope mac,并回写 slot(d->mac),
 * 使"库里的 mac""内存里的 mac"一致 → IP 变更后按 mac 找回(chn_by_mac / find_by_mac)可靠。 */
static void persist_camera(nvr_chan_mgr_t *m, nvr_channel_t *d)
{
    if (!m->cfg.settings || !d) return;
    if (!d->onvif_ip[0]) return;                        /* 无 ip 不落库 */
    char amac[24];
    if (arp_mac_of_ip(d->onvif_ip, amac, sizeof(amac)) == 0 && amac[0])
        snprintf(d->mac, sizeof(d->mac), "%s", amac);   /* ARP mac 权威,覆盖并回写 slot */
    if (!d->mac[0]) return;                             /* ARP/scope 都无 mac → 不落库 */
    nvr_camera_row_t r; memset(&r, 0, sizeof(r));
    r.chn = d->chn;                                     /* 内部 0-based;协议边界再 +1 */
    r.enabled = d->enabled;
    snprintf(r.source,   sizeof(r.source),   "%s", d->poe_port > 0 ? "POE" : "LAN");
    snprintf(r.protocol, sizeof(r.protocol), "%s", d->kind == NVR_DEV_KIND_NOP ? "nop" : "onvif");
    r.kind = d->kind; r.backend = d->backend;
    snprintf(r.type, sizeof(r.type), "single");
    r.dev_chn = 1;                                      /* 从机单目:设备侧 channel=1 */
    snprintf(r.ip,       sizeof(r.ip),       "%s", d->onvif_ip);
    snprintf(r.mac,      sizeof(r.mac),      "%s", d->mac);
    snprintf(r.username, sizeof(r.username), "%s", d->user);
    snprintf(r.password, sizeof(r.password), "%s", d->pass);
    r.onvif_port = d->onvif_port; r.nop_port = 8089;
    snprintf(r.model, sizeof(r.model), "%s", d->model);   /* 型号(hardware) 持久化,重启后清单可回显 */
    snprintf(r.url, sizeof(r.url), "%s", d->url);
    r.onvif_auto = d->onvif_auto; r.poe_port = d->poe_port;
    r.codec = d->codec; r.stream = d->stream; r.record = d->record;
    snprintf(r.name, sizeof(r.name), "%s", d->name);
    nvr_settings_camera_upsert(m->cfg.settings, &r);
}
static void forget_camera(nvr_chan_mgr_t *m, int chn)
{
    if (m->cfg.settings) nvr_settings_camera_delete(m->cfg.settings, chn);
}

int nvr_chan_add(nvr_chan_mgr_t *m, const nvr_channel_t *desc)
{
    if (!m || !desc) return -1;
    slot_t *s = slot_of(m, desc->chn);
    if (!s) return -1;
    if (s->in_use) nvr_chan_remove(m, desc->chn);
    int rc = install_slot(m, desc);
    /* 落库用已装入的 slot(persist_camera 会用 ARP mac 覆盖并回写 s->d.mac,须传可变 slot) */
    if (rc >= 0) {
        persist_camera(m, &s->d);                       /* 有 ip+mac(ARP 优先)才真正写库 */
        /* ★ 运行时加设备(LanAddDevice/setLanDevice)必须**激活**该通道,否则 set_url 因 active=0
         * 只存 URL 不起 puller → 永不出图(开机 start_all 覆盖不到运行时新加的)。 */
        nvr_stream_start(m->cfg.sm, s->d.chn);
    }
    return rc;
}

int nvr_chan_remove(nvr_chan_mgr_t *m, int chn)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) return -1;
    nvr_stream_stop(m->cfg.sm, chn);
    if (s->notified_online && m->cfg.on_offline) m->cfg.on_offline(m->cfg.user, chn);
    forget_camera(m, chn);                              /* 删设备连带清 DB 行(级联清配置) */
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
    if (c.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", c.mac);
    /* NOPONVIF 设备需 ONVIF digest+激活；scopes 无 nopState/active → 待激活(码7)。
     * 再次分类若已 active，则清零（重连/重发现成功清除标志）。 */
    s->sub.inactive = (c.kind == NVR_DEV_KIND_NOPONVIF && !c.active) ? 1 : 0;
    NVR_LOGI("chan", "ch%d 分类=%s backend=%s%s%s", chn, nvr_dev_kind_name(c.kind),
             c.backend == NVR_BACKEND_NOP ? "NOP透传" : "ONVIF翻译",
             c.mac[0] ? " mac=" : "", c.mac);
    persist_camera(m, &s->d);       /* 分类/mac 更新后回写(有 ip+mac 才写) */
    return 0;
}

/* ---- PoE/LAN 自动发现绑定 ---- */
/* recall_only=1:掉线召回模式——只按 host/mac 更新已知通道的 IP,不新增外部设备
 * (LAN 策略:eth0 上不自动把别人的相机绑进来)。 */
typedef struct { nvr_chan_mgr_t *m; int bound; int recall_only; } disc_ctx_t;

static int chn_by_host(nvr_chan_mgr_t *m, const char *host)
{
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use && strcmp(m->slots[i].d.onvif_ip, host) == 0) return i;
    return -1;
}
static int chn_by_mac(nvr_chan_mgr_t *m, const char *mac)   /* mac 找回(IP 变更) */
{
    if (!mac || !mac[0]) return -1;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use && m->slots[i].d.mac[0] &&
            strcasecmp(m->slots[i].d.mac, mac) == 0) return i;
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

    /* ★ MAC 权威化:按发现到的 IP 从 ARP 表取物理 mac,统一覆盖 scope mac —— 使"按 mac 找回"
     * (chn_by_mac 比对 slot 里已落库的 ARP mac)与落库一致,避免 scope/ARP 不一致导致找回失败。 */
    { char amac[24];
      if (arp_mac_of_ip(cam->host, amac, sizeof(amac)) == 0 && amac[0])
          snprintf(cls.mac, sizeof(cls.mac), "%s", amac); }

    /* 已绑定同 IP → 更新分类 + 重算待激活标志（周期性重发现是清零 inactive 的唯一路径） */
    int chn = chn_by_host(m, cam->host);
    if (chn >= 0) {
        slot_t *s = &m->slots[chn];
        s->d.kind    = (int)cls.kind;
        s->d.backend = (int)cls.backend;
        if (cls.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", cls.mac);
        s->sub.inactive = (cls.kind == NVR_DEV_KIND_NOPONVIF && !cls.active) ? 1 : 0;
        persist_camera(m, &s->d);
        return;
    }

    /* MAC 找回:同 mac 但 IP 变了(设备换了 IP)→ 更新该通道 IP,清 url 重解析,不新增通道 */
    int mchn = chn_by_mac(m, cls.mac);
    if (mchn >= 0) {
        slot_t *s = &m->slots[mchn];
        NVR_LOGI("chan", "mac=%s IP 变更 %s→%s,ch%d 找回", cls.mac, s->d.onvif_ip, cam->host, mchn);
        snprintf(s->d.onvif_ip, sizeof(s->d.onvif_ip), "%s", cam->host);
        s->d.kind = (int)cls.kind; s->d.backend = (int)cls.backend;
        s->d.url[0] = 0; s->url_tries = 0; s->url_next = 0;
        s->status = NVR_CHAN_BOUND; s->notified_online = 0;
        persist_camera(m, &s->d);
        return;
    }

    /* ★ PoE 网段发现:相机在 198.18.<seg>.x(seg=第3段,VLAN 2001=NVR 保留,口 P→VLAN P+1→seg=P+1)。
     * 按 seg 匹配到**同网段的预建 PoE 口通道**(其 onvif_ip 也是 198.18.<seg>.x),更新为相机真实 IP
     * 并置在场+重解析。用 seg 匹配而非 octet-1,兼容任意配置映射(NVR 保留段偏移)。 */
    {
        int seg = ip_seg3(cam->host);
        if (seg >= 0) {
            for (int i = 0; i < NVR_POE_PORTS && i < NVR_MAX_CH; i++) {
                slot_t *s = &m->slots[i];
                if (!s->in_use || s->d.poe_port <= 0) continue;
                if (ip_seg3(s->d.onvif_ip) != seg) continue;    /* 同网段的口通道 */
                s->poe_present = 1;
                if (strcmp(s->d.onvif_ip, cam->host) != 0 || !s->url_sub[0] || !s->url_main[0]) {
                    snprintf(s->d.onvif_ip, sizeof(s->d.onvif_ip), "%s", cam->host);
                    s->d.kind = (int)cls.kind; s->d.backend = (int)cls.backend;
                    if (cls.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", cls.mac);
                    s->d.onvif_port = cam->port > 0 ? cam->port : 80;
                    s->d.url[0] = 0; s->url_main[0] = 0; s->url_sub[0] = 0;
                    s->url_tries = 0; s->url_next = 0; s->sub.auth_fail = 0;
                    s->status = NVR_CHAN_BOUND; s->notified_online = 0;
                    persist_camera(m, &s->d);
                    NVR_LOGI("chan", "PoE 段198.18.%d 发现相机 %s → ch%d(口%d)绑定/更新重解析",
                             seg, cam->host, s->d.chn, s->d.poe_port);
                }
                return;
            }
        }
    }

    /* 召回模式:只更新已知通道(上面 host/mac 命中即返回),不新增外部设备 → 直接结束。 */
    if (ctx->recall_only) return;

    /* 通道分配：198.18.<口>.x（第3段=口号，原厂相机=.1）→ PoE 口 => 通道 口-1；否则数字通道空位 */
    int a, b, cc, dd;
    if (sscanf(cam->host, "%d.%d.%d.%d", &a, &b, &cc, &dd) == 4 && a == NVR_POE_NET_A && b == NVR_POE_NET_B &&
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
    snprintf(d.mac, sizeof(d.mac), "%s", cls.mac);
    snprintf(d.user, sizeof(d.user), "admin");    /* 默认；nopOnvif 激活后用 P_act */
    snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1);

    NVR_LOGI("chan", "发现相机 %s (%s) → 绑定 ch%d", cam->host, nvr_dev_kind_name(cls.kind), chn);
    if (nvr_chan_add(m, &d) >= 0) {
        ctx->bound++;
        /* NOPONVIF 待激活设备：绑定即置状态码7的待激活标志，见 nvr_chan_apply_discovery 同理逻辑 */
        nvr_chan_substate_t sub; memset(&sub, 0, sizeof(sub));
        sub.inactive = (cls.kind == NVR_DEV_KIND_NOPONVIF && !cls.active) ? 1 : 0;
        nvr_chan_set_substate(m, chn, &sub);
    }
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

/* 解析某码流(SUB/MAIN)取流 URL 并经 nvr_stream_set_url 提供给 streaming(该路 puller 起)。
 * 首次(子)顺带按 scopes 分类(kind/backend/mac/型号)。返 0 成功。 */
static int resolve_stream_url(nvr_chan_mgr_t *m, slot_t *s, int stream)
{
    if (!s->d.onvif_auto || !s->d.onvif_ip[0]) return -1;
    char url[256], scopes[512]; scopes[0] = 0;
    const char *st = (stream == NVR_STREAM_SUB) ? "sub" : "main";
    if (nvr_onvif_get_url(s->d.onvif_ip, s->d.onvif_port, s->d.user, s->d.pass,
                          st, url, sizeof(url), scopes, sizeof(scopes)) != 0)
        return -1;
    if (stream == NVR_STREAM_SUB) {
        if (scopes[0]) {
            nvr_dev_class_t c;
            if (nvr_dev_classify(scopes, &c) == 0) {
                s->d.kind = (int)c.kind; s->d.backend = (int)c.backend;
                if (c.mac[0])   snprintf(s->d.mac,   sizeof(s->d.mac),   "%s", c.mac);
                if (c.model[0]) snprintf(s->d.model, sizeof(s->d.model), "%s", c.model);
                s->sub.inactive = (c.kind == NVR_DEV_KIND_NOPONVIF && !c.active) ? 1 : 0;
            }
        }
        snprintf(s->url_sub, sizeof(s->url_sub), "%s", url);
        if (!s->d.url[0]) snprintf(s->d.url, sizeof(s->d.url), "%s", url);   /* 已解析标记(兼容) */
    } else {
        snprintf(s->url_main, sizeof(s->url_main), "%s", url);
    }
    nvr_stream_set_url(m->cfg.sm, s->d.chn, stream, url);
    return 0;
}

/* 映射 streaming 状态 → app 状态，并驱动上线/掉线通知与重连。
 * *resolve_budget：本轮 tick 还允许几路做 ONVIF URL 解析(每路 ~2s 广播,阻塞主循环)。 */
static void tick_slot(nvr_chan_mgr_t *m, slot_t *s, time_t now, int *resolve_budget)
{
    if (s->status == NVR_CHAN_DISABLED) return;

    /* ★ PoE 即插即用门控:
     *   · onvif_ip 仍是占位(198.18.<seg>.100 = NVR 自身,4th=100)且未发现 → 等 ONVIF 发现(不解析);
     *   · 一旦发现到真实相机 IP(4th!=100,如 .1)→ 视为已知在场,像 LAN 一样**直接 get_url 重连**,
     *     不再依赖持续的发现应答(相机 ONVIF/RTSP 掉线后靠退避重连恢复,而非等再次广播命中)。
     *   · url_tries 满 → 重置重连(掉线恢复)。 */
    if ((!s->url_sub[0] || !s->url_main[0]) && s->d.onvif_auto && s->d.poe_port > 0) {
        int last_oct = -1; { int a,b,c,d4; if (sscanf(s->d.onvif_ip,"%d.%d.%d.%d",&a,&b,&c,&d4)==4) last_oct=d4; }
        int known_cam = (s->d.onvif_ip[0] && last_oct != 100 && last_oct >= 0);  /* 非 .100 占位 = 已知相机 IP */
        if (!s->poe_present && !known_cam)
            return;                                       /* 仍占位且未发现在场:本口不解析 */
        if (s->url_tries >= NVR_URL_MAX_TRIES) {          /* 在场/已知却已放弃 → 重置重连 */
            s->url_tries = 0; s->url_next = 0; s->sub.auth_fail = 0;
            NVR_LOGI("chan", "ch%d 相机在场(ONVIF发现),重置解析重连(即插即用)", s->d.chn);
        }
    }

    /* URL 尚未解析（待发现）：经 ONVIF 取流 URL 后加入 streaming。
     * ⚠️ 限次+退避：每秒都打相机会几秒内耗尽 IPC 的 5 次鉴权错误额度→触发保护/重启。
     * 故只在到点(url_next)时试；失败退避且累计到 NVR_URL_MAX_TRIES 即停(置鉴权失败码4)，
     * 直到 setLanDevice 改凭据(重装 slot 清零)才再试。 */
    /* ★ 双流:解析主+子两路 URL(优先子=多宫格显示先出图,再主=录像+单宫格),各自 set_url → 两路常拉。
     * 每 tick 至多解析 1 路(限速护 IPC + 不阻塞主循环);两路都解析好即停。 */
    if (s->d.onvif_auto && s->d.onvif_ip[0] && (!s->url_sub[0] || !s->url_main[0])
        && s->url_tries < NVR_URL_MAX_TRIES && now >= s->url_next
        && resolve_budget && *resolve_budget > 0) {
        int stream = !s->url_sub[0] ? NVR_STREAM_SUB : NVR_STREAM_MAIN;   /* 先子后主 */
        (*resolve_budget)--;
        s->url_tries++;
        if (resolve_stream_url(m, s, stream) == 0) {
            s->url_tries = 0;
            NVR_LOGI("chan", "ch%d %s码流已解析取流", s->d.chn, stream == NVR_STREAM_SUB ? "子" : "主");
            /* 首次(子)解析成功即落库(此刻已有 ip+url,persist_camera 从 ARP 取物理 mac)。 */
            if (stream == NVR_STREAM_SUB) persist_camera(m, &s->d);
        } else {
            int back = 30 * s->url_tries;           /* 30s, 60s… 温和退避，别频繁打相机 */
            s->url_next = now + back;
            if (s->url_tries >= NVR_URL_MAX_TRIES) {
                s->sub.auth_fail = 1;               /* 状态码4：取流失败，停止再试(护 IPC) */
                NVR_LOGW("chan", "ch%d 取流URL解析失败达上限(%d) 停止(防IPC锁定)；改凭据后重试",
                         s->d.chn, NVR_URL_MAX_TRIES);
            } else {
                NVR_LOGW("chan", "ch%d %s码流解析失败(%d/%d) 退避%ds",
                         s->d.chn, stream == NVR_STREAM_SUB ? "子" : "主", s->url_tries, NVR_URL_MAX_TRIES, back);
            }
        }
    }

    nvr_ch_state_t st = nvr_stream_state(m->cfg.sm, s->d.chn);
    /* 解码预算准入：只录不显时 mhal 会拒绝该路解码，供状态码 5(超出解码能力)。 */
    s->sub.out_of_res = nvr_stream_decode_denied(m->cfg.sm, s->d.chn) ? 1 : 0;
    switch (st) {
        case NVR_CH_PLAYING:
            s->status = NVR_CHAN_ONLINE;
            s->backoff_s = m->cfg.reconnect_base_s;
            /* ★ 出图=设备已激活:清 inactive(否则 getChannelStatus 恒返 7 让 GUI 报错,尽管已出图)。
             * 若之前误报 inactive,清零后强制 longPolling 推一次让 GUI 重拉更新为在线。 */
            if (s->sub.inactive) {
                s->sub.inactive = 0;
                m->notify_mask |= (1u << s->d.chn);
                NVR_LOGI("chan", "ch%d 出图→清 inactive(status 7→1),通知 GUI 刷新", s->d.chn);
            }
            if (!s->notified_online) {
                s->notified_online = 1;
                m->notify_mask |= (1u << s->d.chn);   /* 通知 gui 该通道状态变(→CNN),重拉状态出图 */
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
                s->caps_probed = 0;                   /* 掉线→下次连上重取设备能力更新 DB(保持实时/自愈) */
                m->notify_mask |= (1u << s->d.chn);   /* 掉线也通知 gui 刷新(→显示 NO SIGNAL) */
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

    /* 连上设备即取设备能力更新 camera_capability(保持实时/自愈):caps_probed 在掉线时清零,
     * 故每次(重)连都取一次。仅在**成功入库**后置 caps_probed=1;失败(HTTP/BUSY/解析)保持 0,
     * 下 tick 在预算内重试(避免开机三机并发写库 SQLITE_BUSY 丢数据)。占用同一会话预算。 */
    if (s->status == NVR_CHAN_ONLINE && !s->caps_probed && m->cfg.settings
        && s->d.onvif_ip[0] && resolve_budget && *resolve_budget > 0) {
        (*resolve_budget)--;
        int stored = 0;

        if (s->d.backend == 0) {
            /* NOP 设备:直接向设备 getDeviceCapabilities,取回真实能力入库。 */
            char *resp = chan_nop_post(s->d.onvif_ip, NVR_DEV_NOP_PORT,
                                       "{\"func\":\"X_NightOwl_getDeviceCapabilities\",\"args\":{}}");
            if (resp) {
                /* 设备应答: {statusCode, content:{device:{...}, channels:[{...}]}}。
                 * 设备是单相机 → 取 content.channels[0] 作本通道能力对象入库。 */
                cJSON *root = cJSON_Parse(resp);
                cJSON *content = root ? cJSON_GetObjectItem(root, "content") : NULL;
                cJSON *chs = content ? cJSON_GetObjectItem(content, "channels") : NULL;
                cJSON *ch0 = (chs && cJSON_IsArray(chs)) ? cJSON_GetArrayItem(chs, 0) : NULL;
                if (ch0) {
                    char *cj = cJSON_PrintUnformatted(ch0);
                    if (cj) {
                        const char *sig = cJSON_GetStringValue(cJSON_GetObjectItem(ch0, "signal"));
                        if (nvr_settings_caps_set(m->cfg.settings, s->d.chn + 1, cj, sig ? sig : "POE") == 0) {
                            stored = 1;
                            NVR_LOGI("chan", "ch%d NOP 能力入库(signal=%s)", s->d.chn, sig ? sig : "POE");
                        } else NVR_LOGW("chan", "ch%d NOP 能力入库失败(DB busy?),下次重试", s->d.chn);
                        free(cj);
                    }
                } else {
                    NVR_LOGW("chan", "ch%d NOP 应答无 content.channels[0]: %s", s->d.chn, resp);
                }
                if (root) cJSON_Delete(root);
                free(resp);
            } else NVR_LOGW("chan", "ch%d NOP getDeviceCapabilities 无应答,下次重试", s->d.chn);
        } else {
            /* ONVIF 设备:探测身份 + 按 NOPMappingONVIF.md 组能力集(Media2 ConfigurationSet +
             * Analytics 规则 + PTZ 子能力),构建与设备 getDeviceCapabilities 同构的通道能力对象。 */
            nvr_onvif_info_t info;
            if (nvr_onvif_probe(s->d.onvif_ip, s->d.onvif_port, s->d.user, s->d.pass, &info) == 0) {
                cJSON *e = cJSON_CreateObject();
                cJSON *caps = cJSON_AddArrayToObject(e, "capabilities");
                if (info.cap_mic)     cJSON_AddItemToArray(caps, cJSON_CreateString("mic"));
                if (info.cap_speaker) cJSON_AddItemToArray(caps, cJSON_CreateString("speaker"));
                if (info.cap_mic && info.cap_speaker)
                                      cJSON_AddItemToArray(caps, cJSON_CreateString("full_duplex"));
                if (info.cap_sensor)  cJSON_AddItemToArray(caps, cJSON_CreateString("sensor"));
                if (info.ptz)         cJSON_AddItemToArray(caps, cJSON_CreateString("ptz"));
                /* ptz[] 子能力 */
                if (info.ptz) {
                    cJSON *p = cJSON_AddArrayToObject(e, "ptz");
                    cJSON_AddItemToArray(p, cJSON_CreateString("pan"));
                    cJSON_AddItemToArray(p, cJSON_CreateString("tilt"));
                    cJSON_AddItemToArray(p, cJSON_CreateString("zoom"));
                    if (info.ptz_focus)   cJSON_AddItemToArray(p, cJSON_CreateString("focus"));
                    if (info.ptz_presets) cJSON_AddItemToArray(p, cJSON_CreateString("nodes"));
                    if (info.ptz_tours)   cJSON_AddItemToArray(p, cJSON_CreateString("patrol"));
                }
                /* sensors[] */
                if (info.cap_sensor) {
                    cJSON *ss = cJSON_AddArrayToObject(e, "sensors");
                    if (info.cap_motion) {
                        cJSON *m1 = cJSON_CreateObject();
                        cJSON_AddStringToObject(m1, "sensor", "motion");
                        cJSON *md = cJSON_AddArrayToObject(m1, "modes");
                        cJSON_AddItemToArray(md, cJSON_CreateString("pixelChange"));
                        cJSON_AddItemToArray(ss, m1);
                    }
                    if (info.cap_objdet) {
                        cJSON *o1 = cJSON_CreateObject();
                        cJSON_AddStringToObject(o1, "sensor", "objectDetection");
                        cJSON *md = cJSON_AddArrayToObject(o1, "modes");
                        if (info.obj_human)   cJSON_AddItemToArray(md, cJSON_CreateString("human"));
                        if (info.obj_vehicle) cJSON_AddItemToArray(md, cJSON_CreateString("vehicle"));
                        if (info.obj_animal)  cJSON_AddItemToArray(md, cJSON_CreateString("animal"));
                        if (info.obj_face)    cJSON_AddItemToArray(md, cJSON_CreateString("face"));
                        cJSON_AddItemToArray(ss, o1);
                    }
                }
                cJSON_AddBoolToObject(e, "hasBattery", 0);
                /* 内部 _ai:供 AI_getChannelAICapabilities 用(getDeviceCapabilities 聚合时剥离)。
                 * objectDetection 类别来自 GetRuleOptions 的 ClassFilter;ruledDetection 来自
                 * GetSupportedRules 的 LineDetector/FieldDetector(maxInstances 等)。 */
                if (info.cap_objdet || info.line_cross || info.field_intrusion) {
                    cJSON *ai = cJSON_AddObjectToObject(e, "_ai");
                    if (info.cap_objdet) {
                        cJSON *od = cJSON_AddArrayToObject(ai, "objectDetection");
                        if (info.obj_human)   cJSON_AddItemToArray(od, cJSON_CreateString("human"));
                        if (info.obj_vehicle) cJSON_AddItemToArray(od, cJSON_CreateString("vehicle"));
                        if (info.obj_animal)  cJSON_AddItemToArray(od, cJSON_CreateString("animal"));
                        if (info.obj_face)    cJSON_AddItemToArray(od, cJSON_CreateString("face"));
                    }
                    if (info.line_cross || info.field_intrusion) {
                        cJSON *rd = cJSON_AddObjectToObject(ai, "ruledDetection");
                        if (info.line_cross) {
                            cJSON *lc = cJSON_AddObjectToObject(rd, "lineCross");
                            cJSON_AddNumberToObject(lc, "maxLineCount", info.line_max > 0 ? info.line_max : 1);
                            cJSON_AddNumberToObject(lc, "maxPointsPerLine", info.line_max_points > 0 ? info.line_max_points : 2);
                            cJSON *dir = cJSON_AddArrayToObject(lc, "direction");
                            cJSON_AddItemToArray(dir, cJSON_CreateString("AB"));
                            cJSON_AddItemToArray(dir, cJSON_CreateString("BA"));
                            cJSON_AddItemToArray(dir, cJSON_CreateString("BOTH"));
                        }
                        if (info.field_intrusion) {
                            cJSON *fi = cJSON_AddObjectToObject(rd, "fieldIntrusion");
                            cJSON_AddNumberToObject(fi, "maxFieldCount", info.field_max > 0 ? info.field_max : 1);
                            cJSON_AddNumberToObject(fi, "maxVerticesPerField", info.field_max_verts > 0 ? info.field_max_verts : 10);
                        }
                    }
                }
                /* 身份留存(NVR 侧展示/诊断用) */
                if (info.serial[0])   cJSON_AddStringToObject(e, "serial", info.serial);
                if (info.model[0])    cJSON_AddStringToObject(e, "model", info.model);
                if (info.firmware[0]) cJSON_AddStringToObject(e, "firmware", info.firmware);
                char *cj = cJSON_PrintUnformatted(e);
                if (cj) {
                    if (nvr_settings_caps_set(m->cfg.settings, s->d.chn + 1, cj, "IPC") == 0) {
                        stored = 1;
                        NVR_LOGI("chan", "ch%d ONVIF 能力入库: %s", s->d.chn, cj);
                    } else NVR_LOGW("chan", "ch%d ONVIF 能力入库失败(DB busy?),下次重试", s->d.chn);
                    free(cj);
                }
                cJSON_Delete(e);
            }
        }
        if (stored) s->caps_probed = 1;   /* 成功才封口;失败保持 0 → 下 tick 重试 */
    }
}

int nvr_chan_set_stream(nvr_chan_mgr_t *m, int chn, int stream)
{
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use || s->status == NVR_CHAN_DISABLED) return -1;
    /* ★ 双流:主+子两路都在拉(tick 各自 set_url),切换只改**喂解码器的码流**(单宫格=主/多宫格=子)。
     * 瞬时、不重连、不重解析。set_decode_stream 内部换解码源即时出图。 */
    s->d.stream = stream;
    nvr_stream_set_decode_stream(m->cfg.sm, chn, stream);
    NVR_LOGI("chan", "ch%d 切%s码流(双流已拉,瞬时)", chn, stream == NVR_STREAM_SUB ? "子" : "主");
    return 0;
}

void nvr_chan_tick(nvr_chan_mgr_t *m)
{
    if (!m) return;
    time_t now = time(NULL);
    /* 每 tick 最多解析 2 路 ONVIF URL(每路 ~2s)。空口已被 ARP 门控跳过、不占额度,故这里只花在
     * 在场相机上 → 首轮出图更快;上限 2 兼顾主循环杂务不被过久阻塞(视频/8089 各自线程不受影响)。 */
    int resolve_budget = 2;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use) tick_slot(m, &m->slots[i], now, &resolve_budget);

    /* ---- PoE 即插即用发现(与 LAN 统一走 ONVIF Discovery):对**未出图**的 PoE 口通道,在其
     * 网段(源 198.18.<口>.100)做 ONVIF 广播发现,扫到相机(任意 IP)→ on_discovered 按 198.18.<口>
     * 子网绑定/更新该口通道并置 poe_present。每口 10s 退避、每 tick 至多 1 口(2s 广播,budget 外单列)。 */
    int poe_n = (NVR_POE_PORTS < NVR_MAX_CH) ? NVR_POE_PORTS : NVR_MAX_CH;
    for (int k = 0; k < poe_n; k++) {
        int i = (m->poe_scan_cursor + k) % poe_n;        /* 游标轮询:公平覆盖所有 PoE 口 */
        slot_t *s = &m->slots[i];
        if (!s->in_use || s->d.poe_port <= 0) continue;
        if (s->status == NVR_CHAN_ONLINE) continue;      /* 真正在线出图才停发现;在线无流(掉线/NOSIGNAL/
                                                          * 未解析)都继续发现→刷新在场/IP→重连(不只看 url 缓存) */
        if (now < s->poe_disc_next) continue;
        int seg = ip_seg3(s->d.onvif_ip);                /* 网段第3段(VLAN 2001=NVR,口 P→段 P+1) */
        if (seg < 0) continue;
        s->poe_disc_next = now + 10;
        m->poe_scan_cursor = (i + 1) % poe_n;            /* 下轮从下一口起 */
        char src[64];                                     /* 发现源=NVR 在该 VLAN 的 IP 198.18.<seg>.100 */
        snprintf(src, sizeof(src), "%d.%d.%d.100", NVR_POE_NET_A, NVR_POE_NET_B, seg);
        disc_ctx_t ctx = { m, 0, 0 };                    /* 非 recall:允许绑定/更新口通道 */
        NVR_LOGI("chan", "PoE ch%d(口%d,段198.18.%d)ONVIF 发现广播 src=%s", s->d.chn, s->d.poe_port, seg, src);
        nvr_onvif_discover(src, 2, on_discovered, &ctx);
        break;                                            /* 每 tick 至多 1 口(2s 广播) */
    }

    /* ---- LAN 找回:已添加(DB)的 LAN 设备(poe_port==0、有 mac)掉线 → 在 eth0 按 mac 重发现其
     * 当前 IP(DHCP 变更),recall_only 只更新不新增。开机不主动扫 LAN;仅"已添加连不上"才找回。
     * 每路 30s 退避、每 tick 至多 1 路(2s 广播)。 */
    for (int i = NVR_IP_CH_BASE; i < NVR_MAX_CH; i++) {
        slot_t *s = &m->slots[i];
        if (!s->in_use || s->d.poe_port > 0 || !s->d.mac[0]) continue;
        if (s->status != NVR_CHAN_NOSIGNAL && s->status != NVR_CHAN_FAIL) continue;  /* 只找回掉线的 */
        if (now < s->recall_next) continue;
        s->recall_next = now + 30;
        char eth0ip[64];
        if (iface_ipv4("eth0", eth0ip, sizeof(eth0ip)) == 0 && eth0ip[0]) {
            disc_ctx_t ctx = { m, 0, 1 };   /* recall_only:只更新已知,不新增 */
            NVR_LOGI("chan", "ch%d LAN 掉线找回:eth0(%s) 按 mac=%s 重发现当前 IP", i, eth0ip, s->d.mac);
            nvr_onvif_discover(eth0ip, 2, on_discovered, &ctx);
        }
        break;   /* 每 tick 至多 1 路找回 */
    }
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
