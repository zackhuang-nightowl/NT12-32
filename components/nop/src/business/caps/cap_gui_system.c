/**
 * @file cap_gui_system.c
 * @brief CAP_SYSTEM GUI handlers for the LVGL/NVR interface: display mode,
 *        extended display overlays, system display resolution, per-channel
 *        audio config/switch and IR light, HDD overwrite, IR light, auto-reboot
 *        schedule, system log query, shutdown, feature restore/list and the
 *        surveillance abnormality notification matrix.
 *
 * State is held in module-static globals so scalar get/set round-trips exactly
 * today; firmware will override these via a HAL layer later. Per-channel values
 * are kept in fixed arrays indexed by channel. Object/array-valued set fields
 * (display overlays, surveillance abnormality matrix) are stored as a serialized
 * JSON snapshot on set and parsed back on get, freeing the previous snapshot and
 * falling back to a spec-shaped default when nothing has been set yet. Pure
 * queries (getSystemLog, getFeatureList) return spec-shaped objects with empty
 * or default contents; actions (shutDown, restoreFeatureSetting) validate any
 * required args and report success.
 */
#include "business/business.h"
#include "base/nop_json.h"

#include <string.h>
#include <stdlib.h>

#define GUI_MAX_CHANNELS 16

static int clamp_channel(int channel)
{
    return (channel < 1 || channel > GUI_MAX_CHANNELS) ? 0 : (channel - 1);
}

/* Store a serialized snapshot of an args sub-object into @p slot, freeing any
 * previous snapshot. A NULL value clears the slot. */
static void store_json_snapshot(char **slot, const nop_json_t *value)
{
    if (*slot) {
        free(*slot);
        *slot = NULL;
    }
    if (value)
        *slot = nop_json_print(value); /* may be NULL on failure; treated as unset */
}

/* ---- module-static state -------------------------------------------------- */
static struct {
    int display_mode;
    int display_page;
} g_device_display = { 4, 1 };

static char *g_display_ext_json;          /* {"channels":[...]} snapshot */

static char g_sys_resolution[32] = "1920*1080";
static int  g_sys_display_opacity = 255;

static int  g_speaker_volume[GUI_MAX_CHANNELS];
static int  g_audio_switch[GUI_MAX_CHANNELS];
static char g_ir_light_state[GUI_MAX_CHANNELS][16];

static int  g_speaker_volume_set[GUI_MAX_CHANNELS];
static int  g_audio_switch_set[GUI_MAX_CHANNELS];
static int  g_ir_light_set[GUI_MAX_CHANNELS];

static int  g_hdd_overwrite = 1;
static int  g_hdd_overwrite_set;

static struct {
    int enable;
    int which_day;
    int which_time;
} g_auto_reboot = { 1, 1, 2230 };

static char *g_surveillance_json;         /* whole abnormality matrix snapshot */

/* ===========================================================================
 * Device display mode (grid layout + page)
 * =========================================================================== */
static nop_status_t handle_get_device_display_mode(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "displayMode", g_device_display.display_mode);
    nop_json_add_int(response->content, "displayPage", g_device_display.display_page);
    return NOP_OK;
}

static nop_status_t handle_set_device_display_mode(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "displayMode") ||
        !nop_json_has(request->args, "displayPage"))
        return NOP_ERR_PARAM;
    g_device_display.display_mode =
        (int)nop_json_num(request->args, "displayMode", g_device_display.display_mode);
    g_device_display.display_page =
        (int)nop_json_num(request->args, "displayPage", g_device_display.display_page);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

/* ===========================================================================
 * Extended display (video overlay blocks)
 * =========================================================================== */
static nop_status_t handle_get_device_display_ext(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)request; (void)handler_context;
    if (g_display_ext_json)
        response->content = nop_json_parse(g_display_ext_json, strlen(g_display_ext_json));
    if (!response->content) {
        response->content = nop_json_obj();
        nop_json_add(response->content, "channels", nop_json_arr());
    }
    return NOP_OK;
}

