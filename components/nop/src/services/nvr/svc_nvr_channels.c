/**
 * @file svc_nvr_channels.c
 * @brief videoRecorder camera channel registry. Thread-safe table of bound
 *        sub-cameras. See nop_sdk/nop_nvr_channels.h.
 */
#include "nop_sdk/nop_nvr_channels.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>

typedef struct nop_nvr_channel_slot {
    int                     active;
    nop_nvr_channel_entry_t entry;
} nop_nvr_channel_slot_t;

struct nop_nvr_channels {
    osal_mutex_t           *mutex;
    nop_nvr_channel_slot_t *slots;
    int                     capacity;
    int                     count;
};

nop_nvr_channels_t *nop_nvr_channels_create(int max_channels)
{
    nop_nvr_channels_t *channels;

    if (max_channels <= 0)
        return NULL;
    channels = (nop_nvr_channels_t *)nop_calloc(1, sizeof(*channels));
    if (!channels)
        return NULL;
    channels->slots = (nop_nvr_channel_slot_t *)nop_calloc((size_t)max_channels,
                                                           sizeof(nop_nvr_channel_slot_t));
    if (!channels->slots) {
        nop_free(channels);
        return NULL;
    }
    channels->capacity = max_channels;
    channels->mutex = osal_mutex_create();
    if (!channels->mutex) {
        nop_free(channels->slots);
        nop_free(channels);
        return NULL;
    }
    return channels;
}

void nop_nvr_channels_destroy(nop_nvr_channels_t *channels)
{
    if (!channels)
        return;
    osal_mutex_destroy(channels->mutex);
    nop_free(channels->slots);
    nop_free(channels);
}

/* Find the slot bound to host+port, or -1. Caller holds the lock. */
static int find_by_host_locked(nop_nvr_channels_t *channels, const char *host, int port)
{
    int i;
    for (i = 0; i < channels->capacity; i++) {
        if (channels->slots[i].active &&
            channels->slots[i].entry.port == port &&
            strncmp(channels->slots[i].entry.host, host, sizeof(channels->slots[i].entry.host)) == 0)
            return i;
    }
    return -1;
}

/* Find the slot for a channel index, or -1. Caller holds the lock. */
static int find_by_channel_locked(nop_nvr_channels_t *channels, int channel)
{
    int i;
    for (i = 0; i < channels->capacity; i++)
        if (channels->slots[i].active && channels->slots[i].entry.channel == channel)
            return i;
    return -1;
}

/* @return 1 if @p channel is already taken by an active slot. Caller holds lock. */
static int channel_in_use_locked(nop_nvr_channels_t *channels, int channel)
{
    return find_by_channel_locked(channels, channel) >= 0;
}

int nop_nvr_channels_add(nop_nvr_channels_t *channels, const nop_nvr_channel_entry_t *entry)
{
    int slot_index = -1, assigned_channel, i;

    if (!channels || !entry || entry->host[0] == '\0')
        return -1;

    osal_mutex_lock(channels->mutex);

    /* Dedup: same host+port keeps its existing channel. */
    i = find_by_host_locked(channels, entry->host, entry->port);
    if (i >= 0) {
        int existing = channels->slots[i].entry.channel;
        osal_mutex_unlock(channels->mutex);
        return existing;
    }

    /* Choose the channel index: requested (if free) or the lowest free one. */
    if (entry->channel >= 0 && !channel_in_use_locked(channels, entry->channel)) {
        assigned_channel = entry->channel;
    } else {
        assigned_channel = 0;
        while (channel_in_use_locked(channels, assigned_channel))
            assigned_channel++;
    }

    for (i = 0; i < channels->capacity; i++) {
        if (!channels->slots[i].active) {
            slot_index = i;
            break;
        }
    }
    if (slot_index < 0) {
        osal_mutex_unlock(channels->mutex);
        return -1;                      /* table full */
    }

    channels->slots[slot_index].active        = 1;
    channels->slots[slot_index].entry         = *entry;
    channels->slots[slot_index].entry.channel = assigned_channel;
    channels->count++;
    osal_mutex_unlock(channels->mutex);
    return assigned_channel;
}

