/**
 * @file nop_record.h
 * @brief SD recording engine — subscribes to the media frame plane, muxes
 *        encoded frames into rotating container segments on local storage
 *        (via the hal_video mux hooks), maintains a segment index, and applies
 *        Overwrite reclaim, event-triggered pre/post recording, and event
 *        tagging (from the detection hub).
 *
 * Role: CAMERA local recording (and, per-channel, the building block the NVR
 * multi-storage engine reuses). The engine owns no codec/container itself — it
 * drives the firmware's muxer through HAL_VIDEO.mux_open/mux_write/mux_close,
 * which on 8856-class parts is a hardware/library mp4 muxer. Segments always
 * begin on a keyframe so each is independently decodable.
 *
 * Wiring:
 *   nop_record_config_t cfg; memset(&cfg,0,sizeof cfg);
 *   strcpy(cfg.storage_dir, "/mnt/sd"); cfg.mode = NOP_RECORD_CONTINUOUS;
 *   nop_record_t *rec = nop_record_create(&cfg);
 *   nop_record_attach_media(rec, media);       // frames -> segments
 *   nop_record_attach_event_hub(rec, hub);     // detections tag/trigger clips
 *   ...
 *   nop_record_destroy(rec);                    // closes the open segment
 *
 * Timing: all recording-duration decisions (segment rotation, event pre/post
 * windows, the 10-min cap) are measured on the media/presentation clock carried
 * by hal_video_frame_t.timestamp_ms — never on detection-event wall-clock
 * timestamps, which are a different clock and are used only to tag segments.
 * Consequently the windows are evaluated as frames arrive: if the stream stalls
 * (no frames), an open event clip is not closed on wall-clock elapse until the
 * next frame or nop_record_destroy(). The HAL mux hooks are invoked under the
 * engine lock and are expected to be quick hand-offs to the firmware muxer (not
 * to re-enter the engine).
 */
#ifndef NOP_SDK_RECORD_H
#define NOP_SDK_RECORD_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_media.h"
#include "nop_sdk/nop_event.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Recording mode. */
typedef enum nop_record_mode {
    NOP_RECORD_OFF = 0,      /**< engine created but not recording */
    NOP_RECORD_CONTINUOUS,   /**< record continuously, rotating segments */
    NOP_RECORD_EVENT         /**< record only around detections (pre/post) */
} nop_record_mode_t;

/**
 * Engine configuration. Zero-initialize, then set at least storage_dir.
 * reserved[] keeps the ABI stable as options are added.
 */
typedef struct nop_record_config {
    char              storage_dir[256];    /**< SD mount dir for segments (required) */
    int               channel;             /**< channel to record (default 0) */
    int               stream;              /**< 0=main, 1=sub (default 0) */
    nop_record_mode_t mode;
    int               segment_seconds;     /**< continuous: rotate every N s (0 => 60) */
    int               max_segments;        /**< Overwrite: reclaim oldest beyond this (0 => 48) */
    int               pre_record_seconds;  /**< event: seconds kept before trigger (0 => off) */
    int               post_record_seconds; /**< event: seconds after last trigger (0 => 30) */
    int               event_max_seconds;   /**< event: clip hard cap, <=600 (0 => 600) */
    uint32_t          reserved[4];
} nop_record_config_t;

/** One recorded segment (backs query/playback/download/cloud enumeration). */
typedef struct nop_record_segment {
    char     path[256];
    int      channel;
    int      stream;
    uint64_t start_ms;
    uint64_t end_ms;
    uint32_t event_mask;   /**< OR of (1u << nop_detect_type_t) tags on this segment */
    int      complete;     /**< 1 once the segment is closed */
} nop_record_segment_t;

/** Opaque recording engine. */
typedef struct nop_record nop_record_t;

/**
 * Create the engine from @p config (copied). Returns NULL on bad config
 * (NULL / empty storage_dir) or allocation failure. Recording begins once a
 * media source is attached and frames arrive (continuous) or an event triggers
 * a clip (event mode).
 */
nop_record_t *nop_record_create(const nop_record_config_t *config);

/** Destroy the engine: detach sources, close the open segment, free the index
 *  and pre-record buffer. NULL-safe. */
void nop_record_destroy(nop_record_t *recorder);

/**
 * Subscribe to @p media so its frames feed the recorder. Only frames matching
 * the configured channel/stream are recorded. @return NOP_OK, or an error if a
 * source is already attached / subscribe fails.
 */
nop_status_t nop_record_attach_media(nop_record_t *recorder, nop_media_t *media);

/**
 * Subscribe to detection @p hub: in event mode a detection on the configured
 * channel triggers/extends a clip; in continuous mode it tags the open segment.
 * @return NOP_OK, or an error if a hub is already attached / subscribe fails.
 */
nop_status_t nop_record_attach_event_hub(nop_record_t *recorder, nop_event_hub_t *hub);

/**
 * Copy up to @p max segments (newest first) into @p out; @return the count
 * written. The currently open segment is not reported until it closes.
 */
int nop_record_segments(nop_record_t *recorder, nop_record_segment_t *out, int max);

/** @return number of complete segments currently in the index (diagnostics). */
int nop_record_segment_count(const nop_record_t *recorder);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_RECORD_H */
