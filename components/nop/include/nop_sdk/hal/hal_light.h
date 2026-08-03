/**
 * @file hal_light.h
 * @brief Flood/IR light HAL. Firmware registers this under HAL_LIGHT.
 */
#ifndef NOP_SDK_HAL_LIGHT_H
#define NOP_SDK_HAL_LIGHT_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_light_if {
    /** Turn the channel light @p on (0/1). */
    nop_status_t (*set_switch)(void *ctx, int channel, int on);
    /** Set brightness 0..100. May be NULL if not dimmable. */
    nop_status_t (*set_brightness)(void *ctx, int channel, int level);
    void *ctx;
} hal_light_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_LIGHT_H */
