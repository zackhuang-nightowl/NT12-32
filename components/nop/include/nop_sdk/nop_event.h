/**
 * @file nop_event.h
 * @brief Detection-event hub — the shared spine of the event pipeline.
 *
 * Firmware (its detection component) publishes an event once via
 * nop_event_publish(); the hub fans it out to every subscriber. Subscribers are
 * the delivery back-ends: the camera-role 8012 event-center server
 * (nop_event8012.h), an ONVIF-events bridge, a NOP longPolling bridge, and the
 * outbound push engine. This decouples "something was detected" from "how it is
 * reported", so a detection integrates once and reaches all channels.
 *
 * Mirrors the nop_media hub pattern (create/subscribe/publish).
 */
#ifndef NOP_SDK_EVENT_H
#define NOP_SDK_EVENT_H

#include "nop_sdk/nop_detect_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One detection event. @p jpeg / @p extra_json are borrowed for the publish
 *  call only; a subscriber that must retain them copies. */
typedef struct nop_event {
    int               channel;
    nop_detect_type_t type;          /* human/vehicle/animal/lineCross/... */
    uint64_t          timestamp_ms;
    const uint8_t    *jpeg;          /* snapshot, extendDataFlag=1 (may be NULL) */
    size_t            jpeg_len;
    const char       *extra_json;    /* extendDataFlag=2, e.g. lineCross name (may be NULL) */
} nop_event_t;

/** Opaque event hub. */
typedef struct nop_event_hub nop_event_hub_t;

/** Opaque subscription handle. */
typedef struct nop_event_subscription nop_event_subscription_t;

/** Subscriber callback; invoked once per published event on the publisher's thread. */
typedef void (*nop_event_sink_fn)(void *sink_ctx, const nop_event_t *event);

nop_event_hub_t *nop_event_hub_create(void);
void             nop_event_hub_destroy(nop_event_hub_t *hub);

nop_event_subscription_t *nop_event_subscribe(nop_event_hub_t *hub,
                                              nop_event_sink_fn sink, void *sink_ctx);
void nop_event_unsubscribe(nop_event_hub_t *hub, nop_event_subscription_t *subscription);

/** Publish @p event to all subscribers. Called by firmware on detection. */
void nop_event_publish(nop_event_hub_t *hub, const nop_event_t *event);

/** @return current subscriber count (tests/diagnostics). */
int  nop_event_subscriber_count(const nop_event_hub_t *hub);

/**
 * Copy up to @p max recent events (newest first) into @p out; @return the count
 * written. Metadata only — the JPEG/extra_json pointers in copies are NULL (the
 * hub does not retain payloads). Backs queryEventList / queryEventCalendar.
 */
int nop_event_hub_recent(nop_event_hub_t *hub, nop_event_t *out, int max);

/**
 * Map a detection type to the NOP/8012 numeric msgType (1 Motion, 2 Human,
 * 3 Face, 30305 Vehicle, 30316 Animal, 30317 Package, 30312 doorbellRing,
 * 30103 lineCross, 30104 fieldIntrusion). @return the code, or 0 if unmapped.
 */
uint32_t nop_event_msgtype_code(nop_detect_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_EVENT_H */
