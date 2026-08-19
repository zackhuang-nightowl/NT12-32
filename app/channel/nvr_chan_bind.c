/***************************************************************************************
 *  nvr_chan_bind.c — 用户密码优先；失败静默试 P_enh / P_act / 无鉴权
 ***************************************************************************************/
#include "nvr_chan_bind.h"
#include "nvr_crypto.h"
#include "nvr_identity.h"
#include "nvr_dev_classify.h"
#include "nvr_log.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

typedef struct { char *buf; size_t len; } bind_http_buf_t;

static size_t bind_http_sink(void *p, size_t sz, size_t nm, void *u)
{
    size_t n = sz * nm;
    bind_http_buf_t *b = (bind_http_buf_t *)u;
    char *nb = realloc(b->buf, b->len + n + 1);
    if (!nb) return 0;
    b->buf = nb;
    memcpy(b->buf + b->len, p, n);
    b->len += n;
    b->buf[b->len] = 0;
    return n;
}

typedef struct { char random[64]; } bind_hdr_t;

static size_t bind_hdr_cb(char *p, size_t sz, size_t nm, void *u)
{
    size_t n = sz * nm;
    bind_hdr_t *h = (bind_hdr_t *)u;
    if (!h || n < 8) return n;
    if (strncasecmp(p, "Random:", 7) == 0) {
        const char *s = p + 7;
        size_t left = n - 7;
        while (left && (*s == ' ' || *s == '\t')) { s++; left--; }
        size_t i = 0;
        while (i + 1 < sizeof(h->random) && i < left && s[i] != '\r' && s[i] != '\n') {
            h->random[i] = s[i];
            i++;
        }
        h->random[i] = 0;
    }
    return n;
}

static int cam_ch(const nvr_channel_t *d)
{
    return (d && d->dev_chn > 0) ? d->dev_chn : 1;
}

static char *do_post(const char *ip, int port, const char *json_in,
                     const char *user, const char *pass, int *http_out, bind_hdr_t *hdr)
{
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d/APPJsonCmd", ip, port > 0 ? port : 80);
    bind_http_buf_t m = {0};
    struct curl_slist *hl = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_in);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, bind_http_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, bind_hdr_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, hdr);
    if (user && user[0] && pass && pass[0]) {
        curl_easy_setopt(c, CURLOPT_USERNAME, user);
        curl_easy_setopt(c, CURLOPT_PASSWORD, pass);
        curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)CURLAUTH_DIGEST);
    }
    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    if (http_out) *http_out = (int)http;
    if (rc != CURLE_OK) { free(m.buf); return NULL; }
    return m.buf;
}

static int is_user_pass(const char *pass)
{
    if (!pass || !pass[0]) return 0;
    if (strcmp(pass, "123456") == 0) return 0;
    return 1;
}

static void apply_enh_creds(nvr_channel_t *d, const char *random, const char *penh)
{
    if (!d) return;
    snprintf(d->user, sizeof(d->user), "admin");
    snprintf(d->pass, sizeof(d->pass), "%s", penh ? penh : "");
    snprintf(d->enh_random, sizeof(d->enh_random), "%s", random ? random : "");
}

static void clear_enh_creds(nvr_channel_t *d)
{
    if (!d) return;
    d->enh_random[0] = 0;
    d->pass[0] = 0;
}

char *nvr_chan_bind_post(nvr_channel_t *d, const char *json_in)
{
    bind_hdr_t hdr; memset(&hdr, 0, sizeof(hdr));
    int http = 0, port;
    char *resp;
    if (!d || !d->onvif_ip[0] || !json_in) return NULL;
    port = d->onvif_port > 0 ? d->onvif_port : 80;
    resp = do_post(d->onvif_ip, port, json_in,
                   d->user[0] ? d->user : NULL,
                   is_user_pass(d->pass) ? d->pass : NULL,
                   &http, &hdr);
    if (http == 401 && hdr.random[0] && nvr_pw_enh_ready()) {
        char penh[24], challenge[64];
        snprintf(challenge, sizeof(challenge), "%s", hdr.random);
        if (nvr_pw_from_random(challenge, penh, sizeof(penh)) == 16) {
            free(resp);
            memset(&hdr, 0, sizeof(hdr));
            resp = do_post(d->onvif_ip, port, json_in, "admin", penh, &http, &hdr);
            if (resp && http != 401 && http != 402)
                apply_enh_creds(d, challenge, penh);
        }
    } else if (http == 402) {
        free(resp);
        clear_enh_creds(d);
        memset(&hdr, 0, sizeof(hdr));
        resp = do_post(d->onvif_ip, port, json_in, NULL, NULL, &http, &hdr);
    }
    return resp;
}

