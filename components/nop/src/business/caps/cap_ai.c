/**
 * @file cap_ai.c
 * @brief CAP_AI smart-detect handlers (detect filter/threshold, PTZ track,
 *        per-sensor config, activity zones). In-memory state.
 *
 * Handlers for the AI/smart-detect command family. State is held in
 * module-static per-channel arrays so get/set round-trips correctly today;
 * firmware will override these via the HAL layer later. Object/array-valued
 * fields (sensor filter, threshold, PTZ track modes, activity zone) are stored
 * as a serialized JSON snapshot on set and parsed back on get, falling back to
 * a spec-shaped default when nothing has been set yet.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "business/cap_helpers.h"
#include "nop_sdk/nop_detect_types.h"
#include "onvif/mapping/nop_onvif_map.h"
#include <string.h>
#include <stdlib.h>

#define AI_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= AI_MAX_CHANNELS) ? 0 : channel;
}

/* ---- per-channel snapshot state (indexed by clamped channel) -------------- */
static char *g_detect_filter_json[AI_MAX_CHANNELS];
static char *g_detect_threshold_json[AI_MAX_CHANNELS];
static char *g_ptz_track_json[AI_MAX_CHANNELS];
static char *g_activity_zone_json[AI_MAX_CHANNELS];

/* ---- small helpers -------------------------------------------------------- */

/* Store a serialized snapshot of a JSON value into @p slot, freeing the old. */
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

/* Deep-copy a parsed snapshot of @p value (any type) by round-tripping it. */
static nop_json_t *clone_json(const nop_json_t *value)
{
    char *text;
    nop_json_t *copy;
    if (!value)
        return NULL;
    text = nop_json_print(value);
    if (!text)
        return NULL;
    copy = nop_json_parse(text, strlen(text));
    free(text);
    return copy;
}

/* Append a default sensor entry (sensor + empty min/max + movement) to @p arr. */
static void add_default_filter_sensor(nop_json_t *arr, const char *sensor)
{
    nop_json_t *entry = nop_json_obj();
    nop_json_t *min   = nop_json_arr();
    nop_json_t *max   = nop_json_arr();
    nop_json_add_str(entry, "sensor", sensor);
    nop_json_add(entry, "min", min);
    nop_json_add(entry, "max", max);
    nop_json_add_bool(entry, "movement", false);
    nop_json_arr_push(arr, entry);
}

/* ===========================================================================
 * AI detect filter (per-sensor min/max size + movement)
 * =========================================================================== */
static nop_status_t handle_get_detect_filter(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (g_detect_filter_json[channel]) {
        response->content = nop_json_parse(g_detect_filter_json[channel],
                                           strlen(g_detect_filter_json[channel]));
    }
    if (!response->content) {
        nop_json_t *sensors = nop_json_arr();
        response->content = nop_json_obj();
        cap_for_each_detection_type(handler_context, add_default_filter_sensor, sensors);
        nop_json_add(response->content, "sensors", sensors);
    }
    return NOP_OK;
}

