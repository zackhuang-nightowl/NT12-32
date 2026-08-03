/**
 * @file cap_video.c
 * @brief CAP_MISC handlers: video/image settings (brightness, contrast,
 *        orientation, resolution, fisheye, time-lapse) plus panoramic and
 *        snapshot commands. Backed by in-memory module-static state.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_video.h"

#include <string.h>

#define VIDEO_MAX_CHANNELS 16

/* Per-channel image settings, indexed by clamped channel number. */
static int  g_brightness[VIDEO_MAX_CHANNELS];
static int  g_contrast[VIDEO_MAX_CHANNELS];
static int  g_orientation[VIDEO_MAX_CHANNELS];
static int  g_main_stream_width[VIDEO_MAX_CHANNELS];
static int  g_main_stream_height[VIDEO_MAX_CHANNELS];
static int  g_sub_stream_width[VIDEO_MAX_CHANNELS];
static int  g_sub_stream_height[VIDEO_MAX_CHANNELS];
static bool g_time_lapse_switch[VIDEO_MAX_CHANNELS];

/* Fisheye mode is a single device-wide setting per the command shape. */
static char g_fisheye_mode[32] = "spherical";

static bool g_state_initialized = false;

/* Initialize per-channel state to the spec-shaped default values once. */
static void video_state_init(void)
{
    int channel;

    if (g_state_initialized)
        return;

    for (channel = 0; channel < VIDEO_MAX_CHANNELS; channel++) {
        g_brightness[channel]         = 150;
        g_contrast[channel]           = 150;
        g_orientation[channel]        = 0;
        g_main_stream_width[channel]  = 1920;
        g_main_stream_height[channel] = 1080;
        g_sub_stream_width[channel]   = 800;
        g_sub_stream_height[channel]  = 600;
        g_time_lapse_switch[channel]  = false;
    }

    g_state_initialized = true;
}

/* Read the "channel" argument and clamp it into the per-channel array range.
 * Channels in the JSON API start from 1, so map down to a zero-based index. */
static int video_channel_index(const nop_request_t *request)
{
    int channel = (int)nop_json_num(request->args, "channel", 1);

    channel -= 1;
    if (channel < 0 || channel >= VIDEO_MAX_CHANNELS)
        channel = 0;
    return channel;
}

/* Apply a stream's resolution to the video HAL encoder (modify-stream reaching
 * hardware). No-op when HAL_VIDEO or set_encoder_config is absent. */
static void video_apply_encoder(int channel, int stream, int width, int height)
{
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    hal_video_encoder_config_t config;
    if (!video || !video->set_encoder_config)
        return;
    memset(&config, 0, sizeof(config));
    config.codec        = HAL_VIDEO_CODEC_H264;
    config.width        = width;
    config.height       = height;
    config.framerate    = 25;
    config.bitrate_kbps = (stream == 0) ? 4096 : 1024;
    config.gop          = 50;
    video->set_encoder_config(video->ctx, channel, stream, &config);
}

/* Build a {"width":w,"height":h} resolution object. */
static nop_json_t *video_make_resolution(int width, int height)
{
    nop_json_t *resolution = nop_json_obj();

    nop_json_add_int(resolution, "width", width);
    nop_json_add_int(resolution, "height", height);
    return resolution;
}

static nop_status_t handle_get_video_brightness(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    (void)handler_context;

    video_state_init();
    channel = video_channel_index(request);

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", g_brightness[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_video_brightness(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;

    channel = video_channel_index(request);
    g_brightness[channel] = (int)nop_json_num(request->args, "value", 150);
    return NOP_OK;
}

static nop_status_t handle_get_video_contrast(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel;
    (void)handler_context;

    video_state_init();
    channel = video_channel_index(request);

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", g_contrast[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_video_contrast(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;

    channel = video_channel_index(request);
    g_contrast[channel] = (int)nop_json_num(request->args, "value", 150);
    return NOP_OK;
}