nop_status_t nop_nvr_channels_upsert(nop_nvr_channels_t *channels,
                                     const nop_nvr_channel_entry_t *entry)
{
    int slot_index, i;

    if (!channels || !entry || entry->host[0] == '\0' || entry->channel < 0)
        return NOP_ERR_PARAM;

    osal_mutex_lock(channels->mutex);

    slot_index = find_by_channel_locked(channels, entry->channel);
    if (slot_index >= 0) {
        channels->slots[slot_index].entry = *entry;
        osal_mutex_unlock(channels->mutex);
        return NOP_OK;
    }

    if (channel_in_use_locked(channels, entry->channel)) {
        osal_mutex_unlock(channels->mutex);
        return NOP_ERR_PARAM;
    }

    for (i = 0; i < channels->capacity; i++) {
        if (!channels->slots[i].active) {
            slot_index = i;
            break;
        }
    }
    if (slot_index < 0) {
        osal_mutex_unlock(channels->mutex);
        return NOP_ERR_NOMEM;
    }

    channels->slots[slot_index].active        = 1;
    channels->slots[slot_index].entry         = *entry;
    channels->slots[slot_index].entry.channel = entry->channel;
    channels->count++;
    osal_mutex_unlock(channels->mutex);
    return NOP_OK;
}

nop_status_t nop_nvr_channels_remove(nop_nvr_channels_t *channels, int channel)
{
    int slot_index;

    if (!channels)
        return NOP_ERR_PARAM;
    osal_mutex_lock(channels->mutex);
    slot_index = find_by_channel_locked(channels, channel);
    if (slot_index < 0) {
        osal_mutex_unlock(channels->mutex);
        return NOP_ERR_NOTFOUND;
    }
    memset(&channels->slots[slot_index], 0, sizeof(channels->slots[slot_index]));
    channels->count--;
    osal_mutex_unlock(channels->mutex);
    return NOP_OK;
}

int nop_nvr_channels_get(nop_nvr_channels_t *channels, int channel,
                         nop_nvr_channel_entry_t *out)
{
    int slot_index, found = 0;

    if (!channels || !out)
        return 0;
    osal_mutex_lock(channels->mutex);
    slot_index = find_by_channel_locked(channels, channel);
    if (slot_index >= 0) {
        *out = channels->slots[slot_index].entry;
        found = 1;
    }
    osal_mutex_unlock(channels->mutex);
    return found;
}

int nop_nvr_channels_list(nop_nvr_channels_t *channels,
                          nop_nvr_channel_entry_t *out, int max)
{
    int written = 0, i;

    if (!channels || !out || max <= 0)
        return 0;
    osal_mutex_lock(channels->mutex);
    /* Collect active entries, insertion-sorting by channel as we go. */
    for (i = 0; i < channels->capacity && written < max; i++) {
        int j;
        if (!channels->slots[i].active)
            continue;
        for (j = written; j > 0 && out[j - 1].channel > channels->slots[i].entry.channel; j--)
            out[j] = out[j - 1];
        out[j] = channels->slots[i].entry;
        written++;
    }
    osal_mutex_unlock(channels->mutex);
    return written;
}

int nop_nvr_channels_count(const nop_nvr_channels_t *channels)
{
    int count;
    if (!channels)
        return 0;
    osal_mutex_lock(channels->mutex);
    count = channels->count;
    osal_mutex_unlock(channels->mutex);
    return count;
}

int nop_nvr_channels_find(nop_nvr_channels_t *channels, const char *host, int port)
{
    int slot_index, result = -1;

    if (!channels || !host)
        return -1;
    osal_mutex_lock(channels->mutex);
    slot_index = find_by_host_locked(channels, host, port);
    if (slot_index >= 0)
        result = channels->slots[slot_index].entry.channel;
    osal_mutex_unlock(channels->mutex);
    return result;
}
