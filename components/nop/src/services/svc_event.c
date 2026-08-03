/**
 * @file svc_event.c
 * @brief Detection-event hub: fan a published event out to every subscriber.
 *        Thread-safe (detection thread publishes; control threads subscribe).
 *        See nop_sdk/nop_event.h.
 */
#include "nop_sdk/nop_event.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>

#ifndef NOP_EVENT_MAX_SINKS
#  define NOP_EVENT_MAX_SINKS 8
#endif
#ifndef NOP_EVENT_STORE_SIZE
#  define NOP_EVENT_STORE_SIZE 64      /* recent-event ring for queryEventList */
#endif

struct nop_event_subscription {
    nop_event_sink_fn sink;
    void             *sink_ctx;
    int               active;
};

/* Stored event: metadata only (no payload pointers retained). */
typedef struct nop_event_record {
    int               channel;
    nop_detect_type_t type;
    uint64_t          timestamp_ms;
} nop_event_record_t;

struct nop_event_hub {
    osal_mutex_t                  *mutex;
    struct nop_event_subscription  sinks[NOP_EVENT_MAX_SINKS];
    nop_event_record_t             store[NOP_EVENT_STORE_SIZE];
    int                            store_count;   /* total ever stored */
    int                            store_head;    /* next write slot */
    int                            publishing;    /* in-flight publish() count */
};

nop_event_hub_t *nop_event_hub_create(void)
{
    nop_event_hub_t *hub = (nop_event_hub_t *)nop_calloc(1, sizeof(*hub));
    if (!hub)
        return NULL;
    hub->mutex = osal_mutex_create();
    if (!hub->mutex) {
        nop_free(hub);
        return NULL;
    }
    return hub;
}

void nop_event_hub_destroy(nop_event_hub_t *hub)
{
    if (!hub)
        return;
    osal_mutex_destroy(hub->mutex);
    nop_free(hub);
}

nop_event_subscription_t *nop_event_subscribe(nop_event_hub_t *hub,
                                              nop_event_sink_fn sink, void *sink_ctx)
{
    nop_event_subscription_t *result = NULL;
    int i;
    if (!hub || !sink)
        return NULL;
    osal_mutex_lock(hub->mutex);
    for (i = 0; i < NOP_EVENT_MAX_SINKS; i++) {
        if (!hub->sinks[i].active) {
            hub->sinks[i].sink     = sink;
            hub->sinks[i].sink_ctx = sink_ctx;
            hub->sinks[i].active   = 1;
            result = &hub->sinks[i];
            break;
        }
    }
    osal_mutex_unlock(hub->mutex);
    return result;
}

void nop_event_unsubscribe(nop_event_hub_t *hub, nop_event_subscription_t *subscription)
{
    if (!hub || !subscription)
        return;
    osal_mutex_lock(hub->mutex);
    subscription->active   = 0;
    subscription->sink     = NULL;
    subscription->sink_ctx = NULL;
    /* Barrier: an in-flight publish() may have snapshotted this sink before we
     * cleared it and be about to invoke it (the invoke runs outside the lock).
     * Wait for all in-flight publishes to finish so the caller can safely free
     * the subscriber's context immediately after unsubscribe returns. New
     * publishes started after this point no longer see this sink. */
    while (hub->publishing > 0) {
        osal_mutex_unlock(hub->mutex);
        osal_sleep_ms(1);
        osal_mutex_lock(hub->mutex);
    }
    osal_mutex_unlock(hub->mutex);
}

void nop_event_publish(nop_event_hub_t *hub, const nop_event_t *event)
{
    /* Snapshot the sink table under the lock, invoke outside it (a slow sink
     * must not block others, and a sink may (un)subscribe re-entrantly). */
    nop_event_sink_fn sinks[NOP_EVENT_MAX_SINKS];
    void             *ctxs[NOP_EVENT_MAX_SINKS];
    int               count = 0, i;

    if (!hub || !event)
        return;
    osal_mutex_lock(hub->mutex);
    /* Record into the recent-event ring (metadata only). */
    hub->store[hub->store_head].channel      = event->channel;
    hub->store[hub->store_head].type         = event->type;
    hub->store[hub->store_head].timestamp_ms = event->timestamp_ms;
    hub->store_head = (hub->store_head + 1) % NOP_EVENT_STORE_SIZE;
    if (hub->store_count < NOP_EVENT_STORE_SIZE)
        hub->store_count++;
    for (i = 0; i < NOP_EVENT_MAX_SINKS; i++) {
        if (hub->sinks[i].active) {
            sinks[count] = hub->sinks[i].sink;
            ctxs[count]  = hub->sinks[i].sink_ctx;
            count++;
        }
    }
    hub->publishing++;                 /* keep unsubscribe waiting for us */
    osal_mutex_unlock(hub->mutex);

    for (i = 0; i < count; i++)
        sinks[i](ctxs[i], event);

    osal_mutex_lock(hub->mutex);
    hub->publishing--;
    osal_mutex_unlock(hub->mutex);
}

int nop_event_subscriber_count(const nop_event_hub_t *hub)
{
    int count = 0, i;
    if (!hub)
        return 0;
    for (i = 0; i < NOP_EVENT_MAX_SINKS; i++)
        if (hub->sinks[i].active)
            count++;
    return count;
}

int nop_event_hub_recent(nop_event_hub_t *hub, nop_event_t *out, int max)
{
    int written = 0, i;
    if (!hub || !out || max <= 0)
        return 0;
    osal_mutex_lock(hub->mutex);
    /* Walk newest-first from the slot before head. */
    for (i = 0; i < hub->store_count && written < max; i++) {
        int idx = (hub->store_head - 1 - i + NOP_EVENT_STORE_SIZE) % NOP_EVENT_STORE_SIZE;
        memset(&out[written], 0, sizeof(out[written]));
        out[written].channel      = hub->store[idx].channel;
        out[written].type         = hub->store[idx].type;
        out[written].timestamp_ms = hub->store[idx].timestamp_ms;
        written++;
    }
    osal_mutex_unlock(hub->mutex);
    return written;
}

uint32_t nop_event_msgtype_code(nop_detect_type_t type)
{
    switch (type) {
    case NOP_DETECT_MOTION:          return 1;
    case NOP_DETECT_HUMAN:           return 2;
    case NOP_DETECT_FACE:            return 3;
    case NOP_DETECT_VEHICLE:         return 30305;
    case NOP_DETECT_DOORBELL_RING:   return 30312;
    case NOP_DETECT_ANIMAL:          return 30316;
    case NOP_DETECT_PACKAGE:         return 30317;
    case NOP_DETECT_LINE_CROSS:      return 30103;
    case NOP_DETECT_FIELD_INTRUSION: return 30104;
    default:                         return 0;
    }
}
