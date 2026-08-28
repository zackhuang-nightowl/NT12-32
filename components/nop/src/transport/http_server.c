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
#include <netinet/tcp.h>   /* TCP_NODELAY */
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>      /* struct timeval (SO_RCVTIMEO/SO_SNDTIMEO) */

#define NOP_HTTP_DEFAULT_PORT   8089
#define NOP_HTTP_MAX_HEADER     8192u
#define NOP_HTTP_MAX_BODY       (4u * 1024u * 1024u)

/* ── 监控行业 NVR 命令服务器构建原则 ─────────────────────────────────────
 * 现场进程内命令口(GUI/隧道/BLE 都打 8089)。工业级 NVR(海康/大华/ONVIF 设备)
 * 命令口的三条铁律,本实现逐条落地:
 *   1) 每个已 accept 的 socket 都有收/发超时(SO_RCVTIMEO/SO_SNDTIMEO)——任何
 *      停滞/半开的对端都**不可能永久占住一个 worker**。这是之前 8089"崩溃"(19 条
 *      CLOSE_WAIT 堆积、5 worker 全卡在阻塞 read)的根因修复。
 *   2) HTTP/1.1 keep-alive:GUI 高频轮询复用同一条 TCP,消灭每请求一条连接的抖动
 *      (原先每响应都 Connection: close → 连接churn + TIME/CLOSE_WAIT 堆积)。
 *   3) 有界 worker 池 + 有界队列 + 过载**快速拒绝(503)**(load shedding),而不是
 *      让 listener 阻塞在入队上停止 accept。慢的相机透传只会占用发起它的那个 worker,
 *      且受命令级超时封顶(见 nvr_cmd_router forward),不会拖垮整池。
 * worker 栈显式收小(避免 N×8MB 的虚拟地址膨胀,32/16 路机上更省)。
 *
 *   ★ 池容量按**通道规模**定:worker 是 I/O-bound(阻塞在相机上游 socket,最长受
 *     forward 命令级超时 connect 3s / cmd 8s 封顶,期间 CPU 空转)。32 路机 GUI 一次刷新
 *     会向 32 台相机并发透传;16 worker 时一次满扇出即占满池→队列积压→本地命令 503。
 *     加倍到 32 让整扇出并行,几乎零 CPU 代价(阻塞线程不占算力),把空闲核用起来。 */
#define NOP_HTTP_MAX_WORKERS    32
/* Accepted-but-not-yet-served connections wait here. Full → shed load (503+close). */
#define NOP_HTTP_CONN_QUEUE     64u
/* Per-recv/-send deadline. Bounds header/body reads AND the keep-alive idle wait,
 * so a worker parked on a slow/idle peer frees itself within this window. */
#define NOP_HTTP_SOCK_TIMEOUT_S 10
/* Cap requests per kept-alive connection (defensive; forces periodic fd cycling). */
#define NOP_HTTP_KEEPALIVE_MAX  256
/* Worker stack: JSON dispatch + curl; 512K is ample and keeps VSZ sane at 16×. */
#define NOP_HTTP_WORKER_STACK   (512u * 1024u)

struct nop_http_server {
    int          listen_fd;
    int          port;
    nop_app_t   *app;
    pthread_t    thread;              /* listener (accept loop) */
    volatile int stop;
    int          thread_started;
    nop_http_handler_fn handler;      /* NULL → default nop_app_dispatch path */
    void        *handler_ctx;
    nop_http_uri_handler_fn uri_handler; /* GET only; NULL = no extra routes */
    void        *uri_ctx;

    /* Bounded worker pool + connection hand-off queue. */
    pthread_t       workers[NOP_HTTP_MAX_WORKERS];
    int             num_workers;
    int             conn_q[NOP_HTTP_CONN_QUEUE];
    unsigned        q_head, q_tail, q_count;
    pthread_mutex_t q_lock;
    pthread_cond_t  q_nonempty;       /* worker waits: queue has a connection */
    pthread_cond_t  q_nonfull;        /* listener waits: queue has room */
};

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

