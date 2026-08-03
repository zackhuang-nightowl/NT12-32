/**
 * @file rtsp_server.c
 * @brief Minimal RTSP media server over TCP (RTP/AVP/TCP interleaved). Serves
 *        nop_media video (H.264/H.265) as RTP and accepts an ONVIF talk
 *        backchannel (G.711) routed to a callback. See nop_sdk/nop_rtsp_server.h.
 *
 * Design: one accept thread; one thread per client connection that parses RTSP
 * and reads interleaved backchannel RTP; one nop_media subscription whose sink
 * fans encoded frames out to every PLAYing session as RTP. Lock order is always
 * server list-lock then per-connection write-lock.
 */
#define _GNU_SOURCE 1   /* memmem */
#include "nop_sdk/nop_rtsp_server.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "base/nop_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>

#define RTSP_MAX_PAYLOAD   1400      /* RTP payload cap (fits a 1500 MTU) */
#define RTSP_VIDEO_PT      96
#define RTSP_AUDIO_PT      0         /* PCMU */

typedef struct rtsp_conn {
    int                fd;
    struct nop_rtsp_server *server;
    struct rtsp_conn  *next;

    int                stream;          /* 0=main, 1=sub (from DESCRIBE path) */
    int                playing;
    int                dead;

    /* video RTP state */
    uint16_t           video_seq;
    uint32_t           video_ssrc;
    uint32_t           video_ts;
    int                video_interleaved;   /* RTP channel for video (SETUP) */

    /* backchannel audio */
    int                audio_interleaved;    /* RTP channel for talk audio, -1 if none */

    pthread_mutex_t    write_lock;           /* serialize socket writes */
    pthread_t          thread;
} rtsp_conn;

struct nop_rtsp_server {
    int                       listen_fd;
    int                       port;
    nop_media_t              *media;
    nop_media_subscription_t *subscription;
    int                       enable_backchannel;
    nop_rtsp_backchannel_fn   backchannel;
    void                     *backchannel_ctx;

    pthread_t                 accept_thread;
    volatile int              stop;

    pthread_mutex_t           list_lock;
    rtsp_conn                *conns;          /* singly linked list */
    int                       conn_count;
    uint32_t                  ssrc_seed;
};

/* ---- socket write helpers ------------------------------------------------- */
static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        sent += (size_t)n;
    }
    return 0;
}

/* Send one RTP packet over the interleaved channel ($ ch len rtp...). */
static int send_rtp_interleaved(rtsp_conn *conn, int channel, int marker,
                                int payload_type, uint32_t ts,
                                const uint8_t *payload, size_t payload_len)
{
    uint8_t pkt[4 + 12 + RTSP_MAX_PAYLOAD + 8];
    size_t  n = 0;
    int     rc;
    uint16_t rtp_len = (uint16_t)(12 + payload_len);

    if (payload_len > RTSP_MAX_PAYLOAD)
        return -1;
    /* interleaved framing */
    pkt[n++] = '$';
    pkt[n++] = (uint8_t)channel;
    pkt[n++] = (uint8_t)(rtp_len >> 8);
    pkt[n++] = (uint8_t)(rtp_len & 0xFF);
    /* RTP header */
    pkt[n++] = 0x80;
    pkt[n++] = (uint8_t)((marker ? 0x80 : 0) | (payload_type & 0x7F));
    pkt[n++] = (uint8_t)(conn->video_seq >> 8);
    pkt[n++] = (uint8_t)(conn->video_seq & 0xFF);
    conn->video_seq++;
    pkt[n++] = (uint8_t)(ts >> 24); pkt[n++] = (uint8_t)(ts >> 16);
    pkt[n++] = (uint8_t)(ts >> 8);  pkt[n++] = (uint8_t)(ts & 0xFF);
    pkt[n++] = (uint8_t)(conn->video_ssrc >> 24); pkt[n++] = (uint8_t)(conn->video_ssrc >> 16);
    pkt[n++] = (uint8_t)(conn->video_ssrc >> 8);  pkt[n++] = (uint8_t)(conn->video_ssrc & 0xFF);
    memcpy(pkt + n, payload, payload_len);
    n += payload_len;

    pthread_mutex_lock(&conn->write_lock);
    rc = write_all(conn->fd, pkt, n);
    pthread_mutex_unlock(&conn->write_lock);
    if (rc != 0)
        conn->dead = 1;
    return rc;
}

