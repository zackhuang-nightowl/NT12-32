/**
 * @file cap_ai_advanced.c
 * @brief CAP_AI advanced smart-detect handlers (per-channel AI capabilities,
 *        line-cross / field-intrusion rules, and event metaData ext-info).
 *        In-memory state.
 *
 * Handlers for the advanced AI/smart-detect command family, all gated by
 * CAP_AI. State is held in module-static per-channel arrays so get/set
 * round-trips correctly today; firmware will override these via the HAL layer
 * later. Object/array-valued fields (line-cross rules, field-intrusion rules,
 * event ext-info config) are stored as a serialized JSON snapshot on set and
 * parsed back on get, falling back to a spec-shaped default when nothing has
 * been set yet.
 *
 * Detection / trigger type tokens are never hardcoded as string literals; they
 * come from the shared vocabulary in nop_detect_types.h via
 * nop_detect_type_name().
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_detect_types.h"
#include "onvif/mapping/nop_onvif_map.h"
#include <string.h>
#include <stdlib.h>

#define AI_ADV_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= AI_ADV_MAX_CHANNELS) ? 0 : channel;
}

/* ---- per-channel snapshot state (indexed by clamped channel) -------------- */
static char *g_event_ext_info_config_json[AI_ADV_MAX_CHANNELS];

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

/* Append the default ruled-detection trigger set (human + vehicle). */
static void add_default_triggers(nop_json_t *arr)
{
    nop_json_arr_push_str(arr, nop_detect_type_name(NOP_DETECT_HUMAN));
    nop_json_arr_push_str(arr, nop_detect_type_name(NOP_DETECT_VEHICLE));
}

/* ===========================================================================
 * Channel AI capabilities (get-only, spec-shaped capabilities object)
 * =========================================================================== */

/* Build one objectDetection capability entry (drawRegion/drawText/drawIn/
 * minMaxFilter/threshold/metaData). */
static nop_json_t *make_object_detection_caps(void)
{
    nop_json_t *entry    = nop_json_obj();
    nop_json_t *draw_in  = nop_json_arr();
    nop_json_t *min_max  = nop_json_obj();
    nop_json_t *threshold = nop_json_obj();
    nop_json_t *meta     = nop_json_obj();
    nop_json_add_bool(entry, "drawRegion", true);
    nop_json_add_bool(entry, "drawText", true);
    nop_json_arr_push_str(draw_in, "main");
    nop_json_arr_push_str(draw_in, "sub");
    nop_json_add(entry, "drawIn", draw_in);
    nop_json_add(entry, "minMaxFilter", min_max);
    nop_json_add(entry, "threshold", threshold);
    nop_json_add_bool(meta, "eventExtInfo", true);
    nop_json_add(entry, "metaData", meta);
    return entry;
}

static nop_status_t handle_get_channel_ai_capabilities(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * Channel line-cross detect (rules: enable/name/line/direction/triggers)
 * =========================================================================== */

static nop_status_t handle_get_channel_line_cross_detect(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    /* NOP 相机由 NVR 8089 透传；此处仅服务 ONVIF mapping。 */
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_channel_line_cross_detect(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * Channel field-intrusion detect (rules: enable/name/area/timeThreshold/triggers)
 * =========================================================================== */

static nop_status_t handle_get_channel_field_intrusion_detect(const nop_request_t *request,
                                                              nop_response_t *response,
                                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_channel_field_intrusion_detect(const nop_request_t *request,
                                                              nop_response_t *response,
                                                              void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * Event ext-info (metaData) (get-only, single event by startTime)
 * =========================================================================== */
static nop_status_t handle_get_event_ext_info(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    double start_time;
    (void)handler_context;
    if (!nop_json_has(request->args, "startTime"))
        return NOP_ERR_PARAM;
    start_time = nop_json_num(request->args, "startTime", 0);
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "startTime", start_time);
    /* No cached metaData today: report empty per-type trigger arrays. */
    nop_json_add(response->content, "hdTrigger", nop_json_arr());
    nop_json_add(response->content, "fdTrigger", nop_json_arr());
    nop_json_add(response->content, "vdTrigger", nop_json_arr());
    nop_json_add(response->content, "adTrigger", nop_json_arr());
    nop_json_add(response->content, "pdTrigger", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * Event ext-info batch (get-only, reverse-time list by startTime)
 * =========================================================================== */
static nop_status_t handle_get_event_ext_info_batch_by_reverse_time(
    const nop_request_t *request,
    nop_response_t *response,
    void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "startTime"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    /* No cached metaData today: report an empty list. */
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * Event ext-info config (enable + collection interval)
 * =========================================================================== */
static nop_status_t handle_get_event_ext_info_config(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    (void)handler_context;
    if (g_event_ext_info_config_json[channel]) {
        response->content = nop_json_parse(g_event_ext_info_config_json[channel],
                                           strlen(g_event_ext_info_config_json[channel]));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        nop_json_add_bool(response->content, "enable", false);
        nop_json_add_int(response->content, "collectionIntervalMs", 1000);
    }
    return NOP_OK;
}

static nop_status_t handle_set_event_ext_info_config(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "enable") &&
        !nop_json_has(request->args, "collectionIntervalMs"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    nop_json_add_bool(snapshot, "enable",
                      nop_json_bool(request->args, "enable", false));
    nop_json_add_int(snapshot, "collectionIntervalMs",
                     nop_json_num(request->args, "collectionIntervalMs", 1000));
    store_json_snapshot(&g_event_ext_info_config_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Registration
 * =========================================================================== */
void cap_ai_advanced_register(nop_router_t *router)
{
    nop_router_register(router, "AI_getChannelAICapabilities", CAP_AI, handle_get_channel_ai_capabilities);
    nop_router_register(router, "AI_getChannelLineCrossDetect", CAP_AI, handle_get_channel_line_cross_detect);
    nop_router_register(router, "AI_setChannelLineCrossDetect", CAP_AI, handle_set_channel_line_cross_detect);
    nop_router_register(router, "AI_getChannelFieldIntrusionDetect", CAP_AI, handle_get_channel_field_intrusion_detect);
    nop_router_register(router, "AI_setChannelFieldIntrusionDetect", CAP_AI, handle_set_channel_field_intrusion_detect);
    nop_router_register(router, "AI_getEventExtInfo", CAP_AI, handle_get_event_ext_info);
    nop_router_register(router, "AI_getEventExtInfoBatchByReverseTime", CAP_AI, handle_get_event_ext_info_batch_by_reverse_time);
    nop_router_register(router, "AI_getEventExtInfoConfig", CAP_AI, handle_get_event_ext_info_config);
    nop_router_register(router, "AI_setEventExtInfoConfig", CAP_AI, handle_set_event_ext_info_config);
}
