/**
 * @file svc_push.c
 * @brief Push engine: subscribe the event hub, apply the push policy (per
 *        (channel,type) silent window + global rate limit, doorbellRing
 *        exempt), and invoke the integrator's delivery callback when allowed.
 *        See nop_sdk/nop_push.h.
 */
#include "nop_sdk/nop_push.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>

#ifndef NOP_PUSH_MAX_CHANNELS
#  define NOP_PUSH_MAX_CHANNELS 16
#endif

struct nop_push {
    nop_event_hub_t          *hub;
    nop_event_subscription_t *subscription;
    nop_push_send_fn          send;
    void                     *send_ctx;
    int                       min_interval_ms;
    int                       silent_window_ms;
    osal_mutex_t             *mutex;
    /* last-send monotonic ms, per (channel, detection type); 0 = never. */
    uint64_t                  last_sent_ms[NOP_PUSH_MAX_CHANNELS][NOP_DETECT_TYPE_MAX];
    uint64_t                  last_global_ms;
};

static int channel_index(int channel)
{
    return (channel < 0 || channel >= NOP_PUSH_MAX_CHANNELS) ? 0 : channel;
}

/* Elapsed now - since, clamped to 0 so a monotonic-clock regression can't
 * underflow the unsigned subtraction and silently defeat the windows. */
static uint64_t elapsed_ms(uint64_t now, uint64_t since)
{
    return now >= since ? now - since : 0;
}

/* Policy gate: returns 1 if this event should be pushed now. */
static int policy_allows(nop_push_t *push, const nop_event_t *event, uint64_t now_ms)
{
    int allow = 1;
    int is_doorbell = (event->type == NOP_DETECT_DOORBELL_RING);
    int ch = channel_index(event->channel);
    int type = (event->type >= 0 && event->type < NOP_DETECT_TYPE_MAX) ? (int)event->type : 0;

    osal_mutex_lock(push->mutex);
    /* doorbellRing is the highest-priority event and is fully exempt from both
     * throttles — every ring must push. */
    if (!is_doorbell) {
        /* Per-type silent window; silent_window_ms <= 0 disables dedup. */
        if (push->silent_window_ms > 0) {
            uint64_t last = push->last_sent_ms[ch][type];
            if (last != 0 && elapsed_ms(now_ms, last) < (uint64_t)push->silent_window_ms)
                allow = 0;
        }
        /* Global rate limit so the server does not discard bursts.
         * min_interval_ms <= 0 disables the limit. */
        if (allow && push->min_interval_ms > 0 && push->last_global_ms != 0 &&
            elapsed_ms(now_ms, push->last_global_ms) < (uint64_t)push->min_interval_ms)
            allow = 0;
    }
    if (allow) {
        push->last_sent_ms[ch][type] = now_ms;
        push->last_global_ms = now_ms;
    }
    osal_mutex_unlock(push->mutex);
    return allow;
}

static void push_sink(void *sink_ctx, const nop_event_t *event)
{
    nop_push_t *push = (nop_push_t *)sink_ctx;
    if (policy_allows(push, event, osal_time_ms()))
        push->send(push->send_ctx, event);
}

nop_push_t *nop_push_start(const nop_push_config_t *config)
{
    nop_push_t *push;
    if (!config || !config->hub || !config->send)
        return NULL;
    push = (nop_push_t *)nop_calloc(1, sizeof(*push));
    if (!push)
        return NULL;
    push->hub              = config->hub;
    push->send             = config->send;
    push->send_ctx         = config->send_ctx;
    /* 0 disables the respective control (see header). */
    push->min_interval_ms  = config->min_interval_ms;
    push->silent_window_ms = config->silent_window_ms;
    push->mutex = osal_mutex_create();
    if (!push->mutex) {
        nop_free(push);
        return NULL;
    }
    push->subscription = nop_event_subscribe(push->hub, push_sink, push);
    if (!push->subscription) {
        osal_mutex_destroy(push->mutex);
        nop_free(push);
        return NULL;
    }
    return push;
}

void nop_push_stop(nop_push_t *push)
{
    if (!push)
        return;
    nop_event_unsubscribe(push->hub, push->subscription);
    osal_mutex_destroy(push->mutex);
    nop_free(push);
}
