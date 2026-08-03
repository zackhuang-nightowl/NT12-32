/**
 * @file nop_transport_unix.h
 * @brief Cross-process NOP over a Unix-domain socket — the real path for an
 *        on-NVR LVGL UI process talking to the NOP backend process.
 *
 * Wire framing: each message is a 4-byte big-endian length prefix followed by
 * that many bytes of request/response envelope JSON. Server side accepts one
 * client at a time (the UI), reads a framed request, runs nop_app_dispatch, and
 * writes the framed response.
 *
 * POSIX only (AF_UNIX + pthreads). On the backend:
 *   nop_unix_server_t *srv = nop_unix_server_start("/run/nop.sock", app);
 *   ... ; nop_unix_server_stop(srv);
 * In the LVGL UI process:
 *   nop_client_channel_if ch;
 *   nop_channel_unix_connect(&ch, "/run/nop.sock");
 *   nop_client_t *cli = nop_client_create(&ch);
 *   ... ; nop_client_destroy(cli); nop_channel_unix_close(&ch);
 */
#ifndef NOP_SDK_TRANSPORT_UNIX_H
#define NOP_SDK_TRANSPORT_UNIX_H

#include "nop_sdk/nop_client.h"
#include "nop_sdk/nop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- server (NVR backend) ------------------------------------------------- */

/** Opaque background server handle. */
typedef struct nop_unix_server nop_unix_server_t;

/**
 * Bind @p socket_path, listen, and serve framed NOP requests into @p app on a
 * background thread. Replaces any stale socket file at the path. @p app must
 * outlive the server. Returns NULL on failure (bind/listen/thread).
 */
nop_unix_server_t *nop_unix_server_start(const char *socket_path, nop_app_t *app);

/** Stop the server thread, close the socket, and unlink the path. NULL-safe. */
void nop_unix_server_stop(nop_unix_server_t *server);

/* ---- client (UI process) -------------------------------------------------- */

/**
 * Connect to a NOP server at @p socket_path and initialize @p channel for use
 * with nop_client_create(). @return 0 on success, non-zero on failure.
 */
int nop_channel_unix_connect(nop_client_channel_if *channel, const char *socket_path);

/** Close the channel's socket and release its state. NULL-safe. */
void nop_channel_unix_close(nop_client_channel_if *channel);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_TRANSPORT_UNIX_H */
