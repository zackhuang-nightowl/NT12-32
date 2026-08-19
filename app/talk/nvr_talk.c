/***************************************************************************************
 *  nvr_talk.c — 本机 127.0.0.1:7000 收 App 音频，转发到相机（NOP:7000 / ONVIF backchannel）。
 *  App startSpeaker URL = tcp://iotc-tunnel:7000/speaker；POST 后不回 HTTP。
 ***************************************************************************************/
#include "nvr_talk.h"
#include "nvr_onvif.h"
#include "nvr_crypto.h"
#include "nvr_log.h"
#include "nvr_dev_classify.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define TALK_MAX_CH 32
#define TALK_G711_PT 160   /* 20ms @ 8kHz */
#define TALK_IDLE_S  20    /* 文档：20s 无数据关连接 */

enum { CODEC_PCM = 0, CODEC_U = 1, CODEC_A = 2 };

typedef struct {
    int fd, cseq, pt, rtp_ch;
    uint16_t seq;
    uint32_t ts, ssrc;
    char session[64];
    char url[300];
    char host[128];
    int  port;
    char path[160];
    char user[64], pass[64];
    char realm[64], nonce[128], qop[32];
    int  need_auth;
} bc_t;

typedef struct {
    int  active, backend, onvif_port, codec, chn;
    char ip[64], user[64], pass[64], vsrc[100];
    int  cam_fd;
    bc_t *bc;
    struct nvr_talk *owner;
} talk_ch_t;

struct nvr_talk {
    int listen_fd, listen_port;
    volatile int run;
    pthread_t tid;
    pthread_mutex_t lock;
    talk_ch_t ch[TALK_MAX_CH];
    int last_chn;
    int sess_fd;           /* 同时只一会话；>=0 时拒新连接 */
    nvr_talk_enh_fn on_enh;
    void *on_enh_ud;
};

/* ---- G.711 ---- */
static uint8_t lin2ulaw(int16_t pcm)
{
    int sign = (pcm >> 8) & 0x80;
    if (sign) pcm = -pcm;
    if (pcm > 32635) pcm = 32635;
    pcm += 0x84;
    int exp = 7;
    for (int mask = 0x4000; (pcm & mask) == 0 && exp > 0; mask >>= 1) exp--;
    int mant = (pcm >> (exp + 3)) & 0x0F;
    return (uint8_t)(~(sign | (exp << 4) | mant));
}
static uint8_t lin2alaw(int16_t pcm)
{
    int sign = (pcm >> 8) & 0x80;
    if (sign) pcm = (int16_t)(-pcm - 1);
    if (pcm > 32635) pcm = 32635;
    int exp = 7;
    for (int mask = 0x4000; (pcm & mask) == 0 && exp > 0; mask >>= 1) exp--;
    int mant = (pcm >> ((exp == 0) ? 4 : (exp + 3))) & 0x0F;
    uint8_t alaw = (uint8_t)(sign | (exp << 4) | mant);
    return alaw ^ 0x55;
}

static int codec_of(const char *s)
{
    if (!s || !s[0]) return CODEC_U;
    if (!strcmp(s, "g711a") || !strcmp(s, "G711A")) return CODEC_A;
    if (!strcmp(s, "pcm") || !strcmp(s, "PCM")) return CODEC_PCM;
    return CODEC_U;
}

static void md5hex(const char *s, char out[33])
{
    uint8_t d[NVR_MD5_LEN];
    nvr_md5(s, strlen(s), d);
    nvr_hex(d, NVR_MD5_LEN, out, 33);
}

static int tcp_connect(const char *ip, int port, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    return fd;
}

