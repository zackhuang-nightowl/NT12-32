/**
 * @file test_nvr_recorder.c
 * @brief NVR multi-channel recording manager: several channels record in
 *        parallel from one shared media hub, each engine filtering by channel,
 *        channels spread across two storage disks. Drives frames through the
 *        stub HAL_VIDEO mux.
 */
#include "nop_sdk/nop_nvr_recorder.h"
#include "nop_sdk/nop_media.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_storage.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DISK0 "/tmp/nop_nvr_disk0"
#define DISK1 "/tmp/nop_nvr_disk1"

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* Publish a keyframe on @p channel with timestamp @p ts. */
static void publish_key(nop_media_t *media, int channel, uint64_t ts)
{
    static const uint8_t payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0xAA };
    hal_video_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream       = 0;
    frame.codec        = HAL_VIDEO_CODEC_H264;
    frame.is_keyframe  = 1;
    frame.timestamp_ms = ts;
    frame.data         = payload;
    frame.length       = sizeof(payload);
    nop_media_publish(media, channel, &frame);
}

/* Controllable stub HAL_STORAGE: two READY volumes whose free space the test
 * sets, to drive capacity-aware placement. */
static long long g_free_a, g_free_b;
static int test_volume_count(void *ctx) { (void)ctx; return 2; }
static nop_status_t test_volume_info(void *ctx, int index, hal_storage_volume_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->status = HAL_STORAGE_STATUS_READY;
    if (index == 0) { strcpy(out->name, "diskA"); out->free_size_kilobytes = g_free_a; }
    else            { strcpy(out->name, "diskB"); out->free_size_kilobytes = g_free_b; }
    out->total_size_kilobytes = 1000000;
    return NOP_OK;
}
static const hal_storage_if g_test_storage = { test_volume_count, test_volume_info, NULL, NULL, NULL };

static int test_capacity_aware(nop_media_t *media)
{
    nop_nvr_recorder_config_t cfg;
    nop_nvr_recorder_t       *nvr;
    char                      root[256];

    hal_register(HAL_STORAGE, &g_test_storage);

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = NOP_RECORD_CONTINUOUS;
    cfg.capacity_aware_placement = 1;
    strcpy(cfg.storage_roots[0], DISK0); strcpy(cfg.storage_volume_names[0], "diskA");
    strcpy(cfg.storage_roots[1], DISK1); strcpy(cfg.storage_volume_names[1], "diskB");
    cfg.storage_root_count = 2;

    /* diskB has far more free space → every channel should land on DISK1. */
    g_free_a = 100; g_free_b = 900;
    nvr = nop_nvr_recorder_create(&cfg, media, NULL);
    if (!nvr) return fail("cap: create");
    if (nop_nvr_recorder_add_channel(nvr, 0) != NOP_OK) return fail("cap: add ch0");
    if (nop_nvr_recorder_add_channel(nvr, 1) != NOP_OK) return fail("cap: add ch1");
    nop_nvr_recorder_channel_storage(nvr, 0, root, sizeof(root));
    if (strcmp(root, DISK1) != 0) return fail("cap: ch0 should pick most-free DISK1");
    nop_nvr_recorder_channel_storage(nvr, 1, root, sizeof(root));
    if (strcmp(root, DISK1) != 0) return fail("cap: ch1 should pick most-free DISK1");
    nop_nvr_recorder_destroy(nvr);

    /* Flip: diskA now has more free → channels land on DISK0. */
    g_free_a = 5000; g_free_b = 50;
    nvr = nop_nvr_recorder_create(&cfg, media, NULL);
    if (!nvr) return fail("cap: create2");
    if (nop_nvr_recorder_add_channel(nvr, 0) != NOP_OK) return fail("cap: add ch0 (2)");
    nop_nvr_recorder_channel_storage(nvr, 0, root, sizeof(root));
    if (strcmp(root, DISK0) != 0) return fail("cap: ch0 should pick most-free DISK0");
    nop_nvr_recorder_destroy(nvr);
    return 0;
}

