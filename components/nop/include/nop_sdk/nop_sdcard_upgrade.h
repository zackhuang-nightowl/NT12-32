/**
 * @file nop_sdcard_upgrade.h
 * @brief SD-card firmware upgrade entry. At boot (after the SD card is
 *        mounted), firmware calls this to scan the SD /upgrade directory for a
 *        firmware image and hand it to HAL_OTA.apply_sdcard_image, which
 *        validates + flashes it (and typically reboots).
 *
 * This is the "drop an image on the SD card and power-cycle to upgrade" flow.
 * The SDK only locates the image and drives the HAL hook; the actual flashing
 * (and preserving device identity across the flash) is the firmware's job.
 */
#ifndef NOP_SDK_SDCARD_UPGRADE_H
#define NOP_SDK_SDCARD_UPGRADE_H

#include "nop_sdk/nop_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Scan @p upgrade_dir for a firmware image (a regular file whose name ends in
 * ".img", case-insensitive) and, if one is found, apply it via
 * HAL_OTA.apply_sdcard_image.
 *
 * @param upgrade_dir   directory to scan, e.g. "/mnt/sd/upgrade" (required).
 * @param applied_path  optional buffer that receives the full path of the
 *                      image that was applied (only on NOP_OK); may be NULL.
 * @param applied_cap   capacity of @p applied_path.
 * @return NOP_OK if an image was found and accepted by the HAL;
 *         NOP_ERR_NOTFOUND if the directory has no image;
 *         NOP_ERR_NOTIMPL if HAL_OTA / apply_sdcard_image is unavailable
 *         (or the platform has no directory support);
 *         otherwise the HAL's error (invalid/incompatible image).
 */
nop_status_t nop_sdcard_upgrade_scan(const char *upgrade_dir,
                                     char *applied_path, size_t applied_cap);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_SDCARD_UPGRADE_H */
