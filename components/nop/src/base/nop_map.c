#include "base/nop_map.h"
#include "base/nop_mem.h"

#include <string.h>

#define NOP_MAP_DEFAULT_BUCKETS 64

typedef struct nop_map_node {
    char                 *key;
    void                 *value;
    struct nop_map_node  *next;
} nop_map_node;

struct nop_map {
    nop_map_node **buckets;
    size_t         bucket_count;
    size_t         entry_count;
};

static unsigned long hash_djb2(const char *key)
{
    unsigned long hash = 5381;
    int           character;
    while ((character = (unsigned char)*key++))
        hash = ((hash << 5) + hash) + (unsigned long)character;
    return hash;
}

nop_map_t *nop_map_create(size_t bucket_count)
{
    nop_map_t *map;
    if (bucket_count == 0)
        bucket_count = NOP_MAP_DEFAULT_BUCKETS;
    map = (nop_map_t *)nop_calloc(1, sizeof(*map));
    if (!map)
        return NULL;
    map->buckets = (nop_map_node **)nop_calloc(bucket_count, sizeof(nop_map_node *));
    if (!map->buckets) {
        nop_free(map);
        return NULL;
    }
    map->bucket_count = bucket_count;
    return map;
}

void nop_map_destroy(nop_map_t *map)
{
    size_t bucket_index;
    if (!map)
        return;
    for (bucket_index = 0; bucket_index < map->bucket_count; bucket_index++) {
        nop_map_node *node = map->buckets[bucket_index];
        while (node) {
            nop_map_node *next_node = node->next;
            nop_free(node->key);
            nop_free(node);
            node = next_node;
        }
    }
    nop_free(map->buckets);
    nop_free(map);
}

int nop_map_put(nop_map_t *map, const char *key, void *value)
{
    size_t        bucket_index;
    nop_map_node *node;
    if (!map || !key)
        return -1;
    bucket_index = hash_djb2(key) % map->bucket_count;
    for (node = map->buckets[bucket_index]; node; node = node->next) {
        if (strcmp(node->key, key) == 0) {
            node->value = value;   /* replace */
            return 0;
        }
    }
    node = (nop_map_node *)nop_malloc(sizeof(*node));
    if (!node)
        return -1;
    node->key = nop_strdup(key);
    if (!node->key) {
        nop_free(node);
        return -1;
    }
    node->value = value;
    node->next = map->buckets[bucket_index];
    map->buckets[bucket_index] = node;
    map->entry_count++;
    return 0;
}

void *nop_map_get(const nop_map_t *map, const char *key)
{
    size_t        bucket_index;
    nop_map_node *node;
    if (!map || !key)
        return NULL;
    bucket_index = hash_djb2(key) % map->bucket_count;
    for (node = map->buckets[bucket_index]; node; node = node->next) {
        if (strcmp(node->key, key) == 0)
            return node->value;
    }
    return NULL;
}

size_t nop_map_size(const nop_map_t *map)
{
    return map ? map->entry_count : 0;
}

void nop_map_foreach(const nop_map_t *map, nop_map_iter_fn callback, void *user)
{
    size_t bucket_index;
    if (!map || !callback)
        return;
    for (bucket_index = 0; bucket_index < map->bucket_count; bucket_index++) {
        nop_map_node *node;
        for (node = map->buckets[bucket_index]; node; node = node->next) {
            if (callback(node->key, node->value, user))
                return;
        }
    }
}
