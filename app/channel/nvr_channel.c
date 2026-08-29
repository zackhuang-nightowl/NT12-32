/***************************************************************************************
 *  nvr_channel.c — 通道管理 + 在线状态机。见 nvr_channel.h / 计划 §B1。
 ***************************************************************************************/
#include "nvr_channel.h"
#include "nvr_defaults.h"     /* PoE 内网段宏 NVR_POE_NET_A/B */
#include "nvr_onvif.h"        /* nvr_onvif_get_url（弱兜底在 app/onvif 提供强符号） */
#include "nvr_lan34569.h"     /* UDP 34569 LocalLAN 备用发现 / MAC 找回 */
#include "nvr_dev_classify.h" /* nvr_dev_classify */
#include "nvr_chan_nop_sync.h"
#include "nvr_chan_bind.h"
#include "nvr_netime.h"
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>       /* getifaddrs —— 取 eth0 IP 作 LAN 发现的多播源 */
#include <arpa/inet.h>     /* inet_ntop */
#include <netinet/in.h>    /* sockaddr_in */
#include <curl/curl.h>     /* NOP 设备 getDeviceCapabilities 直取(POST 8089/APPJsonCmd) */
#include "cJSON.h"
#include "nop_sdk/nop_app.h"  /* ONVIF 能力集经 nop 进程内映射收集入库(nop_app_dispatch) */

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
/* 空口令 = 无鉴权，不带 HTTP Digest；123456 视为真实口令(带鉴权)。 */
static int chan_nop_need_auth(const char *user, const char *pass)
{
    if (!user || !user[0]) return 0;
    if (!pass || !pass[0]) return 0;
    return 1;
}

/* 向 NOP/nopOnvif 设备 POST 一条 JSON 命令,返回 malloc 应答体(调用方 free);失败返回 NULL。 */
static char *chan_nop_post(const char *ip, int port, const char *user, const char *pass,
                           const char *json_in)
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
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (chan_nop_need_auth(user, pass)) {
        char up[160]; snprintf(up, sizeof(up), "%s:%s", user, pass);
        curl_easy_setopt(c, CURLOPT_USERPWD, up);
        curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_DIGEST | CURLAUTH_BASIC));
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdr); curl_easy_cleanup(c);
    if (rc != CURLE_OK) { free(m.buf); return NULL; }
    return m.buf;
}

static int json_status_code(cJSON *root)
{
    cJSON *sc = root ? cJSON_GetObjectItem(root, "statusCode") : NULL;
    if (!sc) {
        cJSON *r = root ? cJSON_GetObjectItem(root, "result") : NULL;
        sc = r ? cJSON_GetObjectItem(r, "statusCode") : NULL;
    }
    return (sc && cJSON_IsNumber(sc)) ? sc->valueint : 0;
}

/* capabilities[] 字符串数组是否已含某项(能力去重用)。 */
static int caps_arr_has(cJSON *arr, const char *s)
{
    if (!arr || !s) return 0;
    for (cJSON *it = arr->child; it; it = it->next)
        if (cJSON_IsString(it) && it->valuestring && strcmp(it->valuestring, s) == 0) return 1;
    return 0;
}

/* light 能力存在时补 light[];NOPMappingONVIF 默认至少 fixed。 */
static void caps_ensure_light_modes(cJSON *e)
{
    cJSON *capsarr, *lightarr;
    if (!e) return;
    capsarr = cJSON_GetObjectItem(e, "capabilities");
    if (!caps_arr_has(capsarr, "light")) return;
    lightarr = cJSON_GetObjectItem(e, "light");
    if (!lightarr) {
        lightarr = cJSON_AddArrayToObject(e, "light");
        cJSON_AddItemToArray(lightarr, cJSON_CreateString("fixed"));
        return;
    }
    if (cJSON_IsArray(lightarr) && cJSON_GetArraySize(lightarr) == 0)
        cJSON_AddItemToArray(lightarr, cJSON_CreateString("fixed"));
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
    int               url_resolving;/* 该通道正有一个解析 worker 在跑(并发解析:防同通道重复派线程) */
    time_t            recall_next; /* 下次允许"LAN 找回"重发现的时间（已添加 LAN 设备连不上时；30s 退避） */
    char              url_main[256]; /* 已解析的主码流取流 URL 缓存（切主/子码流免重新 ONVIF 解析→秒切） */
    char              url_sub[256];  /* 已解析的子码流取流 URL 缓存 */
    int               caps_probed; /* 首次上线已按设备探测并写 camera_capability(只探一次) */
    int               auth_ready;   /* BIND_IPC 握手已写出账密 */
    int               first_add;    /* 首次添加(PoE/LAN Add/改密)：只试一轮，失败等用户密码 */
    int               prio;         /* 用户刚加/改密:下一 tick 优先解析(单列额度),不排队等开机批量解析 */
    int               poe_present;  /* PoE 口:ONVIF 广播发现到相机在场(替代写死 .1 的 ARP 门控) */
    time_t            poe_disc_next;/* PoE 口下次允许 ONVIF 广播发现的时间(退避) */
    int               poe_seen;     /* 本轮发现是否命中该口相机(每轮探测前清0,on_discovered 命中置1) */
    int               poe_miss;     /* 连续未发现轮数;≥阈值→相机已移除→清空口(PoE 拔线即清) */
} slot_t;

#define NVR_POE_MISS_CLEAR 2   /* PoE 连续 N 轮(每轮~10s)未发现 → 判定相机移除,清空该口 */

/* 已落库设备重连：限次+退避。首次添加另走 first_add，一轮失败即停。 */
#define NVR_URL_MAX_TRIES 2

struct nvr_chan_mgr {
    nvr_chan_mgr_cfg_t cfg;
    slot_t slots[NVR_MAX_CH];
    unsigned notify_mask;   /* 状态变化位图(bit=chn)：上线/掉线置位,GUI_longPolling drain→ChannelStatusNotify */
    int      lp_poke;       /* 非通道状态唤醒(向导 APPNotifySetupStatus 等) */
    int      poe_scan_cursor; /* PoE 发现轮询游标(公平轮扫所有未出图 PoE 口,避免只扫前几口) */
    /* 保护 slots[]/poe_scan_cursor:写线程(主循环 nvr_chan_tick + 其内发现回调)
     * 与 8089 派发线程池(≤5)的读/增删并发。递归锁——因公有函数彼此嵌套(add→remove、
     * tick/run_discovery→on_discovered→add、status_code_of→status)。tick 内两处多秒网络 I/O
     * (URL 解析 / 能力探测)按"快照→解锁 I/O→重锁→校验"释放本锁,避免拖住 getChannelStatus。 */
    pthread_mutex_t lock;
    /* notify_mask / longPolling 挂起专用(非递归)。与 lock 分离,避免 tick 持 cm 锁做 ONVIF 时
     * 卡住 GUI_longPolling 的 cond_wait。加锁顺序若两把都要:先 lock 再 notify_mu。 */
    pthread_mutex_t notify_mu;
    pthread_cond_t  notify_cv;

    /* ★ 异步持久化:persist_camera 的 SQLite 写(几十ms)移出 CM_LOCK → 单写库线程串行执行。
     * 调用方在 CM_LOCK 内只做"快照 d + 标记 dirty"(微秒级),写库离开 CM_LOCK → 解析/发现再密集
     * 也不占 CM_LOCK,8089 命令全程不被阻塞。按通道合并(最新覆盖)+ 单写者 → 有序、无 SQLITE_BUSY、
     * 不丢更新。persist_mu 独立于 CM_LOCK(锁序:CM_LOCK→persist_mu;写库线程只持 persist_mu,不碰 CM_LOCK)。 */
    pthread_t       persist_thr;
    int             persist_thr_ok;
    pthread_mutex_t persist_mu;
    pthread_cond_t  persist_cv;
    volatile int    persist_stop;
    nvr_channel_t   persist_pending[NVR_MAX_CH];   /* 每通道待写的最新快照 */
    unsigned char   persist_dirty[NVR_MAX_CH];     /* 1=该通道有待写快照 */
};

/* 递归锁便捷宏(空指针安全由各入口的 !m 检查兜底)。 */
#define CM_LOCK(m)   pthread_mutex_lock(&(m)->lock)
#define CM_UNLOCK(m) pthread_mutex_unlock(&(m)->lock)

/* 取点分十进制 IPv4 的第 3 段(198.18.<seg>.x → seg);失败返回 -1。 */
static int ip_seg3(const char *ip)
{
    int a, b, c, d;
    if (ip && sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) return c;
    return -1;
}

