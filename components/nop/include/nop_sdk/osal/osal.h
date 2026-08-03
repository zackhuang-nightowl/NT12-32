/**
 * @file osal.h
 * @brief OS/platform porting contract (lwIP sys_arch style).
 *
 * Protocol and business layers call ONLY these abstractions, never raw OS APIs,
 * so the same sources build for arm-uClibc, x86 POSIX, and Windows. Each target
 * provides an implementation under ports/<platform>/ selected by NOP_OSAL_PORT.
 *
 * N0 scope: mutex + monotonic time (router/cap_registry/longpoll need a lock).
 * Threads/queues/sockets are added with the http_server and tutk backends.
 */
#ifndef NOP_SDK_OSAL_H
#define NOP_SDK_OSAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque recursive-capable mutex handle (NULL on failure). */
typedef struct osal_mutex osal_mutex_t;

osal_mutex_t *osal_mutex_create(void);
void          osal_mutex_destroy(osal_mutex_t *m);
void          osal_mutex_lock(osal_mutex_t *m);   /**< NULL-safe no-op */
void          osal_mutex_unlock(osal_mutex_t *m); /**< NULL-safe no-op */

/** @return monotonic milliseconds since an arbitrary epoch. */
uint64_t      osal_time_ms(void);

/** Sleep the calling thread for @p milliseconds. */
void          osal_sleep_ms(uint32_t milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_OSAL_H */
