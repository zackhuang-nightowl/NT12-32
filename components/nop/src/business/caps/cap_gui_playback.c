/**
 * @file cap_gui_playback.c
 * @brief GUI playback / backup / longpolling handlers. Gated by CAP_MISC.
 *
 *        Backed by module-static state so scalar get/set pairs round-trip
 *        (set -> get): playback audio enable mask, playback display mode and
 *        channel layout, and per-channel record pack duration. List/query
 *        commands (getFileList, GetChannelBackupStatus, getChannelEventRecordingSchedule)
 *        return a spec-shaped object with sensible defaults / empty arrays.
 *        Action commands (playbackControl, ChannelBackupFiles, StopChannelBackup,
 *        setChannelEventRecordingSchedule, longPolling, longPolling_Test) validate
 *        their required args and return a spec-shaped result (longPolling returns
 *        an empty events object). Object/array-valued set fields are kept as a
 *        serialized JSON snapshot and parsed back on get; otherwise a spec default
 *        is returned. Modeled on nop_api/.../<command>.txt "//Format".
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop/nop_longpoll.h"

#include <string.h>
#include <stdlib.h>

#define PLAYBACK_MAX_CHANNELS 16

/* ---- module-static state -------------------------------------------------- */

/* Playback audio: one enable flag per grid cell (round-trip). The active count
 * follows the most recent set; defaults to a 4-cell all-muted layout. */
static int g_playback_audio_enable[PLAYBACK_MAX_CHANNELS] = { 0, 0, 0, 0 };
static int g_playback_audio_count = 4;

/* Playback display mode + channel layout (round-trip). */
static int g_playback_display_mode = 4;
static int g_playback_channels[PLAYBACK_MAX_CHANNELS] = { 1, 2, 3, 4 };
static int g_playback_channel_count = 4;

/* Most recent playback control state (round-trip via status action). */
static char g_playback_status[16]    = "stopped";
static char g_playback_speed[16]     = "1X";
static char g_playback_direction[16] = "forward";

/* Per-channel record pack duration in minutes (round-trip), default 10. */
static int g_pack_duration[PLAYBACK_MAX_CHANNELS];
static bool g_pack_duration_set[PLAYBACK_MAX_CHANNELS];

/* Object/array-valued snapshots kept across get/set. */
static char *g_event_schedule_json = NULL;

static int clamp_channel(int channel)
{
    if (channel < 0)
        channel = 0;
    if (channel >= PLAYBACK_MAX_CHANNELS)
        channel = PLAYBACK_MAX_CHANNELS - 1;
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
 * Playback control + status (round-trip via action=status)
 * =========================================================================== */
static nop_status_t handle_playback_control(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const char *action    = nop_json_str(request->args, "action", NULL);
    const char *speed     = nop_json_str(request->args, "speed", NULL);
    const char *direction = nop_json_str(request->args, "direction", NULL);
    (void)handler_context;
    if (!action)
        return NOP_ERR_PARAM;

    if (speed)
        copy_str(g_playback_speed, sizeof(g_playback_speed), speed);
    if (direction)
        copy_str(g_playback_direction, sizeof(g_playback_direction), direction);

    if (strcmp(action, "play") == 0 || strcmp(action, "start") == 0)
        copy_str(g_playback_status, sizeof(g_playback_status), "playing");
    else if (strcmp(action, "pause") == 0)
        copy_str(g_playback_status, sizeof(g_playback_status), "paused");
    else if (strcmp(action, "stop") == 0)
        copy_str(g_playback_status, sizeof(g_playback_status), "stopped");
    /* "status", "zoomIn", "zoomOut": report current state unchanged. */

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "status", g_playback_status);
    nop_json_add_str(response->content, "speed", g_playback_speed);
    nop_json_add_str(response->content, "direction", g_playback_direction);
    nop_json_add_int(response->content, "timestamp",
                     nop_json_num(request->args, "startTime", 0));
    return NOP_OK;
}

/* ===========================================================================
 * Playback audio enable mask (round-trip)
 * =========================================================================== */
static nop_status_t handle_get_playback_audio(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    nop_json_t *enable;
    int         i;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    enable = nop_json_arr();
    for (i = 0; i < g_playback_audio_count; ++i)
        nop_json_arr_push_int(enable, g_playback_audio_enable[i]);
    nop_json_add(response->content, "enable", enable);
    return NOP_OK;
}

static nop_status_t handle_set_playback_audio(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    nop_json_t *enable;
    int         count, i;
    (void)response; (void)handler_context;
    enable = nop_json_get(request->args, "enable");
    if (!enable || !nop_json_is_arr(enable))
        return NOP_ERR_PARAM;
    count = nop_json_arr_size(enable);
    if (count > PLAYBACK_MAX_CHANNELS)
        count = PLAYBACK_MAX_CHANNELS;
    for (i = 0; i < count; ++i) {
        nop_json_t *item = nop_json_arr_at(enable, i);
        g_playback_audio_enable[i] = (item && atoi(nop_json_as_str(item, "0")) != 0) ? 1 : 0;
    }
    g_playback_audio_count = count;
    return NOP_OK;
}

/* ===========================================================================
 * Playback display mode + channel layout (round-trip)
 * =========================================================================== */
