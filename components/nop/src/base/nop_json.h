/**
 * @file nop_json.h  (internal)
 * @brief Thin facade over cJSON. ALL NOP code goes through this header so the
 *        third-party JSON library stays isolated and replaceable. Business code
 *        must not #include cJSON.h directly.
 */
#ifndef NOP_BASE_JSON_H
#define NOP_BASE_JSON_H

#include "cJSON.h"

#include <stdbool.h>
#include <stddef.h>

typedef cJSON nop_json_t;

/* ---- parse / print -------------------------------------------------------- */
nop_json_t *nop_json_parse(const char *text, size_t len);
char       *nop_json_print(const nop_json_t *j);          /* compact; caller frees */
void        nop_json_free(nop_json_t *j);

/* ---- object construction -------------------------------------------------- */
nop_json_t *nop_json_obj(void);
nop_json_t *nop_json_arr(void);
void        nop_json_add(nop_json_t *obj, const char *key, nop_json_t *val);
void        nop_json_add_str(nop_json_t *obj, const char *key, const char *val);
void        nop_json_add_int(nop_json_t *obj, const char *key, double val);
void        nop_json_add_bool(nop_json_t *obj, const char *key, bool val);
void        nop_json_arr_push(nop_json_t *arr, nop_json_t *val);
void        nop_json_arr_push_str(nop_json_t *arr, const char *val);
void        nop_json_arr_push_int(nop_json_t *arr, double val);

/* ---- safe field access (return defaults on miss/type mismatch) ------------ */
nop_json_t *nop_json_get(const nop_json_t *obj, const char *key);
const char *nop_json_str(const nop_json_t *obj, const char *key, const char *dflt);
double      nop_json_num(const nop_json_t *obj, const char *key, double dflt);
bool        nop_json_bool(const nop_json_t *obj, const char *key, bool dflt);
bool        nop_json_has(const nop_json_t *obj, const char *key);
bool        nop_json_is_arr(const nop_json_t *j);
int         nop_json_arr_size(const nop_json_t *arr);
nop_json_t *nop_json_arr_at(const nop_json_t *arr, int index);
const char *nop_json_as_str(const nop_json_t *node, const char *dflt);
double      nop_json_as_num(const nop_json_t *node, double dflt);

#endif /* NOP_BASE_JSON_H */
