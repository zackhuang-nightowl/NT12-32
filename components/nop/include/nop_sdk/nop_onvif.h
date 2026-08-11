/**
 * @file nop_onvif.h
 * @brief Public C ABI for the built-in ONVIF client.
 *
 * This is the ONLY surface the rest of the SDK uses to reach ONVIF. The vendored
 * Happytimesoft ONVIF client library (third_party/onvif/, unmodified) is hidden
 * entirely behind this header — no NOP code includes ONVIF headers directly.
 *
 * Covers the full client workflow:
 *   runtime → discovery → device handle (connect/auth) →
 *   device service (info / capabilities / date-time / reboot / reset) →
 *   media (profiles / stream & snapshot URIs / snapshot) →
 *   PTZ (move / stop / presets / home) → imaging (get/set) →
 *   events (pull-point) → firmware/system maintenance.
 *
 * Available only when the SDK is built with NOP_WITH_ONVIF=ON.
 * All int-returning calls use 0 for success and a negative value for failure.
 */
#ifndef NOP_SDK_ONVIF_H
#define NOP_SDK_ONVIF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* Runtime                                                                  */
/* ======================================================================== */

/** Initialize the ONVIF client runtime (network, buffers). Idempotent. 0=ok. */
int  nop_onvif_global_init(void);
/** Release ONVIF client runtime resources. NULL-safe / idempotent. */
void nop_onvif_global_cleanup(void);

/* ======================================================================== */
/* Discovery (WS-Discovery)                                                 */
/* ======================================================================== */

/** One device found by discovery. */
typedef struct nop_onvif_device_info {
    char endpoint_reference[100]; /**< urn:uuid:... unique device id  */
    char host[128];               /**< device service IP             */
    int  port;                    /**< device service port           */
    char service_url[128];        /**< e.g. /onvif/device_service     */
    char scopes[512];             /**< space-joined onvif scope items */
} nop_onvif_device_info_t;

typedef void (*nop_onvif_discovery_cb)(const nop_onvif_device_info_t *device, void *user);

/**
 * Run WS-Discovery for @p seconds, invoking @p callback per device.
 * @return number of devices discovered (>=0), or negative on error.
 */
int  nop_onvif_discover(const char *local_ip, int seconds,
                        nop_onvif_discovery_cb callback, void *user);

/** Continuously monitor Hello/Bye announcements until monitor_stop(). 0=ok. */
int  nop_onvif_monitor_start(nop_onvif_discovery_cb callback, void *user);
void nop_onvif_monitor_stop(void);

/* ======================================================================== */
/* Device handle (connection + credentials)                                 */
/* ======================================================================== */

/** Opaque connected-device handle. */
typedef struct nop_onvif_device nop_onvif_device_t;

/** Create a device handle for host:port + service path. @p https: 0/1. */
nop_onvif_device_t *nop_onvif_device_create(const char *host, int port,
                                            const char *service_url, int https);
/** Set credentials (WS-UsernameToken). */
void  nop_onvif_device_set_auth(nop_onvif_device_t *device,
                                const char *username, const char *password);
/** Set per-request timeout in milliseconds (default 5000). */
void  nop_onvif_device_set_timeout(nop_onvif_device_t *device, int timeout_ms);
/** Destroy the handle and free all fetched data. NULL-safe. */
void  nop_onvif_device_destroy(nop_onvif_device_t *device);
/** @return last ONVIF error string for the device, or "" . */
const char *nop_onvif_device_last_error(nop_onvif_device_t *device);

/* ======================================================================== */
/* Device service (tds)                                                      */
/* ======================================================================== */

typedef struct nop_onvif_device_information {
    char manufacturer[64];
    char model[64];
    char firmware_version[64];
    char serial_number[64];
    char hardware_id[64];
} nop_onvif_device_information_t;

int nop_onvif_get_device_information(nop_onvif_device_t *device,
                                     nop_onvif_device_information_t *out);
/** Fetch capability/service sets into the handle (validates connectivity). */
int nop_onvif_get_capabilities(nop_onvif_device_t *device);
int nop_onvif_get_services(nop_onvif_device_t *device);
/** Last SOAP call's low-level error code (ONVIF_ERR_*: -1 conn, -4 rx timeout, -6 no content, -7 parse). */
int nop_onvif_last_error(nop_onvif_device_t *device);

typedef struct nop_onvif_datetime {
    int year, month, day, hour, minute, second;
} nop_onvif_datetime_t;

int nop_onvif_get_system_datetime(nop_onvif_device_t *device, nop_onvif_datetime_t *out);
/** Set the device clock to the host's current UTC time. */
int nop_onvif_set_system_datetime_now(nop_onvif_device_t *device);

int nop_onvif_reboot(nop_onvif_device_t *device);
/** @p hard: non-zero = factory hard reset, 0 = soft. */
int nop_onvif_factory_reset(nop_onvif_device_t *device, int hard);

/* ======================================================================== */
/* Media (trt)                                                              */
/* ======================================================================== */

/** RTSP transport protocols, matching onvif_TransportProtocol. */
typedef enum nop_onvif_transport {
    NOP_ONVIF_TRANSPORT_UDP  = 0,
    NOP_ONVIF_TRANSPORT_TCP  = 1,
    NOP_ONVIF_TRANSPORT_RTSP = 2,
    NOP_ONVIF_TRANSPORT_HTTP = 3
} nop_onvif_transport_t;

