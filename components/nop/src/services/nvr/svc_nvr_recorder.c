/**
 * @file svc_nvr_recorder.c
 * @brief videoRecorder multi-channel recording manager: one per-channel
 *        nop_record_t engine, spread across storage disks, all fed by the
 *        shared media plane and detection hub. See nop_sdk/nop_nvr_recorder.h.
 */
#include "nop_sdk/nop_nvr_recorder.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_storage.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

/* One managed channel: its recording engine and the disk it was placed on. */
typedef struct nop_nvr_channel {
    int           channel;
    int           active;
    nop_record_t *recorder;
    char          storage_root[256];
} nop_nvr_channel_t;

struct nop_nvr_recorder {
    nop_nvr_recorder_config_t config;      /* copied; read-only after create */
    osal_mutex_t             *mutex;
    nop_media_t              *media;        /* borrowed */
    nop_event_hub_t          *event_hub;   /* borrowed (may be NULL) */
    nop_nvr_channel_t         channels[NOP_NVR_MAX_CHANNELS];
    int                       channel_count;
    int                       next_disk;    /* round-robin disk cursor */
};

nop_nvr_recorder_t *nop_nvr_recorder_create(const nop_nvr_recorder_config_t *config,
                                            nop_media_t *media, nop_event_hub_t *hub)
{
    nop_nvr_recorder_t *manager;

    if (!config || !media || config->storage_root_count <= 0)
        return NULL;

    manager = (nop_nvr_recorder_t *)nop_calloc(1, sizeof(*manager));
    if (!manager)
        return NULL;
    manager->config = *config;
    if (manager->config.storage_root_count > NOP_NVR_MAX_DISKS)
        manager->config.storage_root_count = NOP_NVR_MAX_DISKS;
    manager->media     = media;
    manager->event_hub = hub;
    manager->mutex     = osal_mutex_create();
    if (!manager->mutex) {
        nop_free(manager);
        return NULL;
    }
    return manager;
}

/* Find the slot index for @p channel, or -1. Caller holds the lock. */
static int nvr_find_channel_locked(const nop_nvr_recorder_t *manager, int channel)
{
    int i;
    for (i = 0; i < NOP_NVR_MAX_CHANNELS; i++)
        if (manager->channels[i].active && manager->channels[i].channel == channel)
            return i;
    return -1;
}

/* Find a free slot index, or -1 if the table is full. Caller holds the lock. */
static int nvr_free_slot_locked(const nop_nvr_recorder_t *manager)
{
    int i;
    for (i = 0; i < NOP_NVR_MAX_CHANNELS; i++)
        if (!manager->channels[i].active)
            return i;
    return -1;
}

/* Choose the storage-root index for the next channel. Caller holds the lock.
 * Capacity-aware: the configured disk whose paired HAL_STORAGE volume is READY
 * and has the most free space; falls back to round-robin (advancing the cursor)
 * when disabled, HAL is absent, or no volume matches. */
static int nvr_select_root_index_locked(nop_nvr_recorder_t *manager)
{
    if (manager->config.capacity_aware_placement) {
        const hal_storage_if *storage =
            (const hal_storage_if *)hal_registry_get(HAL_STORAGE);
        if (storage && storage->volume_count && storage->get_volume_info) {
            int       volume_count = storage->volume_count(storage->ctx);
            int       best_root = -1, i;
            long long best_free = -1;
            for (i = 0; i < manager->config.storage_root_count; i++) {
                const char *want = manager->config.storage_volume_names[i];
                int         j;
                if (!want[0])
                    continue;
                for (j = 0; j < volume_count; j++) {
                    hal_storage_volume_t volume;
                    if (storage->get_volume_info(storage->ctx, j, &volume) != NOP_OK)
                        continue;
                    if (volume.status != HAL_STORAGE_STATUS_READY)
                        continue;
                    if (strncmp(volume.name, want, sizeof(volume.name)) != 0)
                        continue;
                    if (volume.free_size_kilobytes > best_free) {
                        best_free = volume.free_size_kilobytes;
                        best_root = i;
                    }
                }
            }
            if (best_root >= 0)
                return best_root;
        }
    }
    /* Round-robin fallback. */
    {
        int index = manager->next_disk % manager->config.storage_root_count;
        manager->next_disk = (manager->next_disk + 1) % manager->config.storage_root_count;
        return index;
    }
}

