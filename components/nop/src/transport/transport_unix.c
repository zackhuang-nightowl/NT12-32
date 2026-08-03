/**
 * @file transport_unix.c
 * @brief Cross-process NOP transport over AF_UNIX (POSIX). Length-prefixed
 *        framing; threaded server feeding nop_app_dispatch. See
 *        nop_sdk/nop_transport_unix.h.
 */
#include "nop_sdk/nop_transport_unix.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "base/nop_mem.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#define NOP_UNIX_MAX_MESSAGE (4u * 1024u * 1024u)   /* 4 MiB frame ceiling */

/* ---- framed I/O over a stream fd ------------------------------------------ */

static int read_fully(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n == 0)
            return -1;                       /* peer closed */
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

static int write_fully(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Write a 4-byte big-endian length prefix + payload. */
static int write_frame(int fd, const char *payload, size_t len)
{
    uint8_t header[4];
    header[0] = (uint8_t)((len >> 24) & 0xFF);
    header[1] = (uint8_t)((len >> 16) & 0xFF);
    header[2] = (uint8_t)((len >> 8) & 0xFF);
    header[3] = (uint8_t)(len & 0xFF);
    if (write_fully(fd, header, 4) != 0)
        return -1;
    return write_fully(fd, (const uint8_t *)payload, len);
}

/* Read a framed message into a freshly malloc'd, NUL-terminated buffer. */
static int read_frame(int fd, char **out, size_t *out_len)
{
    uint8_t  header[4];
    uint32_t len;
    char    *buf;

    *out = NULL;
    if (read_fully(fd, header, 4) != 0)
        return -1;
    len = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
          ((uint32_t)header[2] << 8) | (uint32_t)header[3];
    if (len == 0 || len > NOP_UNIX_MAX_MESSAGE)
        return -1;
    buf = (char *)malloc(len + 1);
    if (!buf)
        return -1;
    if (read_fully(fd, (uint8_t *)buf, len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';
    *out = buf;
    if (out_len)
        *out_len = len;
    return 0;
}

/* ======================================================================== */
/* Client channel                                                           */
/* ======================================================================== */

typedef struct {
    int fd;
} unix_channel_ctx;

static int unix_request(void *ctx, const char *request_json,
                        char **response_json, uint32_t timeout_ms)
{
    unix_channel_ctx *c = (unix_channel_ctx *)ctx;
    if (!c || c->fd < 0 || !request_json || !response_json)
        return -1;

    if (timeout_ms) {
        struct timeval tv;
        tv.tv_sec  = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    if (write_frame(c->fd, request_json, strlen(request_json)) != 0)
        return -1;
    return read_frame(c->fd, response_json, NULL);
}

int nop_channel_unix_connect(nop_client_channel_if *channel, const char *socket_path)
{
    unix_channel_ctx   *c;
    struct sockaddr_un  addr;
    int                 fd;

    if (!channel || !socket_path)
        return -1;
    if (strlen(socket_path) >= sizeof(addr.sun_path))
        return -1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    c = (unix_channel_ctx *)nop_calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        return -1;
    }
    c->fd            = fd;
    channel->request = unix_request;
    channel->ctx     = c;
    return 0;
}

void nop_channel_unix_close(nop_client_channel_if *channel)
{
    unix_channel_ctx *c;
    if (!channel || !channel->ctx)
        return;
    c = (unix_channel_ctx *)channel->ctx;
    if (c->fd >= 0)
        close(c->fd);
    nop_free(c);
    channel->ctx     = NULL;
    channel->request = NULL;
}

/* ======================================================================== */
/* Server                                                                   */
/* ======================================================================== */

struct nop_unix_server {
    int        listen_fd;
    nop_app_t *app;
    char       path[108];        /* sun_path size */
    pthread_t  thread;
    volatile int stop;
    int        thread_started;
};

/* Serve one accepted connection: framed request -> dispatch -> framed reply,
 * until the peer closes or errors. */
static void serve_connection(nop_unix_server_t *server, int conn_fd)
{
    for (;;) {
        char        *request = NULL;
        char        *response = NULL;
        nop_status_t status;

        if (read_frame(conn_fd, &request, NULL) != 0)
            return;
        status = nop_app_dispatch(server->app, request, &response);
        free(request);
        if (status != NOP_OK || !response)
            return;
        if (write_frame(conn_fd, response, strlen(response)) != 0) {
            nop_app_free_response(response);
            return;
        }
        nop_app_free_response(response);
    }
}

static void *serve_loop(void *arg)
{
    nop_unix_server_t *server = (nop_unix_server_t *)arg;
    while (!server->stop) {
        fd_set         readfds;
        struct timeval tv;
        int            ready;

        FD_ZERO(&readfds);
        FD_SET(server->listen_fd, &readfds);
        tv.tv_sec  = 0;
        tv.tv_usec = 200000;     /* 200 ms — bounds stop latency */
        ready = select(server->listen_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0)
            continue;            /* timeout or EINTR: re-check stop flag */
        {
            int conn_fd = accept(server->listen_fd, NULL, NULL);
            if (conn_fd < 0)
                continue;
            serve_connection(server, conn_fd);
            close(conn_fd);
        }
    }
    return NULL;
}

nop_unix_server_t *nop_unix_server_start(const char *socket_path, nop_app_t *app)
{
    nop_unix_server_t *server;
    struct sockaddr_un addr;

    if (!socket_path || !app || strlen(socket_path) >= sizeof(addr.sun_path))
        return NULL;
    server = (nop_unix_server_t *)nop_calloc(1, sizeof(*server));
    if (!server)
        return NULL;
    server->app       = app;
    server->listen_fd = -1;
    strncpy(server->path, socket_path, sizeof(server->path) - 1);

    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->listen_fd < 0)
        goto fail;
    unlink(socket_path);          /* clear any stale socket file */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        goto fail;
    if (listen(server->listen_fd, 4) != 0)
        goto fail;
    if (pthread_create(&server->thread, NULL, serve_loop, server) != 0)
        goto fail;
    server->thread_started = 1;
    return server;

fail:
    if (server->listen_fd >= 0)
        close(server->listen_fd);
    unlink(socket_path);
    nop_free(server);
    return NULL;
}

void nop_unix_server_stop(nop_unix_server_t *server)
{
    if (!server)
        return;
    server->stop = 1;
    if (server->thread_started)
        pthread_join(server->thread, NULL);
    if (server->listen_fd >= 0)
        close(server->listen_fd);
    unlink(server->path);
    nop_free(server);
}

#else /* non-POSIX: provide stubs so the symbols exist */

#include <stddef.h>

nop_unix_server_t *nop_unix_server_start(const char *socket_path, nop_app_t *app)
{ (void)socket_path; (void)app; return NULL; }
void nop_unix_server_stop(nop_unix_server_t *server) { (void)server; }
int nop_channel_unix_connect(nop_client_channel_if *channel, const char *socket_path)
{ (void)channel; (void)socket_path; return -1; }
void nop_channel_unix_close(nop_client_channel_if *channel) { (void)channel; }

#endif
