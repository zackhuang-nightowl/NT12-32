/**
 * @file svc_event8012.c
 * @brief Camera-role 8012 event-center TCP server (POSIX). Login + heartbeat;
 *        pushes hub events as SEND_MSG + JPEG to logged-in XVR clients.
 *        See nop_sdk/nop_event8012.h.
 */
#include "nop_sdk/nop_event8012.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "base/nop_mem.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

/* Bound a blocking send to a logged-in client so one stalled peer cannot wedge
 * the event fan-out (which serializes clients) or teardown. */
#define EV8012_SEND_TIMEOUT_SECONDS 5

#define EV8012_MAGIC     0x1AA1B22Cu
#define EV8012_VERSION   1u
#define EV8012_HDR_SIZE  40
#define EV8012_LOGIN_SIZE 64
#define EV8012_USER_SIZE 24              /* stMSG_LOGIN.username[24] */
#define EV8012_PASS_SIZE 40              /* stMSG_LOGIN.password[40] (holds 36B ownerId) */
#define EV8012_MAX_JPEG  (2u * 1024u * 1024u)

enum { CMD_LOGIN = 0, CMD_HEARTBEAT = 1, CMD_ACK_OK = 2, CMD_ACK_FAIL = 3,
       CMD_SEND_MSG = 4, CMD_CLOSE = 5 };

typedef struct ev8012_conn {
    int                 fd;
    int                 logged_in;
    int                 dead;
    struct nop_event8012_server *server;
    struct ev8012_conn *next;
    pthread_mutex_t     write_lock;
    pthread_t           thread;
} ev8012_conn;

struct nop_event8012_server {
    int                       listen_fd;
    int                       port;
    nop_event_hub_t          *hub;
    nop_event_subscription_t *subscription;
    char                      username[32];
    char                      password[64];
    pthread_t                 accept_thread;
    volatile int              stop;
    pthread_mutex_t           list_lock;
    ev8012_conn              *conns;
    int                       conn_count;
};

/* ---- little-endian header helpers ---------------------------------------- */
static void put_le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v>>8); p[2] = (uint8_t)(v>>16); p[3] = (uint8_t)(v>>24); }
static uint32_t get_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

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

/* Build a 40-byte cmdHeader. */
static void build_header(uint8_t hdr[EV8012_HDR_SIZE], uint32_t cmd,
                         uint32_t data_size, uint32_t msg_type, uint32_t extend_flag)
{
    memset(hdr, 0, EV8012_HDR_SIZE);
    put_le32(hdr + 0,  EV8012_MAGIC);
    put_le32(hdr + 4,  EV8012_VERSION);
    put_le32(hdr + 8,  data_size);
    put_le32(hdr + 12, cmd);
    put_le32(hdr + 16, 0);            /* timestamp reserved */
    put_le32(hdr + 20, msg_type);
    put_le32(hdr + 24, extend_flag);
    /* reserved[3] @ 28..39 left zero */
}

static int send_header_only(ev8012_conn *conn, uint32_t cmd)
{
    uint8_t hdr[EV8012_HDR_SIZE];
    int rc;
    build_header(hdr, cmd, 0, 0, 0);
    pthread_mutex_lock(&conn->write_lock);
    rc = write_fully(conn->fd, hdr, EV8012_HDR_SIZE);
    pthread_mutex_unlock(&conn->write_lock);
    return rc;
}

/* ---- event fan-out (hub sink) -------------------------------------------- */
static void ev8012_sink(void *sink_ctx, const nop_event_t *event)
{
    nop_event8012_server_t *server = (nop_event8012_server_t *)sink_ctx;
    uint32_t msg_type = nop_event_msgtype_code(event->type);
    uint32_t extend   = event->jpeg && event->jpeg_len ? 1u : 0u;
    uint32_t data_sz  = extend ? (uint32_t)event->jpeg_len : 0u;
    uint8_t  hdr[EV8012_HDR_SIZE];
    ev8012_conn *conn;

    if (event->jpeg_len > EV8012_MAX_JPEG)
        return;
    build_header(hdr, CMD_SEND_MSG, data_sz, msg_type, extend);

    pthread_mutex_lock(&server->list_lock);
    for (conn = server->conns; conn; conn = conn->next) {
        if (!conn->logged_in || conn->dead)
            continue;
        pthread_mutex_lock(&conn->write_lock);
        if (write_fully(conn->fd, hdr, EV8012_HDR_SIZE) != 0)
            conn->dead = 1;
        else if (extend && write_fully(conn->fd, event->jpeg, event->jpeg_len) != 0)
            conn->dead = 1;
        pthread_mutex_unlock(&conn->write_lock);
    }
    pthread_mutex_unlock(&server->list_lock);
}

