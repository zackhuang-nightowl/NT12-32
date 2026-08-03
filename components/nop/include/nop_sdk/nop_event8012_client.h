/**
 * @file nop_event8012_client.h
 * @brief videoRecorder-role 8012 event-center client — the NVR side that logs
 *        into a camera's 8012 server (nop_event8012.h) and receives its alarm
 *        events + JPEG snapshots.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR). Mirrors the server wire protocol: sends a
 * 40-byte LE cmdHeader LOGIN (+64-byte user/pass), expects ACK_OK, then sends a
 * HEARTBEAT every ~30s and reads SEND_MSG events (msgType + optional JPEG),
 * handing each to a callback. One client instance per attached camera.
 * POSIX only (TCP + pthreads).
 */
#ifndef NOP_SDK_EVENT8012_CLIENT_H
#define NOP_SDK_EVENT8012_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque client handle. */
typedef struct nop_event8012_client nop_event8012_client_t;

/**
 * Received event callback. @p msg_type is the 8012 numeric code (2 Human,
 * 30316 Animal, …). @p extend_flag is the payload kind: 1 = @p data is a raw
 * JPEG; 2 = @p data is a sequence of extendDataUnit blocks (mixed JPEG + JSON,
 * e.g. a lineCross name) the caller must parse; 0 = no payload. @p data/@p len
 * is borrowed for the call only. @p ctx is opaque caller state.
 */
typedef void (*nop_event8012_event_fn)(void *ctx, uint32_t msg_type,
                                       uint32_t extend_flag,
                                       const uint8_t *data, size_t len);

/** Configuration. Zero-initialize; host is required. */
typedef struct nop_event8012_client_config {
    char                   host[64];      /**< camera IP/host (required) */
    int                    port;          /**< 0 => 8012 */
    char                   username[32];  /**< default "admin" if empty */
    char                   password[64];  /**< default "admin"; ownerId when set */
    nop_event8012_event_fn on_event;      /**< received-event sink (may be NULL) */
    void                  *ctx;
} nop_event8012_client_config_t;

/**
 * Connect to the camera's 8012 server, log in, and receive events on a
 * background thread (auto-heartbeat). Returns NULL on failure (connect/login/
 * thread). The connection is not auto-reconnected in this version.
 */
nop_event8012_client_t *nop_event8012_client_start(const nop_event8012_client_config_t *config);

/** Stop the client and close the connection. NULL-safe. */
void nop_event8012_client_stop(nop_event8012_client_t *client);

/** @return non-zero if currently connected + logged in. */
int nop_event8012_client_is_connected(const nop_event8012_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_EVENT8012_CLIENT_H */
