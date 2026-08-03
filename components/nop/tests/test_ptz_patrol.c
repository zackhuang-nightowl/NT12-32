/**
 * @file test_ptz_patrol.c
 * @brief PTZ patrol execution engine (background preset cycling) + the
 *        preset/home command→HAL wiring, driven through a counting HAL_PTZ.
 */
#include "nop_sdk/nop_ptz_patrol.h"
#include "nop_sdk/nop_app.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* Counting HAL_PTZ. */
static int g_goto_count, g_set_preset_count, g_remove_preset_count;
static int g_set_home_count, g_goto_home_count, g_last_preset = -1;

static nop_status_t c_move(void *c, int ch, hal_ptz_dir_t d, int s) { (void)c;(void)ch;(void)d;(void)s; return NOP_OK; }
static nop_status_t c_goto_preset(void *c, int ch, int p) { (void)c;(void)ch; g_goto_count++; g_last_preset = p; return NOP_OK; }
static nop_status_t c_move_step(void *c, int ch, hal_ptz_dir_t d, int s) { (void)c;(void)ch;(void)d;(void)s; return NOP_OK; }
static nop_status_t c_stop(void *c, int ch) { (void)c;(void)ch; return NOP_OK; }
static nop_status_t c_focus_step(void *c, int ch, int in, int s) { (void)c;(void)ch;(void)in;(void)s; return NOP_OK; }
static nop_status_t c_focus_stop(void *c, int ch) { (void)c;(void)ch; return NOP_OK; }
static nop_status_t c_set_preset(void *c, int ch, int p) { (void)c;(void)ch;(void)p; g_set_preset_count++; return NOP_OK; }
static nop_status_t c_remove_preset(void *c, int ch, int p) { (void)c;(void)ch;(void)p; g_remove_preset_count++; return NOP_OK; }
static nop_status_t c_set_home(void *c, int ch) { (void)c;(void)ch; g_set_home_count++; return NOP_OK; }
static nop_status_t c_goto_home(void *c, int ch) { (void)c;(void)ch; g_goto_home_count++; return NOP_OK; }
static const hal_ptz_if g_counting_ptz = {
    c_move, c_goto_preset, c_move_step, c_stop, c_focus_step, c_focus_stop,
    c_set_preset, c_remove_preset, c_set_home, c_goto_home, NULL
};

/* Dispatch a command; @return 1 if the response carries statusCode 200. */
static int dispatch_ok(nop_app_t *app, const char *json_in)
{
    char *out = NULL;
    int   ok;
    if (nop_app_dispatch(app, json_in, &out) != NOP_OK || !out)
        return 0;
    ok = strstr(out, "\"statusCode\":200") != NULL;
    nop_app_free_response(out);
    return ok;
}

static int test_engine(void)
{
    nop_ptz_patrol_t *engine;
    nop_ptz_spot_t    spots[2];

    engine = nop_ptz_patrol_create();
    if (!engine) return fail("engine create");

    spots[0].preset = 0; spots[0].dwell_seconds = 0; spots[0].speed = 50;
    spots[1].preset = 1; spots[1].dwell_seconds = 0; spots[1].speed = 50;

    g_goto_count = 0; g_last_preset = -1;
    if (nop_ptz_patrol_start(engine, 0, spots, 2) != NOP_OK) return fail("patrol start");
    if (!nop_ptz_patrol_is_running(engine)) return fail("patrol should be running");
    osal_sleep_ms(150);                          /* let it cycle a few spots */
    if (nop_ptz_patrol_step_count(engine) < 2) return fail("patrol should have cycled >=2 spots");
    if (nop_ptz_patrol_stop(engine) != NOP_OK) return fail("patrol stop");
    if (nop_ptz_patrol_is_running(engine)) return fail("patrol should be stopped");
    {
        int after = nop_ptz_patrol_step_count(engine);
        osal_sleep_ms(80);
        if (nop_ptz_patrol_step_count(engine) != after) return fail("no steps after stop");
    }
    nop_ptz_patrol_destroy(engine);
    return 0;
}

