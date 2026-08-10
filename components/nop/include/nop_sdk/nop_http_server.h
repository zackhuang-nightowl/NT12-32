/**
 * @file nop_http_server.h
 * @brief Inbound NOP-over-HTTP server (port 8089 by convention) — how the
 *        mobile app / cloud reaches the device's NOP command surface.
 *
 * Accepts HTTP/1.1 requests whose body is a NOP request envelope (typically
 * POST with a JSON body; the path is not significant), runs nop_app_dispatch,
 * and returns the response envelope as the HTTP response body
 * (Content-Type: application/json). Runs an accept/serve loop on a background
 * thread. POSIX only (TCP + pthreads).
 *
 *   nop_http_server_t *http = nop_http_server_start(8089, app);
 *   ... ; nop_http_server_stop(http);
 */
#ifndef NOP_SDK_HTTP_SERVER_H
#define NOP_SDK_HTTP_SERVER_H

#include "nop_sdk/nop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque HTTP server handle. */
typedef struct nop_http_server nop_http_server_t;

/**
 * Optional custom request handler. Given the request body, returns a malloc'd
 * response JSON string (server frees it with free()), or NULL on failure.
 * When set, it replaces the default nop_app_dispatch path — this lets the
 * single 8089 inbound server run app-level processing (display / channel
 * forward / nop fallback) instead of dispatching straight into nop_app.
 */
typedef char *(*nop_http_handler_fn)(void *ctx, const char *body);

/** Install a custom request handler (see nop_http_handler_fn). NULL-safe. */
void nop_http_server_set_handler(nop_http_server_t *server,
                                 nop_http_handler_fn handler, void *ctx);

/**
 * Bind @p port (0 selects the conventional 8089), listen, and serve NOP
 * requests into @p app on a background thread. @p app must outlive the server.
 * Returns NULL on failure (bind/listen/thread).
 */
nop_http_server_t *nop_http_server_start(int port, nop_app_t *app);

/** Stop the server thread and close the listening socket. NULL-safe. */
void nop_http_server_stop(nop_http_server_t *server);

/** @return the TCP port the server is bound to, or 0 if @p server is NULL. */
int nop_http_server_port(const nop_http_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HTTP_SERVER_H */
