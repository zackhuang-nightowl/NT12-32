/**
 * @file cap_floodlight.c
 * @brief CAP_LIGHT flood/indicator/IR light settings. In-memory state.
 *
 * Handlers for the flood light, indicator light and NightOwl IR-light command
 * family. State is held in module-static per-channel arrays (and a few globals)
 * so get/set round-trips correctly today; firmware will override these via the
 * HAL layer later. Scalar values round-trip exactly. Nested schedule values are
 * stored as a serialized JSON snapshot on set and parsed back on get, falling
 * back to a spec-shaped default when nothing has been set yet.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_detect_types.h"
#include <string.h>
#include <stdlib.h>

#define FLOODLIGHT_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= FLOODLIGHT_MAX_CHANNELS) ? 0 : channel;
}

/* ---- per-channel state (indexed by clamped channel) ----------------------- */
static int   g_floodlight_switch[FLOODLIGHT_MAX_CHANNELS];
static int   g_floodlight_detect_switch[FLOODLIGHT_MAX_CHANNELS];
static int   g_floodlight_pir_switch[FLOODLIGHT_MAX_CHANNELS];
static int   g_floodlight_duration_seconds[FLOODLIGHT_MAX_CHANNELS];
static int   g_indicator_light_switch[FLOODLIGHT_MAX_CHANNELS];
static int   g_floodlight_intensity_percent[FLOODLIGHT_MAX_CHANNELS];
static char  g_floodlight_distance[FLOODLIGHT_MAX_CHANNELS][16];
static char  g_floodlight_night_vision_mode[FLOODLIGHT_MAX_CHANNELS][8];
static char *g_floodlight_schedule_json[FLOODLIGHT_MAX_CHANNELS];

static int   g_auto_light_trigger_set[FLOODLIGHT_MAX_CHANNELS];
static char  g_auto_light_trigger[FLOODLIGHT_MAX_CHANNELS][16];
static int   g_auto_light_duration_seconds[FLOODLIGHT_MAX_CHANNELS];
static int   g_light_detection_switch[FLOODLIGHT_MAX_CHANNELS];
static int   g_light_duration_seconds[FLOODLIGHT_MAX_CHANNELS];
static char  g_light_mode[FLOODLIGHT_MAX_CHANNELS][8];
static int   g_light_switch[FLOODLIGHT_MAX_CHANNELS];
static char *g_light_schedule_json[FLOODLIGHT_MAX_CHANNELS];

