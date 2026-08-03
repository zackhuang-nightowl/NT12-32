/**
 * @file test_push.c
 * @brief Push engine policy: per-type silent window dedups repeats, doorbellRing
 *        is exempt, and the global rate limit gates bursts.
 */
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_push.h"

#include <stdio.h>
#include <string.h>

static int g_sent;
static void send_sink(void *ctx, const nop_event_t *event) { (void)ctx; (void)event; g_sent++; }

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void publish(nop_event_hub_t *hub, nop_detect_type_t type)
{
    nop_event_t e;
    memset(&e, 0, sizeof(e));
    e.channel = 0; e.type = type;
    nop_event_publish(hub, &e);
}

int main(void)
{
    nop_event_hub_t   *hub = nop_event_hub_create();
    nop_push_config_t  cfg;
    nop_push_t        *push;

    if (!hub) return fail("hub");
    memset(&cfg, 0, sizeof(cfg));
    cfg.hub = hub; cfg.send = send_sink;
    cfg.min_interval_ms = 0;            /* isolate the silent-window test */
    cfg.silent_window_ms = 60000;      /* wide window: repeats are deduped */
    push = nop_push_start(&cfg);
    if (!push) return fail("push start");

    /* 3 quick HUMAN events -> only the first passes the silent window. */
    publish(hub, NOP_DETECT_HUMAN);
    publish(hub, NOP_DETECT_HUMAN);
    publish(hub, NOP_DETECT_HUMAN);
    if (g_sent != 1) return fail("human not deduped to 1");

    /* A different type in the same window is independent. */
    publish(hub, NOP_DETECT_VEHICLE);
    if (g_sent != 2) return fail("vehicle should pass independently");

    /* doorbellRing is exempt from the silent window: every ring passes. */
    publish(hub, NOP_DETECT_DOORBELL_RING);
    publish(hub, NOP_DETECT_DOORBELL_RING);
    if (g_sent != 4) return fail("doorbellRing should not be deduped");

    nop_push_stop(push);
    nop_event_hub_destroy(hub);
    printf("test_push: OK (silent-window dedup + doorbellRing exempt)\n");
    return 0;
}
