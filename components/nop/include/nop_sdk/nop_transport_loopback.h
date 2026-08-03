/**
 * @file nop_transport_loopback.h
 * @brief In-process NOP client channel: the client talks directly to a
 *        nop_app_t server via nop_app_dispatch — no sockets, no serialization
 *        beyond the envelope. Use when the UI and the NOP backend share a
 *        process, or for host-side tests of the full command surface.
 */
#ifndef NOP_SDK_TRANSPORT_LOOPBACK_H
#define NOP_SDK_TRANSPORT_LOOPBACK_H

#include "nop_sdk/nop_client.h"
#include "nop_sdk/nop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize @p channel to route requests straight into @p app. @p app must
 * outlive any client using the channel. The channel holds no resources of its
 * own (nothing to close).
 */
void nop_channel_loopback_init(nop_client_channel_if *channel, nop_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_TRANSPORT_LOOPBACK_H */
