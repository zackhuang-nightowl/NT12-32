/**
 * @file nvr_demo.c
 * @brief videoRecorder (NVR) firmware integration skeleton on top of the NOP
 *        SDK. Shows the NVR-role assembly: NOP command app in NVR role (GUI_*
 *        local-UI commands enabled), the shared media plane + detection hub,
 *        and the multi-channel recording manager spreading channels across
 *        storage disks.
 *
 * An NVR records several cameras at once. Each channel's decoded/relayed frames
 * are published into the shared media hub (by whatever pulls the remote camera
 * stream — TUTK / RTSP client, the integrator's code); the recording manager
 * runs one per-channel engine, each filtering the hub by its channel. Camera
 * alarms arrive via the 8012 event-center *client* (one per attached camera)
 * and are republished into the detection hub to tag the recordings.
 *
 * Pure C, public API only. With no args it runs a short smoke sequence and
 * exits; used as the nvr_smoke ctest.
 *
 *   ./nvr_demo [frames_per_channel]
 */
#include "nop_sdk/nop_sdk.h"
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_media.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_nvr_recorder.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/nop_nvr_addcamera.h"
#include "hal_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NVR_DISK0 "/tmp/nop_nvr_demo_disk0"
#define NVR_DISK1 "/tmp/nop_nvr_demo_disk1"

/* Publish a keyframe for @p channel at @p ts on the shared media hub. */
static void publish_keyframe(nop_media_t *media, int channel, unsigned long long ts)
{
    static const uint8_t payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0xAB, 0xCD };
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

int main(int argc, char **argv)
{
    nop_app_config_t          app_config;
    nop_app_t                *app;
    nop_media_t              *media;
    nop_event_hub_t          *events;
    nop_nvr_recorder_config_t rec_config;
    nop_nvr_recorder_t       *nvr;
    int                       channel, i;
    int                       frames_per_channel = (argc > 1) ? atoi(argv[1]) : 3;

    if (frames_per_channel < 1)
        frames_per_channel = 1;

    /* Firmware registers its drivers (here: the print-stub HAL, which provides
     * HAL_VIDEO with the mux hooks the recorder needs). */
    hal_stub_register_all();
    mkdir(NVR_DISK0, 0777);
    mkdir(NVR_DISK1, 0777);

    /* NOP application in videoRecorder role: GUI_* local commands are routed. */
    memset(&app_config, 0, sizeof(app_config));
    app_config.role      = NOP_ROLE_NVR;
    app_config.auto_caps = 1;
    app = nop_app_create(&app_config);

    /* Shared media plane + detection hub. */
    media  = nop_media_create();
    events = nop_event_hub_create();

    /* Multi-channel recording across two disks. */
    memset(&rec_config, 0, sizeof(rec_config));
    rec_config.mode                     = NOP_RECORD_CONTINUOUS;
    rec_config.segment_seconds          = 2;
    rec_config.max_segments_per_channel = 8;
    strcpy(rec_config.storage_roots[0], NVR_DISK0);
    strcpy(rec_config.storage_roots[1], NVR_DISK1);
    rec_config.storage_root_count = 2;
    nvr = nop_nvr_recorder_create(&rec_config, media, events);

    for (channel = 0; channel < 4; channel++)
        nop_nvr_recorder_add_channel(nvr, channel);

    printf("NVR up: role=videoRecorder, %d channels recording across 2 disks.\n",
           nop_nvr_recorder_channel_count(nvr));

    /* Simulate each channel's relayed frames (the integrator's stream puller
     * would publish real frames here) — keyframes 2 s apart to roll segments. */
    for (i = 0; i < frames_per_channel; i++)
        for (channel = 0; channel < 4; channel++)
            publish_keyframe(media, channel, (unsigned long long)i * 2000ull);

    /* A camera alarm (normally from the 8012 client) tags channel 1's recording. */
    {
        nop_event_t event;
        memset(&event, 0, sizeof(event));
        event.channel      = 1;
        event.type         = NOP_DETECT_HUMAN;
        event.timestamp_ms = 500;
        nop_event_publish(events, &event);
    }

    for (channel = 0; channel < 4; channel++) {
        char                 root[256] = "";
        nop_record_segment_t segs[16];
        int                  count;
        nop_nvr_recorder_channel_storage(nvr, channel, root, sizeof(root));
        count = nop_nvr_recorder_segments(nvr, channel, segs, 16);
        printf("  ch%d: %d segment(s) on %s\n", channel, count, root);
    }

    /* AddWirelessCameras: attach the pairing engine + registry, then drive it
     * through the real NOP command path (startAddWirelessCameras →
     * getAddWirelessCamerasStatus) to show the wired loop. */
    {
        nop_nvr_channels_t  *registry = nop_nvr_channels_create(8);
        nop_nvr_addcamera_t *pairing  = nop_nvr_addcamera_create(registry);
        char                *resp = NULL;
        nop_app_set_nvr_addcamera(app, pairing);

        if (nop_app_dispatch(app, "{\"func\":\"startAddWirelessCameras\",\"args\":{}}", &resp) == NOP_OK)
            nop_app_free_response(resp);
        resp = NULL;
        if (nop_app_dispatch(app, "{\"func\":\"getAddWirelessCamerasStatus\",\"args\":{}}", &resp) == NOP_OK) {
            printf("  addWirelessCameras status: %s\n", resp ? resp : "(null)");
            nop_app_free_response(resp);
        }
        nop_app_set_nvr_addcamera(app, NULL);
        nop_nvr_addcamera_destroy(pairing);
        nop_nvr_channels_destroy(registry);
    }

    nop_nvr_recorder_destroy(nvr);
    nop_event_hub_destroy(events);
    nop_media_destroy(media);
    nop_app_destroy(app);
    printf("NVR demo done.\n");
    return 0;
}
