/**
 * @file hal_system.h
 * @brief System HAL: device identity + power control. Firmware implements this
 *        table and registers it under HAL_SYSTEM.
 */
#ifndef NOP_SDK_HAL_SYSTEM_H
#define NOP_SDK_HAL_SYSTEM_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Static device identity reported by getDeviceInfo. */
typedef struct hal_device_info {
    char type[24];            /**< videoRecorder|standaloneIpCamera|doorbell... */
    char serial_number[16];   /**< 12-digit serial */
    char model[48];
    char firmware_version[64];
    char mac[20];
    char ip[40];
    char name[48];
    int  maximum_channel_count;
    int  current_channel_count;
    int  upgrade_fail;        /**< boolean 0/1 */
} hal_device_info_t;

/** Physical device events firmware injects into the SDK. */
typedef enum hal_system_event {
    HAL_SYSTEM_EVENT_BUTTON_RESET_SHORT = 0, /**< reset button short press */
    HAL_SYSTEM_EVENT_BUTTON_RESET_LONG,      /**< reset button long press (factory) */
    HAL_SYSTEM_EVENT_SDCARD_INSERTED,        /**< SD card mounted */
    HAL_SYSTEM_EVENT_SDCARD_REMOVED          /**< SD card removed */
} hal_system_event_t;

/** System-event sink installed by the SDK; firmware invokes it per event. */
typedef void (*hal_system_event_fn)(void *sink_ctx, hal_system_event_t event);

typedef struct hal_system_if {
    /** Fill @p out with device identity. */
    nop_status_t (*get_device_info)(void *ctx, hal_device_info_t *out);
    /** Request a reboot (return immediately; reboot may be deferred). */
    nop_status_t (*reboot)(void *ctx);
    /** Restore factory defaults. May be NULL if unsupported. */
    nop_status_t (*factory_reset)(void *ctx);
    /**
     * Install the system-event sink. Firmware stores @p sink/@p sink_ctx and
     * calls sink(sink_ctx, event) on reset-button / SD-card hardware events.
     * Pass sink=NULL to detach. May be NULL if the device injects no events.
     */
    nop_status_t (*set_event_sink)(void *ctx, hal_system_event_fn sink, void *sink_ctx);
    void *ctx;
} hal_system_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_SYSTEM_H */
