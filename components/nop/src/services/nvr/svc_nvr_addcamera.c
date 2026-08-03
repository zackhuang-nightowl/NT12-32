/**
 * @file svc_nvr_addcamera.c
 * @brief "Add wireless cameras" scan-mode state machine over the channel
 *        registry. See nop_sdk/nop_nvr_addcamera.h.
 */
#include "nop_sdk/nop_nvr_addcamera.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#define NOP_ADDCAMERA_DEFAULT_SECONDS 60

struct nop_nvr_addcamera {
    nop_nvr_channels_t   *registry;    /* borrowed */
    osal_mutex_t         *mutex;
    nop_addcamera_state_t state;
    int                   added_count;
    uint64_t              window_start_ms;
    uint64_t              window_ms;
};

nop_nvr_addcamera_t *nop_nvr_addcamera_create(nop_nvr_channels_t *registry)
{
    nop_nvr_addcamera_t *service;

    if (!registry)
        return NULL;
    service = (nop_nvr_addcamera_t *)nop_calloc(1, sizeof(*service));
    if (!service)
        return NULL;
    service->registry = registry;
    service->state    = NOP_ADDCAMERA_IDLE;
    service->mutex    = osal_mutex_create();
    if (!service->mutex) {
        nop_free(service);
        return NULL;
    }
    return service;
}

void nop_nvr_addcamera_destroy(nop_nvr_addcamera_t *service)
{
    if (!service)
        return;
    osal_mutex_destroy(service->mutex);
    nop_free(service);
}

/* Recompute expiry: if the window has elapsed, transition SCANNING -> DONE.
 * Caller holds the lock. */
static void refresh_state_locked(nop_nvr_addcamera_t *service, uint64_t now_ms)
{
    if (service->state != NOP_ADDCAMERA_SCANNING)
        return;
    if (now_ms >= service->window_start_ms &&
        now_ms - service->window_start_ms >= service->window_ms)
        service->state = NOP_ADDCAMERA_DONE;
}

nop_status_t nop_nvr_addcamera_start(nop_nvr_addcamera_t *service, int scan_seconds)
{
    if (!service)
        return NOP_ERR_PARAM;
    if (scan_seconds <= 0)
        scan_seconds = NOP_ADDCAMERA_DEFAULT_SECONDS;

    osal_mutex_lock(service->mutex);
    refresh_state_locked(service, osal_time_ms());
    if (service->state == NOP_ADDCAMERA_SCANNING) {
        osal_mutex_unlock(service->mutex);
        return NOP_ERR_STATE;          /* a window is already open */
    }
    service->state           = NOP_ADDCAMERA_SCANNING;
    service->added_count     = 0;
    service->window_start_ms = osal_time_ms();
    service->window_ms       = (uint64_t)scan_seconds * 1000u;
    osal_mutex_unlock(service->mutex);
    return NOP_OK;
}

nop_status_t nop_nvr_addcamera_stop(nop_nvr_addcamera_t *service)
{
    if (!service)
        return NOP_ERR_PARAM;
    osal_mutex_lock(service->mutex);
    service->state = NOP_ADDCAMERA_IDLE;
    osal_mutex_unlock(service->mutex);
    return NOP_OK;
}

void nop_nvr_addcamera_status(nop_nvr_addcamera_t *service, nop_addcamera_status_t *out)
{
    uint64_t now_ms;

    if (!out)
        return;
    if (!service) {
        out->state = NOP_ADDCAMERA_IDLE;
        out->added_count = 0;
        out->seconds_remaining = 0;
        return;
    }
    now_ms = osal_time_ms();
    osal_mutex_lock(service->mutex);
    refresh_state_locked(service, now_ms);
    out->state       = service->state;
    out->added_count = service->added_count;
    if (service->state == NOP_ADDCAMERA_SCANNING) {
        uint64_t elapsed = now_ms - service->window_start_ms;
        uint64_t left_ms = service->window_ms > elapsed ? service->window_ms - elapsed : 0;
        out->seconds_remaining = (int)(left_ms / 1000u);
    } else {
        out->seconds_remaining = 0;
    }
    osal_mutex_unlock(service->mutex);
}

int nop_nvr_addcamera_offer(nop_nvr_addcamera_t *service,
                            const nop_nvr_channel_entry_t *found)
{
    int channel;

    if (!service || !found)
        return -1;
    osal_mutex_lock(service->mutex);
    refresh_state_locked(service, osal_time_ms());
    if (service->state != NOP_ADDCAMERA_SCANNING) {
        osal_mutex_unlock(service->mutex);
        return -1;                     /* window closed: ignore late discoveries */
    }
    osal_mutex_unlock(service->mutex);

    /* Admit outside our lock: the registry has its own lock, and a duplicate
     * host+port returns the existing channel without adding. */
    channel = nop_nvr_channels_add(service->registry, found);
    if (channel < 0)
        return -1;

    osal_mutex_lock(service->mutex);
    if (service->state == NOP_ADDCAMERA_SCANNING)
        service->added_count++;
    osal_mutex_unlock(service->mutex);
    return channel;
}
