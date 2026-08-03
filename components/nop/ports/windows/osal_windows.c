/**
 * @file osal_windows.c
 * @brief Windows OSAL port (Win32). Selected by NOP_OSAL_PORT=windows.
 */
#include "nop_sdk/osal/osal.h"

#include <windows.h>
#include <stdlib.h>

struct osal_mutex {
    CRITICAL_SECTION cs;   /* recursive by definition */
};

osal_mutex_t *osal_mutex_create(void)
{
    osal_mutex_t *mx = (osal_mutex_t *)malloc(sizeof(*mx));
    if (!mx)
        return NULL;
    InitializeCriticalSection(&mx->cs);
    return mx;
}

void osal_mutex_destroy(osal_mutex_t *m)
{
    if (!m)
        return;
    DeleteCriticalSection(&m->cs);
    free(m);
}

void osal_mutex_lock(osal_mutex_t *m)
{
    if (m)
        EnterCriticalSection(&m->cs);
}

void osal_mutex_unlock(osal_mutex_t *m)
{
    if (m)
        LeaveCriticalSection(&m->cs);
}

uint64_t osal_time_ms(void)
{
    return (uint64_t)GetTickCount64();
}

void osal_sleep_ms(uint32_t milliseconds)
{
    Sleep(milliseconds);
}
