/**
 * @file cap_misc_ext.c
 * @brief CAP_MISC assorted channel/stats/security/OSD handlers.
 *
 * Handlers for a grab-bag of channel-scoped commands: per-channel statistics
 * and status queries, recording-content and sensor-linkage configuration,
 * cloud-record-stats monitoring switch, enhanced-security provisioning, OSD,
 * privacy zone, digital zoom/pan and device activation. State is held in
 * module-static arrays (per-channel where a command carries a channel
 * argument) so get/set round-trips correctly today; firmware will override
 * these via the HAL layer later. Scalar values round-trip exactly.
 * Array/object-valued fields (sensor linkage lists, OSD configs, privacy-zone
 * points) are stored as a serialized JSON snapshot on set and parsed back on
 * get, falling back to a spec-shaped default when nothing has been set yet.
 * The query/stats/status commands (channel stats, cable connect, binding mode,
 * cloud-status history, channel loading, sensor config, cloud-record stats)
 * return spec-shaped objects with scalar defaults and empty/default arrays.
 * Action commands (clear enhanced security, factory-test popup answer, set
 * device active) validate required args and return a spec-shaped success.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_error_str.h"
#include "onvif/mapping/nop_onvif_map.h"
#include <string.h>
#include <stdlib.h>

#define MISC_EXT_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= MISC_EXT_MAX_CHANNELS) ? 0 : channel;
}

/* ---- module-static state -------------------------------------------------- */
static int   g_recording_no_audio[MISC_EXT_MAX_CHANNELS];
static int   g_cloud_record_stats_switch[MISC_EXT_MAX_CHANNELS];
static char *g_sensor_linkage_json[MISC_EXT_MAX_CHANNELS];   /* {"sensors":[...]} snapshot */
static char *g_osd_configs_json[MISC_EXT_MAX_CHANNELS];      /* {"OSDConfigs":[...]} snapshot */
static char *g_privacy_zone_json[MISC_EXT_MAX_CHANNELS];     /* {"privacyZonePoints":[...]} snapshot */

static int   g_zoom_pan_enable[MISC_EXT_MAX_CHANNELS];
static int   g_zoom_pan_center_x[MISC_EXT_MAX_CHANNELS];
static int   g_zoom_pan_center_y[MISC_EXT_MAX_CHANNELS];
static int   g_zoom_pan_ratio[MISC_EXT_MAX_CHANNELS];
static int   g_zoom_pan_initialized[MISC_EXT_MAX_CHANNELS];

static char *g_enhanced_security_random[MISC_EXT_MAX_CHANNELS]; /* current random; NULL => normal mode */
static int   g_device_active[MISC_EXT_MAX_CHANNELS];

/* ---- small helpers -------------------------------------------------------- */
/* Read the "channel" arg (default 1) and clamp into the state-array range. */
static int read_channel(const nop_request_t *request)
{
    return clamp_channel((int)nop_json_num(request->args, "channel", 1));
}

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

/* Parse a stored snapshot back into a fresh tree, or NULL when unset. */
static nop_json_t *load_json_snapshot(const char *text)
{
    if (!text)
        return NULL;
    return nop_json_parse(text, strlen(text));
}

/* Lazily default the zoom/pan state for @p channel to spec values. */
static void ensure_zoom_pan_default(int channel)
{
    if (g_zoom_pan_initialized[channel])
        return;
    g_zoom_pan_enable[channel]   = 1;
    g_zoom_pan_center_x[channel] = 500;
    g_zoom_pan_center_y[channel] = 500;
    g_zoom_pan_ratio[channel]    = 100;
    g_zoom_pan_initialized[channel] = 1;
}

/* ===========================================================================
 * Channel statistics
 * =========================================================================== */
static nop_status_t handle_get_channel_stats(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "startTime", 0);
    nop_json_add_int(response->content, "endTime", 0);
    nop_json_add_int(response->content, "events_total", 0);
    nop_json_add_int(response->content, "events_pir", 0);
    nop_json_add_int(response->content, "events_md", 0);
    nop_json_add_int(response->content, "events_hd", 0);
    nop_json_add_int(response->content, "events_vd", 0);
    nop_json_add_int(response->content, "events_doorbellRing", 0);
    nop_json_add_int(response->content, "push_upload_image_success", 0);
    nop_json_add_int(response->content, "push_upload_image_fail", 0);
    nop_json_add_int(response->content, "push_success", 0);
    nop_json_add_int(response->content, "push_fail", 0);
    nop_json_add_int(response->content, "remote_connectCount", 0);
    nop_json_add_int(response->content, "remote_LiveviewCount", 0);
    nop_json_add_int(response->content, "remote_LiveviewSuccessCount", 0);
    nop_json_add_int(response->content, "remote_PlaybackCount", 0);
    nop_json_add_int(response->content, "remote_PlaybackSuccessCount", 0);
    nop_json_add_int(response->content, "remote_SpeakerCount", 0);
    nop_json_add_int(response->content, "remote_SpeakerSuccessCount", 0);
    nop_json_add_int(response->content, "bat_pirWakeupTotalSecs", 0);
    nop_json_add_int(response->content, "bat_pirFalseAlarmTotalSecs", 0);
    nop_json_add_int(response->content, "bat_remoteWakeupTotalSecs", 0);
    nop_json_add_int(response->content, "storage_usage_eventRec", 0);
    nop_json_add_int(response->content, "storage_usage_continueRec", 0);
    return NOP_OK;
}