/* W0: 合法点分 IPv4?(拦截 ONVIF 发现回的 IPv6 链路本地 fe80:: 等脏地址,见 PoE 脏数据根因)。 */
static int is_ipv4(const char *ip)
{
    int a, b, c, d; char extra;
    if (!ip || !ip[0]) return 0;
    if (sscanf(ip, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return 0;   /* 多字符=非纯IPv4 */
    return a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
           c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

/* W0: 该地址是否为 PoE 口 <port> 的合法段内 IPv4(段=口号,198.18.<port>.x)。 */
static int is_ipv4_in_poe_seg(const char *ip, int port)
{
    int a, b, c, d;
    if (!is_ipv4(ip) || sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return 0;
    return a == NVR_POE_NET_A && b == NVR_POE_NET_B && c == port;
}

/* 置 ChannelStatusNotify 位并唤醒挂起的 GUI_longPolling。 */
static void chan_notify(nvr_chan_mgr_t *m, int chn)
{
    if (!m || chn < 0 || chn >= NVR_MAX_CH) return;
    pthread_mutex_lock(&m->notify_mu);
    m->notify_mask |= (1u << chn);
    pthread_cond_broadcast(&m->notify_cv);
    pthread_mutex_unlock(&m->notify_mu);
}

/* 读并清状态变化位图。供 GUI_longPolling 返回 ChannelStatusNotify（gui 据此重拉 getChannelStatus 出图）。 */
unsigned nvr_chan_drain_notify(nvr_chan_mgr_t *m)
{
    return nvr_chan_wait_notify(m, 0);
}

unsigned nvr_chan_wait_notify(nvr_chan_mgr_t *m, int timeout_ms)
{
    unsigned v;
    if (!m) return 0;
    pthread_mutex_lock(&m->notify_mu);
    if (m->notify_mask == 0 && !m->lp_poke && timeout_ms > 0) {
        struct timespec abs;
        clock_gettime(CLOCK_MONOTONIC, &abs);
        abs.tv_sec += timeout_ms / 1000;
        abs.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; }
        while (m->notify_mask == 0 && !m->lp_poke) {
            if (pthread_cond_timedwait(&m->notify_cv, &m->notify_mu, &abs) != 0) break;
        }
    }
    v = m->notify_mask;
    m->notify_mask = 0;
    m->lp_poke = 0;
    pthread_mutex_unlock(&m->notify_mu);
    return v;
}

void nvr_chan_poke_longpoll(nvr_chan_mgr_t *m)
{
    if (!m) return;
    pthread_mutex_lock(&m->notify_mu);
    m->lp_poke = 1;
    pthread_cond_broadcast(&m->notify_cv);
    pthread_mutex_unlock(&m->notify_mu);
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
static void persist_camera(nvr_chan_mgr_t *m, nvr_channel_t *d);
static void *persist_writer_thread(void *arg);   /* 单写库线程(异步持久化) */
static void sync_nop_registry(nvr_chan_mgr_t *m, const nvr_channel_t *d);

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
    if (!m || !sub) return;   /* 空指针忽略 */
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (s && memcmp(&s->sub, sub, sizeof(*sub)) != 0) {
        s->sub = *sub;        /* 7未激活/6升级/2休眠/5超解码 → 立刻推 GUI */
        chan_notify(m, chn);
    }
    CM_UNLOCK(m);
}

void nvr_chan_set_fw_updating(nvr_chan_mgr_t *m, int chn, int on)
{
    if (!m) return;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    int want = on ? 1 : 0;
    if (s && s->sub.fw_updating != want) {
        s->sub.fw_updating = want;
        chan_notify(m, chn);
    }
    CM_UNLOCK(m);
}

/* 有没有物理设备：空 PoE 口=0；PoE 已发现/LAN 已 Add=有。不要求先有 MAC。 */
static int slot_has_device(const slot_t *s)
{
    if (!s || !s->in_use || !s->d.enabled) return 0;
    if (s->status == NVR_CHAN_ONLINE || s->d.mac[0] || s->d.serial[0] || s->poe_present)
        return 1;
    if (s->d.poe_port > 0)
        return 0;   /* 空 PoE 口：配置里的 .1/.100 都只是占位，要等发现 */
    return s->d.onvif_ip[0] != 0;   /* LAN / 手动 Add：有 IP 即已绑设备 */
}

int nvr_chan_status_code_of(nvr_chan_mgr_t *m, int chn)
{
    if (!m) return 0;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    /* ★ 无真实设备(空 PoE 口/未发现,DB 无记录)→ 状态 0。不再把空口的 ONVIF 探测失败误报为
     * 连接中(3)/鉴权失败(4)。状态按实际设备记录:有真机才反映其在线/连接/子状态。 */
    int rc;
    if (!slot_has_device(s)) {
        rc = 0;
    } else {
        nvr_chan_status_t st = nvr_chan_status(m, chn);   /* 递归重入本锁 */
        rc = nvr_chan_status_code(conn_of(st), &s->sub);
    }
    CM_UNLOCK(m);
    return rc;
}

int nvr_chan_wait_bind(nvr_chan_mgr_t *m, int chn, int timeout_ms)
{
    if (!m) return -1;
    if (timeout_ms <= 0) timeout_ms = NVR_DEF_CMD_TIMEOUT_S * 1000;
    int waited = 0, step = 50;
    while (waited < timeout_ms) {
        int pending = 0;
        CM_LOCK(m);
        slot_t *s = slot_of(m, chn);
        if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
        pending = s->first_add;
        CM_UNLOCK(m);
        if (!pending || nvr_chan_status_code_of(m, chn) == 1) return 0;
        usleep((useconds_t)step * 1000);
        waited += step;
    }
    return 1;
}

int nvr_chan_set_auth(nvr_chan_mgr_t *m, int chn, const char *user, const char *pass)
{
    if (!m) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
    snprintf(s->d.user, sizeof(s->d.user), "%s", user ? user : "admin");
    snprintf(s->d.pass, sizeof(s->d.pass), "%s", pass ? pass : "");
    s->d.url[0] = 0; s->url_main[0] = 0; s->url_sub[0] = 0;
    s->auth_ready = 0; s->url_tries = 0; s->url_next = 0;
    s->sub.auth_fail = 0;
    s->sub.inactive = 0;
    persist_camera(m, &s->d);
    sync_nop_registry(m, &s->d);
    if (m->cfg.sm) {
        nvr_stream_stop(m->cfg.sm, chn);
        nvr_stream_start(m->cfg.sm, chn);
    }
    CM_UNLOCK(m);
    return 0;
}

int nvr_chan_set_enh(nvr_chan_mgr_t *m, int chn, const char *random, const char *penh)
{
    if (!m) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
    snprintf(s->d.user, sizeof(s->d.user), "admin");
    snprintf(s->d.pass, sizeof(s->d.pass), "%s", (random && random[0] && penh) ? penh : "");
    snprintf(s->d.enh_random, sizeof(s->d.enh_random), "%s", random ? random : "");
    s->d.url[0] = 0; s->url_main[0] = 0; s->url_sub[0] = 0;
    s->auth_ready = 0; s->url_tries = 0; s->url_next = 0;
    s->sub.auth_fail = 0;
    persist_camera(m, &s->d);
    sync_nop_registry(m, &s->d);
    if (m->cfg.sm) {
        nvr_stream_stop(m->cfg.sm, chn);
        nvr_stream_start(m->cfg.sm, chn);
    }
    CM_UNLOCK(m);
    return 0;
}

int nvr_chan_noponvif_priv_func(const char *func)
{
    /* nightowl_protocol.md：白灯 / 警笛 / 一键报警 / 激活。GET 回 501 = 设备不支持。 */
    static const char *const k[] = {
        "X_NightOwl_getChannelLightSwitch",
        "X_NightOwl_setChannelLightSwitch",
        "X_NightOwl_getChannelLightDetectionSwitch",
        "X_NightOwl_setChannelLightDetectionSwitch",
        "X_NightOwl_getChannelAudioAlertSwitch",
        "X_NightOwl_setChannelAudioAlertSwitch",
        "X_NightOwl_getChannelAudioAlertDetectSwitch",
        "X_NightOwl_setChannelAudioAlertDetectSwitch",
        "X_NightOwl_getChannelAudioAlert",
        "X_NightOwl_setChannelAudioAlert",
        "X_NightOwl_getPanicSwitch",
        "X_NightOwl_setPanicSwitch",
        "X_NightOwl_getDeviceActive",
        "X_NightOwl_setDeviceActive",
    };
    size_t i;
    if (!func || !func[0]) return 0;
    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (strcmp(func, k[i]) == 0) return 1;
    return 0;
}

char *nvr_chan_dev_post(nvr_chan_mgr_t *m, int chn, const char *func, const char *args_json)
{
    if (!m) return NULL;
    nvr_channel_t snap;
    char body[640], *resp;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use || !s->d.onvif_ip[0]) {
        CM_UNLOCK(m);
        return NULL;
    }
    if (s->d.kind == NVR_DEV_KIND_ONVIF ||
        (s->d.kind == NVR_DEV_KIND_NOPONVIF && !nvr_chan_noponvif_priv_func(func))) {
        CM_UNLOCK(m);
        return NULL;
    }
    snap = s->d;
    {
        int dev_ch = snap.dev_chn > 0 ? snap.dev_chn : 1;
        if (args_json && args_json[0])
            snprintf(body, sizeof(body), "{\"func\":\"%s\",\"args\":%s}", func ? func : "", args_json);
        else
            snprintf(body, sizeof(body), "{\"func\":\"%s\",\"args\":{\"channel\":%d}}",
                     func ? func : "", dev_ch);
    }
    CM_UNLOCK(m);
    resp = nvr_chan_bind_post(&snap, body);
    if (resp) {
        nvr_channel_t cur;
        if (nvr_chan_get(m, chn, &cur) == 0 &&
            (strcmp(cur.pass, snap.pass) != 0 || strcmp(cur.enh_random, snap.enh_random) != 0))
            nvr_chan_set_enh(m, chn, snap.enh_random, snap.pass);
    }
    return resp;
}

int nvr_chan_mgr_init(const nvr_chan_mgr_cfg_t *cfg, nvr_chan_mgr_t **out)
{
    if (!cfg || !cfg->sm || !out) return -1;
    nvr_chan_mgr_t *m = calloc(1, sizeof(*m));
    if (!m) return -1;
    m->cfg = *cfg;
    if (m->cfg.reconnect_base_s <= 0) m->cfg.reconnect_base_s = 5;
    if (m->cfg.reconnect_max_s  <= 0) m->cfg.reconnect_max_s  = 30;
    {
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&m->lock, &a);
        pthread_mutexattr_destroy(&a);
        pthread_mutex_init(&m->notify_mu, NULL);
        {
            pthread_condattr_t ca;
            pthread_condattr_init(&ca);
            pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
            pthread_cond_init(&m->notify_cv, &ca);
            pthread_condattr_destroy(&ca);
        }
    }
    /* ★ 起单写库线程:persist_camera 的 SQLite 写异步化(移出 CM_LOCK)。起线程前 persist_thr_ok=0
     * → 期间的 persist_camera 走同步兜底(极早期,极少)。 */
    pthread_mutex_init(&m->persist_mu, NULL);
    pthread_cond_init(&m->persist_cv, NULL);
    m->persist_stop = 0;
    if (pthread_create(&m->persist_thr, NULL, persist_writer_thread, m) == 0)
        m->persist_thr_ok = 1;
    else
        NVR_LOGE("chan", "写库线程起失败 → persist_camera 退化为 CM_LOCK 内同步写(解析期可能拖慢 8089)");
    *out = m;
    return 0;
}

void nvr_chan_mgr_deinit(nvr_chan_mgr_t *m)
{
    if (!m) return;
    /* ★ 停写库线程:置 stop→唤醒→线程排空剩余待写后退出→join(不丢最后的写)。 */
    if (m->persist_thr_ok) {
        pthread_mutex_lock(&m->persist_mu);
        m->persist_stop = 1;
        pthread_cond_signal(&m->persist_cv);
        pthread_mutex_unlock(&m->persist_mu);
        pthread_join(m->persist_thr, NULL);
        m->persist_thr_ok = 0;
    }
    pthread_cond_destroy(&m->persist_cv);
    pthread_mutex_destroy(&m->persist_mu);
    pthread_cond_destroy(&m->notify_cv);
    pthread_mutex_destroy(&m->notify_mu);
    pthread_mutex_destroy(&m->lock);   /* 调用方须已停止 tick/派发线程 */
    free(m);
}

void nvr_chan_mgr_set_nop_registry(nvr_chan_mgr_t *m, struct nop_nvr_channels *reg)
{
    if (!m) return;
    CM_LOCK(m);
    m->cfg.nop_chans = reg;
    CM_UNLOCK(m);
}

void nvr_chan_mgr_set_onvif_backend(nvr_chan_mgr_t *m, nop_onvif_map_backend_t *be)
{
    if (!m) return;
    CM_LOCK(m);
    m->cfg.onvif_be = be;
    CM_UNLOCK(m);
}

static void sync_nop_registry(nvr_chan_mgr_t *m, const nvr_channel_t *d)
{
    if (m && m->cfg.nop_chans)
        nvr_chan_nop_sync_upsert(m->cfg.nop_chans, d, m->cfg.onvif_be);
}

static void unsync_nop_registry(nvr_chan_mgr_t *m, int chn)
{
    if (m && m->cfg.nop_chans)
        nvr_chan_nop_sync_remove(m->cfg.nop_chans, chn, m->cfg.onvif_be);
}

/* 解析取流 URL：显式 url 优先；否则 onvif_auto+ip → nvr_onvif_get_url。成功写 cc.url。 */
static int resolve_url(const nvr_channel_t *d, char *url, int cap, char *scopes, int scap)
{
    if (scopes && scap > 0) scopes[0] = 0;
    if (d->url[0]) { snprintf(url, cap, "%s", d->url); return 0; }
    if (d->onvif_auto && d->onvif_ip[0]) {
        const char *st = (d->stream == NVR_STREAM_SUB) ? "sub" : "main";
        if (nvr_onvif_get_url(d->onvif_ip, d->onvif_port, d->user, d->pass, st, url, cap, scopes, scap,
                              d->video_source_token) == 0)
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
    s->auth_ready = 0;
    s->first_add = 0;                     /* load_config 默认非首次；运行时 add 再置 1 */
    memset(&s->sub, 0, sizeof(s->sub));   /* 清掉复用 slot 号残留的上一占用者子状态 */

    if (d->enabled == 0) { s->status = NVR_CHAN_DISABLED; return 0; }

    nvr_stream_chan_cfg_t cc; memset(&cc, 0, sizeof(cc));
    cc.chn = d->chn; cc.codec = d->codec; cc.stream = d->stream;
    cc.record = d->record; cc.vout_win = d->vout_win;
    /* ★ 全通道强制 RTP over RTSP/TCP:TCP 自带丢包重传,主/子录像与 live 共用拉流,
     * 避免 PoE UDP 丢包导致录像缺帧 + 硬解 GAPS_DROP。不再按网段切 UDP。 */
    cc.over_tcp = 1;
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
    CM_LOCK(m);
    for (int i = 0; i < cfg->nch; i++) {
        const nvr_channel_t *e = &cfg->ch[i];
        if (e->chn < 0 || e->chn >= NVR_MAX_CH) continue;
        nvr_channel_t d = *e;
        if (d.enabled == 0 && e->enabled == 0) d.enabled = 0; else d.enabled = 1;
        /* W7: 清洗历史脏数据——PoE 口若持久化了非段内 IPv4(如误写的 fe80::)→ 回占位 198.18.<口>.1,
         * 防脏地址复活并毒化按段发现/清口逻辑(段内合法 IPv4/占位本身不受影响)。 */
        if (d.poe_port > 0 && !is_ipv4_in_poe_seg(d.onvif_ip, d.poe_port)) {
            NVR_LOGW("chan", "load: PoE ch%d 脏 onvif_ip=%s → 回占位 198.18.%d.1",
                     d.chn, d.onvif_ip[0] ? d.onvif_ip : "(空)", d.poe_port);
            snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%d.%d.%d.1",
                     NVR_POE_NET_A, NVR_POE_NET_B, d.poe_port);
        }
        if (install_slot(m, &d) >= 0) n++;
    }
    CM_UNLOCK(m);
    return n;
}

/* 统一成小写 aa:bb:cc:dd:ee:ff，供按 mac 找回(strcasecmp 也挡不住 '-' vs ':')。全 0 视为无效。 */
static int mac_norm(const char *in, char *out, size_t cap)
{
    unsigned char b[6];
    int n = 0, z = 1;
    if (!out || cap < 18) return -1;
    out[0] = 0;
    if (!in) return -1;
    while (*in && n < 6) {
        if (*in == ':' || *in == '-' || *in == '.' || *in == ' ') { in++; continue; }
        if (!isxdigit((unsigned char)in[0]) || !isxdigit((unsigned char)in[1])) break;
        {
            char h[3] = { (char)tolower((unsigned char)in[0]),
                          (char)tolower((unsigned char)in[1]), 0 };
            b[n] = (unsigned char)strtol(h, NULL, 16);
            if (b[n]) z = 0;
            n++;
            in += 2;
        }
    }
    if (n != 6 || z) return -1;
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    return 0;
}

/* 按 IP 从内核 ARP 表(/proc/net/arp)匹配物理 MAC。ARP 表只含已通信过的 IP(NVR 连相机后即有),
 * 是设备真实 MAC 的权威来源(ONVIF scope / GetNetworkInterfaces 可能缺失或格式不一)。找到返 0。 */
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
            if (strcmp(aip, ip) == 0 && mac_norm(mac, out, cap) == 0) { rc = 0; break; }
        }
    }
    fclose(f);
    return rc;
}