static char *bind_post(const char *ip, int port, const char *json_in)
{
    return do_post(ip, port, json_in, NULL, NULL, NULL, NULL);
}

static int json_status(cJSON *root)
{
    cJSON *r = root ? cJSON_GetObjectItem(root, "result") : NULL;
    cJSON *sc = r ? cJSON_GetObjectItem(r, "statusCode") : NULL;
    if (!sc) sc = root ? cJSON_GetObjectItem(root, "statusCode") : NULL;
    return (sc && cJSON_IsNumber(sc)) ? sc->valueint : 0;
}

static int cred_same(const nvr_auth_cred_t *a, const char *user, const char *pass)
{
    return strcmp(a->user, user ? user : "") == 0 &&
           strcmp(a->pass, pass ? pass : "") == 0;
}

static int cred_add(nvr_auth_cred_t *out, int *n, int cap, const char *user, const char *pass)
{
    int i;
    if (*n >= cap) return 0;
    for (i = 0; i < *n; i++)
        if (cred_same(&out[i], user, pass)) return 0;
    snprintf(out[*n].user, sizeof(out[*n].user), "%s", user ? user : "");
    snprintf(out[*n].pass, sizeof(out[*n].pass), "%s", pass ? pass : "");
    (*n)++;
    return 1;
}

static int parse_enh_random(cJSON *root, char *out, size_t cap)
{
    cJSON *ct = root ? cJSON_GetObjectItem(root, "content") : NULL;
    cJSON *rj = ct ? cJSON_GetObjectItem(ct, "random") : NULL;
    if (out && cap) out[0] = 0;
    if (!cJSON_IsString(rj) || !rj->valuestring || !rj->valuestring[0]) return 0;
    snprintf(out, cap, "%s", rj->valuestring);
    return 1;
}

static void gen_enh_random(char *out, size_t cap)
{
    static const char tab[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)out;
    int i, n = 8;
    if (!out || cap < 2) return;
    if (cap < 9) n = (int)cap - 1;
    for (i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        out[i] = tab[(seed >> 16) % 36u];
    }
    out[n] = 0;
}

static int rpc_enh_get(nvr_channel_t *d, char *random_out, size_t cap)
{
    char body[192], *resp;
    int code;
    snprintf(body, sizeof(body),
             "{\"func\":\"getEnhancedSecurity\",\"args\":{\"channel\":%d}}", cam_ch(d));
    resp = nvr_chan_bind_post(d, body);
    if (!resp) return -1;
    {
        cJSON *root = cJSON_Parse(resp);
        code = json_status(root);
        parse_enh_random(root, random_out, cap);
        if (root) cJSON_Delete(root);
        free(resp);
    }
    if (code == 501) return 1;
    return 0;
}

static int rpc_enh_set(nvr_channel_t *d, const char *random)
{
    char setb[320], *resp;
    int code = 0;
    snprintf(setb, sizeof(setb),
             "{\"func\":\"setEnhancedSecurity\",\"args\":{\"channel\":%d,\"random\":\"%s\"}}",
             cam_ch(d), random ? random : "");
    resp = nvr_chan_bind_post(d, setb);
    if (!resp) return -1;
    {
        cJSON *root = cJSON_Parse(resp);
        code = json_status(root);
        if (root) cJSON_Delete(root);
        free(resp);
    }
    if (code == 501) return 1;
    return 0;
}

int nvr_chan_enh_get(nvr_channel_t *d, char *random_out, size_t cap)
{
    if (!d || !d->onvif_ip[0]) return -1;
    return rpc_enh_get(d, random_out, cap);
}

