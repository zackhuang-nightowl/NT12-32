#include "hal_stub.h"

#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"
#include "nop_sdk/hal/hal_video.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "nop_sdk/hal/hal_light.h"
#include "nop_sdk/hal/hal_storage.h"
#include "nop_sdk/hal/hal_record.h"
#include "nop_sdk/hal/hal_audio.h"
#include "nop_sdk/hal/hal_ota.h"
#include "nop_sdk/hal/hal_analog.h"
#include "nop_sdk/nop_log.h"

#include <stdio.h>
#include <string.h>

/* ---- system --------------------------------------------------------------- */
static nop_status_t stub_get_device_info(void *ctx, hal_device_info_t *out)
{
    (void)ctx;
    if (!out)
        return NOP_ERR_PARAM;
    memset(out, 0, sizeof(*out));
    strcpy(out->type, "standaloneIpCamera");
    strcpy(out->serial_number, "001122334455");
    strcpy(out->model, "NOP-STUB-CAM");
    strcpy(out->firmware_version, "NOP-STUB-CAM_20260627");
    strcpy(out->mac, "F0:76:1C:CE:42:11");
    strcpy(out->ip, "192.168.1.10");
    strcpy(out->name, "NOP Stub Camera");
    out->maximum_channel_count = 1;
    out->current_channel_count = 1;
    out->upgrade_fail = 0;
    return NOP_OK;
}

static nop_status_t stub_reboot(void *ctx)
{
    (void)ctx;
    NOP_LOGI("hal_stub: reboot requested (no-op)");
    return NOP_OK;
}

static nop_status_t stub_factory_reset(void *ctx)
{
    (void)ctx;
    NOP_LOGI("hal_stub: factory_reset requested (no-op)");
    return NOP_OK;
}

static hal_system_event_fn g_stub_system_event_sink = NULL;
static void               *g_stub_system_event_ctx  = NULL;
static nop_status_t stub_set_system_event_sink(void *ctx, hal_system_event_fn sink, void *sink_ctx)
{ (void)ctx; g_stub_system_event_sink = sink; g_stub_system_event_ctx = sink_ctx; return NOP_OK; }

void hal_stub_fire_system_event(hal_system_event_t event)
{
    if (g_stub_system_event_sink)
        g_stub_system_event_sink(g_stub_system_event_ctx, event);
}

static const hal_system_if g_stub_system = {
    stub_get_device_info, stub_reboot, stub_factory_reset,
    stub_set_system_event_sink, NULL
};

/* ---- video ---------------------------------------------------------------- */
static hal_video_frame_sink_fn g_stub_video_sink = NULL;
static void                   *g_stub_video_sink_ctx = NULL;

static int stub_channel_count(void *ctx) { (void)ctx; return 1; }
static nop_status_t stub_start_stream(void *ctx, int ch, int s)
{
    (void)ctx;
    NOP_LOGI("hal_stub: start_stream ch=%d stream=%d", ch, s);
    /* Emit one fake keyframe so the media plane has something to fan out. */
    if (g_stub_video_sink) {
        static const uint8_t fake_nal[] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E };
        hal_video_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.stream       = s;
        frame.codec        = HAL_VIDEO_CODEC_H264;
        frame.is_keyframe  = 1;
        frame.timestamp_ms = 0;
        frame.data         = fake_nal;
        frame.length       = sizeof(fake_nal);
        g_stub_video_sink(g_stub_video_sink_ctx, ch, &frame);
    }
    return NOP_OK;
}
static nop_status_t stub_stop_stream(void *ctx, int ch, int s)
{ (void)ctx; NOP_LOGI("hal_stub: stop_stream ch=%d stream=%d", ch, s); return NOP_OK; }
static nop_status_t stub_set_frame_sink(void *ctx, hal_video_frame_sink_fn sink, void *sink_ctx)
{ (void)ctx; g_stub_video_sink = sink; g_stub_video_sink_ctx = sink_ctx; return NOP_OK; }
static nop_status_t stub_set_encoder_config(void *ctx, int ch, int s,
                                            const hal_video_encoder_config_t *cfg)
{ (void)ctx; NOP_LOGI("hal_stub: set_encoder ch=%d stream=%d %dx%d @%dfps %dkbps", ch, s,
                      cfg->width, cfg->height, cfg->framerate, cfg->bitrate_kbps); return NOP_OK; }