/* ---- H.264 / H.265 RTP packetization ------------------------------------- */
/* Send one NAL (no start code) as single packet or FU fragments. */
static void send_nal(rtsp_conn *conn, hal_video_codec_t codec,
                     const uint8_t *nal, size_t len, uint32_t ts, int last_nal)
{
    if (len == 0)
        return;
    if (len <= RTSP_MAX_PAYLOAD) {
        send_rtp_interleaved(conn, conn->video_interleaved, last_nal,
                             RTSP_VIDEO_PT, ts, nal, len);
        return;
    }
    if (codec == HAL_VIDEO_CODEC_H265) {
        /* RFC 7798 FU: 2-byte payload header (type=49) + 1-byte FU header. */
        uint8_t  b0 = (uint8_t)((nal[0] & 0x81) | (49 << 1));
        uint8_t  b1 = nal[1];
        uint8_t  fu_type = (uint8_t)((nal[0] >> 1) & 0x3F);
        const uint8_t *p = nal + 2;
        size_t   remain = len - 2;
        int      first = 1;
        while (remain > 0) {
            size_t chunk = remain > (RTSP_MAX_PAYLOAD - 3) ? (RTSP_MAX_PAYLOAD - 3) : remain;
            int    end = (chunk == remain);
            uint8_t buf[RTSP_MAX_PAYLOAD];
            buf[0] = b0; buf[1] = b1;
            buf[2] = (uint8_t)((first ? 0x80 : 0) | (end ? 0x40 : 0) | fu_type);
            memcpy(buf + 3, p, chunk);
            send_rtp_interleaved(conn, conn->video_interleaved,
                                 (last_nal && end), RTSP_VIDEO_PT, ts, buf, chunk + 3);
            p += chunk; remain -= chunk; first = 0;
        }
    } else {
        /* RFC 6184 FU-A: 1-byte indicator + 1-byte FU header. */
        uint8_t  indicator = (uint8_t)((nal[0] & 0xE0) | 28);
        uint8_t  nal_type   = (uint8_t)(nal[0] & 0x1F);
        const uint8_t *p = nal + 1;
        size_t   remain = len - 1;
        int      first = 1;
        while (remain > 0) {
            size_t chunk = remain > (RTSP_MAX_PAYLOAD - 2) ? (RTSP_MAX_PAYLOAD - 2) : remain;
            int    end = (chunk == remain);
            uint8_t buf[RTSP_MAX_PAYLOAD];
            buf[0] = indicator;
            buf[1] = (uint8_t)((first ? 0x80 : 0) | (end ? 0x40 : 0) | nal_type);
            memcpy(buf + 2, p, chunk);
            send_rtp_interleaved(conn, conn->video_interleaved,
                                 (last_nal && end), RTSP_VIDEO_PT, ts, buf, chunk + 2);
            p += chunk; remain -= chunk; first = 0;
        }
    }
}

/* Find the next Annex-B start code (00 00 01 / 00 00 00 01) at or after @p from.
 * Returns offset of the start code, or @p len if none; sets *sc_len. */
static size_t find_start_code(const uint8_t *d, size_t len, size_t from, int *sc_len)
{
    size_t i;
    for (i = from; i + 3 <= len; i++) {
        if (d[i] == 0 && d[i+1] == 0 && d[i+2] == 1) { *sc_len = 3; return i; }
        if (i + 4 <= len && d[i] == 0 && d[i+1] == 0 && d[i+2] == 0 && d[i+3] == 1) {
            *sc_len = 4; return i;
        }
    }
    return len;
}

