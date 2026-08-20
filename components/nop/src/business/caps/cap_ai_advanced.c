/**
 * @file cap_ai_advanced.c
 * @brief CAP_AI advanced smart-detect — ONVIF mapping only on NVR.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "onvif/mapping/nop_onvif_map.h"

static int read_channel(const nop_request_t *request)
{
    int ch = (int)nop_json_num(request->args, "channel", 1);
    return ch < 0 ? 0 : ch;
}

static nop_status_t handle_get_channel_ai_capabilities(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_get_channel_line_cross_detect(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_channel_line_cross_detect(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_get_channel_field_intrusion_detect(const nop_request_t *request,
                                                              nop_response_t *response,
                                                              void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_channel_field_intrusion_detect(const nop_request_t *request,
                                                              nop_response_t *response,
                                                              void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, read_channel(request)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

void cap_ai_advanced_register(nop_router_t *router)
{
    nop_router_register(router, "AI_getChannelAICapabilities", CAP_AI,
                        handle_get_channel_ai_capabilities);
    nop_router_register(router, "AI_getChannelLineCrossDetect", CAP_AI,
                        handle_get_channel_line_cross_detect);
    nop_router_register(router, "AI_setChannelLineCrossDetect", CAP_AI,
                        handle_set_channel_line_cross_detect);
    nop_router_register(router, "AI_getChannelFieldIntrusionDetect", CAP_AI,
                        handle_get_channel_field_intrusion_detect);
    nop_router_register(router, "AI_setChannelFieldIntrusionDetect", CAP_AI,
                        handle_set_channel_field_intrusion_detect);
}
