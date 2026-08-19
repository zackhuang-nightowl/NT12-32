/***************************************************************************************
 *  nvr_nop8012.c — 8012 事件中心客户端 reactor。见 nvr_nop8012.h。
 *
 *  一个线程 + poll() 管所有 NOP 通道 socket。每通道状态机:
 *    DISCONN → CONNECTING(非阻塞 connect) → LOGIN_WAIT(发 LOGIN,5s 等 ACK)
 *            → ONLINE(收 SEND_MSG + 30s 心跳)
 *  断线/超时/ACK_FAIL → 关连接 + 指数退避(5→30s)重连。
 ***************************************************************************************/
#include "nvr_nop8012.h"
#include "nvr_nop8012_proto.h"
#include "nvr_event.h"              /* nvr_evt_detect_from_msgtype */
#include "nvr_dev_classify.h"           /* NVR_DEV_KIND_NOP */
#include "nvr_crypto.h"                 /* nvr_pw_8012_digest */
#include "nvr_log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define N8012_PORT_DEFAULT 8012
#define HEARTBEAT_S        30
#define LOGIN_TIMEOUT_S    5
#define BACKOFF_BASE_S     5
#define BACKOFF_MAX_S      30
#define RXBUF_CAP          (64 * 1024)   /* 常规事件帧;超大 JPEG 分多次 recv 累积到 payload 缓冲 */

enum { ST_DISCONN = 0, ST_CONNECTING, ST_LOGIN_WAIT, ST_ONLINE };

typedef struct {
    int      used;
    int      chn;                 /* NVR 0 基通道 */
    char     ip[64];
    char     user[64];
    char     pass[64];
    int      fd;
    int      state;
    time_t   next_retry;
    time_t   login_deadline;
    time_t   last_hb;
    int      backoff;
    /* 接收累积缓冲:先凑 40B 头,再按 data_size 收满 payload */
    uint8_t *buf;
    size_t   len;                 /* 已累积字节 */
    size_t   cap;                 /* buf 容量 */
    size_t   need;                /* 当前期望的完整帧长(40 或 40+data_size);0=未定 */
} slot_t;

struct nvr_nop8012 {
    nvr_nop8012_cfg_t cfg;
    int               port;
    volatile int      running;
    pthread_t         thr;
    slot_t            slot[NVR_MAX_CH];
};

/* ---- 小工具 ---- */
static void set_nonblock(int fd) { int fl = fcntl(fd, F_GETFL, 0); if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK); }

static void slot_close(slot_t *s)
{
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    s->state = ST_DISCONN;
    s->len = 0; s->need = 0;
}

static void slot_backoff(slot_t *s, time_t now)
{
    slot_close(s);
    s->backoff = s->backoff ? (s->backoff * 2 > BACKOFF_MAX_S ? BACKOFF_MAX_S : s->backoff * 2) : BACKOFF_BASE_S;
    s->next_retry = now + s->backoff;
}

/* 选登录账密:
 *   通道 pass 非空 = digest 已开 → 上层用时间戳 + 8012 digest(P_enh)；
 *   通道 pass 空 = digest 已关 → 空密码交互。 */
static void pick_password(nvr_nop8012_t *c, const slot_t *s, char *out, size_t cap)
{
    (void)c;
    snprintf(out, cap, "%s", s->pass[0] ? s->pass : "");
}

