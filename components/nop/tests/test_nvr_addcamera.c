/**
 * @file test_nvr_addcamera.c
 * @brief "Add wireless cameras" scan-mode state machine: start/stop/status,
 *        admission into the registry only while the window is open, and window
 *        expiry (SCANNING -> DONE).
 */
#include "nop_sdk/nop_nvr_addcamera.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void make_entry(nop_nvr_channel_entry_t *e, const char *host)
{
    memset(e, 0, sizeof(*e));
    e->channel = -1;
    e->enabled = 1;
    e->port    = 8089;
    snprintf(e->host, sizeof(e->host), "%s", host);
}

int main(void)
{
    nop_nvr_channels_t     *reg;
    nop_nvr_addcamera_t    *svc;
    nop_addcamera_status_t  st;
    nop_nvr_channel_entry_t e;

    reg = nop_nvr_channels_create(8);
    svc = nop_nvr_addcamera_create(reg);
    if (!reg || !svc) return fail("create");

    /* Idle before starting. */
    nop_nvr_addcamera_status(svc, &st);
    if (st.state != NOP_ADDCAMERA_IDLE) return fail("initial state should be IDLE");

    /* Offer before scanning is rejected. */
    make_entry(&e, "192.168.1.10");
    if (nop_nvr_addcamera_offer(svc, &e) != -1) return fail("offer before scan should fail");

    /* Open a window. */
    if (nop_nvr_addcamera_start(svc, 30) != NOP_OK) return fail("start");
    if (nop_nvr_addcamera_start(svc, 30) != NOP_ERR_STATE) return fail("double start should be STATE");
    nop_nvr_addcamera_status(svc, &st);
    if (st.state != NOP_ADDCAMERA_SCANNING) return fail("state should be SCANNING");
    if (st.seconds_remaining <= 0 || st.seconds_remaining > 30) return fail("seconds_remaining range");

    /* Admit two distinct cameras. */
    make_entry(&e, "192.168.1.10"); if (nop_nvr_addcamera_offer(svc, &e) != 0) return fail("offer camA -> ch0");
    make_entry(&e, "192.168.1.11"); if (nop_nvr_addcamera_offer(svc, &e) != 1) return fail("offer camB -> ch1");
    nop_nvr_addcamera_status(svc, &st);
    if (st.added_count != 2) return fail("added_count should be 2");
    if (nop_nvr_channels_count(reg) != 2) return fail("registry should hold 2");

    /* Stop closes the window; later offers are rejected. */
    if (nop_nvr_addcamera_stop(svc) != NOP_OK) return fail("stop");
    nop_nvr_addcamera_status(svc, &st);
    if (st.state != NOP_ADDCAMERA_IDLE) return fail("state should be IDLE after stop");
    make_entry(&e, "192.168.1.12"); if (nop_nvr_addcamera_offer(svc, &e) != -1) return fail("offer after stop should fail");
    if (nop_nvr_channels_count(reg) != 2) return fail("registry unchanged after stop");

    /* Window expiry: a 1s window elapses to DONE and stops admitting. */
    if (nop_nvr_addcamera_start(svc, 1) != NOP_OK) return fail("start short window");
    osal_sleep_ms(1100);
    nop_nvr_addcamera_status(svc, &st);
    if (st.state != NOP_ADDCAMERA_DONE) return fail("window should have elapsed to DONE");
    if (st.seconds_remaining != 0) return fail("no seconds remaining when DONE");
    make_entry(&e, "192.168.1.13"); if (nop_nvr_addcamera_offer(svc, &e) != -1) return fail("offer after window elapse should fail");

    nop_nvr_addcamera_destroy(svc);
    nop_nvr_channels_destroy(reg);
    printf("test_nvr_addcamera: OK (start/stop/status/offer/expiry)\n");
    return 0;
}
