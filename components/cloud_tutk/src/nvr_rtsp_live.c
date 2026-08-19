/***************************************************************************************
 *  nvr_rtsp_live.c — 隧道 RTSP:直播 + 回放(interleaved TCP)
 *
 *  契约(APP_client):
 *    URL  host 必须是 iotc-tunnel;直播 /ch{N}_{stream}.264,回放 /playback/<unix>
 *    SET_PARAMETER body: playback_ctrl: seek + year/month/day/hour/min/sec (UTC)
 *    Seek 后立刻停推并清缓冲,等 PLAY / playback_ctrl:play 再推
 *    每个 RTP 带拓展头 playbackStatus(1真/0空白) + playbackTimestamp(UTC,空白=0)
 *    无录像:同编码黑/静音空白帧 2fps,不自主 Seek
 ***************************************************************************************/
#define _GNU_SOURCE
#include "nvr_rtsp_live.h"
#include "rsdk.h"
#include "nvr_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define RTSP_MAX_SESS  4
#define RTSP_RBUF      16384
#define RTP_MTU        1400
#define RTP_HDR        12
#define RTP_EXT_WORDS  4          /* 16B AVTECH section */
#define RTP_EXT_BYTES  (4 + RTP_EXT_WORDS * 4)
#define PB_MAX_SEGS    512
#define KEY_CACHE_MAX  (256 * 1024)

enum { MODE_LIVE = 0, MODE_PB = 1 };

typedef struct {
    uint8_t *data;
    int      len;
    int      codec;
} key_cache_t;

typedef struct rtsp_sess {
    pthread_mutex_t send_lk;
    volatile int    alive;
    volatile int    playing;       /* PLAY=1; Seek 后清 0,等 PLAY */
    int             fd;
    int             mode;          /* MODE_LIVE / MODE_PB */
    int             chn;           /* 0-based */
    int             stream;        /* 0主/1子 */
    int             want_audio;
    int             want_video;
    int             codec;         /* 0=H264 1=H265 */
    int             v_ch, a_ch;
    uint16_t        vseq, aseq;
    uint32_t        vts, ats, ssrc;
    char            session[32];
    uint32_t        pb_start;
    uint32_t        seek_wall;
    volatile int    seek_pending;
    volatile int    flush;         /* Seek:丢掉未发帧 */
    uint8_t        *key_copy;
    int             key_len, key_codec;
    pthread_t       pb_tid;
    int             has_pb_tid;
    uint8_t         rbuf[RTSP_RBUF];
    int             rlen;
} rtsp_sess_t;

static struct {
    volatile int     run;
    int              port;
    int              listen_fd;
    pthread_t        accept_tid;
    pthread_mutex_t  lock;
    rtsp_sess_t     *sess[RTSP_MAX_SESS];
    struct rsdk_group *group;
    int              want_chn;
    int              want_stream;
    int              want_video;
    int              want_audio;
    key_cache_t      live_key[32][2];
    int              live_codec[32][2];
    struct {
        uint32_t start;
        int      chn;
        int      stream;
        int      want_audio;
        int      want_video;
        int      used;
    } pb_bind[8];
    int              pb_bind_i;
} g;

