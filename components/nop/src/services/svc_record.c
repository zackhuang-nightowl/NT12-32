/**
 * @file svc_record.c
 * @brief SD recording engine. Fans media frames into rotating container
 *        segments via the HAL_VIDEO mux hooks, keeps a segment index with
 *        Overwrite reclaim, and does event-triggered pre/post recording plus
 *        event tagging. See nop_sdk/nop_record.h.
 *
 * Threading: frames arrive on the encoder thread (media sink), detections on
 * the detection thread (event sink), queries on a control thread. All engine
 * state is guarded by one osal_mutex; the HAL mux calls run under it (they are
 * expected to be quick hand-offs to the firmware muxer).
 */
#include "nop_sdk/nop_record.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_video.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

/* Defaults applied when the corresponding config field is 0. */
#define NOP_RECORD_DEFAULT_SEGMENT_SECONDS 60
#define NOP_RECORD_DEFAULT_MAX_SEGMENTS    48
#define NOP_RECORD_DEFAULT_POST_SECONDS    30
#define NOP_RECORD_DEFAULT_EVENT_MAX_SECS  600   /* 10 min hard cap */
/* Pre-record ring sizing: assume this frame rate, cap the buffered frame count. */
#define NOP_RECORD_ASSUMED_FPS             30
#define NOP_RECORD_PRE_FRAMES_MAX          900

/* One buffered pre-record frame (owns a copy of the encoded bitstream). */
typedef struct nop_record_pre_frame {
    uint8_t          *data;
    size_t            length;
    int               is_keyframe;
    uint64_t          timestamp_ms;
} nop_record_pre_frame_t;

struct nop_record {
    nop_record_config_t       config;         /* copied; defaults resolved */
    osal_mutex_t             *mutex;
    const hal_video_if       *video;          /* cached HAL for the mux hooks */

    nop_media_t              *media;
    nop_media_subscription_t *media_subscription;
    nop_event_hub_t          *event_hub;
    nop_event_subscription_t *event_subscription;

    /* Currently open segment. */
    hal_video_mux_t           mux;
    int                       segment_open;
    char                      segment_path[256];
    uint64_t                  segment_start_ms;
    uint64_t                  segment_last_ms;
    uint32_t                  segment_event_mask;

    /* Most recent frame timestamp for this channel/stream — the single media
     * (presentation) clock all recording-duration timing is measured against.
     * Detection-event wall-clock timestamps are deliberately NOT used for
     * timing (different clock); they only tag segments. */
    uint64_t                  last_frame_ms;

    /* Event-mode recording state. */
    int                       event_recording;
    uint64_t                  event_last_trigger_ms;   /* anchored to last_frame_ms */
    int                       trigger_pending;          /* triggered, awaiting a frame to anchor */
    uint32_t                  pending_event_mask;       /* tags to apply once the clip opens */

    /* Segment index ring (newest = head-1). */
    nop_record_segment_t     *segments;
    int                       segment_capacity;
    int                       segment_count;
    int                       segment_head;

    /* Pre-record ring (event mode; empty when pre_record_seconds == 0). */
    nop_record_pre_frame_t   *pre_frames;
    int                       pre_capacity;
    int                       pre_count;
    int                       pre_head;
};

/* ---- segment index -------------------------------------------------------- */

/* Append a closed segment to the index ring; when full, delete the oldest
 * segment's file and reclaim its slot (Overwrite). */
static void record_index_add_locked(nop_record_t *recorder)
{
    nop_record_segment_t *slot;

    if (recorder->segment_count == recorder->segment_capacity) {
        /* Ring full: the slot at head is the oldest — reclaim its file. */
        remove(recorder->segments[recorder->segment_head].path);
    } else {
        recorder->segment_count++;
    }
    slot = &recorder->segments[recorder->segment_head];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->path, sizeof(slot->path), "%s", recorder->segment_path);
    slot->channel    = recorder->config.channel;
    slot->stream     = recorder->config.stream;
    slot->start_ms   = recorder->segment_start_ms;
    slot->end_ms     = recorder->segment_last_ms;
    slot->event_mask = recorder->segment_event_mask;
    slot->complete   = 1;
    recorder->segment_head = (recorder->segment_head + 1) % recorder->segment_capacity;
}

