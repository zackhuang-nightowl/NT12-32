/**
 * @file nop_event8012.h
 * @brief Camera-role 8012 event center — the TCP server a camera runs so an
 *        XVR/NVR can log in and receive its alarm events + JPEG snapshots.
 *
 * Role: CAMERA (NOP_ROLE_IPC). The NVR side is the *client* of this. Not started
 * in standalone mode. Wire protocol (per nop_doc/camera/8012事件中心.md):
 *   - Fixed TCP port 8012, long connection + 30s heartbeat.
 *   - 40-byte little-endian packed cmdHeader {magic=0x1AA1B22C, version, dataSize,
 *     cmd, timestamp, msgType, extendDataFlag, reserved[3]} + optional payload.
 *   - cmd: LOGIN=0, HEARTBEAT=1, ACK_OK=2, ACK_FAIL=3, SEND_MSG=4, CLOSE=5.
 *   - XVR sends LOGIN (+64-byte username/password); camera replies ACK_OK or
 *     ACK_FAIL(+disconnect). Only login is acked. Camera pushes SEND_MSG with
 *     msgType (2 Human, 30316 Animal, …) and extendDataFlag=1 + JPEG payload.
 *
 * Subscribes to a nop_event_hub; every published event goes to logged-in XVRs.
 * POSIX only (TCP + pthreads).
 */
#ifndef NOP_SDK_EVENT8012_H
#define NOP_SDK_EVENT8012_H

#include "nop_sdk/nop_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque 8012 server handle. */
typedef struct nop_event8012_server nop_event8012_server_t;

/** Configuration. Zero-initialize; hub is required. */
typedef struct nop_event8012_config {
    int              port;        /**< 0 => 8012 */
    nop_event_hub_t *hub;         /**< event source (required) */
    char             username[32];/**< login user (default "admin" if empty) */
    char             password[64];/**< login password (default "admin"; use ownerId when set) */
} nop_event8012_config_t;

/**
 * Start the 8012 server: bind/listen, subscribe to @p config->hub, and push
 * every event to logged-in clients. Returns NULL on failure. @p config->hub
 * must outlive the server.
 */
nop_event8012_server_t *nop_event8012_server_start(const nop_event8012_config_t *config);

/** Stop the server and drop all client sessions. NULL-safe. */
void nop_event8012_server_stop(nop_event8012_server_t *server);

/** @return bound port, or 0 if @p server is NULL. */
int nop_event8012_server_port(const nop_event8012_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_EVENT8012_H */