static nop_status_t handle_set_device_display_ext(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    nop_json_t *snapshot;
    char       *channels_text;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    channels_text = nop_json_print(nop_json_get(request->args, "channels"));
    if (channels_text) {
        nop_json_t *channels = nop_json_parse(channels_text, strlen(channels_text));
        free(channels_text);
        if (channels)
            nop_json_add(snapshot, "channels", channels);
    }
    if (!nop_json_get(snapshot, "channels"))
        nop_json_add(snapshot, "channels", nop_json_arr());
    store_json_snapshot(&g_display_ext_json, snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * System display (output resolution + opacity)
 * =========================================================================== */
static nop_status_t handle_get_sys_display(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    static const char *resolutions[] = { "1280*720", "1280*1024", "1920*1080" };
    nop_json_t *resolution_list;
    size_t      i;
    (void)request; (void)handler_context;

    response->content = nop_json_obj();
    resolution_list = nop_json_arr();
    for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++)
        nop_json_arr_push_str(resolution_list, resolutions[i]);
    nop_json_add(response->content, "resolutionList", resolution_list);
    nop_json_add_str(response->content, "resolution", g_sys_resolution);
    nop_json_add_int(response->content, "displayOpacity", g_sys_display_opacity);
    return NOP_OK;
}

static nop_status_t handle_set_sys_display(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const char *resolution = nop_json_str(request->args, "resolution", NULL);
    (void)response; (void)handler_context;
    if (resolution) {
        strncpy(g_sys_resolution, resolution, sizeof(g_sys_resolution) - 1);
        g_sys_resolution[sizeof(g_sys_resolution) - 1] = '\0';
    }
    if (nop_json_has(request->args, "displayOpacity"))
        g_sys_display_opacity =
            (int)nop_json_num(request->args, "displayOpacity", g_sys_display_opacity);
    return NOP_OK;
}

/* ===========================================================================
 * Audio config (speaker volume) per channel
 * =========================================================================== */
static nop_status_t handle_get_audio_cfg(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    int index;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "speakerVolume",
                     g_speaker_volume_set[index] ? g_speaker_volume[index] : 50);
    return NOP_OK;
}

static nop_status_t handle_set_audio_cfg(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    int index;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    if (nop_json_has(request->args, "speakerVolume")) {
        g_speaker_volume[index] = (int)nop_json_num(request->args, "speakerVolume", 50);
        g_speaker_volume_set[index] = 1;
    }
    return NOP_OK;
}

/* ===========================================================================
 * Audio switch (live audio playback) per channel
 * =========================================================================== */
static nop_status_t handle_get_audio_switch(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    int index;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "enable",
                      g_audio_switch_set[index] ? (g_audio_switch[index] != 0) : false);
    return NOP_OK;
}

static nop_status_t handle_set_audio_switch(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    int index;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    g_audio_switch[index] = nop_json_bool(request->args, "enable", false) ? 1 : 0;
    g_audio_switch_set[index] = 1;
    return NOP_OK;
}

/* ===========================================================================
 * HDD config (overwrite)
 * =========================================================================== */
static nop_status_t handle_get_hdd_config(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "overWrite",
                      g_hdd_overwrite_set ? (g_hdd_overwrite != 0) : true);
    return NOP_OK;
}

static nop_status_t handle_set_hdd_config(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "overWrite"))
        return NOP_ERR_PARAM;
    g_hdd_overwrite = nop_json_bool(request->args, "overWrite", true) ? 1 : 0;
    g_hdd_overwrite_set = 1;
    return NOP_OK;
}

/* ===========================================================================
 * IR light per channel
 * =========================================================================== */
static nop_status_t handle_get_ir_light(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    int index;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "State",
                     g_ir_light_set[index] ? g_ir_light_state[index] : "Auto");
    return NOP_OK;
}

