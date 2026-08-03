/**
 * @file nop_longpoll.h  (internal)
 * @brief GUI_longPolling support — an event bitmap the NVR raises and a polling
 *        client drains. N0 provides the non-blocking bitmap; the suspend/wake
 *        path is wired when the http_server transport lands (needs osal cond).
 */
#ifndef NOP_NOP_LONGPOLL_H
#define NOP_NOP_LONGPOLL_H

#include <stdint.h>

typedef struct nop_longpoll nop_longpoll_t;

nop_longpoll_t *nop_longpoll_create(void);
void            nop_longpoll_destroy(nop_longpoll_t *lp);

/** Raise event bits (OR into the pending mask), e.g. from an event thread. */
void     nop_longpoll_signal(nop_longpoll_t *longpoll, uint32_t bits);

/** Atomically read and clear the pending mask (what a poll returns). */
uint32_t nop_longpoll_drain(nop_longpoll_t *longpoll);

#endif /* NOP_NOP_LONGPOLL_H */
