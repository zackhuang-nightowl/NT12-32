/**
 * @file hal_stub.h
 * @brief Print-stub HAL implementations so the SDK runs out-of-the-box with no
 *        real hardware. Each call logs and returns canned data. This is the
 *        "every interface ships with a mock" rule from the architecture.
 */
#ifndef NOP_PORT_HAL_STUB_H
#define NOP_PORT_HAL_STUB_H

#include "nop_sdk/hal/hal_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register every stub HAL table (HAL_SYSTEM/VIDEO/PTZ/LIGHT). */
void hal_stub_register_all(void);

/** Test helper: fire a system event to the installed sink (simulates a
 *  reset-button press / SD-card insert). No-op if no sink is installed. */
void hal_stub_fire_system_event(hal_system_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* NOP_PORT_HAL_STUB_H */