/* W4: PoE 相机在场探测(二层,鲁棒于相机屏蔽 ICMP)。先 ping 触发 ARP 解析(即便 ICMP 被丢,
 * 内核仍会发 ARP 请求),再查 /proc/net/arp 是否有该 IP 的完整表项。有=在场。 */
static int poe_cam_reachable(const char *ip)
{
    if (!is_ipv4(ip)) return 0;
    char cmd[96], mac[24];
    snprintf(cmd, sizeof(cmd), "ping -c1 -W1 %s >/dev/null 2>&1", ip);
    (void)!system(cmd);
    return arp_mac_of_ip(ip, mac, sizeof(mac)) == 0 && mac[0];
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

/* ---- 设备落库(camera 表)。有真实 ip 即写；mac/型号/序列号等随后由连接补全。 ----
 * ★ MAC 权威性:优先用 ARP 表(按 IP 匹配的物理 mac)覆盖 ONVIF scope mac,并回写 slot(d->mac),
 * 使"库里的 mac""内存里的 mac"一致 → IP 变更后按 mac 找回(chn_by_mac / find_by_mac)可靠。
 * ARP 尚未就绪时仍用 discovery/GUI 带来的 mac（可空）先入库，不阻塞添加。 */
/* 记录构建 + upsert:慢 SQLite 写。由**写库线程**用快照跑(不占 CM_LOCK);线程未起时同步兜底。 */
static void persist_camera_write(nvr_settings_t *settings, const nvr_channel_t *d)
{
    if (!settings || !d || !d->onvif_ip[0]) return;
    nvr_camera_row_t r; memset(&r, 0, sizeof(r));
    r.chn = d->chn;                                     /* 内部 0-based;协议边界再 +1 */
    r.enabled = d->enabled;
    snprintf(r.source,   sizeof(r.source),   "%s", d->poe_port > 0 ? "POE" : "LAN");
    snprintf(r.protocol, sizeof(r.protocol), "%s", d->kind == NVR_DEV_KIND_NOP ? "nop" : "onvif");
    r.kind = d->kind; r.backend = d->backend;
    /* 多视频源:type/dev_chn/token 按 channel 实际值落库(额外源 type=multi、dev_chn=源序号、
     * token=VideoSourceToken);重启后 get_url 按 token 拉各自的源流。 */
    snprintf(r.type, sizeof(r.type), "%s", d->type[0] ? d->type : "single");
    r.dev_chn = d->dev_chn > 0 ? d->dev_chn : 1;
    snprintf(r.video_source_token, sizeof(r.video_source_token), "%s", d->video_source_token);
    snprintf(r.enh_random, sizeof(r.enh_random), "%s", d->enh_random);
    snprintf(r.ip,       sizeof(r.ip),       "%s", d->onvif_ip);
    snprintf(r.mac,      sizeof(r.mac),      "%s", d->mac);
    snprintf(r.username, sizeof(r.username), "%s", d->user);
    snprintf(r.password, sizeof(r.password), "%s", d->pass);
    r.onvif_port = d->onvif_port;
    r.nop_port = d->onvif_port > 0 ? d->onvif_port : 80;
    snprintf(r.service_url, sizeof(r.service_url), "%s", d->service_url);
    snprintf(r.serial, sizeof(r.serial), "%s", d->serial);
    snprintf(r.model, sizeof(r.model), "%s", d->model);   /* 型号(hardware) 持久化,重启后清单可回显 */
    snprintf(r.firmware, sizeof(r.firmware), "%s", d->firmware);
    snprintf(r.url, sizeof(r.url), "%s", d->url);
    snprintf(r.url_main, sizeof(r.url_main), "%s", d->url_main);   /* 解析后主/子流地址落库(同设备直连) */
    snprintf(r.url_sub,  sizeof(r.url_sub),  "%s", d->url_sub);
    r.onvif_auto = d->onvif_auto; r.poe_port = d->poe_port;
    r.codec = d->codec; r.stream = d->stream; r.record = d->record;
    snprintf(r.name, sizeof(r.name), "%s", d->name);
    nvr_settings_camera_upsert(settings, &r);
}

/* 单写库线程:串行 drain 各通道待写快照,写库离开 CM_LOCK。停止时先排空再退出(不丢最后的写)。 */
static void *persist_writer_thread(void *arg)
{
    nvr_chan_mgr_t *m = (nvr_chan_mgr_t *)arg;
    for (;;) {
        nvr_channel_t snap; int found = -1;
        pthread_mutex_lock(&m->persist_mu);
        for (;;) {
            for (int i = 0; i < NVR_MAX_CH; i++) if (m->persist_dirty[i]) { found = i; break; }
            if (found >= 0 || m->persist_stop) break;
            pthread_cond_wait(&m->persist_cv, &m->persist_mu);
        }
        if (found < 0) { pthread_mutex_unlock(&m->persist_mu); break; }   /* 已停且无待写 → 退出 */
        snap = m->persist_pending[found];       /* 取最新快照(合并后) */
        m->persist_dirty[found] = 0;
        pthread_mutex_unlock(&m->persist_mu);
        persist_camera_write(m->cfg.settings, &snap);   /* ★ 慢 DB 写:全程锁外(既不占 CM_LOCK 也不占 persist_mu) */
    }
    return NULL;
}

/* 在调用方 CM_LOCK 内调用:①ARP mac 就地回写 slot(快);②入队快照交写库线程(微秒级),不在锁内写库。 */
static void persist_camera(nvr_chan_mgr_t *m, nvr_channel_t *d)
{
    if (!m->cfg.settings || !d) return;
    if (!d->onvif_ip[0]) return;                        /* 无 ip 不落库 */
    char amac[24];
    if (arp_mac_of_ip(d->onvif_ip, amac, sizeof(amac)) == 0 && amac[0])
        snprintf(d->mac, sizeof(d->mac), "%s", amac);   /* ARP mac 权威,覆盖并回写 slot(锁内就地改 d) */
    else if (d->mac[0]) {
        char nm[24];
        if (mac_norm(d->mac, nm, sizeof(nm)) == 0)
            snprintf(d->mac, sizeof(d->mac), "%s", nm);
    }
    if (m->persist_thr_ok && d->chn >= 0 && d->chn < NVR_MAX_CH) {
        /* 入队:快照 d + 标记 dirty(按通道合并,最新覆盖)。写库离开 CM_LOCK,8089 全程不受解析影响。 */
        pthread_mutex_lock(&m->persist_mu);
        m->persist_pending[d->chn] = *d;
        m->persist_dirty[d->chn] = 1;
        pthread_cond_signal(&m->persist_cv);
        pthread_mutex_unlock(&m->persist_mu);
    } else {
        persist_camera_write(m->cfg.settings, d);       /* 兜底:写库线程未起/已停 → 同步写 */
    }
}

/* ARP 权威；discovery 无 mac 且 ARP 未就绪时用 GetNetworkInterfaces 补。mac 变了返 1。 */
static int adopt_mac(nvr_channel_t *d, const char *arp, const char *onvif_hw)
{
    char n[24];
    n[0] = 0;
    if (arp && arp[0] && mac_norm(arp, n, sizeof(n)) == 0) {
        /* ARP 优先 */
    } else if (!(d->mac[0]) && onvif_hw && onvif_hw[0]) {
        mac_norm(onvif_hw, n, sizeof(n));
    }
    if (!n[0]) return 0;
    if (strcasecmp(d->mac, n) == 0) return 0;
    snprintf(d->mac, sizeof(d->mac), "%s", n);
    return 1;
}
static void forget_camera(nvr_chan_mgr_t *m, int chn)
{
    if (m->cfg.settings) nvr_settings_camera_delete(m->cfg.settings, chn);
}

int nvr_chan_add(nvr_chan_mgr_t *m, const nvr_channel_t *desc)
{
    if (!m || !desc) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, desc->chn);
    if (!s) { CM_UNLOCK(m); return -1; }
    if (s->in_use) nvr_chan_remove(m, desc->chn);       /* 递归重入本锁 */
    int rc = install_slot(m, desc);
    /* 落库用已装入的 slot(persist_camera 会用 ARP mac 覆盖并回写 s->d.mac,须传可变 slot) */
    if (rc >= 0) {
        persist_camera(m, &s->d);                       /* 有 ip 即写库；mac 可后补 */
        sync_nop_registry(m, &s->d);
        /* ★ 运行时加设备(LanAddDevice/setLanDevice)必须**激活**该通道,否则 set_url 因 active=0
         * 只存 URL 不起 puller → 永不出图(开机 start_all 覆盖不到运行时新加的)。 */
        nvr_stream_start(m->cfg.sm, s->d.chn);
        s->first_add = 1;                     /* PoE/LAN Add/setLanDevice：试一轮，失败等密码 */
        chan_notify(m, s->d.chn);   /* 已绑物理设备 → GUI 立刻看到 status 3 */
    }
    CM_UNLOCK(m);
    return rc;
}

static int others_same_dev(nvr_chan_mgr_t *m, int chn, const char *ip, int port)
{
    int i, n = 0, p = port > 0 ? port : 80;
    if (!ip || !ip[0]) return 0;
    for (i = 0; i < NVR_MAX_CH; i++) {
        if (i == chn || !m->slots[i].in_use) continue;
        if (strcmp(m->slots[i].d.onvif_ip, ip) == 0 &&
            (m->slots[i].d.onvif_port > 0 ? m->slots[i].d.onvif_port : 80) == p)
            n++;
    }
    return n;
}