/* ===========================================================================
 * Cable connect status (HDMI/VGA)
 * =========================================================================== */
static nop_status_t handle_get_cable_connect_status(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "HDMI", false);
    nop_json_add_bool(response->content, "VGA", false);
    return NOP_OK;
}

/* ===========================================================================
 * Channel binding mode
 * =========================================================================== */
static nop_status_t handle_get_channel_binding_mode(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "mode", "notBinded");
    return NOP_OK;
}

/* ===========================================================================
 * Cloud status history (DataDog-shaped, empty bucket list)
 * =========================================================================== */
static nop_status_t handle_get_cloud_status_history(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "retentionHours", 168);
    nop_json_add_int(response->content, "totalBuckets", 0);
    nop_json_add_str(response->content, "earliestWindowStart", "");
    nop_json_add_str(response->content, "latestWindowEnd", "");
    nop_json_add(response->content, "buckets", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * Channel recording content (noAudio)
 * =========================================================================== */
static nop_status_t handle_get_channel_recording_content(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "noAudio", g_recording_no_audio[channel] != 0);
    return NOP_OK;
}

static nop_status_t handle_set_channel_recording_content(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    g_recording_no_audio[channel] = nop_json_bool(request->args, "noAudio", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Channel sensor config (read-only detection sensor list)
 * =========================================================================== */
static void add_default_sensor_config(nop_json_t *content)
{
    static const char *const sensors[] = {
        "pixelChange", "human", "vehicle", "animal", "package"
    };
    nop_json_t *array = nop_json_arr();
    size_t index;
    for (index = 0; index < sizeof(sensors) / sizeof(sensors[0]); index++) {
        nop_json_t *sensor = nop_json_obj();
        nop_json_add_str(sensor, "sensor", sensors[index]);
        nop_json_add_bool(sensor, "enable", true);
        nop_json_add_bool(sensor, "drawRegion", false);
        nop_json_add_bool(sensor, "drawText", false);
        nop_json_add_int(sensor, "eventInterval", 30);
        nop_json_arr_push(array, sensor);
    }
    nop_json_add(content, "sensors", array);
}

static nop_status_t handle_get_channel_sensor_config(const nop_request_t *request,
                                                     nop_response_t *response,
                                                     void *handler_context)
{
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    response->content = nop_json_obj();
    add_default_sensor_config(response->content);
    return NOP_OK;
}

/* ===========================================================================
 * Channel sensor linkage (mail/buzzer/ftp per sensor)
 * =========================================================================== */
static void add_default_sensor_linkage(nop_json_t *content)
{
    static const char *const sensors[] = { "pixelChange", "human", "vehicle" };
    nop_json_t *array = nop_json_arr();
    size_t index;
    for (index = 0; index < sizeof(sensors) / sizeof(sensors[0]); index++) {
        nop_json_t *sensor = nop_json_obj();
        nop_json_add_str(sensor, "sensor", sensors[index]);
        nop_json_add_bool(sensor, "mail", false);
        nop_json_add_bool(sensor, "buzzer", false);
        nop_json_add_bool(sensor, "ftp", false);
        nop_json_arr_push(array, sensor);
    }
    nop_json_add(content, "sensors", array);
}

static nop_status_t handle_get_channel_sensor_linkage(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = load_json_snapshot(g_sensor_linkage_json[channel]);
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_sensor_linkage(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_channel_sensor_linkage(const nop_request_t *request,
                                                      nop_response_t *response,
                                                      void *handler_context)
{
    int channel;
    nop_json_t *snapshot;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "sensors"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    snapshot = nop_json_obj();
    {
        char *sensors_text = nop_json_print(nop_json_get(request->args, "sensors"));
        if (sensors_text) {
            nop_json_t *sensors = nop_json_parse(sensors_text, strlen(sensors_text));
            free(sensors_text);
            if (sensors)
                nop_json_add(snapshot, "sensors", sensors);
        }
    }
    if (!nop_json_get(snapshot, "sensors"))
        nop_json_add(snapshot, "sensors", nop_json_arr());
    store_json_snapshot(&g_sensor_linkage_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Channel cloud-record stats (read-only monitoring counters)
 * =========================================================================== */
static nop_status_t handle_get_channel_cloud_record_stats(const nop_request_t *request,
                                                          nop_response_t *response,
                                                          void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "startTime", 0);
    nop_json_add_int(response->content, "endTime", 0);
    nop_json_add(response->content, "videoResolution", nop_json_arr());
    nop_json_add_int(response->content, "getUrlOKTimes", 0);
    nop_json_add_int(response->content, "getUrlNGTimes", 0);
    nop_json_add(response->content, "getUrlNGCodes", nop_json_obj());
    nop_json_add_int(response->content, "eventUploadOKTimes", 0);
    nop_json_add_int(response->content, "eventUploadNGTimes", 0);
    nop_json_add_int(response->content, "tsUploadOKTimes", 0);
    nop_json_add_int(response->content, "tsUploadNGTimes", 0);
    nop_json_add(response->content, "tsUploadNGCodes", nop_json_obj());
    nop_json_add_int(response->content, "tagsUpdateOKTimes", 0);
    nop_json_add_int(response->content, "tagsUpdateNGTimes", 0);
    nop_json_add(response->content, "tagsUpdateNGCodes", nop_json_obj());
    nop_json_add_int(response->content, "tsUploadDropForNoSpace", 0);
    nop_json_add_int(response->content, "tsUploadDropForNoNetwork", 0);
    nop_json_add(response->content, "mode", nop_json_arr());
    return NOP_OK;
}

/* ===========================================================================
 * Channel cloud-record stats switch
 * =========================================================================== */
static nop_status_t handle_get_channel_cloud_record_stats_switch(const nop_request_t *request,
                                                                 nop_response_t *response,
                                                                 void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_cloud_record_stats_switch[channel] != 0);
    return NOP_OK;
}

static nop_status_t handle_set_channel_cloud_record_stats_switch(const nop_request_t *request,
                                                                 nop_response_t *response,
                                                                 void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    g_cloud_record_stats_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Channel loading (uptime/CPU/DDR/temperature)
 * =========================================================================== */
static nop_status_t handle_get_channel_loading(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "uptime", 0);
    nop_json_add_int(response->content, "CPUUsage", 0);
    nop_json_add_int(response->content, "DDRUsage", 0);
    nop_json_add_int(response->content, "temperature", 0);
    return NOP_OK;
}

/* ===========================================================================
 * Enhanced security: clear / get / set
 * =========================================================================== */
static nop_status_t handle_clear_enhanced_security(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "random") ||
        !nop_json_has(request->args, "forceToken"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    if (g_enhanced_security_random[channel]) {
        free(g_enhanced_security_random[channel]);
        g_enhanced_security_random[channel] = NULL;
    }
    response->content = nop_json_obj();
    return NOP_OK;
}

static nop_status_t handle_get_enhanced_security(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "random",
                     g_enhanced_security_random[channel] ? g_enhanced_security_random[channel] : "");
    return NOP_OK;
}

static nop_status_t handle_set_enhanced_security(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    const char *random;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "random"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    random = nop_json_str(request->args, "random", "");
    response->content = nop_json_obj();
    if (!random || random[0] == '\0') {
        nop_json_add_str(response->content, "error", NOP_ERRSTR_RANDOM_EMPTY);
        return NOP_OK;
    }
    if (g_enhanced_security_random[channel]) {
        free(g_enhanced_security_random[channel]);
        g_enhanced_security_random[channel] = NULL;
    }
    {
        size_t length = strlen(random);
        char *stored = (char *)malloc(length + 1);
        if (stored) {
            memcpy(stored, random, length + 1);
            g_enhanced_security_random[channel] = stored;
        }
    }
    return NOP_OK;
}

/* ===========================================================================
 * Factory test: answer popup window
 * =========================================================================== */
static nop_status_t handle_factory_test_answer_popup_win(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "button"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    return NOP_OK;
}

/* ===========================================================================
 * OSD: get / set
 * =========================================================================== */
static void add_default_osd_configs(nop_json_t *content)
{
    nop_json_t *array = nop_json_arr();
    nop_json_t *name = nop_json_obj();
    nop_json_t *datetime = nop_json_obj();
    nop_json_t *watermark = nop_json_obj();
    nop_json_add_str(name, "osdToken", "OSD_Name");
    nop_json_add_bool(name, "enable", true);
    nop_json_add_str(name, "positionType", "BottomRight");
    nop_json_arr_push(array, name);
    nop_json_add_str(datetime, "osdToken", "OSD_DateTime");
    nop_json_add_bool(datetime, "enable", true);
    nop_json_add_str(datetime, "positionType", "TopMiddle");
    nop_json_arr_push(array, datetime);
    nop_json_add_str(watermark, "osdToken", "OSD_WaterMark");
    nop_json_add_bool(watermark, "enable", true);
    nop_json_add_str(watermark, "positionType", "TopRight");
    nop_json_arr_push(array, watermark);
    nop_json_add(content, "OSDConfigs", array);
}

static nop_status_t handle_get_osd(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    int channel;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    response->content = load_json_snapshot(g_osd_configs_json[channel]);
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_osd_configs(response->content);
    }
    return NOP_OK;
}

/* Build a one-entry OSDConfigs snapshot from the set args. */
static nop_json_t *build_osd_snapshot(const nop_request_t *request)
{
    nop_json_t *snapshot = nop_json_obj();
    nop_json_t *array = nop_json_arr();
    nop_json_t *entry = nop_json_obj();
    nop_json_add_str(entry, "osdToken", nop_json_str(request->args, "osdToken", "OSD_WaterMark"));
    nop_json_add_bool(entry, "enable", nop_json_bool(request->args, "enable", true));
    if (nop_json_has(request->args, "positionType"))
        nop_json_add_str(entry, "positionType", nop_json_str(request->args, "positionType", ""));
    if (nop_json_has(request->args, "positionX"))
        nop_json_add_int(entry, "positionX", nop_json_num(request->args, "positionX", 0));
    if (nop_json_has(request->args, "positionY"))
        nop_json_add_int(entry, "positionY", nop_json_num(request->args, "positionY", 0));
    nop_json_arr_push(array, entry);
    nop_json_add(snapshot, "OSDConfigs", array);
    return snapshot;
}

static nop_status_t handle_set_osd(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    int channel;
    nop_json_t *snapshot;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "osdToken"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    snapshot = build_osd_snapshot(request);
    store_json_snapshot(&g_osd_configs_json[channel], snapshot);
    nop_json_free(snapshot);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

/* ===========================================================================
 * Privacy zone: get / set
 * =========================================================================== */
static nop_status_t handle_get_channel_privacy_zone(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel;
    nop_json_t *stored;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "width", 22);
    nop_json_add_int(response->content, "height", 18);
    stored = load_json_snapshot(g_privacy_zone_json[channel]);
    if (stored && nop_json_get(stored, "privacyZonePoints")) {
        char *points_text = nop_json_print(nop_json_get(stored, "privacyZonePoints"));
        nop_json_free(stored);
        if (points_text) {
            nop_json_t *points = nop_json_parse(points_text, strlen(points_text));
            free(points_text);
            if (points) {
                nop_json_add(response->content, "privacyZonePoints", points);
                return NOP_OK;
            }
        }
    } else if (stored) {
        nop_json_free(stored);
    }
    nop_json_add(response->content, "privacyZonePoints", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_set_channel_privacy_zone(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    int channel;
    nop_json_t *snapshot;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "privacyZonePoints"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);

    snapshot = nop_json_obj();
    {
        char *points_text = nop_json_print(nop_json_get(request->args, "privacyZonePoints"));
        if (points_text) {
            nop_json_t *points = nop_json_parse(points_text, strlen(points_text));
            free(points_text);
            if (points)
                nop_json_add(snapshot, "privacyZonePoints", points);
        }
    }
    if (!nop_json_get(snapshot, "privacyZonePoints"))
        nop_json_add(snapshot, "privacyZonePoints", nop_json_arr());
    store_json_snapshot(&g_privacy_zone_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Digital zoom/pan: get / set
 * =========================================================================== */
static nop_status_t handle_get_channel_zoom_pan(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    ensure_zoom_pan_default(channel);
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "enable", g_zoom_pan_enable[channel] != 0);
    nop_json_add_int(response->content, "CenterPointX", g_zoom_pan_center_x[channel]);
    nop_json_add_int(response->content, "CenterPointY", g_zoom_pan_center_y[channel]);
    nop_json_add_int(response->content, "ZoomRatio", g_zoom_pan_ratio[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_channel_zoom_pan(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    ensure_zoom_pan_default(channel);
    g_zoom_pan_enable[channel]   = nop_json_bool(request->args, "enable", true) ? 1 : 0;
    g_zoom_pan_center_x[channel] = (int)nop_json_num(request->args, "CenterPointX", g_zoom_pan_center_x[channel]);
    g_zoom_pan_center_y[channel] = (int)nop_json_num(request->args, "CenterPointY", g_zoom_pan_center_y[channel]);
    g_zoom_pan_ratio[channel]    = (int)nop_json_num(request->args, "ZoomRatio", g_zoom_pan_ratio[channel]);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    nop_json_add_int(response->content, "CenterPointX", g_zoom_pan_center_x[channel]);
    nop_json_add_int(response->content, "CenterPointY", g_zoom_pan_center_y[channel]);
    nop_json_add_int(response->content, "ZoomRatio", g_zoom_pan_ratio[channel]);
    return NOP_OK;
}

/* ===========================================================================
 * Device active: get / set
 * =========================================================================== */
static nop_status_t handle_get_device_active(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "status", g_device_active[channel] ? 1 : 0);
    return NOP_OK;
}

static nop_status_t handle_set_device_active(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "password"))
        return NOP_ERR_PARAM;
    channel = read_channel(request);
    response->content = nop_json_obj();
    if (g_device_active[channel]) {
        nop_json_add_str(response->content, "error", NOP_ERRSTR_ALREADY_ACTIVE);
        return NOP_OK;
    }
    g_device_active[channel] = 1;
    nop_json_add_str(response->content, "error", "");
    return NOP_OK;
}

/* =========================================================================== */
void cap_misc_ext_register(nop_router_t *router)
{
    nop_router_register(router, "getChannelStats", CAP_MISC,
                        handle_get_channel_stats);
    nop_router_register(router, "getCableConnectStatus", CAP_MISC,
                        handle_get_cable_connect_status);
    nop_router_register(router, "getChannelBindingMode", CAP_MISC,
                        handle_get_channel_binding_mode);
    nop_router_register(router, "getCloudStatusHistory", CAP_MISC,
                        handle_get_cloud_status_history);
    nop_router_register(router, "getChannelRecordingContent", CAP_MISC,
                        handle_get_channel_recording_content);
    nop_router_register(router, "setChannelRecordingContent", CAP_MISC,
                        handle_set_channel_recording_content);
    nop_router_register(router, "getChannelSensorConfig", CAP_MISC,
                        handle_get_channel_sensor_config);
    nop_router_register(router, "getChannelSensorLinkage", CAP_MISC,
                        handle_get_channel_sensor_linkage);
    nop_router_register(router, "setChannelSensorLinkage", CAP_MISC,
                        handle_set_channel_sensor_linkage);
    nop_router_register(router, "getChannelCloudRecordStats", CAP_MISC,
                        handle_get_channel_cloud_record_stats);
    nop_router_register(router, "getChannelCloudRecordStatsSwitch", CAP_MISC,
                        handle_get_channel_cloud_record_stats_switch);
    nop_router_register(router, "setChannelCloudRecordStatsSwitch", CAP_MISC,
                        handle_set_channel_cloud_record_stats_switch);
    nop_router_register(router, "getChannelLoading", CAP_MISC,
                        handle_get_channel_loading);
    nop_router_register(router, "ClearEnhancedSecurity", CAP_MISC,
                        handle_clear_enhanced_security);
    nop_router_register(router, "getEnhancedSecurity", CAP_MISC,
                        handle_get_enhanced_security);
    nop_router_register(router, "setEnhancedSecurity", CAP_MISC,
                        handle_set_enhanced_security);
    nop_router_register(router, "FactoryTest_AnswerPopupWin", CAP_MISC,
                        handle_factory_test_answer_popup_win);
    nop_router_register(router, "X_NightOwl_getOSD", CAP_MISC,
                        handle_get_osd);
    nop_router_register(router, "X_NightOwl_setOSD", CAP_MISC,
                        handle_set_osd);
    nop_router_register(router, "X_NightOwl_getChannelPrivacyZone", CAP_MISC,
                        handle_get_channel_privacy_zone);
    nop_router_register(router, "X_NightOwl_setChannelPrivacyZone", CAP_MISC,
                        handle_set_channel_privacy_zone);
    nop_router_register(router, "X_NightOwl_getChannelZoomPan", CAP_MISC,
                        handle_get_channel_zoom_pan);
    nop_router_register(router, "X_NightOwl_setChannelZoomPan", CAP_MISC,
                        handle_set_channel_zoom_pan);
    nop_router_register(router, "X_NightOwl_getDeviceActive", CAP_MISC,
                        handle_get_device_active);
    nop_router_register(router, "X_NightOwl_setDeviceActive", CAP_MISC,
                        handle_set_device_active);
}
