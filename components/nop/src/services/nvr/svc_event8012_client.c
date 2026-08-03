/**
 * @file svc_event8012_client.c
 * @brief videoRecorder-role 8012 event-center client (POSIX). Connects to a
 *        camera's 8012 server, logs in, heartbeats, and delivers received
 *        SEND_MSG events (+ JPEG) to a callback. See nop_sdk/nop_event8012_client.h.
 */
#include "nop_sdk/nop_event8012_client.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "base/nop_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#define EV8012_MAGIC      0x1AA1B22Cu
#define EV8012_VERSION    1u
#define EV8012_HDR_SIZE   40
#define EV8012_LOGIN_SIZE 64
#define EV8012_USER_SIZE  24     /* stMSG_LOGIN.username[24] */
#define EV8012_PASS_SIZE  40     /* stMSG_LOGIN.password[40] */
#define EV8012_MAX_JPEG   (2u * 1024u * 1024u)
#define EV8012_HEARTBEAT_SECONDS 30
#define EV8012_CONNECT_TIMEOUT_SECONDS 5   /* bound connect() so start() can't wedge */
#define EV8012_IO_TIMEOUT_SECONDS      10  /* bound login/payload reads on a silent peer */

enum { CMD_LOGIN = 0, CMD_HEARTBEAT = 1, CMD_ACK_OK = 2, CMD_ACK_FAIL = 3,
       CMD_SEND_MSG = 4, CMD_CLOSE = 5 };

struct nop_event8012_client {
    int                    fd;
    volatile int           stop;
    volatile int           connected;
    nop_event8012_event_fn on_event;
    void                  *ctx;
    pthread_t              thread;
};

/* ---- little-endian header helpers ---------------------------------------- */
static void put_le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int read_fully(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n == 0) return -1;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        got += (size_t)n;
    }
    return 0;
}
static int write_fully(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        sent += (size_t)n;
    }
    return 0;
}
static void build_header(uint8_t hdr[EV8012_HDR_SIZE], uint32_t cmd, uint32_t data_size)
{
    memset(hdr, 0, EV8012_HDR_SIZE);
    put_le32(hdr + 0,  EV8012_MAGIC);
    put_le32(hdr + 4,  EV8012_VERSION);
    put_le32(hdr + 8,  data_size);
    put_le32(hdr + 12, cmd);
}

/* Connect with a bounded timeout via a temporary non-blocking connect, then
 * restore blocking mode. @return 0 on success. */
static int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen,
                                int seconds)
{
    int       flags = fcntl(fd, F_GETFL, 0);
    int       so_error = 0;
    socklen_t so_len = sizeof(so_error);
    fd_set    wr;
    struct timeval tv;

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    if (connect(fd, addr, addrlen) == 0) {
        fcntl(fd, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS)
        return -1;
    FD_ZERO(&wr); FD_SET(fd, &wr);
    tv.tv_sec = seconds; tv.tv_usec = 0;
    if (select(fd + 1, NULL, &wr, NULL, &tv) <= 0)
        return -1;                         /* timed out or select error */
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0 || so_error != 0)
        return -1;
    fcntl(fd, F_SETFL, flags);             /* restore blocking */
    return 0;
}

/* Bound blocking recv/send so a silent or dribbling peer can't wedge us. */
static void set_io_timeout(int fd, int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* ---- connect + login ------------------------------------------------------ */
static int connect_and_login(const nop_event8012_client_config_t *config)
{
    struct addrinfo hints, *result = NULL, *ai;
    char            port_str[16];
    int             fd = -1, port = config->port > 0 ? config->port : 8012;
    uint8_t         hdr[EV8012_HDR_SIZE], login[EV8012_LOGIN_SIZE];

    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(config->host, port_str, &hints, &result) != 0)
        return -1;
    for (ai = result; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect_with_timeout(fd, ai->ai_addr, ai->ai_addrlen,
                                 EV8012_CONNECT_TIMEOUT_SECONDS) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0)
        return -1;
    set_io_timeout(fd, EV8012_IO_TIMEOUT_SECONDS);   /* bound login + payload reads */

    /* LOGIN: header (dataSize=64) + 64-byte user/pass. */
    build_header(hdr, CMD_LOGIN, EV8012_LOGIN_SIZE);
    /* stMSG_LOGIN = username[24] + password[40] (64B); password[40] holds a
     * 36-byte ownerId UUID. Copy into the zeroed buffer at the spec offsets. */
    {
        const char *user = config->username[0] ? config->username : "admin";
        const char *pass = config->password[0] ? config->password : "admin";
        size_t user_len = strlen(user), pass_len = strlen(pass);
        if (user_len > EV8012_USER_SIZE) user_len = EV8012_USER_SIZE;
        if (pass_len > EV8012_PASS_SIZE) pass_len = EV8012_PASS_SIZE;
        memset(login, 0, sizeof(login));
        memcpy(login,                  user, user_len);
        memcpy(login + EV8012_USER_SIZE, pass, pass_len);
    }
    if (write_fully(fd, hdr, EV8012_HDR_SIZE) != 0 ||
        write_fully(fd, login, EV8012_LOGIN_SIZE) != 0) {
        close(fd); return -1;
    }
    if (read_fully(fd, hdr, EV8012_HDR_SIZE) != 0 ||
        get_le32(hdr + 0) != EV8012_MAGIC || get_le32(hdr + 12) != CMD_ACK_OK) {
        close(fd); return -1;
    }
    return fd;
}

