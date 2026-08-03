/**
 * @file onvif_map_utils.h  (internal)
 * @brief Shared, non-geometry conversions reused across the NOP->ONVIF domain
 *        mappers. Geometry lives in onvif_coord.h; anything reused by 2+ mappers
 *        that is NOT coordinate math (velocity vectors, speed scaling, return-
 *        code mapping, token formatting, ...) belongs here — never duplicated in
 *        a mapper. Pure C, no ONVIF headers. See CONVENTIONS.md.
 */
#ifndef NOP_ONVIF_MAP_UTILS_H
#define NOP_ONVIF_MAP_UTILS_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Clamp a double to [lo, hi]. */
double onvif_map_clampd(double v, double lo, double hi);

/**
 * Scale a NOP integer level to an ONVIF normalized magnitude in [0,1].
 * e.g. speed 1..10 -> 0.1..1.0 with scale_max=10; 0..100 -> 0..1 with 100.
 * @p level <= 0 returns 0 (let the device pick its default). Result clamped.
 */
float onvif_map_level_to_unit(double level, double scale_max);

/**
 * Map a NOP PTZ direction name to an ONVIF pan/tilt/zoom velocity of magnitude
 * @p mag (ONVIF +y = up). Recognized: up/down/left/right, leftUp/leftDown/
 * rightUp/rightDown, zoomIn/zoomOut. Unknown/NULL -> all zero.
 */
void onvif_map_dir_to_velocity(const char *dir, float mag,
                               float *pan, float *tilt, float *zoom);

/** Map an ONVIF adapter return code (0 = ok, <0 = fail) to an nop_status_t. */
nop_status_t onvif_map_rc(int onvif_rc);

/**
 * Format a NOP integer id as an ONVIF token string (e.g. preset 3 -> "3").
 * Writes at most @p size bytes (always NUL-terminated when size > 0).
 * @return @p buf.
 */
const char *onvif_map_int_token(int id, char *buf, unsigned size);

#ifdef __cplusplus
}
#endif

#endif /* NOP_ONVIF_MAP_UTILS_H */