/* 从 nvr_chan_mgr 同步 NOP 通道到 slot[](按通道号索引)。 */
static void sync_slots(nvr_nop8012_t *c)
{
    nvr_channel_t list[NVR_MAX_CH];
    int n = c->cfg.cm ? nvr_chan_list(c->cfg.cm, list, NVR_MAX_CH) : 0;
    char present[NVR_MAX_CH] = {0};

    for (int i = 0; i < n; i++) {
        const nvr_channel_t *ch = &list[i];
        if (ch->chn < 0 || ch->chn >= NVR_MAX_CH) continue;
        if (!ch->enabled || ch->kind != NVR_DEV_KIND_NOP || !ch->onvif_ip[0]) continue;
        present[ch->chn] = 1;
        slot_t *s = &c->slot[ch->chn];
        /* IP/凭据变化 → 重置连接以用新参数（含开关 digest 后的 P_enh ↔ 空） */
        if (s->used && (strcmp(s->ip, ch->onvif_ip) != 0 ||
                        strcmp(s->user, ch->user[0] ? ch->user : "admin") != 0 ||
                        strcmp(s->pass, ch->pass) != 0)) {
            slot_close(s); s->used = 0;
        }
        if (!s->used) {
            memset(s, 0, sizeof(*s));
            s->used = 1; s->chn = ch->chn; s->fd = -1; s->state = ST_DISCONN;
            snprintf(s->ip, sizeof(s->ip), "%s", ch->onvif_ip);
            snprintf(s->user, sizeof(s->user), "%s", ch->user[0] ? ch->user : "admin");
            snprintf(s->pass, sizeof(s->pass), "%s", ch->pass);
            s->next_retry = 0;   /* 立即尝试 */
        } else {
            snprintf(s->pass, sizeof(s->pass), "%s", ch->pass);  /* 密码可热更新 */
        }
    }
    /* 通道消失 → 关连接释放 slot */
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (c->slot[i].used && !present[i]) {
            slot_close(&c->slot[i]);
            free(c->slot[i].buf);
            memset(&c->slot[i], 0, sizeof(slot_t));
            c->slot[i].fd = -1;
        }
    }
}

/* 发起非阻塞连接。 */
static void slot_connect(nvr_nop8012_t *c, slot_t *s, time_t now)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { slot_backoff(s, now); return; }
    set_nonblock(fd);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)c->port);
    if (inet_pton(AF_INET, s->ip, &sa.sin_addr) != 1) { close(fd); slot_backoff(s, now); return; }
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno != EINPROGRESS) { close(fd); slot_backoff(s, now); return; }
    s->fd = fd; s->state = ST_CONNECTING;
}

/* connect 完成 → 发登录。 */
static void slot_send_login(nvr_nop8012_t *c, slot_t *s, time_t now)
{
    int err = 0; socklen_t el = sizeof(err);
    if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) {
        NVR_LOGW("8012", "ch%d 连接 %s 失败: %s", s->chn, s->ip, strerror(err ? err : errno));
        slot_backoff(s, now); return;
    }
    char user[32], pass[64];
    if (s->pass[0] && nvr_pw_enh_ready()) {
        char ts[24], dig[24];
        snprintf(ts, sizeof(ts), "%ld", (long)now);
        if (nvr_pw_8012_digest(ts, s->pass, dig, sizeof(dig)) != 16) {
            slot_backoff(s, now); return;
        }
        snprintf(user, sizeof(user), "%s", ts);
        snprintf(pass, sizeof(pass), "%s", dig);
    } else {
        snprintf(user, sizeof(user), "%s", s->user[0] ? s->user : "admin");
        pick_password(c, s, pass, sizeof(pass));
    }
    uint8_t pkt[NVR_N8012_HDR_SIZE + NVR_N8012_LOGIN_SIZE];
    int plen = nvr_n8012_pack_login(pkt, user, pass);
    if (send(s->fd, pkt, (size_t)plen, MSG_NOSIGNAL) != plen) { slot_backoff(s, now); return; }
    s->state = ST_LOGIN_WAIT; s->login_deadline = now + LOGIN_TIMEOUT_S;
    NVR_LOGI("8012", "ch%d 登录 %s:%d (user=%s digest=%s)",
             s->chn, s->ip, c->port, user, s->pass[0] ? "on" : "off");
}

