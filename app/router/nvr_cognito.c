/***************************************************************************************
 *  nvr_cognito.c — Cognito InitiateAuth + JWT sub 解码。见 nvr_cognito.h。
 ***************************************************************************************/
#include "nvr_cognito.h"
#include "nvr_log.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(NVR_BUILD_STAGE)
/* Stage：Server_NOP_Account.md */
#  define NVR_COG_CLIENT_ID "5vmvkt4bh9ravj4kf0hnuqiq2t"
#else
/* Production embedded client（与 Tester InitiateAuth / README 一致） */
#  define NVR_COG_CLIENT_ID "7a7t4ds667njvemo0e6aobotce"
#endif

#define NVR_COG_URL "https://cognito-idp.us-east-1.amazonaws.com/"

typedef struct { char *buf; size_t len; } mem_t;

static size_t on_write(void *p, size_t sz, size_t n, void *u)
{
    size_t a = sz * n; mem_t *m = u;
    char *nb = realloc(m->buf, m->len + a + 1);
    if (!nb) return 0;
    m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0;
    return a;
}

static int b64_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* base64url → 原始字节；out 需足够大。返回写字节数，<0 失败。 */
static int b64url_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap)
{
    size_t i = 0, o = 0;
    unsigned val = 0; int valb = -8;
    for (; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        int d;
        if (c == '=') break;
        d = b64_val(c);
        if (d < 0) continue;
        val = (val << 6) + (unsigned)d;
        valb += 6;
        if (valb >= 0) {
            if (o >= out_cap) return -1;
            out[o++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return (int)o;
}

/* 从 IdToken 取 payload JSON 字段。 */
static int jwt_payload_str(const char *jwt, const char *key, char *out, size_t cap)
{
    if (!jwt || !key || !out || cap == 0) return -1;
    out[0] = 0;
    const char *p1 = strchr(jwt, '.');
    if (!p1) return -1;
    const char *p2 = strchr(p1 + 1, '.');
    if (!p2) return -1;
    size_t plen = (size_t)(p2 - (p1 + 1));
    unsigned char raw[4096];
    int n = b64url_decode(p1 + 1, plen, raw, sizeof(raw) - 1);
    if (n <= 0) return -1;
    raw[n] = 0;
    cJSON *j = cJSON_Parse((char *)raw);
    if (!j) return -1;
    const cJSON *v = cJSON_GetObjectItem(j, key);
    int ok = -1;
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(out, cap, "%s", v->valuestring);
        ok = 0;
    }
    cJSON_Delete(j);
    return ok;
}

nvr_cognito_rc_t nvr_cognito_login(const char *username, const char *password,
                                   nvr_cognito_user_t *out)
{
    if (!username || !username[0] || !password || !password[0] || !out)
        return NVR_COGNITO_ERR_PARAM;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (!root || !params) { cJSON_Delete(root); cJSON_Delete(params); return NVR_COGNITO_ERR_OTHER; }
    cJSON_AddStringToObject(root, "ClientId", NVR_COG_CLIENT_ID);
    cJSON_AddStringToObject(root, "AuthFlow", "USER_PASSWORD_AUTH");
    cJSON_AddStringToObject(params, "USERNAME", username);
    cJSON_AddStringToObject(params, "PASSWORD", password);
    cJSON_AddItemToObject(root, "AuthParameters", params);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return NVR_COGNITO_ERR_OTHER;

    CURL *c = curl_easy_init();
    if (!c) { free(body); return NVR_COGNITO_ERR_NETWORK; }

    mem_t m = {0};
    struct curl_slist *hdr = NULL;
    hdr = curl_slist_append(hdr, "Content-Type: application/x-amz-json-1.1");
    hdr = curl_slist_append(hdr, "X-Amz-Target: AWSCognitoIdentityProviderService.InitiateAuth");

    curl_easy_setopt(c, CURLOPT_URL, NVR_COG_URL);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    free(body);

    if (rc != CURLE_OK) {
        NVR_LOGW("cognito", "InitiateAuth transport fail: %s", curl_easy_strerror(rc));
        free(m.buf);
        return NVR_COGNITO_ERR_NETWORK;
    }

    cJSON *resp = m.buf ? cJSON_Parse(m.buf) : NULL;
    free(m.buf);
    if (!resp) return NVR_COGNITO_ERR_OTHER;

    nvr_cognito_rc_t ret = NVR_COGNITO_ERR_OTHER;
    if (http == 200) {
        cJSON *ar = cJSON_GetObjectItem(resp, "AuthenticationResult");
        const char *id_tok = NULL;
        if (cJSON_IsObject(ar)) {
            cJSON *it = cJSON_GetObjectItem(ar, "IdToken");
            if (cJSON_IsString(it)) id_tok = it->valuestring;
            cJSON *at = cJSON_GetObjectItem(ar, "AccessToken");
            if (cJSON_IsString(at) && at->valuestring)
                snprintf(out->access_token, sizeof(out->access_token), "%s", at->valuestring);
        }
        if (id_tok && jwt_payload_str(id_tok, "sub", out->owner_id, sizeof(out->owner_id)) == 0
            && out->owner_id[0]) {
            (void)jwt_payload_str(id_tok, "cognito:username", out->username, sizeof(out->username));
            (void)jwt_payload_str(id_tok, "email", out->email, sizeof(out->email));
            (void)jwt_payload_str(id_tok, "phone_number", out->phone, sizeof(out->phone));
            if (!out->username[0])
                snprintf(out->username, sizeof(out->username), "%s", username);
            ret = NVR_COGNITO_OK;
            NVR_LOGI("cognito", "login ok owner=%s user=%s tok=%d",
                     out->owner_id, out->username, (int)strlen(out->access_token));
        } else {
            NVR_LOGW("cognito", "200 but IdToken/sub missing");
            ret = NVR_COGNITO_ERR_OTHER;
        }
    } else {
        const char *etype = NULL;
        cJSON *t = cJSON_GetObjectItem(resp, "__type");
        if (cJSON_IsString(t) && t->valuestring) {
            etype = t->valuestring;
            const char *hash = strrchr(t->valuestring, '#');
            if (hash && hash[1]) etype = hash + 1;
        }
        NVR_LOGW("cognito", "InitiateAuth http=%ld type=%s", http, etype ? etype : "?");
        if (etype && strstr(etype, "UserNotFound"))
            ret = NVR_COGNITO_ERR_USER_NOT_FOUND;
        else if (etype && (strstr(etype, "NotAuthorized") || strstr(etype, "InvalidParameter")))
            ret = NVR_COGNITO_ERR_CREDENTIALS;
        else
            ret = NVR_COGNITO_ERR_CREDENTIALS;
    }
    cJSON_Delete(resp);
    return ret;
}
