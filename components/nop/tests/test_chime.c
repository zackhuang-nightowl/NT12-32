/**
 * @file test_chime.c
 * @brief Wireless chime registry + pairing window: pair-start/offer/stop,
 *        dedup by id, set volume/tone, list, remove, window expiry.
 */
#include "nop_sdk/nop_chime.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void make_chime(nop_chime_entry_t *e, const char *id, const char *name)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->id, sizeof(e->id), "%s", id);
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->volume = 50;
    e->tone_id = 1;
}

int main(void)
{
    nop_chime_t           *chime;
    nop_chime_entry_t      e, list[8];
    nop_chime_pair_state_t state;
    int                    added, remaining, n;

    chime = nop_chime_create(4);
    if (!chime) return fail("create");

    /* Offer before pairing is rejected. */
    make_chime(&e, "c1", "Kitchen");
    if (nop_chime_offer(chime, &e) != -1) return fail("offer before pairing");

    /* Open window, admit two distinct chimes; dedup a repeat. */
    if (nop_chime_pair_start(chime, 30) != NOP_OK) return fail("pair start");
    if (nop_chime_pair_start(chime, 30) != NOP_ERR_STATE) return fail("double start STATE");
    nop_chime_pair_status(chime, &state, &added, &remaining);
    if (state != NOP_CHIME_PAIRING || remaining <= 0) return fail("status pairing");

    make_chime(&e, "c1", "Kitchen"); if (nop_chime_offer(chime, &e) < 0) return fail("offer c1");
    make_chime(&e, "c2", "Garage");  if (nop_chime_offer(chime, &e) < 0) return fail("offer c2");
    make_chime(&e, "c1", "Kitchen"); nop_chime_offer(chime, &e);   /* dedup */
    if (nop_chime_count(chime) != 2) return fail("count should be 2 (deduped)");

    /* Configure + list. */
    if (nop_chime_set(chime, "c2", 80, 3) != NOP_OK) return fail("set c2");
    if (nop_chime_set(chime, "nope", 10, 0) != NOP_ERR_NOTFOUND) return fail("set unknown NOTFOUND");
    n = nop_chime_list(chime, list, 8);
    if (n != 2) return fail("list count");
    if (!nop_chime_find(chime, "c1") || !nop_chime_find(chime, "c2")) return fail("find paired");

    /* Remove + stop. */
    if (nop_chime_remove(chime, "c1") != NOP_OK) return fail("remove c1");
    if (nop_chime_count(chime) != 1) return fail("count after remove");
    if (nop_chime_remove(chime, "c1") != NOP_ERR_NOTFOUND) return fail("double remove NOTFOUND");
    nop_chime_pair_stop(chime);
    nop_chime_pair_status(chime, &state, NULL, NULL);
    if (state != NOP_CHIME_IDLE) return fail("idle after stop");
    make_chime(&e, "c3", "Den"); if (nop_chime_offer(chime, &e) != -1) return fail("offer after stop");

    /* Window expiry → DONE, no more admissions. */
    if (nop_chime_pair_start(chime, 1) != NOP_OK) return fail("start short");
    osal_sleep_ms(1100);
    nop_chime_pair_status(chime, &state, NULL, &remaining);
    if (state != NOP_CHIME_DONE || remaining != 0) return fail("window should elapse to DONE");
    make_chime(&e, "c4", "Attic"); if (nop_chime_offer(chime, &e) != -1) return fail("offer after expiry");

    nop_chime_destroy(chime);
    printf("test_chime: OK (pair/offer/dedup/set/list/remove/expiry)\n");
    return 0;
}