/* 处理一条完整帧(已在 s->buf[0..need))。返回 0 正常, -1 需关连接。 */
static int handle_frame(nvr_nop8012_t *c, slot_t *s, time_t now)
{
    nvr_n8012_hdr_t h;
    if (nvr_n8012_parse_header(s->buf, &h) != 0) return -1;   /* magic 错 → 关连接重连 */

    switch (h.cmd) {
        case N8012_CMD_ACK_OK:
            s->state = ST_ONLINE; s->backoff = 0; s->last_hb = now;
            NVR_LOGI("8012", "ch%d 登录成功(ACK_OK)", s->chn);
            break;
        case N8012_CMD_ACK_FAIL: {
            char rnd[13];
            memcpy(rnd, s->buf + 28, 12);
            rnd[12] = 0;
            {
                int i;
                for (i = 0; i < 12 && rnd[i]; i++)
                    if (rnd[i] < 32 || rnd[i] > 126) { rnd[i] = 0; break; }
            }
            if (rnd[0] && nvr_pw_enh_ready()) {
                char penh[24];
                if (nvr_pw_from_random(rnd, penh, sizeof(penh)) == 16) {
                    NVR_LOGW("8012", "ch%d ACK_FAIL random=%s → 更新 P_enh 重登", s->chn, rnd);
                    nvr_chan_set_enh(c->cfg.cm, s->chn, rnd, penh);
                    snprintf(s->pass, sizeof(s->pass), "%s", penh);
                }
            } else if (s->pass[0]) {
                /* 增强登录被拒且头里没有 random → 退回普通模式（等同 HTTP 402） */
                NVR_LOGW("8012", "ch%d ACK_FAIL 无 random → 清 digest", s->chn);
                nvr_chan_set_enh(c->cfg.cm, s->chn, "", "");
                s->pass[0] = 0;
            } else
                NVR_LOGW("8012", "ch%d 登录被拒(ACK_FAIL)", s->chn);
            return -1;
        }
        case N8012_CMD_SEND_MSG: {
            nop_detect_type_t type = nvr_evt_detect_from_msgtype(h.msg_type);
            if (type == NOP_DETECT_TYPE_MAX) break;           /* 未知类型忽略 */
            nop_event_t ev; memset(&ev, 0, sizeof(ev));
            ev.channel = s->chn; ev.type = type;
            ev.timestamp_ms = (uint64_t)now * 1000u;
            if (h.data_size > 0) {
                const uint8_t *jpeg = NULL; uint32_t jlen = 0;
                if (nvr_n8012_extract_jpeg(h.extend_flag, s->buf + NVR_N8012_HDR_SIZE,
                                           h.data_size, &jpeg, &jlen) == 0) {
                    ev.jpeg = jpeg; ev.jpeg_len = jlen;
                }
            }
            if (c->cfg.hub) nop_event_publish(c->cfg.hub, &ev);
            NVR_LOGI("8012", "ch%d 事件 msgType=%u → detect=%d%s",
                     s->chn, h.msg_type, (int)type, ev.jpeg ? " (带图)" : "");
            break;
        }
        default: break;   /* HEARTBEAT/CLOSE 等忽略 */
    }
    return 0;
}

/* 从 fd 读入并按帧解析。返回 0 正常, -1 需关连接。 */
static int slot_recv(nvr_nop8012_t *c, slot_t *s, time_t now)
{
    for (;;) {
        /* 确保有头空间 */
        if (!s->buf) { s->cap = RXBUF_CAP; s->buf = malloc(s->cap); if (!s->buf) return -1; }

        /* 定帧长:先要 40B 头 */
        if (s->need == 0) {
            if (s->len < NVR_N8012_HDR_SIZE) {
                /* 继续收头 */
            } else {
                nvr_n8012_hdr_t h;
                if (nvr_n8012_parse_header(s->buf, &h) != 0) return -1;
                if (h.data_size > NVR_N8012_MAX_DATA) return -1;   /* 越界 → 关连接 */
                s->need = (size_t)NVR_N8012_HDR_SIZE + h.data_size;
                /* 扩容以容纳完整帧 */
                if (s->need > s->cap) {
                    size_t ncap = s->need;
                    uint8_t *nb = realloc(s->buf, ncap);
                    if (!nb) return -1;
                    s->buf = nb; s->cap = ncap;
                }
            }
        }

        /* 帧已完整 → 处理并移除 */
        if (s->need && s->len >= s->need) {
            if (handle_frame(c, s, now) != 0) return -1;
            size_t rest = s->len - s->need;
            if (rest) memmove(s->buf, s->buf + s->need, rest);
            s->len = rest; s->need = 0;
            continue;   /* 可能还有下一帧 */
        }

        /* 读更多 */
        size_t want = s->need ? s->need - s->len : (size_t)NVR_N8012_HDR_SIZE - s->len;
        if (s->len + want > s->cap) want = s->cap - s->len;
        if (want == 0) return -1;   /* 异常 */
        ssize_t r = recv(s->fd, s->buf + s->len, want, 0);
        if (r > 0) { s->len += (size_t)r; continue; }
        if (r == 0) return -1;      /* 对端关闭 */
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  /* 收干净了 */
        if (errno == EINTR) continue;
        return -1;                  /* 其它错误 */
    }
}