/* ---- open/write/close ----------------------------------------------------- */

static nop_status_t record_open_segment_locked(nop_record_t *recorder, uint64_t start_ms)
{
    hal_video_mux_t mux = NULL;
    int             written;

    if (!recorder->video || !recorder->video->mux_open)
        return NOP_ERR_NOTIMPL;

    written = snprintf(recorder->segment_path, sizeof(recorder->segment_path),
                       "%s/ch%d_s%d_%llu.mp4", recorder->config.storage_dir,
                       recorder->config.channel, recorder->config.stream,
                       (unsigned long long)start_ms);
    if (written < 0 || (size_t)written >= sizeof(recorder->segment_path))
        return NOP_ERR_PARAM;      /* storage_dir too long for the path buffer */

    if (recorder->video->mux_open(recorder->video->ctx, recorder->config.channel,
                                  recorder->config.stream, recorder->segment_path,
                                  &mux) != NOP_OK)
        return NOP_ERR_IO;

    recorder->mux                = mux;
    recorder->segment_open       = 1;
    recorder->segment_start_ms   = start_ms;
    recorder->segment_last_ms    = start_ms;
    recorder->segment_event_mask = 0;
    return NOP_OK;
}

static void record_write_frame_locked(nop_record_t *recorder,
                                      const hal_video_frame_t *frame)
{
    if (recorder->video && recorder->video->mux_write)
        recorder->video->mux_write(recorder->video->ctx, recorder->mux, frame);
    if (frame->timestamp_ms > recorder->segment_last_ms)
        recorder->segment_last_ms = frame->timestamp_ms;
}

static void record_close_segment_locked(nop_record_t *recorder)
{
    if (!recorder->segment_open)
        return;
    if (recorder->video && recorder->video->mux_close)
        recorder->video->mux_close(recorder->video->ctx, recorder->mux);
    record_index_add_locked(recorder);
    recorder->mux          = NULL;
    recorder->segment_open = 0;
}

/* ---- pre-record ring ------------------------------------------------------ */

static void record_pre_clear_locked(nop_record_t *recorder)
{
    int i;
    for (i = 0; i < recorder->pre_capacity; i++) {
        nop_free(recorder->pre_frames[i].data);
        recorder->pre_frames[i].data   = NULL;
        recorder->pre_frames[i].length = 0;
    }
    recorder->pre_count = 0;
    recorder->pre_head  = 0;
}

/* Buffer a copy of @p frame in the pre-record ring, evicting the oldest.
 * Allocates the copy before disturbing the ring, so an allocation failure just
 * drops the frame cleanly (no counted NULL hole, no accounting drift). */
static void record_pre_push_locked(nop_record_t *recorder, const hal_video_frame_t *frame)
{
    nop_record_pre_frame_t *slot;
    uint8_t                *copy;
    size_t                  length = frame->length;

    if (recorder->pre_capacity <= 0)
        return;
    copy = (uint8_t *)nop_malloc(length ? length : 1);
    if (!copy)
        return;                        /* drop this frame; ring unchanged */
    if (frame->data && length)
        memcpy(copy, frame->data, length);

    slot = &recorder->pre_frames[recorder->pre_head];
    if (recorder->pre_count == recorder->pre_capacity)
        nop_free(slot->data);          /* evict oldest (== head when full) */
    else
        recorder->pre_count++;
    slot->data         = copy;
    slot->length       = length;
    slot->is_keyframe  = frame->is_keyframe;
    slot->timestamp_ms = frame->timestamp_ms;
    recorder->pre_head = (recorder->pre_head + 1) % recorder->pre_capacity;
}

/* @return the timestamp of the oldest buffered keyframe, or @p fallback_ms if
 * the ring holds none. */
static uint64_t record_pre_earliest_keyframe_locked(nop_record_t *recorder,
                                                    uint64_t fallback_ms)
{
    int i, idx, oldest;
    if (recorder->pre_count <= 0)
        return fallback_ms;
    oldest = (recorder->pre_head - recorder->pre_count + recorder->pre_capacity)
             % recorder->pre_capacity;
    for (i = 0; i < recorder->pre_count; i++) {
        idx = (oldest + i) % recorder->pre_capacity;
        if (recorder->pre_frames[idx].is_keyframe && recorder->pre_frames[idx].data)
            return recorder->pre_frames[idx].timestamp_ms;
    }
    return fallback_ms;
}

