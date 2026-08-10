/**
 * @file http_server.c
 * @brief Inbound NOP-over-HTTP server (POSIX TCP + pthreads). Minimal HTTP/1.1:
 *        reads request headers + Content-Length body, dispatches the body as a
 *        NOP envelope, returns the response as the HTTP body. See
 *        nop_sdk/nop_http_server.h.
 */
#include "nop_sdk/nop_http_server.h"

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
#include <sys/socket.h>
#include <sys/select.h>

#define NOP_HTTP_DEFAULT_PORT   8089
#define NOP_HTTP_MAX_HEADER     8192u
#define NOP_HTTP_MAX_BODY       (4u * 1024u * 1024u)

struct nop_http_server {
    int          listen_fd;
    int          port;
    nop_app_t   *app;
    pthread_t    thread;
    volatile int stop;
    int          thread_started;
    nop_http_handler_fn handler;      /* NULL → default nop_app_dispatch path */
    void        *handler_ctx;
};

void nop_http_server_set_handler(nop_http_server_t *server,
                                 nop_http_handler_fn handler, void *ctx)
{
    if (!server)
        return;
    server->handler     = handler;
    server->handler_ctx = ctx;
}

static int write_fully(int fd, const char *buf, size_t len)
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

/* Send a complete HTTP/1.1 response with a JSON body. */
static void send_http_response(int fd, int status_code, const char *status_text,
                               const char *json_body)
{
    char   header[256];
    size_t body_len = json_body ? strlen(json_body) : 0;
    int    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, body_len);
    if (header_len <= 0 || header_len >= (int)sizeof(header))
        return;
    if (write_fully(fd, header, (size_t)header_len) != 0)
        return;
    if (body_len)
        write_fully(fd, json_body, body_len);
}

/* Case-insensitive search for a header value; copies into out, returns length
 * or -1 if not found. Expects @p headers to be NUL-terminated. */
static long find_content_length(const char *headers)
{
    const char *p = headers;
    while (p && *p) {
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            p += 15;
            while (*p == ' ' || *p == '\t')
                p++;
            return strtol(p, NULL, 10);
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return -1;
}

/* Read one HTTP request, dispatch its body, and reply. */
static void serve_connection(nop_http_server_t *server, int conn_fd)
{
    char  *buffer;
    size_t capacity = NOP_HTTP_MAX_HEADER + 1;
    size_t total = 0;
    char  *header_end = NULL;
    long   content_length;
    size_t header_len, body_have, body_need;
    char  *body;
    char  *response = NULL;

    buffer = (char *)malloc(capacity);
    if (!buffer)
        return;

    /* Read until end of headers ("\r\n\r\n"). */
    while (total < NOP_HTTP_MAX_HEADER) {
        ssize_t n = read(conn_fd, buffer + total, capacity - 1 - total);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            free(buffer);
            return;
        }
        total += (size_t)n;
        buffer[total] = '\0';
        header_end = strstr(buffer, "\r\n\r\n");
        if (header_end)
            break;
    }
    if (!header_end) {
        send_http_response(conn_fd, 400, "Bad Request", "{\"statusCode\":400}");
        free(buffer);
        return;
    }

    header_len   = (size_t)(header_end - buffer) + 4;
    content_length = find_content_length(buffer);
    if (content_length < 0)
        content_length = 0;
    if ((size_t)content_length > NOP_HTTP_MAX_BODY) {
        send_http_response(conn_fd, 413, "Payload Too Large", "{\"statusCode\":400}");
        free(buffer);
        return;
    }

    body_have = total - header_len;
    body_need = (size_t)content_length;

    /* Grow buffer and read the rest of the body if needed. */
    if (body_need > body_have) {
        size_t want = header_len + body_need + 1;
        char  *grown = (char *)realloc(buffer, want);
        if (!grown) {
            free(buffer);
            return;
        }
        buffer = grown;
        while (body_have < body_need) {
            ssize_t n = read(conn_fd, buffer + header_len + body_have,
                             body_need - body_have);
            if (n <= 0) {
                if (n < 0 && errno == EINTR)
                    continue;
                break;
            }
            body_have += (size_t)n;
        }
    }
    body = buffer + header_len;
    body[body_need] = '\0';

    if (body_need == 0) {
        send_http_response(conn_fd, 400, "Bad Request",
                           "{\"statusCode\":400,\"statusMsg\":\"empty body\"}");
        free(buffer);
        return;
    }

    /* Single 8089 inbound entry: if an app-level handler is installed, it owns
     * the processing (display / channel forward / nop fallback); otherwise the
     * default path dispatches straight into nop_app. */
    if (server->handler) {
        char *resp = server->handler(server->handler_ctx, body);
        if (resp) {
            send_http_response(conn_fd, 200, "OK", resp);
            free(resp);
        } else {
            send_http_response(conn_fd, 500, "Internal Server Error",
                               "{\"statusCode\":500}");
        }
        free(buffer);
        return;
    }

    if (nop_app_dispatch(server->app, body, &response) == NOP_OK && response) {
        send_http_response(conn_fd, 200, "OK", response);
        nop_app_free_response(response);
    } else {
        send_http_response(conn_fd, 500, "Internal Server Error",
                           "{\"statusCode\":500}");
    }
    free(buffer);
}