int nvr_chan_enh_on_auth_fail(nvr_channel_t *d)
{
    char rnd[64] = {0};
    int rc;
    if (!d) return -1;
    rc = nvr_chan_enh_get(d, rnd, sizeof(rnd));
    if (rc == 1) {                      /* 501：不支持 → 普通模式 */
        clear_enh_creds(d);
        return 1;
    }
    if (rc != 0) return -1;
    if (!rnd[0]) {                      /* 相机已在普通模式（含 402 后） */
        clear_enh_creds(d);
        return 1;
    }
    {
        char penh[24];
        if (nvr_pw_from_random(rnd, penh, sizeof(penh)) != 16) return -1;
        apply_enh_creds(d, rnd, penh);
    }
    return 0;
}

int nvr_chan_enh_apply(nvr_channel_t *d, int enable, const char *set_random,
                       char *penh_out, size_t cap)
{
    char rnd[64] = {0};
    int rc;
    if (penh_out && cap) penh_out[0] = 0;
    if (!d || !d->onvif_ip[0]) return -1;

    if (!enable) {
        rc = rpc_enh_set(d, "");
        if (rc == 1) return 1;
        if (rc != 0) return -1;
        clear_enh_creds(d);
        return 0;
    }
    if (!nvr_pw_enh_ready()) return -1;

    if (set_random && set_random[0]) {
        rc = rpc_enh_set(d, set_random);
        if (rc != 0) return rc;
        rc = rpc_enh_get(d, rnd, sizeof(rnd));
        if (rc != 0) return rc;
        if (!rnd[0]) snprintf(rnd, sizeof(rnd), "%s", set_random);
    } else {
        rc = rpc_enh_get(d, rnd, sizeof(rnd));
        if (rc != 0) return rc;
        if (!rnd[0]) {
            gen_enh_random(rnd, sizeof(rnd));
            rc = rpc_enh_set(d, rnd);
            if (rc != 0) return rc;
            rnd[0] = 0;
            rc = rpc_enh_get(d, rnd, sizeof(rnd));
            if (rc != 0) return rc;
            if (!rnd[0]) return -1;
        }
    }
    if (!penh_out || cap < 17) return -1;
    if (nvr_pw_from_random(rnd, penh_out, cap) != 16) return -1;
    apply_enh_creds(d, rnd, penh_out);
    return 0;
}

/* 方案 A：设备已开 digest 才加入 P_enh。连接路径不 SET。 */
static int fill_penh(nvr_channel_t *d, char *out, size_t cap)
{
    char rnd[64] = {0};
    int rc;
    if (!nvr_pw_enh_ready()) return -1;
    rc = nvr_chan_enh_get(d, rnd, sizeof(rnd));
    if (rc == 1 || (rc == 0 && !rnd[0])) {   /* 501 或不支持/已关 → 普通模式 */
        clear_enh_creds(d);
        return -1;
    }
    if (rc != 0) return -1;
    if (nvr_pw_from_random(rnd, out, cap) != 16) return -1;
    apply_enh_creds(d, rnd, out);
    return 0;
}

/* 方案 B：仅 nopOnvif。P_act 明文；HTTP 激活见 nvr_chan_try_activate。 */
static int fill_pact(nvr_channel_t *d, char *out, size_t cap)
{
    char nvr_sn[64] = {0};
    if (!d || d->kind != NVR_DEV_KIND_NOPONVIF) return -1;
    nvr_identity_get_sn(nvr_sn, sizeof(nvr_sn));
    if (!d->serial[0] || !nvr_sn[0] || !nvr_pw_act_ready()) return -1;
    if (nvr_pw_activate(nvr_sn, d->serial, out, cap) != 16) return -1;
    return 0;
}

/* 设备级激活接口：协议 args.channel 固定 1。 */
static int parse_active_status(cJSON *root)
{
    cJSON *ct = root ? cJSON_GetObjectItem(root, "content") : NULL;
    cJSON *sj = ct ? cJSON_GetObjectItem(ct, "status") : NULL;
    if (sj && cJSON_IsNumber(sj)) return sj->valueint;
    return -1;
}

