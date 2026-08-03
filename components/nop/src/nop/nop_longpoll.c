#include "nop/nop_longpoll.h"
#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

struct nop_longpoll {
    uint32_t      pending_bits;
    osal_mutex_t *mutex;
};

nop_longpoll_t *nop_longpoll_create(void)
{
    nop_longpoll_t *longpoll = (nop_longpoll_t *)nop_calloc(1, sizeof(*longpoll));
    if (!longpoll)
        return NULL;
    longpoll->mutex = osal_mutex_create();
    return longpoll;
}

void nop_longpoll_destroy(nop_longpoll_t *longpoll)
{
    if (!longpoll)
        return;
    osal_mutex_destroy(longpoll->mutex);
    nop_free(longpoll);
}

void nop_longpoll_signal(nop_longpoll_t *longpoll, uint32_t bits)
{
    if (!longpoll)
        return;
    osal_mutex_lock(longpoll->mutex);
    longpoll->pending_bits |= bits;
    osal_mutex_unlock(longpoll->mutex);
}

uint32_t nop_longpoll_drain(nop_longpoll_t *longpoll)
{
    uint32_t bits;
    if (!longpoll)
        return 0;
    osal_mutex_lock(longpoll->mutex);
    bits = longpoll->pending_bits;
    longpoll->pending_bits = 0;
    osal_mutex_unlock(longpoll->mutex);
    return bits;
}
