/**
 * @file cap_device.c
 * @brief CAP_DEVICE handlers: getDeviceInfo, X_NightOwl_getDeviceCapabilities,
 *        X_NightOwl_getAPIVersion. Modeled on nop_api/NOP_APP/openapi.json.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_version.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"
#include "nop_sdk/hal/hal_video.h"

static nop_status_t handle_get_device_info(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const hal_system_if *system = (const hal_system_if *)hal_registry_get(HAL_SYSTEM);
    hal_device_info_t    device_info;
    nop_json_t          *content;
    (void)request; (void)handler_context;

    if (!system || !system->get_device_info)
        return NOP_ERR_NOTIMPL;
    if (system->get_device_info(system->ctx, &device_info) != NOP_OK)
        return NOP_ERR_INTERNAL;

    content = nop_json_obj();
    nop_json_add_str(content, "type", device_info.type);
    nop_json_add_str(content, "serialNumber", device_info.serial_number);
    nop_json_add_str(content, "model", device_info.model);
    nop_json_add_str(content, "firmwareVersion", device_info.firmware_version);
    nop_json_add_str(content, "mac", device_info.mac);
    nop_json_add_str(content, "ip", device_info.ip);
    nop_json_add_str(content, "name", device_info.name);
    nop_json_add_int(content, "maximumChannelCount", device_info.maximum_channel_count);
    nop_json_add_int(content, "currentChannelCount", device_info.current_channel_count);
    nop_json_add_bool(content, "upgradeFail", device_info.upgrade_fail ? true : false);
    response->content = content;
    return NOP_OK;
}

static nop_status_t handle_get_device_capabilities(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    nop_business_context_t *business = (nop_business_context_t *)handler_context;
    const hal_video_if     *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    int                     channel_count = 0;
    (void)request;

    if (video && video->channel_count)
        channel_count = video->channel_count(video->ctx);

    /* Same source as routing: summarize from the capability registry. */
    response->content = cap_registry_summary(
        business ? business->capability_registry : NULL, channel_count);
    return response->content ? NOP_OK : NOP_ERR_NOMEM;
}

static nop_status_t handle_get_api_version(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "version", NOP_API_VERSION_STR);
    return NOP_OK;
}

void cap_device_register(nop_router_t *router)
{
    nop_router_register(router, "getDeviceInfo", CAP_DEVICE, handle_get_device_info);
    nop_router_register(router, "X_NightOwl_getDeviceCapabilities", CAP_DEVICE, handle_get_device_capabilities);
    nop_router_register(router, "X_NightOwl_getAPIVersion", CAP_DEVICE, handle_get_api_version);
}
