/**
 * @file nop_media.h
 * @brief Media frame-source plane — decouples the encoded-frame producer
 *        (hal_video) from the stream consumers (RTSP, TUTK P2P, HTTP-FLV, …).
 *
 * One frame published by firmware is fanned out to every subscribed sink, so a
 * single encode feeds RTSP and P2P at once (per the v3 architecture: "帧源订阅,
 * 不直连 hal; hal_video 同一份帧同时供 RTSP 与 P2P"). The media hub owns no
 * codecs and copies no frames — it just distributes the borrowed frame pointer
 * to sinks synchronously; a sink that must retain data copies it.
 *
 * Wiring:
 *   nop_media_t *media = nop_media_create();
 *   nop_media_bind_hal_video(media);            // HAL_VIDEO frames -> hub
 *   nop_media_subscribe(media, my_rtsp_sink, rtsp_ctx);
 *   nop_media_subscribe(media, my_tutk_sink, tutk_ctx);
 *   // firmware now calls the installed sink per frame; both subscribers see it
 */
#ifndef NOP_SDK_MEDIA_H
#define NOP_SDK_MEDIA_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/hal/hal_video.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque media hub. */
typedef struct nop_media nop_media_t;

/** Opaque subscription handle (returned by subscribe, passed to unsubscribe). */
typedef struct nop_media_subscription nop_media_subscription_t;

/**
 * Sink callback. Invoked once per published frame, on the producer's thread.
 * @p frame (and its data) is only valid for the call; copy to retain.
 */
typedef void (*nop_media_sink_fn)(void *sink_ctx, int channel,
                                  const hal_video_frame_t *frame);

/** Create an empty media hub. Returns NULL on failure. */
nop_media_t *nop_media_create(void);

/** Destroy the hub and drop all subscriptions. NULL-safe. */
void nop_media_destroy(nop_media_t *media);

/**
 * Subscribe @p sink (called for every future frame). Returns a handle, or NULL
 * on failure / when the hub is full. @p sink_ctx is passed back unchanged.
 */
nop_media_subscription_t *nop_media_subscribe(nop_media_t *media,
                                              nop_media_sink_fn sink,
                                              void *sink_ctx);

/** Cancel a subscription. NULL-safe on either argument. */
void nop_media_unsubscribe(nop_media_t *media, nop_media_subscription_t *subscription);

/**
 * Publish @p frame on @p channel to all current subscribers. This is what the
 * HAL frame sink calls; firmware/tests may also call it directly.
 */
void nop_media_publish(nop_media_t *media, int channel, const hal_video_frame_t *frame);

/**
 * Install the hub as the HAL_VIDEO frame sink (frames published automatically).
 * @return NOP_OK, NOP_ERR_NOTIMPL if HAL_VIDEO/​set_frame_sink is absent, or
 * NOP_ERR_PARAM. Call nop_media_unbind_hal_video() before destroying the hub.
 */
nop_status_t nop_media_bind_hal_video(nop_media_t *media);

/** Detach the hub from HAL_VIDEO (sink set to NULL). NULL-safe. */
void nop_media_unbind_hal_video(nop_media_t *media);

/** @return current subscriber count (mainly for tests/diagnostics). */
int nop_media_subscriber_count(const nop_media_t *media);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_MEDIA_H */