static nop_status_t handle_get_video_orientation(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel;
    (void)handler_context;

    video_state_init();
    channel = video_channel_index(request);

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "value", g_orientation[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_video_orientation(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;

    channel = video_channel_index(request);
    g_orientation[channel] = (int)nop_json_num(request->args, "value", 0);
    return NOP_OK;
}

static nop_status_t handle_get_channel_current_video_resolution(const nop_request_t *request,
                                                                nop_response_t *response,
                                                                void *handler_context)
{
    int channel;
    (void)handler_context;

    video_state_init();
    channel = video_channel_index(request);

    response->content = nop_json_obj();
    nop_json_add(response->content, "mainStream",
                 video_make_resolution(g_main_stream_width[channel],
                                       g_main_stream_height[channel]));
    nop_json_add(response->content, "subStream",
                 video_make_resolution(g_sub_stream_width[channel],
                                       g_sub_stream_height[channel]));
    return NOP_OK;
}

static nop_status_t handle_set_channel_current_video_resolution(const nop_request_t *request,
                                                                nop_response_t *response,
                                                                void *handler_context)
{
    const nop_json_t *main_stream;
    const nop_json_t *sub_stream;
    int channel;
    (void)response; (void)handler_context;

    video_state_init();

    main_stream = nop_json_get(request->args, "mainStream");
    sub_stream  = nop_json_get(request->args, "subStream");

    /* At least one of mainStream or subStream must be present. */
    if (!main_stream && !sub_stream)
        return NOP_ERR_PARAM;

    channel = video_channel_index(request);

    if (main_stream) {
        g_main_stream_width[channel]  = (int)nop_json_num(main_stream, "width", 1920);
        g_main_stream_height[channel] = (int)nop_json_num(main_stream, "height", 1080);
        video_apply_encoder(channel, 0, g_main_stream_width[channel], g_main_stream_height[channel]);
    }
    if (sub_stream) {
        g_sub_stream_width[channel]  = (int)nop_json_num(sub_stream, "width", 800);
        g_sub_stream_height[channel] = (int)nop_json_num(sub_stream, "height", 600);
        video_apply_encoder(channel, 1, g_sub_stream_width[channel], g_sub_stream_height[channel]);
    }
    return NOP_OK;
}

static nop_status_t handle_get_channel_supported_video_resolutions(const nop_request_t *request,
                                                                   nop_response_t *response,
                                                                   void *handler_context)
{
    nop_json_t *main_resolutions;
    nop_json_t *sub_resolutions;
    (void)request; (void)handler_context;

    video_state_init();

    main_resolutions = nop_json_arr();
    nop_json_arr_push(main_resolutions, video_make_resolution(800, 600));
    nop_json_arr_push(main_resolutions, video_make_resolution(1920, 1080));

    sub_resolutions = nop_json_arr();
    nop_json_arr_push(sub_resolutions, video_make_resolution(400, 300));
    nop_json_arr_push(sub_resolutions, video_make_resolution(800, 600));

    response->content = nop_json_obj();
    nop_json_add(response->content, "mainStreamResolutions", main_resolutions);
    nop_json_add(response->content, "subStreamResolutions", sub_resolutions);
    return NOP_OK;
}

static nop_status_t handle_get_channel_fisheye_control(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    (void)request; (void)handler_context;

    video_state_init();

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "mode", g_fisheye_mode);
    return NOP_OK;
}

static nop_status_t handle_set_channel_fisheye_control(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    const char *mode;
    (void)response; (void)handler_context;

    video_state_init();

    mode = nop_json_str(request->args, "mode", NULL);
    if (!mode)
        return NOP_ERR_PARAM;

    strncpy(g_fisheye_mode, mode, sizeof(g_fisheye_mode) - 1);
    g_fisheye_mode[sizeof(g_fisheye_mode) - 1] = '\0';
    return NOP_OK;
}

static nop_status_t handle_get_channel_time_lapse_switch(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel;
    (void)handler_context;

    video_state_init();
    channel = video_channel_index(request);

    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", g_time_lapse_switch[channel]);
    return NOP_OK;
}

static nop_status_t handle_set_channel_time_lapse_switch(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    int channel;
    (void)response; (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;

    channel = video_channel_index(request);
    g_time_lapse_switch[channel] = nop_json_bool(request->args, "value", false);
    return NOP_OK;
}

static nop_status_t handle_query_panoramic_status(const nop_request_t *request,
                                                  nop_response_t *response,
                                                  void *handler_context)
{
    (void)request; (void)handler_context;

    video_state_init();

    /* No panoramic capture is in flight in this in-memory implementation. */
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "status", "idle");
    nop_json_add_int(response->content, "timestamp", 0);
    nop_json_add_str(response->content, "url", "");
    return NOP_OK;
}

static nop_status_t handle_take_panoramic_view(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    (void)response; (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;

    /* Capture is acknowledged; status is polled via queryPanoramicStatus. */
    return NOP_OK;
}

static nop_status_t handle_snapshot_channel(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)handler_context;

    video_state_init();
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;

    /* The snapshot image is served by the media plane; no URL yet. */
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "url", "");
    return NOP_OK;
}

void cap_video_register(nop_router_t *router)
{
    nop_router_register(router, "X_NightOwl_getVideoBrightness", CAP_MISC,
                        handle_get_video_brightness);
    nop_router_register(router, "X_NightOwl_setVideoBrightness", CAP_MISC,
                        handle_set_video_brightness);
    nop_router_register(router, "X_NightOwl_getVideoContrast", CAP_MISC,
                        handle_get_video_contrast);
    nop_router_register(router, "X_NightOwl_setVideoContrast", CAP_MISC,
                        handle_set_video_contrast);
    nop_router_register(router, "X_NightOwl_getVideoOrientation", CAP_MISC,
                        handle_get_video_orientation);
    nop_router_register(router, "X_NightOwl_setVideoOrientation", CAP_MISC,
                        handle_set_video_orientation);
    nop_router_register(router, "X_NightOwl_getChannelCurrentVideoResolution", CAP_MISC,
                        handle_get_channel_current_video_resolution);
    nop_router_register(router, "X_NightOwl_setChannelCurrentVideoResolution", CAP_MISC,
                        handle_set_channel_current_video_resolution);
    nop_router_register(router, "X_NightOwl_getChannelSupportedVideoResolutions", CAP_MISC,
                        handle_get_channel_supported_video_resolutions);
    nop_router_register(router, "X_NightOwl_getChannelFisheyeControl", CAP_MISC,
                        handle_get_channel_fisheye_control);
    nop_router_register(router, "X_NightOwl_setChannelFisheyeControl", CAP_MISC,
                        handle_set_channel_fisheye_control);
    nop_router_register(router, "X_NightOwl_getChannelTimeLapseSwitch", CAP_MISC,
                        handle_get_channel_time_lapse_switch);
    nop_router_register(router, "X_NightOwl_setChannelTimeLapseSwitch", CAP_MISC,
                        handle_set_channel_time_lapse_switch);
    nop_router_register(router, "queryPanoramicStatus", CAP_MISC,
                        handle_query_panoramic_status);
    nop_router_register(router, "takePanoramicView", CAP_MISC,
                        handle_take_panoramic_view);
    nop_router_register(router, "snapshotChannel", CAP_MISC,
                        handle_snapshot_channel);
}
