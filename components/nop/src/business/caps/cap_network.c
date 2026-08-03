/**
 * @file cap_network.c
 * @brief CAP_NETWORK handlers: wifi / wireless camera / wireless chime commands.
 *        Gated by CAP_NETWORK. Backed by module-static state so config get/set
 *        round-trips (wifi ssid + wakeup window, chime settings); action commands
 *        (start/stop add/remove, ring) accept args and return success; query/list
 *        commands return spec-shaped (possibly empty) arrays/objects. Firmware can
 *        override these via a network HAL extension later.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_nvr_addcamera.h"
#include "nop_sdk/nop_chime.h"

#include <string.h>

/* ---- wifi connection state ------------------------------------------------ */
static char g_wifi_ssid[64]      = "";
static char g_wifi_password[128] = "";
static int  g_wifi_enctype       = 5;
static int  g_wifi_signal        = 70;
static char g_wifi_status[16]    = "connected";

/* ---- wifi config (sleepable device wakeup window) ------------------------- */
static bool g_wakeup_window_extend = false;

/* ---- current WAN interface ------------------------------------------------ */
static char g_wan_interface[16] = "wifi";

static nop_status_t handle_get_current_wifi(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "ssid", g_wifi_ssid);
    nop_json_add_int(response->content, "enctype", g_wifi_enctype);
    nop_json_add_int(response->content, "signal", g_wifi_signal);
    nop_json_add_str(response->content, "status", g_wifi_status);
    return NOP_OK;
}

static nop_status_t handle_get_wifi_config(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "wakeupWindowExtend", g_wakeup_window_extend);
    return NOP_OK;
}

static nop_status_t handle_set_wifi_config(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "wakeupWindowExtend"))
        return NOP_ERR_PARAM;
    g_wakeup_window_extend = nop_json_bool(request->args, "wakeupWindowExtend",
                                           g_wakeup_window_extend);
    return NOP_OK;
}

static nop_status_t handle_set_wifi(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const char *ssid     = nop_json_str(request->args, "ssid", NULL);
    const char *password = nop_json_str(request->args, "pwd", NULL);
    (void)response; (void)handler_context;
    if (!ssid || !password)
        return NOP_ERR_PARAM;
    strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid) - 1);
    g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
    strncpy(g_wifi_password, password, sizeof(g_wifi_password) - 1);
    g_wifi_password[sizeof(g_wifi_password) - 1] = '\0';
    strncpy(g_wifi_status, "connected", sizeof(g_wifi_status) - 1);
    g_wifi_status[sizeof(g_wifi_status) - 1] = '\0';
    return NOP_OK;
}

static nop_status_t handle_query_wifi_list(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)request; (void)handler_context;
    /* No scan source wired yet — report an empty list so clients get a
     * well-formed response. Firmware can supply scan results via a HAL later. */
    response->content = nop_json_obj();
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_get_wan_interface(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    nop_json_t *list, *connected;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", g_wan_interface);
    list = nop_json_arr();
    nop_json_arr_push_str(list, "wifi");
    nop_json_arr_push_str(list, "eth");
    nop_json_add(response->content, "list", list);
    connected = nop_json_arr();
    nop_json_arr_push_str(connected, g_wan_interface);
    nop_json_add(response->content, "connected", connected);
    return NOP_OK;
}

static nop_status_t handle_get_channel_wifi_signal_strength(const nop_request_t *request,
                                                            nop_response_t *response,
                                                            void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "strength", g_wifi_signal);
    return NOP_OK;
}

/* ---- wireless chimes ------------------------------------------------------ */
/* The wireless-chime registry/pairing engine attached via nop_app_set_chime(). */
static nop_chime_t *chime_engine(void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    return context ? (nop_chime_t *)context->chime : NULL;
}

static const char *chime_state_name(nop_chime_pair_state_t state)
{
    switch (state) {
    case NOP_CHIME_PAIRING: return "pairing";
    case NOP_CHIME_DONE:    return "done";
    case NOP_CHIME_IDLE:
    default:                return "idle";
    }
}