static nop_status_t handle_set_ir_light(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *state;
    int         index;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    state = nop_json_str(request->args, "State", NULL);
    if (!state)
        return NOP_ERR_PARAM;
    index = clamp_channel((int)nop_json_num(request->args, "channel", 1));
    strncpy(g_ir_light_state[index], state, sizeof(g_ir_light_state[index]) - 1);
    g_ir_light_state[index][sizeof(g_ir_light_state[index]) - 1] = '\0';
    g_ir_light_set[index] = 1;
    return NOP_OK;
}

/* ===========================================================================
 * Auto-reboot schedule
 * =========================================================================== */
static nop_status_t handle_get_auto_reboot_setting(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "Enable", g_auto_reboot.enable != 0);
    nop_json_add_int(response->content, "WhichDay", g_auto_reboot.which_day);
    nop_json_add_int(response->content, "WhichTime", g_auto_reboot.which_time);
    return NOP_OK;
}

static nop_status_t handle_set_auto_reboot_setting(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "Enable") ||
        !nop_json_has(request->args, "WhichDay") ||
        !nop_json_has(request->args, "WhichTime"))
        return NOP_ERR_PARAM;
    g_auto_reboot.enable = nop_json_bool(request->args, "Enable", true) ? 1 : 0;
    g_auto_reboot.which_day = (int)nop_json_num(request->args, "WhichDay", g_auto_reboot.which_day);
    g_auto_reboot.which_time = (int)nop_json_num(request->args, "WhichTime", g_auto_reboot.which_time);
    return NOP_OK;
}

/* ===========================================================================
 * System log query
 * =========================================================================== */
static nop_status_t handle_get_system_log(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)handler_context;
    /* No log source wired yet — report an empty, well-formed page rather than a
     * failure. Firmware can supply log items via a HAL later. */
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "TotalLogNum", 0);
    nop_json_add_int(response->content, "LogPageIndex",
                     (int)nop_json_num(request->args, "LogPageIndex", 0));
    nop_json_add_int(response->content, "CurPageLogNum", 0);
    nop_json_add(response->content, "LogList", nop_json_obj());
    return NOP_OK;
}

/* ===========================================================================
 * Shutdown
 * =========================================================================== */
static nop_status_t handle_shut_down(const nop_request_t *request,
                                     nop_response_t *response,
                                     void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    return NOP_OK;
}

/* ===========================================================================
 * Feature restore / list
 * =========================================================================== */
static const char *const g_feature_list[] = {
    "General", "Encode", "Record", "Alarm", "Network",
    "NetService", "OutputSettings", "Account", "RS232"
};