/* ---- connection thread --------------------------------------------------- */
static void conn_remove(ev8012_conn *conn)
{
    nop_event8012_server_t *s = conn->server;
    ev8012_conn **pp;
    pthread_mutex_lock(&s->list_lock);
    for (pp = &s->conns; *pp; pp = &(*pp)->next)
        if (*pp == conn) { *pp = conn->next; s->conn_count--; break; }
    pthread_mutex_unlock(&s->list_lock);
    pthread_mutex_destroy(&conn->write_lock);
    close(conn->fd);
    nop_free(conn);
}

static void *conn_thread(void *arg)
{
    ev8012_conn *conn = (ev8012_conn *)arg;
    while (!conn->server->stop && !conn->dead) {
        uint8_t hdr[EV8012_HDR_SIZE];
        uint32_t magic, cmd, data_size;
        if (read_fully(conn->fd, hdr, EV8012_HDR_SIZE) != 0)
            break;
        magic     = get_le32(hdr + 0);
        data_size = get_le32(hdr + 8);
        cmd       = get_le32(hdr + 12);
        if (magic != EV8012_MAGIC)
            break;                        /* bad packet -> drop connection */

        if (cmd == CMD_LOGIN) {
            /* stMSG_LOGIN = char username[24] + char password[40] (64 bytes). */
            uint8_t login[EV8012_LOGIN_SIZE];
            char user[EV8012_USER_SIZE + 1] = {0}, pass[EV8012_PASS_SIZE + 1] = {0};
            if (data_size != EV8012_LOGIN_SIZE) {
                send_header_only(conn, CMD_ACK_FAIL);   /* malformed login */
                break;
            }
            memset(login, 0, sizeof(login));            /* no uninitialized tail */
            if (read_fully(conn->fd, login, EV8012_LOGIN_SIZE) != 0)
                break;
            memcpy(user, login, EV8012_USER_SIZE); user[EV8012_USER_SIZE] = '\0';
            memcpy(pass, login + EV8012_USER_SIZE, EV8012_PASS_SIZE); pass[EV8012_PASS_SIZE] = '\0';
            if (strncmp(user, conn->server->username, EV8012_USER_SIZE) == 0 &&
                strncmp(pass, conn->server->password, EV8012_PASS_SIZE) == 0) {
                conn->logged_in = 1;
                send_header_only(conn, CMD_ACK_OK);
            } else {
                send_header_only(conn, CMD_ACK_FAIL);
                break;                    /* auth fail -> disconnect */
            }
        } else if (cmd == CMD_HEARTBEAT) {
            /* no ack */
        } else if (cmd == CMD_CLOSE) {
            break;
        } else if (data_size) {
            /* unknown cmd carrying payload: drain it */
            uint8_t drop[256];
            uint32_t left = data_size;
            while (left) { uint32_t c = left > sizeof(drop) ? sizeof(drop) : left;
                           if (read_fully(conn->fd, drop, c) != 0) { conn->dead = 1; break; }
                           left -= c; }
        }
    }
    conn_remove(conn);
    return NULL;
}

