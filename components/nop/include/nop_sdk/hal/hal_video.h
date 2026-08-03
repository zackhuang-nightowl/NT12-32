/**
 * @file hal_video.h
 * @brief Video HAL: stream capabilities + encoded-frame source. Firmware
 *        registers this under HAL_VIDEO; media/ subscribes to it for RTSP/P2P.
 */
#ifndef NOP_SDK_HAL_VIDEO_H
#define NOP_SDK_HAL_VIDEO_H

#include "nop_sdk/nop_err.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Video frame codec, advertised on each encoded frame. */
typedef enum hal_video_codec {
    HAL_VIDEO_CODEC_H264 = 0,
    HAL_VIDEO_CODEC_H265
} hal_video_codec_t;

/**
 * One encoded video frame handed up from firmware. @p data is owned by the
 * caller (firmware) and is only valid for the duration of the sink callback —
 * sinks that need to keep it must copy.
 */
typedef struct hal_video_frame {
    int               stream;        /**< 0=main, 1=sub */
    hal_video_codec_t codec;
    int               is_keyframe;   /**< 1 = IDR/keyframe */
    uint64_t          timestamp_ms;  /**< capture/presentation time */
    const uint8_t    *data;          /**< encoded bitstream (NAL units) */
    size_t            length;
} hal_video_frame_t;

/**
 * Frame sink the SDK media plane installs. Firmware invokes it for every
 * encoded frame on @p channel. Must be callable from the firmware's encoder
 * thread; the media plane is responsible for any fan-out/locking.
 */
typedef void (*hal_video_frame_sink_fn)(void *sink_ctx, int channel,
                                        const hal_video_frame_t *frame);

/** Encoder parameters for a channel/stream (ONVIF SetVideoEncoderConfiguration
 *  and NOP set-resolution map onto this). */
typedef struct hal_video_encoder_config {
    hal_video_codec_t codec;
    int               width;
    int               height;
    int               framerate;     /**< frames/sec */
    int               bitrate_kbps;
    int               gop;           /**< I-frame interval (frames) */
} hal_video_encoder_config_t;

/** On-screen-display overlay content burned into the encoded stream. */
typedef struct hal_video_osd {
    int  show_time;                  /**< burn the timestamp */
    char time_format[24];            /**< e.g. "YYYY-MM-DD HH:MM:SS" */
    int  show_watermark;             /**< burn the NightOwl watermark */
    char channel_name[48];           /**< channel/device name text */
} hal_video_osd_t;

/** Day/night imaging mode. */
typedef enum hal_video_day_night {
    HAL_VIDEO_MODE_AUTO = 0,   /**< SmartIR / light-sensor driven */
    HAL_VIDEO_MODE_DAY,        /**< force day (color) */
    HAL_VIDEO_MODE_NIGHT,      /**< force night (IR/B&W) */
    HAL_VIDEO_MODE_COLOR       /**< color at night (white-light supplement) */
} hal_video_day_night_t;

/** Opaque per-segment muxer handle returned by mux_open (firmware-owned). The
 *  SDK record engine treats it as a token to pass back to mux_write/mux_close. */
typedef void *hal_video_mux_t;

typedef struct hal_video_if {
    /** @return number of live-capable channels (cameras connected). */
    int (*channel_count)(void *ctx);
    /** Start the encoded stream for @p channel/@p stream (0=main,1=sub). */
    nop_status_t (*start_stream)(void *ctx, int channel, int stream);
    /** Stop a previously started stream. */
    nop_status_t (*stop_stream)(void *ctx, int channel, int stream);
    /**
     * Install the encoded-frame sink (the media plane). Firmware stores
     * @p sink/@p sink_ctx and calls sink(sink_ctx, channel, frame) per frame.
     * Pass sink=NULL to detach. May be NULL if the device has no live video.
     */
    nop_status_t (*set_frame_sink)(void *ctx, hal_video_frame_sink_fn sink, void *sink_ctx);
    /** Apply encoder parameters to @p channel/@p stream (modify-stream). May be NULL. */
    nop_status_t (*set_encoder_config)(void *ctx, int channel, int stream,
                                       const hal_video_encoder_config_t *config);
    /** Set the OSD/watermark burned into @p channel. May be NULL. */
    nop_status_t (*set_osd)(void *ctx, int channel, const hal_video_osd_t *osd);
    /**
     * Capture a JPEG snapshot of @p channel/@p stream into @p buffer (capacity
     * @p capacity); writes the byte count to @p *out_len. May be NULL.
     */
    nop_status_t (*capture_snapshot)(void *ctx, int channel, int stream,
                                     uint8_t *buffer, size_t capacity, size_t *out_len);
    /** Set day/night imaging mode for @p channel. May be NULL. */
    nop_status_t (*set_day_night)(void *ctx, int channel, hal_video_day_night_t mode);
    /**
     * Open a recording segment (container file) for @p channel/@p stream at
     * @p path and return its muxer handle in @p *out_mux. On 8856-class parts
     * this is typically a hardware/library mp4 muxer. May be NULL (device has
     * no local recording — svc_record then degrades to NOTIMPL).
     */
    nop_status_t (*mux_open)(void *ctx, int channel, int stream,
                             const char *path, hal_video_mux_t *out_mux);
    /** Append one encoded frame to an open segment @p mux. May be NULL. */
    nop_status_t (*mux_write)(void *ctx, hal_video_mux_t mux,
                              const hal_video_frame_t *frame);
    /** Finalize and close a segment @p mux (writes the container index). May be NULL. */
    nop_status_t (*mux_close)(void *ctx, hal_video_mux_t mux);
    void *ctx;
} hal_video_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_VIDEO_H */