static nop_status_t handle_get_wireless_chimes(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_chime_t *engine = chime_engine(handler_context);
    nop_json_t  *chimes;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    chimes = nop_json_arr();
    if (engine) {
        nop_chime_pair_state_t state;
        nop_chime_entry_t      list[16];
        int                    n, i;
        nop_chime_pair_status(engine, &state, NULL, NULL);
        nop_json_add_str(response->content, "status", chime_state_name(state));
        n = nop_chime_list(engine, list, 16);
        for (i = 0; i < n; ++i) {
            nop_json_t *entry = nop_json_obj();
            nop_json_add_str(entry, "id", list[i].id);
            nop_json_add_str(entry, "name", list[i].name);
            nop_json_add_int(entry, "volume", list[i].volume);
            nop_json_add_int(entry, "tone", list[i].tone_id);
            nop_json_arr_push(chimes, entry);
        }
    } else {
        nop_json_add_str(response->content, "status", "idle");
    }
    nop_json_add(response->content, "chimes", chimes);
    return NOP_OK;
}

static nop_status_t handle_set_wireless_chimes(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_chime_t *engine = chime_engine(handler_context);
    nop_json_t  *chimes;
    (void)response;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    chimes = nop_json_get(request->args, "channels");
    if (!chimes || !nop_json_is_arr(chimes))
        return NOP_ERR_PARAM;

    /* Apply each {id, volume, tone} to the registry (best-effort per entry). */
    if (engine) {
        int size = nop_json_arr_size(chimes), i;
        for (i = 0; i < size; ++i) {
            const nop_json_t *item = nop_json_arr_at(chimes, i);
            const char       *id = item ? nop_json_str(item, "id", NULL) : NULL;
            if (id)
                nop_chime_set(engine, id, (int)nop_json_num(item, "volume", 50),
                              (int)nop_json_num(item, "tone", 0));
        }
    }
    return NOP_OK;
}

static nop_status_t handle_ring_wireless_chimes(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_is_arr(nop_json_get(request->args, "channels")))
        return NOP_ERR_PARAM;
    /* The actual RF ring is a firmware transmit; the registry only tracks state. */
    return NOP_OK;
}

/* ---- add / remove wireless cameras ---------------------------------------- */
/* The AddWirelessCameras engine attached via nop_app_set_nvr_addcamera(). */
static nop_nvr_addcamera_t *addcamera_engine(void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    return context ? (nop_nvr_addcamera_t *)context->nvr_addcamera : NULL;
}

static const char *addcamera_state_name(nop_addcamera_state_t state)
{
    switch (state) {
    case NOP_ADDCAMERA_SCANNING: return "scanning";
    case NOP_ADDCAMERA_DONE:     return "done";
    case NOP_ADDCAMERA_IDLE:
    default:                     return "idle";
    }
}

