/**
 * @file test_record.c
 * @brief SD recording engine: continuous segment rotation + Overwrite reclaim,
 *        and event-triggered pre/post recording with event tagging. Drives the
 *        engine through the stub HAL_VIDEO mux hooks with hand-crafted frames.
 */
#include "nop_sdk/nop_record.h"
#include "nop_sdk/nop_media.h"
#include "nop_sdk/nop_event.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define REC_DIR "/tmp/nop_record_test"

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* Publish one encoded frame on channel 0. */
static void publish_frame(nop_media_t *media, int is_keyframe, uint64_t ts)
{
    static const uint8_t payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x11, 0x22, 0x33 };
    hal_video_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream       = 0;
    frame.codec        = HAL_VIDEO_CODEC_H264;
    frame.is_keyframe  = is_keyframe;
    frame.timestamp_ms = ts;
    frame.data         = payload;
    frame.length       = sizeof(payload);
    nop_media_publish(media, 0, &frame);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int test_continuous(nop_media_t *media)
{
    nop_record_config_t  cfg;
    nop_record_t        *rec;
    nop_record_segment_t segs[8];
    int                  count;

    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.storage_dir, REC_DIR);
    cfg.mode            = NOP_RECORD_CONTINUOUS;
    cfg.segment_seconds = 2;      /* rotate at 2000 ms boundaries */
    cfg.max_segments    = 3;      /* Overwrite: keep at most 3 */

    rec = nop_record_create(&cfg);
    if (!rec) return fail("continuous: create");
    if (nop_record_attach_media(rec, media) != NOP_OK) return fail("continuous: attach_media");

    /* Keyframes at 0/2000/4000/6000/8000 → open A, rotate to B,C,D,E.
     * Closes A(0),B(2000),C(4000),D(6000); cap=3 reclaims A on D's close. */
    publish_frame(media, 1, 0);
    publish_frame(media, 0, 500);
    publish_frame(media, 0, 1000);
    publish_frame(media, 1, 2000);
    publish_frame(media, 1, 4000);
    publish_frame(media, 1, 6000);
    publish_frame(media, 1, 8000);

    count = nop_record_segment_count(rec);
    if (count != 3) { fprintf(stderr, "count=%d\n", count); nop_record_destroy(rec); return fail("continuous: expected 3 segments"); }

    /* Oldest (start=0) reclaimed; B/C/D present. */
    if (file_exists(REC_DIR "/ch0_s0_0.mp4")) { nop_record_destroy(rec); return fail("continuous: oldest not reclaimed"); }
    if (!file_exists(REC_DIR "/ch0_s0_6000.mp4")) { nop_record_destroy(rec); return fail("continuous: newest segment file missing"); }

    /* Newest-first query. */
    count = nop_record_segments(rec, segs, 8);
    if (count != 3 || segs[0].start_ms != 6000) { nop_record_destroy(rec); return fail("continuous: query order"); }

    nop_record_destroy(rec);
    return 0;
}

static int test_event(nop_media_t *media, nop_event_hub_t *hub)
{
    nop_record_config_t  cfg;
    nop_record_t        *rec;
    nop_record_segment_t segs[4];
    nop_event_t          event;
    int                  count;

    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.storage_dir, REC_DIR);
    cfg.mode                = NOP_RECORD_EVENT;
    cfg.pre_record_seconds  = 1;
    cfg.post_record_seconds = 1;   /* close 1000 ms after last trigger */

    rec = nop_record_create(&cfg);
    if (!rec) return fail("event: create");
    if (nop_record_attach_media(rec, media) != NOP_OK) return fail("event: attach_media");
    if (nop_record_attach_event_hub(rec, hub) != NOP_OK) return fail("event: attach_hub");

    /* Pre-roll frames buffered (not yet recording). */
    publish_frame(media, 1, 100);
    publish_frame(media, 0, 200);
    publish_frame(media, 0, 300);

    /* Human detection triggers a clip; pre-roll flushed from keyframe @100.
     * The event carries a wall-clock epoch timestamp (a different clock from
     * the media/frame clock) — the engine must ignore it for timing and anchor
     * on frame timestamps, else the clip would truncate to a single frame. */
    memset(&event, 0, sizeof(event));
    event.channel      = 0;
    event.type         = NOP_DETECT_HUMAN;
    event.timestamp_ms = 1751000000000ULL;   /* realistic epoch ms */
    nop_event_publish(hub, &event);

    /* Live frames, then one past post-record window closes the clip. */
    publish_frame(media, 0, 400);
    publish_frame(media, 0, 500);
    publish_frame(media, 1, 1400);   /* 1400-350 = 1050 >= 1000 → close */

    count = nop_record_segment_count(rec);
    if (count != 1) { fprintf(stderr, "event count=%d\n", count); nop_record_destroy(rec); return fail("event: expected 1 clip"); }

    count = nop_record_segments(rec, segs, 4);
    if (count != 1) { nop_record_destroy(rec); return fail("event: query"); }
    if (segs[0].start_ms != 100) { nop_record_destroy(rec); return fail("event: clip should start at pre-roll keyframe"); }
    if (!(segs[0].event_mask & (1u << NOP_DETECT_HUMAN))) { nop_record_destroy(rec); return fail("event: missing Human tag"); }
    if (!file_exists(REC_DIR "/ch0_s0_100.mp4")) { nop_record_destroy(rec); return fail("event: clip file missing"); }

    nop_record_destroy(rec);
    return 0;
}

int main(void)
{
    nop_media_t     *media;
    nop_event_hub_t *hub;
    int              rc;

    mkdir(REC_DIR, 0777);
    hal_stub_register_all();     /* HAL_VIDEO with mux_open/write/close */

    media = nop_media_create();
    hub   = nop_event_hub_create();
    if (!media || !hub) return fail("setup");

    rc = test_continuous(media);
    if (rc == 0)
        rc = test_event(media, hub);

    nop_event_hub_destroy(hub);
    nop_media_destroy(media);

    if (rc == 0)
        printf("test_record: OK (continuous rotation+reclaim, event pre/post+tag)\n");
    return rc;
}