int nvr_chan_remove(nvr_chan_mgr_t *m, int chn)
{
    if (!m) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
    char ip[64]; int port = s->d.onvif_port;
    snprintf(ip, sizeof(ip), "%s", s->d.onvif_ip);
    int last = (others_same_dev(m, chn, ip, port) == 0);
    NVR_LOGI("chan", "ch%d 删除:停流+清库+通知 GUI%s", chn, last && ip[0] ? " (末路,断 ONVIF)" : "");
    nvr_stream_stop(m->cfg.sm, chn);                    /* 主+子 puller / writer / 解码 */
    unsync_nop_registry(m, chn);                        /* mapping 会话/registry 摘掉 */
    forget_camera(m, chn);                              /* camera 行清空 + 附属表按 chn 删 */
    memset(s, 0, sizeof(*s));                           /* slot 空 → status_code=0 */
    chan_notify(m, chn);                                /* longPolling ChannelStatusNotify */
    if (m->cfg.on_offline) m->cfg.on_offline(m->cfg.user, chn); /* persist=0;GUI 重拉出空窗 */
    CM_UNLOCK(m);
    if (last && ip[0]) nvr_onvif_disconnect(ip, port);
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
    if (!m) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
    nvr_dev_class_t c;
    if (nvr_dev_classify(scopes, &c) != 0) { CM_UNLOCK(m); return -1; }
    s->d.kind    = (int)c.kind;
    s->d.backend = (int)c.backend;
    if (c.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", c.mac);
    if (c.serial[0]) snprintf(s->d.serial, sizeof(s->d.serial), "%s", c.serial);
    if (c.model[0]) snprintf(s->d.model, sizeof(s->d.model), "%s", c.model);
    /* 仅 discovery 含 nopState/inactive → 待激活(码7)；默认已激活。 */
    s->sub.inactive = c.active ? 0 : 1;
    NVR_LOGI("chan", "ch%d 分类=%s backend=%s%s%s", chn, nvr_dev_kind_name(c.kind),
             c.backend == NVR_BACKEND_NOP ? "NOP透传" : "ONVIF翻译",
             c.mac[0] ? " mac=" : "", c.mac);
    persist_camera(m, &s->d);       /* 分类/mac 更新后回写(有 ip+mac 才写) */
    sync_nop_registry(m, &s->d);
    chan_notify(m, chn);
    CM_UNLOCK(m);
    return 0;
}

/* ---- PoE/LAN 自动发现绑定 ---- */
/* recall_only=1:掉线召回模式——只按 host/mac 更新已知通道的 IP,不新增外部设备
 * (LAN 策略:eth0 上不自动把别人的相机绑进来)。 */
/* poe_seg>0:本次发现是从某 PoE 段(198.18.<poe_seg>.100)定向广播发出的 → 命中相机必属该口,
 * 按物理口/段绑定(即使相机只报 IPv6 也不误当 LAN 走 MAC 找回)。0=LAN/非定向。 */
typedef struct { nvr_chan_mgr_t *m; int bound; int recall_only; int poe_seg; } disc_ctx_t;

static int chn_by_host(nvr_chan_mgr_t *m, const char *host)
{
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use && strcmp(m->slots[i].d.onvif_ip, host) == 0) return i;
    return -1;
}
static int chn_by_mac(nvr_chan_mgr_t *m, const char *mac)   /* mac 找回(IP 变更) */
{
    char want[24], have[24];
    if (mac_norm(mac, want, sizeof(want)) != 0) return -1;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use && mac_norm(m->slots[i].d.mac, have, sizeof(have)) == 0 &&
            strcmp(have, want) == 0) return i;
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

    /* ★ 只接受合法 IPv4 的发现结果。fe80:: 等 IPv6 链路本地/非法地址:无从判 PoE 口(WS-Discovery
     * 回包会跨段)、不可路由,且历史上被误写进 onvif_ip 毒化"按段发现/清口"逻辑(PoE ch1 卡死+乱码 IP
     * 根因)→ 一律忽略。相机取到段内 DHCP IPv4(见 dhcpd_vlan.conf)后自然被正确发现绑定。 */
    if (!is_ipv4(cam->host)) {
        NVR_LOGW("chan", "发现非IPv4地址 host=%s → 忽略(防脏数据/误绑)", cam->host);
        return;
    }

    nvr_dev_class_t cls; nvr_dev_classify(cam->scopes, &cls);

    /* ★ MAC 权威化:按发现到的 IP 从 ARP 表取物理 mac,统一覆盖 scope mac —— 使"按 mac 找回"
     * (chn_by_mac 比对 slot 里已落库的 ARP mac)与落库一致,避免 scope/ARP 不一致导致找回失败。 */
    { char amac[24], nm[24];
      if (arp_mac_of_ip(cam->host, amac, sizeof(amac)) == 0 && amac[0])
          snprintf(cls.mac, sizeof(cls.mac), "%s", amac);
      else if (cls.mac[0] && mac_norm(cls.mac, nm, sizeof(nm)) == 0)
          snprintf(cls.mac, sizeof(cls.mac), "%s", nm); }

    /* ★ 关键:PoE 相机所属口由"相机自身 IPv4 段"(ip_seg3(cam->host))决定,不能用探测源段——
     * WS-Discovery 多播回包会跨段(段10 探测能收到段15 相机的 ProbeMatch)。故按段绑口的逻辑一律
     * 走下方 ip_seg3(cam->host);只报 fe80、无段内 IPv4 的相机则无从判口 → W3/新增守卫直接拒绝,
     * 不误绑、不写脏地址(相机取到 DHCP IPv4 后自然被正确发现)。 */

    /* 已绑定同 IP → 更新分类 + 重算待激活标志（周期性重发现是清零 inactive 的唯一路径） */
    int chn = chn_by_host(m, cam->host);
    if (chn >= 0) {
        slot_t *s = &m->slots[chn];
        s->d.kind    = (int)cls.kind;
        s->d.backend = (int)cls.backend;
        if (cls.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", cls.mac);
        if (cls.serial[0]) snprintf(s->d.serial, sizeof(s->d.serial), "%s", cls.serial);
        if (cam->service_url[0]) snprintf(s->d.service_url, sizeof(s->d.service_url), "%s", cam->service_url);
        if (cam->port > 0) s->d.onvif_port = cam->port;
        s->sub.inactive = cls.active ? 0 : 1;
        persist_camera(m, &s->d);
        sync_nop_registry(m, &s->d);
        chan_notify(m, chn);
        return;
    }

    /* MAC 找回:同 mac 但 IP 变了(设备换了 IP)→ 更新该通道 IP,清 url 重解析,不新增通道。
     * ★ 仅限 LAN(eth0):PoE 相机按物理口/网段绑定,换口即换通道,**不做 MAC 找回**——
     *   否则把相机从 A 口拔到 B 口时,会被 MAC 钉死在旧口通道(见 PoE 即插即用:段2→段4 却留旧口)。
     *   PoE 网段(198.18.x.x)的发现直落下方"按网段绑口"逻辑。 */
    int is_poe_net = 0;
    { int oa, ob, oc, od;
      if (sscanf(cam->host, "%d.%d.%d.%d", &oa, &ob, &oc, &od) == 4 &&
          oa == NVR_POE_NET_A && ob == NVR_POE_NET_B)
          is_poe_net = 1; }
    int mchn = is_poe_net ? -1 : chn_by_mac(m, cls.mac);   /* host 已保证 IPv4(函数入口守卫) */
    if (mchn >= 0) {
        slot_t *s = &m->slots[mchn];
        NVR_LOGI("chan", "mac=%s IP 变更 %s→%s,ch%d 找回", cls.mac, s->d.onvif_ip, cam->host, mchn);
        snprintf(s->d.onvif_ip, sizeof(s->d.onvif_ip), "%s", cam->host);
        s->d.kind = (int)cls.kind; s->d.backend = (int)cls.backend;
        if (cls.serial[0]) snprintf(s->d.serial, sizeof(s->d.serial), "%s", cls.serial);
        if (cam->service_url[0]) snprintf(s->d.service_url, sizeof(s->d.service_url), "%s", cam->service_url);
        if (cam->port > 0) s->d.onvif_port = cam->port;
        s->d.url[0] = 0; s->url_tries = 0; s->url_next = 0;
        s->auth_ready = 0;
        s->status = NVR_CHAN_BOUND; s->notified_online = 0;
        persist_camera(m, &s->d);
        sync_nop_registry(m, &s->d);
        chan_notify(m, mchn);
        return;
    }

    /* ★ PoE 网段发现:相机在 198.18.<seg>.x(seg=第3段=口号,口 P→VLAN 2001+P→段 P)。
     * 按 seg 匹配到**同网段的预建 PoE 口通道**(其 onvif_ip 也是 198.18.<seg>.x),更新为相机真实 IP
     * 并置在场+重解析。用 seg 匹配而非 octet-1,兼容任意配置映射。 */
    {
        int seg = ip_seg3(cam->host);
        /* ★ 只有真正 PoE 段(198.18.x, eth1)的相机才按段绑 PoE 口。否则 LAN 相机(如 192.168.**9**.115)
         * 第3段恰好=口号(9)会误配到 PoE 口9 的占位通道(198.18.**9**.100 第3段也=9)→ LAN 设备落 PoE 板!
         * 必须校验前两段=198.18,不能只看第3段。 */
        int cam_is_poe = 0;
        { int pa, pb, pc, pd;
          if (sscanf(cam->host, "%d.%d.%d.%d", &pa, &pb, &pc, &pd) == 4 &&
              pa == NVR_POE_NET_A && pb == NVR_POE_NET_B) cam_is_poe = 1; }
        if (seg >= 0 && cam_is_poe) {
            for (int i = 0; i < NVR_POE_PORTS && i < NVR_MAX_CH; i++) {
                slot_t *s = &m->slots[i];
                if (!s->in_use || s->d.poe_port <= 0) continue;
                if (ip_seg3(s->d.onvif_ip) != seg) continue;    /* 同网段的口通道 */
                int first_seen = !s->poe_present;
                s->poe_present = 1;
                s->poe_seen = 1; s->poe_miss = 0;   /* 本轮命中 → 复位移除计数 */
                if (first_seen) {
                    s->first_add = 1;                     /* 本会话首次插上：试一轮 */
                    chan_notify(m, s->d.chn);   /* 插上物理机 → status 0→3 */
                }
                /* ★ 同口换机检测:发现的 MAC 与库里已落 MAC 不同 = 这口换了一台相机。
                 * 新机常拿到同一段内 IP(如 198.18.<口>.1),仅凭"IP变/无URL"判不出换机 → 会保留旧
                 * service_url/凭据/kind/URL,新机套旧配置连 ONVIF 失败**永不出图**(真机根因:clear_poe_port
                 * 因旧机 ARP 仍可达从不触发,DB 不清)。检测到换机 → 清旧配置,按新机从零重激活/重解析。 */
                int mac_swap = cls.mac[0] && s->d.mac[0] && strcasecmp(s->d.mac, cls.mac) != 0;
                if (strcmp(s->d.onvif_ip, cam->host) != 0 || !s->url_sub[0] || !s->url_main[0] || mac_swap) {
                    if (mac_swap) {
                        NVR_LOGI("chan", "PoE 口%d 换机 MAC %s→%s → 清旧配置(凭据/service_url/能力)重配",
                                 s->d.poe_port, s->d.mac, cls.mac);
                        s->d.user[0] = 0; s->d.pass[0] = 0; s->d.enh_random[0] = 0;  /* 旧凭据/激活态作废 */
                        s->d.service_url[0] = 0; s->caps_probed = 0;
                        s->first_add = 1;                        /* 视作首次插上,重走激活/鉴权一轮 */
                        memset(&s->sub, 0, sizeof(s->sub));
                        if (m->cfg.settings) nvr_settings_caps_set(m->cfg.settings, s->d.chn, "", "");
                    }
                    snprintf(s->d.onvif_ip, sizeof(s->d.onvif_ip), "%s", cam->host);
                    s->d.kind = (int)cls.kind; s->d.backend = (int)cls.backend;
                    if (cls.mac[0]) snprintf(s->d.mac, sizeof(s->d.mac), "%s", cls.mac);
                    s->d.onvif_port = cam->port > 0 ? cam->port : 80;
                    if (cam->service_url[0])
                        snprintf(s->d.service_url, sizeof(s->d.service_url), "%s", cam->service_url);
                    if (cls.serial[0]) snprintf(s->d.serial, sizeof(s->d.serial), "%s", cls.serial);
                    s->d.url[0] = 0; s->url_main[0] = 0; s->url_sub[0] = 0;
                    s->auth_ready = 0;
                    s->status = NVR_CHAN_BOUND; s->notified_online = 0;
                    /* 已试过一轮在等密码：只更新设备信息，不因再次发现而重打鉴权。
                     * ★ 但换机(mac_swap)必须强制重解析,否则旧 url_tries 会门控掉新机的解析。 */
                    if (mac_swap || !(s->first_add && s->url_tries >= 1)) {
                        s->url_tries = 0; s->url_next = 0; s->sub.auth_fail = 0;
                    }
                    persist_camera(m, &s->d);
                    sync_nop_registry(m, &s->d);
                    chan_notify(m, s->d.chn);   /* 物理机已在 → status 3 */
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
    snprintf(d.serial, sizeof(d.serial), "%s", cls.serial);
    snprintf(d.service_url, sizeof(d.service_url), "%s", cam->service_url);
    snprintf(d.user, sizeof(d.user), "admin");    /* 默认；nopOnvif 激活后用 P_act */
    snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1);

    NVR_LOGI("chan", "发现相机 %s (%s) → 绑定 ch%d", cam->host, nvr_dev_kind_name(cls.kind), chn);
    if (nvr_chan_add(m, &d) >= 0) {
        ctx->bound++;
        /* discovery 含 nopState/inactive → 待激活(码7)；默认已激活 */
        nvr_chan_substate_t sub; memset(&sub, 0, sizeof(sub));
        sub.inactive = cls.active ? 0 : 1;
        nvr_chan_set_substate(m, chn, &sub);
    }
}

static void on_34569(const nvr_lan34569_dev_t *d, void *user)
{
    nvr_onvif_cam_t cam;
    nvr_lan34569_fill_cam(d, &cam);
    on_discovered(&cam, user);
}

int nvr_chan_run_discovery(nvr_chan_mgr_t *m, const char *local_ip, int seconds)
{
    if (!m) return -1;
    disc_ctx_t ctx = { m, 0 };
    /* 持锁整个发现(~2s):on_discovered 回调直接改 slots,须与派发线程/tick 互斥;
     * 其内调用的 nvr_chan_add 递归重入本锁。发现是显式扫描动作,偶发 2s 串行可接受。 */
    CM_LOCK(m);
    nvr_onvif_discover(local_ip, seconds > 0 ? seconds : 2, on_discovered, &ctx);  /* 拉入强 onvif 符号 */
    nvr_lan34569_discover(local_ip, 1, on_34569, &ctx);
    CM_UNLOCK(m);
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
    CM_LOCK(m);
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use) m->slots[i].status = NVR_CHAN_BOUND;
    CM_UNLOCK(m);
}

/* 解析某码流(SUB/MAIN)取流 URL 并经 nvr_stream_set_url 提供给 streaming(该路 puller 起)。
 * 首次(子)顺带按 scopes 分类(kind/backend/mac/型号)。返 0 成功。 */
/* 调用时持 m->lock(由 tick 顶层取得,递归深度 1)。ONVIF 广播/取流(~2s)阻塞,故内部
 * 先快照拨号输入 → 解锁 → 锁外 get_url → 重锁 → 校验 slot 未被增删/换设备后回写。 */
static int resolve_stream_url(nvr_chan_mgr_t *m, slot_t *s, int stream)
{
    if (!s->d.onvif_auto || !s->d.onvif_ip[0]) return -1;
    int  chn  = s->d.chn;
    int  port = s->d.onvif_port;
    int  need_auth = !s->auth_ready;
    nvr_channel_t snap = s->d;
    nvr_chan_substate_t sub = s->sub;
    char ip[64], user[64], pass[64], vst[128], svc[128];
    snprintf(ip,   sizeof(ip),   "%s", s->d.onvif_ip);
    snprintf(user, sizeof(user), "%s", s->d.user);
    snprintf(pass, sizeof(pass), "%s", s->d.pass);
    snprintf(vst,  sizeof(vst),  "%s", s->d.video_source_token);
    snprintf(svc,  sizeof(svc),  "%s", s->d.service_url);
    const char *st = (stream == NVR_STREAM_SUB) ? "sub" : "main";
    char url[256], scopes[1024]; scopes[0] = 0;  /* 1024:含 nopState 等尾部 scope,512 会截断 */
    char amac[24], nmac[24];
    amac[0] = 0; nmac[0] = 0;

    CM_UNLOCK(m);
    int grc = -1;
    int auth_seen = 0;
    if (need_auth) {
        char scopes_disc[1024];
        char svc_disc[128];
        int dport = port;
        scopes_disc[0] = 0; svc_disc[0] = 0;
        /* 先 Discovery：nopOnvif 以报文/GetScopes 的 nopOnvif 标识为准，不能凭 SN 猜测。
         * ★ 优先查发现缓存(开机扫一次的结果),命中即免 2s probe(probe 全局串行,是并发瓶颈);
         * 未命中才回落单路 probe。 */
        int drc = nvr_onvif_cached_scopes(ip, scopes_disc, sizeof(scopes_disc),
                                          svc_disc, sizeof(svc_disc), &dport);
        if (drc != 0)
            drc = nvr_onvif_probe_scopes(ip, scopes_disc, sizeof(scopes_disc),
                                         svc_disc, sizeof(svc_disc), &dport);
        if (drc == 0 && scopes_disc[0]) {
            nvr_chan_apply_discovery(m, chn, scopes_disc);
            if (nvr_chan_get(m, chn, &snap) == 0) {
                if (svc_disc[0] && !snap.service_url[0])
                    snprintf(snap.service_url, sizeof(snap.service_url), "%s", svc_disc);
                if (dport > 0) { snap.onvif_port = dport; port = dport; }
            }
        }
        if (snap.kind == NVR_DEV_KIND_NOPONVIF) {
            char pact[24];
            int ar = nvr_chan_try_activate(&snap, &sub, pact, sizeof(pact));
            if (ar == 0) {
                nvr_chan_set_auth(m, chn, "admin", pact);
                nvr_chan_get(m, chn, &snap);
            }
        }

        nvr_auth_cred_t creds[NVR_AUTH_CRED_MAX];
        int nc = nvr_chan_auth_candidates(&snap, m->cfg.settings, &sub, creds, NVR_AUTH_CRED_MAX);
        int i;
        for (i = 0; i < nc; i++) {
            snprintf(user, sizeof(user), "%s", creds[i].user);
            snprintf(pass, sizeof(pass), "%s", creds[i].pass);
            nvr_onvif_disconnect(ip, port);   /* 换密重建，避免沿用失败会话 */
            nvr_onvif_connect(ip, port, svc[0] ? svc : (snap.service_url[0] ? snap.service_url : NULL),
                              user, pass);
            scopes[0] = 0;
            grc = nvr_onvif_get_url(ip, port, user, pass, st, url, sizeof(url),
                                    scopes, sizeof(scopes), vst);
            if (grc == 0) break;
            if (grc == NVR_ONVIF_AUTH || nvr_onvif_auth_failed(ip, port))
                auth_seen = 1;
        }
    } else {
        nvr_onvif_connect(ip, port, svc[0] ? svc : NULL, user, pass);
        grc = nvr_onvif_get_url(ip, port, user, pass, st, url, sizeof(url),
                                scopes, sizeof(scopes), vst);
    }
    if (grc != 0 && snap.kind == NVR_DEV_KIND_NOP) {
        int rec = nvr_chan_enh_on_auth_fail(&snap);
        if (rec >= 0) {
            snprintf(user, sizeof(user), "%s", snap.user);
            snprintf(pass, sizeof(pass), "%s", snap.pass);
            nvr_onvif_disconnect(ip, port);
            nvr_onvif_connect(ip, port, svc[0] ? svc : (snap.service_url[0] ? snap.service_url : NULL),
                              user, pass);
            scopes[0] = 0;
            grc = nvr_onvif_get_url(ip, port, user, pass, st, url, sizeof(url),
                                    scopes, sizeof(scopes), vst);
        }
    }
    if (grc != 0 && (grc == NVR_ONVIF_AUTH || nvr_onvif_auth_failed(ip, port)))
        auth_seen = 1;
    /* discovery 无 mac：连上后再取。ARP 权威；没有再 GetNetworkInterfaces。 */
    if (grc == 0) {
        arp_mac_of_ip(ip, amac, sizeof(amac));
        if (!amac[0])
            nvr_onvif_get_mac(ip, port, user, pass, nmac, sizeof(nmac));
    }
    CM_LOCK(m);
    if (grc != 0) {
        /* 401/402/501 自愈后的 random/P_enh 必须回写，下次开机才是普通或增强模式。 */
        if (s->in_use && s->d.chn == chn && snap.kind == NVR_DEV_KIND_NOP) {
            int chg = strcmp(s->d.enh_random, snap.enh_random) != 0 ||
                      strcmp(s->d.pass, snap.pass) != 0;
            snprintf(s->d.user, sizeof(s->d.user), "%s", snap.user);
            snprintf(s->d.pass, sizeof(s->d.pass), "%s", snap.pass);
            snprintf(s->d.enh_random, sizeof(s->d.enh_random), "%s", snap.enh_random);
            if (chg) persist_camera(m, &s->d);
        }
        /* 试完凭据仍失败且为鉴权错误 → status 4；网络/超时等保持 3。未激活 nopOnvif 仍报 7。 */
        if (need_auth && auth_seen && !sub.inactive &&
            s->in_use && s->d.chn == chn && !s->sub.auth_fail) {
            s->sub.auth_fail = 1;
            chan_notify(m, chn);
        }
        return auth_seen ? NVR_ONVIF_AUTH : NVR_ONVIF_ERR;
    }
    if (!s->in_use || s->d.chn != chn) return -1;   /* 解析期间 slot 被增删/换设备 → 丢弃结果 */
    snprintf(s->d.user, sizeof(s->d.user), "%s", user);
    snprintf(s->d.pass, sizeof(s->d.pass), "%s", pass);
    snprintf(s->d.enh_random, sizeof(s->d.enh_random), "%s", snap.enh_random);
    if (need_auth) s->sub = sub;
    s->sub.auth_fail = 0;
    s->auth_ready = 1;
    sync_nop_registry(m, &s->d);

    if (stream == NVR_STREAM_SUB) {
        if (scopes[0]) {
            nvr_dev_class_t c;
            if (nvr_dev_classify(scopes, &c) == 0) {
                s->d.kind = (int)c.kind; s->d.backend = (int)c.backend;
                if (c.mac[0] && !s->d.mac[0]) {   /* 已有 ARP/GetNetwork 不让 scope 覆盖 */
                    char nm[24];
                    if (mac_norm(c.mac, nm, sizeof(nm)) == 0)
                        snprintf(s->d.mac, sizeof(s->d.mac), "%s", nm);
                }
                if (c.model[0]) snprintf(s->d.model, sizeof(s->d.model), "%s", c.model);
                s->sub.inactive = c.active ? 0 : 1;
                sync_nop_registry(m, &s->d);
            }
        }
        snprintf(s->url_sub, sizeof(s->url_sub), "%s", url);
        snprintf(s->d.url_sub, sizeof(s->d.url_sub), "%s", url);   /* 持久化子流地址(同设备直连) */
        if (!s->d.url[0]) snprintf(s->d.url, sizeof(s->d.url), "%s", url);   /* 已解析标记(兼容) */
    } else {
        snprintf(s->url_main, sizeof(s->url_main), "%s", url);
        snprintf(s->d.url_main, sizeof(s->d.url_main), "%s", url); /* 持久化主流地址(同设备直连) */
    }
    if (adopt_mac(&s->d, amac, nmac)) {
        NVR_LOGI("chan", "ch%d mac=%s (%s) 入库供找回", s->d.chn, s->d.mac,
                 amac[0] ? "ARP" : "GetNetworkInterfaces");
        sync_nop_registry(m, &s->d);
    }
    nvr_stream_set_url(m->cfg.sm, s->d.chn, stream, url);
    return 0;
}

/* ================= 并发取流 URL 解析 =================
 * 原来 tick 单线程串行解析:每路 ONVIF get_url ~2s,16 路 PoE 串行→十几分钟才全出图。
 * 改为**每路一个 worker 线程并发解析**:resolve_stream_url 内部"快照→解锁→网络→重锁→重校验→应用"
 * 本就为解锁期并发设计;ONVIF 设备池按 ip:port per-device 锁,不同相机并发 connect 不互串。
 * worker 里 CM_LOCK→resolve_stream_url(解锁期网络与其它 worker 并行)→处理结果→CM_UNLOCK。
 * g_resolve_active 封顶并发数,url_resolving 防同通道重复派。 */
#define NVR_RESOLVE_MAX_CONCURRENT 10
static volatile int g_resolve_active = 0;
struct resolve_job { nvr_chan_mgr_t *m; int chn; int stream; };

static void *resolve_worker(void *arg)
{
    struct resolve_job *j = (struct resolve_job *)arg;
    nvr_chan_mgr_t *m = j->m; int chn = j->chn, stream = j->stream;
    free(j);
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (s && s->in_use && s->d.chn == chn) {
        int rr = resolve_stream_url(m, s, stream);   /* 内部解锁做网络 → 与其它 worker 并行 */
        s = slot_of(m, chn);                          /* 网络窗口后重取(数组稳定,重校验在场) */
        if (s && s->in_use && s->d.chn == chn) {
            time_t now = time(NULL);
            if (rr == 0) {
                s->url_tries = 0; s->first_add = 0;
                NVR_LOGI("chan", "ch%d %s码流已解析取流", chn, stream == NVR_STREAM_SUB ? "子" : "主");
                if (stream == NVR_STREAM_SUB) persist_camera(m, &s->d);
            } else if (s->first_add) {
                s->url_next = now + 86400 * 365;
                if (rr == NVR_ONVIF_AUTH) {
                    if (s->d.kind != NVR_DEV_KIND_NOPONVIF && !s->d.enh_random[0]) s->d.pass[0] = 0;
                    persist_camera(m, &s->d);
                    NVR_LOGW("chan", "ch%d 首次取流失败(鉴权)，等待用户输入密码", chn);
                } else {
                    NVR_LOGW("chan", "ch%d 首次取流失败(非鉴权)，保持连接中", chn);
                }
                chan_notify(m, chn);
            } else {
                /* 软退避:前 3 次快速重试(5s)——刚上电相机 ONVIF/media 服务几十秒内起稳,快重试能秒级
                 * 抓住,不必等 30s;之后才拉长(30s×次)避免对真没相机的口空转。 */
                int back = (s->url_tries <= 3) ? 5 : 30 * s->url_tries;
                s->url_next = now + back;
                if (s->url_tries >= NVR_URL_MAX_TRIES) {
                    chan_notify(m, chn);
                    NVR_LOGW("chan", "ch%d 取流URL解析失败达上限(%d) 停止；改凭据后重试", chn, NVR_URL_MAX_TRIES);
                } else {
                    NVR_LOGW("chan", "ch%d %s码流解析失败(%d/%d) 退避%ds",
                             chn, stream == NVR_STREAM_SUB ? "子" : "主", s->url_tries, NVR_URL_MAX_TRIES, back);
                }
            }
            s->url_resolving = 0;
        }
    }
    CM_UNLOCK(m);
    __sync_fetch_and_sub(&g_resolve_active, 1);
    return NULL;
}

/* 在 CM_LOCK 内调用:满足解析条件则派一个 worker 并发解析该通道(不阻塞 tick)。派成功返回 1。 */
static int spawn_resolve(nvr_chan_mgr_t *m, slot_t *s, int stream)
{
    if (g_resolve_active >= NVR_RESOLVE_MAX_CONCURRENT) return 0;
    struct resolve_job *j = (struct resolve_job *)malloc(sizeof(*j));
    if (!j) return 0;
    j->m = m; j->chn = s->d.chn; j->stream = stream;
    s->url_tries++;
    s->url_resolving = 1;
    __sync_fetch_and_add(&g_resolve_active, 1);
    pthread_t th; pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 * 1024);
    int rc = pthread_create(&th, &at, resolve_worker, j);
    pthread_attr_destroy(&at);
    if (rc != 0) {   /* 起线程失败:回滚 */
        free(j); s->url_tries--; s->url_resolving = 0;
        __sync_fetch_and_sub(&g_resolve_active, 1);
        return 0;
    }
    return 1;
}

/* 映射 streaming 状态 → app 状态，并驱动上线/掉线通知与重连。
 * *resolve_budget：本轮 tick 还允许派几路 ONVIF URL 解析 worker(并发,不阻塞主循环)。 */
static void tick_slot(nvr_chan_mgr_t *m, slot_t *s, time_t now, int *resolve_budget)
{
    if (s->status == NVR_CHAN_DISABLED) return;

    /* ★ PoE 即插即用门控:
     *   · onvif_ip 仍是占位(198.18.<seg>.100 = NVR 自身,4th=100)且未发现 → 等 ONVIF 发现(不解析);
     *   · 一旦发现到真实相机 IP(4th!=100,如 .1)→ 视为已知在场,像 LAN 一样**直接 get_url 重连**,
     *     不再依赖持续的发现应答(相机 ONVIF/RTSP 掉线后靠退避重连恢复,而非等再次广播命中)。
     *   · 首次添加试一轮失败后停等用户密码，不再因再次发现而重试。 */
    if ((!s->url_sub[0] || !s->url_main[0]) && s->d.onvif_auto && s->d.poe_port > 0) {
        int last_oct = -1; { int a,b,c,d4; if (sscanf(s->d.onvif_ip,"%d.%d.%d.%d",&a,&b,&c,&d4)==4) last_oct=d4; }
        int known_cam = (s->d.onvif_ip[0] && last_oct != 100 && last_oct >= 0);  /* 非 .100 占位 = 已知相机 IP */
        if (!s->poe_present && !known_cam)
            return;                                       /* 仍占位且未发现在场:本口不解析 */
    }

    /* URL 尚未解析：★ 派**并发 worker** 经 ONVIF 取流(不阻塞 tick,多路同时解析)。首次添加只试
     * 一轮,失败停等用户密码;已落库重连限次+退避。双流先子后主。同通道已有 worker 在跑(url_resolving)
     * 则跳过。结果处理(成功/退避/first_add)在 resolve_worker 里做。 */
    int max_tries = s->first_add ? 1 : NVR_URL_MAX_TRIES;
    if (s->d.onvif_auto && s->d.onvif_ip[0] && (!s->url_sub[0] || !s->url_main[0])
        && s->url_tries < max_tries && now >= s->url_next && !s->url_resolving
        && resolve_budget && *resolve_budget > 0) {
        int stream = !s->url_sub[0] ? NVR_STREAM_SUB : NVR_STREAM_MAIN;   /* 先子后主 */
        if (spawn_resolve(m, s, stream))
            (*resolve_budget)--;
    }

    nvr_ch_state_t st = nvr_stream_state(m->cfg.sm, s->d.chn);
    /* 解码预算准入：只录不显时 mhal 会拒绝该路解码，供状态码 5(超出解码能力)。 */
    {
        int oor = nvr_stream_decode_denied(m->cfg.sm, s->d.chn) ? 1 : 0;
        if (s->sub.out_of_res != oor) {
            s->sub.out_of_res = oor;
            chan_notify(m, s->d.chn);   /* 5 ↔ 非5 */
        }
    }
    switch (st) {
        case NVR_CH_PLAYING:
            s->status = NVR_CHAN_ONLINE;
            s->backoff_s = m->cfg.reconnect_base_s;
            /* ★ 出图=设备已激活:清 inactive(否则 getChannelStatus 恒返 7 让 GUI 报错,尽管已出图)。
             * 若之前误报 inactive,清零后强制 longPolling 推一次让 GUI 重拉更新为在线。 */
            if (s->sub.inactive) {
                s->sub.inactive = 0;
                chan_notify(m, s->d.chn);
                NVR_LOGI("chan", "ch%d 出图→清 inactive(status 7→1),通知 GUI 刷新", s->d.chn);
            }
            if (s->sub.auth_fail) {
                s->sub.auth_fail = 0;
                chan_notify(m, s->d.chn);
            }
            if (!s->notified_online) {
                s->notified_online = 1;
                chan_notify(m, s->d.chn);   /* 通知 gui 该通道状态变(→CNN),重拉状态出图 */
                NVR_LOGI("chan", "ch%d ONLINE 出图", s->d.chn);
                /* on_online **异步入队**(见 nvr_app on_chan_online):不在 CM_LOCK 内直接进 preview(取
                 * p->lock)→ 破 CM_LOCK↔p->lock AB-BA 死锁。授时(阻塞 HTTP)也已移到该 worker 锁外做。 */
                if (m->cfg.on_online) m->cfg.on_online(m->cfg.user, s->d.chn);
            }
            /* discovery 无 mac：连上后 ARP 表才有项。ARP 一旦出现即以它覆盖，供 IP 变更找回。 */
            {
                char amac[24];
                if (arp_mac_of_ip(s->d.onvif_ip, amac, sizeof(amac)) == 0 &&
                    strcasecmp(s->d.mac, amac) != 0) {
                    snprintf(s->d.mac, sizeof(s->d.mac), "%s", amac);
                    persist_camera(m, &s->d);
                    NVR_LOGI("chan", "ch%d ARP mac=%s 入库(找回用)", s->d.chn, amac);
                }
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
                chan_notify(m, s->d.chn);   /* 掉线也通知 gui 刷新(→显示 NO SIGNAL) */
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
    /* 能力集收集入库(上线/添加各收集一次,caps_probed 门控;掉线清零→重连再收集,
     * 覆盖改凭据/mac 找回)。NVR 聚合器只读 DB(nvr_cmd_display.c),不 live。
     *   · NOP  → 直接向设备 getDeviceCapabilities(HTTP)取 content.channels[0] 入库;
     *   · 其余(ONVIF)→ 经 nop 进程内映射(getDeviceCapabilities + AI,合并 _ai)入库;
     *     nopOnvif 再按 nightowl_protocol.md 对发现口 GET 白灯/警笛,200 则追加 capabilities[]。 */
    if (s->status == NVR_CHAN_ONLINE && !s->caps_probed && m->cfg.settings
        && s->d.onvif_ip[0] && resolve_budget && *resolve_budget > 0) {
        (*resolve_budget)--;
        int stored = 0;
        /* 在线期间身份稳定:快照 chn/ip/backend 后解锁,锁外做多次 HTTP/映射探测(每次可数秒)
         * + 入库,再重锁封口 —— 全程不持 cm 锁,避免拖住 getChannelStatus 等派发读。 */
        int  chn = s->d.chn;
        int  backend = s->d.backend;
        int  kind = s->d.kind;
        int  dev_chn = s->d.dev_chn > 0 ? s->d.dev_chn : 1;
        int  dport = s->d.onvif_port > 0 ? s->d.onvif_port : 80;
        char ip[64], user[64], pass[64];
        snprintf(ip, sizeof(ip), "%s", s->d.onvif_ip);
        snprintf(user, sizeof(user), "%s", s->d.user);
        snprintf(pass, sizeof(pass), "%s", s->d.pass);
        char devmodel[64] = "", devserial[64] = "", devfw[64] = "";   /* getDeviceInfo 型号/序列号/固件 */
        CM_UNLOCK(m);

        if (backend == 0) {
            /* NOP 设备:直接向设备 getDeviceCapabilities,取回真实能力入库。 */
            char *resp = chan_nop_post(ip, dport, user, pass,
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
                        if (nvr_settings_caps_set(m->cfg.settings, chn, cj, sig ? sig : "POE") == 0) {  /* 0-based:与 camera 表/级联删除一致 */
                            stored = 1;
                            NVR_LOGI("chan", "ch%d NOP 能力入库(signal=%s)", chn, sig ? sig : "POE");
                        } else NVR_LOGW("chan", "ch%d NOP 能力入库失败(DB busy?),下次重试", chn);
                        free(cj);
                    }
                } else {
                    NVR_LOGW("chan", "ch%d NOP 应答无 content.channels[0]: %s", chn, resp);
                }
                if (root) cJSON_Delete(root);
                free(resp);
            } else NVR_LOGW("chan", "ch%d NOP getDeviceCapabilities 无应答,下次重试", chn);
            /* getDeviceInfo 取型号/序列号:getDeviceCapabilities 不含 model(见 nop_doc
             * AttachToMaster_InLan——model/serialNumber 在 getDeviceInfo 返回),PoE(NOP)相机
             * scopes 又无 /hardware/,故必须显式 getDeviceInfo 才能拿到型号入库供清单回显。 */
            {
                char *info = chan_nop_post(ip, dport, user, pass,
                                           "{\"func\":\"getDeviceInfo\",\"args\":{}}");
                if (info) {
                    cJSON *iroot = cJSON_Parse(info);
                    cJSON *ic = iroot ? cJSON_GetObjectItem(iroot, "content") : NULL;
                    if (ic) {
                        const char *mdl = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "model"));
                        const char *ser = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "serialNumber"));
                        const char *fwv = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "firmwareVersion"));
                        if (mdl && mdl[0]) snprintf(devmodel, sizeof(devmodel), "%s", mdl);
                        if (ser && ser[0]) snprintf(devserial, sizeof(devserial), "%s", ser);
                        if (fwv && fwv[0]) snprintf(devfw, sizeof(devfw), "%s", fwv);
                        NVR_LOGI("chan", "ch%d getDeviceInfo: model=%s serial=%s fw=%s", chn,
                                 devmodel[0] ? devmodel : "?", devserial[0] ? devserial : "?",
                                 devfw[0] ? devfw : "?");
                    } else NVR_LOGW("chan", "ch%d getDeviceInfo 无 content,下次重试", chn);
                    if (iroot) cJSON_Delete(iroot);
                    free(info);
                } else NVR_LOGW("chan", "ch%d getDeviceInfo 无应答,下次重试", chn);
            }
        } else if (m->cfg.nop) {
            /* ONVIF 设备:经 nop 进程内映射收集(数据源=map,NOPMappingONVIF.md)。
             * channel 用 0-based(映射注册表按 d->chn 建键)。 */
            char req[128];
            snprintf(req, sizeof(req),
                     "{\"func\":\"X_NightOwl_getDeviceCapabilities\",\"args\":{\"channel\":%d}}", chn);
            char *out = NULL;
            if (nop_app_dispatch(m->cfg.nop, req, &out) == 0 && out) {
                cJSON *root = cJSON_Parse(out);
                cJSON *content = root ? cJSON_GetObjectItem(root, "content") : NULL;
                cJSON *chs = content ? cJSON_GetObjectItem(content, "channels") : NULL;
                cJSON *ch0 = (chs && cJSON_IsArray(chs)) ? cJSON_GetArrayItem(chs, 0) : NULL;
                if (ch0) {
                    cJSON *e = cJSON_Duplicate(ch0, 1);
                    /* 合并 AI 明细 → _ai(objectDetection + ruledDetection),供 AI_getChannelAICapabilities。 */
                    char aireq[128];
                    snprintf(aireq, sizeof(aireq),
                             "{\"func\":\"AI_getChannelAICapabilities\",\"args\":{\"channel\":%d}}", chn);
                    char *aout = NULL;
                    if (nop_app_dispatch(m->cfg.nop, aireq, &aout) == 0 && aout) {
                        cJSON *aroot = cJSON_Parse(aout);
                        cJSON *acontent = aroot ? cJSON_GetObjectItem(aroot, "content") : NULL;
                        if (acontent) cJSON_AddItemToObject(e, "_ai", cJSON_Duplicate(acontent, 1));
                        if (aroot) cJSON_Delete(aroot);
                        nop_app_free_response(aout);
                    }
                    /* ★ 从已并入的 _ai(AI_getChannelAICapabilities 权威给出,Media1 也能拿)派生
                     * sensors[] 与 sensor/ai 能力 —— 避免 getDeviceCapabilities 里 tr2 分析失败致 sensors 空。 */
                    {
                        cJSON *aiobj = cJSON_GetObjectItem(e, "_ai");
                        cJSON *od = aiobj ? cJSON_GetObjectItem(aiobj, "objectDetection") : NULL;
                        cJSON *rd = aiobj ? cJSON_GetObjectItem(aiobj, "ruledDetection") : NULL;
                        cJSON *capsarr = cJSON_GetObjectItem(e, "capabilities");
                        if ((od || rd) && capsarr && !caps_arr_has(capsarr, "sensor"))
                            cJSON_AddItemToArray(capsarr, cJSON_CreateString("sensor"));
                        if ((od || rd) && capsarr && !caps_arr_has(capsarr, "ai"))
                            cJSON_AddItemToArray(capsarr, cJSON_CreateString("ai"));
                        cJSON *sensors = cJSON_GetObjectItem(e, "sensors");
                        if (od && sensors && cJSON_GetArraySize(sensors) == 0) {
                            cJSON *s1 = cJSON_CreateObject();
                            cJSON_AddStringToObject(s1, "sensor", "objectDetection");
                            cJSON *modes = cJSON_AddArrayToObject(s1, "modes");
                            for (cJSON *cls = od->child; cls; cls = cls->next)
                                if (cls->string) cJSON_AddItemToArray(modes, cJSON_CreateString(cls->string));
                            cJSON_AddItemToArray(sensors, s1);
                        }
                    }
                    /* nopOnvif 私有 NOP(nightowl_protocol.md):发现口 GET 开关,200=支持(含未激活
                     * device_not_active),501=不支持。打真实 HTTP,不走本机 mapping 桩。纯 ONVIF 无此口。 */
                    if (kind == NVR_DEV_KIND_NOPONVIF) {
                        static const struct { const char *func; const char *cap; } probes[] = {
                            { "X_NightOwl_getChannelLightSwitch",      "light" },
                            { "X_NightOwl_getChannelAudioAlertSwitch", "audioAlert" },
                        };
                        cJSON *capsarr = cJSON_GetObjectItem(e, "capabilities");
                        if (!capsarr) capsarr = cJSON_AddArrayToObject(e, "capabilities");
                        for (size_t pi = 0; capsarr && pi < sizeof(probes)/sizeof(probes[0]); pi++) {
                            if (caps_arr_has(capsarr, probes[pi].cap)) continue;
                            char preq[192];
                            snprintf(preq, sizeof(preq),
                                     "{\"func\":\"%s\",\"args\":{\"channel\":%d}}",
                                     probes[pi].func, dev_chn);
                            char *pout = chan_nop_post(ip, dport, user, pass, preq);
                            if (!pout) {
                                NVR_LOGW("chan", "ch%d nopOnvif 探测 %s 无应答", chn, probes[pi].func);
                                continue;
                            }
                            cJSON *proot = cJSON_Parse(pout);
                            int code = json_status_code(proot);
                            if (code == 200) {
                                cJSON_AddItemToArray(capsarr, cJSON_CreateString(probes[pi].cap));
                                NVR_LOGI("chan", "ch%d nopOnvif 探测 %s → %s",
                                         chn, probes[pi].func, probes[pi].cap);
                            } else {
                                NVR_LOGI("chan", "ch%d nopOnvif 探测 %s → %d(不追加)",
                                         chn, probes[pi].func, code);
                            }
                            if (proot) cJSON_Delete(proot);
                            free(pout);
                        }
                        caps_ensure_light_modes(e);
                    }
                    char *cj = cJSON_PrintUnformatted(e);
                    if (cj) {
                        if (nvr_settings_caps_set(m->cfg.settings, chn, cj, "IPC") == 0) {  /* 0-based:与 camera 表/级联删除一致 */
                            stored = 1;
                            NVR_LOGI("chan", "ch%d ONVIF 能力(map)入库: %s", chn, cj);
                        } else NVR_LOGW("chan", "ch%d ONVIF 能力入库失败(DB busy?),下次重试", chn);
                        free(cj);
                    }
                    cJSON_Delete(e);
                } else NVR_LOGW("chan", "ch%d ONVIF getDeviceCapabilities(map) 无 channels[0]", chn);
                if (root) cJSON_Delete(root);
                nop_app_free_response(out);
            } else NVR_LOGW("chan", "ch%d ONVIF getDeviceCapabilities(map) 无应答,下次重试", chn);
        }

        CM_LOCK(m);   /* 重锁:仅当 slot 仍是同一在场设备时才封口(解锁窗口内可能被增删/换机) */
        if ((devmodel[0] || devserial[0] || devfw[0]) && s->in_use && s->d.chn == chn) {
            int changed = 0;
            if (devmodel[0] && strcmp(s->d.model, devmodel) != 0) {
                snprintf(s->d.model, sizeof(s->d.model), "%s", devmodel); changed = 1;
            }
            if (devserial[0] && strcmp(s->d.serial, devserial) != 0) {
                snprintf(s->d.serial, sizeof(s->d.serial), "%s", devserial); changed = 1;
            }
            if (devfw[0] && strcmp(s->d.firmware, devfw) != 0) {
                snprintf(s->d.firmware, sizeof(s->d.firmware), "%s", devfw); changed = 1;
            }
            if (changed) { persist_camera(m, &s->d); sync_nop_registry(m, &s->d); }
        }
        if (stored && s->in_use && s->d.chn == chn)
            s->caps_probed = 1;   /* 成功才封口;失败保持 0 → 下 tick 重试 */
    }
}

