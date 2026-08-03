/**
 * @file nop_app.h
 * @brief One-stop façade for the NOP SDK.
 *
 * Typical embed:
 *   nop_app_config_t cfg = { .role = NOP_ROLE_IPC };
 *   nop_app_t *app = nop_app_create(&cfg);
 *   // firmware: hal_register(HAL_SYSTEM, &my_system_if); ...
 *   char *out = NULL;
 *   nop_app_dispatch(app, "{\"func\":\"getDeviceInfo\",\"args\":{}}", &out);
 *   // ... use out ...; nop_app_free_response(out);
 *   nop_app_destroy(app);
 */
#ifndef NOP_SDK_APP_H
#define NOP_SDK_APP_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque application handle. */
typedef struct nop_app nop_app_t;

/** Creation config. Zero-initialize; reserved fields keep ABI stable. */
typedef struct nop_app_config {
    nop_role_t role;        /**< device role (default NOP_ROLE_IPC) */
    int        auto_caps;   /**< 1: light up caps whose HAL is present (default) */
    void      *reserved[6];
} nop_app_config_t;

/**
 * Create an app: builds the router, capability registry, registers all built-in
 * business handlers, and lights up capabilities per @p cfg. Returns NULL on
 * failure.
 */
nop_app_t *nop_app_create(const nop_app_config_t *cfg);

/** Destroy and free all resources. NULL-safe. */
void nop_app_destroy(nop_app_t *app);

/**
 * Dispatch one NOP request envelope (JSON in) and produce the response envelope
 * (JSON out). @p json_out is heap-allocated and must be released with
 * nop_app_free_response(). This is the same path all transports feed into.
 */
nop_status_t nop_app_dispatch(nop_app_t *app, const char *json_in, char **json_out);

/** Release a response string returned by nop_app_dispatch(). NULL-safe. */
void nop_app_free_response(char *json_out);

/**
 * Manually light up (@p enable=1) or gate off a capability by id. Mainly for
 * tests/firmware that toggle features at runtime. @p cap_id is a cap_ids value.
 */
nop_status_t nop_app_set_capability(nop_app_t *app, int cap_id, int enable);

/**
 * Provide the loaded device provisioning config to the handlers, which read it
 * for config-driven defaults (e.g. AI detection types). Stored by pointer —
 * the config must outlive the app. Pass NULL to clear. @p device_config is a
 * const nop_device_config_t* (typed as void* to avoid a public header cycle).
 */
nop_status_t nop_app_set_device_config(nop_app_t *app, const void *device_config);

/**
 * Attach the detection-event hub (nop_event_hub_t) so event-backed query
 * handlers (queryEventList / queryEventCalendar) can read recent events. Stored
 * by pointer; must outlive the app. Pass NULL to detach. @p event_hub is a
 * nop_event_hub_t* (void* to avoid a public header cycle).
 */
nop_status_t nop_app_set_event_hub(nop_app_t *app, void *event_hub);

/**
 * Attach the AddWirelessCameras engine (nop_nvr_addcamera_t, NVR role) so the
 * startAddWirelessCameras / stopAddWirelessCameras / getAddWirelessCamerasStatus
 * commands drive it. Stored by pointer (void* to avoid a public header cycle);
 * must outlive the app. Pass NULL to detach.
 */
nop_status_t nop_app_set_nvr_addcamera(nop_app_t *app, void *addcamera);

/**
 * Attach the PTZ patrol execution engine (nop_ptz_patrol_t) so operatePtzPatrol
 * start/stop drives it. Stored by pointer (void* to avoid a public header
 * cycle); must outlive the app. Pass NULL to detach.
 */
nop_status_t nop_app_set_ptz_patrol(nop_app_t *app, void *ptz_patrol);

/**
 * Attach the wireless-chime registry/pairing engine (nop_chime_t) so the
 * getWirelessChimes / setWirelessChimes / ringWirelessChimes /
 * startAddWirelessChime / stopAddWirelessChime / startRemoveWirelessChime
 * commands drive it. Stored by pointer (void*); must outlive the app. NULL to
 * detach.
 */
nop_status_t nop_app_set_chime(nop_app_t *app, void *chime);

/**
 * Attach the bound-camera channel registry (nop_nvr_channels_t, NVR role) so the
 * ONVIF mapping layer can look up each channel's camera (host/port/credentials)
 * and its control backend. Stored by pointer (void* to avoid a public header
 * cycle); must outlive the app. Pass NULL to detach.
 */
nop_status_t nop_app_set_nvr_channels(nop_app_t *app, void *channels);

/**
 * Attach the ONVIF client mapping backend (nop_onvif_map_backend_t) so that
 * per-channel NOP commands targeting an ONVIF camera are translated to ONVIF.
 * Stored by pointer (void*); must outlive the app. Pass NULL to detach.
 * Only effective when the SDK is built with NOP_WITH_ONVIF=ON.
 */
nop_status_t nop_app_set_onvif_backend(nop_app_t *app, void *onvif_backend);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_APP_H */
