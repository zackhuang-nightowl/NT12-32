#include "capability/cap_registry.h"
#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdint.h>
#include <string.h>

struct cap_registry {
    uint32_t      enabled_mask;   /* bit i set => capability id i enabled */
    osal_mutex_t *mutex;
};

/* Capabilities that exist regardless of hardware (pure protocol). */
/* Core caps plus the software/config domains answered from in-SDK state (no
 * hardware gate). None of the added ones carry a getDeviceCapabilities feature
 * string, so always-on does not over-advertise hardware features. */
#define CAP_ALWAYS_ENABLED \
    ((1u << CAP_DEVICE) | (1u << CAP_AUTH) | (1u << CAP_SYSTEM) | \
     (1u << CAP_NETWORK) | (1u << CAP_PUSH) | (1u << CAP_NOTIFY) | \
     (1u << CAP_AI) | (1u << CAP_MISC))

const char *nop_cap_name(nop_cap_id_t capability)
{
    switch (capability) {
    case CAP_DEVICE:  return "device";
    case CAP_AUTH:    return "auth";
    case CAP_SYSTEM:  return "system";
    case CAP_STREAM:  return "stream";
    case CAP_PTZ:     return "ptz";
    case CAP_LIGHT:   return "light";
    case CAP_AI:      return "ai";
    case CAP_AUDIO:   return "audio";
    case CAP_STORAGE: return "storage";
    case CAP_RECORD:  return "record";
    case CAP_CLOUD:   return "cloud";
    case CAP_OTA:     return "ota";
    case CAP_PUSH:    return "push";
    case CAP_NETWORK: return "network";
    case CAP_BIND:    return "bind";
    case CAP_NOTIFY:  return "notify";
    case CAP_MISC:    return "misc";
    default:          return "";
    }
}

nop_cap_id_t nop_cap_from_name(const char *name)
{
    int i;
    if (!name)
        return NOP_CAP_MAX;
    for (i = 0; i < NOP_CAP_MAX; i++)
        if (strcmp(name, nop_cap_name((nop_cap_id_t)i)) == 0)
            return (nop_cap_id_t)i;
    return NOP_CAP_MAX;
}

cap_registry_t *cap_registry_create(void)
{
    cap_registry_t *registry = (cap_registry_t *)nop_calloc(1, sizeof(*registry));
    if (!registry)
        return NULL;
    registry->enabled_mask = CAP_ALWAYS_ENABLED;
    registry->mutex = osal_mutex_create();
    return registry;
}

void cap_registry_destroy(cap_registry_t *registry)
{
    if (!registry)
        return;
    osal_mutex_destroy(registry->mutex);
    nop_free(registry);
}

nop_status_t cap_registry_enable(cap_registry_t *registry, nop_cap_id_t capability, bool on)
{
    if (!registry || capability < 0 || capability >= NOP_CAP_MAX)
        return NOP_ERR_PARAM;
    osal_mutex_lock(registry->mutex);
    if (on)
        registry->enabled_mask |= (1u << capability);
    else
        registry->enabled_mask &= ~(1u << capability);
    registry->enabled_mask |= CAP_ALWAYS_ENABLED;   /* core caps cannot be turned off */
    osal_mutex_unlock(registry->mutex);
    return NOP_OK;
}

bool cap_registry_is_enabled(const cap_registry_t *registry, nop_cap_id_t capability)
{
    bool enabled;
    if (!registry || capability < 0 || capability >= NOP_CAP_MAX)
        return false;
    osal_mutex_lock(registry->mutex);
    enabled = (registry->enabled_mask & (1u << capability)) != 0;
    osal_mutex_unlock(registry->mutex);
    return enabled;
}

/* Map a lit-up internal capability to its public getDeviceCapabilities feature
 * string, or NULL if the capability has no device-level feature flag. */
static const char *capability_feature_string(nop_cap_id_t capability)
{
    switch (capability) {
    case CAP_PTZ:     return "ptz";
    case CAP_LIGHT:   return "floodlight";
    case CAP_CLOUD:   return "cloudRecording";
    case CAP_STORAGE: return "format";
    case CAP_AUDIO:   return "speaker";
    /* CAP_RECORD / CAP_OTA have no device-capability flag in the spec — their
     * presence is discovered via their commands (501 when the HAL is absent). */
    default:          return NULL;
    }
}

nop_json_t *cap_registry_summary(const cap_registry_t *registry, int channel_count)
{
    nop_json_t *root, *device, *capabilities, *channels;
    int         capability_index, channel_index;

    root = nop_json_obj();
    if (!root)
        return NULL;

    device       = nop_json_obj();
    capabilities = nop_json_arr();
    nop_json_add(device, "capabilities", capabilities);
    nop_json_add(root, "device", device);

    if (registry) {
        for (capability_index = 0; capability_index < NOP_CAP_MAX; capability_index++) {
            if (cap_registry_is_enabled(registry, (nop_cap_id_t)capability_index)) {
                const char *feature = capability_feature_string((nop_cap_id_t)capability_index);
                if (feature)
                    nop_json_arr_push_str(capabilities, feature);
            }
        }
    }

    channels = nop_json_arr();
    for (channel_index = 0; channel_index < channel_count; channel_index++) {
        nop_json_t *channel = nop_json_obj();
        nop_json_add_int(channel, "channel", (double)channel_index);
        nop_json_arr_push(channels, channel);
    }
    nop_json_add(root, "channels", channels);
    return root;
}
