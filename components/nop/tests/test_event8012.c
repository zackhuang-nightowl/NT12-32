/**
 * @file test_event8012.c
 * @brief svc_event hub + camera 8012 event-center server: login OK/FAIL, and a
 *        published event delivered to a logged-in client as SEND_MSG + JPEG.
 */
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_event8012.h"
#include "nop_sdk/nop_event8012_client.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 18012
#define MAGIC 0x1AA1B22Cu
enum { CMD_LOGIN=0, CMD_ACK_OK=2, CMD_ACK_FAIL=3, CMD_SEND_MSG=4 };

static void put_le32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get_le32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* NVR-client received-event capture. */
static uint32_t g_client_msg_type;
static uint8_t  g_client_jpeg[64];
static size_t   g_client_jpeg_len;
static uint32_t g_client_extend_flag;
static void client_event(void *ctx, uint32_t msg_type, uint32_t extend_flag,
                         const uint8_t *jpeg, size_t len)
{
    (void)ctx;
    g_client_msg_type    = msg_type;
    g_client_extend_flag = extend_flag;
    if (jpeg && len <= sizeof(g_client_jpeg)) { memcpy(g_client_jpeg, jpeg, len); g_client_jpeg_len = len; }
}

static int connect_port(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; struct timeval tv;
    memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(PORT);
    inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
    if (connect(fd,(struct sockaddr*)&a,sizeof a)!=0) return -1;
    tv.tv_sec=2; tv.tv_usec=0; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    return fd;
}

/* Send a LOGIN packet (40-byte header + 64-byte user/pass). */
static void send_login(int fd, const char *user, const char *pass)
{
    uint8_t hdr[40]; uint8_t body[64]; ssize_t r;
    memset(hdr,0,sizeof hdr);
    put_le32(hdr+0,MAGIC); put_le32(hdr+4,1); put_le32(hdr+8,64); put_le32(hdr+12,CMD_LOGIN);
    /* username[24] + password[40] per the 8012 spec. */
    memset(body,0,sizeof body);
    strncpy((char*)body, user, 23); strncpy((char*)body+24, pass, 39);
    r = write(fd,hdr,sizeof hdr); (void)r;
    r = write(fd,body,sizeof body); (void)r;
}

int main(void)
{
    nop_event_hub_t        *hub;
    nop_event8012_config_t  cfg;
    nop_event8012_server_t *server;
    int                     fd;
    uint8_t                 hdr[40];
    nop_event_t             evt;
    const unsigned char     jpeg[] = "JPEGDATA";

    hub = nop_event_hub_create();
    if (!hub) return fail("hub create");
    if (nop_event_msgtype_code(NOP_DETECT_ANIMAL) != 30316) return fail("msgtype map");

    memset(&cfg,0,sizeof cfg);
    cfg.port = PORT; cfg.hub = hub;
    strcpy(cfg.username,"admin"); strcpy(cfg.password,"admin");
    server = nop_event8012_server_start(&cfg);
    if (!server) return fail("8012 start");

    /* --- 1) bad password -> ACK_FAIL --- */
    fd = connect_port(); if (fd < 0) return fail("connect1");
    send_login(fd, "admin", "wrong");
    if (read(fd, hdr, 40) != 40) return fail("no ACK for bad login");
    if (get_le32(hdr+12) != CMD_ACK_FAIL) return fail("bad login not rejected");
    close(fd);

    /* --- 2) good login -> ACK_OK, then event -> SEND_MSG + JPEG --- */
    fd = connect_port(); if (fd < 0) return fail("connect2");
    send_login(fd, "admin", "admin");
    if (read(fd, hdr, 40) != 40) return fail("no ACK for good login");
    if (get_le32(hdr+12) != CMD_ACK_OK) return fail("good login not accepted");

    usleep(50000);  /* let subscribe settle */
    memset(&evt,0,sizeof evt);
    evt.channel=0; evt.type=NOP_DETECT_HUMAN; evt.timestamp_ms=1234;
    evt.jpeg=jpeg; evt.jpeg_len=8;  /* "JPEGDATA" */
    nop_event_publish(hub, &evt);

    if (read(fd, hdr, 40) != 40) return fail("no SEND_MSG");
    if (get_le32(hdr+0)  != MAGIC)        return fail("bad magic");
    if (get_le32(hdr+12) != CMD_SEND_MSG) return fail("not SEND_MSG");
    if (get_le32(hdr+20) != 2)            return fail("msgType != Human(2)");
    if (get_le32(hdr+24) != 1)            return fail("extendDataFlag != 1");
    if (get_le32(hdr+8)  != 8)            return fail("dataSize != 8");
    {
        uint8_t payload[8];
        if (read(fd, payload, 8) != 8)    return fail("no JPEG payload");
        if (memcmp(payload, "JPEGDATA", 8) != 0) return fail("JPEG mismatch");
    }
    close(fd);

    /* --- 3) NVR-role client round-trip against the same server --- */
    {
        nop_event8012_client_config_t ccfg;
        nop_event8012_client_t       *client;
        int i;
        memset(&ccfg, 0, sizeof(ccfg));
        strcpy(ccfg.host, "127.0.0.1"); ccfg.port = PORT;
        strcpy(ccfg.username, "admin"); strcpy(ccfg.password, "admin");
        ccfg.on_event = client_event; ccfg.ctx = NULL;
        client = nop_event8012_client_start(&ccfg);
        if (!client) return fail("nvr client connect/login");
        if (!nop_event8012_client_is_connected(client)) return fail("client not connected");

        usleep(50000);
        memset(&evt, 0, sizeof(evt));
        evt.channel = 0; evt.type = NOP_DETECT_ANIMAL;
        evt.jpeg = jpeg; evt.jpeg_len = 8;
        nop_event_publish(hub, &evt);

        for (i = 0; i < 200 && g_client_msg_type == 0; i++) usleep(10000);
        if (g_client_msg_type != 30316) return fail("client did not receive Animal(30316)");
        if (g_client_jpeg_len != 8 || memcmp(g_client_jpeg, "JPEGDATA", 8) != 0)
            return fail("client JPEG mismatch");
        nop_event8012_client_stop(client);
    }

    /* --- 4) recent-event store (backs queryEventList) --- */
    {
        nop_event_t recent[8];
        int n = nop_event_hub_recent(hub, recent, 8);
        /* Two events were published above (HUMAN then ANIMAL); newest first. */
        if (n < 2) return fail("event store did not retain events");
        if (recent[0].type != NOP_DETECT_ANIMAL || recent[1].type != NOP_DETECT_HUMAN)
            return fail("event store order/content wrong");
    }

    nop_event8012_server_stop(server);
    nop_event_hub_destroy(hub);
    printf("test_event8012: OK (login ok/fail + server push + NVR client + event store)\n");
    return 0;
}