int nvr_chan_set_stream(nvr_chan_mgr_t *m, int chn, int stream)
{
    if (!m) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use || s->status == NVR_CHAN_DISABLED) { CM_UNLOCK(m); return -1; }
    /* ★ 双流:主+子两路都在拉(tick 各自 set_url),切换只改**喂解码器的码流**(单宫格=主/多宫格=子)。
     * 瞬时、不重连、不重解析。set_decode_stream 内部换解码源即时出图。 */
    s->d.stream = stream;
    nvr_stream_set_decode_stream(m->cfg.sm, chn, stream);
    CM_UNLOCK(m);
    NVR_LOGI("chan", "ch%d 切%s码流(双流已拉,瞬时)", chn, stream == NVR_STREAM_SUB ? "子" : "主");
    return 0;
}

/* PoE 口相机移除:复位为空口占位(清 mac/serial/url/present/能力,IP 回 198.18.<口>.1,状态回
 * BOUND),停拉流并通知 GUI。PoE 口通道本身保留(不删 slot),仅清"设备"。见 [PoE 拔线即清]。 */
static void clear_poe_port(nvr_chan_mgr_t *m, slot_t *s)
{
    int chn = s->d.chn, port = s->d.poe_port;
    NVR_LOGI("chan", "PoE 口%d(ch%d)相机移除 → 清空口设备信息", port, chn);
    nvr_stream_stop(m->cfg.sm, chn);
    s->poe_present = 0; s->poe_seen = 0; s->poe_miss = 0;
    s->d.mac[0] = 0; s->d.serial[0] = 0; s->d.service_url[0] = 0;
    snprintf(s->d.onvif_ip, sizeof(s->d.onvif_ip), "%d.%d.%d.1",
             NVR_POE_NET_A, NVR_POE_NET_B, port);   /* 回占位 198.18.<口>.1 */
    s->d.url[0] = 0; s->url_main[0] = 0; s->url_sub[0] = 0;
    s->auth_ready = 0; s->url_tries = 0; s->url_next = 0; s->caps_probed = 0;
    s->status = NVR_CHAN_BOUND; s->notified_online = 0;
    memset(&s->sub, 0, sizeof(s->sub));
    if (m->cfg.settings) nvr_settings_caps_set(m->cfg.settings, chn, "", "");  /* 清能力集 */
    persist_camera(m, &s->d);
    sync_nop_registry(m, &s->d);
    if (m->cfg.on_offline) m->cfg.on_offline(m->cfg.user, chn);
    chan_notify(m, chn);
}

