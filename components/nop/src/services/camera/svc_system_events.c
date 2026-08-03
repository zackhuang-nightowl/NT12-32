/**
 * @file svc_system_events.c
 * @brief Reset-button / SD-card event handling. Installs the HAL_SYSTEM event
 *        sink and drives factory reset (with identity preserve), boot snapshot,
 *        and SD-card upgrade. See nop_sdk/nop_system_events.h.
 */
#include "nop_sdk/nop_system_events.h"
#include "nop_sdk/nop_snapshot.h"
#include "nop_sdk/nop_sdcard_upgrade.h"
#include "nop_sdk/hal/hal_registry.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

struct nop_system_events {
    nop_system_events_config_t config;    /* copied; read-only after start */
    const hal_system_if       *system;    /* cached HAL (owns the sink) */
    osal_mutex_t              *mutex;
    int                        stopping;   /* set by stop(); sink then no-ops */
    int                        in_flight;  /* sink invocations currently running */
};

/* Reset long press → preserve identity (if requested) then factory_reset. */
static void system_events_handle_factory_reset(nop_system_events_t *handle)
{
    const hal_system_if *system = handle->system;

    if (handle->config.on_preserve_identity && system->get_device_info) {
        hal_device_info_t identity;
        if (system->get_device_info(system->ctx, &identity) == NOP_OK)
            handle->config.on_preserve_identity(handle->config.ctx, &identity);
    }
    if (system->factory_reset)
        system->factory_reset(system->ctx);
}

/* SD inserted → optional boot snapshot, then optional upgrade scan. */
static void system_events_handle_sdcard_inserted(nop_system_events_t *handle)
{
    if (handle->config.snapshot_path[0])
        nop_snapshot_capture_to_file(handle->config.snapshot_channel,
                                     handle->config.snapshot_stream,
                                     handle->config.snapshot_path);
    if (handle->config.upgrade_dir[0])
        nop_sdcard_upgrade_scan(handle->config.upgrade_dir, NULL, 0);
}

static void system_events_sink(void *sink_ctx, hal_system_event_t event)
{
    nop_system_events_t *handle = (nop_system_events_t *)sink_ctx;
    if (!handle)
        return;
    /* Enter the in-flight barrier: if stop() has begun, do nothing (the handle
     * may be about to be freed); otherwise mark ourselves in-flight so stop()
     * waits for us before freeing. */
    osal_mutex_lock(handle->mutex);
    if (handle->stopping) {
        osal_mutex_unlock(handle->mutex);
        return;
    }
    handle->in_flight++;
    osal_mutex_unlock(handle->mutex);

    switch (event) {
    case HAL_SYSTEM_EVENT_BUTTON_RESET_LONG:
        if (handle->config.factory_reset_on_long_press)
            system_events_handle_factory_reset(handle);
        break;
    case HAL_SYSTEM_EVENT_SDCARD_INSERTED:
        system_events_handle_sdcard_inserted(handle);
        break;
    case HAL_SYSTEM_EVENT_BUTTON_RESET_SHORT:
    case HAL_SYSTEM_EVENT_SDCARD_REMOVED:
    default:
        break;      /* no default action */
    }

    osal_mutex_lock(handle->mutex);
    handle->in_flight--;
    osal_mutex_unlock(handle->mutex);
}

nop_system_events_t *nop_system_events_start(const nop_system_events_config_t *config)
{
    const hal_system_if *system;
    nop_system_events_t *handle;

    if (!config)
        return NULL;
    system = (const hal_system_if *)hal_registry_get(HAL_SYSTEM);
    if (!system || !system->set_event_sink)
        return NULL;

    handle = (nop_system_events_t *)nop_calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;
    handle->config = *config;
    handle->system = system;
    handle->mutex  = osal_mutex_create();
    if (!handle->mutex) {
        nop_free(handle);
        return NULL;
    }

    if (system->set_event_sink(system->ctx, system_events_sink, handle) != NOP_OK) {
        osal_mutex_destroy(handle->mutex);
        nop_free(handle);
        return NULL;
    }
    return handle;
}

void nop_system_events_stop(nop_system_events_t *handle)
{
    if (!handle)
        return;
    /* Detach the HAL sink so no new callback is dispatched with this handle. */
    if (handle->system && handle->system->set_event_sink)
        handle->system->set_event_sink(handle->system->ctx, NULL, NULL);
    /* Barrier: refuse new sink work and wait for any in-flight callback (on the
     * firmware's event thread) to finish before freeing, so it cannot touch a
     * freed handle. */
    osal_mutex_lock(handle->mutex);
    handle->stopping = 1;
    while (handle->in_flight > 0) {
        osal_mutex_unlock(handle->mutex);
        osal_sleep_ms(1);
        osal_mutex_lock(handle->mutex);
    }
    osal_mutex_unlock(handle->mutex);

    osal_mutex_destroy(handle->mutex);
    nop_free(handle);
}
