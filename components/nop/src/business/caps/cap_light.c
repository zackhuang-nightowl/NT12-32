/**
 * @file cap_light.c
 * @brief CAP_LIGHT handlers: setChannelLightSwitch / setChannelLightBrightness.
 *        Gated by CAP_LIGHT, backed by HAL_LIGHT.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_light.h"

static nop_status_t handle_set_light_switch(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const hal_light_if *light = (const hal_light_if *)hal_registry_get(HAL_LIGHT);
    int channel, is_on;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    is_on   = nop_json_bool(request->args, "enable", false) ? 1 : 0;

    if (!light || !light->set_switch)
        return NOP_ERR_NOTIMPL;
    return light->set_switch(light->ctx, channel, is_on);
}

static nop_status_t handle_set_light_brightness(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    const hal_light_if *light = (const hal_light_if *)hal_registry_get(HAL_LIGHT);
    int channel, brightness_level;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "brightness"))
        return NOP_ERR_PARAM;
    channel          = (int)nop_json_num(request->args, "channel", 0);
    brightness_level = (int)nop_json_num(request->args, "brightness", 0);

    if (!light || !light->set_brightness)
        return NOP_ERR_NOTIMPL;
    return light->set_brightness(light->ctx, channel, brightness_level);
}

void cap_light_register(nop_router_t *router)
{
    nop_router_register(router, "setChannelLightSwitch", CAP_LIGHT, handle_set_light_switch);
    nop_router_register(router, "setChannelLightBrightness", CAP_LIGHT, handle_set_light_brightness);
}
