/**
 * @file server_request.c
 * @brief Outbound "request-to-server" services (one home for all of them).
 *        Implemented: OTA. Shared: a plain-HTTP GET primitive that future
 *        services (push registration, cloud upload, telemetry) reuse.
 *        POSIX (sockets + pthreads). See nop_sdk/nop_server_request.h.
 */
#include "nop_sdk/nop_server_request.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>

/* ======================================================================== */
/* Shared outbound HTTP GET (plain http://)                                  */
/* ======================================================================== */

static int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        sent += (size_t)n;
    }
    return 0;
}

void nop_server_http_response_free(nop_http_response_t *response)
{
    if (response && response->body) {
        free(response->body);
        response->body = NULL;
        response->body_length = 0;
    }
}

/* Parse "http://host[:port]/path" → host, port, path. Returns 0 on success. */
static int parse_http_url(const char *url, char *host, size_t host_cap,
                          int *port, char *path, size_t path_cap)
{
    const char *p, *host_start, *host_end, *path_start;
    size_t host_len;

    if (!url || strncmp(url, "http://", 7) != 0)
        return -1;                       /* https:// handled by a plugged fetch */
    host_start = url + 7;
    p = host_start;
    while (*p && *p != ':' && *p != '/')
        p++;
    host_end = p;
    host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= host_cap)
        return -1;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    *port = 80;
    if (*p == ':') {
        *port = atoi(p + 1);
        while (*p && *p != '/')
            p++;
    }
    path_start = (*p == '/') ? p : "/";
    if (strlen(path_start) >= path_cap)
        return -1;
    strcpy(path, path_start);
    return 0;
}

int nop_server_http_get(const char *url, nop_http_response_t *out)
{
    char             host[256], path[1024], request[1408];
    int              port, fd = -1, request_len;
    struct addrinfo  hints, *result = NULL, *ai;
    char             port_str[8];
    char            *buffer = NULL, *header_end;
    size_t           capacity = 8192, total = 0;

    if (!out)
        return -1;
    out->status_code = -1;
    out->body = NULL;
    out->body_length = 0;

    if (parse_http_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return -1;

    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &result) != 0)
        return -1;
    for (ai = result; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0)
        return -1;

    request_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: nop-sdk\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n",
        path, host);
    if (request_len <= 0 || request_len >= (int)sizeof(request) ||
        write_all(fd, request, (size_t)request_len) != 0) {
        close(fd);
        return -1;
    }

    /* Read the whole response (Connection: close) into a growable buffer. */
    buffer = (char *)malloc(capacity);
    if (!buffer) { close(fd); return -1; }
    for (;;) {
        ssize_t n;
        if (total + 1 >= capacity) {
            size_t want = capacity * 2;
            char  *grown = (char *)realloc(buffer, want);
            if (!grown) { free(buffer); close(fd); return -1; }
            buffer = grown;
            capacity = want;
        }
        n = read(fd, buffer + total, capacity - 1 - total);
        if (n == 0)
            break;
        if (n < 0) { if (errno == EINTR) continue; break; }
        total += (size_t)n;
    }
    close(fd);
    buffer[total] = '\0';

    /* Status line: "HTTP/1.x <code> ...". */
    {
        const char *sp = strchr(buffer, ' ');
        if (sp)
            out->status_code = atoi(sp + 1);
    }
    /* Body after the header terminator. */
    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end) {
        const char *body_start = header_end + 4;
        size_t      body_len = total - (size_t)(body_start - buffer);
        out->body = (char *)malloc(body_len + 1);
        if (out->body) {
            memcpy(out->body, body_start, body_len);
            out->body[body_len] = '\0';
            out->body_length = body_len;
        }
    }
    free(buffer);
    return 0;
}

/* ======================================================================== */
/* OTA                                                                      */
/* ======================================================================== */

struct nop_ota {
    nop_config_ota_t  config;
    nop_http_fetch_fn fetch;
    void             *fetch_ctx;
    nop_ota_write_fn  writer;
    void             *writer_ctx;
    osal_mutex_t     *lock;
    pthread_t         thread;
    int               running;    /* download in flight */
    int               joinable;    /* a thread handle awaits pthread_join */
    int               automatic;
    nop_ota_status_t  status;
    int               error;
    char              url[768];
};

static int default_fetch(const char *url, nop_http_response_t *out, void *ctx)
{
    (void)ctx;
    return nop_server_http_get(url, out);
}

static void append_segment(char *out, size_t cap, size_t *len, const char *seg)
{
    if (!seg || !seg[0])
        return;
    if (*len && out[*len - 1] != '/' && *len < cap - 1)
        out[(*len)++] = '/';
    while (*seg && *len < cap - 1)
        out[(*len)++] = *seg++;
    out[*len] = '\0';
}

nop_status_t nop_ota_build_url(const nop_config_ota_t *ota, char *out, size_t capacity)
{
    size_t len = 0;
    if (!ota || !out || capacity == 0)
        return NOP_ERR_PARAM;
    out[0] = '\0';
    if (!ota->url_base[0])
        return NOP_ERR_PARAM;
    append_segment(out, capacity, &len, ota->url_base);
    append_segment(out, capacity, &len, ota->company);
    append_segment(out, capacity, &len, ota->product);
    append_segment(out, capacity, &len, ota->model);
    if (len >= capacity - 1)
        return NOP_ERR_PARAM;            /* truncated */
    return NOP_OK;
}