static nop_status_t handle_set_detect_filter(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    nop_json_t *snapshot;
    nop_json_t *sensors;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "sensors"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    sensors = clone_json(nop_json_get(request->args, "sensors"));
    if (sensors)
        nop_json_add(snapshot, "sensors", sensors);
    else
        nop_json_add(snapshot, "sensors", nop_json_arr());
    store_json_snapshot(&g_detect_filter_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * AI detect threshold (per-sensor confidence threshold)
 * =========================================================================== */
static void add_default_threshold_sensor(nop_json_t *arr, const char *sensor)
{
    nop_json_t *entry = nop_json_obj();
    nop_json_add_str(entry, "sensor", sensor);
    nop_json_add_int(entry, "threshold", 80);
    nop_json_arr_push(arr, entry);
}

static nop_status_t handle_get_detect_threshold(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (g_detect_threshold_json[channel]) {
        response->content = nop_json_parse(g_detect_threshold_json[channel],
                                           strlen(g_detect_threshold_json[channel]));
    }
    if (!response->content) {
        nop_json_t *sensors = nop_json_arr();
        response->content = nop_json_obj();
        cap_for_each_detection_type(handler_context, add_default_threshold_sensor, sensors);
        nop_json_add(response->content, "sensors", sensors);
    }
    return NOP_OK;
}

static nop_status_t handle_set_detect_threshold(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    nop_json_t *snapshot;
    nop_json_t *sensors;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "sensors"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    sensors = clone_json(nop_json_get(request->args, "sensors"));
    if (sensors)
        nop_json_add(snapshot, "sensors", sensors);
    else
        nop_json_add(snapshot, "sensors", nop_json_arr());
    store_json_snapshot(&g_detect_threshold_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * PTZ track (smart-track modes)
 * =========================================================================== */
static nop_status_t handle_get_ptz_track(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    (void)handler_context;
    if (g_ptz_track_json[channel]) {
        response->content = nop_json_parse(g_ptz_track_json[channel],
                                           strlen(g_ptz_track_json[channel]));
    }
    if (!response->content) {
        nop_json_t *modes = nop_json_arr();
        response->content = nop_json_obj();
        nop_json_arr_push_str(modes, "hd");
        nop_json_add(response->content, "modes", modes);
    }
    return NOP_OK;
}

static nop_status_t handle_set_ptz_track(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    nop_json_t *snapshot;
    nop_json_t *modes;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "modes"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    modes = clone_json(nop_json_get(request->args, "modes"));
    if (modes)
        nop_json_add(snapshot, "modes", modes);
    else
        nop_json_add(snapshot, "modes", nop_json_arr());
    store_json_snapshot(&g_ptz_track_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Channel sensor config (set-only: per-sensor enable/draw/interval)
 * =========================================================================== */
static nop_status_t handle_set_channel_sensor_config(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    if (!nop_json_has(request->args, "sensors"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    (void)response;
    /* Accepted and acknowledged; per-sensor enablement is applied by the HAL
     * layer later. No stored state is required for this command today. */
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: channel activity-zone types (get-only)
 * =========================================================================== */
static nop_status_t handle_get_activity_zone_types(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    nop_json_t *triggers;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    triggers = nop_json_arr();
    nop_json_arr_push_str(triggers, nop_detect_type_name(NOP_DETECT_PIXEL_CHANGE));
    nop_json_add(response->content, "triggers", triggers);
    return NOP_OK;
}

/* ===========================================================================
 * NightOwl: channel trigger activity zone (zone geometry + sensitivity)
 * =========================================================================== */
static void add_default_activity_zone(nop_json_t *content)
{
    nop_json_t *points = nop_json_arr();
    int row;
    nop_json_add_int(content, "width", 22);
    nop_json_add_int(content, "height", 18);
    nop_json_add_str(content, "sensitivity", "middle");
    for (row = 0; row < 3; row++) {
        nop_json_t *point = nop_json_arr();
        nop_json_arr_push_int(point, 0);
        nop_json_arr_push_int(point, (double)row);
        nop_json_arr_push(points, point);
    }
    nop_json_add(content, "activityZonePoints", points);
}

static nop_status_t handle_get_trigger_activity_zone(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    if (g_activity_zone_json[channel]) {
        response->content = nop_json_parse(g_activity_zone_json[channel],
                                           strlen(g_activity_zone_json[channel]));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_activity_zone(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_trigger_activity_zone(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    nop_json_t *snapshot;
    nop_json_t *points;
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    (void)response;
    if (!nop_json_has(request->args, "triggers") &&
        !nop_json_has(request->args, "activityZonePoints"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    nop_json_add_int(snapshot, "width", (double)nop_json_num(request->args, "width", 22));
    nop_json_add_int(snapshot, "height", (double)nop_json_num(request->args, "height", 18));
    nop_json_add_str(snapshot, "sensitivity",
                     nop_json_str(request->args, "sensitivity", "middle"));
    points = clone_json(nop_json_get(request->args, "activityZonePoints"));
    if (points)
        nop_json_add(snapshot, "activityZonePoints", points);
    else
        nop_json_add(snapshot, "activityZonePoints", nop_json_arr());
    store_json_snapshot(&g_activity_zone_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Registration
 * =========================================================================== */
void cap_ai_register(nop_router_t *router)
{
    nop_router_register(router, "AI_getDetectFilter", CAP_AI, handle_get_detect_filter);
    nop_router_register(router, "AI_setDetectFilter", CAP_AI, handle_set_detect_filter);
    nop_router_register(router, "AI_getDetectThreshold", CAP_AI, handle_get_detect_threshold);
    nop_router_register(router, "AI_setDetectThreshold", CAP_AI, handle_set_detect_threshold);
    nop_router_register(router, "getPtzTrack", CAP_AI, handle_get_ptz_track);
    nop_router_register(router, "setPtzTrack", CAP_AI, handle_set_ptz_track);
    nop_router_register(router, "setChannelSensorConfig", CAP_AI, handle_set_channel_sensor_config);
    nop_router_register(router, "X_NightOwl_getChannelActivityZoneTypes", CAP_AI, handle_get_activity_zone_types);
    nop_router_register(router, "X_NightOwl_getChannelTriggerActivityZone", CAP_AI, handle_get_trigger_activity_zone);
    nop_router_register(router, "X_NightOwl_setChannelTriggerActivityZone", CAP_AI, handle_set_trigger_activity_zone);
}
