/**
 * @file nop_onvif_server.h
 * @brief Pure-C ABI for the device-side ONVIF server.
 *
 * Lets a NOP device expose itself as a standard ONVIF camera so any ONVIF
 * client/NVR can discover it (WS-Discovery) and drive the Device, Media,
 * Imaging and PTZ services + event pull-point. Mirrors the ONVIF *client*
 * binding (nop_onvif.h): the vendored Happytimesoft ONVIF server (full profile
 * surface) stays hidden behind a thin C++ adapter — callers see only this
 * header.
 *
 * The server is configuration-driven. Fill nop_onvif_server_config_t from the
 * SDK's own device identity (the same facts getDeviceInfo reports) plus one or
 * more media profiles and an optional PTZ node, then start; the adapter renders
 * the library's native cfg and brings the SOAP services up.
 *
 * Typical embed:
 *   nop_onvif_server_config_t cfg;
 *   nop_onvif_server_config_init(&cfg);   // 2 profiles (main+sub), no PTZ
 *   strcpy(cfg.manufacturer, "NightOwl");
 *   strcpy(cfg.model, "NOP-CAM-1");
 *   strcpy(cfg.profiles[0].stream_uri, "rtsp://192.168.1.10:554/live/main");
 *   strcpy(cfg.profiles[1].stream_uri, "rtsp://192.168.1.10:554/live/sub");
 *   cfg.has_ptz = 1;                       // advertise a PTZ node
 *   cfg.http_port = 8080;
 *   nop_onvif_server_start(&cfg);
 *   ...
 *   nop_onvif_server_stop();
 *
 * NOTE: this server speaks the ONVIF *control* plane only. The RTSP media each
 * profile's stream_uri points at is served elsewhere (the SDK media plane / an
 * external RTSP server); the vendored RTSP server is compiled out
 * (NO_RTSP_SERVER).
 */
#ifndef NOP_SDK_ONVIF_SERVER_H
#define NOP_SDK_ONVIF_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/** Max media profiles a server config can advertise. */
#define NOP_ONVIF_SERVER_MAX_PROFILES 4

/** Video encoding advertised for a media profile. */
typedef enum nop_onvif_server_encoding {
    NOP_ONVIF_SERVER_ENCODING_H264 = 0,
    NOP_ONVIF_SERVER_ENCODING_H265
} nop_onvif_server_encoding_t;

/** One media profile (≈ one encoder stream) advertised over ONVIF. */
typedef struct nop_onvif_server_profile {
    char name[32];        /**< profile name, e.g. "MainStream" */
    int  video_width;     /**< encoder width  */
    int  video_height;    /**< encoder height */
    int  framerate;       /**< frames/sec     */
    int  bitrate_kbps;    /**< bitrate limit  */
    nop_onvif_server_encoding_t encoding;
    char stream_uri[256]; /**< RTSP URI clients should pull (served elsewhere) */
} nop_onvif_server_profile_t;

/**
 * Device identity + media profiles + optional PTZ advertised over ONVIF.
 * Zero-initialize via nop_onvif_server_config_init(); reserved[] keeps the ABI
 * stable as fields are added.
 */
typedef struct nop_onvif_server_config {
    /* --- device information (DeviceInformation service) --- */
    char manufacturer[64];
    char model[64];
    char firmware_version[64];
    char serial_number[64];
    char hardware_id[64];

    /* --- HTTP service endpoint --- */
    char server_ip[64];   /**< bind/advertise IP; "" = auto-detect primary */
    int  http_port;       /**< ONVIF service port (default 8080) */

    /* --- access control --- */
    int  need_auth;       /**< 1: require WS-UsernameToken / HTTP digest */
    char username[32];    /**< administrator account */
    char password[32];

    /* --- media profiles --- */
    int  profile_count;   /**< number of valid entries in profiles[] (>=1) */
    nop_onvif_server_profile_t profiles[NOP_ONVIF_SERVER_MAX_PROFILES];

    /* --- PTZ --- */
    int  has_ptz;         /**< 1: advertise a PTZ node + configuration and bind
                               it to the first profile */

    /* --- discovery scopes --- */
    char scope_name[64];     /**< onvif scope name      (default = model) */
    char scope_location[64]; /**< onvif scope location  (default "") */

    void *reserved[8];
} nop_onvif_server_config_t;

/**
 * Fill @p config with library-compatible defaults: identity placeholders, port
 * 8080, auth on (admin/admin), two media profiles (main 1920x1080 + sub
 * 640x480), PTZ off. NULL-safe (no-op).
 */
void nop_onvif_server_config_init(nop_onvif_server_config_t *config);

/**
 * Overlay device identity from the registered HAL_SYSTEM interface onto
 * @p config: model, firmware_version, serial_number and hardware_id (← device
 * type) are taken from hal_system_if.get_device_info(), so the ONVIF server's
 * DeviceInformation reports exactly what getDeviceInfo reports. Fields the HAL
 * leaves blank are preserved. (Device IP and name are intentionally not mapped:
 * the server auto-detects a bindable IP, and a device name may contain spaces
 * that are invalid in a scope URI.) Call after nop_onvif_server_config_init()
 * and before nop_onvif_server_start().
 *
 * @return 0 on success; -1 if @p config is NULL; -2 if no HAL_SYSTEM is
 *         registered; -3 if get_device_info() failed.
 */
int nop_onvif_server_config_from_hal(nop_onvif_server_config_t *config);

/**
 * Bring the ONVIF server up from @p config: initialize the network/buffer
 * subsystems, render the native configuration, and start the SOAP services and
 * WS-Discovery responder. Idempotent guard: returns an error if already running.
 *
 * @return 0 on success, non-zero on failure (bad config / already running /
 *         socket bind failure — bind to a port < 1024 needs privilege).
 */
int nop_onvif_server_start(const nop_onvif_server_config_t *config);

/** @return non-zero if the server is currently running. */
int nop_onvif_server_is_running(void);

/** Stop the server and release its resources. Safe to call when stopped. */
void nop_onvif_server_stop(void);

/** @return vendored ONVIF server version string (e.g. "V11.3"). */
const char *nop_onvif_server_version(void);

/**
 * Attach a detection-event hub (nop_event.h, passed as an opaque void* to keep
 * this header free of the event dependency) as an ONVIF-events source. Each
 * event published to the hub is mapped to its ONVIF analytics topic and queued
 * to every subscribed PullPoint / Notify consumer, so an ONVIF client's
 * PullMessages / base-notification subscription receives SDK detections.
 *
 * The hub pointer is borrowed for the attachment's lifetime. Call
 * nop_onvif_server_detach_event_hub() before destroying the hub, or before
 * nop_onvif_server_stop(). Re-attaching replaces the previous hub.
 *
 * @return 0 on success, non-zero on failure (NULL hub / subscribe failure).
 */
int  nop_onvif_server_attach_event_hub(void *event_hub);

/** Detach the event hub attached by nop_onvif_server_attach_event_hub(). Safe
 *  to call when none is attached. */
void nop_onvif_server_detach_event_hub(void);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ONVIF_SERVER_H */
