#include "nop_mem.h"

#include <stdlib.h>
#include <string.h>

void *nop_malloc(size_t n)            { return malloc(n); }
void *nop_calloc(size_t n, size_t sz) { return calloc(n, sz); }
void *nop_realloc(void *p, size_t n)  { return realloc(p, n); }
void  nop_free(void *p)               { free(p); }

char *nop_strdup(const char *s)
{
    size_t n;
    char  *d;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}
