/**
 * @file hal_ptz.h
 * @brief Pan/Tilt/Zoom HAL. Firmware registers this under HAL_PTZ.
 */
#ifndef NOP_SDK_HAL_PTZ_H
#define NOP_SDK_HAL_PTZ_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hal_ptz_dir {
    HAL_PTZ_STOP = 0,
    HAL_PTZ_UP, HAL_PTZ_DOWN, HAL_PTZ_LEFT, HAL_PTZ_RIGHT,
    HAL_PTZ_ZOOM_IN, HAL_PTZ_ZOOM_OUT
} hal_ptz_dir_t;

typedef struct hal_ptz_if {
    /** Move @p channel in @p dir at @p speed (0..100). */
    nop_status_t (*move)(void *ctx, int channel, hal_ptz_dir_t dir, int speed);
    /** Go to preset index @p preset on @p channel. May be NULL. */
    nop_status_t (*goto_preset)(void *ctx, int channel, int preset);
    /** Step the gimbal @p step increments in @p dir on @p channel. May be NULL. */
    nop_status_t (*move_by_step)(void *ctx, int channel, hal_ptz_dir_t dir, int step);
    /** Stop pan/tilt/zoom motion on @p channel. May be NULL. */
    nop_status_t (*stop)(void *ctx, int channel);
    /** Step focus @p step increments; @p focus_in=1 near, 0 far. May be NULL. */
    nop_status_t (*focus_by_step)(void *ctx, int channel, int focus_in, int step);
    /** Stop focus motion on @p channel. May be NULL. */
    nop_status_t (*focus_stop)(void *ctx, int channel);
    /** Save the current position of @p channel as preset index @p preset. May be NULL. */
    nop_status_t (*set_preset)(void *ctx, int channel, int preset);
    /** Delete preset index @p preset on @p channel. May be NULL. */
    nop_status_t (*remove_preset)(void *ctx, int channel, int preset);
    /** Save the current position of @p channel as the home position. May be NULL. */
    nop_status_t (*set_home)(void *ctx, int channel);
    /** Return @p channel to its home position. May be NULL. */
    nop_status_t (*goto_home)(void *ctx, int channel);
    void *ctx;
} hal_ptz_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_PTZ_H */
