/**
 * @file nop_system_events.h
 * @brief Reset-button and SD-card event handling. Installs itself as the
 *        HAL_SYSTEM event sink and reacts to physical events:
 *          - reset button long press → factory reset (with an identity-preserve
 *            hook so the integrator can keep UID/SN across the wipe);
 *          - SD card inserted → boot snapshot + SD-card upgrade scan.
 *
 * Ties together HAL_SYSTEM (event source + factory_reset), nop_snapshot, and
 * nop_sdcard_upgrade. Every action is optional (driven by config); an absent
 * HAL hook degrades to a no-op.
 */
#ifndef NOP_SDK_SYSTEM_EVENTS_H
#define NOP_SDK_SYSTEM_EVENTS_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/hal/hal_system.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle. */
typedef struct nop_system_events nop_system_events_t;

/**
 * Called just before factory_reset so the integrator can persist device
 * identity (UID / serial number) to storage that survives the reset. @p identity
 * is the current HAL_SYSTEM device info, borrowed for the call only.
 */
typedef void (*nop_system_preserve_identity_fn)(void *ctx, const hal_device_info_t *identity);

/** Configuration. Zero-initialize; every field is optional. */
typedef struct nop_system_events_config {
    int  factory_reset_on_long_press;   /**< 1 = reset long press → factory_reset */

    char snapshot_path[256];            /**< SD inserted → snapshot here (empty = skip) */
    int  snapshot_channel;
    int  snapshot_stream;               /**< 0=main, 1=sub */

    char upgrade_dir[256];              /**< SD inserted → scan here for *.img (empty = skip) */

    nop_system_preserve_identity_fn on_preserve_identity;  /**< may be NULL */
    void                           *ctx;
    uint32_t                        reserved[4];
} nop_system_events_config_t;

/**
 * Start handling system events: install the HAL_SYSTEM event sink from @p config
 * (copied). Returns NULL on bad config / when HAL_SYSTEM has no set_event_sink /
 * allocation failure.
 */
nop_system_events_t *nop_system_events_start(const nop_system_events_config_t *config);

/** Detach the HAL sink and free the handle. NULL-safe. */
void nop_system_events_stop(nop_system_events_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_SYSTEM_EVENTS_H */