static nop_status_t stub_set_osd(void *ctx, int ch, const hal_video_osd_t *osd)
{ (void)ctx; NOP_LOGI("hal_stub: set_osd ch=%d name='%s'", ch, osd->channel_name); return NOP_OK; }
static nop_status_t stub_capture_snapshot(void *ctx, int ch, int s,
                                          uint8_t *buffer, size_t capacity, size_t *out_len)
{
    /* Minimal JPEG SOI/EOI so callers get a valid (tiny) image. */
    static const uint8_t jpeg[] = { 0xFF, 0xD8, 0xFF, 0xD9 };
    (void)ctx; (void)ch; (void)s;
    if (capacity < sizeof(jpeg))
        return NOP_ERR_PARAM;
    memcpy(buffer, jpeg, sizeof(jpeg));
    if (out_len)
        *out_len = sizeof(jpeg);
    return NOP_OK;
}
static nop_status_t stub_set_day_night(void *ctx, int ch, hal_video_day_night_t mode)
{ (void)ctx; NOP_LOGI("hal_stub: day_night ch=%d mode=%d", ch, (int)mode); return NOP_OK; }

/* Stub muxer: write the raw frame bytes to a real file so tests can stat the
 * produced segment. The handle is the open FILE*. */
static nop_status_t stub_mux_open(void *ctx, int ch, int s, const char *path,
                                  hal_video_mux_t *out_mux)
{
    FILE *file;
    (void)ctx; (void)ch; (void)s;
    if (!path || !out_mux)
        return NOP_ERR_PARAM;
    file = fopen(path, "wb");
    if (!file)
        return NOP_ERR_IO;
    *out_mux = (hal_video_mux_t)file;
    NOP_LOGI("hal_stub: mux_open ch=%d stream=%d path=%s", ch, s, path);
    return NOP_OK;
}
static nop_status_t stub_mux_write(void *ctx, hal_video_mux_t mux,
                                   const hal_video_frame_t *frame)
{
    (void)ctx;
    if (!mux || !frame || !frame->data)
        return NOP_ERR_PARAM;
    fwrite(frame->data, 1, frame->length, (FILE *)mux);
    return NOP_OK;
}
static nop_status_t stub_mux_close(void *ctx, hal_video_mux_t mux)
{
    (void)ctx;
    if (!mux)
        return NOP_ERR_PARAM;
    fclose((FILE *)mux);
    return NOP_OK;
}

static const hal_video_if g_stub_video = {
    stub_channel_count, stub_start_stream, stub_stop_stream, stub_set_frame_sink,
    stub_set_encoder_config, stub_set_osd, stub_capture_snapshot, stub_set_day_night,
    stub_mux_open, stub_mux_write, stub_mux_close, NULL
};

