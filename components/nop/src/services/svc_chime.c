/**
 * @file svc_chime.c
 * @brief Wireless chime registry + pairing-window state machine.
 *        See nop_sdk/nop_chime.h.
 */
#include "nop_sdk/nop_chime.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

#define NOP_CHIME_DEFAULT_PAIR_SECONDS 60

typedef struct {
    int               used;
    nop_chime_entry_t entry;
} nop_chime_slot_t;

struct nop_chime {
    osal_mutex_t          *mutex;
    nop_chime_slot_t      *slots;
    int                    capacity;
    int                    count;
    nop_chime_pair_state_t state;
    int                    added_count;
    uint64_t               window_start_ms;
    uint64_t               window_ms;
};

nop_chime_t *nop_chime_create(int max_chimes)
{
    nop_chime_t *chime;
    if (max_chimes <= 0)
        return NULL;
    chime = (nop_chime_t *)nop_calloc(1, sizeof(*chime));
    if (!chime)
        return NULL;
    chime->slots = (nop_chime_slot_t *)nop_calloc((size_t)max_chimes, sizeof(nop_chime_slot_t));
    if (!chime->slots) {
        nop_free(chime);
        return NULL;
    }
    chime->mutex = osal_mutex_create();
    if (!chime->mutex) {
        nop_free(chime->slots);
        nop_free(chime);
        return NULL;
    }
    chime->capacity = max_chimes;
    return chime;
}

void nop_chime_destroy(nop_chime_t *chime)
{
    if (!chime)
        return;
    osal_mutex_destroy(chime->mutex);
    nop_free(chime->slots);
    nop_free(chime);
}

/* Force NUL-termination of the id/name fields (a caller may fill id[32] fully
 * with no terminator; downstream JSON/strcmp use requires C strings). */
static void chime_terminate(nop_chime_entry_t *entry)
{
    entry->id[sizeof(entry->id) - 1]     = '\0';
    entry->name[sizeof(entry->name) - 1] = '\0';
}

/* Find the slot for @p id, or -1. Caller holds the lock. */
static int chime_find_locked(nop_chime_t *chime, const char *id)
{
    int i;
    if (!id || id[0] == '\0')
        return -1;
    for (i = 0; i < chime->capacity; ++i)
        if (chime->slots[i].used &&
            strncmp(chime->slots[i].entry.id, id, sizeof(chime->slots[i].entry.id)) == 0)
            return i;
    return -1;
}

/* Transition PAIRING -> DONE when the window elapses. Caller holds the lock. */
static void chime_refresh_locked(nop_chime_t *chime, uint64_t now_ms)
{
    if (chime->state == NOP_CHIME_PAIRING &&
        now_ms >= chime->window_start_ms &&
        now_ms - chime->window_start_ms >= chime->window_ms)
        chime->state = NOP_CHIME_DONE;
}

nop_status_t nop_chime_pair_start(nop_chime_t *chime, int seconds)
{
    if (!chime)
        return NOP_ERR_PARAM;
    if (seconds <= 0)
        seconds = NOP_CHIME_DEFAULT_PAIR_SECONDS;
    osal_mutex_lock(chime->mutex);
    chime_refresh_locked(chime, osal_time_ms());
    if (chime->state == NOP_CHIME_PAIRING) {
        osal_mutex_unlock(chime->mutex);
        return NOP_ERR_STATE;
    }
    chime->state           = NOP_CHIME_PAIRING;
    chime->added_count     = 0;
    chime->window_start_ms = osal_time_ms();
    chime->window_ms       = (uint64_t)seconds * 1000u;
    osal_mutex_unlock(chime->mutex);
    return NOP_OK;
}

nop_status_t nop_chime_pair_stop(nop_chime_t *chime)
{
    if (!chime)
        return NOP_ERR_PARAM;
    osal_mutex_lock(chime->mutex);
    chime->state = NOP_CHIME_IDLE;
    osal_mutex_unlock(chime->mutex);
    return NOP_OK;
}

