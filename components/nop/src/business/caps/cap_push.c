/**
 * @file cap_push.c
 * @brief CAP_PUSH handlers: cooldown time, push-photo switch, snooze, per-channel
 *        push-notification do-not-disturb / switch / triggers, batched channel
 *        switches, panic switch, and the notify_* fire-and-forget commands.
 *        Backed by in-memory module-static state (no HAL); gated by CAP_PUSH.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "business/cap_helpers.h"

#include <string.h>
#include <stdlib.h>

/* Per-channel state is indexed by (channel - 1); channels start from 1. */
#define PUSH_MAX_CHANNELS 16

/* Clamp a 1-based channel number to a valid 0-based array index. */
static int push_channel_index(int channel)
{
    int index = channel - 1;
    if (index < 0)
        index = 0;
    if (index >= PUSH_MAX_CHANNELS)
        index = PUSH_MAX_CHANNELS - 1;
    return index;
}

/* ---- module-static state -------------------------------------------------- */

static int  g_cooldown_seconds[PUSH_MAX_CHANNELS];
static bool g_push_photo_enabled[PUSH_MAX_CHANNELS];
static int  g_snooze_end_time[PUSH_MAX_CHANNELS];

static bool g_do_not_disturb_enabled[PUSH_MAX_CHANNELS];
static char g_do_not_disturb_start_time[PUSH_MAX_CHANNELS][8];
static char g_do_not_disturb_end_time[PUSH_MAX_CHANNELS][8];

static bool g_push_notification_enabled[PUSH_MAX_CHANNELS];

/* Triggers are stored as a serialized JSON array snapshot per channel. */
static char *g_push_notification_triggers[PUSH_MAX_CHANNELS];

static bool g_panic_enabled;

static bool g_state_initialized;

/* Apply the spec default values once, on first use of any handler. */
static void push_state_initialize(void)
{
    int channel;
    if (g_state_initialized)
        return;
    for (channel = 0; channel < PUSH_MAX_CHANNELS; channel++) {
        g_cooldown_seconds[channel]          = 20;
        g_push_photo_enabled[channel]        = true;
        g_snooze_end_time[channel]           = 0;
        g_do_not_disturb_enabled[channel]    = true;
        strcpy(g_do_not_disturb_start_time[channel], "2100");
        strcpy(g_do_not_disturb_end_time[channel], "0700");
        g_push_notification_enabled[channel] = true;
        g_push_notification_triggers[channel] = NULL;
    }
    g_panic_enabled    = false;
    g_state_initialized = true;
}

/* ---- getCooldownTime / setCooldownTime ------------------------------------ */

static nop_status_t handle_get_cooldown_time(const nop_request_t *request,
                                             nop_response_t *response, void *handler_context)
{
    int channel;
    (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", g_cooldown_seconds[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_cooldown_time(const nop_request_t *request,
                                             nop_response_t *response, void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));
    g_cooldown_seconds[channel] = (int)nop_json_num(request->args, "value", 20);
    return NOP_OK;
}

/* ---- getPushPhotoSwitch / setPushPhotoSwitch ------------------------------ */