/* ---- ptz ------------------------------------------------------------------ */
static nop_status_t stub_ptz_move(void *ctx, int ch, hal_ptz_dir_t d, int sp)
{ (void)ctx; NOP_LOGI("hal_stub: ptz move ch=%d dir=%d speed=%d", ch, (int)d, sp); return NOP_OK; }
static nop_status_t stub_ptz_preset(void *ctx, int ch, int p)
{ (void)ctx; NOP_LOGI("hal_stub: ptz preset ch=%d preset=%d", ch, p); return NOP_OK; }
static nop_status_t stub_ptz_move_by_step(void *ctx, int ch, hal_ptz_dir_t d, int step)
{ (void)ctx; NOP_LOGI("hal_stub: ptz step ch=%d dir=%d step=%d", ch, (int)d, step); return NOP_OK; }
static nop_status_t stub_ptz_stop(void *ctx, int ch)
{ (void)ctx; NOP_LOGI("hal_stub: ptz stop ch=%d", ch); return NOP_OK; }
static nop_status_t stub_ptz_focus_by_step(void *ctx, int ch, int focus_in, int step)
{ (void)ctx; NOP_LOGI("hal_stub: ptz focus ch=%d in=%d step=%d", ch, focus_in, step); return NOP_OK; }
static nop_status_t stub_ptz_focus_stop(void *ctx, int ch)
{ (void)ctx; NOP_LOGI("hal_stub: ptz focus stop ch=%d", ch); return NOP_OK; }
static nop_status_t stub_ptz_set_preset(void *ctx, int ch, int p)
{ (void)ctx; NOP_LOGI("hal_stub: ptz set_preset ch=%d preset=%d", ch, p); return NOP_OK; }
static nop_status_t stub_ptz_remove_preset(void *ctx, int ch, int p)
{ (void)ctx; NOP_LOGI("hal_stub: ptz remove_preset ch=%d preset=%d", ch, p); return NOP_OK; }
static nop_status_t stub_ptz_set_home(void *ctx, int ch)
{ (void)ctx; NOP_LOGI("hal_stub: ptz set_home ch=%d", ch); return NOP_OK; }
static nop_status_t stub_ptz_goto_home(void *ctx, int ch)
{ (void)ctx; NOP_LOGI("hal_stub: ptz goto_home ch=%d", ch); return NOP_OK; }

static const hal_ptz_if g_stub_ptz = {
    stub_ptz_move, stub_ptz_preset, stub_ptz_move_by_step,
    stub_ptz_stop, stub_ptz_focus_by_step, stub_ptz_focus_stop,
    stub_ptz_set_preset, stub_ptz_remove_preset, stub_ptz_set_home, stub_ptz_goto_home, NULL
};

/* ---- light ---------------------------------------------------------------- */
static nop_status_t stub_light_switch(void *ctx, int ch, int on)
{ (void)ctx; NOP_LOGI("hal_stub: light ch=%d on=%d", ch, on); return NOP_OK; }
static nop_status_t stub_light_brightness(void *ctx, int ch, int lvl)
{ (void)ctx; NOP_LOGI("hal_stub: light ch=%d brightness=%d", ch, lvl); return NOP_OK; }

static const hal_light_if g_stub_light = {
    stub_light_switch, stub_light_brightness, NULL
};

/* ---- storage -------------------------------------------------------------- */
static int stub_storage_count(void *ctx) { (void)ctx; return 1; }
static nop_status_t stub_storage_info(void *ctx, int index, hal_storage_volume_t *out)
{
    (void)ctx;
    if (index != 0 || !out)
        return NOP_ERR_PARAM;
    memset(out, 0, sizeof(*out));
    strcpy(out->name, "sdcard");
    out->total_size_kilobytes = 64ll * 1024 * 1024;  /* 64 GiB */
    out->free_size_kilobytes  = 60ll * 1024 * 1024;
    out->status = HAL_STORAGE_STATUS_READY;
    return NOP_OK;
}
static nop_status_t stub_storage_format(void *ctx, const char *name)
{ (void)ctx; NOP_LOGI("hal_stub: format storage '%s'", name ? name : ""); return NOP_OK; }

static const hal_storage_if g_stub_storage = {
    stub_storage_count, stub_storage_info, NULL, stub_storage_format, NULL
};

/* ---- record --------------------------------------------------------------- */
static int g_stub_recording_enabled = 1;
static nop_status_t stub_record_get(void *ctx, int ch, int *enabled_out)
{ (void)ctx; (void)ch; if (enabled_out) *enabled_out = g_stub_recording_enabled; return NOP_OK; }
static nop_status_t stub_record_set(void *ctx, int ch, int enabled)
{ (void)ctx; NOP_LOGI("hal_stub: recording ch=%d enabled=%d", ch, enabled);
  g_stub_recording_enabled = enabled ? 1 : 0; return NOP_OK; }