/* ---------------- 工具 ---------------- */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static int hdr_int(const char *req, const char *key, int def)
{
    const char *p = req;
    size_t klen = strlen(key);
    while (p && *p) {
        if (strncasecmp(p, key, klen) == 0) {
            p += klen;
            while (*p == ' ' || *p == '\t') p++;
            return atoi(p);
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return def;
}

static void hdr_str(const char *req, const char *key, char *out, int cap)
{
    out[0] = 0;
    const char *p = req;
    size_t klen = strlen(key);
    while (p && *p) {
        if (strncasecmp(p, key, klen) == 0) {
            p += klen;
            while (*p == ' ' || *p == '\t') p++;
            int n = 0;
            while (*p && *p != '\r' && *p != '\n' && n < cap - 1) out[n++] = *p++;
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

static const char *req_url(const char *req)
{
    const char *p = strchr(req, ' ');
    return p ? p + 1 : req;
}

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int sess_send(rtsp_sess_t *s, const void *buf, size_t len)
{
    if (!s || s->fd < 0) return -1;
    pthread_mutex_lock(&s->send_lk);
    int rc = (s->fd >= 0) ? send_all(s->fd, buf, len) : -1;
    pthread_mutex_unlock(&s->send_lk);
    return rc;
}

static void rtsp_reply(rtsp_sess_t *s, int cseq, int code, const char *reason,
                       const char *extra, const char *body)
{
    char head[768];
    int bl = body ? (int)strlen(body) : 0;
    int n = snprintf(head, sizeof(head),
                     "RTSP/1.0 %d %s\r\n"
                     "CSeq: %d\r\n"
                     "Session: %s\r\n"
                     "%s"
                     "Content-Length: %d\r\n\r\n",
                     code, reason, cseq, s->session[0] ? s->session : "1",
                     extra ? extra : "", bl);
    if (n > 0) sess_send(s, head, (size_t)n);
    if (body && bl > 0) sess_send(s, body, (size_t)bl);
}

/* RTP X-bit + AVTECH section: FFFE000C | 0F23 | status(4) | utc(4) */
static int send_rtp(rtsp_sess_t *s, int chan, int pt, int marker,
                    uint16_t *seq, uint32_t ts,
                    const uint8_t *pay, int pay_len,
                    int status, uint32_t utc)
{
    if (!s || s->fd < 0 || !pay || pay_len <= 0) return -1;
    if (s->flush) return 0;

    uint8_t pkt[4 + RTP_HDR + RTP_EXT_BYTES + RTP_MTU];
    int hdr = RTP_HDR + RTP_EXT_BYTES;
    int max_pay = RTP_MTU - hdr;
    if (max_pay < 64) max_pay = 64;
    int ncopy = pay_len;
    if (ncopy > max_pay) ncopy = max_pay; /* 调用方应 FU-A 切好 */

    pkt[0] = '$';
    pkt[1] = (uint8_t)chan;
    uint16_t rlen = (uint16_t)(hdr + ncopy);
    pkt[2] = (uint8_t)(rlen >> 8);
    pkt[3] = (uint8_t)(rlen & 0xff);

    uint8_t *h = pkt + 4;
    h[0] = 0x90; /* V=2 X=1 */
    h[1] = (uint8_t)((marker ? 0x80 : 0) | (pt & 0x7f));
    h[2] = (uint8_t)(*seq >> 8);
    h[3] = (uint8_t)(*seq & 0xff);
    (*seq)++;
    h[4] = (uint8_t)(ts >> 24);
    h[5] = (uint8_t)(ts >> 16);
    h[6] = (uint8_t)(ts >> 8);
    h[7] = (uint8_t)ts;
    h[8]  = (uint8_t)(s->ssrc >> 24);
    h[9]  = (uint8_t)(s->ssrc >> 16);
    h[10] = (uint8_t)(s->ssrc >> 8);
    h[11] = (uint8_t)s->ssrc;
    /* RFC 3550 ext hdr: profile 0, length=4 words */
    h[12] = 0; h[13] = 0; h[14] = 0; h[15] = RTP_EXT_WORDS;
    h[16] = 0xFF; h[17] = 0xFE; h[18] = 0x00; h[19] = 0x0C;
    h[20] = 0x0F; h[21] = 0x23;
    h[22] = (uint8_t)(status >> 24);
    h[23] = (uint8_t)(status >> 16);
    h[24] = (uint8_t)(status >> 8);
    h[25] = (uint8_t)status;
    uint32_t ut = status ? utc : 0;
    h[26] = (uint8_t)(ut >> 24);
    h[27] = (uint8_t)(ut >> 16);
    h[28] = (uint8_t)(ut >> 8);
    h[29] = (uint8_t)ut;
    h[30] = 0; h[31] = 0;
    memcpy(h + RTP_EXT_BYTES, pay, (size_t)ncopy);
    return sess_send(s, pkt, (size_t)(4 + hdr + ncopy));
}

static int send_h264_nal(rtsp_sess_t *s, const uint8_t *nal, int nal_len,
                         int marker, uint32_t ts, int status, uint32_t utc)
{
    const int pt = 96;
    int overhead = RTP_HDR + RTP_EXT_BYTES;
    int maxp = RTP_MTU - overhead;
    if (maxp < 64) maxp = 64;
    if (nal_len + 1 <= maxp)
        return send_rtp(s, s->v_ch, pt, marker, &s->vseq, ts, nal, nal_len, status, utc);

    /* FU-A */
    uint8_t ind = (uint8_t)((nal[0] & 0xE0) | 28);
    uint8_t ntype = (uint8_t)(nal[0] & 0x1F);
    int off = 1;
    int first = 1;
    while (off < nal_len) {
        int chunk = nal_len - off;
        if (chunk > maxp - 2) chunk = maxp - 2;
        int last = (off + chunk >= nal_len);
        uint8_t fu[2 + RTP_MTU];
        fu[0] = ind;
        fu[1] = (uint8_t)((first ? 0x80 : 0) | (last ? 0x40 : 0) | ntype);
        memcpy(fu + 2, nal + off, (size_t)chunk);
        if (send_rtp(s, s->v_ch, pt, marker && last, &s->vseq, ts,
                     fu, chunk + 2, status, utc) != 0)
            return -1;
        off += chunk;
        first = 0;
    }
    return 0;
}

static int send_h265_nal(rtsp_sess_t *s, const uint8_t *nal, int nal_len,
                         int marker, uint32_t ts, int status, uint32_t utc)
{
    const int pt = 96;
    int overhead = RTP_HDR + RTP_EXT_BYTES;
    int maxp = RTP_MTU - overhead;
    if (nal_len < 2) return 0;
    if (nal_len + 1 <= maxp)
        return send_rtp(s, s->v_ch, pt, marker, &s->vseq, ts, nal, nal_len, status, utc);

    uint8_t ntype = (uint8_t)((nal[0] >> 1) & 0x3F);
    uint8_t fu[3 + RTP_MTU];
    fu[0] = (uint8_t)((nal[0] & 0x81) | (49 << 1));
    fu[1] = nal[1];
    int off = 2, first = 1;
    while (off < nal_len) {
        int chunk = nal_len - off;
        if (chunk > maxp - 3) chunk = maxp - 3;
        int last = (off + chunk >= nal_len);
        fu[2] = (uint8_t)((first ? 0x80 : 0) | (last ? 0x40 : 0) | ntype);
        memcpy(fu + 3, nal + off, (size_t)chunk);
        if (send_rtp(s, s->v_ch, pt, marker && last, &s->vseq, ts,
                     fu, chunk + 3, status, utc) != 0)
            return -1;
        off += chunk;
        first = 0;
    }
    return 0;
}

/* Annex-B → 逐 NAL RTP。最后一枚 VCL 打 marker。 */
static int send_annexb(rtsp_sess_t *s, const uint8_t *data, int len,
                       int codec, uint32_t ts, int status, uint32_t utc)
{
    if (!data || len <= 4) return 0;
    const uint8_t *nal[64];
    int nlen[64], nn = 0;
    int i = 0;
    while (i < len - 3 && nn < 63) {
        int sc = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) sc = 3;
        else if (i + 3 < len && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1) sc = 4;
        if (!sc) { i++; continue; }
        int start = i + sc;
        int j = start;
        while (j < len - 3) {
            if (data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 1) break;
            if (j + 3 < len && data[j] == 0 && data[j + 1] == 0 &&
                data[j + 2] == 0 && data[j + 3] == 1) break;
            j++;
        }
        if (j >= len - 3) j = len;
        int l = j - start;
        if (l > 0) { nal[nn] = data + start; nlen[nn] = l; nn++; }
        i = j;
    }
    for (int k = 0; k < nn; k++) {
        int last = (k == nn - 1);
        int rc = (codec == 1)
            ? send_h265_nal(s, nal[k], nlen[k], last, ts, status, utc)
            : send_h264_nal(s, nal[k], nlen[k], last, ts, status, utc);
        if (rc != 0) return rc;
    }
    return 0;
}

static int send_aac(rtsp_sess_t *s, const uint8_t *data, int len,
                    uint32_t ts, int status, uint32_t utc)
{
    if (!data || len <= 0 || !s->want_audio) return 0;
    const uint8_t *p = data;
    int n = len;
    if (n >= 7 && p[0] == 0xFF && (p[1] & 0xF0) == 0xF0) {
        int hdr = (p[1] & 0x01) ? 7 : 9;
        if (n > hdr) { p += hdr; n -= hdr; }
    }
    if (n <= 0 || n > 1500) return 0;
    uint8_t au[4 + 1500];
    au[0] = 0x00; au[1] = 0x10; /* AU-headers-length = 16 bits */
    uint16_t h = (uint16_t)((n & 0x1FFF) << 3);
    au[2] = (uint8_t)(h >> 8);
    au[3] = (uint8_t)(h & 0xFF);
    memcpy(au + 4, p, (size_t)n);
    int rc = send_rtp(s, s->a_ch, 97, 1, &s->aseq, ts, au, n + 4, status, utc);
    s->ats += 1024; /* 一帧 AAC */
    return rc;
}

static void cache_key(key_cache_t *k, const uint8_t *data, int len, int codec)
{
    if (!k || !data || len <= 0 || len > KEY_CACHE_MAX) return;
    uint8_t *p = (uint8_t *)realloc(k->data, (size_t)len);
    if (!p) return;
    memcpy(p, data, (size_t)len);
    k->data = p;
    k->len = len;
    k->codec = codec;
}

static void sess_remember_key(rtsp_sess_t *s, const uint8_t *data, int len, int codec)
{
    if (!s || !data || len <= 0 || len > KEY_CACHE_MAX) return;
    uint8_t *p = (uint8_t *)realloc(s->key_copy, (size_t)len);
    if (!p) return;
    memcpy(p, data, (size_t)len);
    s->key_copy = p;
    s->key_len = len;
    s->key_codec = codec;
}

static int send_blank(rtsp_sess_t *s, uint32_t rtp_ts)
{
    const uint8_t *vid = s->key_copy;
    int vlen = s->key_len, codec = s->key_codec;
    if (!vid || vlen <= 0) {
        int ch = s->chn, st = s->stream;
        if (ch >= 0 && ch < 32) {
            pthread_mutex_lock(&g.lock);
            key_cache_t *k = &g.live_key[ch][st < 0 ? 0 : st];
            if ((!k->data || k->len <= 0) && st != 0) k = &g.live_key[ch][0];
            if (k->data && k->len > 0)
                sess_remember_key(s, k->data, k->len, k->codec);
            pthread_mutex_unlock(&g.lock);
            vid = s->key_copy;
            vlen = s->key_len;
            codec = s->key_codec;
        }
    }
    if (s->want_video && vid && vlen > 0)
        send_annexb(s, vid, vlen, codec, rtp_ts, 0, 0);
    if (s->want_audio) {
        static const uint8_t silent[6] = { 0x21, 0x10, 0x04, 0x60, 0x8C, 0x1C };
        send_aac(s, silent, (int)sizeof(silent), s->ats, 0, 0);
    }
    return 0;
}

/* ---------------- URL / bind ---------------- */

static void parse_url(const char *req, int *chn, int *stream, int *is_pb, uint32_t *pb_ts)
{
    const char *u = req_url(req);
    const char *p;
    *is_pb = 0;
    if ((p = strstr(u, "/playback/")) != NULL) {
        *is_pb = 1;
        *pb_ts = (uint32_t)strtoul(p + 10, NULL, 10);
        return;
    }
    if ((p = strstr(u, "/live/ch")) != NULL) {
        int n = atoi(p + 8);
        if (n >= 1) *chn = n - 1;
        const char *us = strchr(p + 8, '_');
        if (us && us[1] >= '0' && us[1] <= '9') *stream = atoi(us + 1) ? 1 : 0;
        return;
    }
    if ((p = strstr(u, "/ch")) != NULL && p[3] >= '0' && p[3] <= '9') {
        int idx = atoi(p + 3);
        *chn = idx;
        const char *us = strchr(p + 3, '_');
        if (us && strncmp(us, "_audio", 6) == 0) {
            *stream = 0;
        } else if (us && us[1] >= '0' && us[1] <= '9') {
            *stream = atoi(us + 1) ? 1 : 0;
        }
    }
}

static void lookup_pb_bind(uint32_t start, int *chn, int *stream, int *want_audio, int *want_video)
{
    for (int i = 0; i < 8; i++) {
        if (g.pb_bind[i].used && g.pb_bind[i].start == start) {
            *chn = g.pb_bind[i].chn;
            *stream = g.pb_bind[i].stream;
            if (want_audio) *want_audio = g.pb_bind[i].want_audio;
            if (want_video) *want_video = g.pb_bind[i].want_video;
            return;
        }
    }
    /* 最近一次 startPlayback */
    int last = (g.pb_bind_i + 7) % 8;
    if (g.pb_bind[last].used) {
        *chn = g.pb_bind[last].chn;
        *stream = g.pb_bind[last].stream;
        if (want_audio) *want_audio = g.pb_bind[last].want_audio;
        if (want_video) *want_video = g.pb_bind[last].want_video;
    }
}

/* 重叠段剩余秒;无段/无盘=0(不写死 30)。 */
static uint32_t pb_duration_of(struct rsdk_group *grp, int chn, uint32_t start)
{
    uint32_t dur = 0;
    if (!grp) return 0;
    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(*segs) * 64);
    if (!segs) return 0;
    int n = rsdk_group_query(grp, start, start + 3600, chn, -1, segs, 64);
    for (int i = 0; i < n; i++) {
        uint32_t t0 = segs[i].start_time;
        uint32_t t1 = segs[i].end_time;
        if (t1 == 0 || t1 == 0xFFFFFFFFu) continue;
        if (t0 <= start && start < t1) {
            dur = t1 - start;
            break;
        }
    }
    free(segs);
    return dur;
}

void nvr_rtsp_live_select(int chn)
{
    g.want_chn = chn;
}

void nvr_rtsp_live_select_ex(int chn, int stream)
{
    nvr_rtsp_live_select_media(chn, stream, 1, 1);
}

void nvr_rtsp_live_select_media(int chn, int stream, int want_video, int want_audio)
{
    g.want_chn = chn;
    g.want_stream = stream;
    g.want_video = want_video ? 1 : 0;
    g.want_audio = want_audio ? 1 : 0;
}

int nvr_rtsp_live_port(void)
{
    return g.run ? g.port : 0;
}

void nvr_rtsp_live_set_group(struct rsdk_group *group)
{
    if (!g.run) {
        g.group = group;
        return;
    }
    pthread_mutex_lock(&g.lock);
    g.group = group;
    pthread_mutex_unlock(&g.lock);
}

int nvr_rtsp_pb_prepare(int chn, uint32_t start_utc, int stream, int want_audio, int want_video,
                        uint32_t *duration_out)
{
    pthread_mutex_lock(&g.lock);
    int i = g.pb_bind_i % 8;
    g.pb_bind[i].start = start_utc;
    g.pb_bind[i].chn = chn;
    g.pb_bind[i].stream = (stream ? 1 : 0);
    g.pb_bind[i].want_audio = want_audio ? 1 : 0;
    g.pb_bind[i].want_video = want_video ? 1 : 0;
    g.pb_bind[i].used = 1;
    g.pb_bind_i = i + 1;
    struct rsdk_group *grp = g.group;
    pthread_mutex_unlock(&g.lock);
    uint32_t d = pb_duration_of(grp, chn, start_utc);
    if (duration_out) *duration_out = d;
    return 0;
}

void nvr_rtsp_pb_stop(void)
{
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < RTSP_MAX_SESS; i++) {
        rtsp_sess_t *s = g.sess[i];
        if (s && s->mode == MODE_PB) {
            s->playing = 0;
            s->alive = 0;
            if (s->fd >= 0) shutdown(s->fd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&g.lock);
}

/* ---------------- 读 RTSP 报文(跳过 interleaved) ---------------- */

static int sess_fill(rtsp_sess_t *s, int need)
{
    while (s->rlen < need) {
        if (s->rlen >= RTSP_RBUF - 1) return -1;
        ssize_t n = recv(s->fd, s->rbuf + s->rlen, (size_t)(RTSP_RBUF - 1 - s->rlen), 0);
        if (n <= 0) return -1;
        s->rlen += (int)n;
    }
    return 0;
}

static void sess_consume(rtsp_sess_t *s, int n)
{
    if (n <= 0 || n > s->rlen) { s->rlen = 0; return; }
    memmove(s->rbuf, s->rbuf + n, (size_t)(s->rlen - n));
    s->rlen -= n;
}

static int sess_read_msg(rtsp_sess_t *s, char *out, int cap)
{
    for (;;) {
        if (sess_fill(s, 1) != 0) return -1;
        while (s->rlen >= 1 && s->rbuf[0] == '$') {
            if (sess_fill(s, 4) != 0) return -1;
            int plen = ((int)s->rbuf[2] << 8) | s->rbuf[3];
            if (plen < 0 || plen > 8192) return -1;
            if (sess_fill(s, 4 + plen) != 0) return -1;
            sess_consume(s, 4 + plen);
            if (s->rlen < 1 && sess_fill(s, 1) != 0) return -1;
        }
        /* 找头结束 */
        int hdr_end = -1;
        for (int i = 0; i + 3 < s->rlen; i++) {
            if (s->rbuf[i] == '\r' && s->rbuf[i + 1] == '\n' &&
                s->rbuf[i + 2] == '\r' && s->rbuf[i + 3] == '\n') {
                hdr_end = i + 4;
                break;
            }
        }
        if (hdr_end < 0) {
            if (sess_fill(s, s->rlen + 1) != 0) return -1;
            continue;
        }
        int clen = 0;
        for (int i = 0; i + 15 < hdr_end; i++) {
            if (strncasecmp((char *)s->rbuf + i, "Content-Length:", 15) == 0 ||
                strncasecmp((char *)s->rbuf + i, "Content-length:", 15) == 0) {
                clen = atoi((char *)s->rbuf + i + 15);
                break;
            }
        }
        if (clen < 0) clen = 0;
        if (clen > 4096) clen = 4096;
        if (sess_fill(s, hdr_end + clen) != 0) return -1;
        int total = hdr_end + clen;
        if (total >= cap) total = cap - 1;
        memcpy(out, s->rbuf, (size_t)total);
        out[total] = 0;
        sess_consume(s, hdr_end + clen);
        return total;
    }
}

static uint32_t parse_seek_utc(const char *msg)
{
    const char *body = strstr(msg, "\r\n\r\n");
    if (!body) return 0;
    body += 4;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    const char *p;
    if ((p = strstr(body, "year:")) != NULL) y = atoi(p + 5);
    if ((p = strstr(body, "month:")) != NULL) mo = atoi(p + 6);
    if ((p = strstr(body, "day:")) != NULL) d = atoi(p + 4);
    if ((p = strstr(body, "hour:")) != NULL) h = atoi(p + 5);
    if ((p = strstr(body, "min:")) != NULL) mi = atoi(p + 4);
    if ((p = strstr(body, "sec:")) != NULL) sec = atoi(p + 4);
    if (y >= 1970 && mo >= 1 && d >= 1) {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = y - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = sec;
        time_t u = timegm(&t);
        if (u > 0) return (uint32_t)u;
    }
    /* 兜底:Seek: unix 或 YYYYMMDDHHMMSS */
    if ((p = strstr(body, "Seek:")) != NULL || (p = strstr(body, "seek:")) != NULL) {
        p += 5;
        while (*p == ' ') p++;
        if (strlen(p) >= 14 && p[0] == '2') {
            char buf[16];
            memcpy(buf, p, 14); buf[14] = 0;
            int Y, M, D, H, MI, S;
            if (sscanf(buf, "%4d%2d%2d%2d%2d%2d", &Y, &M, &D, &H, &MI, &S) == 6) {
                struct tm t; memset(&t, 0, sizeof(t));
                t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
                t.tm_hour = H; t.tm_min = MI; t.tm_sec = S;
                time_t u = timegm(&t);
                if (u > 0) return (uint32_t)u;
            }
        }
        return (uint32_t)strtoul(p, NULL, 10);
    }
    return 0;
}

static int parse_interleaved(const char *req, int def_lo)
{
    const char *p = strstr(req, "interleaved=");
    if (!p) return def_lo;
    return atoi(p + 12);
}

static void ensure_session(rtsp_sess_t *s, const char *req)
{
    char got[32];
    hdr_str(req, "Session:", got, (int)sizeof(got));
    if (got[0]) {
        strncpy(s->session, got, sizeof(s->session) - 1);
        s->session[sizeof(s->session) - 1] = 0;
        /* 去掉可能的 ;timeout= */
        char *sc = strchr(s->session, ';');
        if (sc) *sc = 0;
        return;
    }
    if (!s->session[0])
        snprintf(s->session, sizeof(s->session), "%u",
                 (unsigned)((uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)s));
}

static void build_sdp(rtsp_sess_t *s, char *sdp, int cap)
{
    int hevc = (s->codec == 1);
    int n = snprintf(sdp, (size_t)cap,
                     "v=0\r\n"
                     "o=- 0 0 IN IP4 127.0.0.1\r\n"
                     "s=%s\r\n"
                     "c=IN IP4 0.0.0.0\r\n"
                     "t=0 0\r\n"
                     "a=control:*\r\n"
                     "a=range:npt=now-\r\n",
                     s->mode == MODE_PB ? "NVR Playback" : "NVR Live");
    if (n < 0) n = 0;
    if (s->want_video && n < cap)
        n += snprintf(sdp + n, (size_t)(cap - n),
                      "m=video 0 RTP/AVP 96\r\n"
                      "a=rtpmap:96 %s/90000\r\n"
                      "a=control:track=v\r\n",
                      hevc ? "H265" : "H264");
    if (s->want_audio && n < cap)
        n += snprintf(sdp + n, (size_t)(cap - n),
                      "m=audio 0 RTP/AVP 97\r\n"
                      "a=rtpmap:97 MPEG4-GENERIC/8000/1\r\n"
                      "a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;"
                      "sizelength=13;indexlength=3;indexdeltalength=3\r\n"
                      "a=control:track=a\r\n");
    (void)n;
}

/* ---------------- 回放线程 ---------------- */

static int pb_open_players(rtsp_sess_t *s, uint32_t wall,
                           rsdk_group_player_t **gp, rsdk_group_player_t **gp_au)
{
    if (*gp) { rsdk_group_play_close(*gp); *gp = NULL; }
    if (*gp_au) { rsdk_group_play_close(*gp_au); *gp_au = NULL; }
    struct rsdk_group *grp;
    pthread_mutex_lock(&g.lock);
    grp = g.group;
    pthread_mutex_unlock(&g.lock);
    if (!grp) return -1;

    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(*segs) * PB_MAX_SEGS);
    if (!segs) return -1;
    uint32_t t1 = wall + 48u * 3600u;
    int want = s->stream;
    int n = rsdk_group_query_stream(grp, wall, t1, s->chn, -1, want, segs, PB_MAX_SEGS);
    if (n <= 0 && want == 1)
        n = rsdk_group_query_stream(grp, wall, t1, s->chn, -1, -1, segs, PB_MAX_SEGS);
    if (n > 0) rsdk_group_play_open(grp, segs, n, gp);

    if (s->want_audio && s->stream != 0) {
        int na = rsdk_group_query_stream(grp, wall, t1, s->chn, -1, 0, segs, PB_MAX_SEGS);
        if (na > 0) rsdk_group_play_open(grp, segs, na, gp_au);
    }
    free(segs);
    return (*gp || *gp_au) ? 0 : -1;
}

static void *pb_thread(void *arg)
{
    rtsp_sess_t *s = (rtsp_sess_t *)arg;
    rsdk_group_player_t *gp = NULL, *gp_au = NULL;
    rsdk_frame_hdr_t hold;
    const uint8_t *hold_data = NULL;
    uint32_t hold_len = 0;
    int have_hold = 0, disk = 0, gap = 0;
    uint32_t virt = s->pb_start;
    uint64_t orig_ms = now_ms();
    uint32_t orig_wall = virt;
    uint64_t last_blank_ms = 0;
    uint64_t pts0 = 0;
    uint32_t last_fw = 0;
    uint64_t last_send_ms = 0;

    pb_open_players(s, virt, &gp, &gp_au);

    while (s->alive) {
        if (s->seek_pending) {
            s->flush = 1;
            s->playing = 0;
            virt = s->seek_wall ? s->seek_wall : virt;
            orig_wall = virt;
            orig_ms = now_ms();
            have_hold = 0;
            last_blank_ms = 0;
            pts0 = 0;
            last_fw = 0;
            pb_open_players(s, virt, &gp, &gp_au);
            s->seek_pending = 0;
            s->flush = 0;
            NVR_LOGI("rtsp", "seek ch%d -> %u, pause until PLAY", s->chn, virt);
        }
        if (!s->playing) {
            usleep(20000);
            continue;
        }

        virt = orig_wall + (uint32_t)((now_ms() - orig_ms) / 1000ull);

        while (!have_hold && gp) {
            if (rsdk_group_play_next2(gp, &hold, &hold_data, &hold_len, &disk, &gap) != RSDK_OK) {
                rsdk_group_play_close(gp); gp = NULL;
                break;
            }
            (void)disk; (void)gap;
            if (hold.rec_kind != RSDK_RK_FRAME) continue;
            if (hold.frame_type == RSDK_FRAME_AUDIO) {
                if (s->want_audio && s->stream == 0 &&
                    (uint32_t)hold.wall_time + 1 >= orig_wall)
                    send_aac(s, hold_data, (int)hold_len, s->ats, 1,
                             (uint32_t)hold.wall_time);
                continue;
            }
            if ((uint32_t)hold.wall_time + 1 < orig_wall) continue;
            have_hold = 1;
            break;
        }

        uint32_t next_w = have_hold ? (uint32_t)hold.wall_time : 0xFFFFFFFFu;
        if (!have_hold || next_w > virt) {
            uint64_t nms = now_ms();
            if (nms - last_blank_ms >= 500) {
                s->vts += 45000;
                send_blank(s, s->vts);
                last_blank_ms = nms;
            } else {
                usleep(20000);
            }
            continue;
        }

        uint64_t elapsed = now_ms() - orig_ms;
        uint64_t target;
        if (hold.pts && pts0) {
            target = hold.pts - pts0;
            if (target > elapsed + 2000ull) target = elapsed; /* pts 非毫秒则放弃 */
        } else if (hold.pts && !pts0) {
            pts0 = hold.pts;
            orig_ms = now_ms();
            orig_wall = (uint32_t)hold.wall_time;
            target = 0;
        } else if ((uint32_t)hold.wall_time == last_fw && last_send_ms) {
            target = elapsed + 40; /* 同秒内约 25fps */
        } else {
            target = (uint64_t)((uint32_t)hold.wall_time - orig_wall) * 1000ull;
        }
        if (target > elapsed + 8) {
            uint64_t sl = target - elapsed;
            if (sl > 200) sl = 200;
            usleep((useconds_t)(sl * 1000));
        }
        last_fw = (uint32_t)hold.wall_time;
        last_send_ms = now_ms();
        int codec = (hold.codec == RSDK_CODEC_H265) ? 1 : 0;
        s->codec = codec;
        if (hold.frame_type == RSDK_FRAME_I)
            sess_remember_key(s, hold_data, (int)hold_len, codec);
        s->vts = (uint32_t)(hold.pts ? hold.pts * 90ull : s->vts + 3000);
        if (s->want_video)
            send_annexb(s, hold_data, (int)hold_len, codec, s->vts, 1,
                        (uint32_t)hold.wall_time);
        have_hold = 0;

        if (gp_au && s->want_audio && s->stream != 0) {
            for (int gdi = 0; gdi < 16; gdi++) {
                rsdk_frame_hdr_t ah;
                const uint8_t *ad = NULL;
                uint32_t al = 0;
                int d2 = 0, gp2 = 0;
                if (rsdk_group_play_next2(gp_au, &ah, &ad, &al, &d2, &gp2) != RSDK_OK)
                    break;
                if (ah.rec_kind != RSDK_RK_FRAME) continue;
                if (ah.frame_type != RSDK_FRAME_AUDIO) continue;
                if ((uint32_t)ah.wall_time + 1 < (uint32_t)hold.wall_time) continue;
                if ((uint32_t)ah.wall_time > (uint32_t)hold.wall_time + 1) break;
                send_aac(s, ad, (int)al, s->ats, 1, (uint32_t)ah.wall_time);
            }
        }
    }
    if (gp) rsdk_group_play_close(gp);
    if (gp_au) rsdk_group_play_close(gp_au);
    return NULL;
}

/* ---------------- 会话 ---------------- */

static void sess_close(rtsp_sess_t *s)
{
    if (!s) return;
    s->alive = 0;
    s->playing = 0;
    if (s->fd >= 0) {
        shutdown(s->fd, SHUT_RDWR);
        close(s->fd);
        s->fd = -1;
    }
    if (s->has_pb_tid) {
        pthread_join(s->pb_tid, NULL);
        s->has_pb_tid = 0;
    }
    free(s->key_copy);
    s->key_copy = NULL;
    pthread_mutex_destroy(&s->send_lk);
    free(s);
}

static void start_pb_if_needed(rtsp_sess_t *s)
{
    if (s->mode != MODE_PB || s->has_pb_tid) return;
    s->alive = 1;
    if (pthread_create(&s->pb_tid, NULL, pb_thread, s) == 0)
        s->has_pb_tid = 1;
}

static void handle_client(rtsp_sess_t *s)
{
    char req[RTSP_RBUF];
    const char *pub =
        "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, "
        "SET_PARAMETER, GET_PARAMETER\r\n";

    for (;;) {
        if (sess_read_msg(s, req, (int)sizeof(req)) < 0) break;
        int cseq = hdr_int(req, "CSeq:", 1);
        ensure_session(s, req);

        if (strncmp(req, "OPTIONS", 7) == 0) {
            rtsp_reply(s, cseq, 200, "OK", pub, NULL);
        } else if (strncmp(req, "DESCRIBE", 8) == 0) {
            int is_pb = 0;
            uint32_t pb_ts = 0;
            parse_url(req, &s->chn, &s->stream, &is_pb, &pb_ts);
            if (is_pb) {
                int wa = 1, wv = 1;
                s->mode = MODE_PB;
                s->pb_start = pb_ts;
                s->seek_wall = pb_ts;
                lookup_pb_bind(pb_ts, &s->chn, &s->stream, &wa, &wv);
                s->want_audio = wa;
                s->want_video = wv;
            } else {
                s->mode = MODE_LIVE;
                if (s->chn < 0 && g.want_chn >= 0) s->chn = g.want_chn;
                if (s->stream < 0 && g.want_stream >= 0) s->stream = g.want_stream;
                if (s->stream < 0) s->stream = 0;
                s->want_audio = g.want_audio;
                s->want_video = g.want_video;
                if (strstr(req, "_audio")) {
                    s->want_audio = 1;
                    s->want_video = 0;
                    s->stream = 0;
                }
            }
            if (s->chn >= 0 && s->chn < 32)
                s->codec = g.live_codec[s->chn][s->stream < 0 ? 0 : s->stream];
            char sdp[768];
            build_sdp(s, sdp, (int)sizeof(sdp));
            char extra[256];
            snprintf(extra, sizeof(extra),
                     "Content-Type: application/sdp\r\n"
                     "Content-Base: rtsp://127.0.0.1:%d/\r\n", g.port);
            rtsp_reply(s, cseq, 200, "OK", extra, sdp);
        } else if (strncmp(req, "SETUP", 5) == 0) {
            int is_audio = (strstr(req, "track=a") != NULL) ||
                           (strstr(req, "track4") != NULL) ||
                           (strstr(req, "track=2") != NULL) ||
                           (strstr(req, "/track2") != NULL);
            int lo = parse_interleaved(req, is_audio ? 2 : 0);
            if (is_audio) s->a_ch = lo;
            else s->v_ch = lo;
            if (s->ssrc == 0) s->ssrc = (uint32_t)time(NULL) ^ (uint32_t)s->fd;
            char tr[160];
            snprintf(tr, sizeof(tr),
                     "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n",
                     lo, lo + 1);
            rtsp_reply(s, cseq, 200, "OK", tr, NULL);
        } else if (strncmp(req, "PLAY", 4) == 0) {
            s->flush = 0;
            s->playing = 1;
            start_pb_if_needed(s);
            rtsp_reply(s, cseq, 200, "OK", "Range: npt=now-\r\n", NULL);
        } else if (strncmp(req, "PAUSE", 5) == 0) {
            s->playing = 0;
            rtsp_reply(s, cseq, 200, "OK", NULL, NULL);
        } else if (strncmp(req, "TEARDOWN", 8) == 0) {
            s->playing = 0;
            rtsp_reply(s, cseq, 200, "OK", NULL, NULL);
            break;
        } else if (strncmp(req, "GET_PARAMETER", 13) == 0) {
            rtsp_reply(s, cseq, 200, "OK", NULL, NULL);
        } else if (strncmp(req, "SET_PARAMETER", 13) == 0) {
            const char *body = strstr(req, "\r\n\r\n");
            body = body ? body + 4 : "";
            if (strstr(body, "playback_ctrl: seek") || strstr(body, "playback_ctrl:seek")) {
                uint32_t utc = parse_seek_utc(req);
                s->flush = 1;
                s->playing = 0;          /* 停推,等 PLAY */
                if (utc) s->seek_wall = utc;
                s->seek_pending = 1;
                start_pb_if_needed(s);
                NVR_LOGI("rtsp", "SET_PARAMETER seek %u ch%d", utc, s->chn);
            } else if (strstr(body, "playback_ctrl: play") || strstr(body, "playback_ctrl:play")) {
                s->flush = 0;
                s->playing = 1;
                start_pb_if_needed(s);
            }
            rtsp_reply(s, cseq, 200, "OK", NULL, NULL);
        } else {
            rtsp_reply(s, cseq, 501, "Not Implemented", NULL, NULL);
        }
    }
    s->alive = 0;
    s->playing = 0;
}

static void *client_thread(void *arg)
{
    rtsp_sess_t *s = (rtsp_sess_t *)arg;
    handle_client(s);
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < RTSP_MAX_SESS; i++)
        if (g.sess[i] == s) g.sess[i] = NULL;
    pthread_mutex_unlock(&g.lock);
    sess_close(s);
    return NULL;
}

static void *accept_thread(void *arg)
{
    (void)arg;
    while (g.run) {
        struct sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        int fd = accept(g.listen_fd, (struct sockaddr *)&peer, &pl);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (!g.run) break;
            usleep(100000);
            continue;
        }
        rtsp_sess_t *s = (rtsp_sess_t *)calloc(1, sizeof(*s));
        if (!s) { close(fd); continue; }
        pthread_mutex_init(&s->send_lk, NULL);
        s->fd = fd;
        s->alive = 1;
        s->chn = g.want_chn;
        s->stream = g.want_stream;
        s->v_ch = 0;
        s->a_ch = 2;
        s->want_audio = g.want_audio;
        s->want_video = g.want_video;

        pthread_mutex_lock(&g.lock);
        int slot = -1;
        for (int i = 0; i < RTSP_MAX_SESS; i++)
            if (!g.sess[i]) { slot = i; break; }
        if (slot < 0) {
            pthread_mutex_unlock(&g.lock);
            sess_close(s);
            continue;
        }
        g.sess[slot] = s;
        pthread_mutex_unlock(&g.lock);

        pthread_t tid;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &at, client_thread, s) != 0) {
            pthread_mutex_lock(&g.lock);
            if (g.sess[slot] == s) g.sess[slot] = NULL;
            pthread_mutex_unlock(&g.lock);
            sess_close(s);
        }
        pthread_attr_destroy(&at);
    }
    return NULL;
}