/* @return non-zero if the pre-record ring currently holds a keyframe. */
static int record_pre_has_keyframe_locked(const nop_record_t *recorder)
{
    int i, idx, oldest;
    if (recorder->pre_count <= 0)
        return 0;
    oldest = (recorder->pre_head - recorder->pre_count + recorder->pre_capacity)
             % recorder->pre_capacity;
    for (i = 0; i < recorder->pre_count; i++) {
        idx = (oldest + i) % recorder->pre_capacity;
        if (recorder->pre_frames[idx].is_keyframe && recorder->pre_frames[idx].data)
            return 1;
    }
    return 0;
}

/* Flush buffered pre-record frames into the open segment, starting at the
 * oldest keyframe so the clip is decodable. Clears the ring afterward. */
static void record_pre_flush_locked(nop_record_t *recorder)
{
    int i, idx, oldest, started = 0;
    hal_video_frame_t frame;

    if (recorder->pre_count <= 0)
        return;
    oldest = (recorder->pre_head - recorder->pre_count + recorder->pre_capacity)
             % recorder->pre_capacity;
    for (i = 0; i < recorder->pre_count; i++) {
        nop_record_pre_frame_t *buffered;
        idx = (oldest + i) % recorder->pre_capacity;
        buffered = &recorder->pre_frames[idx];
        if (!buffered->data)
            continue;
        if (!started) {
            if (!buffered->is_keyframe)
                continue;              /* skip until the first keyframe */
            started = 1;
        }
        memset(&frame, 0, sizeof(frame));
        frame.stream       = recorder->config.stream;
        frame.is_keyframe  = buffered->is_keyframe;
        frame.timestamp_ms = buffered->timestamp_ms;
        frame.data         = buffered->data;
        frame.length       = buffered->length;
        record_write_frame_locked(recorder, &frame);
    }
    record_pre_clear_locked(recorder);
}

/* ---- frame + event sinks -------------------------------------------------- */

/* @return effective segment duration in ms. */
static uint64_t record_segment_ms(const nop_record_t *recorder)
{
    return (uint64_t)recorder->config.segment_seconds * 1000u;
}

/* Elapsed @p now - @p start on the media clock, clamped to 0 so a backward or
 * reordered timestamp never underflows the unsigned subtraction. */
static uint64_t record_elapsed_ms(uint64_t now, uint64_t start)
{
    return now >= start ? now - start : 0;
}

/* Detection-type bit for the segment event mask (guarded shift). */
static uint32_t record_event_mask_bit(nop_detect_type_t type)
{
    return (unsigned)type < 32u ? (1u << (unsigned)type) : 0u;
}

static void record_on_frame_locked(nop_record_t *recorder, const hal_video_frame_t *frame)
{
    recorder->last_frame_ms = frame->timestamp_ms;   /* the media-clock reference */

    if (recorder->config.mode == NOP_RECORD_CONTINUOUS) {
        /* Rotate on a keyframe boundary once the segment reaches its duration. */
        if (recorder->segment_open && frame->is_keyframe &&
            record_elapsed_ms(frame->timestamp_ms, recorder->segment_start_ms)
                >= record_segment_ms(recorder))
            record_close_segment_locked(recorder);
        if (!recorder->segment_open) {
            if (!frame->is_keyframe)
                return;                /* wait for a keyframe to start a segment */
            if (record_open_segment_locked(recorder, frame->timestamp_ms) != NOP_OK)
                return;
        }
        record_write_frame_locked(recorder, frame);
        return;
    }

    if (recorder->config.mode != NOP_RECORD_EVENT)
        return;

    /* A trigger arrived with no keyframe buffered yet: anchor the clip on the
     * first keyframe that follows (all timing is on the media clock). */
    if (recorder->trigger_pending && !recorder->event_recording) {
        if (!frame->is_keyframe) {
            record_pre_push_locked(recorder, frame);
            return;
        }
        if (record_open_segment_locked(recorder, frame->timestamp_ms) != NOP_OK)
            return;
        recorder->event_recording       = 1;
        recorder->trigger_pending        = 0;
        recorder->event_last_trigger_ms  = frame->timestamp_ms;
        recorder->segment_event_mask    |= recorder->pending_event_mask;
        recorder->pending_event_mask     = 0;
        record_write_frame_locked(recorder, frame);
        return;
    }

    if (recorder->event_recording) {
        uint64_t post_ms      = (uint64_t)recorder->config.post_record_seconds * 1000u;
        uint64_t event_max_ms = (uint64_t)recorder->config.event_max_seconds * 1000u;
        record_write_frame_locked(recorder, frame);
        if (record_elapsed_ms(frame->timestamp_ms, recorder->event_last_trigger_ms) >= post_ms ||
            record_elapsed_ms(frame->timestamp_ms, recorder->segment_start_ms) >= event_max_ms) {
            record_close_segment_locked(recorder);
            recorder->event_recording = 0;
        }
    } else {
        record_pre_push_locked(recorder, frame);
    }
}