static nop_status_t handle_restore_feature_setting(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    nop_json_t *restore;
    (void)response; (void)handler_context;
    restore = nop_json_get(request->args, "Restore");
    if (!restore || !nop_json_is_arr(restore))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_get_feature_list(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    nop_json_t *restore;
    size_t      i;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    restore = nop_json_arr();
    for (i = 0; i < sizeof(g_feature_list) / sizeof(g_feature_list[0]); i++)
        nop_json_arr_push_str(restore, g_feature_list[i]);
    nop_json_add(response->content, "Restore", restore);
    return NOP_OK;
}

/* ===========================================================================
 * Surveillance abnormality notification matrix
 * =========================================================================== */
static void add_default_abnormality_entry(nop_json_t *content, const char *key,
                                          int with_less_than)
{
    nop_json_t *entry = nop_json_obj();
    nop_json_add_bool(entry, "MessageEnable", true);
    nop_json_add_bool(entry, "BeepEnable", true);
    nop_json_add_bool(entry, "EmailEnable", true);
    nop_json_add_bool(entry, "NotificationEnable", true);
    if (with_less_than)
        nop_json_add_int(entry, "LessThan", 5);
    nop_json_add(content, key, entry);
}

static void add_default_surveillance(nop_json_t *content)
{
    add_default_abnormality_entry(content, "nodisk", 0);
    add_default_abnormality_entry(content, "diskerr", 0);
    add_default_abnormality_entry(content, "disknospace", 1);
    add_default_abnormality_entry(content, "netdiscon", 0);
    add_default_abnormality_entry(content, "ipconflict", 0);
}

static nop_status_t handle_get_surveillance_abnormality(const nop_request_t *request,
                                                        nop_response_t *response,
                                                        void *handler_context)
{
    (void)request; (void)handler_context;
    if (g_surveillance_json)
        response->content = nop_json_parse(g_surveillance_json, strlen(g_surveillance_json));
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_surveillance(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_surveillance_abnormality(const nop_request_t *request,
                                                        nop_response_t *response,
                                                        void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "nodisk") ||
        !nop_json_has(request->args, "diskerr") ||
        !nop_json_has(request->args, "disknospace") ||
        !nop_json_has(request->args, "netdiscon") ||
        !nop_json_has(request->args, "ipconflict"))
        return NOP_ERR_PARAM;
    store_json_snapshot(&g_surveillance_json, request->args);
    return NOP_OK;
}

/* ===========================================================================
 * Registration
 * =========================================================================== */
void cap_gui_system_register(nop_router_t *router)
{
    nop_router_register(router, "GUI_getDeviceDisplayMode", CAP_SYSTEM, handle_get_device_display_mode);
    nop_router_register(router, "GUI_setDeviceDisplayMode", CAP_SYSTEM, handle_set_device_display_mode);
    nop_router_register(router, "GUI_getDeviceDisplayExt", CAP_SYSTEM, handle_get_device_display_ext);
    nop_router_register(router, "GUI_setDeviceDisplayExt", CAP_SYSTEM, handle_set_device_display_ext);
    nop_router_register(router, "GUI_getSysDisplay", CAP_SYSTEM, handle_get_sys_display);
    nop_router_register(router, "GUI_setSysDisplay", CAP_SYSTEM, handle_set_sys_display);
    nop_router_register(router, "GUI_getAudioCfg", CAP_SYSTEM, handle_get_audio_cfg);
    nop_router_register(router, "GUI_setAudioCfg", CAP_SYSTEM, handle_set_audio_cfg);
    nop_router_register(router, "GUI_getAudioSwitch", CAP_SYSTEM, handle_get_audio_switch);
    nop_router_register(router, "GUI_setAudioSwitch", CAP_SYSTEM, handle_set_audio_switch);
    nop_router_register(router, "GUI_getHddConfig", CAP_SYSTEM, handle_get_hdd_config);
    nop_router_register(router, "GUI_setHddConfig", CAP_SYSTEM, handle_set_hdd_config);
    nop_router_register(router, "GUI_getIrLight", CAP_SYSTEM, handle_get_ir_light);
    nop_router_register(router, "GUI_setIrLight", CAP_SYSTEM, handle_set_ir_light);
    nop_router_register(router, "GUI_getAutoRebootSetting", CAP_SYSTEM, handle_get_auto_reboot_setting);
    nop_router_register(router, "GUI_setAutoRebootSetting", CAP_SYSTEM, handle_set_auto_reboot_setting);
    nop_router_register(router, "GUI_getSystemLog", CAP_SYSTEM, handle_get_system_log);
    nop_router_register(router, "GUI_shutDown", CAP_SYSTEM, handle_shut_down);
    nop_router_register(router, "GUI_restoreFeatureSetting", CAP_SYSTEM, handle_restore_feature_setting);
    nop_router_register(router, "GUI_getFeatureList", CAP_SYSTEM, handle_get_feature_list);
    nop_router_register(router, "GUI_getSurveillanceAbnormality", CAP_SYSTEM, handle_get_surveillance_abnormality);
    nop_router_register(router, "GUI_setSurveillanceAbnormality", CAP_SYSTEM, handle_set_surveillance_abnormality);
}