/* ---- small helpers -------------------------------------------------------- */
static void copy_string_field(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (!source)
        source = "";
    length = strlen(source);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

/* Store a serialized snapshot of an args sub-object into @p slot. */
static void store_json_snapshot(char **slot, const nop_json_t *value)
{
    char *text;
    if (*slot) {
        free(*slot);
        *slot = NULL;
    }
    if (!value)
        return;
    text = nop_json_print(value);
    *slot = text; /* may be NULL on failure; treated as "unset" */
}

/* ===========================================================================
 * Flood light: manual on/off switch
 * =========================================================================== */
static nop_status_t handle_get_floodlight_switch(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_floodlight_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_switch(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_floodlight_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: detection-trigger switch
 * =========================================================================== */
static nop_status_t handle_get_floodlight_detect_switch(const nop_request_t *request,
                                                        nop_response_t *response,
                                                        void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_floodlight_detect_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_detect_switch(const nop_request_t *request,
                                                        nop_response_t *response,
                                                        void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_floodlight_detect_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: PIR-trigger switch
 * =========================================================================== */
static nop_status_t handle_get_floodlight_pir_switch(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_floodlight_pir_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_pir_switch(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_floodlight_pir_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: trigger duration (seconds)
 * =========================================================================== */
static nop_status_t handle_get_floodlight_duration(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", (double)g_floodlight_duration_seconds[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_duration(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_floodlight_duration_seconds[channel] = (int)nop_json_num(request->args, "value", 30);
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: schedule (nested switch + rules array)
 * =========================================================================== */
static void add_default_floodlight_schedule(nop_json_t *content)
{
    nop_json_t *rules = nop_json_arr();
    nop_json_t *rule  = nop_json_obj();
    nop_json_add_str(rule, "startTime", "180000");
    nop_json_add_str(rule, "endTime", "060000");
    nop_json_arr_push(rules, rule);
    nop_json_add_str(content, "switch", "on");
    nop_json_add(content, "rules", rules);
}

static nop_status_t handle_get_floodlight_schedule(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    if (g_floodlight_schedule_json[channel]) {
        response->content = nop_json_parse(g_floodlight_schedule_json[channel],
                                           strlen(g_floodlight_schedule_json[channel]));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_floodlight_schedule(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_schedule(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "switch") && !nop_json_has(request->args, "rules"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    if (nop_json_has(request->args, "switch"))
        nop_json_add_str(snapshot, "switch", nop_json_str(request->args, "switch", "on"));
    else
        nop_json_add_str(snapshot, "switch", "on");
    if (nop_json_get(request->args, "rules")) {
        char *rules_text = nop_json_print(nop_json_get(request->args, "rules"));
        if (rules_text) {
            nop_json_t *rules = nop_json_parse(rules_text, strlen(rules_text));
            free(rules_text);
            if (rules)
                nop_json_add(snapshot, "rules", rules);
        }
    }
    if (!nop_json_get(snapshot, "rules")) {
        nop_json_t *rules = nop_json_arr();
        nop_json_t *rule  = nop_json_obj();
        nop_json_add_str(rule, "startTime", "180000");
        nop_json_add_str(rule, "endTime", "060000");
        nop_json_arr_push(rules, rule);
        nop_json_add(snapshot, "rules", rules);
    }
    store_json_snapshot(&g_floodlight_schedule_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Indicator light: on/off switch
 * =========================================================================== */
static nop_status_t handle_get_indicator_light_switch(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_indicator_light_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_indicator_light_switch(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_indicator_light_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: PIR detection distance ("near"/"medium"/"far")
 * =========================================================================== */
static nop_status_t handle_get_floodlight_distance(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *distance = g_floodlight_distance[channel][0] ? g_floodlight_distance[channel] : "far";
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", distance);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_distance(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    copy_string_field(g_floodlight_distance[channel], sizeof(g_floodlight_distance[channel]),
                      nop_json_str(request->args, "value", "far"));
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: brightness intensity (percent)
 * =========================================================================== */
static nop_status_t handle_get_floodlight_intensity(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", (double)g_floodlight_intensity_percent[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_intensity(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_floodlight_intensity_percent[channel] = (int)nop_json_num(request->args, "value", 80);
    return NOP_OK;
}

/* ===========================================================================
 * Flood light: night-vision mode ("on"/"off")
 * =========================================================================== */
static nop_status_t handle_get_floodlight_night_vision_mode(const nop_request_t *request,
                                                            nop_response_t *response,
                                                            void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *mode = g_floodlight_night_vision_mode[channel][0] ? g_floodlight_night_vision_mode[channel] : "on";
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "mode", mode);
    return NOP_OK;
}

static nop_status_t handle_set_floodlight_night_vision_mode(const nop_request_t *request,
                                                            nop_response_t *response,
                                                            void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "mode"))
        return NOP_ERR_PARAM;
    copy_string_field(g_floodlight_night_vision_mode[channel], sizeof(g_floodlight_night_vision_mode[channel]),
                      nop_json_str(request->args, "mode", "on"));
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: auto light trigger (trigger mode + duration)
 * =========================================================================== */
static nop_status_t handle_get_auto_light_trigger(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *trigger = g_auto_light_trigger_set[channel]
                        ? g_auto_light_trigger[channel]
                        : nop_detect_type_name(NOP_DETECT_MOTION);
    int duration = g_auto_light_trigger_set[channel] ? g_auto_light_duration_seconds[channel] : 3;
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "trigger", trigger);
    nop_json_add_int(response->content, "duration", (double)duration);
    return NOP_OK;
}

static nop_status_t handle_set_auto_light_trigger(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "trigger"))
        return NOP_ERR_PARAM;
    copy_string_field(g_auto_light_trigger[channel], sizeof(g_auto_light_trigger[channel]),
                      nop_json_str(request->args, "trigger",
                                   nop_detect_type_name(NOP_DETECT_MOTION)));
    g_auto_light_duration_seconds[channel] = (int)nop_json_num(request->args, "duration", 3);
    g_auto_light_trigger_set[channel] = 1;
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: light detection switch
 * =========================================================================== */
static nop_status_t handle_get_light_detection_switch(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_light_detection_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_light_detection_switch(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_light_detection_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: light duration (seconds)
 * =========================================================================== */
static nop_status_t handle_get_light_duration(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", (double)g_light_duration_seconds[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_light_duration(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_light_duration_seconds[channel] = (int)nop_json_num(request->args, "value", 3);
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: light mode ("fixed"/"strobe")
 * =========================================================================== */
static nop_status_t handle_get_light_mode(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *mode = g_light_mode[channel][0] ? g_light_mode[channel] : "fixed";
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", mode);
    return NOP_OK;
}

static nop_status_t handle_set_light_mode(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    copy_string_field(g_light_mode[channel], sizeof(g_light_mode[channel]),
                      nop_json_str(request->args, "value", "fixed"));
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: light switch (on/off)
 * =========================================================================== */
static nop_status_t handle_get_light_switch(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_light_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_light_switch(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_light_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: light schedule (nested rules array + switch)
 * =========================================================================== */
static void add_default_light_schedule(nop_json_t *content)
{
    nop_json_t *rules = nop_json_arr();
    nop_json_t *rule  = nop_json_obj();
    nop_json_t *triggers = nop_json_arr();
    nop_json_t *weekdays = nop_json_arr();
    nop_json_add_str(rule, "id", "rule-id-1");
    nop_json_arr_push_str(triggers, nop_detect_type_name(NOP_DETECT_MOTION));
    nop_json_add(rule, "triggers", triggers);
    nop_json_arr_push_int(weekdays, 1);
    nop_json_arr_push_int(weekdays, 2);
    nop_json_arr_push_int(weekdays, 7);
    nop_json_add(rule, "weekdays", weekdays);
    nop_json_add_str(rule, "startTime", "110000");
    nop_json_add_str(rule, "endTime", "203000");
    nop_json_arr_push(rules, rule);
    nop_json_add(content, "rules", rules);
    nop_json_add_str(content, "switch", "on");
}

static nop_status_t handle_get_light_schedule(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    if (g_light_schedule_json[channel]) {
        response->content = nop_json_parse(g_light_schedule_json[channel],
                                           strlen(g_light_schedule_json[channel]));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_light_schedule(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_light_schedule(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "switch") && !nop_json_has(request->args, "rules"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    if (nop_json_get(request->args, "rules")) {
        char *rules_text = nop_json_print(nop_json_get(request->args, "rules"));
        if (rules_text) {
            nop_json_t *rules = nop_json_parse(rules_text, strlen(rules_text));
            free(rules_text);
            if (rules)
                nop_json_add(snapshot, "rules", rules);
        }
    }
    if (!nop_json_get(snapshot, "rules"))
        add_default_light_schedule(snapshot);
    nop_json_add_str(snapshot, "switch", nop_json_str(request->args, "switch", "on"));
    store_json_snapshot(&g_light_schedule_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Registration
 * =========================================================================== */
void cap_floodlight_register(nop_router_t *router)
{
    nop_router_register(router, "getChannelFloodLightDetectSwitch", CAP_LIGHT, handle_get_floodlight_detect_switch);
    nop_router_register(router, "setChannelFloodLightDetectSwitch", CAP_LIGHT, handle_set_floodlight_detect_switch);
    nop_router_register(router, "getChannelFloodLightDuration", CAP_LIGHT, handle_get_floodlight_duration);
    nop_router_register(router, "setChannelFloodLightDuration", CAP_LIGHT, handle_set_floodlight_duration);
    nop_router_register(router, "getChannelFloodLightPirSwitch", CAP_LIGHT, handle_get_floodlight_pir_switch);
    nop_router_register(router, "setChannelFloodLightPirSwitch", CAP_LIGHT, handle_set_floodlight_pir_switch);
    nop_router_register(router, "getChannelFloodLightSchedule", CAP_LIGHT, handle_get_floodlight_schedule);
    nop_router_register(router, "setChannelFloodLightSchedule", CAP_LIGHT, handle_set_floodlight_schedule);
    nop_router_register(router, "getChannelFloodLightSwitch", CAP_LIGHT, handle_get_floodlight_switch);
    nop_router_register(router, "setChannelFloodLightSwitch", CAP_LIGHT, handle_set_floodlight_switch);
    nop_router_register(router, "getChannelIndicatorLightSwitch", CAP_LIGHT, handle_get_indicator_light_switch);
    nop_router_register(router, "setChannelIndicatorLightSwitch", CAP_LIGHT, handle_set_indicator_light_switch);
    nop_router_register(router, "getFloodLightDistance", CAP_LIGHT, handle_get_floodlight_distance);
    nop_router_register(router, "setFloodLightDistance", CAP_LIGHT, handle_set_floodlight_distance);
    nop_router_register(router, "getFloodLightIntensity", CAP_LIGHT, handle_get_floodlight_intensity);
    nop_router_register(router, "setFloodLightIntensity", CAP_LIGHT, handle_set_floodlight_intensity);
    nop_router_register(router, "getFloodLightNightVisionMode", CAP_LIGHT, handle_get_floodlight_night_vision_mode);
    nop_router_register(router, "setFloodLightNightVisionMode", CAP_LIGHT, handle_set_floodlight_night_vision_mode);
    nop_router_register(router, "X_NightOwl_getChannelAutoLightTrigger", CAP_LIGHT, handle_get_auto_light_trigger);
    nop_router_register(router, "X_NightOwl_setChannelAutoLightTrigger", CAP_LIGHT, handle_set_auto_light_trigger);
    nop_router_register(router, "X_NightOwl_getChannelLightDetectionSwitch", CAP_LIGHT, handle_get_light_detection_switch);
    nop_router_register(router, "X_NightOwl_setChannelLightDetectionSwitch", CAP_LIGHT, handle_set_light_detection_switch);
    nop_router_register(router, "X_NightOwl_getChannelLightDuration", CAP_LIGHT, handle_get_light_duration);
    nop_router_register(router, "X_NightOwl_setChannelLightDuration", CAP_LIGHT, handle_set_light_duration);
    nop_router_register(router, "X_NightOwl_getChannelLightMode", CAP_LIGHT, handle_get_light_mode);
    nop_router_register(router, "X_NightOwl_setChannelLightMode", CAP_LIGHT, handle_set_light_mode);
    nop_router_register(router, "X_NightOwl_getChannelLightSchedule", CAP_LIGHT, handle_get_light_schedule);
    nop_router_register(router, "X_NightOwl_setChannelLightSchedule", CAP_LIGHT, handle_set_light_schedule);
    nop_router_register(router, "X_NightOwl_getChannelLightSwitch", CAP_LIGHT, handle_get_light_switch);
    nop_router_register(router, "X_NightOwl_setChannelLightSwitch", CAP_LIGHT, handle_set_light_switch);
}