static int test_preset_home_wiring(void)
{
    nop_app_config_t cfg;
    nop_app_t       *app;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;                           /* HAL_PTZ present → CAP_PTZ lit */
    app = nop_app_create(&cfg);
    if (!app) return fail("app create");

    g_set_preset_count = g_goto_count = g_set_home_count = g_goto_home_count = g_remove_preset_count = 0;
    g_last_preset = -1;

    if (!dispatch_ok(app, "{\"func\":\"setPtzPreset\",\"args\":{\"channel\":0,\"token\":\"t1\",\"name\":\"Gate\"}}"))
        return fail("setPtzPreset 200");
    if (g_set_preset_count != 1) return fail("setPtzPreset should call HAL set_preset");

    if (!dispatch_ok(app, "{\"func\":\"gotoPtzPreset\",\"args\":{\"channel\":0,\"token\":\"t1\",\"speed\":50}}"))
        return fail("gotoPtzPreset 200");
    if (g_goto_count != 1) return fail("gotoPtzPreset should call HAL goto_preset");

    if (!dispatch_ok(app, "{\"func\":\"setPtzHome\",\"args\":{\"channel\":0}}"))
        return fail("setPtzHome 200");
    if (g_set_home_count != 1) return fail("setPtzHome should call HAL set_home");

    if (!dispatch_ok(app, "{\"func\":\"gotoPtzHome\",\"args\":{\"channel\":0,\"speed\":50}}"))
        return fail("gotoPtzHome 200");
    if (g_goto_home_count != 1) return fail("gotoPtzHome should call HAL goto_home");

    if (!dispatch_ok(app, "{\"func\":\"removePtzPreset\",\"args\":{\"channel\":0,\"token\":\"t1\"}}"))
        return fail("removePtzPreset 200");
    if (g_remove_preset_count != 1) return fail("removePtzPreset should call HAL remove_preset");

    nop_app_destroy(app);
    return 0;
}

/* Count non-overlapping occurrences of @p needle in @p hay. */
static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* Dispatch and return the malloc'd response (caller frees), or NULL. */
static char *dispatch_raw(nop_app_t *app, const char *json_in)
{
    char *out = NULL;
    if (nop_app_dispatch(app, json_in, &out) != NOP_OK)
        return NULL;
    return out;
}

static int test_patrol_dispatch(void)
{
    nop_app_config_t  cfg;
    nop_app_t        *app;
    nop_ptz_patrol_t *engine;
    char             *resp;
    int               running_count;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;
    app = nop_app_create(&cfg);
    engine = nop_ptz_patrol_create();
    if (!app || !engine) return fail("patrol dispatch setup");
    nop_app_set_ptz_patrol(app, engine);

    /* Two patrols → deterministic tokens patrol_0 / patrol_1 in this process. */
    if (!dispatch_ok(app, "{\"func\":\"createPtzPatrol\",\"args\":{\"channel\":0,\"name\":\"A\",\"spots\":[{\"preset\":0,\"dwell\":0,\"speed\":50}]}}"))
        return fail("create patrol A");
    if (!dispatch_ok(app, "{\"func\":\"createPtzPatrol\",\"args\":{\"channel\":0,\"name\":\"B\",\"spots\":[{\"preset\":1,\"dwell\":0,\"speed\":50}]}}"))
        return fail("create patrol B");

    /* Start A → exactly one patrol reports Running. */
    if (!dispatch_ok(app, "{\"func\":\"operatePtzPatrol\",\"args\":{\"channel\":0,\"token\":\"patrol_0\",\"op\":\"start\"}}"))
        return fail("operate start A");
    resp = dispatch_raw(app, "{\"func\":\"getPtzPatrols\",\"args\":{\"channel\":0}}");
    running_count = resp ? count_substr(resp, "\"status\":\"Running\"") : -1;
    nop_app_free_response(resp);
    if (running_count != 1) return fail("exactly one patrol should be Running after start A");

    /* Start B replaces A → still exactly one Running. */
    if (!dispatch_ok(app, "{\"func\":\"operatePtzPatrol\",\"args\":{\"channel\":0,\"token\":\"patrol_1\",\"op\":\"start\"}}"))
        return fail("operate start B");
    resp = dispatch_raw(app, "{\"func\":\"getPtzPatrols\",\"args\":{\"channel\":0}}");
    running_count = resp ? count_substr(resp, "\"status\":\"Running\"") : -1;
    nop_app_free_response(resp);
    if (running_count != 1) return fail("still exactly one Running after start B");

    /* Stop B → none Running. */
    if (!dispatch_ok(app, "{\"func\":\"operatePtzPatrol\",\"args\":{\"channel\":0,\"token\":\"patrol_1\",\"op\":\"stop\"}}"))
        return fail("operate stop B");
    resp = dispatch_raw(app, "{\"func\":\"getPtzPatrols\",\"args\":{\"channel\":0}}");
    running_count = resp ? count_substr(resp, "\"status\":\"Running\"") : -1;
    nop_app_free_response(resp);
    if (running_count != 0) return fail("none Running after stop B");

    nop_ptz_patrol_stop(engine);
    nop_app_set_ptz_patrol(app, NULL);
    nop_ptz_patrol_destroy(engine);
    nop_app_destroy(app);
    return 0;
}

int main(void)
{
    int rc;
    hal_register(HAL_PTZ, &g_counting_ptz);

    rc = test_engine();
    if (rc == 0)
        rc = test_preset_home_wiring();
    if (rc == 0)
        rc = test_patrol_dispatch();

    if (rc == 0)
        printf("test_ptz_patrol: OK (cycling + preset/home wiring + patrol token attribution)\n");
    return rc;
}
