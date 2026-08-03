/**
 * @file svc_analog_camera.c
 * @brief Analog (coaxial) camera plug-and-play manager over HAL_ANALOG and the
 *        channel registry. See nop_sdk/nop_analog_camera.h.
 */
#include "nop_sdk/nop_analog_camera.h"
#include "nop_sdk/hal/hal_registry.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

struct nop_analog_camera {
    nop_nvr_channels_t *registry;         /* borrowed */
    osal_mutex_t       *mutex;
    int                 physical_channels;
    int                 target_framerate;
    /* Per physical channel: the registry channel index it is bound to, or -1 if
     * not bound. The registry may assign an index != the physical channel when
     * that index is already taken, so we must remember the real one to unbind. */
    int                *registry_channel;
};

nop_analog_camera_t *nop_analog_camera_create(nop_nvr_channels_t *registry,
                                              int physical_channels,
                                              int target_framerate)
{
    nop_analog_camera_t *manager;
    int                  i;

    if (!registry || physical_channels <= 0)
        return NULL;
    manager = (nop_analog_camera_t *)nop_calloc(1, sizeof(*manager));
    if (!manager)
        return NULL;
    manager->registry_channel = (int *)nop_calloc((size_t)physical_channels, sizeof(int));
    if (!manager->registry_channel) {
        nop_free(manager);
        return NULL;
    }
    manager->mutex = osal_mutex_create();
    if (!manager->mutex) {
        nop_free(manager->registry_channel);
        nop_free(manager);
        return NULL;
    }
    for (i = 0; i < physical_channels; i++)
        manager->registry_channel[i] = -1;   /* -1 = not bound */
    manager->registry          = registry;
    manager->physical_channels = physical_channels;
    manager->target_framerate  = target_framerate;
    return manager;
}

void nop_analog_camera_destroy(nop_analog_camera_t *manager)
{
    if (!manager)
        return;
    osal_mutex_destroy(manager->mutex);
    nop_free(manager->registry_channel);
    nop_free(manager);
}

/* Register a just-detected analog camera as a channel; record the registry
 * index actually assigned (may differ from the physical channel if that index
 * was taken). Synthetic host keeps registry host+port uniqueness per channel.
 * Leaves the channel unbound (registry_channel stays -1) if the add fails, so
 * the next poll retries. Caller holds the lock. */
static void analog_bind_locked(nop_analog_camera_t *manager, const hal_analog_if *analog,
                               int channel)
{
    nop_nvr_channel_entry_t entry;
    int                     assigned;

    if (analog->set_framerate && manager->target_framerate > 0)
        analog->set_framerate(analog->ctx, channel, manager->target_framerate);

    memset(&entry, 0, sizeof(entry));
    entry.channel = channel;
    entry.enabled = 1;
    entry.source  = NOP_CAMERA_SOURCE_ANALOG;
    entry.port    = 0;
    snprintf(entry.host, sizeof(entry.host), "analog%d", channel);
    snprintf(entry.name, sizeof(entry.name), "Analog CH%d", channel + 1);
    assigned = nop_nvr_channels_add(manager->registry, &entry);
    if (assigned >= 0)
        manager->registry_channel[channel] = assigned;   /* bound only on success */
}

int nop_analog_camera_poll(nop_analog_camera_t *manager)
{
    const hal_analog_if *analog;
    int                  bound_count = 0, channel;

    if (!manager)
        return 0;
    analog = (const hal_analog_if *)hal_registry_get(HAL_ANALOG);

    osal_mutex_lock(manager->mutex);
    for (channel = 0; channel < manager->physical_channels; channel++) {
        int present = (analog && analog->get_presence)
                    ? analog->get_presence(analog->ctx, channel) : 0;
        if (present && manager->registry_channel[channel] < 0) {
            analog_bind_locked(manager, analog, channel);
        } else if (!present && manager->registry_channel[channel] >= 0) {
            nop_nvr_channels_remove(manager->registry, manager->registry_channel[channel]);
            manager->registry_channel[channel] = -1;       /* unplugged */
        }
        if (manager->registry_channel[channel] >= 0)
            bound_count++;
    }
    osal_mutex_unlock(manager->mutex);
    return bound_count;
}

nop_status_t nop_analog_camera_control(nop_analog_camera_t *manager, int channel,
                                       hal_analog_utc_t command)
{
    const hal_analog_if *analog;
    int                  bound;

    if (!manager || channel < 0 || channel >= manager->physical_channels)
        return NOP_ERR_PARAM;

    osal_mutex_lock(manager->mutex);
    bound = (manager->registry_channel[channel] >= 0);
    osal_mutex_unlock(manager->mutex);
    if (!bound)
        return NOP_ERR_STATE;          /* no camera bound on this channel */

    analog = (const hal_analog_if *)hal_registry_get(HAL_ANALOG);
    if (!analog || !analog->utc_send)
        return NOP_ERR_NOTIMPL;
    /* UTC targets the physical (coax) channel, not the registry index. */
    return analog->utc_send(analog->ctx, channel, command);
}

int nop_analog_camera_is_bound(nop_analog_camera_t *manager, int channel)
{
    int bound;
    if (!manager || channel < 0 || channel >= manager->physical_channels)
        return 0;
    osal_mutex_lock(manager->mutex);
    bound = (manager->registry_channel[channel] >= 0);
    osal_mutex_unlock(manager->mutex);
    return bound;
}
