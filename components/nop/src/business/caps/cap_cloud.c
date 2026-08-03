/**
 * @file cap_cloud.c
 * @brief CAP_MISC cloud-recording, recording-extras and playback handlers.
 *
 * Handlers for the cloud recording, recording backup/test and playback command
 * family. State is held in module-static arrays/globals (per-channel where a
 * command carries a channel argument) so get/set round-trips correctly today;
 * firmware will override these via the HAL layer later. Scalar values round-trip
 * exactly. Array/object-valued fields (configs, triggers, schedule, intervals)
 * are stored as a serialized JSON snapshot on set and parsed back on get,
 * falling back to a spec-shaped default when nothing has been set yet. Test and
 * backup progress, playback capabilities and the query* commands return
 * spec-shaped objects (idle/empty/progress 0).
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "business/cap_helpers.h"
#include <string.h>
#include <stdlib.h>

#define CLOUD_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= CLOUD_MAX_CHANNELS) ? 0 : channel;
}

/* ---- module-static state -------------------------------------------------- */
static char *g_cloud_record_configs_json;                    /* whole {mode,channels} snapshot */
static int   g_cloud_record_log_interval = 86400;
static int   g_cloud_record_log_start_time = 1598489497;
static int   g_cloud_record_switch;

static int   g_channel_recording_time[CLOUD_MAX_CHANNELS];
static int   g_continuous_schedule_switch[CLOUD_MAX_CHANNELS];
static char *g_recording_triggers_json[CLOUD_MAX_CHANNELS];  /* {"triggers":[...]} snapshot */
static char *g_continuous_schedule_json[CLOUD_MAX_CHANNELS]; /* {"rules":[...]} snapshot */

/* ---- small helpers -------------------------------------------------------- */
/* Store a serialized snapshot of an args sub-object into @p slot. */
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
 * Cloud recording: detailed configs (mode + channels array)
 * =========================================================================== */
static void add_default_cloud_record_configs(void *handler_context, nop_json_t *content)
{
    nop_json_t *channels = nop_json_arr();
    nop_json_t *channel  = nop_json_obj();
    nop_json_t *triggers = nop_json_arr();
    cap_emit_detection_type_names(handler_context, triggers);
    nop_json_add_int(channel, "channel", 1);
    nop_json_add_str(channel, "streamType", "main");
    nop_json_add(channel, "triggers", triggers);
    nop_json_arr_push(channels, channel);
    nop_json_add_str(content, "mode", "sync");
    nop_json_add(content, "channels", channels);
}