int nvr_rtsp_live_start(int port)
{
    if (g.run) return 0;
    if (port <= 0) port = 8554;
    memset(&g, 0, sizeof(g));
    g.want_chn = -1;
    g.want_stream = 0;
    g.want_video = 1;
    g.want_audio = 1;
    g.port = port;
    pthread_mutex_init(&g.lock, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    g.listen_fd = fd;
    g.run = 1;
    if (pthread_create(&g.accept_tid, NULL, accept_thread, NULL) != 0) {
        g.run = 0;
        close(g.listen_fd);
        g.listen_fd = -1;
        return -1;
    }
    NVR_LOGI("rtsp", "listen :%d (live+playback)", port);
    return 0;
}

void nvr_rtsp_live_stop(void)
{
    if (!g.run) return;
    g.run = 0;
    if (g.listen_fd >= 0) {
        shutdown(g.listen_fd, SHUT_RDWR);
        close(g.listen_fd);
        g.listen_fd = -1;
    }
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < RTSP_MAX_SESS; i++) {
        rtsp_sess_t *s = g.sess[i];
        if (s && s->fd >= 0) shutdown(s->fd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.accept_tid, NULL);
    /* client 线程 detach,稍等关闭 */
    usleep(50000);
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < RTSP_MAX_SESS; i++) g.sess[i] = NULL;
    for (int c = 0; c < 32; c++)
        for (int st = 0; st < 2; st++) {
            free(g.live_key[c][st].data);
            g.live_key[c][st].data = NULL;
        }
    pthread_mutex_unlock(&g.lock);
    pthread_mutex_destroy(&g.lock);
}