typedef struct nop_onvif_profile {
    char token[100];
    char name[64];
    char stream_uri[300];   /**< populated if stream URIs were fetched */
    char source_token[100]; /**< VideoSourceConfiguration.SourceToken = the physical
                                 video source this profile belongs to (多源区分用;空=未知) */
} nop_onvif_profile_t;

/** Fetch the media profiles into the handle. @return profile count (>=0). */
int nop_onvif_get_profiles(nop_onvif_device_t *device);
/** Read a cached profile by index (call get_profiles first). */
int nop_onvif_get_profile(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out);

/** Resolve the RTSP stream URI for a profile. */
int nop_onvif_get_stream_uri(nop_onvif_device_t *device, const char *profile_token,
                             nop_onvif_transport_t proto, char *out_uri, size_t out_size);

/* ---- Media2 (ver20 / Profile T)：同上，但走 tr2_* SOAP。部分相机(Profile T)
 *      在 media1(trt) 返回空 profiles，需用这组。结果写入独立的 media_profiles 表。 ---- */
int nop_onvif_get_profiles2(nop_onvif_device_t *device);
int nop_onvif_get_profile2(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out);
int nop_onvif_get_stream_uri2(nop_onvif_device_t *device, const char *profile_token,
                              char *out_uri, size_t out_size);
/** Resolve the JPEG snapshot URI for a profile. */
int nop_onvif_get_snapshot_uri(nop_onvif_device_t *device, const char *profile_token,
                               char *out_uri, size_t out_size);
/** Fetch a JPEG snapshot. Free the buffer with nop_onvif_free_buffer(). */
int nop_onvif_get_snapshot(nop_onvif_device_t *device, const char *profile_token,
                           unsigned char **out_buf, int *out_len);
void nop_onvif_free_buffer(void *buf);

/** @return number of video sources reported by the device (>=0). */
int nop_onvif_get_video_source_count(nop_onvif_device_t *device);

/* ======================================================================== */
/* PTZ (ptz)                                                                */
/* ======================================================================== */

/** @return number of PTZ nodes (0 => device has no PTZ). */
int nop_onvif_ptz_get_node_count(nop_onvif_device_t *device);

/** Velocities/positions are normalized in [-1.0, 1.0]; zoom in [0,1] or [-1,1]. */
int nop_onvif_ptz_continuous_move(nop_onvif_device_t *device, const char *profile_token,
                                  float pan, float tilt, float zoom);
int nop_onvif_ptz_stop(nop_onvif_device_t *device, const char *profile_token,
                       int stop_pan_tilt, int stop_zoom);
int nop_onvif_ptz_relative_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom);
int nop_onvif_ptz_absolute_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom);
int nop_onvif_ptz_goto_home(nop_onvif_device_t *device, const char *profile_token);
/* GotoHomePosition with a requested speed (0..1). @p speed<=0 uses the device
 * default (no Speed sent). Maps NOP gotoPtzHome.speed (1..10 -> 0.1..1.0). */
int nop_onvif_ptz_goto_home_speed(nop_onvif_device_t *device,
                                  const char *profile_token, float speed);
int nop_onvif_ptz_goto_preset(nop_onvif_device_t *device, const char *profile_token,
                              const char *preset_token);
int nop_onvif_ptz_set_preset(nop_onvif_device_t *device, const char *profile_token,
                             const char *preset_name);
int nop_onvif_ptz_remove_preset(nop_onvif_device_t *device, const char *profile_token,
                                const char *preset_token);

typedef struct nop_onvif_preset {
    char token[100];
    char name[64];
} nop_onvif_preset_t;

/** List presets into @p out_array (capped at @p max). @return count (>=0). */
int nop_onvif_ptz_get_presets(nop_onvif_device_t *device, const char *profile_token,
                              nop_onvif_preset_t *out_array, int max);

/* ======================================================================== */
/* Imaging (img)                                                            */
/* ======================================================================== */

typedef struct nop_onvif_imaging {
    int   has_brightness;       float brightness;
    int   has_contrast;         float contrast;
    int   has_color_saturation; float color_saturation;
    int   has_sharpness;        float sharpness;
} nop_onvif_imaging_t;

int nop_onvif_get_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   nop_onvif_imaging_t *out);
int nop_onvif_set_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   const nop_onvif_imaging_t *in);

/* ======================================================================== */
/* Events (tev — pull-point)                                                */
/* ======================================================================== */

/** Create a pull-point subscription on the device. 0=ok. */
int nop_onvif_events_create_pullpoint(nop_onvif_device_t *device);
/** Pull pending notifications, blocking up to @p timeout_s. @return msg count. */
int nop_onvif_events_pull(nop_onvif_device_t *device, int timeout_s, int max_messages);
/** Tear down the subscription. */
int nop_onvif_events_unsubscribe(nop_onvif_device_t *device);

/* ======================================================================== */
/* Firmware / system maintenance                                            */
/* ======================================================================== */

int nop_onvif_firmware_upgrade(nop_onvif_device_t *device, const char *filename);
int nop_onvif_system_backup(nop_onvif_device_t *device, const char *filename);
int nop_onvif_system_restore(nop_onvif_device_t *device, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ONVIF_H */