/* ---- receive loop (with periodic heartbeat via select timeout) ----------- */
static void *client_thread(void *arg)
{
    nop_event8012_client_t *client = (nop_event8012_client_t *)arg;
    time_t last_heartbeat = time(NULL);   /* first heartbeat after the interval */

    while (!client->stop) {
        fd_set rd; struct timeval tv; int ready;
        FD_ZERO(&rd); FD_SET(client->fd, &rd);
        tv.tv_sec = 1; tv.tv_usec = 0;
        ready = select(client->fd + 1, &rd, NULL, NULL, &tv);
        if (client->stop)
            break;
        /* heartbeat every ~30s (no ack expected) */
        {
            time_t now = time(NULL);
            if (now - last_heartbeat >= EV8012_HEARTBEAT_SECONDS) {
                uint8_t hb[EV8012_HDR_SIZE];
                build_header(hb, CMD_HEARTBEAT, 0);
                if (write_fully(client->fd, hb, EV8012_HDR_SIZE) != 0)
                    break;
                last_heartbeat = now;
            }
        }
        if (ready <= 0)
            continue;                     /* timeout: loop to heartbeat check */
        {
            uint8_t  hdr[EV8012_HDR_SIZE];
            uint32_t cmd, data_size, msg_type, extend_flag;
            uint8_t *payload = NULL;
            if (read_fully(client->fd, hdr, EV8012_HDR_SIZE) != 0)
                break;
            if (get_le32(hdr + 0) != EV8012_MAGIC)
                break;
            data_size   = get_le32(hdr + 8);
            cmd         = get_le32(hdr + 12);
            msg_type    = get_le32(hdr + 20);
            extend_flag = get_le32(hdr + 24);
            if (data_size > EV8012_MAX_JPEG)
                break;
            if (data_size) {
                payload = (uint8_t *)malloc(data_size);
                if (!payload || read_fully(client->fd, payload, data_size) != 0) {
                    free(payload); break;
                }
            }
            /* Pass extend_flag through: 1=JPEG, 2=extendDataUnit blocks (the
             * caller parses JSON+JPEG, e.g. lineCross name). */
            if (cmd == CMD_SEND_MSG && client->on_event)
                client->on_event(client->ctx, msg_type, extend_flag, payload, data_size);
            free(payload);
        }
    }
    client->connected = 0;
    return NULL;
}

/* ---- public API ----------------------------------------------------------- */
nop_event8012_client_t *nop_event8012_client_start(const nop_event8012_client_config_t *config)
{
    nop_event8012_client_t *client;
    int fd;

    if (!config || !config->host[0])
        return NULL;
    fd = connect_and_login(config);
    if (fd < 0)
        return NULL;

    client = (nop_event8012_client_t *)nop_calloc(1, sizeof(*client));
    if (!client) { close(fd); return NULL; }
    client->fd        = fd;
    client->on_event  = config->on_event;
    client->ctx       = config->ctx;
    client->connected = 1;
    if (pthread_create(&client->thread, NULL, client_thread, client) != 0) {
        close(fd); nop_free(client); return NULL;
    }
    return client;
}

void nop_event8012_client_stop(nop_event8012_client_t *client)
{
    if (!client)
        return;
    client->stop = 1;
    shutdown(client->fd, SHUT_RDWR);
    pthread_join(client->thread, NULL);
    close(client->fd);
    nop_free(client);
}

int nop_event8012_client_is_connected(const nop_event8012_client_t *client)
{
    return client ? client->connected : 0;
}

#else /* non-POSIX stubs */
#include <stddef.h>
nop_event8012_client_t *nop_event8012_client_start(const nop_event8012_client_config_t *c){ (void)c; return NULL; }
void nop_event8012_client_stop(nop_event8012_client_t *c){ (void)c; }
int  nop_event8012_client_is_connected(const nop_event8012_client_t *c){ (void)c; return 0; }
#endif