void nvr_rtsp_live_feed(int chn, int stream, const uint8_t *data, int len,
                        int codec, int is_key, uint32_t ts_ms)
{
    if (!g.run || !data || len <= 0) return;
    if (chn >= 0 && chn < 32 && (stream == 0 || stream == 1)) {
        g.live_codec[chn][stream] = codec;
        if (is_key) {
            pthread_mutex_lock(&g.lock);
            cache_key(&g.live_key[chn][stream], data, len, codec);
            pthread_mutex_unlock(&g.lock);
        }
    }

    pthread_mutex_lock(&g.lock);
    rtsp_sess_t *list[RTSP_MAX_SESS];
    int n = 0;
    for (int i = 0; i < RTSP_MAX_SESS; i++) {
        rtsp_sess_t *s = g.sess[i];
        if (!s || s->mode != MODE_LIVE || !s->playing || s->fd < 0) continue;
        if (s->chn >= 0 && chn != s->chn) continue;
        int want = (s->stream < 0) ? 0 : s->stream;
        if (stream != want) continue;
        list[n++] = s;
    }
    pthread_mutex_unlock(&g.lock);

    uint32_t utc = (uint32_t)time(NULL);
    for (int i = 0; i < n; i++) {
        rtsp_sess_t *s = list[i];
        if (!s->playing || s->fd < 0) continue;
        s->codec = codec;
        if (is_key) sess_remember_key(s, data, len, codec);
        s->vts = ts_ms * 90u;
        if (s->want_video)
            send_annexb(s, data, len, codec, s->vts, 1, utc);
    }
}

void nvr_rtsp_live_feed_audio(int chn, const uint8_t *data, int len, uint32_t ts_ms)
{
    (void)ts_ms;
    if (!g.run || !data || len <= 0) return;
    pthread_mutex_lock(&g.lock);
    rtsp_sess_t *list[RTSP_MAX_SESS];
    int n = 0;
    for (int i = 0; i < RTSP_MAX_SESS; i++) {
        rtsp_sess_t *s = g.sess[i];
        if (!s || s->mode != MODE_LIVE || !s->playing || !s->want_audio) continue;
        if (s->chn >= 0 && chn != s->chn) continue;
        list[n++] = s;
    }
    pthread_mutex_unlock(&g.lock);
    uint32_t utc = (uint32_t)time(NULL);
    for (int i = 0; i < n; i++)
        send_aac(list[i], data, len, list[i]->ats, 1, utc);
}
