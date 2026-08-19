/***************************************************************************************
 *  nvr_push.c — 推送：策略 → 读 rsdk_pic → POST 图床 → GET TPNS。见 nvr_push.h。
 ***************************************************************************************/
#include "nvr_push.h"
#include "nvr_urls.h"
#include "nvr_settings.h"
#include "nvr_chan_persist.h"
#include "nvr_identity.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "rsdk.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PUSH_Q_CAP 16

typedef struct {
    int               chn;
    uint64_t          eid;
    uint32_t          ts;
    nop_detect_type_t type;
} push_job_t;

struct nvr_push {
    nvr_push_opt_t  opt;
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    pthread_t       tid;
    int             started;
    volatile int    run;
    push_job_t      q[PUSH_Q_CAP];
    int             head, tail, n;
    uint64_t        last_seen_ms[NVR_DEF_CAPACITY][NOP_DETECT_TYPE_MAX];
    uint64_t        last_send_ms;
};

typedef struct { char *buf; size_t len; } mem_t;

static size_t on_write(void *p, size_t sz, size_t n, void *u)
{
    size_t a = sz * n; mem_t *m = u;
    char *nb = realloc(m->buf, m->len + a + 1);
    if (!nb) return 0;
    m->buf = nb; memcpy(m->buf + m->len, p, a); m->len += a; m->buf[m->len] = 0;
    return a;
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void curl_tls(CURL *c)
{
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
}

static int b64_encode(const unsigned char *in, size_t n, char *out, size_t cap)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, i = 0;
    if (!in || !out || cap < 4) return -1;
    while (i < n) {
        unsigned v = (unsigned)in[i] << 16;
        if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)in[i + 2];
        if (o + 4 >= cap) return -1;
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? T[v & 63] : '=';
        i += 3;
    }
    out[o] = 0;
    return 0;
}

static int csv_has(const char *csv, const char *tok)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", csv ? csv : "");
    for (char *p = strtok(buf, ","); p; p = strtok(NULL, ",")) {
        while (*p == ' ') p++;
        if (strcmp(p, tok) == 0) return 1;
    }
    return 0;
}

static const char *trigger_of(nop_detect_type_t t)
{
    switch (t) {
        case NOP_DETECT_PIXEL_CHANGE:
        case NOP_DETECT_MOTION:          return "pixelChange";
        case NOP_DETECT_HUMAN:           return "human";
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return "face";
        case NOP_DETECT_VEHICLE:         return "vehicle";
        case NOP_DETECT_ANIMAL:          return "animal";
        case NOP_DETECT_PACKAGE:         return "package";
        case NOP_DETECT_DOORBELL_RING:   return "doorbellRing";
        case NOP_DETECT_LINE_CROSS:      return "lineCross";
        case NOP_DETECT_FIELD_INTRUSION: return "fieldIntrusion";
        default:                         return NULL;
    }
}

static int is_image_alarm(nop_detect_type_t t)
{
    return t != NOP_DETECT_DOORBELL_RING;
}

static int is_ai_photo(nop_detect_type_t t)
{
    return t == NOP_DETECT_HUMAN || t == NOP_DETECT_FACE ||
           t == NOP_DETECT_FACIAL_RECOGNITION || t == NOP_DETECT_VEHICLE;
}

static int hhmm_to_min(const char *s)
{
    if (!s || strlen(s) < 4) return 0;
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[2] - '0') * 10 + (s[3] - '0');
    if (h < 0) h = 0;
    if (h > 23) h = 23;
    if (m < 0) m = 0;
    if (m > 59) m = 59;
    return h * 60 + m;
}

static int in_dnd(const nvr_push_cfg_t *p)
{
    if (!p->dnd_enable) return 0;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int cur = tmv.tm_hour * 60 + tmv.tm_min;
    int a = hhmm_to_min(p->dnd_start), b = hhmm_to_min(p->dnd_end);
    if (a == b) return 1;
    if (a < b) return cur >= a && cur < b;
    return cur >= a || cur < b;   /* 跨零点 2100~0700 */
}

