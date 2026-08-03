/**
 * @file nop_mem.h  (internal)
 * @brief Allocation wrappers. Centralized so a future static/pool backend can
 *        replace malloc without touching call sites. Embedded rule: do not
 *        allocate on hot paths.
 */
#ifndef NOP_BASE_MEM_H
#define NOP_BASE_MEM_H

#include <stddef.h>

void *nop_malloc(size_t n);
void *nop_calloc(size_t n, size_t sz);
void *nop_realloc(void *p, size_t n);
void  nop_free(void *p);
char *nop_strdup(const char *s);

#endif /* NOP_BASE_MEM_H */
