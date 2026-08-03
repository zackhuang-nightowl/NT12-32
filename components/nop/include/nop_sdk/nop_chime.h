/**
 * @file nop_chime.h
 * @brief Wireless chime registry + pairing-window state machine, backing the
 *        getWirelessChimes / setWirelessChimes / ringWirelessChimes /
 *        startAddWirelessChime / stopAddWirelessChime / startRemoveWirelessChime
 *        commands.
 *
 * A device (doorbell / BaseStation / NVR) pairs wireless chimes in a timed
 * window: while it is open, the RF layer (firmware) reports each chime it finds
 * by calling nop_chime_offer(), which admits it into the registry. This service
 * owns the pairing window + the paired-chime table (id / name / volume / tone);
 * the actual RF pairing and ring transmission are the firmware's job.
 */
#ifndef NOP_SDK_CHIME_H
#define NOP_SDK_CHIME_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One paired wireless chime. Zero-initialize, then set at least id. */
typedef struct nop_chime_entry {
    char id[32];        /**< chime identifier (required, dedup key) */
    char name[48];
    int  volume;        /**< 0..100 */
    int  tone_id;       /**< selected chime tone */
} nop_chime_entry_t;

/** Pairing-window state. */
typedef enum nop_chime_pair_state {
    NOP_CHIME_IDLE = 0,
    NOP_CHIME_PAIRING,
    NOP_CHIME_DONE
} nop_chime_pair_state_t;

/** Opaque handle. */
typedef struct nop_chime nop_chime_t;

/** Create a registry holding up to @p max_chimes. NULL on bad arg / alloc fail. */
nop_chime_t *nop_chime_create(int max_chimes);

/** Destroy. NULL-safe. */
void nop_chime_destroy(nop_chime_t *chime);

/* ---- pairing window ---- */

/** Open a pairing window of @p seconds (<=0 => 60). @return NOP_OK, or
 *  NOP_ERR_STATE if a window is already open. */
nop_status_t nop_chime_pair_start(nop_chime_t *chime, int seconds);

/** Close the pairing window early. @return NOP_OK (idempotent). */
nop_status_t nop_chime_pair_stop(nop_chime_t *chime);

/** Report the pairing status (any out-pointer may be NULL). */
void nop_chime_pair_status(nop_chime_t *chime, nop_chime_pair_state_t *state,
                           int *added_count, int *seconds_remaining);

/** Offer a discovered chime for admission (only while pairing). @return the
 *  chime slot index (>= 0), or -1 if not pairing / window elapsed / full. */
int nop_chime_offer(nop_chime_t *chime, const nop_chime_entry_t *found);

/* ---- registry ---- */

/** Copy up to @p max paired chimes into @p out. @return the count. */
int nop_chime_list(nop_chime_t *chime, nop_chime_entry_t *out, int max);

/** @return the number of paired chimes. */
int nop_chime_count(nop_chime_t *chime);

/** Update volume + tone of the chime with @p id. @return NOP_OK or NOP_ERR_NOTFOUND. */
nop_status_t nop_chime_set(nop_chime_t *chime, const char *id, int volume, int tone_id);

/** Remove the chime with @p id. @return NOP_OK or NOP_ERR_NOTFOUND. */
nop_status_t nop_chime_remove(nop_chime_t *chime, const char *id);

/** @return non-zero if a chime with @p id is paired. */
int nop_chime_find(nop_chime_t *chime, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_CHIME_H */