/* ---- 发现"扫一次"缓存刷新:后台线程,一次广播扫 LAN + 各 PoE 段,把全部相机 scopes 存缓存
 * (nvr_onvif_scan)。各通道解析查缓存免每路 2s probe(probe 全局串行是并发瓶颈)。开机 kick 一次,
 * 之后每 ~60s 刷新。g_scan_busy 防重叠。 */
static volatile int g_scan_busy = 0;
static void *disc_scan_thread(void *arg)
{
    (void)arg;
    nvr_onvif_scan(NULL, 3);                                  /* LAN(eth0/默认接口) */
    for (int p = 1; p <= NVR_POE_PORTS; p++) {
        char lip[32]; snprintf(lip, sizeof(lip), "198.18.%d.100", p);
        nvr_onvif_scan(lip, 1);                               /* PoE 段 p(eth1);段内相机少,1s 足够 */
    }
    g_scan_busy = 0;
    return NULL;
}
static void disc_scan_kick(void)
{
    if (g_scan_busy) return;
    g_scan_busy = 1;
    pthread_t th; pthread_attr_t at;
    pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, disc_scan_thread, NULL) != 0) g_scan_busy = 0;
    pthread_attr_destroy(&at);
}

/* ★ 锁外发现用的回调包装:on_discovered/on_34569 假定调用方持 CM_LOCK(直接改 slots);把 2s 广播
 * 挪出 CM_LOCK 后,改用这两个包装——每发现一台相机才**短暂**取 CM_LOCK,广播本身零占锁。
 * 这样开机期 PoE/LAN 发现广播不再把 CM_LOCK 占 2~6s,setDeviceDisplayMode/getDeviceDisplayMode
 * 取 CM_LOCK 立即拿到 → ≤2s 回复。CM_LOCK 递归,嵌套安全。 */
