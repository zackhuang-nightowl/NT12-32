/**
 * @file hal_ota.h
 * @brief Firmware-upgrade HAL. Firmware registers this under HAL_OTA; backs
 *        CAP_OTA commands (upgradeFirmware, checkFirmwareUpgradeStatus,
 *        writeFirmware).
 */
#ifndef NOP_SDK_HAL_OTA_H
#define NOP_SDK_HAL_OTA_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Upgrade progress, matching checkFirmwareUpgradeStatus "status" values. */
typedef enum hal_ota_status {
    HAL_OTA_STATUS_IDLE = 0,            /**< not updating */
    HAL_OTA_STATUS_DOWNLOADING = 1,     /**< fetching new firmware */
    HAL_OTA_STATUS_FAILED = 2,          /**< update failed (see error code) */
    HAL_OTA_STATUS_DOWNLOADED = 3,      /**< firmware file ready to apply */
    HAL_OTA_STATUS_NONE_AVAILABLE = 4   /**< no new firmware available */
} hal_ota_status_t;

typedef struct hal_ota_if {
    /**
     * Start an upgrade. @p url is where the firmware lives; pass NULL to let
     * the device construct its own OTA URL. @p automatic=1 means apply as soon
     * as the download finishes; 0 waits for the client to drive it.
     */
    nop_status_t (*begin_upgrade)(void *ctx, const char *url, int automatic);
    /**
     * Report current progress into @p *status_out; @p *error_out gets a
     * device-specific error code when status is FAILED (0 otherwise).
     */
    nop_status_t (*get_upgrade_status)(void *ctx, hal_ota_status_t *status_out, int *error_out);
    /**
     * Receive a firmware image chunk pushed by the client (writeFirmware):
     * @p length bytes at byte @p offset. May be NULL on URL-only devices.
     */
    nop_status_t (*write_firmware_chunk)(void *ctx, const void *data, int length, long long offset);
    /**
     * Apply a firmware image already present on local storage (SD-card upgrade:
     * the device found an image under the SD /upgrade directory at boot). The
     * implementation validates and flashes @p path, then typically reboots.
     * Returns NOP_OK once the image is accepted/flashing; an error if the image
     * is invalid/incompatible. May be NULL (device has no SD-card upgrade path).
     */
    nop_status_t (*apply_sdcard_image)(void *ctx, const char *path);
    void *ctx;
} hal_ota_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_OTA_H */