nop_status_t nop_nvr_recorder_add_channel(nop_nvr_recorder_t *manager, int channel)
{
    nop_record_config_t rec_config;
    nop_nvr_channel_t  *slot;
    char                chosen_root[256];
    int                 slot_index;
    nop_record_t       *recorder;

    if (!manager)
        return NOP_ERR_PARAM;

    osal_mutex_lock(manager->mutex);
    if (nvr_find_channel_locked(manager, channel) >= 0) {
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_CONFLICT;
    }
    slot_index = nvr_free_slot_locked(manager);
    if (slot_index < 0) {
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_STATE;      /* channel table full */
    }

    /* Choose the disk (capacity-aware or round-robin); copy the root to a local
     * so later writes into the manager can't be seen as aliasing the source. */
    snprintf(chosen_root, sizeof(chosen_root), "%s",
             manager->config.storage_roots[nvr_select_root_index_locked(manager)]);

    memset(&rec_config, 0, sizeof(rec_config));
    snprintf(rec_config.storage_dir, sizeof(rec_config.storage_dir), "%s", chosen_root);
    rec_config.channel             = channel;
    rec_config.stream              = manager->config.stream;
    rec_config.mode                = manager->config.mode;
    rec_config.segment_seconds     = manager->config.segment_seconds;
    rec_config.max_segments        = manager->config.max_segments_per_channel;
    rec_config.pre_record_seconds  = manager->config.pre_record_seconds;
    rec_config.post_record_seconds = manager->config.post_record_seconds;
    rec_config.event_max_seconds   = manager->config.event_max_seconds;

    recorder = nop_record_create(&rec_config);
    if (!recorder) {
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_STATE;
    }
    if (nop_record_attach_media(recorder, manager->media) != NOP_OK) {
        nop_record_destroy(recorder);
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_STATE;
    }
    if (manager->event_hub &&
        nop_record_attach_event_hub(recorder, manager->event_hub) != NOP_OK) {
        nop_record_destroy(recorder);
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_STATE;
    }

    slot = &manager->channels[slot_index];
    slot->channel  = channel;
    slot->active   = 1;
    slot->recorder = recorder;
    snprintf(slot->storage_root, sizeof(slot->storage_root), "%s", chosen_root);
    manager->channel_count++;
    osal_mutex_unlock(manager->mutex);
    return NOP_OK;
}

nop_status_t nop_nvr_recorder_remove_channel(nop_nvr_recorder_t *manager, int channel)
{
    nop_record_t *recorder;
    int           slot_index;

    if (!manager)
        return NOP_ERR_PARAM;

    osal_mutex_lock(manager->mutex);
    slot_index = nvr_find_channel_locked(manager, channel);
    if (slot_index < 0) {
        osal_mutex_unlock(manager->mutex);
        return NOP_ERR_NOTFOUND;
    }
    /* Detach from the slot before dropping the lock so a concurrent query can't
     * see a half-freed engine; destroy the engine outside the lock (it drains
     * in-flight media/event sinks, which may block briefly). */
    recorder = manager->channels[slot_index].recorder;
    memset(&manager->channels[slot_index], 0, sizeof(manager->channels[slot_index]));
    manager->channel_count--;
    osal_mutex_unlock(manager->mutex);

    nop_record_destroy(recorder);
    return NOP_OK;
}

void nop_nvr_recorder_destroy(nop_nvr_recorder_t *manager)
{
    nop_record_t *pending[NOP_NVR_MAX_CHANNELS];
    int           pending_count = 0, i;

    if (!manager)
        return;

    /* Snapshot the engines under the lock, then destroy them outside it. */
    osal_mutex_lock(manager->mutex);
    for (i = 0; i < NOP_NVR_MAX_CHANNELS; i++) {
        if (manager->channels[i].active) {
            pending[pending_count++] = manager->channels[i].recorder;
            memset(&manager->channels[i], 0, sizeof(manager->channels[i]));
        }
    }
    manager->channel_count = 0;
    osal_mutex_unlock(manager->mutex);

    for (i = 0; i < pending_count; i++)
        nop_record_destroy(pending[i]);

    osal_mutex_destroy(manager->mutex);
    nop_free(manager);
}

int nop_nvr_recorder_channel_count(const nop_nvr_recorder_t *manager)
{
    int count;
    if (!manager)
        return 0;
    osal_mutex_lock(manager->mutex);
    count = manager->channel_count;
    osal_mutex_unlock(manager->mutex);
    return count;
}

int nop_nvr_recorder_segments(nop_nvr_recorder_t *manager, int channel,
                              nop_record_segment_t *out, int max)
{
    nop_record_t *recorder = NULL;
    int           slot_index;

    if (!manager || !out || max <= 0)
        return 0;
    osal_mutex_lock(manager->mutex);
    slot_index = nvr_find_channel_locked(manager, channel);
    if (slot_index >= 0)
        recorder = manager->channels[slot_index].recorder;
    osal_mutex_unlock(manager->mutex);

    /* nop_record_segments is itself thread-safe; the engine is not freed while
     * a channel remains active (remove/destroy clear the slot first). */
    if (!recorder)
        return 0;
    return nop_record_segments(recorder, out, max);
}

int nop_nvr_recorder_channel_storage(nop_nvr_recorder_t *manager, int channel,
                                     char *out, size_t out_cap)
{
    int found = 0, slot_index;

    if (!manager || !out || out_cap == 0)
        return 0;
    osal_mutex_lock(manager->mutex);
    slot_index = nvr_find_channel_locked(manager, channel);
    if (slot_index >= 0) {
        snprintf(out, out_cap, "%s", manager->channels[slot_index].storage_root);
        found = 1;
    }
    osal_mutex_unlock(manager->mutex);
    return found;
}