static void on_discovered_locked(const nvr_onvif_cam_t *cam, void *user)
{
    disc_ctx_t *c = (disc_ctx_t *)user;
    if (!c || !c->m) return;
    CM_LOCK(c->m);
    on_discovered(cam, user);
    CM_UNLOCK(c->m);
}
static void on_34569_locked(const nvr_lan34569_dev_t *d, void *user)
{
    disc_ctx_t *c = (disc_ctx_t *)user;
    if (!c || !c->m) return;
    CM_LOCK(c->m);
    on_34569(d, user);      /* 内部再调 on_discovered(不自锁),递归 CM_LOCK 安全 */
    CM_UNLOCK(c->m);
}

void nvr_chan_tick(nvr_chan_mgr_t *m)
{
    if (!m) return;
    /* 开机暖缓存:首 tick 与第 ~10s 各扫一次(覆盖开机稍后才上线的相机),之后**不再周期扫**,
     * 避免周期性 ONVIF 广播干扰实时流。后续新增/改 IP 走各自 probe 兜底。 */
    static unsigned s_scan_tick = 0;
    if (s_scan_tick == 0 || s_scan_tick == 10) disc_scan_kick();
    s_scan_tick++;
    CM_LOCK(m);
    time_t now = time(NULL);
    /* 每 tick 最多解析 2 路 ONVIF URL(每路 ~2s)。空口已被 ARP 门控跳过、不占额度,故这里只花在
     * 在场相机上 → 首轮出图更快;上限 2 兼顾主循环杂务不被过久阻塞(视频/8089 各自线程不受影响)。 */
    /* ★ 优先解析:用户刚加设备/改密(prio)的通道先解析,单列额度,不排在开机批量解析(每 tick 2 路)
     * 后面 → setLanDevice 改密后立即用新账密去连,而不是等十几路 ONVIF 解析完。一次性(处理后清 prio)。 */
    int prio_budget = NVR_RESOLVE_MAX_CONCURRENT;   /* 并发解析:每 tick 可派多路(受 g_resolve_active 封顶) */
    for (int i = 0; i < NVR_MAX_CH && prio_budget > 0; i++) {
        if (m->slots[i].in_use && m->slots[i].prio) {
            m->slots[i].prio = 0;                       /* 一次性:给它抢先解析这一发 */
            tick_slot(m, &m->slots[i], now, &prio_budget);
        }
    }

    int resolve_budget = NVR_RESOLVE_MAX_CONCURRENT;   /* 同上:并发派发,封顶在 g_resolve_active */
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->slots[i].in_use) tick_slot(m, &m->slots[i], now, &resolve_budget);

    /* ---- PoE 即插即用发现 / LAN 找回:**持锁只做选路+调度状态更新(快),2s 广播挪到锁外** ----
     * 原来在 CM_LOCK 内做 ONVIF/34569 广播(每 tick 2~6s),开机期把 CM_LOCK 占住 → setDeviceDisplayMode
     * / getDeviceDisplayMode 等取 CM_LOCK 的命令被拖到数秒才回。现改:持锁选出本 tick 要广播的 1 个 PoE
     * 口 + 1 路 LAN 找回并快照参数,解锁后广播(回调用 *_locked 包装,每相机短临界区),PoE 的 ARP 判定/
     * 清口再重锁应用。PoE 口↔段 1:1;段=物理口号(不用 ip_seg3,脏 IP/fe80 会永久跳过)。 */
    int  poe_do = 0, poe_slot = -1, poe_seg = 0, poe_had_dev = 0;
    char poe_src[64] = "", poe_camip[32] = "";
    int  poe_n = (NVR_POE_PORTS < NVR_MAX_CH) ? NVR_POE_PORTS : NVR_MAX_CH;
    for (int k = 0; k < poe_n; k++) {
        int i = (m->poe_scan_cursor + k) % poe_n;        /* 游标轮询:公平覆盖所有 PoE 口 */
        slot_t *s = &m->slots[i];
        if (!s->in_use || s->d.poe_port <= 0) continue;
        if (s->status == NVR_CHAN_ONLINE) continue;      /* 真正在线出图才停发现 */
        if (now < s->poe_disc_next) continue;
        poe_seg = s->d.poe_port;
        s->poe_disc_next = now + 10;
        m->poe_scan_cursor = (i + 1) % poe_n;            /* 下轮从下一口起 */
        snprintf(poe_src, sizeof(poe_src), "%d.%d.%d.100", NVR_POE_NET_A, NVR_POE_NET_B, poe_seg);
        poe_had_dev = s->poe_present || s->d.mac[0] || s->d.serial[0];
        s->poe_seen = 0;                                  /* 本轮探测前清;命中由广播期 on_discovered_locked 置1 */
        if (is_ipv4_in_poe_seg(s->d.onvif_ip, poe_seg))   /* ARP 兜底目标:已绑段内 IPv4;否则该段 .1 */
            snprintf(poe_camip, sizeof(poe_camip), "%s", s->d.onvif_ip);
        else
            snprintf(poe_camip, sizeof(poe_camip), "%d.%d.%d.1", NVR_POE_NET_A, NVR_POE_NET_B, poe_seg);
        poe_slot = i; poe_do = 1;
        NVR_LOGI("chan", "PoE ch%d(口%d,段198.18.%d)ONVIF 发现广播 src=%s", s->d.chn, s->d.poe_port, poe_seg, poe_src);
        break;                                            /* 每 tick 至多 1 口 */
    }

    int  lan_do = 0;
    char lan_eth0[64] = "";
    for (int i = NVR_IP_CH_BASE; i < NVR_MAX_CH; i++) {
        slot_t *s = &m->slots[i];
        if (!s->in_use || s->d.poe_port > 0 || !s->d.mac[0]) continue;
        if (s->status != NVR_CHAN_NOSIGNAL && s->status != NVR_CHAN_FAIL) continue;  /* 只找回掉线的 */
        if (now < s->recall_next) continue;
        s->recall_next = now + 30;
        if (iface_ipv4("eth0", lan_eth0, sizeof(lan_eth0)) == 0 && lan_eth0[0]) {
            lan_do = 1;
            NVR_LOGI("chan", "ch%d LAN 掉线找回:eth0(%s) 按 mac=%s（ONVIF+34569）", i, lan_eth0, s->d.mac);
        }
        break;   /* 每 tick 至多 1 路找回 */
    }
    CM_UNLOCK(m);

    /* ---- 锁外广播(2s 不占 CM_LOCK → setDeviceDisplayMode 等命令立即拿到 CM_LOCK)---- */
    if (poe_do) {
        disc_ctx_t ctx = { m, 0, 0, poe_seg };            /* poe_seg:on_discovered 按段绑口(W2) */
        nvr_onvif_discover(poe_src, 2, on_discovered_locked, &ctx);
        /* WS-Discovery 多播偶发丢包 → 不能仅凭"未发现"就清口。未命中再 ARP 二层探测确认在场;
         * 可达=在场(清零);不可达累计达阈值 → 判定拔除清口。poe_seen 由广播期回调置。 */
        int seen = 1, still = 0;
        CM_LOCK(m);
        { slot_t *s = &m->slots[poe_slot];
          still = (s->in_use && s->d.poe_port == poe_seg);
          seen  = still ? s->poe_seen : 1; }
        CM_UNLOCK(m);
        if (still && !seen && poe_had_dev) {
            int reachable = poe_cam_reachable(poe_camip);   /* ARP 探测:网络,锁外 */
            CM_LOCK(m);
            { slot_t *s = &m->slots[poe_slot];
              if (s->in_use && s->d.poe_port == poe_seg) {  /* 重校验:解锁窗口内可能被增删/换机 */
                  if (reachable) s->poe_miss = 0;
                  else if (++s->poe_miss >= NVR_POE_MISS_CLEAR) clear_poe_port(m, s);
              } }
            CM_UNLOCK(m);
        }
    }
    if (lan_do) {
        disc_ctx_t ctx = { m, 0, 1 };                     /* recall_only:只更新已知,不新增 */
        nvr_lan34569_discover(lan_eth0, 1, on_34569_locked, &ctx);   /* 先 34569:应答带 MAC */
        nvr_onvif_discover(lan_eth0, 2, on_discovered_locked, &ctx);
    }
}