static nop_status_t handle_get_add_wireless_cameras_status(const nop_request_t *request,
                                                           nop_response_t *response,
                                                           void *handler_context)
{
    nop_nvr_addcamera_t   *engine = addcamera_engine(handler_context);
    nop_addcamera_status_t status;
    (void)request;

    response->content = nop_json_obj();
    if (engine) {
        nop_nvr_addcamera_status(engine, &status);
        nop_json_add_str(response->content, "status", addcamera_state_name(status.state));
        nop_json_add_int(response->content, "addedCount", status.added_count);
        nop_json_add_int(response->content, "secondsRemaining", status.seconds_remaining);
    } else {
        /* No engine attached — report idle (backward-compatible). */
        nop_json_add_str(response->content, "status", "idle");
    }
    nop_json_add(response->content, "channels", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_start_add_wireless_cameras(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    nop_nvr_addcamera_t *engine = addcamera_engine(handler_context);
    (void)request; (void)response;
    /* Open the pairing window (default duration). Idempotent if already open. */
    if (engine)
        nop_nvr_addcamera_start(engine, 0);
    return NOP_OK;
}

static nop_status_t handle_stop_add_wireless_cameras(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    nop_nvr_addcamera_t *engine = addcamera_engine(handler_context);
    (void)request; (void)response;
    if (engine)
        nop_nvr_addcamera_stop(engine);
    return NOP_OK;
}

static nop_status_t handle_start_remove_wireless_cameras(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_is_arr(nop_json_get(request->args, "channels")))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ---- add / remove wireless chimes ----------------------------------------- */
static nop_status_t handle_start_add_wireless_chime(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    nop_chime_t *engine = chime_engine(handler_context);
    (void)response;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (engine)
        nop_chime_pair_start(engine, 0);   /* open pairing window (default) */
    return NOP_OK;
}

static nop_status_t handle_stop_add_wireless_chime(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    nop_chime_t *engine = chime_engine(handler_context);
    (void)response;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (engine)
        nop_chime_pair_stop(engine);
    return NOP_OK;
}

static nop_status_t handle_start_remove_wireless_chime(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    nop_chime_t *engine = chime_engine(handler_context);
    nop_json_t  *chimes;
    (void)response;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    chimes = nop_json_get(request->args, "channels");
    if (!chimes || !nop_json_is_arr(chimes))
        return NOP_ERR_PARAM;
    /* Unpair each listed chime id from the registry. */
    if (engine) {
        int size = nop_json_arr_size(chimes), i;
        for (i = 0; i < size; ++i) {
            const nop_json_t *item = nop_json_arr_at(chimes, i);
            const char       *id = item ? nop_json_str(item, "id", NULL) : NULL;
            if (id)
                nop_chime_remove(engine, id);
        }
    }
    return NOP_OK;
}

void cap_network_register(nop_router_t *router)
{
    nop_router_register(router, "getCurrentWifi", CAP_NETWORK, handle_get_current_wifi);
    nop_router_register(router, "getWiFiConfig", CAP_NETWORK, handle_get_wifi_config);
    nop_router_register(router, "setWiFiConfig", CAP_NETWORK, handle_set_wifi_config);
    nop_router_register(router, "setWifi", CAP_NETWORK, handle_set_wifi);
    nop_router_register(router, "queryWifiList", CAP_NETWORK, handle_query_wifi_list);
    nop_router_register(router, "getWanInterface", CAP_NETWORK, handle_get_wan_interface);
    nop_router_register(router, "X_NightOwl_getChannelWifiSignalStrength", CAP_NETWORK,
                        handle_get_channel_wifi_signal_strength);
    nop_router_register(router, "getWirelessChimes", CAP_NETWORK, handle_get_wireless_chimes);
    nop_router_register(router, "setWirelessChimes", CAP_NETWORK, handle_set_wireless_chimes);
    nop_router_register(router, "ringWirelessChimes", CAP_NETWORK, handle_ring_wireless_chimes);
    nop_router_register(router, "getAddWirelessCamerasStatus", CAP_NETWORK,
                        handle_get_add_wireless_cameras_status);
    nop_router_register(router, "startAddWirelessCameras", CAP_NETWORK,
                        handle_start_add_wireless_cameras);
    nop_router_register(router, "stopAddWirelessCameras", CAP_NETWORK,
                        handle_stop_add_wireless_cameras);
    nop_router_register(router, "startRemoveWirelessCameras", CAP_NETWORK,
                        handle_start_remove_wireless_cameras);
    nop_router_register(router, "startAddWirelessChime", CAP_NETWORK,
                        handle_start_add_wireless_chime);
    nop_router_register(router, "stopAddWirelessChime", CAP_NETWORK,
                        handle_stop_add_wireless_chime);
    nop_router_register(router, "startRemoveWirelessChime", CAP_NETWORK,
                        handle_start_remove_wireless_chime);
}