/* ---- accept loop --------------------------------------------------------- */
static void *accept_loop(void *arg)
{
    nop_event8012_server_t *server = (nop_event8012_server_t *)arg;
    while (!server->stop) {
        fd_set rd; struct timeval tv; int ready, fd;
        ev8012_conn *conn;
        FD_ZERO(&rd); FD_SET(server->listen_fd, &rd);
        tv.tv_sec = 0; tv.tv_usec = 200000;
        ready = select(server->listen_fd + 1, &rd, NULL, NULL, &tv);
        if (ready <= 0) continue;
        fd = accept(server->listen_fd, NULL, NULL);
        if (fd < 0) continue;
        conn = (ev8012_conn *)nop_calloc(1, sizeof(*conn));
        if (!conn) { close(fd); continue; }
        conn->fd = fd; conn->server = server;
        {
            struct timeval snd_timeout;
            snd_timeout.tv_sec  = EV8012_SEND_TIMEOUT_SECONDS;
            snd_timeout.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout));
        }
        pthread_mutex_init(&conn->write_lock, NULL);
        pthread_mutex_lock(&server->list_lock);
        conn->next = server->conns; server->conns = conn; server->conn_count++;
        pthread_mutex_unlock(&server->list_lock);
        if (pthread_create(&conn->thread, NULL, conn_thread, conn) != 0)
            conn_remove(conn);
        else
            pthread_detach(conn->thread);
    }
    return NULL;
}

/* ---- public API ---------------------------------------------------------- */
nop_event8012_server_t *nop_event8012_server_start(const nop_event8012_config_t *config)
{
    nop_event8012_server_t *server;
    struct sockaddr_in addr;
    int reuse = 1, port;

    if (!config || !config->hub)
        return NULL;
    port = config->port > 0 ? config->port : 8012;

    server = (nop_event8012_server_t *)nop_calloc(1, sizeof(*server));
    if (!server)
        return NULL;
    server->hub = config->hub;
    strncpy(server->username, config->username[0] ? config->username : "admin", sizeof(server->username) - 1);
    server->username[sizeof(server->username) - 1] = '\0';
    strncpy(server->password, config->password[0] ? config->password : "admin", sizeof(server->password) - 1);
    server->password[sizeof(server->password) - 1] = '\0';
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

    server->subscription = nop_event_subscribe(server->hub, ev8012_sink, server);
    if (!server->subscription) goto fail;
    if (pthread_create(&server->accept_thread, NULL, accept_loop, server) != 0) goto fail;
    return server;

fail:
    if (server->subscription) nop_event_unsubscribe(server->hub, server->subscription);
    if (server->listen_fd >= 0) close(server->listen_fd);
    pthread_mutex_destroy(&server->list_lock);
    nop_free(server);
    return NULL;
}

void nop_event8012_server_stop(nop_event8012_server_t *server)
{
    ev8012_conn *conn;
    if (!server)
        return;
    /* 1) Stop accepting: the accept loop wakes within its 200ms select. */
    server->stop = 1;
    pthread_join(server->accept_thread, NULL);
    /* 2) Detach from the hub. nop_event_unsubscribe drains in-flight publishes,
     *    so ev8012_sink is neither running nor will start after this returns. */
    nop_event_unsubscribe(server->hub, server->subscription);
    /* 3) Unblock every connection thread: shutdown() forces its blocking read/
     *    write to return an error so it exits and self-removes (conn_count--). */
    pthread_mutex_lock(&server->list_lock);
    for (conn = server->conns; conn; conn = conn->next) {
        conn->dead = 1;
        shutdown(conn->fd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&server->list_lock);
    /* 4) Wait unbounded for every connection thread to exit before freeing the
     *    server. Bounding this (as a prior version did) risked freeing the
     *    server while a slow detached thread still referenced it — a UAF. Each
     *    thread is guaranteed to exit: reads unblock via shutdown() and writes
     *    are bounded by SO_SNDTIMEO. */
    for (;;) {
        int remaining;
        pthread_mutex_lock(&server->list_lock);
        remaining = server->conn_count;
        pthread_mutex_unlock(&server->list_lock);
        if (remaining == 0)
            break;
        usleep(10000);
    }
    close(server->listen_fd);
    pthread_mutex_destroy(&server->list_lock);
    nop_free(server);
}

int nop_event8012_server_port(const nop_event8012_server_t *server)
{
    return server ? server->port : 0;
}

#else /* non-POSIX stubs */
#include <stddef.h>
nop_event8012_server_t *nop_event8012_server_start(const nop_event8012_config_t *c){ (void)c; return NULL; }
void nop_event8012_server_stop(nop_event8012_server_t *s){ (void)s; }
int  nop_event8012_server_port(const nop_event8012_server_t *s){ (void)s; return 0; }
#endif