int nvr_chan_get(nvr_chan_mgr_t *m, int chn, nvr_channel_t *out)
{
    if (!m || !out) return -1;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    int rc = (s && s->in_use) ? (*out = s->d, 0) : -1;
    CM_UNLOCK(m);
    return rc;
}

int nvr_chan_query_device_info(nvr_chan_mgr_t *m, int chn, nvr_chan_devinfo_t *out)
{
    if (!m || !out) return -1;
    memset(out, 0, sizeof(*out));
    /* 锁内只快照地址/凭据/后端(微秒级);网络往返在锁外做(不占 CM_LOCK)。 */
    char ip[64], user[64], pass[64], svc[128];
    int  port, backend;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    if (!s || !s->in_use) { CM_UNLOCK(m); return -1; }
    snprintf(ip,   sizeof(ip),   "%s", s->d.onvif_ip);
    snprintf(user, sizeof(user), "%s", s->d.user);
    snprintf(pass, sizeof(pass), "%s", s->d.pass);
    snprintf(svc,  sizeof(svc),  "%s", s->d.service_url);
    port    = s->d.onvif_port;
    backend = s->d.backend;
    CM_UNLOCK(m);
    if (!ip[0]) return -1;

    if (backend == 0) {
        /* NOP / nopOnvif 透传:向设备下发 getDeviceInfo,取 content.model/serialNumber/firmwareVersion。 */
        int dport = port > 0 ? port : 80;
        char *resp = chan_nop_post(ip, dport, user, pass, "{\"func\":\"getDeviceInfo\",\"args\":{}}");
        if (!resp) return -1;
        cJSON *j = cJSON_Parse(resp); free(resp);
        if (!j) return -1;
        cJSON *ic = cJSON_GetObjectItem(j, "content");
        int ok = -1;
        if (ic) {
            const char *mdl = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "model"));
            const char *ser = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "serialNumber"));
            const char *fwv = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "firmwareVersion"));
            const char *man = cJSON_GetStringValue(cJSON_GetObjectItem(ic, "manufacturer"));
            if (mdl) snprintf(out->model,        sizeof(out->model),        "%s", mdl);
            if (ser) snprintf(out->serial,       sizeof(out->serial),       "%s", ser);
            if (fwv) snprintf(out->firmware,     sizeof(out->firmware),     "%s", fwv);
            if (man) snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", man);
            ok = 0;
        }
        cJSON_Delete(j);
        return ok;
    }
    /* ONVIF 翻译:GetDeviceInformation(用已存 service_url 直连,不广播)。 */
    return nvr_onvif_get_device_info(ip, port > 0 ? port : 80, svc[0] ? svc : NULL, user, pass,
                                     out->manufacturer, (int)sizeof(out->manufacturer),
                                     out->model,        (int)sizeof(out->model),
                                     out->firmware,     (int)sizeof(out->firmware),
                                     out->serial,       (int)sizeof(out->serial));
}

int nvr_chan_list(nvr_chan_mgr_t *m, nvr_channel_t *out, int cap)
{
    if (!m || !out) return -1;
    int n = 0;
    CM_LOCK(m);
    for (int i = 0; i < NVR_MAX_CH && n < cap; i++)
        if (m->slots[i].in_use) out[n++] = m->slots[i].d;
    CM_UNLOCK(m);
    return n;
}

nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t *m, int chn)
{
    if (!m) return NVR_CHAN_EMPTY;
    CM_LOCK(m);
    slot_t *s = slot_of(m, chn);
    nvr_chan_status_t st = (s && s->in_use) ? s->status : NVR_CHAN_EMPTY;
    CM_UNLOCK(m);
    return st;
}