/* Split an Annex-B access unit into NALs and RTP-send them. */
static void send_access_unit(rtsp_conn *conn, const hal_video_frame_t *frame)
{
    const uint8_t *d = frame->data;
    size_t len = frame->length, pos;
    int sc;
    uint32_t ts;
    /* NAL boundaries */
    size_t starts[64]; int start_sc[64]; int count = 0;

    if (!d || len == 0)
        return;
    ts = frame->timestamp_ms ? (uint32_t)(frame->timestamp_ms * 90) : (conn->video_ts += 3000);

    pos = find_start_code(d, len, 0, &sc);
    while (pos < len && count < 64) {
        starts[count] = pos; start_sc[count] = sc; count++;
        pos = find_start_code(d, len, pos + sc + 1, &sc);
    }
    if (count == 0) {
        /* No start code: treat whole buffer as one NAL. */
        send_nal(conn, frame->codec, d, len, ts, 1);
        return;
    }
    {
        int k;
        for (k = 0; k < count; k++) {
            size_t nal_off = starts[k] + start_sc[k];
            size_t nal_end = (k + 1 < count) ? starts[k+1] : len;
            send_nal(conn, frame->codec, d + nal_off, nal_end - nal_off, ts,
                     (k + 1 == count));
        }
    }
}

/* nop_media sink: fan a frame out to every PLAYing session on the matching stream. */
static void rtsp_media_sink(void *sink_ctx, int channel, const hal_video_frame_t *frame)
{
    nop_rtsp_server_t *server = (nop_rtsp_server_t *)sink_ctx;
    rtsp_conn *conn;
    (void)channel;
    pthread_mutex_lock(&server->list_lock);
    for (conn = server->conns; conn; conn = conn->next) {
        if (conn->playing && !conn->dead && conn->stream == frame->stream)
            send_access_unit(conn, frame);
    }
    pthread_mutex_unlock(&server->list_lock);
}

/* ---- RTSP request handling ------------------------------------------------ */
static void header_value(const char *req, const char *name, char *out, size_t cap)
{
    const char *p = req;
    size_t nlen = strlen(name);
    out[0] = '\0';
    while (p && *p) {
        if (strncasecmp(p, name, nlen) == 0) {
            p += nlen;
            while (*p == ' ' || *p == ':' || *p == '\t') p++;
            { size_t i = 0; while (p[i] && p[i] != '\r' && p[i] != '\n' && i < cap - 1) { out[i] = p[i]; i++; } out[i] = '\0'; }
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

static void send_rtsp(rtsp_conn *conn, const char *text)
{
    pthread_mutex_lock(&conn->write_lock);
    write_all(conn->fd, (const uint8_t *)text, strlen(text));
    pthread_mutex_unlock(&conn->write_lock);
}

static void handle_rtsp_request(rtsp_conn *conn, const char *req)
{
    char method[16] = {0}, url[512] = {0}, cseq[32] = {0}, transport[256] = {0};
    char resp[1024];

    sscanf(req, "%15s %511s", method, url);
    header_value(req, "CSeq", cseq, sizeof(cseq));
    if (cseq[0] == '\0') strcpy(cseq, "0");

    if (strstr(url, "sub")) conn->stream = 1; else conn->stream = 0;

    if (strcasecmp(method, "OPTIONS") == 0) {
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n\r\n", cseq);
        send_rtsp(conn, resp);
    } else if (strcasecmp(method, "DESCRIBE") == 0) {
        char sdp[640];
        int  bc = conn->server->enable_backchannel;
        int  sdp_len = snprintf(sdp, sizeof(sdp),
            "v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=NOP\r\nt=0 0\r\n"
            "m=video 0 RTP/AVP %d\r\na=rtpmap:%d H264/90000\r\na=control:trackID=0\r\n"
            "%s",
            RTSP_VIDEO_PT, RTSP_VIDEO_PT,
            bc ? "m=audio 0 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n"
                 "a=control:trackID=1\r\na=sendonly\r\n" : "");
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\nContent-Type: application/sdp\r\n"
            "Content-Base: %s/\r\nContent-Length: %d\r\n\r\n%s", cseq, url, sdp_len, sdp);
        send_rtsp(conn, resp);
    } else if (strcasecmp(method, "SETUP") == 0) {
        int lo = 0, hi = 1, is_audio = (strstr(url, "trackID=1") != NULL);
        header_value(req, "Transport", transport, sizeof(transport));
        { const char *il = strstr(transport, "interleaved=");
          if (il) sscanf(il + 12, "%d-%d", &lo, &hi); }
        if (is_audio) conn->audio_interleaved = lo;
        else          conn->video_interleaved = lo;
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
            "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\nSession: 12345678\r\n\r\n",
            cseq, lo, hi);
        send_rtsp(conn, resp);
    } else if (strcasecmp(method, "PLAY") == 0) {
        conn->playing = 1;
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: 12345678\r\nRTP-Info: url=%s\r\n\r\n",
            cseq, url);
        send_rtsp(conn, resp);
    } else if (strcasecmp(method, "GET_PARAMETER") == 0) {
        snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: 12345678\r\n\r\n", cseq);
        send_rtsp(conn, resp);
    } else if (strcasecmp(method, "TEARDOWN") == 0) {
        snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
        send_rtsp(conn, resp);
        conn->dead = 1;
    } else {
        snprintf(resp, sizeof(resp), "RTSP/1.0 501 Not Implemented\r\nCSeq: %s\r\n\r\n", cseq);
        send_rtsp(conn, resp);
    }
}