static int push_event_type(nop_detect_type_t t)
{
    switch (t) {
        case NOP_DETECT_PIXEL_CHANGE:
        case NOP_DETECT_MOTION:          return NVR_URL_PUSH_EVT_MOTION;
        case NOP_DETECT_HUMAN:           return NVR_URL_PUSH_EVT_HUMAN;
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return NVR_URL_PUSH_EVT_FACE;
        case NOP_DETECT_VEHICLE:         return NVR_URL_PUSH_EVT_VEHICLE;
        case NOP_DETECT_DOORBELL_RING:   return NVR_URL_PUSH_EVT_DOORBELL;
        case NOP_DETECT_ANIMAL:          return NVR_URL_PUSH_EVT_ANIMAL;
        case NOP_DETECT_PACKAGE:         return NVR_URL_PUSH_EVT_PACKAGE;
        case NOP_DETECT_LINE_CROSS:      return NVR_URL_PUSH_EVT_LINECROSS;
        case NOP_DETECT_FIELD_INTRUSION: return NVR_URL_PUSH_EVT_INTRUSION;
        default:                         return 0;
    }
}

static const char *payload_key(nop_detect_type_t t)
{
    switch (t) {
        case NOP_DETECT_PIXEL_CHANGE:
        case NOP_DETECT_MOTION:          return NVR_URL_PUSH_KEY_MOTION;
        case NOP_DETECT_HUMAN:           return NVR_URL_PUSH_KEY_HUMAN;
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return NVR_URL_PUSH_KEY_FACE;
        case NOP_DETECT_VEHICLE:         return NVR_URL_PUSH_KEY_VEHICLE;
        case NOP_DETECT_DOORBELL_RING:   return NVR_URL_PUSH_KEY_DOORBELL;
        default:                         return NULL;
    }
}

/* 策略：0=发；<0=丢。静默命中会重置窗口。 */
static int policy_ok(nvr_push_t *p, const push_job_t *j, const nvr_push_cfg_t *cfg)
{
    int doorbell = (j->type == NOP_DETECT_DOORBELL_RING);
    time_t now = time(NULL);
    if (cfg->snooze_end > 0 && (int)now < cfg->snooze_end) return -1;
    if (in_dnd(cfg)) return -1;

    const char *trig = trigger_of(j->type);
    if (!trig) return -1;
    if (!csv_has(cfg->triggers, trig)) return -1;
    if (is_image_alarm(j->type) && !cfg->switch_on) return -1;

    if (!doorbell && NVR_URL_PUSH_SILENT_MS > 0) {
        uint64_t t = now_ms();
        int ty = (int)j->type;
        if (ty < 0 || ty >= NOP_DETECT_TYPE_MAX) return -1;
        if (j->chn < 0 || j->chn >= NVR_DEF_CAPACITY) return -1;
        uint64_t last = p->last_seen_ms[j->chn][ty];
        p->last_seen_ms[j->chn][ty] = t;   /* 命中也重置静默 */
        if (last && t - last < (uint64_t)NVR_URL_PUSH_SILENT_MS)
            return -1;
    }
    return 0;
}

static int load_event_jpeg(nvr_push_t *p, uint64_t eid, nop_detect_type_t type,
                           void **jpeg, size_t *len)
{
#if !RSDK_CFG_METADATA
    (void)p; (void)eid; (void)type; (void)jpeg; (void)len;
    return -1;
#else
    if (!p->opt.meta || !p->opt.group || !eid) return -1;
    int want = is_ai_photo(type) ? RSDK_PIC_TARGET : RSDK_PIC_MAIN;
    rsdk_pic_ref_t r[4];
    int n = rsdk_pic_list_event(p->opt.meta, eid, want, r, 4);
    if (n <= 0) n = rsdk_pic_list_event(p->opt.meta, eid, RSDK_PIC_MAIN, r, 4);
    if (n <= 0) n = rsdk_pic_list_event(p->opt.meta, eid, -1, r, 4);
    if (n <= 0) return -1;
    rsdk_dev_t *d = rsdk_group_dev(p->opt.group, (int)r[0].disk);
    if (!d) d = rsdk_group_dev(p->opt.group, 0);
    if (!d) return -1;
    return rsdk_pic_read(d, p->opt.meta, r[0].pic_id, jpeg, len) == RSDK_OK ? 0 : -1;
#endif
}

