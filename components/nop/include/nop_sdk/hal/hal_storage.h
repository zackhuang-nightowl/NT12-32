/**
 * @file hal_storage.h
 * @brief Storage HAL: SD card / disk volumes. Firmware registers this under
 *        HAL_STORAGE; backs CAP_STORAGE commands (getCurrentStorage,
 *        X_NightOwl_getStorageInfo, getAllDisksHealth, formatStorage).
 */
#ifndef NOP_SDK_HAL_STORAGE_H
#define NOP_SDK_HAL_STORAGE_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Lifecycle/health state of one storage volume, as reported to clients. */
typedef enum hal_storage_status {
    HAL_STORAGE_STATUS_UNKNOWN = 0,
    HAL_STORAGE_STATUS_READY,        /**< mounted, usable */
    HAL_STORAGE_STATUS_FORMATTING,   /**< format in progress */
    HAL_STORAGE_STATUS_UNFORMATTED,  /**< present but needs formatting */
    HAL_STORAGE_STATUS_ERROR,        /**< faulted / unreadable */
    HAL_STORAGE_STATUS_ABSENT        /**< slot empty */
} hal_storage_status_t;

/** Identity + usage of a single storage volume. */
typedef struct hal_storage_volume {
    char                 name[16];               /**< "sdcard","hdd","usb","nas","hdd2"... */
    long long            total_size_kilobytes;   /**< total capacity in KiB */
    long long            free_size_kilobytes;    /**< free space in KiB */
    hal_storage_status_t status;
} hal_storage_volume_t;

typedef struct hal_storage_if {
    /** @return number of storage volumes present (0 if none). */
    int (*volume_count)(void *ctx);
    /** Fill @p out for volume @p index (0-based, < volume_count()). */
    nop_status_t (*get_volume_info)(void *ctx, int index, hal_storage_volume_t *out);
    /**
     * Copy the in-use volume name into @p name_out (capacity @p name_capacity).
     * May be NULL on single-volume devices (the sole volume is implied).
     */
    nop_status_t (*get_current_volume_name)(void *ctx, char *name_out, int name_capacity);
    /** Format volume @p volume_name (return immediately; may run async). */
    nop_status_t (*format)(void *ctx, const char *volume_name);
    void *ctx;
} hal_storage_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_STORAGE_H */
