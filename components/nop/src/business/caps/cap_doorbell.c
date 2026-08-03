/**
 * @file cap_doorbell.c
 * @brief CAP_MISC doorbell, audio-alert and speaker-capability handlers.
 *
 * Handlers for the NightOwl doorbell command family, the per-channel audio
 * alert (siren) configuration, and the speaker-capability query. State is held
 * in module-static per-channel arrays so get/set round-trips correctly today;
 * firmware will override these via the HAL layer later. Scalar values
 * round-trip exactly. The object-valued audio-alert configuration is stored as
 * a serialized JSON snapshot on set and parsed back on get, falling back to a
 * spec-shaped default when nothing has been set yet. getSpeakerCapabilities
 * returns a fixed spec-shaped default.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include <string.h>
#include <stdlib.h>

#define DOORBELL_MAX_CHANNELS 16
static int clamp_channel(int channel)
{
    return (channel < 0 || channel >= DOORBELL_MAX_CHANNELS) ? 0 : channel;
}

/* ---- per-channel state (indexed by clamped channel) ----------------------- */
static int   g_audio_alert_switch[DOORBELL_MAX_CHANNELS];
static int   g_audio_alert_detect_switch[DOORBELL_MAX_CHANNELS];
static char *g_audio_alert_json[DOORBELL_MAX_CHANNELS];

static int   g_doorbell_duration_set[DOORBELL_MAX_CHANNELS];
static int   g_doorbell_duration_seconds[DOORBELL_MAX_CHANNELS];
static int   g_doorbell_switch_set[DOORBELL_MAX_CHANNELS];
static int   g_doorbell_switch[DOORBELL_MAX_CHANNELS];
static char  g_doorbell_type[DOORBELL_MAX_CHANNELS][16];

/* ---- small helpers -------------------------------------------------------- */
static void copy_string_field(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (!source)
        source = "";
    length = strlen(source);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

/* Store a serialized snapshot of a JSON value into @p slot. */
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
 * Speaker capabilities (spec-shaped default)
 * =========================================================================== */
static void add_audio_format(nop_json_t *formats, const char *codec, int sample_rate,
                             int data_bit, const char *audio_channel)
{
    nop_json_t *format = nop_json_obj();
    nop_json_add_str(format, "codec", codec);
    nop_json_add_int(format, "sampleRate", (double)sample_rate);
    nop_json_add_int(format, "dataBit", (double)data_bit);
    nop_json_add_str(format, "audioChannel", audio_channel);
    nop_json_arr_push(formats, format);
}

static nop_status_t handle_get_speaker_capabilities(const nop_request_t *request,
                                                    nop_response_t *response,
                                                    void *handler_context)
{
    nop_json_t *protocol;
    nop_json_t *channels;
    nop_json_t *formats;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();

    protocol = nop_json_arr();
    nop_json_arr_push_str(protocol, "https");
    nop_json_arr_push_str(protocol, "iotc-av");
    nop_json_add(response->content, "protocol", protocol);

    channels = nop_json_arr();
    nop_json_arr_push_int(channels, 0);
    nop_json_arr_push_int(channels, 1);
    nop_json_add(response->content, "channels", channels);

    formats = nop_json_arr();
    add_audio_format(formats, "pcm", 8000, 16, "mono");
    add_audio_format(formats, "g711u", 8000, 16, "mono");
    nop_json_add(response->content, "supportAudioFormats", formats);
    return NOP_OK;
}

/* ===========================================================================
 * Audio alert: configuration (volume + selected source + sources list)
 * =========================================================================== */
static void add_default_audio_alert(nop_json_t *content)
{
    nop_json_t *sources = nop_json_arr();
    nop_json_add_int(content, "soundVolume", 66);
    nop_json_add_str(content, "selectedSource", "Smile");
    nop_json_arr_push_str(sources, "Smile");
    nop_json_arr_push_str(sources, "Unavailable");
    nop_json_arr_push_str(sources, "Siren");
    nop_json_add(content, "sources", sources);
}

static nop_status_t handle_get_audio_alert(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    if (g_audio_alert_json[channel]) {
        response->content = nop_json_parse(g_audio_alert_json[channel],
                                           strlen(g_audio_alert_json[channel]));
    }
    if (!response->content) {
        response->content = nop_json_obj();
        add_default_audio_alert(response->content);
    }
    return NOP_OK;
}

static nop_status_t handle_set_audio_alert(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    nop_json_t *snapshot;
    nop_json_t *sources;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "soundVolume") || !nop_json_has(request->args, "source"))
        return NOP_ERR_PARAM;
    snapshot = nop_json_obj();
    nop_json_add_int(snapshot, "soundVolume", nop_json_num(request->args, "soundVolume", 66));
    nop_json_add_str(snapshot, "selectedSource", nop_json_str(request->args, "source", "Smile"));
    sources = nop_json_arr();
    nop_json_arr_push_str(sources, "Smile");
    nop_json_arr_push_str(sources, "Unavailable");
    nop_json_arr_push_str(sources, "Siren");
    nop_json_add(snapshot, "sources", sources);
    store_json_snapshot(&g_audio_alert_json[channel], snapshot);
    nop_json_free(snapshot);
    return NOP_OK;
}