/* ---- connection thread: parse RTSP + interleaved backchannel RTP ---------- */
static void conn_remove(rtsp_conn *conn)
{
    nop_rtsp_server_t *s = conn->server;
    rtsp_conn **pp;
    pthread_mutex_lock(&s->list_lock);
    for (pp = &s->conns; *pp; pp = &(*pp)->next) {
        if (*pp == conn) { *pp = conn->next; break; }
    }
    s->conn_count--;
    pthread_mutex_unlock(&s->list_lock);
    pthread_mutex_destroy(&conn->write_lock);
    close(conn->fd);
    nop_free(conn);
}

static void *conn_thread(void *arg)
{
    rtsp_conn *conn = (rtsp_conn *)arg;
    uint8_t    buf[8192];
    size_t     have = 0;

    while (!conn->server->stop && !conn->dead) {
        ssize_t n;
        /* Process complete units already buffered. */
        for (;;) {
            if (have == 0) break;
            if (buf[0] == '$') {                 /* interleaved RTP from client */
                uint16_t plen;
                size_t   total;
                if (have < 4) break;
                plen = (uint16_t)((buf[2] << 8) | buf[3]);
                total = 4 + plen;
                if (have < total) break;
                /* backchannel audio: strip 12-byte RTP header, hand payload up */
                if ((int)buf[1] == conn->audio_interleaved && conn->server->backchannel &&
                    plen > 12) {
                    conn->server->backchannel(conn->server->backchannel_ctx, 0,
                                              buf + 4 + 12, plen - 12);
                }
                memmove(buf, buf + total, have - total);
                have -= total;
            } else {                              /* RTSP request: ends at \r\n\r\n */
                uint8_t *end = (uint8_t *)memmem(buf, have, "\r\n\r\n", 4);
                size_t   reqlen;
                char     req[2048];
                if (!end) break;
                reqlen = (size_t)(end - buf) + 4;
                if (reqlen < sizeof(req)) {
                    memcpy(req, buf, reqlen); req[reqlen] = '\0';
                    handle_rtsp_request(conn, req);
                }
                memmove(buf, buf + reqlen, have - reqlen);
                have -= reqlen;
            }
        }
        if (have == sizeof(buf)) { conn->dead = 1; break; }   /* overflow guard */
        n = read(conn->fd, buf + have, sizeof(buf) - have);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; break; }
        have += (size_t)n;
    }
    conn_remove(conn);
    return NULL;
}

