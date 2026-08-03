/**
 * @file cap_ota.c
 * @brief CAP_OTA handlers: upgradeFirmware / checkFirmwareUpgradeStatus. Gated
 *        by CAP_OTA, backed by HAL_OTA.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ota.h"

static nop_status_t handle_upgrade_firmware(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const hal_ota_if *ota = (const hal_ota_if *)hal_registry_get(HAL_OTA);
    const char       *url;
    int               automatic;
    (void)response; (void)handler_context;

    /* url is optional: when absent the device constructs its own OTA URL. */
    url       = nop_json_str(request->args, "url", NULL);
    automatic = nop_json_bool(request->args, "auto", true) ? 1 : 0;

    if (!ota || !ota->begin_upgrade)
        return NOP_ERR_NOTIMPL;
    return ota->begin_upgrade(ota->ctx, url, automatic);
}

static nop_status_t handle_check_upgrade_status(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    const hal_ota_if *ota = (const hal_ota_if *)hal_registry_get(HAL_OTA);
    hal_ota_status_t  status = HAL_OTA_STATUS_IDLE;
    int               error_code = 0;
    (void)request; (void)handler_context;

    if (!ota || !ota->get_upgrade_status)
        return NOP_ERR_NOTIMPL;
    if (ota->get_upgrade_status(ota->ctx, &status, &error_code) != NOP_OK)
        return NOP_ERR_INTERNAL;

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "status", (double)status);
    if (status == HAL_OTA_STATUS_FAILED)
        nop_json_add_int(response->content, "error", (double)error_code);
    return NOP_OK;
}

void cap_ota_register(nop_router_t *router)
{
    nop_router_register(router, "upgradeFirmware", CAP_OTA, handle_upgrade_firmware);
    nop_router_register(router, "checkFirmwareUpgradeStatus", CAP_OTA, handle_check_upgrade_status);
}
