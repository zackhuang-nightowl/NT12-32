/**
 * @file test_util.h
 * @brief Tiny assertion harness for the ctest suite (no external deps).
 */
#ifndef NOP_TEST_UTIL_H
#define NOP_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static int g_test_fails = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        g_test_fails++;                                                    \
    }                                                                     \
} while (0)

#define CHECK_EQ_INT(a, b) do {                                            \
    long _a = (long)(a), _b = (long)(b);                                  \
    if (_a != _b) {                                                        \
        fprintf(stderr, "  FAIL %s:%d: %s (%ld) != %s (%ld)\n",            \
                __FILE__, __LINE__, #a, _a, #b, _b);                       \
        g_test_fails++;                                                    \
    }                                                                     \
} while (0)

#define CHECK_STR(a, b) do {                                               \
    const char *_a = (a), *_b = (b);                                      \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                              \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n",                \
                __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
        g_test_fails++;                                                    \
    }                                                                     \
} while (0)

#define TEST_RETURN() return (g_test_fails == 0) ? 0 : 1

/* Helpers to inspect a response envelope string. */
static int resp_status(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *sc;
    int    v = -1;
    if (root) {
        sc = cJSON_GetObjectItemCaseSensitive(root, "statusCode");
        if (cJSON_IsNumber(sc))
            v = sc->valueint;
        cJSON_Delete(root);
    }
    return v;
}

/* Returns a malloc'd copy of content.<field> string, or NULL. Caller frees. */
static char *resp_content_str(const char *json, const char *field)
{
    cJSON *root = cJSON_Parse(json);
    char  *out = NULL;
    if (root) {
        cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *f = c ? cJSON_GetObjectItemCaseSensitive(c, field) : NULL;
        if (f && cJSON_IsString(f) && f->valuestring) {
            size_t n = strlen(f->valuestring) + 1;
            out = (char *)malloc(n);
            if (out)
                memcpy(out, f->valuestring, n);
        }
        cJSON_Delete(root);
    }
    return out;
}

#endif /* NOP_TEST_UTIL_H */
