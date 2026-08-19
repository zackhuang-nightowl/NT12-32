/**
 * @file nop_http_server.h
 * @brief Inbound NOP-over-HTTP server (port 8089 by convention) — how the
 *        mobile app / cloud reaches the device's NOP command surface.
 *
 * Accepts HTTP/1.1 from localhost (LVGL) and the on-box TUTK agent. External
 * App traffic reaches this port only via TUTK port mapping, not by direct WAN
 * access. Typical POST body is a NOP JSON envelope; GET `/eventSnap` is a
 * tunnel-only JPEG shortcut and does not change the POST JSON path.
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
 * Optional GET interceptor. Invoked only for HTTP GET (POST /APPJsonCmd is
 * unchanged). Return 1 if the response was written to @p fd; 0 to fall through.
 * Used for tunnel-only assets such as event JPEG (`/eventSnap?eid=`).
 */
typedef int (*nop_http_uri_handler_fn)(void *ctx, int fd,
                                       const char *method, const char *uri);
void nop_http_server_set_uri_handler(nop_http_server_t *server,
                                     nop_http_uri_handler_fn handler, void *ctx);

/** Write a complete HTTP/1.1 response (JSON or JPEG). */
int nop_http_send_response(int fd, int status, const char *status_text,
                           const char *content_type,
                           const void *body, size_t body_len);

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