/* ---- accept loop ---------------------------------------------------------- */
static void *accept_loop(void *arg)
{
    nop_rtsp_server_t *server = (nop_rtsp_server_t *)arg;
    while (!server->stop) {
        fd_set rd; struct timeval tv; int ready, fd;
        rtsp_conn *conn;
        FD_ZERO(&rd); FD_SET(server->listen_fd, &rd);
        tv.tv_sec = 0; tv.tv_usec = 200000;
        ready = select(server->listen_fd + 1, &rd, NULL, NULL, &tv);
        if (ready <= 0) continue;
        fd = accept(server->listen_fd, NULL, NULL);
        if (fd < 0) continue;
        {   int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            struct timeval wt; wt.tv_sec = 2; wt.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wt, sizeof(wt)); }
        conn = (rtsp_conn *)nop_calloc(1, sizeof(*conn));
        if (!conn) { close(fd); continue; }
        conn->fd = fd; conn->server = server; conn->audio_interleaved = -1;
        conn->video_interleaved = 0;
        conn->video_ssrc = (server->ssrc_seed += 0x10001);
        pthread_mutex_init(&conn->write_lock, NULL);
        pthread_mutex_lock(&server->list_lock);
        conn->next = server->conns; server->conns = conn; server->conn_count++;
        pthread_mutex_unlock(&server->list_lock);
        if (pthread_create(&conn->thread, NULL, conn_thread, conn) != 0) {
            conn_remove(conn);
        } else {
            pthread_detach(conn->thread);
        }
    }
    return NULL;
}

/* ---- public API ----------------------------------------------------------- */
nop_rtsp_server_t *nop_rtsp_server_start(const nop_rtsp_server_config_t *config)
{
    nop_rtsp_server_t *server;
    struct sockaddr_in addr;
    int reuse = 1, port;

    if (!config || !config->media)
        return NULL;
    port = config->port > 0 ? config->port : 554;

    server = (nop_rtsp_server_t *)nop_calloc(1, sizeof(*server));
    if (!server)
        return NULL;
    server->media = config->media;
    server->enable_backchannel = config->enable_backchannel;
    server->backchannel = config->backchannel;
    server->backchannel_ctx = config->backchannel_ctx;
    server->ssrc_seed = 0x4E4F5000u;
    pthread_mutex_init(&server->list_lock, NULL);

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) goto fail;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) goto fail;
    if (listen(server->listen_fd, 8) != 0) goto fail;
    server->port = port;

    server->subscription = nop_media_subscribe(server->media, rtsp_media_sink, server);
    if (!server->subscription) goto fail;

    if (pthread_create(&server->accept_thread, NULL, accept_loop, server) != 0) goto fail;
    return server;

fail:
    if (server->subscription) nop_media_unsubscribe(server->media, server->subscription);
    if (server->listen_fd >= 0) close(server->listen_fd);
    pthread_mutex_destroy(&server->list_lock);
    nop_free(server);
    return NULL;
}

void nop_rtsp_server_stop(nop_rtsp_server_t *server)
{
    int waited = 0;
    rtsp_conn *conn;
    if (!server) return;

    server->stop = 1;
    pthread_join(server->accept_thread, NULL);
    /* stop frame fan-out */
    nop_media_unsubscribe(server->media, server->subscription);
    /* unblock client threads; they self-remove */
    pthread_mutex_lock(&server->list_lock);
    for (conn = server->conns; conn; conn = conn->next) {
        conn->dead = 1;
        shutdown(conn->fd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&server->list_lock);
    /* wait for connection threads to drain (≤1s) */
    while (waited < 1000) {
        int c;
        pthread_mutex_lock(&server->list_lock);
        c = server->conn_count;
        pthread_mutex_unlock(&server->list_lock);
        if (c == 0) break;
        usleep(10000); waited += 10;
    }
    close(server->listen_fd);
    pthread_mutex_destroy(&server->list_lock);
    nop_free(server);
}

int nop_rtsp_server_port(const nop_rtsp_server_t *server)
{
    return server ? server->port : 0;
}

#else /* non-POSIX stubs */
#include <stddef.h>
nop_rtsp_server_t *nop_rtsp_server_start(const nop_rtsp_server_config_t *c) { (void)c; return NULL; }
void nop_rtsp_server_stop(nop_rtsp_server_t *s) { (void)s; }
int  nop_rtsp_server_port(const nop_rtsp_server_t *s) { (void)s; return 0; }
#endif
