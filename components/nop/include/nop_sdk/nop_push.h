/**
 * @file nop_push.h
 * @brief Push engine — turns detection events into outbound push notifications
 *        under the documented policy (silent window per event type, global
 *        rate limit ≥1s, doorbellRing exempt). It is a nop_event_hub subscriber.
 *
 * Delivery itself (image upload + push server POST over HTTPS) is outbound and
 * supplied by the integrator via nop_push_send_fn — this module only decides
 * *whether* to send (policy), keeping the core free of a TLS dependency. Wire
 * the send callback to the outbound server-request layer.
 */
#ifndef NOP_SDK_PUSH_H
#define NOP_SDK_PUSH_H

#include "nop_sdk/nop_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque push engine. */
typedef struct nop_push nop_push_t;

/** Delivery callback: send @p event to the cloud (image upload + push). Called
 *  only when policy permits; @p event borrowed for the call. */
typedef void (*nop_push_send_fn)(void *ctx, const nop_event_t *event);

/** Configuration. Zero-initialize; hub + send are required. */
typedef struct nop_push_config {
    nop_event_hub_t *hub;              /**< event source (required) */
    nop_push_send_fn send;             /**< delivery callback (required) */
    void            *send_ctx;
    int              min_interval_ms;  /**< global min gap between sends; 0 disables (prod ≥1000) */
    int              silent_window_ms; /**< per (channel,type) dedup window; 0 disables (typ. 30000) */
} nop_push_config_t;

/** Start the push engine (subscribes to config->hub). NULL on failure. */
nop_push_t *nop_push_start(const nop_push_config_t *config);

/** Stop the engine (unsubscribes). NULL-safe. */
void nop_push_stop(nop_push_t *push);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_PUSH_H */