static int sock_sendall(int fd, const void *p, size_t n)
{
    const char *s = p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, s + off, n - off, 0);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static void hdr_get(const char *hdr, const char *key, char *out, size_t cap)
{
    out[0] = 0;
    size_t klen = strlen(key);
    const char *p = hdr;
    while (p && *p) {
        if (strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
            p += klen + 1;
            while (*p == ' ' || *p == '\t') p++;
            size_t i = 0;
            while (p[i] && p[i] != '\r' && p[i] != '\n' && i + 1 < cap) {
                out[i] = p[i]; i++;
            }
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

static int digest_resp(const char *user, const char *pass, const char *realm,
                       const char *nonce, const char *method, const char *uri,
                       const char *qop, const char *cnonce, char out[33])
{
    char a1[256], a2[320], ha1[33], ha2[33], resp[512];
    snprintf(a1, sizeof(a1), "%s:%s:%s", user, realm, pass);
    md5hex(a1, ha1);
    snprintf(a2, sizeof(a2), "%s:%s", method, uri);
    md5hex(a2, ha2);
    if (qop && qop[0])
        snprintf(resp, sizeof(resp), "%s:%s:00000001:%s:auth:%s", ha1, nonce, cnonce, ha2);
    else
        snprintf(resp, sizeof(resp), "%s:%s:%s", ha1, nonce, ha2);
    md5hex(resp, out);
    return 0;
}

static void parse_digest_chal(const char *www, char *realm, size_t rc,
                              char *nonce, size_t nc, char *qop, size_t qc)
{
    realm[0] = nonce[0] = qop[0] = 0;
    const char *p;
    if ((p = strstr(www, "realm=\""))) {
        p += 7; size_t i = 0;
        while (p[i] && p[i] != '"' && i + 1 < rc) { realm[i] = p[i]; i++; }
        realm[i] = 0;
    }
    if ((p = strstr(www, "nonce=\""))) {
        p += 7; size_t i = 0;
        while (p[i] && p[i] != '"' && i + 1 < nc) { nonce[i] = p[i]; i++; }
        nonce[i] = 0;
    }
    if ((p = strstr(www, "qop=\""))) {
        p += 5; size_t i = 0;
        while (p[i] && p[i] != '"' && i + 1 < qc) { qop[i] = p[i]; i++; }
        qop[i] = 0;
    } else if ((p = strstr(www, "qop="))) {
        p += 4; size_t i = 0;
        while (p[i] && p[i] != ',' && p[i] != ' ' && i + 1 < qc) { qop[i] = p[i]; i++; }
        qop[i] = 0;
    }
}

static int parse_random_hdr(const char *http, char *out, size_t cap)
{
    const char *p = http;
    if (!out || cap == 0) return -1;
    out[0] = 0;
    while (p && *p) {
        if (strncasecmp(p, "Random:", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            size_t i = 0;
            while (i + 1 < cap && p[i] && p[i] != '\r' && p[i] != '\n') {
                out[i] = p[i];
                i++;
            }
            out[i] = 0;
            return out[0] ? 0 : -1;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return -1;
}

static void talk_note_enh(talk_ch_t *c, const char *random, const char *penh)
{
    nvr_talk_t *t;
    if (!c || !c->owner) return;
    t = c->owner;
    if (t->on_enh) t->on_enh(t->on_enh_ud, c->chn, random ? random : "", penh ? penh : "");
}
static int nop_open(talk_ch_t *c)
{
    int fd = tcp_connect(c->ip, NVR_TALK_PORT, 3000);
    if (fd < 0) return -1;
    char req[512];
    snprintf(req, sizeof(req),
             "POST /speaker HTTP/1.1\r\nHost: %s:%d\r\n"
             "Content-Type: application/octet-stream\r\n"
             "Connection: keep-alive\r\n\r\n",
             c->ip, NVR_TALK_PORT);
    if (sock_sendall(fd, req, strlen(req)) != 0) { close(fd); return -1; }
    /* 短等 401/402：401+Random 重算 P_enh 再 Digest；402 清 digest 保持无鉴权。 */
    char peek[1024];
    fd_set rf; struct timeval tv = { 0, 300000 };
    FD_ZERO(&rf); FD_SET(fd, &rf);
    if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
        ssize_t n = recv(fd, peek, sizeof(peek) - 1, MSG_DONTWAIT);
        if (n > 0) {
            peek[n] = 0;
            if (strstr(peek, "402")) {
                close(fd);
                c->pass[0] = 0;
                talk_note_enh(c, "", "");
                fd = tcp_connect(c->ip, NVR_TALK_PORT, 3000);
                if (fd < 0) return -1;
                if (sock_sendall(fd, req, strlen(req)) != 0) { close(fd); return -1; }
            } else if (strstr(peek, "401")) {
                char rnd[64] = {0};
                parse_random_hdr(peek, rnd, sizeof(rnd));
                if (rnd[0] && nvr_pw_enh_ready()) {
                    char penh[24];
                    if (nvr_pw_from_random(rnd, penh, sizeof(penh)) == 16) {
                        snprintf(c->user, sizeof(c->user), "admin");
                        snprintf(c->pass, sizeof(c->pass), "%s", penh);
                        talk_note_enh(c, rnd, penh);
                    }
                }
                if (c->user[0] && c->pass[0]) {
                    close(fd);
                    fd = tcp_connect(c->ip, NVR_TALK_PORT, 3000);
                    if (fd < 0) return -1;
                    char realm[64], nonce[128], qop[32], dresp[33];
                    parse_digest_chal(peek, realm, sizeof(realm), nonce, sizeof(nonce), qop, sizeof(qop));
                    digest_resp(c->user, c->pass, realm, nonce, "POST", "/speaker",
                                qop, "nvr1", dresp);
                    int m = snprintf(req, sizeof(req),
                        "POST /speaker HTTP/1.1\r\nHost: %s:%d\r\n"
                        "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                        "uri=\"/speaker\", response=\"%s\"%s%s\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Connection: keep-alive\r\n\r\n",
                        c->ip, NVR_TALK_PORT, c->user, realm, nonce, dresp,
                        qop[0] ? ", qop=auth, nc=00000001, cnonce=\"nvr1\"" : "", "");
                    if (m <= 0 || sock_sendall(fd, req, (size_t)m) != 0) { close(fd); return -1; }
                }
            }
        }
    }
    c->cam_fd = fd;
    return 0;
}

/* ---- ONVIF RTSP backchannel ---- */
static int rtsp_read(int fd, char *buf, size_t cap, size_t *body_off)
{
    size_t n = 0;
    *body_off = 0;
    while (n + 1 < cap) {
        ssize_t r = recv(fd, buf + n, cap - 1 - n, 0);
        if (r <= 0) return -1;
        n += (size_t)r;
        buf[n] = 0;
        char *end = strstr(buf, "\r\n\r\n");
        if (!end) continue;
        *body_off = (size_t)(end - buf) + 4;
        long cl = 0;
        const char *p = buf;
        while (p && p < end) {
            if (strncasecmp(p, "Content-Length:", 15) == 0) {
                cl = strtol(p + 15, NULL, 10);
                break;
            }
            p = strchr(p, '\n');
            if (p) p++;
        }
        while (n < *body_off + (size_t)cl && n + 1 < cap) {
            r = recv(fd, buf + n, cap - 1 - n, 0);
            if (r <= 0) break;
            n += (size_t)r;
        }
        buf[n] = 0;
        return (int)n;
    }
    return -1;
}

static int blk_has(const char *s, const char *e, const char *need)
{
    size_t nlen = strlen(need);
    size_t blen = (size_t)(e - s);
    if (blen < nlen) return 0;
    for (size_t i = 0; i + nlen <= blen; i++)
        if (memcmp(s + i, need, nlen) == 0) return 1;
    return 0;
}

static int rtsp_status(const char *r)
{
    int c = 0;
    if (sscanf(r, "RTSP/%*s %d", &c) == 1) return c;
    return 0;
}

static void parse_rtsp_url(const char *in, char *host, int *port, char *path,
                           char *user, char *pass)
{
    host[0] = path[0] = user[0] = pass[0] = 0;
    *port = 554;
    const char *p = strstr(in, "://");
    p = p ? p + 3 : in;
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s", p);
    char *at = strchr(tmp, '@');
    char *hp = tmp;
    if (at) {
        *at = 0;
        char *col = strchr(tmp, ':');
        if (col) { *col = 0; snprintf(user, 64, "%s", tmp); snprintf(pass, 64, "%s", col + 1); }
        else snprintf(user, 64, "%s", tmp);
        hp = at + 1;
    }
    char *sl = strchr(hp, '/');
    if (sl) { snprintf(path, 160, "%s", sl); *sl = 0; }
    else snprintf(path, 160, "/");
    char *pc = strchr(hp, ':');
    if (pc) { *pc = 0; *port = atoi(pc + 1); if (*port <= 0) *port = 554; }
    snprintf(host, 128, "%s", hp);
}

static void auth_hdr(const bc_t *b, const char *method, const char *uri, char *out, size_t cap)
{
    out[0] = 0;
    if (!b->need_auth || !b->user[0]) return;
    char dresp[33];
    digest_resp(b->user, b->pass, b->realm, b->nonce, method, uri, b->qop, "nvr1", dresp);
    snprintf(out, cap,
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
             "uri=\"%s\", response=\"%s\"%s\r\n",
             b->user, b->realm, b->nonce, uri, dresp,
             b->qop[0] ? ", qop=auth, nc=00000001, cnonce=\"nvr1\"" : "");
}

static int rtsp_txn(bc_t *b, const char *req, char *resp, size_t cap)
{
    if (sock_sendall(b->fd, req, strlen(req)) != 0) return -1;
    size_t bo = 0;
    if (rtsp_read(b->fd, resp, cap, &bo) < 0) return -1;
    int st = rtsp_status(resp);
    hdr_get(resp, "Session", b->session, sizeof(b->session));
    char *semi = strchr(b->session, ';');
    if (semi) *semi = 0;
    return st;
}

static int bc_open(talk_ch_t *c)
{
    char url[300] = {0};
    if (nvr_onvif_get_url(c->ip, c->onvif_port, c->user, c->pass, "main",
                          url, (int)sizeof(url), NULL, 0, c->vsrc[0] ? c->vsrc : NULL) != 0)
        return -1;
    bc_t *b = calloc(1, sizeof(*b));
    if (!b) return -1;
    snprintf(b->url, sizeof(b->url), "%s", url);
    parse_rtsp_url(url, b->host, &b->port, b->path, b->user, b->pass);
    if (!b->user[0]) { snprintf(b->user, sizeof(b->user), "%s", c->user); snprintf(b->pass, sizeof(b->pass), "%s", c->pass); }
    b->fd = tcp_connect(b->host, b->port, 4000);
    if (b->fd < 0) { free(b); return -1; }
    b->cseq = 1; b->pt = 0; b->ssrc = 0x4E565254; b->rtp_ch = 0;

    char req[1024], resp[8192];
    char uri[300];
    snprintf(uri, sizeof(uri), "rtsp://%s:%d%s", b->host, b->port, b->path);

    snprintf(req, sizeof(req),
             "DESCRIBE %s RTSP/1.0\r\nCSeq: %d\r\n"
             "Accept: application/sdp\r\n"
             "Require: www.onvif.org/ver20/backchannel\r\n\r\n",
             uri, b->cseq++);
    int st = rtsp_txn(b, req, resp, sizeof(resp));
    if (st == 401) {
        char www[512];
        hdr_get(resp, "WWW-Authenticate", www, sizeof(www));
        parse_digest_chal(www, b->realm, sizeof(b->realm), b->nonce, sizeof(b->nonce),
                          b->qop, sizeof(b->qop));
        b->need_auth = 1;
        char ah[400];
        auth_hdr(b, "DESCRIBE", uri, ah, sizeof(ah));
        snprintf(req, sizeof(req),
                 "DESCRIBE %s RTSP/1.0\r\nCSeq: %d\r\nAccept: application/sdp\r\n"
                 "Require: www.onvif.org/ver20/backchannel\r\n%s\r\n",
                 uri, b->cseq++, ah);
        st = rtsp_txn(b, req, resp, sizeof(resp));
    }
    if (st / 100 != 2) { close(b->fd); free(b); return -1; }

    char ctrl[200] = {0};
    const char *sdp = strstr(resp, "\r\n\r\n");
    sdp = sdp ? sdp + 4 : resp;
    const char *ma = strstr(sdp, "m=audio");
    while (ma) {
        const char *nextm = strstr(ma + 1, "\nm=");
        const char *blk_end = nextm ? nextm : (ma + strlen(ma));
        if (strstr(ma, "sendonly") || blk_has(ma, blk_end, "a=sendonly")) {
            const char *pt = ma + 7;
            while (*pt == ' ') pt++;
            while (*pt && *pt != ' ') pt++;
            while (*pt == ' ') pt++;
            while (*pt && *pt != ' ') pt++;
            while (*pt == ' ') pt++;
            b->pt = atoi(pt);
            const char *ac = strstr(ma, "a=control:");
            if (ac && ac < blk_end) {
                ac += 10;
                size_t i = 0;
                while (ac[i] && ac[i] != '\r' && ac[i] != '\n' && i + 1 < sizeof(ctrl)) {
                    ctrl[i] = ac[i]; i++;
                }
                ctrl[i] = 0;
            }
            if (blk_has(ma, blk_end, "PCMA")) b->pt = 8;
            else if (blk_has(ma, blk_end, "PCMU")) b->pt = 0;
            break;
        }
        ma = nextm ? strstr(nextm, "m=audio") : NULL;
    }
    char setup_uri[360];
    if (ctrl[0] && strstr(ctrl, "rtsp://")) snprintf(setup_uri, sizeof(setup_uri), "%s", ctrl);
    else if (ctrl[0]) snprintf(setup_uri, sizeof(setup_uri), "%s%s%s", uri,
                               (ctrl[0] == '/' || uri[strlen(uri)-1] == '/') ? "" : "/", ctrl);
    else snprintf(setup_uri, sizeof(setup_uri), "%s", uri);

    {
        char ah[400];
        auth_hdr(b, "SETUP", setup_uri, ah, sizeof(ah));
        snprintf(req, sizeof(req),
                 "SETUP %s RTSP/1.0\r\nCSeq: %d\r\n"
                 "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n"
                 "Require: www.onvif.org/ver20/backchannel\r\n"
                 "%s%s%s%s\r\n",
                 setup_uri, b->cseq++, ah,
                 b->session[0] ? "Session: " : "", b->session, b->session[0] ? "\r\n" : "");
    }
    st = rtsp_txn(b, req, resp, sizeof(resp));
    if (st / 100 != 2) { close(b->fd); free(b); return -1; }
    {
        char ah[400];
        auth_hdr(b, "PLAY", uri, ah, sizeof(ah));
        snprintf(req, sizeof(req),
                 "PLAY %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\n"
                 "Require: www.onvif.org/ver20/backchannel\r\n%s\r\n",
                 uri, b->cseq++, b->session, ah);
    }
    st = rtsp_txn(b, req, resp, sizeof(resp));
    if (st / 100 != 2) { close(b->fd); free(b); return -1; }
    c->bc = b;
    return 0;
}

static int bc_send_g711(bc_t *b, const uint8_t *pay, int len)
{
    if (!b || b->fd < 0 || len <= 0) return -1;
    uint8_t pkt[12 + 320 + 4];
    int rtp_len = 12 + len;
    pkt[0] = '$'; pkt[1] = (uint8_t)b->rtp_ch;
    pkt[2] = (uint8_t)((rtp_len >> 8) & 0xFF);
    pkt[3] = (uint8_t)(rtp_len & 0xFF);
    pkt[4] = 0x80;
    pkt[5] = (uint8_t)(b->pt & 0x7F);
    pkt[6] = (uint8_t)((b->seq >> 8) & 0xFF);
    pkt[7] = (uint8_t)(b->seq & 0xFF);
    pkt[8] = (uint8_t)((b->ts >> 24) & 0xFF);
    pkt[9] = (uint8_t)((b->ts >> 16) & 0xFF);
    pkt[10] = (uint8_t)((b->ts >> 8) & 0xFF);
    pkt[11] = (uint8_t)(b->ts & 0xFF);
    pkt[12] = (uint8_t)((b->ssrc >> 24) & 0xFF);
    pkt[13] = (uint8_t)((b->ssrc >> 16) & 0xFF);
    pkt[14] = (uint8_t)((b->ssrc >> 8) & 0xFF);
    pkt[15] = (uint8_t)(b->ssrc & 0xFF);
    memcpy(pkt + 16, pay, (size_t)len);
    b->seq++;
    b->ts += (uint32_t)len;   /* 8kHz, 1 sample per octet */
    return sock_sendall(b->fd, pkt, (size_t)(4 + rtp_len));
}

static int ch_ensure_cam(talk_ch_t *c)
{
    if (c->backend == NVR_BACKEND_NOP) {
        if (c->cam_fd >= 0) return 0;
        return nop_open(c);
    }
    if (c->bc) return 0;
    return bc_open(c);
}

static void ch_close_cam(talk_ch_t *c)
{
    if (c->cam_fd >= 0) { close(c->cam_fd); c->cam_fd = -1; }
    if (c->bc) {
        if (c->bc->fd >= 0) close(c->bc->fd);
        free(c->bc);
        c->bc = NULL;
    }
}

static int to_g711(int codec, int want_alaw, const uint8_t *in, int in_len,
                   uint8_t *out, int out_cap)
{
    if (codec == CODEC_PCM) {
        int ns = in_len / 2;
        if (ns > out_cap) ns = out_cap;
        const int16_t *s = (const int16_t *)in;
        for (int i = 0; i < ns; i++)
            out[i] = want_alaw ? lin2alaw(s[i]) : lin2ulaw(s[i]);
        return ns;
    }
    int n = in_len < out_cap ? in_len : out_cap;
    memcpy(out, in, (size_t)n);
    return n;
}

static int fwd_audio(talk_ch_t *c, const uint8_t *data, int len)
{
    if (ch_ensure_cam(c) != 0) return -1;
    int want_a = (c->bc && c->bc->pt == 8) || c->codec == CODEC_A;
    uint8_t g711[2048];
    int n = to_g711(c->codec, want_a, data, len, g711, (int)sizeof(g711));
    if (n <= 0) return 0;
    if (c->backend == NVR_BACKEND_NOP)
        return sock_sendall(c->cam_fd, g711, (size_t)n);
    /* RTP 20ms 切片 */
    int off = 0;
    while (off < n) {
        int chunk = n - off;
        if (chunk > TALK_G711_PT) chunk = TALK_G711_PT;
        if (bc_send_g711(c->bc, g711 + off, chunk) != 0) return -1;
        off += chunk;
    }
    return 0;
}

typedef struct { nvr_talk_t *t; int fd; } cli_arg_t;

/* URI: /speaker/chN 、/chN ；/cgi-bin/speaker、/speaker、/tutkmty/speaker 用 last_chn。 */
static int talk_uri_chn(const char *uri)
{
    const char *p;
    if (!uri || !uri[0]) return -1;
    p = strstr(uri, "/ch");
    if (p && p[3] >= '1' && p[3] <= '9')
        return atoi(p + 3) - 1;
    return -1;
}

static void talk_sess_clear(nvr_talk_t *t, int fd)
{
    pthread_mutex_lock(&t->lock);
    if (t->sess_fd == fd) t->sess_fd = -1;
    pthread_mutex_unlock(&t->lock);
}

static void *client_thread(void *arg)
{
    cli_arg_t *ca = arg;
    nvr_talk_t *t = ca->t;
    int fd = ca->fd;
    free(ca);

    {
        struct timeval tv = { TALK_IDLE_S, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    char buf[4096];
    size_t n = 0;
    int hdr_done = 0, chn = -1;
    while (n < sizeof(buf) - 1 && !hdr_done) {
        ssize_t r = recv(fd, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) { talk_sess_clear(t, fd); close(fd); return NULL; }
        n += (size_t)r;
        buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) hdr_done = 1;
        if (n > 4 && buf[0] != 'G' && buf[0] != 'P' && buf[0] != 'p') {
            hdr_done = 2; /* 裸流，无 HTTP 头 */
            break;
        }
    }
    const uint8_t *audio = (const uint8_t *)buf;
    int audio_len = (int)n;
    if (hdr_done == 1) {
        char uri[256] = {0};
        sscanf(buf, "%*s %255s", uri);
        chn = talk_uri_chn(uri);
        char *body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            audio = (const uint8_t *)body;
            audio_len = (int)(n - (size_t)(body - buf));
        }
        /* 文档：收到 POST 后不回 HTTP，保持 TCP 等音频。 */
    }
    pthread_mutex_lock(&t->lock);
    if (chn < 0 || chn >= TALK_MAX_CH || !t->ch[chn].active)
        chn = t->last_chn;
    talk_ch_t local = {0};
    if (chn >= 0 && chn < TALK_MAX_CH && t->ch[chn].active)
        local = t->ch[chn];
    pthread_mutex_unlock(&t->lock);
    if (!local.active) { talk_sess_clear(t, fd); close(fd); return NULL; }
    local.cam_fd = -1; local.bc = NULL;

    if (audio_len > 0) fwd_audio(&local, audio, audio_len);
    for (;;) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        if (fwd_audio(&local, (const uint8_t *)buf, (int)r) != 0) break;
    }
    ch_close_cam(&local);
    talk_sess_clear(t, fd);
    close(fd);
    return NULL;
}

static void *listen_thread(void *arg)
{
    nvr_talk_t *t = arg;
    while (t->run) {
        fd_set rf; struct timeval tv = { 0, 200000 };
        FD_ZERO(&rf); FD_SET(t->listen_fd, &rf);
        if (select(t->listen_fd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
        int cfd = accept(t->listen_fd, NULL, NULL);
        if (cfd < 0) continue;
        pthread_mutex_lock(&t->lock);
        if (t->sess_fd >= 0) {          /* 已有会话：拒新连接 */
            pthread_mutex_unlock(&t->lock);
            close(cfd);
            continue;
        }
        t->sess_fd = cfd;
        pthread_mutex_unlock(&t->lock);
        cli_arg_t *ca = malloc(sizeof(*ca));
        if (!ca) { talk_sess_clear(t, cfd); close(cfd); continue; }
        ca->t = t; ca->fd = cfd;
        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, ca) != 0) {
            talk_sess_clear(t, cfd);
            close(cfd); free(ca); continue;
        }
        pthread_detach(th);
    }
    return NULL;
}

int nvr_talk_init(int listen_port, nvr_talk_t **out)
{
    if (!out) return -1;
    nvr_talk_t *t = calloc(1, sizeof(*t));
    if (!t) return -1;
    t->listen_port = listen_port > 0 ? listen_port : NVR_TALK_PORT;
    t->last_chn = -1;
    t->sess_fd = -1;
    for (int i = 0; i < TALK_MAX_CH; i++) t->ch[i].cam_fd = -1;
    t->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (t->listen_fd < 0) { free(t); return -1; }
    int one = 1;
    setsockopt(t->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)t->listen_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 仅本机；外网走 TUTK 映射 */
    if (bind(t->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(t->listen_fd, 8) != 0) {
        close(t->listen_fd); free(t); return -1;
    }
    pthread_mutex_init(&t->lock, NULL);
    t->run = 1;
    if (pthread_create(&t->tid, NULL, listen_thread, t) != 0) {
        t->run = 0; close(t->listen_fd); pthread_mutex_destroy(&t->lock); free(t); return -1;
    }
    NVR_LOGI("talk", "对讲收听 127.0.0.1:%d (TUTK 映射 iotc-tunnel:%d)", t->listen_port, t->listen_port);
    *out = t;
    return 0;
}

void nvr_talk_deinit(nvr_talk_t *t)
{
    if (!t) return;
    t->run = 0;
    shutdown(t->listen_fd, SHUT_RDWR);
    close(t->listen_fd);
    pthread_join(t->tid, NULL);
    pthread_mutex_lock(&t->lock);
    for (int i = 0; i < TALK_MAX_CH; i++) {
        ch_close_cam(&t->ch[i]);
        t->ch[i].active = 0;
    }
    pthread_mutex_unlock(&t->lock);
    pthread_mutex_destroy(&t->lock);
    free(t);
}

int nvr_talk_start(nvr_talk_t *t, int chn0, int backend,
                   const char *ip, int onvif_port,
                   const char *user, const char *pass, const char *vsrc,
                   const char *codec, char *url_out, int url_cap)
{
    if (!t || chn0 < 0 || chn0 >= TALK_MAX_CH || !ip || !ip[0]) return -1;
    pthread_mutex_lock(&t->lock);
    talk_ch_t *c = &t->ch[chn0];
    ch_close_cam(c);
    memset(c, 0, sizeof(*c));
    c->cam_fd = -1;
    c->active = 1;
    c->chn = chn0;
    c->owner = t;
    c->backend = backend;
    c->onvif_port = onvif_port > 0 ? onvif_port : 80;
    c->codec = codec_of(codec);
    snprintf(c->ip, sizeof(c->ip), "%s", ip);
    if (user) snprintf(c->user, sizeof(c->user), "%s", user);
    if (pass) snprintf(c->pass, sizeof(c->pass), "%s", pass);
    if (vsrc) snprintf(c->vsrc, sizeof(c->vsrc), "%s", vsrc);
    t->last_chn = chn0;
    pthread_mutex_unlock(&t->lock);
    if (url_out && url_cap > 0)
        snprintf(url_out, url_cap, "tcp://iotc-tunnel:%d/speaker", t->listen_port);
    NVR_LOGI("talk", "ch%d start backend=%s ip=%s codec=%s",
             chn0, backend == NVR_BACKEND_NOP ? "NOP:7000" : "ONVIF-BC", ip,
             codec && codec[0] ? codec : "g711u");
    return 0;
}

void nvr_talk_set_enh_cb(nvr_talk_t *t, nvr_talk_enh_fn fn, void *ud)
{
    if (!t) return;
    t->on_enh = fn;
    t->on_enh_ud = ud;
}

int nvr_talk_stop(nvr_talk_t *t, int chn0)
{
    if (!t || chn0 < 0 || chn0 >= TALK_MAX_CH) return -1;
    pthread_mutex_lock(&t->lock);
    talk_ch_t *c = &t->ch[chn0];
    ch_close_cam(c);
    c->active = 0;
    if (t->last_chn == chn0) t->last_chn = -1;
    pthread_mutex_unlock(&t->lock);
    return 0;
}