static int upload_jpeg(const char *uid, const void *jpeg, size_t jlen, char *url_out, size_t url_cap)
{
    if (!uid || !uid[0] || !jpeg || jlen == 0 || !url_out) return -1;
    url_out[0] = 0;
    char post_url[320];
    snprintf(post_url, sizeof(post_url), "%s?realm=%s", NVR_URL_UPLOAD_IMAGE, NVR_URL_UPLOAD_REALM);

    CURL *c = curl_easy_init();
    if (!c) return -1;
    curl_mime *mime = curl_mime_init(c);
    if (!mime) { curl_easy_cleanup(c); return -1; }

    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "validation");
    curl_mime_data(part, uid, CURL_ZERO_TERMINATED);
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "uploadFile");
    curl_mime_filename(part, "push.jpg");
    curl_mime_type(part, "application/octet-stream");
    curl_mime_data(part, jpeg, jlen);

    mem_t m = {0};
    curl_tls(c);
    curl_easy_setopt(c, CURLOPT_URL, post_url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_mime_free(mime);
    curl_easy_cleanup(c);

    int ok = -1;
    if (rc == CURLE_OK && http == 200 && m.buf) {
        cJSON *j = cJSON_Parse(m.buf);
        cJSON *u = j ? cJSON_GetObjectItem(j, "url") : NULL;
        if (cJSON_IsString(u) && u->valuestring && u->valuestring[0]) {
            snprintf(url_out, url_cap, "%s", u->valuestring);
            ok = 0;
        }
        cJSON_Delete(j);
    } else {
        NVR_LOGW("push", "upload fail curl=%s http=%ld",
                 rc == CURLE_OK ? "-" : curl_easy_strerror(rc), http);
    }
    free(m.buf);
    return ok;
}