static void record_media_sink(void *sink_ctx, int channel, const hal_video_frame_t *frame)
{
    nop_record_t *recorder = (nop_record_t *)sink_ctx;
    if (!recorder || !frame)
        return;
    if (channel != recorder->config.channel || frame->stream != recorder->config.stream)
        return;
    osal_mutex_lock(recorder->mutex);
    record_on_frame_locked(recorder, frame);
    osal_mutex_unlock(recorder->mutex);
}

static void record_event_sink(void *sink_ctx, const nop_event_t *event)
{
    nop_record_t *recorder = (nop_record_t *)sink_ctx;
    uint32_t      bit;

    if (!recorder || !event || event->channel != recorder->config.channel)
        return;
    bit = record_event_mask_bit(event->type);

    osal_mutex_lock(recorder->mutex);
    if (recorder->config.mode == NOP_RECORD_CONTINUOUS) {
        /* Tag the open segment with the detection type. */
        if (recorder->segment_open)
            recorder->segment_event_mask |= bit;
    } else if (recorder->config.mode == NOP_RECORD_EVENT) {
        if (recorder->event_recording) {
            /* Extend the post-record window from the current media time. */
            recorder->event_last_trigger_ms = recorder->last_frame_ms;
            recorder->segment_event_mask   |= bit;
        } else if (record_pre_has_keyframe_locked(recorder)) {
            /* Pre-roll available: open from its oldest keyframe and flush it.
             * Timing anchors to the media clock (last seen frame). */
            uint64_t start_ms = record_pre_earliest_keyframe_locked(recorder,
                                                                    recorder->last_frame_ms);
            if (record_open_segment_locked(recorder, start_ms) == NOP_OK) {
                recorder->event_recording       = 1;
                record_pre_flush_locked(recorder);
                recorder->event_last_trigger_ms = recorder->last_frame_ms;
                recorder->segment_event_mask   |= bit;
            }
        } else {
            /* No keyframe buffered yet — defer opening to the next keyframe
             * frame so the clip and its timing anchor on the media clock. */
            recorder->trigger_pending    = 1;
            recorder->pending_event_mask |= bit;
        }
    }
    osal_mutex_unlock(recorder->mutex);
}

/* ---- lifecycle + queries -------------------------------------------------- */