void nop_http_server_set_handler(nop_http_server_t *server,
                                 nop_http_handler_fn handler, void *ctx)
{
    if (!server)
        return;
    server->handler     = handler;
    server->handler_ctx = ctx;
}

void nop_http_server_set_uri_handler(nop_http_server_t *server,
                                     nop_http_uri_handler_fn handler, void *ctx)
{
    if (!server)
        return;
    server->uri_handler = handler;
    server->uri_ctx     = ctx;
}

int nop_http_send_response(int fd, int status, const char *status_text,
                           const char *content_type,
                           const void *body, size_t body_len)
{
    char header[256];
    int  header_len;

    if (fd < 0)
        return -1;
    if (!status_text)
        status_text = "OK";
    if (!content_type)
        content_type = "application/octet-stream";
    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    if (header_len <= 0 || header_len >= (int)sizeof(header))
        return -1;
    if (write_fully(fd, header, (size_t)header_len) != 0)
        return -1;
    if (body_len && body && write_fully(fd, (const char *)body, body_len) != 0)
        return -1;
    return 0;
}

/* Send a complete HTTP/1.1 JSON response. keep_alive selects the Connection
 * header so the peer knows whether the socket is reusable. Returns 0 on a fully
 * written response, -1 on write error (caller should drop the connection). */
static int send_http_response_ka(int fd, int status_code, const char *status_text,
                                  const char *json_body, int keep_alive)
{
    char   header[256];
    size_t body_len = json_body ? strlen(json_body) : 0;
    int    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status_code, status_text, body_len, keep_alive ? "keep-alive" : "close");
    if (header_len <= 0 || header_len >= (int)sizeof(header))
        return -1;
    if (write_fully(fd, header, (size_t)header_len) != 0)
        return -1;
    if (body_len && write_fully(fd, json_body, body_len) != 0)
        return -1;
    return 0;
}

/* Legacy one-shot (always closes) — used by error paths that also drop the conn. */
static void send_http_response(int fd, int status_code, const char *status_text,
                               const char *json_body)
{
    (void)send_http_response_ka(fd, status_code, status_text, json_body, 0);
}

/* Whether the peer wants the connection kept alive for another request.
 * HTTP/1.1 default = keep-alive unless "Connection: close"; HTTP/1.0 = close
 * unless "Connection: keep-alive". @p reqline is the request line, @p headers
 * the NUL-terminated header block. */
