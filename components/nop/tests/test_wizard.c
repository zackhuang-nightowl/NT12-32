/**
 * @file test_wizard.c
 * @brief Startup setup wizard: password policy, step progression, completion
 *        gating, factory-complete skip, and reset.
 */
#include "nop_sdk/nop_wizard.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(void)
{
    nop_wizard_t       *w;
    nop_wizard_status_t st;

    /* Password policy. */
    if (nop_wizard_check_password("12345") != NOP_WIZARD_PW_TOO_SHORT) return fail("short");
    if (nop_wizard_check_password("123456789012345678901") != NOP_WIZARD_PW_TOO_LONG) return fail("long");
    if (nop_wizard_check_password("pass@1") != NOP_WIZARD_PW_BAD_CHAR) return fail("special char");
    if (nop_wizard_check_password("Admin123") != NOP_WIZARD_PW_OK) return fail("valid pw");

    /* Factory boot → PENDING, first step is PASSWORD. */
    w = nop_wizard_create(0);
    if (!w) return fail("create");
    nop_wizard_status(w, &st);
    if (st.state != NOP_WIZARD_PENDING || st.step != NOP_WIZARD_STEP_PASSWORD)
        return fail("initial should be PENDING@PASSWORD");

    /* Cannot complete without a password. */
    if (nop_wizard_complete(w) != NOP_ERR_STATE) return fail("complete without pw must fail");

    /* Bad password rejected; good one advances to DEVICE_NAME. */
    if (nop_wizard_set_password(w, "bad!!") != NOP_ERR_PARAM) return fail("bad pw rejected");
    if (nop_wizard_set_password(w, "Admin123") != NOP_OK) return fail("set pw");
    nop_wizard_status(w, &st);
    if (st.state != NOP_WIZARD_IN_PROGRESS || !st.has_password) return fail("after pw: IN_PROGRESS");
    if (st.step != NOP_WIZARD_STEP_DEVICE_NAME) return fail("next step DEVICE_NAME");

    /* Name → channel-imaging → firmware → DONE, in the published order. */
    if (nop_wizard_set_device_name(w, "") != NOP_ERR_PARAM) return fail("empty name rejected");
    if (nop_wizard_set_device_name(w, "FrontNVR") != NOP_OK) return fail("set name");
    nop_wizard_status(w, &st);
    if (st.step != NOP_WIZARD_STEP_CHANNEL_IMAGING) return fail("next step CHANNEL_IMAGING");
    nop_wizard_mark_channel_imaging(w);
    nop_wizard_status(w, &st);
    if (st.step != NOP_WIZARD_STEP_FIRMWARE) return fail("next step FIRMWARE");
    nop_wizard_mark_firmware_checked(w);
    nop_wizard_status(w, &st);
    if (st.step != NOP_WIZARD_STEP_DONE) return fail("next step DONE");
    if (strcmp(st.device_name, "FrontNVR") != 0) return fail("device name stored");

    /* Complete. */
    if (nop_wizard_complete(w) != NOP_OK) return fail("complete");
    if (!nop_wizard_is_complete(w)) return fail("is_complete");
    nop_wizard_status(w, &st);
    if (st.state != NOP_WIZARD_COMPLETE || st.step != NOP_WIZARD_STEP_DONE) return fail("COMPLETE state");

    /* Reset returns to factory. */
    nop_wizard_reset(w);
    if (nop_wizard_is_complete(w)) return fail("reset should clear complete");
    nop_wizard_status(w, &st);
    if (st.state != NOP_WIZARD_PENDING || st.has_password) return fail("reset PENDING clean");
    nop_wizard_destroy(w);

    /* Persisted "already complete" skips the wizard. */
    w = nop_wizard_create(1);
    if (!nop_wizard_is_complete(w)) return fail("already_complete should be COMPLETE");
    nop_wizard_destroy(w);

    printf("test_wizard: OK (policy, steps, completion gating, reset, skip)\n");
    return 0;
}
