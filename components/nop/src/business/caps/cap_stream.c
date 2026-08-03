/**
 * @file cap_stream.c
 * @brief CAP_STREAM handlers: getLiveCapabilities / startLiveStream /
 *        stopLiveStream. Gated by CAP_STREAM and backed by HAL_VIDEO.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_video.h"

static nop_status_t handle_get_live_capabilities(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    nop_json_t         *channels;
    int                 channel_count, channel_index;
    (void)request; (void)handler_context;

    if (!video || !video->channel_count)
        return NOP_ERR_NOTIMPL;
    channel_count = video->channel_count(video->ctx);

    response->content = nop_json_obj();
    channels = nop_json_arr();
    for (channel_index = 0; channel_index < channel_count; channel_index++) {
        nop_json_t *channel = nop_json_obj();
        nop_json_add_int(channel, "channel", channel_index);
        nop_json_arr_push(channels, channel);
    }
    nop_json_add(response->content, "channels", channels);
    return NOP_OK;
}

static nop_status_t handle_start_live_stream(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    int channel, stream;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    stream  = (int)nop_json_num(request->args, "stream", 0);

    if (!video || !video->start_stream)
        return NOP_ERR_NOTIMPL;
    return video->start_stream(video->ctx, channel, stream);
}

static nop_status_t handle_stop_live_stream(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    int channel, stream;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    stream  = (int)nop_json_num(request->args, "stream", 0);

    if (!video || !video->stop_stream)
        return NOP_ERR_NOTIMPL;
    return video->stop_stream(video->ctx, channel, stream);
}

void cap_stream_register(nop_router_t *router)
{
    nop_router_register(router, "getLiveCapabilities", CAP_STREAM, handle_get_live_capabilities);
    nop_router_register(router, "startLiveStream", CAP_STREAM, handle_start_live_stream);
    nop_router_register(router, "stopLiveStream", CAP_STREAM, handle_stop_live_stream);
}