static void send_tpns(const char *uid, uint32_t ts, int event_type, int ch1,
                      const char *payload_b64, const char *img_url)
{
    CURL *c = curl_easy_init();
    if (!c) return;
    char *esc_pl = curl_easy_escape(c, payload_b64 ? payload_b64 : "", 0);
    char *esc_img = (img_url && img_url[0]) ? curl_easy_escape(c, img_url, 0) : NULL;
    char url[2048];
    int n = snprintf(url, sizeof(url),
        "%s?cmd=%s&uid=%s&event_time=%u&event_type=%d&dev_type=%s"
        "&customized_payload=%s&channel=%d",
        NVR_URL_PUSH, NVR_URL_PUSH_CMD, uid, ts, event_type, NVR_URL_PUSH_DEV_TYPE,
        esc_pl ? esc_pl : "", ch1);
    if (n > 0 && (size_t)n < sizeof(url) && esc_img)
        n += snprintf(url + n, sizeof(url) - (size_t)n, "&img=%s", esc_img);
    if (n > 0 && (size_t)n < sizeof(url) && NVR_URL_PUSH_DEBUG)
        snprintf(url + n, sizeof(url) - (size_t)n, "&debug=1");

    mem_t m = {0};
    curl_tls(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    if (rc != CURLE_OK || http != 200)
        NVR_LOGW("push", "tpns fail curl=%s http=%ld body=%.80s",
                 rc == CURLE_OK ? "-" : curl_easy_strerror(rc), http,
                 m.buf ? m.buf : "");
    else
        NVR_LOGI("push", "tpns ok ch%d type=%d", ch1, event_type);
    free(m.buf);
    curl_free(esc_pl);
    curl_free(esc_img);
    curl_easy_cleanup(c);
}

static void handle_job(nvr_push_t *p, push_job_t *j)
{
    if (!p->opt.settings || j->chn < 0 || j->chn >= NVR_DEF_CAPACITY) return;
    nvr_push_cfg_t cfg;
    nvr_settings_push_get(p->opt.settings, j->chn, &cfg);

    pthread_mutex_lock(&p->lock);
    int allow = policy_ok(p, j, &cfg);
    pthread_mutex_unlock(&p->lock);
    if (allow != 0) return;

    char uid[64] = "";
    nvr_identity_get_uid(uid, sizeof(uid));
    if (!uid[0]) {
        NVR_LOGW("push", "no UID, skip");
        return;
    }

    int want_photo = (j->type == NOP_DETECT_DOORBELL_RING) ||
                     (cfg.photo_on && is_ai_photo(j->type));
    void *jpeg = NULL; size_t jlen = 0;
    char img[512] = "";
    if (want_photo && j->eid) {
        int k;
        usleep(NVR_URL_PUSH_SNAP_WAIT_MS * 1000);
        for (k = 0; k < NVR_URL_PUSH_SNAP_RETRY; k++) {
            if (load_event_jpeg(p, j->eid, j->type, &jpeg, &jlen) == 0) break;
            if (k + 1 < NVR_URL_PUSH_SNAP_RETRY)
                usleep(NVR_URL_PUSH_SNAP_WAIT_MS * 1000);
        }
        if (jpeg && jlen > 0 && jlen <= (size_t)NVR_URL_PUSH_IMG_MAX) {
            if (upload_jpeg(uid, jpeg, jlen, img, sizeof(img)) != 0) img[0] = 0;
        } else if (jpeg && jlen > (size_t)NVR_URL_PUSH_IMG_MAX) {
            NVR_LOGI("push", "jpeg %zu > %d, skip img", jlen, NVR_URL_PUSH_IMG_MAX);
        }
        free(jpeg);
    }

    const char *pkey = payload_key(j->type);
    int et = push_event_type(j->type);
    if (!pkey || !et) return;

    char name[64] = "";
    nvr_chan_persist_get_name(p->opt.persist, j->chn + 1, name, sizeof(name));

    cJSON *pl = cJSON_CreateObject();
    cJSON_AddStringToObject(pl, "event_type", pkey);
    cJSON_AddStringToObject(pl, "arg1", name);
    cJSON_AddStringToObject(pl, "arg2", "");
    cJSON_AddStringToObject(pl, "arg3", "");
    char *js = cJSON_PrintUnformatted(pl);
    cJSON_Delete(pl);
    if (!js) return;
    char b64[256];
    int b64ok = b64_encode((unsigned char *)js, strlen(js), b64, sizeof(b64));
    free(js);
    if (b64ok != 0) return;

    uint64_t t = now_ms();
    if (p->last_send_ms && t - p->last_send_ms < (uint64_t)NVR_URL_PUSH_INTERVAL_MS) {
        unsigned wait = (unsigned)((uint64_t)NVR_URL_PUSH_INTERVAL_MS - (t - p->last_send_ms));
        usleep(wait * 1000);
    }
    send_tpns(uid, j->ts, et, j->chn + 1, b64, img);
    p->last_send_ms = now_ms();
}

static void *push_worker(void *arg)
{
    nvr_push_t *p = arg;
    while (p->run) {
        push_job_t job;
        pthread_mutex_lock(&p->lock);
        while (p->n == 0 && p->run)
            pthread_cond_wait(&p->cv, &p->lock);
        if (!p->run && p->n == 0) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        job = p->q[p->head];
        p->head = (p->head + 1) % PUSH_Q_CAP;
        p->n--;
        pthread_mutex_unlock(&p->lock);
        handle_job(p, &job);
    }
    return NULL;
}

int nvr_push_start(const nvr_push_opt_t *opt, nvr_push_t **out)
{
    if (!opt || !out) return -1;
    nvr_push_t *p = calloc(1, sizeof(*p));
    if (!p) return -1;
    p->opt = *opt;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->run = 1;
    if (pthread_create(&p->tid, NULL, push_worker, p) != 0) {
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->cv);
        free(p);
        return -1;
    }
    p->started = 1;
    *out = p;
    NVR_LOGI("push", "engine up %s", NVR_URL_PUSH);
    return 0;
}

void nvr_push_stop(nvr_push_t *p)
{
    if (!p) return;
    p->run = 0;
    pthread_mutex_lock(&p->lock);
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->lock);
    if (p->started) pthread_join(p->tid, NULL);
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->cv);
    free(p);
}

void nvr_push_on_event(nvr_push_t *p, int chn, uint64_t eid, uint32_t ts,
                       nop_detect_type_t type)
{
    if (!p || chn < 0 || chn >= NVR_DEF_CAPACITY) return;
    if (push_event_type(type) == 0) return;
    push_job_t job;
    memset(&job, 0, sizeof(job));
    job.chn = chn; job.eid = eid; job.ts = ts; job.type = type;
    pthread_mutex_lock(&p->lock);
    if (p->n < PUSH_Q_CAP) {
        p->q[p->tail] = job;
        p->tail = (p->tail + 1) % PUSH_Q_CAP;
        p->n++;
        pthread_cond_signal(&p->cv);
    }
    pthread_mutex_unlock(&p->lock);
}
