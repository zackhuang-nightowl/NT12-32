/**
 * @file osal_posix.c
 * @brief POSIX OSAL port (Linux glibc/uClibc, macOS). Selected by
 *        NOP_OSAL_PORT=posix (default) or linux_uclibc.
 */
#include "nop_sdk/osal/osal.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct osal_mutex {
    pthread_mutex_t m;
};

osal_mutex_t *osal_mutex_create(void)
{
    osal_mutex_t       *mutex;
    pthread_mutexattr_t attributes;

    mutex = (osal_mutex_t *)malloc(sizeof(*mutex));
    if (!mutex)
        return NULL;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&mutex->m, &attributes) != 0) {
        pthread_mutexattr_destroy(&attributes);
        free(mutex);
        return NULL;
    }
    pthread_mutexattr_destroy(&attributes);
    return mutex;
}

void osal_mutex_destroy(osal_mutex_t *m)
{
    if (!m)
        return;
    pthread_mutex_destroy(&m->m);
    free(m);
}

void osal_mutex_lock(osal_mutex_t *m)
{
    if (m)
        pthread_mutex_lock(&m->m);
}

void osal_mutex_unlock(osal_mutex_t *m)
{
    if (m)
        pthread_mutex_unlock(&m->m);
}

uint64_t osal_time_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void osal_sleep_ms(uint32_t milliseconds)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(milliseconds / 1000u);
    ts.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}
