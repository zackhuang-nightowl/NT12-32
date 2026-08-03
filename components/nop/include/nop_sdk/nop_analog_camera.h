/**
 * @file nop_analog_camera.h
 * @brief videoRecorder analog (coaxial) camera plug-and-play manager. Polls the
 *        DVR's physical channels via HAL_ANALOG: a camera plugged into physical
 *        channel N binds to channel N (frame rate adjusted to the DVR budget),
 *        unplugging unbinds it. Bound cameras appear in the channel registry
 *        (nop_nvr_channels.h) with source NOP_CAMERA_SOURCE_ANALOG. UTC control
 *        (OSD menu / navigation) passes through to the bound camera.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR / DVR). The SDK orchestrates detect→bind→
 * set-framerate and UTC passthrough; the coaxial signalling itself is the
 * firmware's HAL_ANALOG implementation.
 */
#ifndef NOP_SDK_ANALOG_CAMERA_H
#define NOP_SDK_ANALOG_CAMERA_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/hal/hal_analog.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque manager. */
typedef struct nop_analog_camera nop_analog_camera_t;

/**
 * Create the manager over @p registry (borrowed; must outlive this), polling
 * physical channels 0..@p physical_channels-1 and applying @p target_framerate
 * to a newly-detected camera (<=0 => skip the frame-rate step). Returns NULL on
 * bad argument / allocation failure.
 */
nop_analog_camera_t *nop_analog_camera_create(nop_nvr_channels_t *registry,
                                              int physical_channels,
                                              int target_framerate);

/** Destroy the manager (does not touch the registry). NULL-safe. */
void nop_analog_camera_destroy(nop_analog_camera_t *manager);

/**
 * Poll every physical channel once: bind newly-present cameras (set frame rate
 * + register) and unbind removed ones. Call on a plug/unplug event or a timer.
 * @return the number of channels currently bound.
 */
int nop_analog_camera_poll(nop_analog_camera_t *manager);

/**
 * Send a UTC control command to the analog camera on physical @p channel.
 * @return NOP_OK; NOP_ERR_STATE if no camera is bound there; NOP_ERR_NOTIMPL if
 * HAL_ANALOG/utc_send is unavailable.
 */
nop_status_t nop_analog_camera_control(nop_analog_camera_t *manager, int channel,
                                       hal_analog_utc_t command);

/** @return non-zero if a camera is currently bound on physical @p channel. */
int nop_analog_camera_is_bound(nop_analog_camera_t *manager, int channel);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ANALOG_CAMERA_H */