nop_ota_t *nop_ota_create(const nop_config_ota_t *ota)
{
    nop_ota_t *session;
    if (!ota)
        return NULL;
    session = (nop_ota_t *)nop_calloc(1, sizeof(*session));
    if (!session)
        return NULL;
    session->config = *ota;
    session->fetch  = default_fetch;
    session->status = NOP_OTA_IDLE;
    session->lock   = osal_mutex_create();
    if (!session->lock) {
        nop_free(session);
        return NULL;
    }
    return session;
}

static void set_status(nop_ota_t *s, nop_ota_status_t status, int error)
{
    osal_mutex_lock(s->lock);
    s->status = status;
    s->error  = error;
    osal_mutex_unlock(s->lock);
}

void nop_ota_destroy(nop_ota_t *ota)
{
    if (!ota)
        return;
    if (ota->joinable) {
        pthread_join(ota->thread, NULL);
        ota->joinable = 0;
    }
    osal_mutex_destroy(ota->lock);
    nop_free(ota);
}

void nop_ota_set_fetch(nop_ota_t *ota, nop_http_fetch_fn fetch, void *ctx)
{
    if (!ota)
        return;
    ota->fetch     = fetch ? fetch : default_fetch;
    ota->fetch_ctx = ctx;
}

void nop_ota_set_writer(nop_ota_t *ota, nop_ota_write_fn writer, void *ctx)
{
    if (!ota)
        return;
    ota->writer     = writer;
    ota->writer_ctx = ctx;
}

static void *ota_thread(void *arg)
{
    nop_ota_t          *s = (nop_ota_t *)arg;
    nop_http_response_t response;
    int                 rc;

    memset(&response, 0, sizeof(response));
    rc = s->fetch(s->url, &response, s->fetch_ctx);

    if (rc != 0 || response.status_code < 0) {
        set_status(s, NOP_OTA_FAILED, -1);
    } else if (response.status_code == 404) {
        set_status(s, NOP_OTA_NONE, 0);
    } else if (response.status_code == 200 && response.body && response.body_length) {
        if (s->writer)
            s->writer(s->writer_ctx, (const unsigned char *)response.body,
                      response.body_length, 0);
        set_status(s, NOP_OTA_DOWNLOADED, 0);
    } else {
        set_status(s, NOP_OTA_FAILED, response.status_code);
    }
    nop_server_http_response_free(&response);

    osal_mutex_lock(s->lock);
    s->running = 0;
    osal_mutex_unlock(s->lock);
    return NULL;
}

nop_status_t nop_ota_start(nop_ota_t *ota, const char *url, int automatic)
{
    if (!ota)
        return NOP_ERR_PARAM;

    osal_mutex_lock(ota->lock);
    if (ota->running) {
        osal_mutex_unlock(ota->lock);
        return NOP_ERR_STATE;
    }
    osal_mutex_unlock(ota->lock);
    /* Reap a prior finished thread before reusing the handle. */
    if (ota->joinable) {
        pthread_join(ota->thread, NULL);
        ota->joinable = 0;
    }

    /* Resolve the URL. */
    if (url && url[0]) {
        strncpy(ota->url, url, sizeof(ota->url) - 1);
        ota->url[sizeof(ota->url) - 1] = '\0';
    } else if (nop_ota_build_url(&ota->config, ota->url, sizeof(ota->url)) != NOP_OK) {
        return NOP_ERR_PARAM;
    }

    ota->automatic = automatic;
    set_status(ota, NOP_OTA_DOWNLOADING, 0);
    osal_mutex_lock(ota->lock);
    ota->running = 1;
    osal_mutex_unlock(ota->lock);

    if (pthread_create(&ota->thread, NULL, ota_thread, ota) != 0) {
        set_status(ota, NOP_OTA_FAILED, -1);
        osal_mutex_lock(ota->lock);
        ota->running = 0;
        osal_mutex_unlock(ota->lock);
        return NOP_ERR_INTERNAL;
    }
    ota->joinable = 1;
    return NOP_OK;
}

nop_ota_status_t nop_ota_status_get(nop_ota_t *ota, int *error_out)
{
    nop_ota_status_t status;
    if (!ota) {
        if (error_out) *error_out = -1;
        return NOP_OTA_FAILED;
    }
    osal_mutex_lock(ota->lock);
    status = ota->status;
    if (error_out)
        *error_out = ota->error;
    osal_mutex_unlock(ota->lock);
    return status;
}

#else /* non-POSIX stubs */

#include <stddef.h>
int  nop_server_http_get(const char *url, nop_http_response_t *out)
{ (void)url; if (out) { out->status_code = -1; out->body = NULL; out->body_length = 0; } return -1; }
void nop_server_http_response_free(nop_http_response_t *r) { (void)r; }
nop_status_t nop_ota_build_url(const nop_config_ota_t *o, char *out, size_t c)
{ (void)o; (void)out; (void)c; return NOP_ERR_NOTIMPL; }
nop_ota_t *nop_ota_create(const nop_config_ota_t *o) { (void)o; return NULL; }
void nop_ota_destroy(nop_ota_t *o) { (void)o; }
void nop_ota_set_fetch(nop_ota_t *o, nop_http_fetch_fn f, void *c) { (void)o; (void)f; (void)c; }
void nop_ota_set_writer(nop_ota_t *o, nop_ota_write_fn w, void *c) { (void)o; (void)w; (void)c; }
nop_status_t nop_ota_start(nop_ota_t *o, const char *u, int a) { (void)o; (void)u; (void)a; return NOP_ERR_NOTIMPL; }
nop_ota_status_t nop_ota_status_get(nop_ota_t *o, int *e) { (void)o; if (e) *e = -1; return NOP_OTA_FAILED; }

#endif
