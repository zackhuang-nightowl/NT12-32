/**
 * @file nop_nvr_addcamera.h
 * @brief videoRecorder "add wireless cameras" scan-mode state machine, backing
 *        the BaseStation/AddCamera commands startAddWirelessCameras /
 *        stopAddWirelessCameras / getAddWirelessCamerasStatus.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR). Adding cameras is a timed pairing window:
 * the app/GUI opens a scan window; while it is open, the discovery layer (ONVIF
 * WS-Discovery / PoE plug-and-play / BLE — the integrator's code) reports each
 * camera it finds by calling nop_nvr_addcamera_offer(), which admits it into the
 * channel registry (nop_nvr_channels.h). Status (state + count + seconds left)
 * backs getAddWirelessCamerasStatus. This service owns only the scan window and
 * admission policy; it performs no network I/O itself.
 */
#ifndef NOP_SDK_NVR_ADDCAMERA_H
#define NOP_SDK_NVR_ADDCAMERA_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_nvr_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Scan-mode state. */
typedef enum nop_addcamera_state {
    NOP_ADDCAMERA_IDLE = 0,   /**< not scanning */
    NOP_ADDCAMERA_SCANNING,   /**< pairing window open */
    NOP_ADDCAMERA_DONE        /**< window elapsed (results available) */
} nop_addcamera_state_t;

/** Snapshot for getAddWirelessCamerasStatus. */
typedef struct nop_addcamera_status {
    nop_addcamera_state_t state;
    int                   added_count;        /**< cameras admitted this window */
    int                   seconds_remaining;  /**< 0 unless SCANNING */
} nop_addcamera_status_t;

/** Opaque handle. */
typedef struct nop_nvr_addcamera nop_nvr_addcamera_t;

/**
 * Create the scan-mode service over @p registry (borrowed; must outlive this).
 * Returns NULL on NULL registry / allocation failure.
 */
nop_nvr_addcamera_t *nop_nvr_addcamera_create(nop_nvr_channels_t *registry);

/** Destroy the service (does not touch the registry). NULL-safe. */
void nop_nvr_addcamera_destroy(nop_nvr_addcamera_t *service);

/**
 * Open a pairing window of @p scan_seconds (<=0 => a default of 60). Resets the
 * admitted-count. @return NOP_OK, or NOP_ERR_STATE if already scanning.
 */
nop_status_t nop_nvr_addcamera_start(nop_nvr_addcamera_t *service, int scan_seconds);

/** Close the pairing window early. @return NOP_OK (idempotent). */
nop_status_t nop_nvr_addcamera_stop(nop_nvr_addcamera_t *service);

/** Fill @p out with the current status (recomputing window expiry). */
void nop_nvr_addcamera_status(nop_nvr_addcamera_t *service, nop_addcamera_status_t *out);

/**
 * Offer a discovered camera for admission. Admits @p found to the registry only
 * while the pairing window is open; @return the assigned channel index (>= 0),
 * or -1 if not scanning / window elapsed / registry full.
 */
int nop_nvr_addcamera_offer(nop_nvr_addcamera_t *service,
                            const nop_nvr_channel_entry_t *found);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_NVR_ADDCAMERA_H */
