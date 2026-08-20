/**
 * @file cap_misc_ext.c
 * @brief ONVIF mapping entry points for channel-scoped misc commands.
 *
 * Standalone-camera memory stubs (stats/loading/cloud-history/enhanced-security/
 * zoom-pan/device-active/sensor-linkage/cable-connect, etc.) removed — NVR
 * implements those via app/router LOCAL handlers or returns 501.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "onvif/mapping/nop_onvif_map.h"

static int read_channel(const nop_request_t *request)
{
    int ch = (int)nop_json_num(request->args, "channel", 1);
    return ch < 0 ? 0 : ch;
}

static nop_status_t handle_get_channel_sensor_config(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_get_osd(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_osd(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "osdToken"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_get_channel_privacy_zone(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_channel_privacy_zone(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "privacyZonePoints"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

void cap_misc_ext_register(nop_router_t *router)
{
    nop_router_register(router, "getChannelSensorConfig", CAP_MISC,
                        handle_get_channel_sensor_config);
    nop_router_register(router, "X_NightOwl_getOSD", CAP_MISC, handle_get_osd);
    nop_router_register(router, "X_NightOwl_setOSD", CAP_MISC, handle_set_osd);
    nop_router_register(router, "X_NightOwl_getChannelPrivacyZone", CAP_MISC,
                        handle_get_channel_privacy_zone);
    nop_router_register(router, "X_NightOwl_setChannelPrivacyZone", CAP_MISC,
                        handle_set_channel_privacy_zone);
}