void nop_chime_pair_status(nop_chime_t *chime, nop_chime_pair_state_t *state,
                           int *added_count, int *seconds_remaining)
{
    uint64_t now_ms;
    if (!chime) {
        if (state) *state = NOP_CHIME_IDLE;
        if (added_count) *added_count = 0;
        if (seconds_remaining) *seconds_remaining = 0;
        return;
    }
    now_ms = osal_time_ms();
    osal_mutex_lock(chime->mutex);
    chime_refresh_locked(chime, now_ms);
    if (state) *state = chime->state;
    if (added_count) *added_count = chime->added_count;
    if (seconds_remaining) {
        if (chime->state == NOP_CHIME_PAIRING) {
            uint64_t elapsed = now_ms - chime->window_start_ms;
            uint64_t left = chime->window_ms > elapsed ? chime->window_ms - elapsed : 0;
            *seconds_remaining = (int)(left / 1000u);
        } else {
            *seconds_remaining = 0;
        }
    }
    osal_mutex_unlock(chime->mutex);
}

int nop_chime_offer(nop_chime_t *chime, const nop_chime_entry_t *found)
{
    int slot_index = -1, i;
    if (!chime || !found || found->id[0] == '\0')
        return -1;
    osal_mutex_lock(chime->mutex);
    chime_refresh_locked(chime, osal_time_ms());
    if (chime->state != NOP_CHIME_PAIRING) {
        osal_mutex_unlock(chime->mutex);
        return -1;                       /* window closed */
    }
    i = chime_find_locked(chime, found->id);
    if (i >= 0) {                        /* already paired — refresh in place */
        chime->slots[i].entry = *found;
        chime_terminate(&chime->slots[i].entry);
        osal_mutex_unlock(chime->mutex);
        return i;
    }
    for (i = 0; i < chime->capacity; ++i)
        if (!chime->slots[i].used) { slot_index = i; break; }
    if (slot_index < 0) {
        osal_mutex_unlock(chime->mutex);
        return -1;                       /* full */
    }
    chime->slots[slot_index].used  = 1;
    chime->slots[slot_index].entry = *found;
    chime_terminate(&chime->slots[slot_index].entry);   /* defend against unterminated caller strings */
    chime->count++;
    chime->added_count++;
    osal_mutex_unlock(chime->mutex);
    return slot_index;
}

int nop_chime_list(nop_chime_t *chime, nop_chime_entry_t *out, int max)
{
    int written = 0, i;
    if (!chime || !out || max <= 0)
        return 0;
    osal_mutex_lock(chime->mutex);
    for (i = 0; i < chime->capacity && written < max; ++i)
        if (chime->slots[i].used)
            out[written++] = chime->slots[i].entry;
    osal_mutex_unlock(chime->mutex);
    return written;
}

int nop_chime_count(nop_chime_t *chime)
{
    int count;
    if (!chime)
        return 0;
    osal_mutex_lock(chime->mutex);
    count = chime->count;
    osal_mutex_unlock(chime->mutex);
    return count;
}

nop_status_t nop_chime_set(nop_chime_t *chime, const char *id, int volume, int tone_id)
{
    int i;
    if (!chime || !id)
        return NOP_ERR_PARAM;
    osal_mutex_lock(chime->mutex);
    i = chime_find_locked(chime, id);
    if (i >= 0) {
        chime->slots[i].entry.volume  = volume;
        chime->slots[i].entry.tone_id = tone_id;
    }
    osal_mutex_unlock(chime->mutex);
    return i >= 0 ? NOP_OK : NOP_ERR_NOTFOUND;
}

nop_status_t nop_chime_remove(nop_chime_t *chime, const char *id)
{
    int i;
    if (!chime || !id)
        return NOP_ERR_PARAM;
    osal_mutex_lock(chime->mutex);
    i = chime_find_locked(chime, id);
    if (i >= 0) {
        memset(&chime->slots[i], 0, sizeof(chime->slots[i]));
        chime->count--;
    }
    osal_mutex_unlock(chime->mutex);
    return i >= 0 ? NOP_OK : NOP_ERR_NOTFOUND;
}

int nop_chime_find(nop_chime_t *chime, const char *id)
{
    int found;
    if (!chime)
        return 0;
    osal_mutex_lock(chime->mutex);
    found = (chime_find_locked(chime, id) >= 0);
    osal_mutex_unlock(chime->mutex);
    return found;
}
