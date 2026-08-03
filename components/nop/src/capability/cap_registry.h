/**
 * @file cap_registry.h  (internal)
 * @brief Capability registry — single source of truth for which capabilities
 *        are "lit up" on this device. Drives command gating (501 when off) and
 *        the getDeviceCapabilities summary.
 */
#ifndef NOP_CAPABILITY_REGISTRY_H
#define NOP_CAPABILITY_REGISTRY_H

#include "base/nop_json.h"
#include "nop_sdk/nop_caps.h"
#include "nop_sdk/nop_err.h"

#include <stdbool.h>

typedef struct cap_registry cap_registry_t;

cap_registry_t *cap_registry_create(void);
void            cap_registry_destroy(cap_registry_t *registry);

/** Turn a capability on/off. CAP_DEVICE/AUTH/SYSTEM are always on. */
nop_status_t cap_registry_enable(cap_registry_t *registry, nop_cap_id_t capability, bool on);
bool         cap_registry_is_enabled(const cap_registry_t *registry, nop_cap_id_t capability);

/**
 * Build the getDeviceCapabilities response content:
 *   { "device": { "capabilities": [..] }, "channels": [..] }
 * Capability strings are derived from the lit-up capabilities. Caller owns it.
 */
nop_json_t *cap_registry_summary(const cap_registry_t *registry, int channel_count);

#endif /* NOP_CAPABILITY_REGISTRY_H */