static nop_status_t handle_get_playback_mode(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    nop_json_t *channels;
    int         i;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "displayMode", g_playback_display_mode);
    channels = nop_json_arr();
    for (i = 0; i < g_playback_channel_count; ++i)
        nop_json_arr_push_int(channels, g_playback_channels[i]);
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_set_playback_mode(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    nop_json_t *channels;
    int         count, i;
    (void)handler_context;
    if (!nop_json_has(request->args, "displayMode"))
        return NOP_ERR_PARAM;
    channels = nop_json_get(request->args, "channels");
    if (!channels || !nop_json_is_arr(channels))
        return NOP_ERR_PARAM;

    g_playback_display_mode = (int)nop_json_num(request->args, "displayMode", 4);
    count = nop_json_arr_size(channels);
    if (count > PLAYBACK_MAX_CHANNELS)
        count = PLAYBACK_MAX_CHANNELS;
    for (i = 0; i < count; ++i) {
        nop_json_t *item = nop_json_arr_at(channels, i);
        g_playback_channels[i] = item ? atoi(nop_json_as_str(item, "0")) : 0;
    }
    g_playback_channel_count = count;

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

/* ===========================================================================
 * File list query
 * =========================================================================== */
static nop_status_t handle_get_file_list(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add(response->content, "FileList", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * Channel backup: start / status / stop
 * =========================================================================== */
static nop_status_t handle_channel_backup_files(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    nop_json_t *list;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    list = nop_json_get(request->args, "list");
    if (!list || !nop_json_is_arr(list))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_get_channel_backup_status(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "percent", 0);
    return NOP_OK;
}

static nop_status_t handle_stop_channel_backup(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    return NOP_OK;
}

/* ===========================================================================
 * Per-channel event recording schedule (snapshot round-trip)
 * =========================================================================== */
static nop_status_t handle_get_channel_event_recording_schedule(const nop_request_t *request,
                                                               nop_response_t *response,
                                                               void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_str(request->args, "sensor", NULL))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    if (g_event_schedule_json) {
        nop_json_t *stored = nop_json_parse(g_event_schedule_json,
                                            strlen(g_event_schedule_json));
        if (stored && nop_json_is_arr(stored)) {
            nop_json_add(response->content, "rules", stored);
            return NOP_OK;
        }
        nop_json_free(stored);
    }
    nop_json_add(response->content, "rules", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_set_channel_event_recording_schedule(const nop_request_t *request,
                                                               nop_response_t *response,
                                                               void *handler_context)
{
    nop_json_t *rules;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_str(request->args, "sensor", NULL))
        return NOP_ERR_PARAM;
    rules = nop_json_get(request->args, "rules");
    if (!rules || !nop_json_is_arr(rules))
        return NOP_ERR_PARAM;
    store_json_snapshot(&g_event_schedule_json, rules);
    return NOP_OK;
}

/* ===========================================================================
 * Per-channel record pack duration (round-trip)
 * =========================================================================== */
static nop_status_t handle_get_channel_pack_duration(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value",
                     g_pack_duration_set[channel] ? g_pack_duration[channel] : 10);
    return NOP_OK;
}

static nop_status_t handle_set_channel_pack_duration(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_pack_duration[channel]     = (int)nop_json_num(request->args, "value", 10);
    g_pack_duration_set[channel] = true;
    return NOP_OK;
}

/* ===========================================================================
 * Long polling: NVR -> GUI status push channel
 * =========================================================================== */
static nop_status_t handle_long_polling(const nop_request_t *request,
                                       nop_response_t *response,
                                       void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    uint32_t pending = 0;
    (void)request;
    /* Drain the pending-event mask the event->longpoll bridge raised. */
    if (context && context->longpoll)
        pending = nop_longpoll_drain((nop_longpoll_t *)context->longpoll);
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "pending", pending ? true : false);
    return NOP_OK;
}

static nop_status_t handle_long_polling_test(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_str(request->args, "sub", NULL))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ===========================================================================
 * Registration: every command under CAP_MISC (15 total)
 * =========================================================================== */
void cap_gui_playback_register(nop_router_t *router)
{
    nop_router_register(router, "GUI_playbackControl", CAP_MISC, handle_playback_control);
    nop_router_register(router, "GUI_getPlaybackAudio", CAP_MISC, handle_get_playback_audio);
    nop_router_register(router, "GUI_setPlaybackAudio", CAP_MISC, handle_set_playback_audio);
    nop_router_register(router, "GUI_getPlaybackMode", CAP_MISC, handle_get_playback_mode);
    nop_router_register(router, "GUI_setPlaybackMode", CAP_MISC, handle_set_playback_mode);
    nop_router_register(router, "GUI_getFileList", CAP_MISC, handle_get_file_list);
    nop_router_register(router, "GUI_ChannelBackupFiles", CAP_MISC, handle_channel_backup_files);
    nop_router_register(router, "GUI_GetChannelBackupStatus", CAP_MISC, handle_get_channel_backup_status);
    nop_router_register(router, "GUI_StopChannelBackup", CAP_MISC, handle_stop_channel_backup);
    nop_router_register(router, "GUI_getChannelEventRecordingSchedule", CAP_MISC, handle_get_channel_event_recording_schedule);
    nop_router_register(router, "GUI_setChannelEventRecordingSchedule", CAP_MISC, handle_set_channel_event_recording_schedule);
    nop_router_register(router, "GUI_getChannelPackDuration", CAP_MISC, handle_get_channel_pack_duration);
    nop_router_register(router, "GUI_setChannelPackDuration", CAP_MISC, handle_set_channel_pack_duration);
    nop_router_register(router, "GUI_longPolling", CAP_MISC, handle_long_polling);
    nop_router_register(router, "GUI_longPolling_Test", CAP_MISC, handle_long_polling_test);
}
