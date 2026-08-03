/**
 * @file hal_audio.h
 * @brief Two-way-audio HAL: speaker (talk-down) + volume. Firmware registers
 *        this under HAL_AUDIO; backs CAP_AUDIO commands (startSpeaker,
 *        stopSpeaker, getSpeakerCapabilities, audio volume config).
 *
 * Note the same gap flagged for video: start_speaker only opens the path; the
 * actual downlink samples arrive via write_audio_frame (optional until the
 * media tunnel is wired). Mirror hal_video.h's encoded-frame TODO here.
 */
#ifndef NOP_SDK_HAL_AUDIO_H
#define NOP_SDK_HAL_AUDIO_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hal_audio_codec {
    HAL_AUDIO_CODEC_PCM = 0,
    HAL_AUDIO_CODEC_G711A,
    HAL_AUDIO_CODEC_G711U,
    HAL_AUDIO_CODEC_AAC
} hal_audio_codec_t;

typedef enum hal_audio_channel_mode {
    HAL_AUDIO_CHANNEL_MONO = 0,
    HAL_AUDIO_CHANNEL_STEREO
} hal_audio_channel_mode_t;

/** Sample format negotiated for a talk-down session. */
typedef struct hal_audio_format {
    hal_audio_codec_t        codec;
    int                      sample_rate;   /**< Hz, e.g. 8000, 16000 */
    int                      data_bit;      /**< bits per sample, e.g. 16 */
    hal_audio_channel_mode_t channel_mode;
} hal_audio_format_t;

typedef struct hal_audio_if {
    /** Open the speaker on @p channel for talk-down in @p format. */
    nop_status_t (*start_speaker)(void *ctx, int channel, const hal_audio_format_t *format);
    /** Close a speaker session previously opened on @p channel. */
    nop_status_t (*stop_speaker)(void *ctx, int channel);
    /**
     * Feed @p length bytes of audio (in the started format) to @p channel's
     * speaker. May be NULL until the media tunnel delivers samples directly.
     */
    nop_status_t (*write_audio_frame)(void *ctx, int channel, const void *data, int length);
    /** Read playback volume 0..100 into @p *level_out. May be NULL. */
    nop_status_t (*get_volume)(void *ctx, int channel, int *level_out);
    /** Set playback volume 0..100. May be NULL if volume is fixed. */
    nop_status_t (*set_volume)(void *ctx, int channel, int level);
    /**
     * Play a built-in alert tone (siren / audio-alert) on @p channel for
     * @p seconds (0 = stop any playing alert). @p tone_id selects the tone
     * (0 = default siren). May be NULL if the device has no siren.
     */
    nop_status_t (*play_alert)(void *ctx, int channel, int tone_id, int seconds);
    void *ctx;
} hal_audio_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_AUDIO_H */