static nop_status_t handle_get_cloud_record_configs(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    (void)request;
    if (g_cloud_record_configs_json) {
        response->content = nop_json_parse(g_cloud_record_configs_json,
                                           strlen(g_cloud_record_configs_json));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_cloud_record_configs(handler_context, response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_cloud_record_configs(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    if (nop_json_has(request->args, "mode"))
        nop_json_add_str(snapshot, "mode", nop_json_str(request->args, "mode", "sync"));
    else
        nop_json_add_str(snapshot, "mode", "sync");
    {
        char *channels_text = nop_json_print(nop_json_get(request->args, "channels"));
        if (channels_text) {
            nop_json_t *channels = nop_json_parse(channels_text, strlen(channels_text));
            free(channels_text);
            if (channels)
                nop_json_add(snapshot, "channels", channels);
        }
    }
    if (!nop_json_get(snapshot, "channels"))
        nop_json_add(snapshot, "channels", nop_json_arr());
    store_json_snapshot(&g_cloud_record_configs_json, snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Cloud recording: log upload config (interval + startTime)
 * =========================================================================== */
static nop_status_t handle_get_cloud_record_log_config(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "interval", (double)g_cloud_record_log_interval);
    nop_json_add_int(response->content, "startTime", (double)g_cloud_record_log_start_time);
    return NOP_OK;
}

static nop_status_t handle_set_cloud_record_log_config(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "interval"))
        return NOP_ERR_PARAM;
    g_cloud_record_log_interval = (int)nop_json_num(request->args, "interval", 86400);
    return NOP_OK;
}

/* ===========================================================================
 * Cloud recording: test progress / start / stop
 * =========================================================================== */
static nop_status_t handle_get_cloud_record_test_progress(const nop_request_t *request,
                                                          nop_response_t *response,
                                                          void *handler_context)
{
    nop_json_t *stream_type;
    nop_json_t *status;
    nop_json_t *timestamp;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "channel", 1);
    nop_json_add_int(response->content, "startTime", 1598489497);
    nop_json_add_int(response->content, "endTime", -1);
    stream_type = nop_json_arr();
    nop_json_add(response->content, "streamType", stream_type);
    status = nop_json_arr();
    nop_json_arr_push_str(status, "idle");
    nop_json_add(response->content, "status", status);
    timestamp = nop_json_arr();
    nop_json_arr_push_int(timestamp, -1);
    nop_json_add(response->content, "timestamp", timestamp);
    return NOP_OK;
}

static nop_status_t handle_start_cloud_record_test(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    return NOP_OK;
}

static nop_status_t handle_stop_cloud_record_test(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    return NOP_OK;
}

/* ===========================================================================
 * Cloud recording: master switch
 * =========================================================================== */
static nop_status_t handle_get_cloud_record_switch(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_cloud_record_switch ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_cloud_record_switch(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_cloud_record_switch = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Channel recording time (post-record seconds)
 * =========================================================================== */
static nop_status_t handle_get_channel_recording_time(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", (double)g_channel_recording_time[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_channel_recording_time(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    g_channel_recording_time[channel] = (int)nop_json_num(request->args, "value", 10);
    return NOP_OK;
}

/* ===========================================================================
 * Recording backup: progress / start
 * =========================================================================== */
static nop_status_t handle_get_recording_backup_progress(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    nop_json_t *date;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "channel", 1);
    nop_json_add_int(response->content, "percentage", 0);
    date = nop_json_arr();
    nop_json_add(response->content, "date", date);
    nop_json_add_int(response->content, "startTime", 1598489497);
    nop_json_add_int(response->content, "endTime", -1);
    return NOP_OK;
}

static nop_status_t handle_start_recording_backup(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "date"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    return NOP_OK;
}

/* ===========================================================================
 * Playback: capabilities / start
 * =========================================================================== */
static nop_status_t handle_get_playback_capabilities(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    nop_json_t *protocol;
    nop_json_t *stream_type;
    nop_json_t *channels;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    protocol = nop_json_arr();
    nop_json_arr_push_str(protocol, "rtsp-iotc-tunnel");
    nop_json_add(response->content, "protocol", protocol);
    stream_type = nop_json_arr();
    nop_json_arr_push_str(stream_type, "audioAndVideo");
    nop_json_arr_push_str(stream_type, "audioAndSubVideo");
    nop_json_add(response->content, "streamType", stream_type);
    channels = nop_json_arr();
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_start_playback(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "streamType") ||
        !nop_json_has(request->args, "preferProtocol"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "url", "");
    return NOP_OK;
}

/* ===========================================================================
 * Continuous schedule recording switch (per channel)
 * =========================================================================== */
static nop_status_t handle_get_continuous_schedule_switch(const nop_request_t *request,
                                                          nop_response_t *response,
                                                          void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_continuous_schedule_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_continuous_schedule_switch(const nop_request_t *request,
                                                          nop_response_t *response,
                                                          void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    g_continuous_schedule_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Channel recording triggers (per channel, array snapshot)
 * =========================================================================== */
static nop_status_t handle_get_recording_triggers(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    if (g_recording_triggers_json[channel]) {
        response->content = nop_json_parse(g_recording_triggers_json[channel],
                                           strlen(g_recording_triggers_json[channel]));
    }
    if (!response->content) {
        nop_json_t *triggers = nop_json_arr();
        cap_emit_detection_type_names(handler_context, triggers);
        response->content = nop_json_obj();
        nop_json_add(response->content, "triggers", triggers);
    }
    return NOP_OK;
}

static nop_status_t handle_set_recording_triggers(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel;
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "triggers"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    snapshot = nop_json_obj();
    {
        char *triggers_text = nop_json_print(nop_json_get(request->args, "triggers"));
        if (triggers_text) {
            nop_json_t *triggers = nop_json_parse(triggers_text, strlen(triggers_text));
            free(triggers_text);
            if (triggers)
                nop_json_add(snapshot, "triggers", triggers);
        }
    }
    if (!nop_json_get(snapshot, "triggers"))
        nop_json_add(snapshot, "triggers", nop_json_arr());
    store_json_snapshot(&g_recording_triggers_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Continuous recording schedule (per channel, rules array snapshot)
 * =========================================================================== */
static nop_status_t handle_set_continuous_schedule(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel;
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "rules"))
        return NOP_ERR_PARAM;
    channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    snapshot = nop_json_obj();
    {
        char *rules_text = nop_json_print(nop_json_get(request->args, "rules"));
        if (rules_text) {
            nop_json_t *rules = nop_json_parse(rules_text, strlen(rules_text));
            free(rules_text);
            if (rules)
                nop_json_add(snapshot, "rules", rules);
        }
    }
    if (!nop_json_get(snapshot, "rules"))
        nop_json_add(snapshot, "rules", nop_json_arr());
    store_json_snapshot(&g_continuous_schedule_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Calendar / interval queries (spec-shaped empty results)
 * =========================================================================== */
static nop_status_t handle_query_continuous_calendar(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "resolution") || !nop_json_has(request->args, "year") ||
        !nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "resolution",
                     nop_json_str(request->args, "resolution", "day"));
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_query_recording_interval(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    nop_json_t *list;
    int hour;
    (void)handler_context;
    if (!nop_json_has(request->args, "year") || !nop_json_has(request->args, "month") ||
        !nop_json_has(request->args, "day") || !nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    list = nop_json_arr();
    for (hour = 0; hour < 24; hour++)
        nop_json_arr_push_int(list, 0);
    nop_json_add(response->content, "list", list);
    return NOP_OK;
}

/* =========================================================================== */
void cap_cloud_register(nop_router_t *router)
{
    nop_router_register(router, "getCloudRecordConfigs", CAP_MISC,
                        handle_get_cloud_record_configs);
    nop_router_register(router, "setCloudRecordConfigs", CAP_MISC,
                        handle_set_cloud_record_configs);
    nop_router_register(router, "getCloudRecordLogConfig", CAP_MISC,
                        handle_get_cloud_record_log_config);
    nop_router_register(router, "setCloudRecordLogConfig", CAP_MISC,
                        handle_set_cloud_record_log_config);
    nop_router_register(router, "getCloudRecordTestProgress", CAP_MISC,
                        handle_get_cloud_record_test_progress);
    nop_router_register(router, "startCloudRecordTest", CAP_MISC,
                        handle_start_cloud_record_test);
    nop_router_register(router, "stopCloudRecordTest", CAP_MISC,
                        handle_stop_cloud_record_test);
    nop_router_register(router, "X_NightOwl_getCloudRecordSwitch", CAP_MISC,
                        handle_get_cloud_record_switch);
    nop_router_register(router, "X_NightOwl_setCloudRecordSwitch", CAP_MISC,
                        handle_set_cloud_record_switch);
    nop_router_register(router, "getChannelRecordingTime", CAP_MISC,
                        handle_get_channel_recording_time);
    nop_router_register(router, "setChannelRecordingTime", CAP_MISC,
                        handle_set_channel_recording_time);
    nop_router_register(router, "getRecordingBackupProgress", CAP_MISC,
                        handle_get_recording_backup_progress);
    nop_router_register(router, "startRecordingBackup", CAP_MISC,
                        handle_start_recording_backup);
    nop_router_register(router, "getPlaybackCapabilities", CAP_MISC,
                        handle_get_playback_capabilities);
    nop_router_register(router, "startPlayback", CAP_MISC,
                        handle_start_playback);
    nop_router_register(router, "X_NightOwl_getChannelContinuousScheduleRecordingSwitch", CAP_MISC,
                        handle_get_continuous_schedule_switch);
    nop_router_register(router, "X_NightOwl_setChannelContinuousScheduleRecordingSwitch", CAP_MISC,
                        handle_set_continuous_schedule_switch);
    nop_router_register(router, "X_NightOwl_getChannelRecordingTriggers", CAP_MISC,
                        handle_get_recording_triggers);
    nop_router_register(router, "X_NightOwl_setChannelRecordingTriggers", CAP_MISC,
                        handle_set_recording_triggers);
    nop_router_register(router, "X_NightOwl_setChannelContinuousRecordingSchedule", CAP_MISC,
                        handle_set_continuous_schedule);
    nop_router_register(router, "X_NightOwl_queryContinuousCalendar", CAP_MISC,
                        handle_query_continuous_calendar);
    nop_router_register(router, "X_NightOwl_queryRecordingInterval", CAP_MISC,
                        handle_query_recording_interval);
}
