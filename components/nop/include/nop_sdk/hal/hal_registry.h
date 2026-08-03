/**
 * @file hal_registry.h
 * @brief Hardware Abstraction Layer registry (Beningo vtable + opaque handle).
 *
 * Firmware registers a function-pointer table for each capability it supports
 * at startup. Business handlers fetch the table via hal_registry_get(); if a
 * capability is unregistered, its commands return 501 — the same gate the
 * capability registry enforces.
 */
#ifndef NOP_SDK_HAL_REGISTRY_H
#define NOP_SDK_HAL_REGISTRY_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Identifies a HAL interface table. Keep in sync with the hal_*.h headers.
 * Values are part of the ABI — only append new ids before HAL_ID_MAX, never
 * renumber existing ones.
 */
typedef enum hal_id {
    HAL_VIDEO = 0,
    HAL_PTZ,
    HAL_LIGHT,
    HAL_SYSTEM,
    HAL_STORAGE,   /**< SD card / disk volumes — hal_storage.h */
    HAL_RECORD,    /**< recording enable + schedules — hal_record.h */
    HAL_AUDIO,     /**< two-way audio / speaker — hal_audio.h */
    HAL_OTA,       /**< firmware upgrade — hal_ota.h */
    HAL_ANALOG,    /**< coaxial (analog) camera control — hal_analog.h */
    HAL_ID_MAX
} hal_id_t;

/**
 * Register an interface table for @p id. @p vtable must outlive the SDK (it is
 * stored by pointer, not copied). Pass NULL to clear a registration.
 */
nop_status_t hal_register(hal_id_t id, const void *vtable);

/** @return registered vtable for @p id, or NULL if none. */
const void *hal_registry_get(hal_id_t id);

/** @return non-zero if @p id has a registered implementation. */
int hal_registry_has(hal_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_REGISTRY_H */
