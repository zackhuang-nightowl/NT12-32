#include "nop_json.h"

nop_json_t *nop_json_parse(const char *text, size_t len)
{
    if (!text)
        return NULL;
    return cJSON_ParseWithLength(text, len);
}

char *nop_json_print(const nop_json_t *j)
{
    if (!j)
        return NULL;
    return cJSON_PrintUnformatted(j);
}

void nop_json_free(nop_json_t *j)
{
    cJSON_Delete(j);
}

nop_json_t *nop_json_obj(void) { return cJSON_CreateObject(); }
nop_json_t *nop_json_arr(void) { return cJSON_CreateArray(); }

void nop_json_add(nop_json_t *obj, const char *key, nop_json_t *val)
{
    if (obj && key && val)
        cJSON_AddItemToObject(obj, key, val);
}

void nop_json_add_str(nop_json_t *obj, const char *key, const char *val)
{
    if (obj && key)
        cJSON_AddStringToObject(obj, key, val ? val : "");
}

void nop_json_add_int(nop_json_t *obj, const char *key, double val)
{
    if (obj && key)
        cJSON_AddNumberToObject(obj, key, val);
}

void nop_json_add_bool(nop_json_t *obj, const char *key, bool val)
{
    if (obj && key)
        cJSON_AddBoolToObject(obj, key, val);
}

void nop_json_arr_push(nop_json_t *arr, nop_json_t *val)
{
    if (arr && val)
        cJSON_AddItemToArray(arr, val);
}

void nop_json_arr_push_str(nop_json_t *arr, const char *val)
{
    nop_json_t *s;
    if (!arr)
        return;
    s = cJSON_CreateString(val ? val : "");
    if (s)
        cJSON_AddItemToArray(arr, s);
}

void nop_json_arr_push_int(nop_json_t *arr, double val)
{
    nop_json_t *n;
    if (!arr)
        return;
    n = cJSON_CreateNumber(val);
    if (n)
        cJSON_AddItemToArray(arr, n);
}

nop_json_t *nop_json_get(const nop_json_t *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive((nop_json_t *)obj, key);
}

const char *nop_json_str(const nop_json_t *obj, const char *key, const char *dflt)
{
    nop_json_t *it = nop_json_get(obj, key);
    if (it && cJSON_IsString(it) && it->valuestring)
        return it->valuestring;
    return dflt;
}

double nop_json_num(const nop_json_t *obj, const char *key, double dflt)
{
    nop_json_t *it = nop_json_get(obj, key);
    if (it && cJSON_IsNumber(it))
        return it->valuedouble;
    return dflt;
}

bool nop_json_bool(const nop_json_t *obj, const char *key, bool dflt)
{
    nop_json_t *it = nop_json_get(obj, key);
    if (it && cJSON_IsBool(it))
        return cJSON_IsTrue(it) ? true : false;
    return dflt;
}

bool nop_json_has(const nop_json_t *obj, const char *key)
{
    return nop_json_get(obj, key) != NULL;
}

bool nop_json_is_arr(const nop_json_t *j)
{
    return cJSON_IsArray((nop_json_t *)j) ? true : false;
}

int nop_json_arr_size(const nop_json_t *arr)
{
    return cJSON_GetArraySize((nop_json_t *)arr);
}

nop_json_t *nop_json_arr_at(const nop_json_t *arr, int index)
{
    return cJSON_GetArrayItem((nop_json_t *)arr, index);
}

const char *nop_json_as_str(const nop_json_t *node, const char *dflt)
{
    if (node && cJSON_IsString((nop_json_t *)node) && node->valuestring)
        return node->valuestring;
    return dflt;
}

double nop_json_as_num(const nop_json_t *node, double dflt)
{
    if (node && cJSON_IsNumber((nop_json_t *)node))
        return node->valuedouble;
    return dflt;
}
