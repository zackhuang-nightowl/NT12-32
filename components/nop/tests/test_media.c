/**
 * @file test_media.c
 * @brief Media frame-source hub: direct publish fan-out to multiple sinks, and
 *        the HAL_VIDEO-bound path (stub emits a keyframe on start_stream).
 */
#include "nop_sdk/nop_media.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_video.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>

static int   g_sink_a_count, g_sink_b_count;
static size_t g_last_len_a;
static int   g_last_keyframe_a;

static void sink_a(void *ctx, int channel, const hal_video_frame_t *frame)
{
    (void)ctx; (void)channel;
    g_sink_a_count++;
    g_last_len_a      = frame->length;
    g_last_keyframe_a = frame->is_keyframe;
}
static void sink_b(void *ctx, int channel, const hal_video_frame_t *frame)
{
    (void)ctx; (void)channel; (void)frame;
    g_sink_b_count++;
}

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(void)
{
    nop_media_t              *media;
    nop_media_subscription_t *sub_a, *sub_b;
    hal_video_frame_t         frame;
    const unsigned char       payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const hal_video_if       *video;

    media = nop_media_create();
    if (!media)
        return fail("create");

    /* --- direct publish fans out to both sinks --- */
    sub_a = nop_media_subscribe(media, sink_a, NULL);
    sub_b = nop_media_subscribe(media, sink_b, NULL);
    if (!sub_a || !sub_b)
        return fail("subscribe");
    if (nop_media_subscriber_count(media) != 2)
        return fail("count != 2");

    memset(&frame, 0, sizeof(frame));
    frame.stream = 0; frame.codec = HAL_VIDEO_CODEC_H264; frame.is_keyframe = 1;
    frame.data = payload; frame.length = sizeof(payload);
    nop_media_publish(media, 0, &frame);
    if (g_sink_a_count != 1 || g_sink_b_count != 1)
        return fail("fan-out count");
    if (g_last_len_a != sizeof(payload) || !g_last_keyframe_a)
        return fail("frame contents");

    /* --- unsubscribe drops one sink --- */
    nop_media_unsubscribe(media, sub_b);
    nop_media_publish(media, 0, &frame);
    if (g_sink_a_count != 2 || g_sink_b_count != 1)
        return fail("unsubscribe");

    /* --- HAL-bound path: stub emits a keyframe on start_stream --- */
    hal_stub_register_all();
    if (nop_media_bind_hal_video(media) != NOP_OK)
        return fail("bind hal video");
    video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    if (!video || !video->start_stream)
        return fail("hal video missing");
    g_sink_a_count = 0;
    video->start_stream(video->ctx, 0, 0);      /* -> stub sink -> hub -> sink_a */
    if (g_sink_a_count != 1)
        return fail("hal frame not delivered");

    nop_media_unbind_hal_video(media);
    nop_media_unsubscribe(media, sub_a);
    nop_media_destroy(media);
    printf("test_media: OK\n");
    return 0;
}
