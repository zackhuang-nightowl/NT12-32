/**
 * @file cap_ptz.c
 * @brief CAP_PTZ handlers: ptzMove / ptzGotoPreset. Gated by CAP_PTZ, backed by
 *        HAL_PTZ.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "onvif/mapping/nop_onvif_map.h"

#include <string.h>

static hal_ptz_dir_t parse_direction(const char *direction_name)
{
    if (!direction_name)                       return HAL_PTZ_STOP;
    if (!strcmp(direction_name, "up"))         return HAL_PTZ_UP;
    if (!strcmp(direction_name, "down"))       return HAL_PTZ_DOWN;
    if (!strcmp(direction_name, "left"))       return HAL_PTZ_LEFT;
    if (!strcmp(direction_name, "right"))      return HAL_PTZ_RIGHT;
    if (!strcmp(direction_name, "zoomIn"))     return HAL_PTZ_ZOOM_IN;
    if (!strcmp(direction_name, "zoomOut"))    return HAL_PTZ_ZOOM_OUT;
    return HAL_PTZ_STOP;
}

static nop_status_t handle_ptz_move(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    const char       *direction_name;
    int               channel, speed;
    (void)response; (void)handler_context;

    direction_name = nop_json_str(request->args, "direction", NULL);
    if (!direction_name)
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    speed   = (int)nop_json_num(request->args, "speed", 50);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->move)
        return NOP_ERR_NOTIMPL;
    return ptz->move(ptz->ctx, channel, parse_direction(direction_name), speed);
}

static nop_status_t handle_ptz_goto_preset(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    int               channel, preset;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "preset"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    preset  = (int)nop_json_num(request->args, "preset", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->goto_preset)
        return NOP_ERR_NOTIMPL;
    return ptz->goto_preset(ptz->ctx, channel, preset);
}

static nop_status_t handle_ptz_move_by_step(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    const char       *direction_name;
    int               channel, step;
    (void)response; (void)handler_context;

    direction_name = nop_json_str(request->args, "direction", NULL);
    if (!direction_name)
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    step    = (int)nop_json_num(request->args, "step", 1);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->move_by_step)
        return NOP_ERR_NOTIMPL;
    return ptz->move_by_step(ptz->ctx, channel, parse_direction(direction_name), step);
}

static nop_status_t handle_ptz_move_stop(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    int               channel;
    (void)response; (void)handler_context;

    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->stop)
        return NOP_ERR_NOTIMPL;
    return ptz->stop(ptz->ctx, channel);
}

static nop_status_t handle_ptz_focus_by_step(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    const char       *direction_name;
    int               channel, step, focus_in;
    (void)response; (void)handler_context;

    direction_name = nop_json_str(request->args, "direction", NULL);
    if (!direction_name)
        return NOP_ERR_PARAM;
    channel  = (int)nop_json_num(request->args, "channel", 0);
    step     = (int)nop_json_num(request->args, "step", 1);
    focus_in = !strcmp(direction_name, "focusIn") ? 1 : 0;

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->focus_by_step)
        return NOP_ERR_NOTIMPL;
    return ptz->focus_by_step(ptz->ctx, channel, focus_in, step);
}

static nop_status_t handle_ptz_focus_stop(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const hal_ptz_if *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    int               channel;
    (void)response; (void)handler_context;

    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    if (!ptz || !ptz->focus_stop)
        return NOP_ERR_NOTIMPL;
    return ptz->focus_stop(ptz->ctx, channel);
}

void cap_ptz_register(nop_router_t *router)
{
    nop_router_register(router, "ptzMove", CAP_PTZ, handle_ptz_move);
    nop_router_register(router, "ptzGotoPreset", CAP_PTZ, handle_ptz_goto_preset);
    nop_router_register(router, "ptzMoveByStep", CAP_PTZ, handle_ptz_move_by_step);
    nop_router_register(router, "ptzMoveStop", CAP_PTZ, handle_ptz_move_stop);
    nop_router_register(router, "ptzFocusByStep", CAP_PTZ, handle_ptz_focus_by_step);
    nop_router_register(router, "ptzFocusStop", CAP_PTZ, handle_ptz_focus_stop);
}