static void *reactor_main(void *arg)
{
    nvr_nop8012_t *c = arg;
    while (c->running) {
        sync_slots(c);
        time_t now = time(NULL);

        struct pollfd pfd[NVR_MAX_CH];
        int idx[NVR_MAX_CH];
        int nf = 0;

        for (int i = 0; i < NVR_MAX_CH; i++) {
            slot_t *s = &c->slot[i];
            if (!s->used) continue;
            if (s->state == ST_DISCONN) {
                if (now >= s->next_retry) slot_connect(c, s, now);
            }
            if (s->fd < 0) continue;
            pfd[nf].fd = s->fd;
            pfd[nf].events = (s->state == ST_CONNECTING) ? POLLOUT : POLLIN;
            pfd[nf].revents = 0;
            idx[nf] = i; nf++;
        }

        int pr = poll(pfd, (nfds_t)nf, 1000);
        now = time(NULL);

        if (pr > 0) {
            for (int k = 0; k < nf; k++) {
                slot_t *s = &c->slot[idx[k]];
                if (s->fd < 0) continue;
                short re = pfd[k].revents;
                if (re & (POLLERR | POLLHUP | POLLNVAL)) { slot_backoff(s, now); continue; }
                if (s->state == ST_CONNECTING && (re & POLLOUT)) {
                    slot_send_login(c, s, now);
                } else if ((re & POLLIN) && (s->state == ST_LOGIN_WAIT || s->state == ST_ONLINE)) {
                    if (slot_recv(c, s, now) != 0) slot_backoff(s, now);
                }
            }
        }

        /* 定时器:登录超时 / 心跳 */
        for (int i = 0; i < NVR_MAX_CH; i++) {
            slot_t *s = &c->slot[i];
            if (!s->used || s->fd < 0) continue;
            if (s->state == ST_LOGIN_WAIT && now >= s->login_deadline) {
                NVR_LOGW("8012", "ch%d 登录超时", s->chn); slot_backoff(s, now);
            } else if (s->state == ST_ONLINE && now - s->last_hb >= HEARTBEAT_S) {
                uint8_t hb[NVR_N8012_HDR_SIZE];
                int n = nvr_n8012_pack_heartbeat(hb);
                if (send(s->fd, hb, (size_t)n, MSG_NOSIGNAL) != n) slot_backoff(s, now);
                else s->last_hb = now;
            }
        }
    }
    return NULL;
}

int nvr_nop8012_start(const nvr_nop8012_cfg_t *cfg, nvr_nop8012_t **out)
{
    if (!cfg || !cfg->hub || !out) return -1;
    nvr_nop8012_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;
    c->cfg = *cfg;
    c->port = cfg->port > 0 ? cfg->port : N8012_PORT_DEFAULT;
    for (int i = 0; i < NVR_MAX_CH; i++) c->slot[i].fd = -1;
    c->running = 1;
    if (pthread_create(&c->thr, NULL, reactor_main, c) != 0) { free(c); return -1; }
    NVR_LOGI("8012", "事件中心客户端启动(port %d)", c->port);
    *out = c;
    return 0;
}

void nvr_nop8012_stop(nvr_nop8012_t *c)
{
    if (!c) return;
    c->running = 0;
    pthread_join(c->thr, NULL);
    for (int i = 0; i < NVR_MAX_CH; i++) { slot_close(&c->slot[i]); free(c->slot[i].buf); }
    free(c);
}
