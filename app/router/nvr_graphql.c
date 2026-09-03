/***************************************************************************************
 *  nvr_graphql.c — Protect GraphQL addDevice。见 nvr_graphql.h / APP guide 埋点。
 ***************************************************************************************/
#include "nvr_graphql.h"
#include "nvr_identity.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ADD_DEVICE_QUERY =
    "mutation addDevice($data: AddDeviceInput!) {"
    " addDevice(data: $data) {"
    "  uid deviceType name"
    "  credentials { cloudToken primaryKey avKey bluetoothId }"
    "  role p2pProvider"
    " }"
    "}";

typedef struct { char *buf; size_t len; } mem_t;

static size_t on_write(void *p, size_t sz, size_t n, void *u)
{
    size_t a = sz * n; mem_t *m = u;
    char *nb = realloc(m->buf, m->len + a + 1);
    if (!nb) return 0;
    m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0;
    return a;
}

nvr_gql_rc_t nvr_graphql_add_device(const nvr_gql_add_device_in_t *in,
                                    char *stoken_out, size_t stoken_cap)
{
    if (!in || !in->access_token || !in->access_token[0] || !in->uid || !in->uid[0] ||
        !stoken_out || stoken_cap == 0)
        return NVR_GQL_ERR_PARAM;
    stoken_out[0] = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON *vars = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    cJSON *cred = cJSON_CreateObject();
    if (!root || !vars || !data || !cred) {
        cJSON_Delete(root); cJSON_Delete(vars); cJSON_Delete(data); cJSON_Delete(cred);
        return NVR_GQL_ERR_OTHER;
    }
    cJSON_AddStringToObject(root, "operationName", "addDevice");
    cJSON_AddStringToObject(root, "query", ADD_DEVICE_QUERY);
    cJSON_AddStringToObject(data, "uid", in->uid);
    cJSON_AddStringToObject(data, "locationId", "default");
    cJSON_AddStringToObject(data, "name",
                            (in->name && in->name[0]) ? in->name : "NVR");
    cJSON_AddStringToObject(data, "deviceType",
                            (in->device_type && in->device_type[0])
                                ? in->device_type : NVR_DEF_DEVICE_TYPE);
    cJSON_AddStringToObject(data, "role", "admin");
    cJSON_AddStringToObject(data, "p2pProvider", "tutk");
    cJSON_AddStringToObject(cred, "primaryKey",
                            (in->primary_key && in->primary_key[0]) ? in->primary_key : NVR_IDENTITY_DEF_IOTCKEY);
    cJSON_AddStringToObject(cred, "avKey",
                            (in->av_key && in->av_key[0]) ? in->av_key : NVR_IDENTITY_DEF_AVKEY);
    if (in->bluetooth_id && in->bluetooth_id[0])
        cJSON_AddStringToObject(cred, "bluetoothId", in->bluetooth_id);
    cJSON_AddItemToObject(data, "credentials", cred);
    cJSON_AddItemToObject(vars, "data", data);
    cJSON_AddItemToObject(root, "variables", vars);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return NVR_GQL_ERR_OTHER;

    CURL *c = curl_easy_init();
    if (!c) { free(body); return NVR_GQL_ERR_NETWORK; }

    mem_t m = {0};
    char auth[2200];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", in->access_token);
    struct curl_slist *hdr = NULL;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    hdr = curl_slist_append(hdr, auth);
    hdr = curl_slist_append(hdr, "Cache-Control: no-cache");

    curl_easy_setopt(c, CURLOPT_URL, NVR_URL_GRAPHQL);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    free(body);

    if (rc != CURLE_OK) {
        NVR_LOGW("graphql", "addDevice transport: %s", curl_easy_strerror(rc));
        free(m.buf);
        return NVR_GQL_ERR_NETWORK;
    }

    cJSON *resp = m.buf ? cJSON_Parse(m.buf) : NULL;
    free(m.buf);
    if (!resp) return NVR_GQL_ERR_OTHER;

    nvr_gql_rc_t ret = NVR_GQL_ERR_ADD_DEVICE;
    if (http == 401 || http == 403) {
        ret = NVR_GQL_ERR_AUTH;
    } else {
        cJSON *err = cJSON_GetObjectItem(resp, "errors");
        if (cJSON_IsArray(err) && cJSON_GetArraySize(err) > 0) {
            cJSON *e0 = cJSON_GetArrayItem(err, 0);
            const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(e0, "message"));
            NVR_LOGW("graphql", "addDevice errors: %s", msg ? msg : "?");
            ret = NVR_GQL_ERR_ADD_DEVICE;
        } else {
            cJSON *d = cJSON_GetObjectItem(resp, "data");
            cJSON *ad = d ? cJSON_GetObjectItem(d, "addDevice") : NULL;
            cJSON *cr = ad ? cJSON_GetObjectItem(ad, "credentials") : NULL;
            cJSON *ct = cr ? cJSON_GetObjectItem(cr, "cloudToken") : NULL;
            if (cJSON_IsString(ct) && ct->valuestring && ct->valuestring[0]) {
                snprintf(stoken_out, stoken_cap, "%s", ct->valuestring);
                ret = NVR_GQL_OK;
                NVR_LOGI("graphql", "addDevice ok uid=%s stoken_len=%d",
                         in->uid, (int)strlen(stoken_out));
            } else {
                NVR_LOGW("graphql", "addDevice http=%ld missing cloudToken", http);
            }
        }
    }
    cJSON_Delete(resp);
    return ret;
}
