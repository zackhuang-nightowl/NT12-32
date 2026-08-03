/**
 * @file nop_map.h  (internal)
 * @brief Small string-keyed hash map (djb2 + separate chaining). Keys are
 *        copied; values are borrowed void* owned by the caller. Replaces leo's
 *        long strcmp chains for O(1) command lookup.
 */
#ifndef NOP_BASE_MAP_H
#define NOP_BASE_MAP_H

#include <stddef.h>

typedef struct nop_map nop_map_t;

/** Iterator callback; return non-zero to stop iteration. */
typedef int (*nop_map_iter_fn)(const char *key, void *value, void *user);

nop_map_t *nop_map_create(size_t bucket_count); /**< 0 => default bucket count */
void       nop_map_destroy(nop_map_t *map);     /**< frees keys, not values    */

/** Insert/replace @p key -> @p value. Returns 0 on success, -1 on OOM. */
int   nop_map_put(nop_map_t *map, const char *key, void *value);
void *nop_map_get(const nop_map_t *map, const char *key);  /**< NULL if absent */
size_t nop_map_size(const nop_map_t *map);

/** Visit every entry until the callback returns non-zero. */
void  nop_map_foreach(const nop_map_t *map, nop_map_iter_fn callback, void *user);

#endif /* NOP_BASE_MAP_H */