static int rpc_get_active(const nvr_channel_t *d, int *status_out)
{
    char q[160], *resp;
    int port = d->onvif_port > 0 ? d->onvif_port : 80;
    int code, st;
    snprintf(q, sizeof(q),
             "{\"func\":\"X_NightOwl_getDeviceActive\",\"args\":{\"channel\":1}}");
    resp = bind_post(d->onvif_ip, port, q);
    if (!resp) return -1;
    {
        cJSON *root = cJSON_Parse(resp);
        code = json_status(root);
        st = parse_active_status(root);
        if (root) cJSON_Delete(root);
        free(resp);
    }
    if (status_out) *status_out = (st > 0) ? 1 : 0;
    return code > 0 ? code : -1;
}

int nvr_chan_get_device_active(const nvr_channel_t *d, int *status_out)
{
    if (!d || !d->onvif_ip[0]) return -1;
    return rpc_get_active(d, status_out);
}

int nvr_chan_try_activate(nvr_channel_t *d, nvr_chan_substate_t *sub,
                          char *pact_out, size_t cap)
{
    char nvr_sn[64] = {0}, enc[80], *resp;
    int port, code, active = 0;
    if (!d || !d->onvif_ip[0] || !pact_out || cap < 17) return -1;
    if (d->kind != NVR_DEV_KIND_NOPONVIF) return -1;
    nvr_identity_get_sn(nvr_sn, sizeof(nvr_sn));
    if (!d->serial[0] || !nvr_sn[0] || !nvr_pw_act_ready()) return -1;
    if (nvr_pw_activate(nvr_sn, d->serial, pact_out, cap) != 16) return -1;

    port = d->onvif_port > 0 ? d->onvif_port : 80;
    code = rpc_get_active(d, &active);
    if (code == 501) return 1;          /* 不支持激活 */
    if (code != 200) return -1;
    if (!active) {
        if (nvr_pw_act_encrypt(nvr_sn, d->serial, enc, sizeof(enc)) < 0) return -1;
        char setb[256];
        snprintf(setb, sizeof(setb),
                 "{\"func\":\"X_NightOwl_setDeviceActive\",\"args\":{\"channel\":1,\"password\":\"%s\"}}",
                 enc);
        resp = bind_post(d->onvif_ip, port, setb);
        if (resp) {
            cJSON *root = cJSON_Parse(resp);
            cJSON *ct = root ? cJSON_GetObjectItem(root, "content") : NULL;
            const char *err = NULL;
            if (ct) {
                cJSON *ej = cJSON_GetObjectItem(ct, "error");
                if (cJSON_IsString(ej)) err = ej->valuestring;
            }
            /* already_active_error 视为已激活，仍再 GET 确认 */
            (void)err;
            if (root) cJSON_Delete(root);
            free(resp);
        }
        active = 0;
        code = rpc_get_active(d, &active);
        if (code != 200 || !active) {
            NVR_LOGW("bind", "激活未确认 ip=%s getDeviceActive code=%d status=%d",
                     d->onvif_ip, code, active);
            return -1;
        }
    }
    if (sub) sub->inactive = 0;
    NVR_LOGI("bind", "nopOnvif 已激活 ip=%s", d->onvif_ip);
    return 0;
}

int nvr_chan_auth_candidates(nvr_channel_t *d, nvr_settings_t *st,
                             nvr_chan_substate_t *sub,
                             nvr_auth_cred_t *out, int cap)
{
    int n = 0;
    char tmp[24];
    (void)st; (void)sub;
    if (!d || !d->onvif_ip[0] || !out || cap <= 0) return 0;

    /* 1) 用户输入优先（123456 不当作用户密码） */
    if (is_user_pass(d->pass))
        cred_add(out, &n, cap, d->user[0] ? d->user : "admin", d->pass);

    /* 2) 设备已开 digest 才加入 P_enh（连接不 SET） */
    if (fill_penh(d, tmp, sizeof(tmp)) == 0)
        cred_add(out, &n, cap, "admin", tmp);

    /* 3) 激活 P_act：仅已判定 nopOnvif */
    if (fill_pact(d, tmp, sizeof(tmp)) == 0)
        cred_add(out, &n, cap, "admin", tmp);

    /* 4) 无鉴权（原 admin/123456） */
    cred_add(out, &n, cap, "", "");
    return n;
}
