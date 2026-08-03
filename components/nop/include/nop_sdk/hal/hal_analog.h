/**
 * @file hal_analog.h
 * @brief Analog (coaxial) camera HAL. A DVR drives analog cameras over the coax
 *        cable: it detects presence per physical channel, adjusts the camera's
 *        frame rate to fit the DVR's encoding budget, and sends UTC
 *        ("up-the-coax") control signals (OSD-menu navigation / PTZ). Firmware
 *        registers this under HAL_ANALOG. All methods may be NULL.
 */
#ifndef NOP_SDK_HAL_ANALOG_H
#define NOP_SDK_HAL_ANALOG_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** UTC (up-the-coax) control command sent to an analog camera. */
typedef enum hal_analog_utc {
    HAL_ANALOG_UTC_MENU_OPEN = 0,  /**< open the camera's OSD menu */
    HAL_ANALOG_UTC_MENU_ENTER,     /**< enter / confirm */
    HAL_ANALOG_UTC_MENU_EXIT,      /**< exit / back */
    HAL_ANALOG_UTC_UP,
    HAL_ANALOG_UTC_DOWN,
    HAL_ANALOG_UTC_LEFT,
    HAL_ANALOG_UTC_RIGHT
} hal_analog_utc_t;

typedef struct hal_analog_if {
    /** @return non-zero if an analog camera is present on physical @p channel. */
    int (*get_presence)(void *ctx, int channel);
    /**
     * Set the analog camera's output frame rate on @p channel (the DVR reduces
     * it to fit its encoding budget; the camera reboots to apply). May be NULL.
     */
    nop_status_t (*set_framerate)(void *ctx, int channel, int framerate);
    /** Send a UTC control command to the camera on @p channel. May be NULL. */
    nop_status_t (*utc_send)(void *ctx, int channel, hal_analog_utc_t command);
    void *ctx;
} hal_analog_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_ANALOG_H */
