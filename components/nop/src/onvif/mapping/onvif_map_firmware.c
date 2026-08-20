/**
 * @file onvif_map_firmware.c
 * @brief §6 Firmware handlers — NOP GUI_/X_NightOwl_upgradeChannelFirmware ->
 *        ONVIF StartFirmwareUpgrade + upload (via the existing client ABI
 *        nop_onvif_firmware_upgrade). Args: {channel, FileName}.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_utils.h"
#include "base/nop_json.h"

nop_status_t onvif_map_upgradeChannelFirmware(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *file;
    nop_status_t     rc;

    /* GUI_ uses "FileName"; X_NightOwl_ uses "url" — accept either. */
    file = nop_json_str(req->args, "FileName", NULL);
    if (!file)
        file = nop_json_str(req->args, "url", NULL);
    if (!file)
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s)
        return ONVIF_MAP_FAIL;
    rc = onvif_map_rc(nop_onvif_firmware_upgrade(onvif_session_dev(s), file));
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif firmware upgrade failed");
    return rc;
}

#endif /* NOP_ONVIF_MAP */
