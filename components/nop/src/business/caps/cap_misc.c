/**
 * @file cap_misc.c
 * @brief CAP_MISC catch-all handlers: device/channel/cloud-account/p2p commands
 *        that do not belong to a dedicated capability group. Gated by CAP_MISC.
 *
 *        Backed by module-static state so scalar get/set pairs round-trip
 *        (set -> get). Status/info/query/action commands accept their args and
 *        return a spec-shaped object (scalar fields with sensible defaults,
 *        arrays empty) or plain success. Object/array-valued set fields are kept
 *        as a serialized JSON snapshot and parsed back on get; otherwise a spec
 *        default is returned. Modeled on nop_api/NOP_APP/<command>.txt "//Format".
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "onvif/mapping/nop_onvif_map.h"

#include <string.h>
#include <stdlib.h>

#define MISC_MAX_CHANNELS 16

/* ---- module-static state -------------------------------------------------- */

/* Device-wide scalar get/set pairs (round-trip). */
static char g_av_password[64]   = "xxxxxx";
static char g_iotc_auth_key[64] = "xxxxxx";
static char g_serial_number[32] = "";
static char g_current_storage[16] = "sdcard";
static bool g_chromecast_switch = true;
static bool g_time_sync_switch  = true;

/* Owner triple. */
static char g_owner_id[64]  = "";
static char g_owner_user[64] = "";
static char g_owner_stoken[256] = "";

/* Per-channel input channel names (round-trip). */
static char g_channel_name[MISC_MAX_CHANNELS][64];

/* Object/array-valued snapshot: attached IP devices supplied by attachIPDevices. */
static char *g_attach_devices_json = NULL;

static int clamp_channel(int channel)
{
    if (channel < 0)
        channel = 0;
    if (channel >= MISC_MAX_CHANNELS)
        channel = MISC_MAX_CHANNELS - 1;
    return channel;
}

static void copy_str(char *slot, size_t size, const char *value)
{
    strncpy(slot, value, size - 1);
    slot[size - 1] = '\0';
}

/* Store a serialized snapshot of an args sub-object/array into @p slot. */
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

/* ===========================================================================
 * Cloud account / credential scalar get-set pairs
 * =========================================================================== */
static nop_status_t handle_get_av_password(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", g_av_password);
    return NOP_OK;
}

static nop_status_t handle_set_av_password(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const char *value = nop_json_str(request->args, "value", NULL);
    (void)response; (void)handler_context;
    if (!value)
        return NOP_ERR_PARAM;
    copy_str(g_av_password, sizeof(g_av_password), value);
    return NOP_OK;
}

static nop_status_t handle_get_iotc_auth_key(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", g_iotc_auth_key);
    return NOP_OK;
}

static nop_status_t handle_set_iotc_auth_key(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char *value = nop_json_str(request->args, "value", NULL);
    (void)response; (void)handler_context;
    if (!value)
        return NOP_ERR_PARAM;
    copy_str(g_iotc_auth_key, sizeof(g_iotc_auth_key), value);
    return NOP_OK;
}

static nop_status_t handle_get_current_clouds(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    nop_json_t *available;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "currentCloud", "tutk");
    available = nop_json_arr();
    nop_json_arr_push_str(available, "tutk");
    nop_json_arr_push_str(available, "pepper");
    nop_json_add(response->content, "availableClouds", available);
    return NOP_OK;
}

static nop_status_t handle_get_owner(const nop_request_t *request,
                                     nop_response_t *response,
                                     void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "owner_id", g_owner_id);
    nop_json_add_str(response->content, "username", g_owner_user);
    nop_json_add_str(response->content, "stoken", g_owner_stoken);
    return NOP_OK;
}

static nop_status_t handle_set_owner(const nop_request_t *request,
                                     nop_response_t *response,
                                     void *handler_context)
{
    const char *owner_id = nop_json_str(request->args, "owner_id", NULL);
    const char *username = nop_json_str(request->args, "username", NULL);
    const char *stoken   = nop_json_str(request->args, "stoken", NULL);
    (void)response; (void)handler_context;
    if (!owner_id || !username || !stoken)
        return NOP_ERR_PARAM;
    copy_str(g_owner_id, sizeof(g_owner_id), owner_id);
    copy_str(g_owner_user, sizeof(g_owner_user), username);
    copy_str(g_owner_stoken, sizeof(g_owner_stoken), stoken);
    return NOP_OK;
}