static nop_status_t stub_record_get_schedule(void *ctx, int ch, hal_record_schedule_t *out)
{
    (void)ctx; (void)ch;
    if (!out)
        return NOP_ERR_PARAM;
    memset(out, 0, sizeof(*out));
    out->rule_count = 1;
    strcpy(out->rules[0].id, "rule-all-day");
    out->rules[0].weekday_mask = 0x7F;            /* Mon..Sun */
    strcpy(out->rules[0].start_time, "000000");
    strcpy(out->rules[0].end_time,   "235959");
    return NOP_OK;
}

static const hal_record_if g_stub_record = {
    stub_record_get, stub_record_set, stub_record_get_schedule, NULL, NULL
};

/* ---- audio ---------------------------------------------------------------- */
static nop_status_t stub_audio_start(void *ctx, int ch, const hal_audio_format_t *fmt)
{ (void)ctx; (void)fmt; NOP_LOGI("hal_stub: start_speaker ch=%d", ch); return NOP_OK; }
static nop_status_t stub_audio_stop(void *ctx, int ch)
{ (void)ctx; NOP_LOGI("hal_stub: stop_speaker ch=%d", ch); return NOP_OK; }
static nop_status_t stub_audio_play_alert(void *ctx, int ch, int tone_id, int seconds)
{ (void)ctx; NOP_LOGI("hal_stub: play_alert ch=%d tone=%d secs=%d", ch, tone_id, seconds); return NOP_OK; }

static const hal_audio_if g_stub_audio = {
    stub_audio_start, stub_audio_stop, NULL, NULL, NULL, stub_audio_play_alert, NULL
};

/* ---- ota ------------------------------------------------------------------ */
static nop_status_t stub_ota_begin(void *ctx, const char *url, int automatic)
{ (void)ctx; NOP_LOGI("hal_stub: begin_upgrade url='%s' auto=%d", url ? url : "(self)", automatic);
  return NOP_OK; }
static nop_status_t stub_ota_status(void *ctx, hal_ota_status_t *status_out, int *error_out)
{ (void)ctx; if (status_out) *status_out = HAL_OTA_STATUS_IDLE; if (error_out) *error_out = 0;
  return NOP_OK; }
static nop_status_t stub_ota_apply_sdcard(void *ctx, const char *path)
{
    FILE *file;
    (void)ctx;
    if (!path)
        return NOP_ERR_PARAM;
    /* Accept the image only if it exists and is non-empty (stand-in for the
     * real validate+flash). */
    file = fopen(path, "rb");
    if (!file)
        return NOP_ERR_IO;
    fclose(file);
    NOP_LOGI("hal_stub: apply_sdcard_image path=%s (accepted)", path);
    return NOP_OK;
}

static const hal_ota_if g_stub_ota = {
    stub_ota_begin, stub_ota_status, NULL, stub_ota_apply_sdcard, NULL
};

/* ---- analog (coaxial) camera --------------------------------------------- */
static int stub_analog_presence(void *ctx, int channel)
{ (void)ctx; (void)channel; return 0; }   /* no analog camera in the stub */
static nop_status_t stub_analog_set_framerate(void *ctx, int channel, int framerate)
{ (void)ctx; NOP_LOGI("hal_stub: analog set_framerate ch=%d fps=%d", channel, framerate); return NOP_OK; }
static nop_status_t stub_analog_utc_send(void *ctx, int channel, hal_analog_utc_t command)
{ (void)ctx; NOP_LOGI("hal_stub: analog utc ch=%d cmd=%d", channel, (int)command); return NOP_OK; }

static const hal_analog_if g_stub_analog = {
    stub_analog_presence, stub_analog_set_framerate, stub_analog_utc_send, NULL
};

void hal_stub_register_all(void)
{
    hal_register(HAL_SYSTEM,  &g_stub_system);
    hal_register(HAL_VIDEO,   &g_stub_video);
    hal_register(HAL_PTZ,     &g_stub_ptz);
    hal_register(HAL_LIGHT,   &g_stub_light);
    hal_register(HAL_STORAGE, &g_stub_storage);
    hal_register(HAL_RECORD,  &g_stub_record);
    hal_register(HAL_AUDIO,   &g_stub_audio);
    hal_register(HAL_OTA,     &g_stub_ota);
    hal_register(HAL_ANALOG,  &g_stub_analog);
}
