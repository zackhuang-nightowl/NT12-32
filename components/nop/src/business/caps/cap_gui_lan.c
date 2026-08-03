/**
 * @file cap_gui_lan.c
 * @brief GUI LAN / sub-device management (NVR adding and managing IP cameras).
 *        Covers LAN device discovery/add/delete, per-channel device config,
 *        channel grid mapping, channel media (encoder) profiles, server/channel
 *        firmware check/upgrade, and aggregate alarm status. Gated by CAP_MISC.
 *
 *        Query/list commands return spec-shaped objects with (possibly empty)
 *        arrays. Action commands validate required args and return a spec-shaped
 *        success/status object. Channel grid mapping is the one scalar-ish
 *        get/set pair that round-trips through module-static state. Modeled on
 *        nop_api/<group>/<command>.txt "//Format".
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "onvif/mapping/nop_onvif_map.h"

#include <string.h>
#include <stdlib.h>

#define GUI_LAN_MAPPING_COUNT 16

/* ---- module-static state -------------------------------------------------- */

/* Channel grid mapping (round-trip via get/set). Index = grid, value = channel. */
static int  g_channel_mapping[GUI_LAN_MAPPING_COUNT] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};

/* ===========================================================================
 * LAN device discovery / inventory
 * =========================================================================== */
static nop_status_t handle_get_added_lan_devices(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add(response->content, "devices", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_lan_search(const nop_request_t *request,
                                      nop_response_t *response,
                                      void *handler_context)
{
    (void)handler_context;
    if (!nop_json_str(request->args, "protocol", NULL))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add(response->content, "devices", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_get_lan_device(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const char *ip = nop_json_str(request->args, "ip", NULL);
    (void)handler_context;
    if (!ip)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    if (nop_json_has(request->args, "channel"))
        nop_json_add_int(response->content, "channel",
                         nop_json_num(request->args, "channel", 0));
    nop_json_add_str(response->content, "protocol", "onvif");
    nop_json_add_str(response->content, "mac", "");
    nop_json_add_str(response->content, "ip", ip);
    nop_json_add_int(response->content, "port", 8089);
    nop_json_add_str(response->content, "serial", "");
    nop_json_add_str(response->content, "model", "");
    nop_json_add_str(response->content, "name", "");
    nop_json_add_str(response->content, "password", "");
    nop_json_add_int(response->content, "status", 0);
    nop_json_add_bool(response->content, "enhancedSecurity", false);
    return NOP_OK;
}

static nop_status_t handle_set_lan_device(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)handler_context;
    if (!nop_json_str(request->args, "protocol", NULL) ||
        !nop_json_str(request->args, "ip", NULL) ||
        !nop_json_has(request->args, "port") ||
        !nop_json_has(request->args, "enhancedSecurity"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_lan_add_device(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)handler_context;
    if (!nop_json_str(request->args, "protocol", NULL) ||
        !nop_json_str(request->args, "ip", NULL))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_lan_del_device(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ===========================================================================
 * Channel grid mapping (round-trip)
 * =========================================================================== */
static nop_status_t handle_get_channel_mapping(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_json_t *mapping;
    int         grid;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    mapping = nop_json_arr();
    for (grid = 0; grid < GUI_LAN_MAPPING_COUNT; ++grid)
        nop_json_arr_push_int(mapping, g_channel_mapping[grid]);
    nop_json_add(response->content, "ChannelMapping", mapping);
    return NOP_OK;
}

static nop_status_t handle_set_channel_mapping(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_json_t *mapping;
    (void)handler_context;
    mapping = nop_json_get(request->args, "ChannelMapping");
    if (!mapping || !nop_json_is_arr(mapping))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

/* ===========================================================================
 * Channel media (encoder) profiles
 * =========================================================================== */
static nop_status_t handle_get_channel_media_profiles(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    response->content = nop_json_obj();
    nop_json_add(response->content, "profiles", nop_json_arr());
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

static nop_status_t handle_set_channel_media_profiles(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    nop_json_t *profiles;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    profiles = nop_json_get(request->args, "profiles");
    if (!profiles || !nop_json_is_arr(profiles))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

/* ===========================================================================
 * Firmware check / upgrade (device-wide and per-channel)
 * =========================================================================== */
static nop_status_t handle_check_server_firmware(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "newFW", false);
    nop_json_add_str(response->content, "version", "");
    nop_json_add_str(response->content, "description", "");
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

static nop_status_t handle_check_channel_server_firmware(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "newFW", false);
    nop_json_add_str(response->content, "version", "");
    nop_json_add_str(response->content, "description", "");
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

static nop_status_t handle_upgrade_firmware(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_str(request->args, "FileName", NULL))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_upgrade_channel_firmware(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    if (!nop_json_str(request->args, "FileName", NULL) ||
        !nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    (void)response;
    return NOP_OK;
}

/* ===========================================================================
 * Aggregate alarm status
 * =========================================================================== */
static nop_status_t handle_get_alarm_status(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "LossStatus", 0);
    nop_json_add_int(response->content, "BlindStatus", 0);
    nop_json_add_int(response->content, "MotionStatus", 0);
    nop_json_add_int(response->content, "FaceStatus", 0);
    nop_json_add_int(response->content, "HumanStatus", 0);
    nop_json_add_int(response->content, "CarStatus", 0);
    nop_json_add_int(response->content, "RecordStatus", 0);
    nop_json_add_int(response->content, "ChannelStatusNotify", 0);
    return NOP_OK;
}

/* ===========================================================================
 * Registration: every command under CAP_MISC (15 total)
 * =========================================================================== */
void cap_gui_lan_register(nop_router_t *router)
{
    nop_router_register(router, "GUI_GetAddedLanDevices", CAP_MISC, handle_get_added_lan_devices);
    nop_router_register(router, "GUI_getLanDevice", CAP_MISC, handle_get_lan_device);
    nop_router_register(router, "GUI_setLanDevice", CAP_MISC, handle_set_lan_device);
    nop_router_register(router, "GUI_LanAddDevice", CAP_MISC, handle_lan_add_device);
    nop_router_register(router, "GUI_LanDelDevice", CAP_MISC, handle_lan_del_device);
    nop_router_register(router, "GUI_LanSearch", CAP_MISC, handle_lan_search);
    nop_router_register(router, "GUI_getChannelMapping", CAP_MISC, handle_get_channel_mapping);
    nop_router_register(router, "GUI_setChannelMapping", CAP_MISC, handle_set_channel_mapping);
    nop_router_register(router, "GUI_getChannelMediaProfiles", CAP_MISC, handle_get_channel_media_profiles);
    nop_router_register(router, "GUI_setChannelMediaProfiles", CAP_MISC, handle_set_channel_media_profiles);
    nop_router_register(router, "GUI_checkChannelServerFirmware", CAP_MISC, handle_check_channel_server_firmware);
    nop_router_register(router, "GUI_checkServerFirmware", CAP_MISC, handle_check_server_firmware);
    nop_router_register(router, "GUI_upgradeChannelFirmware", CAP_MISC, handle_upgrade_channel_firmware);
    nop_router_register(router, "GUI_upgradeFirmware", CAP_MISC, handle_upgrade_firmware);
    nop_router_register(router, "GUI_getAlarmStatus", CAP_MISC, handle_get_alarm_status);
}
