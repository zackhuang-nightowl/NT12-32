/**
 * @file test_analog_camera.c
 * @brief Analog (coaxial) camera plug-and-play: presence poll binds/unbinds
 *        channels in the registry, frame-rate applied on bind, and UTC control
 *        passthrough gated on bound state. Uses a controllable stub HAL_ANALOG.
 */
#include "nop_sdk/nop_analog_camera.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/hal/hal_registry.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* Controllable stub HAL_ANALOG. */
static int g_present[4];
static int g_last_framerate_channel = -1, g_last_framerate = -1;
static int g_utc_channel = -1, g_utc_command = -1;

static int test_presence(void *ctx, int ch) { (void)ctx; return (ch >= 0 && ch < 4) ? g_present[ch] : 0; }
static nop_status_t test_set_framerate(void *ctx, int ch, int fps)
{ (void)ctx; g_last_framerate_channel = ch; g_last_framerate = fps; return NOP_OK; }
static nop_status_t test_utc(void *ctx, int ch, hal_analog_utc_t cmd)
{ (void)ctx; g_utc_channel = ch; g_utc_command = (int)cmd; return NOP_OK; }
static const hal_analog_if g_test_analog = { test_presence, test_set_framerate, test_utc, NULL };

int main(void)
{
    nop_nvr_channels_t      *reg;
    nop_analog_camera_t     *mgr;
    nop_nvr_channel_entry_t  entry;

    hal_register(HAL_ANALOG, &g_test_analog);
    reg = nop_nvr_channels_create(8);
    mgr = nop_analog_camera_create(reg, 4, 15);
    if (!reg || !mgr) return fail("create");

    /* Nothing plugged in yet. */
    if (nop_analog_camera_poll(mgr) != 0) return fail("no cameras initially");

    /* Plug a camera into physical channel 2. */
    g_present[2] = 1;
    if (nop_analog_camera_poll(mgr) != 1) return fail("poll should bind 1");
    if (!nop_analog_camera_is_bound(mgr, 2)) return fail("ch2 should be bound");
    if (g_last_framerate_channel != 2 || g_last_framerate != 15) return fail("framerate applied on bind");
    if (nop_nvr_channels_get(reg, 2, &entry) != 1) return fail("registry should have ch2");
    if (entry.source != NOP_CAMERA_SOURCE_ANALOG) return fail("source should be ANALOG");

    /* Idempotent: re-poll with the same presence does not rebind. */
    g_last_framerate_channel = -1;
    if (nop_analog_camera_poll(mgr) != 1) return fail("re-poll still 1");
    if (g_last_framerate_channel != -1) return fail("no rebind on steady presence");

    /* UTC control passes through only for the bound channel. */
    if (nop_analog_camera_control(mgr, 2, HAL_ANALOG_UTC_MENU_OPEN) != NOP_OK) return fail("utc bound ch");
    if (g_utc_channel != 2 || g_utc_command != (int)HAL_ANALOG_UTC_MENU_OPEN) return fail("utc delivered");
    if (nop_analog_camera_control(mgr, 0, HAL_ANALOG_UTC_UP) != NOP_ERR_STATE) return fail("utc unbound ch -> STATE");

    /* Unplug: poll unbinds and removes from the registry. */
    g_present[2] = 0;
    if (nop_analog_camera_poll(mgr) != 0) return fail("unplug -> 0 bound");
    if (nop_analog_camera_is_bound(mgr, 2)) return fail("ch2 should be unbound");
    if (nop_nvr_channels_get(reg, 2, &entry) != 0) return fail("registry should drop ch2");

    /* Assigned-index tracking: pre-occupy registry channel 2 with another
     * camera, then a plug on physical channel 2 must take a DIFFERENT registry
     * index and, on unplug, remove the analog entry — never the pre-existing
     * camera at index 2. */
    {
        nop_nvr_channel_entry_t manual;
        memset(&manual, 0, sizeof(manual));
        manual.channel = 2; manual.enabled = 1; manual.port = 8089;
        snprintf(manual.host, sizeof(manual.host), "192.168.9.9");
        if (nop_nvr_channels_add(reg, &manual) != 2) return fail("preoccupy ch2");

        g_present[2] = 1;
        nop_analog_camera_poll(mgr);
        if (nop_nvr_channels_get(reg, 2, &entry) != 1 || entry.source == NOP_CAMERA_SOURCE_ANALOG)
            return fail("manual camera at index 2 must be untouched by analog plug");

        g_present[2] = 0;
        nop_analog_camera_poll(mgr);
        if (nop_nvr_channels_get(reg, 2, &entry) != 1)
            return fail("unplug must remove the analog entry, not the manual index-2 camera");
    }

    nop_analog_camera_destroy(mgr);
    nop_nvr_channels_destroy(reg);
    printf("test_analog_camera: OK (bind/framerate/UTC/unbind/assigned-index)\n");
    return 0;
}
