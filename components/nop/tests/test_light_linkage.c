/**
 * @file test_light_linkage.c
 * @brief Light-linkage engine: a detection turns the warning light on then it
 *        auto-offs after the hold time; panic drives light + siren.
 *        Uses counting HAL_LIGHT / HAL_AUDIO tables.
 */
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_light_linkage.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_light.h"
#include "nop_sdk/hal/hal_audio.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_light_state, g_light_off_calls;
static int g_alert_calls, g_alert_secs;

static nop_status_t t_light_switch(void *ctx, int ch, int on)
{ (void)ctx; (void)ch; g_light_state = on; if (!on) g_light_off_calls++; return NOP_OK; }
static const hal_light_if t_light = { t_light_switch, NULL, NULL };

static nop_status_t t_play_alert(void *ctx, int ch, int tone, int secs)
{ (void)ctx; (void)ch; (void)tone; g_alert_calls++; g_alert_secs = secs; return NOP_OK; }
static const hal_audio_if t_audio = { NULL, NULL, NULL, NULL, NULL, t_play_alert, NULL };

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(void)
{
    nop_event_hub_t           *hub;
    nop_light_linkage_config_t cfg;
    nop_light_linkage_t       *linkage;
    nop_event_t                evt;
    int i;

    hal_register(HAL_LIGHT, &t_light);
    hal_register(HAL_AUDIO, &t_audio);

    hub = nop_event_hub_create();
    if (!hub) return fail("hub");
    memset(&cfg, 0, sizeof(cfg));
    cfg.hub = hub; cfg.light_seconds = 1; cfg.siren_seconds = 30;
    linkage = nop_light_linkage_start(&cfg);
    if (!linkage) return fail("linkage start");

    /* detection -> light on */
    memset(&evt, 0, sizeof(evt));
    evt.channel = 0; evt.type = NOP_DETECT_HUMAN;
    nop_event_publish(hub, &evt);
    for (i = 0; i < 50 && g_light_state == 0; i++) usleep(10000);
    if (g_light_state != 1) return fail("detection did not turn light on");

    /* auto-off after ~1s hold */
    for (i = 0; i < 200 && g_light_state == 1; i++) usleep(10000);
    if (g_light_state != 0 || g_light_off_calls < 1) return fail("light did not auto-off");

    /* panic -> light + siren */
    nop_light_linkage_panic(linkage, 0, 1);
    if (g_light_state != 1) return fail("panic did not turn light on");
    if (g_alert_calls < 1 || g_alert_secs <= 0) return fail("panic did not sound siren");
    nop_light_linkage_panic(linkage, 0, 0);
    if (g_light_state != 0) return fail("panic clear did not turn light off");

    nop_light_linkage_stop(linkage);
    nop_event_hub_destroy(hub);
    printf("test_light_linkage: OK (detect->light->auto-off + panic light+siren)\n");
    return 0;
}
