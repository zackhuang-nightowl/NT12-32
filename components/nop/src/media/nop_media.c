/**
 * @file nop_media.c
 * @brief Media frame-source hub: fan one published frame out to every sink.
 *        Thread-safe (firmware encoder thread publishes; control thread
 *        subscribes). See nop_sdk/nop_media.h.
 */
#include "nop_sdk/nop_media.h"
#include "nop_sdk/hal/hal_registry.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>

#ifndef NOP_MEDIA_MAX_SINKS
#  define NOP_MEDIA_MAX_SINKS 8
#endif

struct nop_media_subscription {
    nop_media_sink_fn sink;
    void             *sink_ctx;
    int               active;
};

struct nop_media {
    osal_mutex_t                   *mutex;
    struct nop_media_subscription   sinks[NOP_MEDIA_MAX_SINKS];
    int                             bound_to_hal;
    int                             publishing;   /* in-flight publish() count */
};

nop_media_t *nop_media_create(void)
{
    nop_media_t *media = (nop_media_t *)nop_calloc(1, sizeof(*media));
    if (!media)
        return NULL;
    media->mutex = osal_mutex_create();
    if (!media->mutex) {
        nop_free(media);
        return NULL;
    }
    return media;
}

void nop_media_destroy(nop_media_t *media)
{
    if (!media)
        return;
    nop_media_unbind_hal_video(media);
    osal_mutex_destroy(media->mutex);
    nop_free(media);
}

nop_media_subscription_t *nop_media_subscribe(nop_media_t *media,
                                              nop_media_sink_fn sink,
                                              void *sink_ctx)
{
    nop_media_subscription_t *result = NULL;
    int i;
    if (!media || !sink)
        return NULL;
    osal_mutex_lock(media->mutex);
    for (i = 0; i < NOP_MEDIA_MAX_SINKS; i++) {
        if (!media->sinks[i].active) {
            media->sinks[i].sink     = sink;
            media->sinks[i].sink_ctx = sink_ctx;
            media->sinks[i].active   = 1;
            result = &media->sinks[i];
            break;
        }
    }
    osal_mutex_unlock(media->mutex);
    return result;
}

void nop_media_unsubscribe(nop_media_t *media, nop_media_subscription_t *subscription)
{
    if (!media || !subscription)
        return;
    osal_mutex_lock(media->mutex);
    subscription->active   = 0;
    subscription->sink     = NULL;
    subscription->sink_ctx = NULL;
    /* Barrier: wait for any in-flight publish() (which snapshotted this sink
     * before the flag cleared and invokes outside the lock) so the caller can
     * free the sink's context immediately after unsubscribe returns. */
    while (media->publishing > 0) {
        osal_mutex_unlock(media->mutex);
        osal_sleep_ms(1);
        osal_mutex_lock(media->mutex);
    }
    osal_mutex_unlock(media->mutex);
}

void nop_media_publish(nop_media_t *media, int channel, const hal_video_frame_t *frame)
{
    /* Snapshot the sink table under the lock, then invoke outside it so a slow
     * sink can't block the encoder thread from other locking, and a sink may
     * (un)subscribe re-entrantly without deadlock. */
    nop_media_sink_fn sinks[NOP_MEDIA_MAX_SINKS];
    void             *ctxs[NOP_MEDIA_MAX_SINKS];
    int               count = 0, i;

    if (!media || !frame)
        return;
    osal_mutex_lock(media->mutex);
    for (i = 0; i < NOP_MEDIA_MAX_SINKS; i++) {
        if (media->sinks[i].active) {
            sinks[count] = media->sinks[i].sink;
            ctxs[count]  = media->sinks[i].sink_ctx;
            count++;
        }
    }
    media->publishing++;               /* keep unsubscribe waiting for us */
    osal_mutex_unlock(media->mutex);

    for (i = 0; i < count; i++)
        sinks[i](ctxs[i], channel, frame);

    osal_mutex_lock(media->mutex);
    media->publishing--;
    osal_mutex_unlock(media->mutex);
}

/* HAL_VIDEO frame sink trampoline: sink_ctx is the nop_media_t*. Matches
 * hal_video_frame_sink_fn. */
static void media_hal_video_sink(void *sink_ctx, int channel,
                                 const hal_video_frame_t *frame)
{
    nop_media_publish((nop_media_t *)sink_ctx, channel, frame);
}

nop_status_t nop_media_bind_hal_video(nop_media_t *media)
{
    const hal_video_if *video;
    nop_status_t        status;
    if (!media)
        return NOP_ERR_PARAM;
    video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    if (!video || !video->set_frame_sink)
        return NOP_ERR_NOTIMPL;
    status = video->set_frame_sink(video->ctx, media_hal_video_sink, media);
    if (status == NOP_OK)
        media->bound_to_hal = 1;
    return status;
}

void nop_media_unbind_hal_video(nop_media_t *media)
{
    const hal_video_if *video;
    if (!media || !media->bound_to_hal)
        return;
    video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    if (video && video->set_frame_sink)
        video->set_frame_sink(video->ctx, NULL, NULL);
    media->bound_to_hal = 0;
}

int nop_media_subscriber_count(const nop_media_t *media)
{
    int count = 0, i;
    if (!media)
        return 0;
    /* Read-only count; the small race vs concurrent (un)subscribe is benign for
     * diagnostics. */
    for (i = 0; i < NOP_MEDIA_MAX_SINKS; i++)
        if (media->sinks[i].active)
            count++;
    return count;
}