/* ===========================================================================
 * Chromecast / time-sync switches (round-trip) and chromecast info
 * =========================================================================== */
static nop_status_t handle_get_chromecast_switch(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_chromecast_switch);
    return NOP_OK;
}

static nop_status_t handle_set_chromecast_switch(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_chromecast_switch = nop_json_bool(request->args, "value", g_chromecast_switch);
    return NOP_OK;
}

static nop_status_t handle_get_chromecast_info(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_json_t *protocol, *stream_type, *channels;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "deviceName", "officeDVR");
    nop_json_add_str(response->content, "firmwareVersion", "");
    nop_json_add_str(response->content, "serialNumber", g_serial_number);
    nop_json_add_str(response->content, "model", "");
    nop_json_add_str(response->content, "mac", "");
    nop_json_add_str(response->content, "manufacturer", "NightOwl");
    nop_json_add_str(response->content, "type", "videoRecorder");
    nop_json_add_str(response->content, "ip", "");
    protocol = nop_json_arr();
    nop_json_add(response->content, "protocol", protocol);
    stream_type = nop_json_arr();
    nop_json_add(response->content, "streamType", stream_type);
    channels = nop_json_arr();
    nop_json_add(response->content, "channels", channels);
    nop_json_add_int(response->content, "maximumChannelCount", 0);
    nop_json_add_int(response->content, "currentChannelCount", 0);
    return NOP_OK;
}

static nop_status_t handle_get_time_sync_switch(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "enable", g_time_sync_switch);
    return NOP_OK;
}

static nop_status_t handle_set_time_sync_switch(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    g_time_sync_switch = nop_json_bool(request->args, "enable", g_time_sync_switch);
    return NOP_OK;
}

/* ===========================================================================
 * Input channel names (per-channel round-trip)
 * =========================================================================== */
static nop_status_t handle_get_input_channel_name(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "name", g_channel_name[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_input_channel_name(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int         channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *name    = nop_json_str(request->args, "name", NULL);
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !name)
        return NOP_ERR_PARAM;
    copy_str(g_channel_name[channel], sizeof(g_channel_name[channel]), name);
    return NOP_OK;
}

static nop_status_t handle_get_input_channel_names(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    nop_json_t *names;
    int         channel;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    names = nop_json_arr();
    for (channel = 0; channel < MISC_MAX_CHANNELS; ++channel) {
        if (g_channel_name[channel][0] != '\0') {
            nop_json_t *entry = nop_json_obj();
            nop_json_add_str(entry, "name", g_channel_name[channel]);
            nop_json_add_int(entry, "channel", channel);
            nop_json_arr_push(names, entry);
        }
    }
    nop_json_add(response->content, "names", names);
    return NOP_OK;
}

/* ===========================================================================
 * Channel status / info / battery queries
 * =========================================================================== */
static nop_status_t handle_get_channels_status(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add(response->content, "channels", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_get_channel_status(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "status", 0);
    return NOP_OK;
}

static nop_status_t handle_get_channel_info(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "name", g_channel_name[channel]);
    nop_json_add_str(response->content, "firmwareVersion", "");
    nop_json_add_str(response->content, "serialNumber", "");
    nop_json_add_str(response->content, "model", "");
    nop_json_add_str(response->content, "mac", "");
    nop_json_add_str(response->content, "manufacturer", "NightOwl");
    nop_json_add_str(response->content, "type", "");
    nop_json_add_str(response->content, "ip", "");
    nop_json_add_str(response->content, "storageType", "none");
    nop_json_add_str(response->content, "network", "");
    nop_json_add_int(response->content, "signalStrength", 0);
    return NOP_OK;
}

static nop_status_t handle_get_channel_battery_status(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "isCharging", false);
    nop_json_add_int(response->content, "batteryLevel", 0);
    return NOP_OK;
}

/* ===========================================================================
 * Power supply statistics
 * =========================================================================== */
