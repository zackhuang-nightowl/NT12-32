/**
 * @file nop_nvr_channels.h
 * @brief videoRecorder-role camera channel registry — the table of sub-cameras
 *        an NVR has bound (added). Each entry holds how to reach one camera
 *        (host/port/credentials + main/sub stream URIs) and its identity.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR). Per the AddCamera flow, an NVR discovers
 * cameras over ONVIF WS-Discovery, probes them (GetDeviceInformation /
 * GetStreamUri) via the ONVIF client, then binds each as a channel. This
 * registry is the persistence-agnostic spine that holds those bindings: the
 * onboarding code fills entries, the stream puller reads stream_uri_* to relay
 * frames into the media hub, and the recording manager records the channels.
 * The SDK keeps the table in memory; firmware persists it (e.g. via device
 * config) and repopulates on boot.
 */
#ifndef NOP_SDK_NVR_CHANNELS_H
#define NOP_SDK_NVR_CHANNELS_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How a camera was added. */
typedef enum nop_camera_source {
    NOP_CAMERA_SOURCE_MANUAL = 0,   /**< entered by IP/URL */
    NOP_CAMERA_SOURCE_ONVIF,        /**< found via ONVIF WS-Discovery */
    NOP_CAMERA_SOURCE_POE,          /**< plug-and-play on a PoE port */
    NOP_CAMERA_SOURCE_ANALOG        /**< coaxial camera on a physical DVR channel */
} nop_camera_source_t;

/**
 * Control protocol used to DRIVE a bound camera. Distinct from how it was
 * discovered (nop_camera_source_t): a camera found over ONVIF discovery may
 * still be a NightOwl-native device driven by NOP, and vice-versa. The
 * add-camera flow stamps this once (scope/digest inspection); every per-channel
 * command reads it to decide whether to translate to ONVIF or handle natively.
 */
typedef enum nop_camera_backend {
    NOP_BACKEND_NOP = 0,   /**< NightOwl private protocol (native handling) */
    NOP_BACKEND_ONVIF      /**< standard ONVIF client control (mapping layer) */
} nop_camera_backend_t;

/** One bound camera. Zero-initialize, then set at least host. */
typedef struct nop_nvr_channel_entry {
    int                  channel;            /**< assigned channel index (output of add) */
    int                  enabled;            /**< 1 = should be streamed/recorded */
    nop_camera_source_t  source;
    nop_camera_backend_t backend;            /**< control protocol (NOP vs ONVIF) */
    char                host[64];            /**< camera IP / host (required) */
    int                 port;                /**< control port (0 => device default) */
    char                username[32];
    char                password[64];
    char                stream_uri_main[256];/**< RTSP main-stream URI (from GetStreamUri) */
    char                stream_uri_sub[256]; /**< RTSP sub-stream URI */
    char                name[48];            /**< display name */
    char                mac[20];             /**< from the ONVIF ProbeMatch scope */
    char                model[48];
    /* Multi-video-source (§10): the ONVIF VideoSourceToken this channel is bound
     * to on its (shared) device. A physical device with N video sources is added
     * as N channels sharing host/port, each pinned to one source token. Empty =
     * first/only source (single-source cameras and NOP backends). All per-source
     * ONVIF tokens (VSC / analytics / profile) are resolved from this. */
    char                video_source_token[100];
    char                service_url[128];    /**< device_service path from discovery */
} nop_nvr_channel_entry_t;

/** Opaque registry. */
typedef struct nop_nvr_channels nop_nvr_channels_t;

/** Create a registry holding up to @p max_channels entries. Returns NULL on bad
 *  argument / allocation failure. */
nop_nvr_channels_t *nop_nvr_channels_create(int max_channels);

/** Destroy the registry. NULL-safe. */
void nop_nvr_channels_destroy(nop_nvr_channels_t *channels);

/**
 * Bind a camera. If @p entry->channel is >= 0 and free it is used; otherwise
 * the lowest free channel index is assigned. A camera already present with the
 * same host+port is not duplicated (its existing channel is returned).
 * @return the assigned channel index (>= 0), or -1 if the table is full or the
 * entry is invalid (NULL / empty host).
 */
int nop_nvr_channels_add(nop_nvr_channels_t *channels, const nop_nvr_channel_entry_t *entry);

/**
 * Insert or update a binding keyed by @p entry->channel (required, >= 0).
 * Unlike add(), does not dedupe by host+port — multiple channels may share one
 * device (multi-video-source). @return NOP_OK, NOP_ERR_PARAM, or NOP_ERR_NOMEM.
 */
nop_status_t nop_nvr_channels_upsert(nop_nvr_channels_t *channels,
                                     const nop_nvr_channel_entry_t *entry);

/** Remove the camera on @p channel. @return NOP_OK or NOP_ERR_NOTFOUND. */
nop_status_t nop_nvr_channels_remove(nop_nvr_channels_t *channels, int channel);

/** Copy the entry for @p channel into @p out. @return 1 if found, 0 otherwise. */
int nop_nvr_channels_get(nop_nvr_channels_t *channels, int channel,
                         nop_nvr_channel_entry_t *out);

/** Copy up to @p max entries into @p out (ascending channel). @return the count. */
int nop_nvr_channels_list(nop_nvr_channels_t *channels,
                          nop_nvr_channel_entry_t *out, int max);

/** @return number of bound cameras. */
int nop_nvr_channels_count(const nop_nvr_channels_t *channels);

/** @return the channel bound to @p host + @p port, or -1 if none (dedup helper). */
int nop_nvr_channels_find(nop_nvr_channels_t *channels, const char *host, int port);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_NVR_CHANNELS_H */