static nop_status_t handle_get_push_photo_switch(const nop_request_t *request,
                                                 nop_response_t *response, void *handler_context)
{
    nop_json_t *channels;
    int         channel;
    (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;

    /* The argument carries a list of channel numbers; we report all known
     * channels with their stored switch status. */
    response->content = nop_json_obj();
    channels = nop_json_arr();
    for (channel = 0; channel < PUSH_MAX_CHANNELS; channel++) {
        nop_json_t *entry = nop_json_obj();
        nop_json_add_bool(entry, "enable", g_push_photo_enabled[channel]);
        nop_json_add_int(entry, "channel", channel + 1);
        nop_json_arr_push(channels, entry);
    }
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_set_push_photo_switch(const nop_request_t *request,
                                                 nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    push_state_initialize();

    /* "channels" is an array of {enable, channel} dicts; element-level access
     * is outside the allowed JSON facade, so we accept and acknowledge. */
    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ---- getSnooze / setSnooze ------------------------------------------------ */

static nop_status_t handle_get_snooze(const nop_request_t *request,
                                      nop_response_t *response, void *handler_context)
{
    nop_json_t *channels;
    int         channel;
    (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    channels = nop_json_arr();
    for (channel = 0; channel < PUSH_MAX_CHANNELS; channel++) {
        nop_json_t *entry = nop_json_obj();
        nop_json_add_int(entry, "endTime", g_snooze_end_time[channel]);
        nop_json_add_int(entry, "channel", channel + 1);
        nop_json_arr_push(channels, entry);
    }
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_set_snooze(const nop_request_t *request,
                                      nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    push_state_initialize();

    /* "channels" is an array of {endTime, channel} dicts; accept and acknowledge. */
    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ---- X_NightOwl_get/setChannelPushNotificationDoNotDisturb ---------------- */

static nop_status_t handle_get_do_not_disturb(const nop_request_t *request,
                                              nop_response_t *response, void *handler_context)
{
    int channel;
    (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));

    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "isEnabled", g_do_not_disturb_enabled[channel]);
    nop_json_add_str(response->content, "startTime", g_do_not_disturb_start_time[channel]);
    nop_json_add_str(response->content, "endTime", g_do_not_disturb_end_time[channel]);
    nop_json_add_str(response->content, "timeUnit", "hour");
    return NOP_OK;
}

static nop_status_t handle_set_do_not_disturb(const nop_request_t *request,
                                              nop_response_t *response, void *handler_context)
{
    int         channel;
    const char *start_time, *end_time;
    (void)response; (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));
    g_do_not_disturb_enabled[channel] = nop_json_bool(request->args, "enable", true);

    start_time = nop_json_str(request->args, "startTime", NULL);
    end_time   = nop_json_str(request->args, "endTime", NULL);
    if (start_time) {
        strncpy(g_do_not_disturb_start_time[channel], start_time,
                sizeof(g_do_not_disturb_start_time[channel]) - 1);
        g_do_not_disturb_start_time[channel][sizeof(g_do_not_disturb_start_time[channel]) - 1] = '\0';
    }
    if (end_time) {
        strncpy(g_do_not_disturb_end_time[channel], end_time,
                sizeof(g_do_not_disturb_end_time[channel]) - 1);
        g_do_not_disturb_end_time[channel][sizeof(g_do_not_disturb_end_time[channel]) - 1] = '\0';
    }
    return NOP_OK;
}

/* ---- X_NightOwl_get/setChannelPushNotificationSwitch ---------------------- */

static nop_status_t handle_get_channel_push_switch(const nop_request_t *request,
                                                   nop_response_t *response, void *handler_context)
{
    int channel;
    (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));

    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_push_notification_enabled[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_channel_push_switch(const nop_request_t *request,
                                                   nop_response_t *response, void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));
    g_push_notification_enabled[channel] = nop_json_bool(request->args, "value", true);
    return NOP_OK;
}

/* ---- X_NightOwl_get/setChannelPushNotificationTriggers -------------------- */

static nop_status_t handle_get_channel_triggers(const nop_request_t *request,
                                                nop_response_t *response, void *handler_context)
{
    int         channel;
    nop_json_t *triggers;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = push_channel_index((int)nop_json_num(request->args, "channel", 1));

    response->content = nop_json_obj();
    if (g_push_notification_triggers[channel]) {
        /* Re-emit the snapshot stored on the last set. */
        triggers = nop_json_parse(g_push_notification_triggers[channel],
                                  strlen(g_push_notification_triggers[channel]));
        if (!triggers)
            triggers = nop_json_arr();
    } else {
        /* Default trigger set = the device's supported detection types. */
        triggers = nop_json_arr();
        cap_emit_detection_type_names(handler_context, triggers);
    }
    nop_json_add(response->content, "triggers", triggers);
    return NOP_OK;
}

static nop_status_t handle_set_channel_triggers(const nop_request_t *request,
                                                nop_response_t *response, void *handler_context)
{
    int         channel;
    nop_json_t *triggers;
    char       *snapshot;
    (void)response; (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "triggers"))
        return NOP_ERR_PARAM;
    channel  = push_channel_index((int)nop_json_num(request->args, "channel", 1));
    triggers = nop_json_get(request->args, "triggers");

    /* Store a serialized snapshot of the triggers array for later replay. */
    snapshot = nop_json_print(triggers);
    if (g_push_notification_triggers[channel])
        free(g_push_notification_triggers[channel]);
    g_push_notification_triggers[channel] = snapshot;
    return NOP_OK;
}

/* ---- X_NightOwl_get/setChannelsPushNotificationSwitch --------------------- */

static nop_status_t handle_get_channels_push_switch(const nop_request_t *request,
                                                    nop_response_t *response, void *handler_context)
{
    nop_json_t *channels;
    int         channel;
    (void)request; (void)handler_context;
    push_state_initialize();

    response->content = nop_json_obj();
    channels = nop_json_arr();
    for (channel = 0; channel < PUSH_MAX_CHANNELS; channel++) {
        nop_json_t *entry = nop_json_obj();
        nop_json_add_int(entry, "channel", channel + 1);
        nop_json_add_bool(entry, "value", g_push_notification_enabled[channel]);
        nop_json_arr_push(channels, entry);
    }
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_set_channels_push_switch(const nop_request_t *request,
                                                    nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    push_state_initialize();

    /* The args themselves are the array of {channel, value} dicts; element-level
     * access is outside the allowed JSON facade, so we accept and acknowledge. */
    if (!request->args)
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ---- X_NightOwl_get/setPanicSwitch ---------------------------------------- */

static nop_status_t handle_get_panic_switch(const nop_request_t *request,
                                            nop_response_t *response, void *handler_context)
{
    (void)request; (void)handler_context;
    push_state_initialize();

    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_panic_enabled);
    return NOP_OK;
}

static nop_status_t handle_set_panic_switch(const nop_request_t *request,
                                            nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    push_state_initialize();

    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_panic_enabled = nop_json_bool(request->args, "value", false);
    return NOP_OK;
}

/* ---- notify_* (fire-and-forget; accept args, acknowledge with empty content) */

static nop_status_t handle_notify_app_setup_status(const nop_request_t *request,
                                                   nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "status"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_notify_login_success(const nop_request_t *request,
                                                nop_response_t *response, void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    return NOP_OK;
}

static nop_status_t handle_notify_session_count(const nop_request_t *request,
                                                nop_response_t *response, void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ---- registration --------------------------------------------------------- */

void cap_push_register(nop_router_t *router)
{
    nop_router_register(router, "getCooldownTime", CAP_PUSH,
                        handle_get_cooldown_time);
    nop_router_register(router, "setCooldownTime", CAP_PUSH,
                        handle_set_cooldown_time);
    nop_router_register(router, "getPushPhotoSwitch", CAP_PUSH,
                        handle_get_push_photo_switch);
    nop_router_register(router, "setPushPhotoSwitch", CAP_PUSH,
                        handle_set_push_photo_switch);
    nop_router_register(router, "getSnooze", CAP_PUSH,
                        handle_get_snooze);
    nop_router_register(router, "setSnooze", CAP_PUSH,
                        handle_set_snooze);
    nop_router_register(router, "X_NightOwl_getChannelPushNotificationDoNotDisturb", CAP_PUSH,
                        handle_get_do_not_disturb);
    nop_router_register(router, "X_NightOwl_setChannelPushNotificationDoNotDisturb", CAP_PUSH,
                        handle_set_do_not_disturb);
    nop_router_register(router, "X_NightOwl_getChannelPushNotificationSwitch", CAP_PUSH,
                        handle_get_channel_push_switch);
    nop_router_register(router, "X_NightOwl_setChannelPushNotificationSwitch", CAP_PUSH,
                        handle_set_channel_push_switch);
    nop_router_register(router, "X_NightOwl_getChannelPushNotificationTriggers", CAP_PUSH,
                        handle_get_channel_triggers);
    nop_router_register(router, "X_NightOwl_setChannelPushNotificationTriggers", CAP_PUSH,
                        handle_set_channel_triggers);
    nop_router_register(router, "X_NightOwl_getChannelsPushNotificationSwitch", CAP_PUSH,
                        handle_get_channels_push_switch);
    nop_router_register(router, "X_NightOwl_setChannelsPushNotificationSwitch", CAP_PUSH,
                        handle_set_channels_push_switch);
    nop_router_register(router, "X_NightOwl_getPanicSwitch", CAP_PUSH,
                        handle_get_panic_switch);
    nop_router_register(router, "X_NightOwl_setPanicSwitch", CAP_PUSH,
                        handle_set_panic_switch);
    nop_router_register(router, "notify_APPSetupStatus", CAP_PUSH,
                        handle_notify_app_setup_status);
    nop_router_register(router, "notifyLoginSuccess", CAP_PUSH,
                        handle_notify_login_success);
    nop_router_register(router, "notifySessionCount", CAP_PUSH,
                        handle_notify_session_count);
}