/* ===========================================================================
 * Audio alert: manual on/off switch
 * =========================================================================== */
static nop_status_t handle_get_audio_alert_switch(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_audio_alert_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_audio_alert_switch(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_audio_alert_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Audio alert: event-detection trigger switch
 * =========================================================================== */
static nop_status_t handle_get_audio_alert_detect_switch(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_audio_alert_detect_switch[channel] ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_audio_alert_detect_switch(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_audio_alert_detect_switch[channel] = nop_json_bool(request->args, "value", false) ? 1 : 0;
    return NOP_OK;
}

/* ===========================================================================
 * Doorbell: ring duration (seconds)
 * =========================================================================== */
static nop_status_t handle_get_doorbell_duration(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    int duration = g_doorbell_duration_set[channel] ? g_doorbell_duration_seconds[channel] : 10;
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", (double)duration);
    return NOP_OK;
}

static nop_status_t handle_set_doorbell_duration(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_doorbell_duration_seconds[channel] = (int)nop_json_num(request->args, "value", 10);
    g_doorbell_duration_set[channel] = 1;
    return NOP_OK;
}

/* ===========================================================================
 * Doorbell: enabled/disabled switch
 * =========================================================================== */
static nop_status_t handle_get_doorbell_switch(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    int value = g_doorbell_switch_set[channel] ? g_doorbell_switch[channel] : 1;
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", value ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_doorbell_switch(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    g_doorbell_switch[channel] = nop_json_bool(request->args, "value", true) ? 1 : 0;
    g_doorbell_switch_set[channel] = 1;
    return NOP_OK;
}

/* ===========================================================================
 * Doorbell: type ("mechanical"/"digital")
 * =========================================================================== */
static nop_status_t handle_get_doorbell_type(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    const char *type = g_doorbell_type[channel][0] ? g_doorbell_type[channel] : "mechanical";
    (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", type);
    return NOP_OK;
}

static nop_status_t handle_set_doorbell_type(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int channel = clamp_channel((int)nop_json_num(request->args, "channel", 0));
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    copy_string_field(g_doorbell_type[channel], sizeof(g_doorbell_type[channel]),
                      nop_json_str(request->args, "value", "mechanical"));
    return NOP_OK;
}

/* ===========================================================================
 * Registration
 * =========================================================================== */
void cap_doorbell_register(nop_router_t *router)
{
    nop_router_register(router, "getSpeakerCapabilities", CAP_MISC, handle_get_speaker_capabilities);
    nop_router_register(router, "X_NightOwl_getChannelAudioAlert", CAP_MISC, handle_get_audio_alert);
    nop_router_register(router, "X_NightOwl_setChannelAudioAlert", CAP_MISC, handle_set_audio_alert);
    nop_router_register(router, "X_NightOwl_getChannelAudioAlertSwitch", CAP_MISC, handle_get_audio_alert_switch);
    nop_router_register(router, "X_NightOwl_setChannelAudioAlertSwitch", CAP_MISC, handle_set_audio_alert_switch);
    nop_router_register(router, "X_NightOwl_getChannelAudioAlertDetectSwitch", CAP_MISC, handle_get_audio_alert_detect_switch);
    nop_router_register(router, "X_NightOwl_setChannelAudioAlertDetectSwitch", CAP_MISC, handle_set_audio_alert_detect_switch);
    nop_router_register(router, "X_NightOwl_getDoorbellDuration", CAP_MISC, handle_get_doorbell_duration);
    nop_router_register(router, "X_NightOwl_setDoorbellDuration", CAP_MISC, handle_set_doorbell_duration);
    nop_router_register(router, "X_NightOwl_getDoorbellSwitch", CAP_MISC, handle_get_doorbell_switch);
    nop_router_register(router, "X_NightOwl_setDoorbellSwitch", CAP_MISC, handle_set_doorbell_switch);
    nop_router_register(router, "X_NightOwl_getDoorbellType", CAP_MISC, handle_get_doorbell_type);
    nop_router_register(router, "X_NightOwl_setDoorbellType", CAP_MISC, handle_set_doorbell_type);
}
