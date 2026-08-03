/**
 * @file svc_light_linkage.c
 * @brief Camera-role light/siren/day-night linkage: detection → warning light +
 *        color mode with a hold timer; panic → light + siren. See
 *        nop_sdk/nop_light_linkage.h.
 */
#include "nop_sdk/nop_light_linkage.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_light.h"
#include "nop_sdk/hal/hal_video.h"
#include "nop_sdk/hal/hal_audio.h"
#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>

#ifndef NOP_LINKAGE_MAX_CHANNELS
#  define NOP_LINKAGE_MAX_CHANNELS 16
#endif

struct nop_light_linkage {
    nop_event_hub_t          *hub;
    nop_event_subscription_t *subscription;
    int                       light_seconds;
    int                       siren_seconds;
    osal_mutex_t             *mutex;
    /* per channel */
    int       light_on[NOP_LINKAGE_MAX_CHANNELS];
    uint64_t  off_deadline_ms[NOP_LINKAGE_MAX_CHANNELS];  /* 0 = no timed light */
    int       panic[NOP_LINKAGE_MAX_CHANNELS];
    pthread_t timer_thread;
    volatile int stop;
};

static int channel_index(int channel)
{
    return (channel < 0 || channel >= NOP_LINKAGE_MAX_CHANNELS) ? 0 : channel;
}

/* Turn the warning light on/off for @p channel and follow imaging mode. */
static void apply_light(int channel, int on)
{
    const hal_light_if *light = (const hal_light_if *)hal_registry_get(HAL_LIGHT);
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    if (light && light->set_switch)
        light->set_switch(light->ctx, channel, on);
    /* White light on → color for color night recording; off → auto/SmartIR. */
    if (video && video->set_day_night)
        video->set_day_night(video->ctx, channel,
                             on ? HAL_VIDEO_MODE_COLOR : HAL_VIDEO_MODE_AUTO);
}

static void apply_siren(int channel, int on, int seconds)
{
    const hal_audio_if *audio = (const hal_audio_if *)hal_registry_get(HAL_AUDIO);
    if (audio && audio->play_alert)
        audio->play_alert(audio->ctx, channel, 0, on ? seconds : 0);
}

/* Detection → light on + hold. */
static void linkage_sink(void *sink_ctx, const nop_event_t *event)
{
    nop_light_linkage_t *linkage = (nop_light_linkage_t *)sink_ctx;
    int ch = channel_index(event->channel);
    /* doorbellRing is not a "warning" trigger. */
    if (event->type == NOP_DETECT_DOORBELL_RING)
        return;
    osal_mutex_lock(linkage->mutex);
    if (!linkage->panic[ch]) {
        linkage->light_on[ch] = 1;
        linkage->off_deadline_ms[ch] = osal_time_ms() + (uint64_t)linkage->light_seconds * 1000;
        apply_light(ch, 1);
    }
    osal_mutex_unlock(linkage->mutex);
}

/* Auto-off timer: expire held lights (panic overrides). */
static void *timer_thread(void *arg)
{
    nop_light_linkage_t *linkage = (nop_light_linkage_t *)arg;
    while (!linkage->stop) {
        uint64_t now = osal_time_ms();
        int ch;
        osal_mutex_lock(linkage->mutex);
        for (ch = 0; ch < NOP_LINKAGE_MAX_CHANNELS; ch++) {
            if (linkage->light_on[ch] && !linkage->panic[ch] &&
                linkage->off_deadline_ms[ch] != 0 && now >= linkage->off_deadline_ms[ch]) {
                linkage->light_on[ch] = 0;
                linkage->off_deadline_ms[ch] = 0;
                apply_light(ch, 0);
            }
        }
        osal_mutex_unlock(linkage->mutex);
        usleep(100000);   /* 100 ms tick */
    }
    return NULL;
}

nop_light_linkage_t *nop_light_linkage_start(const nop_light_linkage_config_t *config)
{
    nop_light_linkage_t *linkage;
    if (!config || !config->hub)
        return NULL;
    linkage = (nop_light_linkage_t *)nop_calloc(1, sizeof(*linkage));
    if (!linkage)
        return NULL;
    linkage->hub           = config->hub;
    linkage->light_seconds = config->light_seconds > 0 ? config->light_seconds : 30;
    linkage->siren_seconds = config->siren_seconds > 0 ? config->siren_seconds : 30;
    linkage->mutex = osal_mutex_create();
    if (!linkage->mutex) {
        nop_free(linkage);
        return NULL;
    }
    linkage->subscription = nop_event_subscribe(linkage->hub, linkage_sink, linkage);
    if (!linkage->subscription) {
        osal_mutex_destroy(linkage->mutex);
        nop_free(linkage);
        return NULL;
    }
    if (pthread_create(&linkage->timer_thread, NULL, timer_thread, linkage) != 0) {
        nop_event_unsubscribe(linkage->hub, linkage->subscription);
        osal_mutex_destroy(linkage->mutex);
        nop_free(linkage);
        return NULL;
    }
    return linkage;
}

void nop_light_linkage_stop(nop_light_linkage_t *linkage)
{
    int ch;
    if (!linkage)
        return;
    linkage->stop = 1;
    pthread_join(linkage->timer_thread, NULL);
    nop_event_unsubscribe(linkage->hub, linkage->subscription);
    /* leave hardware in a safe state */
    for (ch = 0; ch < NOP_LINKAGE_MAX_CHANNELS; ch++) {
        if (linkage->light_on[ch] || linkage->panic[ch]) {
            apply_light(ch, 0);
            apply_siren(ch, 0, 0);
        }
    }
    osal_mutex_destroy(linkage->mutex);
    nop_free(linkage);
}

void nop_light_linkage_panic(nop_light_linkage_t *linkage, int channel, int enable)
{
    int ch;
    if (!linkage)
        return;
    ch = channel_index(channel);
    osal_mutex_lock(linkage->mutex);
    linkage->panic[ch] = enable ? 1 : 0;
    linkage->light_on[ch] = enable ? 1 : 0;
    linkage->off_deadline_ms[ch] = 0;   /* panic has no auto-off */
    apply_light(ch, enable);
    apply_siren(ch, enable, linkage->siren_seconds);
    osal_mutex_unlock(linkage->mutex);
}

#else /* non-POSIX stubs */
#include <stddef.h>
nop_light_linkage_t *nop_light_linkage_start(const nop_light_linkage_config_t *c){ (void)c; return NULL; }
void nop_light_linkage_stop(nop_light_linkage_t *l){ (void)l; }
void nop_light_linkage_panic(nop_light_linkage_t *l, int channel, int enable){ (void)l; (void)channel; (void)enable; }
#endif
