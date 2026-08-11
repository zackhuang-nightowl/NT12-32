/***************************************************************************************
 *  nvr_rtsp_live.c — 极简 RTSP/RTP over TCP(interleaved),单客户端,单会话 /live
 ***************************************************************************************/
#include "nvr_rtsp_live.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RTSP_BUF   4096
#define RTP_MTU    1400
#define RTP_HDR    12

static struct {
    volatile int     run;
    int              port;
    int              listen_fd;
    pthread_t        accept_tid;
    pthread_mutex_t  lock;
    int              client_fd;
    volatile int     playing;
    int              rtp_ch;       /* interleaved channel, default 0 */
    uint16_t         seq;
    uint32_t         rtp_ts;
    int              feed_chn;
    int              feed_codec;   /* 0=H264 1=H265 */
} g;

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

static void rtsp_reply(int fd, int code, const char *reason, const char *hdrs, const char *body)
{
    char head[512];
    int bl = body ? (int)strlen(body) : 0;
    int n = snprintf(head, sizeof(head),
                     "RTSP/1.0 %d %s\r\nCSeq: 1\r\n%sContent-Length: %d\r\n\r\n",
                     code, reason, hdrs ? hdrs : "", bl);
    if (n > 0) send_all(fd, head, (size_t)n);
    if (body && bl > 0) send_all(fd, body, (size_t)bl);
}

static void close_client(void)
{
    if (g.client_fd >= 0) { close(g.client_fd); g.client_fd = -1; }
    g.playing = 0;
    g.seq = 0;
    g.rtp_ts = 0;
}

static void handle_client(int fd)
{
    char req[RTSP_BUF];
    for (;;) {
        ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
        if (n <= 0) break;
        req[n] = '\0';

        if (strncmp(req, "OPTIONS", 7) == 0) {
            rtsp_reply(fd, 200, "OK", "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n", NULL);
        } else if (strncmp(req, "DESCRIBE", 8) == 0) {
            const char *sdp =
                "v=0\r\n"
                "o=- 0 0 IN IP4 127.0.0.1\r\n"
                "s=NVR Live\r\n"
                "c=IN IP4 0.0.0.0\r\n"
                "t=0 0\r\n"
                "m=video 0 RTP/AVP 96\r\n"
                "a=rtpmap:96 H264/90000\r\n"
                "a=control:track0\r\n"
                "a=framerate:20.0\r\n";
            rtsp_reply(fd, 200, "OK", "Content-Type: application/sdp\r\n", sdp);
        } else if (strncmp(req, "SETUP", 5) == 0) {
            g.rtp_ch = 0;
            char tr[128];
            snprintf(tr, sizeof(tr),
                     "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n",
                     g.rtp_ch, g.rtp_ch + 1);
            rtsp_reply(fd, 200, "OK", tr, NULL);
        } else if (strncmp(req, "PLAY", 4) == 0) {
            g.playing = 1;
            rtsp_reply(fd, 200, "OK", "Session: 1\r\n", NULL);
        } else if (strncmp(req, "TEARDOWN", 8) == 0) {
            g.playing = 0;
            rtsp_reply(fd, 200, "OK", "Session: 1\r\n", NULL);
            break;
        } else {
            rtsp_reply(fd, 501, "Not Implemented", NULL, NULL);
        }
    }
    close_client();
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
        pthread_mutex_lock(&g.lock);
        close_client();
        g.client_fd = fd;
        pthread_mutex_unlock(&g.lock);
        handle_client(fd);
    }
    return NULL;
}

int nvr_rtsp_live_start(int port)
{
    if (g.run) return 0;
    if (port <= 0) port = 554;
    memset(&g, 0, sizeof(g));
    g.client_fd = -1;
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
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 2) != 0) {
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
    close_client();
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.accept_tid, NULL);
    pthread_mutex_destroy(&g.lock);
}

static int send_rtp_nal(int fd, int chan, const uint8_t *nal, int nal_len, uint32_t ts)
{
    uint8_t pkt[RTP_HDR + RTP_MTU];
    pkt[0] = 0x80;
    pkt[1] = 0x60 | ((nal[0] & 0x1f) == 5 ? 0x80 : 0); /* marker on IDR-ish */
    pkt[2] = (uint8_t)(g.seq >> 8);
    pkt[3] = (uint8_t)(g.seq & 0xff);
    g.seq++;
    pkt[4] = (uint8_t)(ts >> 24);
    pkt[5] = (uint8_t)(ts >> 16);
    pkt[6] = (uint8_t)(ts >> 8);
    pkt[7] = (uint8_t)(ts);
    pkt[8] = 0x00;
    pkt[9] = 0x00;
    pkt[10] = 0x00;
    pkt[11] = 0x00;

    int pay = nal_len;
    if (pay + RTP_HDR > RTP_MTU) pay = RTP_MTU - RTP_HDR;
    memcpy(pkt + RTP_HDR, nal, (size_t)pay);

    uint8_t frame[4 + RTP_HDR + RTP_MTU];
    frame[0] = '$';
    frame[1] = (uint8_t)chan;
    uint16_t flen = htons((uint16_t)(RTP_HDR + pay));
    memcpy(frame + 2, &flen, 2);
    memcpy(frame + 4, pkt, (size_t)(RTP_HDR + pay));
    return send_all(fd, frame, (size_t)(4 + RTP_HDR + pay));
}

void nvr_rtsp_live_feed(int chn, const uint8_t *data, int len, int codec, int is_key, uint32_t ts_ms)
{
    (void)is_key;
    if (!data || len <= 0) return;
    pthread_mutex_lock(&g.lock);
    if (!g.playing || g.client_fd < 0) {
        pthread_mutex_unlock(&g.lock);
        return;
    }
    g.feed_chn = chn;
    g.feed_codec = codec;
    g.rtp_ts = ts_ms * 90; /* 90kHz */

    /* Annex-B → 单 NAL RTP(简化;大帧截断) */
    const uint8_t *p = data;
    int rem = len;
    while (rem > 3) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            p += 3; rem -= 3;
        } else if (rem > 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            p += 4; rem -= 4;
        } else { p++; rem--; continue; }
        const uint8_t *nal = p;
        while (rem > 0) {
            if (rem >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) break;
            if (rem >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) break;
            p++; rem--;
        }
        int nal_len = (int)(p - nal);
        if (nal_len > 0 && g.feed_codec == 0)
            send_rtp_nal(g.client_fd, g.rtp_ch, nal, nal_len, g.rtp_ts);
    }
    pthread_mutex_unlock(&g.lock);
}