static int conn_keep_alive(const char *reqline, const char *headers)
{
    int http11 = (reqline && strstr(reqline, "HTTP/1.1") != NULL);
    const char *p = headers;
    while (p && *p) {
        if (strncasecmp(p, "Connection:", 11) == 0) {
            p += 11;
            while (*p == ' ' || *p == '\t') p++;
            if (strncasecmp(p, "close", 5) == 0)      return 0;
            if (strncasecmp(p, "keep-alive", 10) == 0) return 1;
            break;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return http11;
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

/* read() wrapper: retries EINTR; returns bytes (>0), 0 on peer close, -1 on
 * error or SO_RCVTIMEO expiry (EAGAIN/EWOULDBLOCK) → caller drops the conn. */
static ssize_t recv_some(int fd, char *p, size_t cap)
{
    for (;;) {
        ssize_t n = read(fd, p, cap);
        if (n >= 0)
            return n;
        if (errno == EINTR)
            continue;
        return -1;
    }
}

/* Serve requests on one connection until the peer closes, a read times out, or
 * the request asks to close. HTTP/1.1 keep-alive with pipelined-byte carry-over.
 * Bounded by SO_RCVTIMEO (set on accept) so an idle/stalled peer never pins the
 * worker beyond NOP_HTTP_SOCK_TIMEOUT_S. */
static void serve_connection(nop_http_server_t *server, int conn_fd)
{
    size_t cap = NOP_HTTP_MAX_HEADER + 1;
    char  *buf = (char *)malloc(cap);
    size_t have = 0;                 /* bytes buffered (may span >1 request) */
    int    served = 0;

    if (!buf)
        return;

    for (;;) {                       /* keep-alive request loop */
        char  *header_end;
        size_t header_len, body_need, body_have, want;
        long   content_length;
        char   reqline[288] = {0};
        int    keep_alive;
        char  *body, *grown, *resp;

        /* 1) Read until end-of-headers "\r\n\r\n" (buf stays NUL-terminated). */
        buf[have] = '\0';
        while (!(header_end = strstr(buf, "\r\n\r\n"))) {
            if (have >= NOP_HTTP_MAX_HEADER) {   /* headers too large */
                send_http_response(conn_fd, 431, "Request Header Fields Too Large",
                                   "{\"statusCode\":400}");
                goto done;
            }
            ssize_t n = recv_some(conn_fd, buf + have, cap - 1 - have);
            if (n <= 0)
                goto done;           /* peer closed / timeout / error */
            have += (size_t)n;
            buf[have] = '\0';
        }

        header_len     = (size_t)(header_end - buf) + 4;
        content_length = find_content_length(buf);
        if (content_length < 0)
            content_length = 0;
        if ((size_t)content_length > NOP_HTTP_MAX_BODY) {
            send_http_response(conn_fd, 413, "Payload Too Large", "{\"statusCode\":400}");
            goto done;
        }
        body_need = (size_t)content_length;

        /* Snapshot request line + keep-alive decision before the body read (which
         * may realloc buf). */
        {
            size_t rl = header_len < sizeof(reqline) ? header_len : sizeof(reqline) - 1;
            memcpy(reqline, buf, rl);
            reqline[rl] = '\0';
            keep_alive = conn_keep_alive(reqline, buf);
        }

        /* 2) Ensure the full body is buffered (grow if needed). */
        want = header_len + body_need + 1;
        if (want > cap) {
            grown = (char *)realloc(buf, want);
            if (!grown)
                goto done;
            buf = grown;
            cap = want;
        }
        body_have = (have > header_len) ? have - header_len : 0;
        while (body_have < body_need) {
            ssize_t n = recv_some(conn_fd, buf + header_len + body_have,
                                  body_need - body_have);
            if (n <= 0)
                goto done;
            body_have += (size_t)n;
            have = header_len + body_have;
        }
        body = buf + header_len;
        body[body_need] = '\0';

        /* 3a) GET → optional URI handler (tunnel JPEG). One-shot: it writes its
         *     own response and owns the fd, so we always close after. */
        {
            char method[16] = {0};
            char uri[256] = {0};
            if (sscanf(reqline, "%15s %255s", method, uri) == 2 &&
                strcasecmp(method, "GET") == 0 && server->uri_handler) {
                if (server->uri_handler(server->uri_ctx, conn_fd, method, uri))
                    goto done;
            }
        }

        /* 3a') 派发前先探对端是否已关闭:GUI 超时/放弃后会关连接(FIN),此时再跑派发
         *      (可能进 8s 级的相机透传/等图)纯属浪费,还会把"已死请求"堆进 worker/锁里加剧
         *      拥塞。非阻塞 MSG_PEEK 读到 0 = 对端已发 FIN → 直接清理本连接(worker_loop close),
         *      不派发。>0(pipelined 下一请求)或 EAGAIN(无多余字节、连接仍在)则继续。
         *      前提:本机 8089 客户端(GUI/隧道/BLE)均全双工 keep-alive,不做"发完即半关写端仍等回复",
         *      故读端 EOF 即代表对端整体已弃用本连接,可安全丢弃。 */
        {
            char probe;
            ssize_t pk = recv(conn_fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
            if (pk == 0)
                goto done;   /* peer closed after sending request → drop & cleanup */
        }

        /* 3b) POST JSON dispatch. */
        if (body_need == 0) {
            if (send_http_response_ka(conn_fd, 400, "Bad Request",
                    "{\"statusCode\":400,\"statusMsg\":\"empty body\"}", keep_alive) != 0)
                goto done;
        } else if (server->handler) {
            resp = server->handler(server->handler_ctx, body);
            if (resp) {
                int w = send_http_response_ka(conn_fd, 200, "OK", resp, keep_alive);
                free(resp);
                if (w != 0) goto done;
            } else if (send_http_response_ka(conn_fd, 500, "Internal Server Error",
                                             "{\"statusCode\":500}", keep_alive) != 0) {
                goto done;
            }
        } else {
            char *response = NULL;
            if (nop_app_dispatch(server->app, body, &response) == NOP_OK && response) {
                int w = send_http_response_ka(conn_fd, 200, "OK", response, keep_alive);
                nop_app_free_response(response);
                if (w != 0) goto done;
            } else if (send_http_response_ka(conn_fd, 500, "Internal Server Error",
                                             "{\"statusCode\":500}", keep_alive) != 0) {
                goto done;
            }
        }

        if (!keep_alive || ++served >= NOP_HTTP_KEEPALIVE_MAX)
            goto done;

        /* 4) Carry any pipelined bytes past this request to the front of buf. */
        {
            size_t consumed = header_len + body_need;
            size_t leftover = have > consumed ? have - consumed : 0;
            if (leftover)
                memmove(buf, buf + consumed, leftover);
            have = leftover;
        }
    }

done:
    free(buf);
}

/* Hand an accepted connection to the worker pool. Non-blocking: returns 0 when
 * queued, -1 when the pool is saturated (queue full) so the listener can shed
 * load instead of stalling the accept path. */
static int enqueue_conn(nop_http_server_t *server, int conn_fd)
{
    pthread_mutex_lock(&server->q_lock);
    if (server->stop || server->q_count == NOP_HTTP_CONN_QUEUE) {
        pthread_mutex_unlock(&server->q_lock);
        return -1;
    }
    server->conn_q[server->q_tail] = conn_fd;
    server->q_tail = (server->q_tail + 1) % NOP_HTTP_CONN_QUEUE;
    server->q_count++;
    pthread_cond_signal(&server->q_nonempty);
    pthread_mutex_unlock(&server->q_lock);
    return 0;
}

/* One pool worker: dequeue a connection, serve it to completion, close, repeat.
 * Exits once stop is set and the queue has drained. */
static void *worker_loop(void *arg)
{
    nop_http_server_t *server = (nop_http_server_t *)arg;
    for (;;) {
        int conn_fd;

        pthread_mutex_lock(&server->q_lock);
        while (server->q_count == 0 && !server->stop)
            pthread_cond_wait(&server->q_nonempty, &server->q_lock);
        if (server->q_count == 0 && server->stop) {
            pthread_mutex_unlock(&server->q_lock);
            break;
        }
        conn_fd = server->conn_q[server->q_head];
        server->q_head = (server->q_head + 1) % NOP_HTTP_CONN_QUEUE;
        server->q_count--;
        pthread_cond_signal(&server->q_nonfull);
        pthread_mutex_unlock(&server->q_lock);

        serve_connection(server, conn_fd);
        close(conn_fd);
    }
    return NULL;
}

/* Listener: accept connections and hand them to the pool. Never runs a handler
 * itself, so a slow request cannot stall the accept path. */
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
            /* Bound every accepted socket so no worker can be pinned by a slow or
             * half-open peer (root cause of the CLOSE_WAIT pileup / 8089 wedge). */
            {
                struct timeval to = { NOP_HTTP_SOCK_TIMEOUT_S, 0 };
                int one = 1;
                setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
                setsockopt(conn_fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
                setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            }
            /* Overload → shed by closing immediately. The listener must NEVER do
             * a (potentially blocking) write, or a misbehaving client could stall
             * accept(); the peer sees the close and retries. */
            if (enqueue_conn(server, conn_fd) != 0)
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

    /* Bring up the worker pool before the listener starts enqueueing. */
    pthread_mutex_init(&server->q_lock, NULL);
    pthread_cond_init(&server->q_nonempty, NULL);
    pthread_cond_init(&server->q_nonfull, NULL);
    {
        pthread_attr_t attr;
        pthread_attr_t *pattr = NULL;
        if (pthread_attr_init(&attr) == 0) {
            pthread_attr_setstacksize(&attr, NOP_HTTP_WORKER_STACK);
            pattr = &attr;
        }
        for (server->num_workers = 0; server->num_workers < NOP_HTTP_MAX_WORKERS;
             server->num_workers++) {
            if (pthread_create(&server->workers[server->num_workers], pattr,
                               worker_loop, server) != 0) {
                fprintf(stderr, "[nop_http] worker pthread: %s\n", strerror(errno));
                /* Need at least one worker; unwind whatever we started. */
                if (server->num_workers == 0) {
                    if (pattr) pthread_attr_destroy(pattr);
                    pthread_mutex_destroy(&server->q_lock);
                    pthread_cond_destroy(&server->q_nonempty);
                    pthread_cond_destroy(&server->q_nonfull);
                    goto fail;
                }
                break;      /* run with fewer workers than requested */
            }
        }
        if (pattr)
            pthread_attr_destroy(pattr);
    }

    if (pthread_create(&server->thread, NULL, serve_loop, server) != 0) {
        fprintf(stderr, "[nop_http] listener pthread: %s\n", strerror(errno));
        server->stop = 1;
        pthread_cond_broadcast(&server->q_nonempty);
        pthread_cond_broadcast(&server->q_nonfull);
        { int i; for (i = 0; i < server->num_workers; i++)
                     pthread_join(server->workers[i], NULL); }
        pthread_mutex_destroy(&server->q_lock);
        pthread_cond_destroy(&server->q_nonempty);
        pthread_cond_destroy(&server->q_nonfull);
        goto fail;
    }
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
    int i;

    if (!server)
        return;
    server->stop = 1;
    /* Wake the listener (blocked in enqueue) and every worker (blocked on an
     * empty queue) so they observe stop and exit. */
    pthread_mutex_lock(&server->q_lock);
    pthread_cond_broadcast(&server->q_nonempty);
    pthread_cond_broadcast(&server->q_nonfull);
    pthread_mutex_unlock(&server->q_lock);

    if (server->thread_started)
        pthread_join(server->thread, NULL);           /* listener first */
    for (i = 0; i < server->num_workers; i++)
        pthread_join(server->workers[i], NULL);

    /* Close any connections still queued at shutdown. */
    while (server->q_count > 0) {
        close(server->conn_q[server->q_head]);
        server->q_head = (server->q_head + 1) % NOP_HTTP_CONN_QUEUE;
        server->q_count--;
    }
    pthread_mutex_destroy(&server->q_lock);
    pthread_cond_destroy(&server->q_nonempty);
    pthread_cond_destroy(&server->q_nonfull);

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
void nop_http_server_set_handler(nop_http_server_t *server,
                                 nop_http_handler_fn handler, void *ctx)
{ (void)server; (void)handler; (void)ctx; }
void nop_http_server_set_uri_handler(nop_http_server_t *server,
                                     nop_http_uri_handler_fn handler, void *ctx)
{ (void)server; (void)handler; (void)ctx; }
int nop_http_send_response(int fd, int status, const char *status_text,
                           const char *content_type,
                           const void *body, size_t body_len)
{ (void)fd; (void)status; (void)status_text; (void)content_type; (void)body; (void)body_len; return -1; }

#endif
