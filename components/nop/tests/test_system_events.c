/**
 * @file test_system_events.c
 * @brief Reset-button / SD-card event handling: long-press → factory reset with
 *        identity preserve; SD insert → upgrade scan. Fires events through the
 *        stub HAL_SYSTEM sink.
 */
#include "nop_sdk/nop_system_events.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define UP_DIR "/tmp/nop_system_events_test"

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static int  g_preserve_calls;
static char g_preserved_serial[16];

static void on_preserve(void *ctx, const hal_device_info_t *identity)
{
    (void)ctx;
    g_preserve_calls++;
    snprintf(g_preserved_serial, sizeof(g_preserved_serial), "%s", identity->serial_number);
}

int main(void)
{
    nop_system_events_config_t cfg;
    nop_system_events_t       *handle;
    FILE                      *f;

    hal_stub_register_all();
    mkdir(UP_DIR, 0777);

    memset(&cfg, 0, sizeof(cfg));
    cfg.factory_reset_on_long_press = 1;
    cfg.on_preserve_identity        = on_preserve;
    strcpy(cfg.upgrade_dir, UP_DIR);

    handle = nop_system_events_start(&cfg);
    if (!handle) return fail("start");

    /* Short press must NOT trigger factory reset / preserve. */
    hal_stub_fire_system_event(HAL_SYSTEM_EVENT_BUTTON_RESET_SHORT);
    if (g_preserve_calls != 0) return fail("short press should not preserve");

    /* Long press → identity preserved (SN captured) then factory_reset. */
    hal_stub_fire_system_event(HAL_SYSTEM_EVENT_BUTTON_RESET_LONG);
    if (g_preserve_calls != 1) return fail("long press should preserve once");
    if (g_preserved_serial[0] == '\0') return fail("serial not preserved");

    /* SD inserted with an image present → upgrade scan applies it (no crash;
     * the stub HAL accepts the image). Without an image it's a benign no-op. */
    f = fopen(UP_DIR "/fw.img", "wb");
    if (f) { fputs("IMG", f); fclose(f); }
    hal_stub_fire_system_event(HAL_SYSTEM_EVENT_SDCARD_INSERTED);
    remove(UP_DIR "/fw.img");

    nop_system_events_stop(handle);

    /* After stop, further events are a no-op (sink detached). */
    hal_stub_fire_system_event(HAL_SYSTEM_EVENT_BUTTON_RESET_LONG);
    if (g_preserve_calls != 1) return fail("event after stop should be ignored");

    printf("test_system_events: OK (preserved SN=%s)\n", g_preserved_serial);
    return 0;
}
