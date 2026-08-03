/**
 * @file nop_ptz_patrol.h
 * @brief PTZ patrol (cruise) execution engine. Cycles the gimbal through an
 *        ordered list of preset spots, holding each for its dwell time, driving
 *        HAL_PTZ.goto_preset on a background thread. Backs the operatePtzPatrol
 *        start/stop command; the patrol/preset metadata lives in the CAP_PTZ
 *        command layer, which hands the resolved spot list here to run.
 *
 * One patrol runs at a time per engine; starting a new one replaces the current.
 * POSIX only (background thread); on other platforms the calls are no-ops.
 */
#ifndef NOP_SDK_PTZ_PATROL_H
#define NOP_SDK_PTZ_PATROL_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One patrol stop: go to @p preset, hold @p dwell_seconds, at @p speed. */
typedef struct nop_ptz_spot {
    int preset;          /**< HAL preset index to go to */
    int dwell_seconds;   /**< seconds to hold before the next spot (0 => minimal) */
    int speed;           /**< advisory move speed 0..100 */
} nop_ptz_spot_t;

/** Opaque engine. */
typedef struct nop_ptz_patrol nop_ptz_patrol_t;

/** Create an idle engine. Returns NULL on allocation failure. */
nop_ptz_patrol_t *nop_ptz_patrol_create(void);

/** Stop any running patrol and destroy the engine. NULL-safe. */
void nop_ptz_patrol_destroy(nop_ptz_patrol_t *engine);

/**
 * Start cycling @p spots (copied) on @p channel, looping until stopped. Any
 * patrol already running is stopped first. @return NOP_OK; NOP_ERR_PARAM on bad
 * args; NOP_ERR_NOTIMPL if HAL_PTZ/goto_preset is unavailable or the platform
 * has no thread support; NOP_ERR_STATE on thread/allocation failure.
 */
nop_status_t nop_ptz_patrol_start(nop_ptz_patrol_t *engine, int channel,
                                  const nop_ptz_spot_t *spots, int count);

/** Stop the running patrol (joins the thread). Idempotent. @return NOP_OK. */
nop_status_t nop_ptz_patrol_stop(nop_ptz_patrol_t *engine);

/** @return non-zero if a patrol is currently running. */
int nop_ptz_patrol_is_running(nop_ptz_patrol_t *engine);

/** @return the number of goto-preset moves issued since the last start
 *  (diagnostics / tests). */
int nop_ptz_patrol_step_count(nop_ptz_patrol_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_PTZ_PATROL_H */