int main(void)
{
    nop_nvr_recorder_config_t cfg;
    nop_nvr_recorder_t       *nvr;
    nop_media_t              *media;
    nop_record_segment_t      segs[8];
    int                       n;

    hal_stub_register_all();
    mkdir(DISK0, 0777);
    mkdir(DISK1, 0777);
    media = nop_media_create();
    if (!media) return fail("media create");

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode            = NOP_RECORD_CONTINUOUS;
    cfg.segment_seconds = 2;                 /* rotate at 2000 ms */
    cfg.max_segments_per_channel = 4;
    strcpy(cfg.storage_roots[0], DISK0);
    strcpy(cfg.storage_roots[1], DISK1);
    cfg.storage_root_count = 2;

    nvr = nop_nvr_recorder_create(&cfg, media, NULL);
    if (!nvr) return fail("nvr create");

    /* Add three channels; they spread disk0/disk1/disk0. */
    if (nop_nvr_recorder_add_channel(nvr, 0) != NOP_OK) return fail("add ch0");
    if (nop_nvr_recorder_add_channel(nvr, 1) != NOP_OK) return fail("add ch1");
    if (nop_nvr_recorder_add_channel(nvr, 2) != NOP_OK) return fail("add ch2");
    if (nop_nvr_recorder_add_channel(nvr, 0) != NOP_ERR_CONFLICT) return fail("dup ch0 should conflict");
    if (nop_nvr_recorder_channel_count(nvr) != 3) return fail("expected 3 channels");

    /* Disk spread: ch0→disk0, ch1→disk1, ch2→disk0. */
    {
        char root[256];
        if (!nop_nvr_recorder_channel_storage(nvr, 0, root, sizeof(root)) || strcmp(root, DISK0) != 0) return fail("ch0 disk0");
        if (!nop_nvr_recorder_channel_storage(nvr, 1, root, sizeof(root)) || strcmp(root, DISK1) != 0) return fail("ch1 disk1");
        if (!nop_nvr_recorder_channel_storage(nvr, 2, root, sizeof(root)) || strcmp(root, DISK0) != 0) return fail("ch2 disk0");
        if (nop_nvr_recorder_channel_storage(nvr, 9, root, sizeof(root))) return fail("unmanaged channel should return 0");
    }

    /* Drive each channel independently: keyframes at 0/2000/4000 close two
     * segments per channel (rotation at each boundary). */
    {
        int ch;
        for (ch = 0; ch <= 2; ch++) {
            publish_key(media, ch, 0);
            publish_key(media, ch, 2000);
            publish_key(media, ch, 4000);
        }
    }

    /* Each channel should have 2 closed segments, tagged with its own channel. */
    n = nop_nvr_recorder_segments(nvr, 1, segs, 8);
    if (n != 2) { fprintf(stderr, "ch1 segs=%d\n", n); return fail("ch1 expected 2 segments"); }
    if (segs[0].channel != 1 || segs[1].channel != 1) return fail("ch1 segment channel mismatch");
    if (segs[0].start_ms != 2000) return fail("ch1 newest segment start");

    /* Removing a channel drops its engine; count decrements; query returns 0. */
    if (nop_nvr_recorder_remove_channel(nvr, 1) != NOP_OK) return fail("remove ch1");
    if (nop_nvr_recorder_channel_count(nvr) != 2) return fail("expected 2 after remove");
    if (nop_nvr_recorder_segments(nvr, 1, segs, 8) != 0) return fail("removed channel should have no segments");
    if (nop_nvr_recorder_remove_channel(nvr, 1) != NOP_ERR_NOTFOUND) return fail("double remove should be NOTFOUND");

    nop_nvr_recorder_destroy(nvr);

    /* Capacity-aware placement (uses its own recorder instances + stub storage). */
    {
        int rc = test_capacity_aware(media);
        if (rc) { nop_media_destroy(media); return rc; }
    }

    nop_media_destroy(media);

    printf("test_nvr_recorder: OK (round-robin + capacity-aware placement)\n");
    return 0;
}