static nop_status_t handle_get_power_supply_stats(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add(response->content, "timestamp", nop_json_arr());
    nop_json_add(response->content, "voltage", nop_json_arr());
    nop_json_add(response->content, "solarEnergyWh", nop_json_arr());
    nop_json_add(response->content, "solarTodayWh", nop_json_arr());
    nop_json_add_int(response->content, "greenEnergyTotal", 0);
    nop_json_add(response->content, "charge", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_set_power_supply_stats(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ===========================================================================
 * Profile / storage / serial number
 * =========================================================================== */
static nop_status_t handle_get_profile(const nop_request_t *request,
                                       nop_response_t *response,
                                       void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    return NOP_OK;
}

static nop_status_t handle_set_current_storage(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    const char *value = nop_json_str(request->args, "value", NULL);
    (void)response; (void)handler_context;
    if (!value)
        return NOP_ERR_PARAM;
    copy_str(g_current_storage, sizeof(g_current_storage), value);
    return NOP_OK;
}

static nop_status_t handle_set_serial_number(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char *security_code = nop_json_str(request->args, "securityCode", NULL);
    const char *serial        = nop_json_str(request->args, "serial", NULL);
    (void)response; (void)handler_context;
    if (!security_code || !serial)
        return NOP_ERR_PARAM;
    copy_str(g_serial_number, sizeof(g_serial_number), serial);
    return NOP_OK;
}

/* ===========================================================================
 * Tunnel / sleep / firmware actions
 * =========================================================================== */
static nop_status_t handle_build_tunnel(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)handler_context;
    if (!nop_json_str(request->args, "protocol", NULL))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "username", "admin");
    nop_json_add_str(response->content, "password", g_av_password);
    nop_json_add_int(response->content, "iotc-channel", 0);
    return NOP_OK;
}

static nop_status_t handle_sleep_camera(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_sleep_packet(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    return NOP_OK;
}

static nop_status_t handle_wake_up_camera(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_write_firmware(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    return NOP_OK;
}

static nop_status_t handle_upgrade_channel_firmware(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_str(request->args, "url", NULL))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_check_channel_upgrade_status(const nop_request_t *request,
                                                        nop_response_t *response,
                                                        void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "status", 0);
    return NOP_OK;
}

/* ===========================================================================
 * IP device attach / detach
 * =========================================================================== */
static nop_status_t handle_attach_ip_devices(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    nop_json_t *devices;
    (void)handler_context;
    devices = nop_json_get(request->args, "devices");
    if (!devices || !nop_json_is_arr(devices))
        return NOP_ERR_PARAM;
    store_json_snapshot(&g_attach_devices_json, devices);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

static nop_status_t handle_detach_ip_device(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_get(request->args, "device"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_get_attach_status(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add(response->content, "devices", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * P2P credential / upload service / reset / unlock / login
 * =========================================================================== */
static nop_status_t handle_update_p2p_credential(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "func", "X_NightOwl_updateP2PCredential");
    nop_json_add_str(response->content, "authKey", g_iotc_auth_key);
    nop_json_add_str(response->content, "avPassword", g_av_password);
    return NOP_OK;
}

static nop_status_t handle_set_upload_service(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    const char *url   = nop_json_str(request->args, "url", NULL);
    const char *token = nop_json_str(request->args, "token", NULL);
    (void)response; (void)handler_context;
    if (!url || !token)
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_reset_to_factory_settings(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    return NOP_OK;
}

static nop_status_t handle_unlock(const nop_request_t *request,
                                  nop_response_t *response,
                                  void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "func", "X_NightOwl_unlock");
    return NOP_OK;
}

static nop_status_t handle_login_user(const nop_request_t *request,
                                      nop_response_t *response,
                                      void *handler_context)
{
    const char *user    = nop_json_str(request->args, "user", NULL);
    const char *ble_key = nop_json_str(request->args, "BLEKey", NULL);
    (void)handler_context;
    if (!user || !ble_key)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "func", "X_NightOwl_loginUser");
    return NOP_OK;
}

/* ===========================================================================
 * Registration: every command under CAP_MISC (39 total)
 * =========================================================================== */
void cap_misc_register(nop_router_t *router)
{
    nop_router_register(router, "buildTunnel", CAP_MISC, handle_build_tunnel);
    nop_router_register(router, "getAvPassword", CAP_MISC, handle_get_av_password);
    nop_router_register(router, "setAvPassword", CAP_MISC, handle_set_av_password);
    nop_router_register(router, "getChannelsStatus", CAP_MISC, handle_get_channels_status);
    nop_router_register(router, "getCurrentClouds", CAP_MISC, handle_get_current_clouds);
    nop_router_register(router, "getIotcAuthKey", CAP_MISC, handle_get_iotc_auth_key);
    nop_router_register(router, "setIotcAuthKey", CAP_MISC, handle_set_iotc_auth_key);
    nop_router_register(router, "getPowerSupplyStats", CAP_MISC, handle_get_power_supply_stats);
    nop_router_register(router, "setPowerSupplyStats", CAP_MISC, handle_set_power_supply_stats);
    nop_router_register(router, "getProfile", CAP_MISC, handle_get_profile);
    nop_router_register(router, "setCurrentStorage", CAP_MISC, handle_set_current_storage);
    nop_router_register(router, "setSerialNumber", CAP_MISC, handle_set_serial_number);
    nop_router_register(router, "sleepCamera", CAP_MISC, handle_sleep_camera);
    nop_router_register(router, "sleepPacket", CAP_MISC, handle_sleep_packet);
    nop_router_register(router, "writeFirmware", CAP_MISC, handle_write_firmware);
    nop_router_register(router, "X_NightOwl_attachIPDevices", CAP_MISC, handle_attach_ip_devices);
    nop_router_register(router, "X_NightOwl_checkChannelUpgradeStatus", CAP_MISC, handle_check_channel_upgrade_status);
    nop_router_register(router, "X_NightOwl_detachIPDevice", CAP_MISC, handle_detach_ip_device);
    nop_router_register(router, "X_NightOwl_getAttachStatus", CAP_MISC, handle_get_attach_status);
    nop_router_register(router, "X_NightOwl_getChannelBatteryStatus", CAP_MISC, handle_get_channel_battery_status);
    nop_router_register(router, "X_NightOwl_getChannelInfo", CAP_MISC, handle_get_channel_info);
    nop_router_register(router, "X_NightOwl_getChannelStatus", CAP_MISC, handle_get_channel_status);
    nop_router_register(router, "X_NightOwl_getChromecastInfo", CAP_MISC, handle_get_chromecast_info);
    nop_router_register(router, "X_NightOwl_getChromecastSwitch", CAP_MISC, handle_get_chromecast_switch);
    nop_router_register(router, "X_NightOwl_setChromecastSwitch", CAP_MISC, handle_set_chromecast_switch);
    nop_router_register(router, "X_NightOwl_getInputChannelName", CAP_MISC, handle_get_input_channel_name);
    nop_router_register(router, "X_NightOwl_getInputChannelNames", CAP_MISC, handle_get_input_channel_names);
    nop_router_register(router, "X_NightOwl_setInputChannelName", CAP_MISC, handle_set_input_channel_name);
    nop_router_register(router, "X_NightOwl_getOwner", CAP_MISC, handle_get_owner);
    nop_router_register(router, "X_NightOwl_setOwner", CAP_MISC, handle_set_owner);
    nop_router_register(router, "X_NightOwl_getTimeSyncSwitch", CAP_MISC, handle_get_time_sync_switch);
    nop_router_register(router, "X_NightOwl_setTimeSyncSwitch", CAP_MISC, handle_set_time_sync_switch);
    nop_router_register(router, "X_NightOwl_resetToFactorySettings", CAP_MISC, handle_reset_to_factory_settings);
    nop_router_register(router, "X_NightOwl_setUploadService", CAP_MISC, handle_set_upload_service);
    nop_router_register(router, "X_NightOwl_unlock", CAP_MISC, handle_unlock);
    nop_router_register(router, "X_NightOwl_updateP2PCredential", CAP_MISC, handle_update_p2p_credential);
    nop_router_register(router, "X_NightOwl_upgradeChannelFirmware", CAP_MISC, handle_upgrade_channel_firmware);
    nop_router_register(router, "X_NightOwl_wakeUpCamera", CAP_MISC, handle_wake_up_camera);
    nop_router_register(router, "X_NightOwl_loginUser", CAP_MISC, handle_login_user);
}
