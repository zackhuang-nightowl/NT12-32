/**
 * @file nop_light_linkage.h
 * @brief Camera-role light / siren / day-night linkage engine.
 *
 * Subscribes the detection-event hub and enacts the documented warning
 * behaviour: on a detection it turns the warning light on (HAL_LIGHT), switches
 * imaging to color for color night recording (HAL_VIDEO.set_day_night), and
 * turns the light back off after a hold time. A one-key "panic" turns on the
 * white light + siren (HAL_AUDIO.play_alert) at highest priority until cleared.
 *
 * Role: CAMERA (NOP_ROLE_IPC). POSIX only (pthread timer). All hardware effects
 * go through the HAL; absent hooks degrade to no-ops.
 */
#ifndef NOP_SDK_LIGHT_LINKAGE_H
#define NOP_SDK_LIGHT_LINKAGE_H

#include "nop_sdk/nop_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque linkage engine. */
typedef struct nop_light_linkage nop_light_linkage_t;

/** Configuration. Zero-initialize; hub is required. */
typedef struct nop_light_linkage_config {
    nop_event_hub_t *hub;            /**< detection source (required) */
    int              light_seconds;  /**< light hold time after a trigger (0 => 30) */
    int              siren_seconds;  /**< panic siren duration (0 => 30) */
} nop_light_linkage_config_t;

/** Start the engine (subscribes hub, starts the auto-off timer). NULL on failure. */
nop_light_linkage_t *nop_light_linkage_start(const nop_light_linkage_config_t *config);

/** Stop the engine and turn off any active light/siren. NULL-safe. */
void nop_light_linkage_stop(nop_light_linkage_t *linkage);

/**
 * One-key panic on @p channel: @p enable=1 turns white light + siren on (top
 * priority, no auto-off); @p enable=0 clears it.
 */
void nop_light_linkage_panic(nop_light_linkage_t *linkage, int channel, int enable);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_LIGHT_LINKAGE_H */
