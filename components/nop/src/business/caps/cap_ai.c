/**
 * @file cap_ai.c
 * @brief CAP_AI smart-detect handlers (detect filter/threshold, PTZ track,
 *        per-sensor config, activity zones).
 *
 * ONVIF 通道走 onvif mapping；NOP 相机不在此处理（NVR 8089 透传相机），
 * 禁止本机内存桩/默认数据。
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "onvif/mapping/nop_onvif_map.h"

/* ===========================================================================
 * AI detect filter (per-sensor min/max size + movement)
 * =========================================================================== */
static nop_status_t handle_get_detect_filter(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)request;
    (void)response;
    (void)handler_context;
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_detect_filter(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)request;
    (void)response;
    (void)handler_context;
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * AI detect threshold (per-sensor confidence threshold)
 * =========================================================================== */
static nop_status_t handle_get_detect_threshold(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel = (int)nop_json_num(request->args, "channel", 0);
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_detect_threshold(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel = (int)nop_json_num(request->args, "channel", 0);
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * PTZ track (smart-track modes)
 * =========================================================================== */
static nop_status_t handle_get_ptz_track(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    (void)request;
    (void)response;
    (void)handler_context;
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_ptz_track(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    (void)request;
    (void)response;
    (void)handler_context;
    return NOP_ERR_NOTIMPL;
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
    return NOP_ERR_NOTIMPL;
}

/* ===========================================================================
 * NightOwl: channel activity-zone types (get-only)
 * =========================================================================== */
static nop_status_t handle_get_activity_zone_types(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    /* 只信 mapping：GetRules/SupportedRules 含 Motion 才报 pixelChange。 */
    return nop_onvif_map_dispatch(handler_context, request, response);
}

/* ===========================================================================
 * NightOwl: channel trigger activity zone (zone geometry + sensitivity)
 * =========================================================================== */
static nop_status_t handle_get_trigger_activity_zone(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    return nop_onvif_map_dispatch(handler_context, request, response);
}

static nop_status_t handle_set_trigger_activity_zone(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    return nop_onvif_map_dispatch(handler_context, request, response);
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