static void *serve_loop(void *arg)
{
    nop_http_server_t *server = (nop_http_server_t *)arg;
    while (!server->stop) {
        fd_set         readfds;
        struct timeval tv;
        int            ready;

        FD_ZERO(&readfds);
        FD_SET(server->listen_fd, &readfds);
        tv.tv_sec  = 0;
        tv.tv_usec = 200000;
        ready = select(server->listen_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0)
            continue;
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

nop_http_server_t *nop_http_server_start(int port, nop_app_t *app)
{
    nop_http_server_t *server;
    struct sockaddr_in addr;
    int                reuse = 1;
    socklen_t          addr_len = sizeof(addr);

    if (!app)
        return NULL;
    if (port <= 0)
        port = NOP_HTTP_DEFAULT_PORT;

    server = (nop_http_server_t *)nop_calloc(1, sizeof(*server));
    if (!server)
        return NULL;
    server->app       = app;
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0)
        { fprintf(stderr, "[nop_http] socket: %s\n", strerror(errno)); goto fail; }
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);
    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        { fprintf(stderr, "[nop_http] bind %d: %s\n", port, strerror(errno)); goto fail; }
    if (listen(server->listen_fd, 8) != 0)
        { fprintf(stderr, "[nop_http] listen: %s\n", strerror(errno)); goto fail; }
    /* Resolve the actual port (useful when caller passed an ephemeral 0... but
     * we map 0 to 8089 above; still read back for correctness). */
    if (getsockname(server->listen_fd, (struct sockaddr *)&addr, &addr_len) == 0)
        server->port = ntohs(addr.sin_port);
    else
        server->port = port;

    if (pthread_create(&server->thread, NULL, serve_loop, server) != 0)
        { fprintf(stderr, "[nop_http] pthread: %s\n", strerror(errno)); goto fail; }
    server->thread_started = 1;
    return server;

fail:
    if (server->listen_fd >= 0)
        close(server->listen_fd);
    nop_free(server);
    return NULL;
}

void nop_http_server_stop(nop_http_server_t *server)
{
    if (!server)
        return;
    server->stop = 1;
    if (server->thread_started)
        pthread_join(server->thread, NULL);
    if (server->listen_fd >= 0)
        close(server->listen_fd);
    nop_free(server);
}

int nop_http_server_port(const nop_http_server_t *server)
{
    return server ? server->port : 0;
}

#else /* non-POSIX stubs */

#include <stddef.h>
nop_http_server_t *nop_http_server_start(int port, nop_app_t *app)
{ (void)port; (void)app; return NULL; }
void nop_http_server_stop(nop_http_server_t *server) { (void)server; }
int  nop_http_server_port(const nop_http_server_t *server) { (void)server; return 0; }

#endif
