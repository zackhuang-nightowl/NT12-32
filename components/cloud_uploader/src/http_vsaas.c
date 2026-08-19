/* http_vsaas.c — 云存 VSaaS HTTP（libcurl）。见 http_vsaas.h / docs cloudRec。 */
#include "http_vsaas.h"
#include "nvr_urls.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vsaas_http_init(void)  { return curl_global_init(CURL_GLOBAL_DEFAULT) == 0 ? 0 : -1; }
void vsaas_http_cleanup(void) { curl_global_cleanup(); }

/* --- 收响应体到内存 --- */
typedef struct { char *buf; size_t len; } mem_t;
static size_t on_write(void *p, size_t sz, size_t n, void *u)
{
    size_t add = sz * n; mem_t *m = u;
    char *nb = realloc(m->buf, m->len + add + 1);
    if (!nb) return 0;
    m->buf = nb; memcpy(m->buf + m->len, p, add); m->len += add; m->buf[m->len] = 0;
    return add;
}

int vsaas_get_url(int stage, const char *udid, const char *stoken,
                  uint32_t starttime_embedded, int event_id_code, const char *tags,
                  vsaas_url_t *out)
{
    if (!udid || !stoken || !out) return -1;
    (void)stage;   /* 域名编译期定(NVR_URL_VSAAS_HOST),运行期 stage 不再切主机 */
    memset(out, 0, sizeof(*out));
    out->event_recording_max_length = 300;

    CURL *c = curl_easy_init();
    if (!c) return -1;

    char *e_stoken = curl_easy_escape(c, stoken, 0);
    char *e_tags   = curl_easy_escape(c, tags ? tags : "", 0);
    char url[2048];
    snprintf(url, sizeof(url),
        NVR_URL_VSAAS_STREAM,
        NVR_URL_VSAAS_HOST, udid, e_stoken ? e_stoken : "",
        starttime_embedded, event_id_code, e_tags ? e_tags : "");
    if (e_stoken) curl_free(e_stoken);
    if (e_tags)   curl_free(e_tags);

    mem_t m = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    CURLcode rc = curl_easy_perform(c);
    long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { free(m.buf); return -1; }

    /* 解析 JSON：成功 {code:1,result:{url,...}}；失败 {error:{code:-1002,...}} */
    cJSON *j = m.buf ? cJSON_Parse(m.buf) : NULL;
    free(m.buf);
    if (!j) return -1;

    cJSON *err = cJSON_GetObjectItem(j, "error");
    if (err) {
        cJSON *code = cJSON_GetObjectItem(err, "code");
        out->err_code = code && cJSON_IsNumber(code) ? code->valueint : -1;
        cJSON_Delete(j);
        return 0;   /* 有响应，但服务器拒绝 */
    }
    cJSON *result = cJSON_GetObjectItem(j, "result");
    if (result) {
        cJSON *u = cJSON_GetObjectItem(result, "url");
        if (u && cJSON_IsString(u)) { snprintf(out->url, sizeof(out->url), "%s", u->valuestring); out->http_ok = 1; }
        cJSON *ml = cJSON_GetObjectItem(result, "event_recording_max_length");
        if (ml && cJSON_IsNumber(ml)) out->event_recording_max_length = ml->valueint;
    }
    cJSON_Delete(j);
    return (http == 200 && out->http_ok) ? 0 : 0;
}

int vsaas_post_ts(const char *upload_url, const uint8_t *ts, size_t ts_len,
                  uint64_t file_start_ms, uint32_t file_dur_ms, int event_end)
{
    if (!upload_url || !ts) return -1;
    CURL *c = curl_easy_init();
    if (!c) return -1;

    /* filename: {13位ms}_D{durMs}[E].ts */
    char filename[64];
    snprintf(filename, sizeof(filename), "%llu_D%u%s.ts",
             (unsigned long long)file_start_ms, file_dur_ms, event_end ? "E" : "");

    char fstart[24], fdur[16], fend[4];
    snprintf(fstart, sizeof(fstart), "%llu", (unsigned long long)file_start_ms);
    snprintf(fdur,   sizeof(fdur),   "%u", file_dur_ms);
    snprintf(fend,   sizeof(fend),   "%d", event_end ? 1 : 0);

    curl_mime *mime = curl_mime_init(c);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "media_file");
    curl_mime_filename(part, filename);
    curl_mime_type(part, "video/mp2t");
    curl_mime_data(part, (const char *)ts, ts_len);
    /* 附加字段（服务器读取 form-data 的 file_start_time/file_duration/event_end） */
    struct { const char *k, *v; } fields[] = {
        {"file_start_time", fstart}, {"file_duration", fdur}, {"event_end", fend}
    };
    for (int i = 0; i < 3; i++) {
        curl_mimepart *p = curl_mime_addpart(mime);
        curl_mime_name(p, fields[i].k);
        curl_mime_data(p, fields[i].v, CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(c, CURLOPT_URL, upload_url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    CURLcode rc = curl_easy_perform(c);
    long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_mime_free(mime);
    curl_easy_cleanup(c);

    return (rc == CURLE_OK && http == 200) ? 0 : -1;
}

int vsaas_update_tags(int stage, const char *udid, const char *stoken,
                      uint32_t starttime_embedded, const char *tags)
{
    if (!udid || !stoken) return -1;
    (void)stage;   /* 域名编译期定(NVR_URL_VSAAS_HOST) */
    CURL *c = curl_easy_init();
    if (!c) return -1;
    char *e_stoken = curl_easy_escape(c, stoken, 0);
    char *e_tags   = curl_easy_escape(c, tags ? tags : "", 0);
    char url[2048];
    snprintf(url, sizeof(url),
        NVR_URL_VSAAS_EVENT,
        NVR_URL_VSAAS_HOST, udid, e_stoken ? e_stoken : "",
        starttime_embedded, e_tags ? e_tags : "");
    if (e_stoken) curl_free(e_stoken);
    if (e_tags)   curl_free(e_tags);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(c);
    long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    return (rc == CURLE_OK && http == 200) ? 0 : -1;
}