nop_record_t *nop_record_create(const nop_record_config_t *config)
{
    nop_record_t *recorder;

    if (!config || config->storage_dir[0] == '\0')
        return NULL;

    recorder = (nop_record_t *)nop_calloc(1, sizeof(*recorder));
    if (!recorder)
        return NULL;
    recorder->config = *config;

    /* Resolve defaults. */
    if (recorder->config.segment_seconds <= 0)
        recorder->config.segment_seconds = NOP_RECORD_DEFAULT_SEGMENT_SECONDS;
    if (recorder->config.max_segments <= 0)
        recorder->config.max_segments = NOP_RECORD_DEFAULT_MAX_SEGMENTS;
    if (recorder->config.post_record_seconds <= 0)
        recorder->config.post_record_seconds = NOP_RECORD_DEFAULT_POST_SECONDS;
    if (recorder->config.event_max_seconds <= 0 ||
        recorder->config.event_max_seconds > NOP_RECORD_DEFAULT_EVENT_MAX_SECS)
        recorder->config.event_max_seconds = NOP_RECORD_DEFAULT_EVENT_MAX_SECS;

    recorder->mutex = osal_mutex_create();
    if (!recorder->mutex) {
        nop_free(recorder);
        return NULL;
    }

    recorder->segment_capacity = recorder->config.max_segments;
    recorder->segments = (nop_record_segment_t *)nop_calloc(
        (size_t)recorder->segment_capacity, sizeof(nop_record_segment_t));
    if (!recorder->segments) {
        osal_mutex_destroy(recorder->mutex);
        nop_free(recorder);
        return NULL;
    }

    if (recorder->config.pre_record_seconds > 0) {
        int frames = recorder->config.pre_record_seconds * NOP_RECORD_ASSUMED_FPS;
        if (frames > NOP_RECORD_PRE_FRAMES_MAX)
            frames = NOP_RECORD_PRE_FRAMES_MAX;
        recorder->pre_capacity = frames;
        recorder->pre_frames = (nop_record_pre_frame_t *)nop_calloc(
            (size_t)frames, sizeof(nop_record_pre_frame_t));
        if (!recorder->pre_frames) {
            nop_free(recorder->segments);
            osal_mutex_destroy(recorder->mutex);
            nop_free(recorder);
            return NULL;
        }
    }

    recorder->video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    return recorder;
}

void nop_record_destroy(nop_record_t *recorder)
{
    if (!recorder)
        return;
    /* Detach sources first so no sink fires during teardown. */
    if (recorder->media && recorder->media_subscription)
        nop_media_unsubscribe(recorder->media, recorder->media_subscription);
    if (recorder->event_hub && recorder->event_subscription)
        nop_event_unsubscribe(recorder->event_hub, recorder->event_subscription);

    osal_mutex_lock(recorder->mutex);
    record_close_segment_locked(recorder);
    if (recorder->pre_frames)
        record_pre_clear_locked(recorder);
    osal_mutex_unlock(recorder->mutex);

    nop_free(recorder->pre_frames);
    nop_free(recorder->segments);
    osal_mutex_destroy(recorder->mutex);
    nop_free(recorder);
}

nop_status_t nop_record_attach_media(nop_record_t *recorder, nop_media_t *media)
{
    if (!recorder || !media)
        return NOP_ERR_PARAM;
    if (recorder->media)
        return NOP_ERR_STATE;
    recorder->media_subscription = nop_media_subscribe(media, record_media_sink, recorder);
    if (!recorder->media_subscription)
        return NOP_ERR_STATE;
    recorder->media = media;
    return NOP_OK;
}

nop_status_t nop_record_attach_event_hub(nop_record_t *recorder, nop_event_hub_t *hub)
{
    if (!recorder || !hub)
        return NOP_ERR_PARAM;
    if (recorder->event_hub)
        return NOP_ERR_STATE;
    recorder->event_subscription = nop_event_subscribe(hub, record_event_sink, recorder);
    if (!recorder->event_subscription)
        return NOP_ERR_STATE;
    recorder->event_hub = hub;
    return NOP_OK;
}

int nop_record_segments(nop_record_t *recorder, nop_record_segment_t *out, int max)
{
    int written = 0, i;
    if (!recorder || !out || max <= 0)
        return 0;
    osal_mutex_lock(recorder->mutex);
    for (i = 0; i < recorder->segment_count && written < max; i++) {
        int idx = (recorder->segment_head - 1 - i + recorder->segment_capacity)
                  % recorder->segment_capacity;
        out[written++] = recorder->segments[idx];
    }
    osal_mutex_unlock(recorder->mutex);
    return written;
}

int nop_record_segment_count(const nop_record_t *recorder)
{
    int count;
    if (!recorder)
        return 0;
    osal_mutex_lock(recorder->mutex);
    count = recorder->segment_count;
    osal_mutex_unlock(recorder->mutex);
    return count;
}
