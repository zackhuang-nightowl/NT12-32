/**
 * @file cap_audio.c
 * @brief CAP_AUDIO handlers: startSpeaker / stopSpeaker (two-way talk-down).
 *        Gated by CAP_AUDIO, backed by HAL_AUDIO.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_audio.h"

#include <string.h>

static hal_audio_codec_t parse_codec(const char *codec_name)
{
    if (!codec_name)                          return HAL_AUDIO_CODEC_PCM;
    if (!strcmp(codec_name, "g711a") ||
        !strcmp(codec_name, "G711A"))         return HAL_AUDIO_CODEC_G711A;
    if (!strcmp(codec_name, "g711u") ||
        !strcmp(codec_name, "G711U") ||
        !strcmp(codec_name, "g711") ||
        !strcmp(codec_name, "G711"))          return HAL_AUDIO_CODEC_G711U;
    if (!strcmp(codec_name, "aac")  ||
        !strcmp(codec_name, "AAC"))           return HAL_AUDIO_CODEC_AAC;
    return HAL_AUDIO_CODEC_PCM;
}

static nop_status_t handle_start_speaker(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    const hal_audio_if *audio = (const hal_audio_if *)hal_registry_get(HAL_AUDIO);
    const nop_json_t   *format_obj;
    hal_audio_format_t  format;
    const char         *channel_mode;
    int                 channel;
    nop_status_t        status;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    memset(&format, 0, sizeof(format));
    format.codec        = HAL_AUDIO_CODEC_PCM;
    format.sample_rate  = 8000;
    format.data_bit     = 16;
    format.channel_mode = HAL_AUDIO_CHANNEL_MONO;

    format_obj = nop_json_get(request->args, "audioFormat");
    if (format_obj) {
        format.codec       = parse_codec(nop_json_str(format_obj, "codec", "pcm"));
        format.sample_rate = (int)nop_json_num(format_obj, "sampleRate", 8000);
        format.data_bit    = (int)nop_json_num(format_obj, "dataBit", 16);
        channel_mode       = nop_json_str(format_obj, "audioChannel", "mono");
        format.channel_mode = (channel_mode && !strcmp(channel_mode, "stereo"))
                            ? HAL_AUDIO_CHANNEL_STEREO : HAL_AUDIO_CHANNEL_MONO;
    }

    if (!audio || !audio->start_speaker)
        return NOP_ERR_NOTIMPL;
    status = audio->start_speaker(audio->ctx, channel, &format);
    if (status != NOP_OK)
        return status;

    /* The talk-down media tunnel is served by the media plane; no URL yet. */
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "url", "");
    return NOP_OK;
}

static nop_status_t handle_stop_speaker(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const hal_audio_if *audio = (const hal_audio_if *)hal_registry_get(HAL_AUDIO);
    int channel;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (!audio || !audio->stop_speaker)
        return NOP_ERR_NOTIMPL;
    return audio->stop_speaker(audio->ctx, channel);
}

void cap_audio_register(nop_router_t *router)
{
    nop_router_register(router, "startSpeaker", CAP_AUDIO, handle_start_speaker);
    nop_router_register(router, "stopSpeaker", CAP_AUDIO, handle_stop_speaker);
}
