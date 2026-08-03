/**
 * @file test_rtsp_server.c
 * @brief RTSP server end-to-end over TCP-interleaved: SETUP+PLAY, publish a
 *        video frame via nop_media and confirm the client receives it as an
 *        RTP packet; then send a backchannel RTP audio packet and confirm it
 *        reaches the talk callback.
 */
#include "nop_sdk/nop_media.h"
#include "nop_sdk/nop_rtsp_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 18554

static char   g_talk[64];
static size_t g_talk_len;
static void talk_sink(void *ctx, int channel, const unsigned char *data, size_t len)
{
    (void)ctx; (void)channel;
    if (len < sizeof(g_talk)) { memcpy(g_talk, data, len); g_talk_len = len; }
}

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void send_str(int fd, const char *s) { ssize_t r = write(fd, s, strlen(s)); (void)r; }

int main(void)
{
    nop_media_t              *media;
    nop_rtsp_server_config_t  cfg;
    nop_rtsp_server_t        *server;
    int                       fd;
    struct sockaddr_in        addr;
    unsigned char             rx[2048];
    ssize_t                   n;
    struct timeval            tv;
    /* one Annex-B IDR NAL: start code + NAL(type5) + 3 payload bytes */
    const unsigned char       frame_data[] = { 0,0,0,1, 0x65, 0x11, 0x22, 0x33 };
    hal_video_frame_t         frame;
    int                       i, found_rtp = 0;

    media = nop_media_create();
    if (!media) return fail("media create");

    memset(&cfg, 0, sizeof(cfg));
    cfg.port = PORT; cfg.media = media;
    cfg.enable_backchannel = 1; cfg.backchannel = talk_sink;
    server = nop_rtsp_server_start(&cfg);
    if (!server) return fail("rtsp start");

    fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) return fail("connect");
    tv.tv_sec = 2; tv.tv_usec = 0; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* RTSP handshake: SETUP video (interleaved 0-1), SETUP audio (2-3), PLAY. */
    send_str(fd, "SETUP rtsp://127.0.0.1/main/trackID=0 RTSP/1.0\r\nCSeq: 1\r\n"
                 "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n");
    usleep(50000);
    send_str(fd, "SETUP rtsp://127.0.0.1/main/trackID=1 RTSP/1.0\r\nCSeq: 2\r\n"
                 "Transport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n\r\n");
    usleep(50000);
    send_str(fd, "PLAY rtsp://127.0.0.1/main RTSP/1.0\r\nCSeq: 3\r\n\r\n");
    usleep(100000);
    /* drain RTSP responses */
    while ((n = read(fd, rx, sizeof(rx))) > 0 && n == sizeof(rx)) {}

    /* publish a video frame; the server should RTP it to us */
    memset(&frame, 0, sizeof(frame));
    frame.stream = 0; frame.codec = HAL_VIDEO_CODEC_H264; frame.is_keyframe = 1;
    frame.data = frame_data; frame.length = sizeof(frame_data);
    nop_media_publish(media, 0, &frame);
    usleep(100000);

    n = read(fd, rx, sizeof(rx));
    if (n <= 0) return fail("no RTP received after publish");
    /* scan for an interleaved RTP frame on channel 0 */
    for (i = 0; i + 4 + 12 <= (int)n; i++) {
        if (rx[i] == '$' && rx[i+1] == 0) {
            int rtp = i + 4;
            int pt  = rx[rtp + 1] & 0x7F;
            const unsigned char *payload = rx + rtp + 12;
            if (pt == 96 && payload[0] == 0x65 && payload[1] == 0x11 &&
                payload[2] == 0x22 && payload[3] == 0x33) { found_rtp = 1; break; }
        }
    }
    if (!found_rtp) return fail("RTP packet/payload mismatch");

    /* backchannel: send an interleaved RTP audio packet (channel 2) with G.711
     * payload "ABCD"; expect it at the talk callback. */
    {
        unsigned char pkt[4 + 12 + 4];
        int m = 0;
        pkt[m++] = '$'; pkt[m++] = 2; pkt[m++] = 0; pkt[m++] = 12 + 4;
        memset(pkt + m, 0, 12); pkt[m+1] = 0; m += 12;        /* RTP header */
        pkt[m++] = 'A'; pkt[m++] = 'B'; pkt[m++] = 'C'; pkt[m++] = 'D';
        { ssize_t r = write(fd, pkt, m); (void)r; }
        usleep(100000);
    }
    if (g_talk_len != 4 || memcmp(g_talk, "ABCD", 4) != 0)
        return fail("backchannel audio not delivered");

    close(fd);
    nop_rtsp_server_stop(server);
    nop_media_destroy(media);
    printf("test_rtsp_server: OK (RTP video out + backchannel audio in)\n");
    return 0;
}
