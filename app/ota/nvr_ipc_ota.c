/***************************************************************************************
 *  nvr_ipc_ota.c — NVR 统一下载相机固件，再按协议下推。
 *
 *  NOP: POST multipart /cgi-bin/upload.cgi + GET /cgi-bin/upgrade_rate.cgi
 *       （OTA_slaveMode.md：下载占进度 0–50%，相机 rate 占 50–100%）
 *  ONVIF: mapping GUI_upgradeChannelFirmware → StartFirmwareUpgrade + HTTP POST
 ***************************************************************************************/
#include "nvr_ipc_ota.h"
#include "nvr_ota.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "cJSON.h"

#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IPC_ST_IDLE        0
#define IPC_ST_DOWNLOADING 1
#define IPC_ST_FAILED      2
#define IPC_ST_DOWNLOADED  3
#define IPC_ST_NONE        4

#define IPC_ERR_NET        1
#define IPC_ERR_SERVER     2
#define IPC_ERR_DOWNLOAD   3
#define IPC_ERR_VERIFY     4
#define IPC_ERR_STORAGE    5
#define IPC_ERR_UNKNOWN    6

#define NVR_MAX_IPC_OTA    32

static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static int g_busy;
static int g_st[NVR_MAX_IPC_OTA];
static int g_err[NVR_MAX_IPC_OTA];

static void set_st(int chn, int st, int err)
{
    if (chn < 0 || chn >= NVR_MAX_IPC_OTA) return;
    pthread_mutex_lock(&g_lk);
    g_st[chn] = st;
    g_err[chn] = err;
    pthread_mutex_unlock(&g_lk);
}

int nvr_ipc_ota_status(int chn, int *error_out)
{
    int st = IPC_ST_IDLE, err = 0;
    if (chn < 0 || chn >= NVR_MAX_IPC_OTA) {
        if (error_out) *error_out = 0;
        return IPC_ST_IDLE;
    }
    pthread_mutex_lock(&g_lk);
    st = g_st[chn];
    err = g_err[chn];
    pthread_mutex_unlock(&g_lk);
    if (error_out) *error_out = (st == IPC_ST_FAILED) ? err : 0;
    return st;
}

void nvr_ipc_ota_set_status(int chn, int status, int error)
{
    set_st(chn, status, error);
}

static int nop_need_auth(const char *user, const char *pass)
{
    if (!user || !user[0]) return 0;
    if (!pass || !pass[0]) return 0;   /* 空=无鉴权;123456 视为真实口令 */
    return 1;
}

static void apply_auth(CURL *c, const nvr_channel_t *ch)
{
    if (!nop_need_auth(ch->user, ch->pass)) return;
    char up[160];
    snprintf(up, sizeof(up), "%s:%s", ch->user, ch->pass);
    curl_easy_setopt(c, CURLOPT_USERPWD, up);
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_DIGEST | CURLAUTH_BASIC));
}

typedef struct { char *buf; size_t len; } mem_t;
static size_t mem_write(void *p, size_t sz, size_t n, void *u)
{
    size_t a = sz * n;
    mem_t *m = u;
    char *nb = realloc(m->buf, m->len + a + 1);
    if (!nb) return 0;
    m->buf = nb;
    memcpy(m->buf + m->len, p, a);
    m->len += a;
    m->buf[m->len] = 0;
    return a;
}

static const char *base_name(const char *path)
{
    const char *s = path ? strrchr(path, '/') : NULL;
    return (s && s[1]) ? s + 1 : (path ? path : "firmware.bin");
}

/* NOP 从机：multipart Filename + Filedata → :port/cgi-bin/upload.cgi */
static int nop_upload(const nvr_channel_t *ch, const char *file)
{
    CURL *c;
    curl_mime *mime;
    curl_mimepart *part;
    char url[160];
    mem_t m = {0};
    long http = 0;
    int ok = 0;
    int port = ch->onvif_port > 0 ? ch->onvif_port : NVR_DEF_NOP_PORT;

    if (!ch->onvif_ip[0] || !file || !file[0]) return -1;
    snprintf(url, sizeof(url), "http://%s:%d/cgi-bin/upload.cgi", ch->onvif_ip, port);
    c = curl_easy_init();
    if (!c) return -1;
    mime = curl_mime_init(c);
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "Filename");
    curl_mime_data(part, base_name(file), CURL_ZERO_TERMINATED);
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "Filedata");
    curl_mime_filedata(part, file);
    curl_mime_filename(part, base_name(file));
    curl_mime_type(part, "application/octet-stream");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FORBID_REUSE, 1L);
    apply_auth(c, ch);
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    ok = (http == 200);
    if (ok && m.buf) {
        char *p = m.buf;
        while (*p && isspace((unsigned char)*p)) p++;
        if (strncmp(p, "OK", 2) != 0 && *p)
            NVR_LOGW("ipcota", "upload.cgi body=%s", m.buf);
    }
    curl_mime_free(mime);
    curl_easy_cleanup(c);
    free(m.buf);
    NVR_LOGI("ipcota", "upload.cgi %s → HTTP %ld", url, http);
    return ok ? 0 : -1;
}

/* GET upgrade_rate.cgi?cmd=upgrade_rate → body 为 0..100。失败返回 -1。 */
static int nop_upgrade_rate(const nvr_channel_t *ch)
{
    CURL *c;
    char url[180];
    mem_t m = {0};
    long http = 0;
    int rate = -1;
    int port = ch->onvif_port > 0 ? ch->onvif_port : NVR_DEF_NOP_PORT;

    snprintf(url, sizeof(url),
             "http://%s:%d/cgi-bin/upgrade_rate.cgi?cmd=upgrade_rate",
             ch->onvif_ip, port);
    c = curl_easy_init();
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FORBID_REUSE, 1L);
    apply_auth(c, ch);
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    if (http == 200 && m.buf) {
        char *p = m.buf;
        while (*p && isspace((unsigned char)*p)) p++;
        if (isdigit((unsigned char)*p)) rate = atoi(p);
        if (rate < 0) rate = 0;
        if (rate > 100) rate = 100;
    }
    free(m.buf);
    return rate;
}

static int onvif_push(nop_app_t *nop, int chn0, const char *file)
{
    cJSON *req, *args;
    char *js, *out = NULL;
    int ok = 0;
    if (!nop || !file) return -1;
    req = cJSON_CreateObject();
    if (!req) return -1;
    cJSON_AddStringToObject(req, "func", "GUI_upgradeChannelFirmware");
    args = cJSON_AddObjectToObject(req, "args");
    cJSON_AddNumberToObject(args, "channel", chn0); /* mapping 0-based */
    cJSON_AddStringToObject(args, "FileName", file);
    js = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!js) return -1;
    if (nop_app_dispatch(nop, js, &out) == 0 && out) {
        cJSON *o = cJSON_Parse(out);
        cJSON *sc = o ? cJSON_GetObjectItem(o, "statusCode") : NULL;
        ok = (sc && cJSON_IsNumber(sc) && sc->valueint == 200);
        if (o) cJSON_Delete(o);
        nop_app_free_response(out);
    }
    free(js);
    return ok ? 0 : -1;
}

static int json_int(cJSON *o, const char *k, int d)
{
    cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
    return (v && cJSON_IsNumber(v)) ? v->valueint : d;
}

static int battery_too_low(nvr_chan_mgr_t *cm, int chn0, int backend)
{
    char *resp;
    cJSON *root, *c;
    int level = -1;
    if (backend != 0 || !cm) return 0;
    resp = nvr_chan_dev_post(cm, chn0, "X_NightOwl_getChannelBatteryStatus", NULL);
    if (!resp) return 0;
    root = cJSON_Parse(resp);
    free(resp);
    if (!root) return 0;
    if (json_int(root, "statusCode", 0) != 200) {
        cJSON_Delete(root);
        return 0;
    }
    c = cJSON_GetObjectItem(root, "content");
    level = json_int(c, "batteryLevel", -1);
    cJSON_Delete(root);
    return (level >= 0 && level < 20);
}

static void *ipc_thread(void *arg)
{
    nvr_ipc_ota_job_t *j = arg;
    char staging[256];
    const char *file = j->local_file;
    int own_file = 0;
    staging[0] = 0;

    set_st(j->chn, IPC_ST_DOWNLOADING, 0);

    if (battery_too_low(j->cm, j->chn, j->ch.backend)) {
        NVR_LOGW("ipcota", "ch%d 电量<20%，跳过 OTA", j->chn);
        set_st(j->chn, IPC_ST_FAILED, IPC_ERR_UNKNOWN);
        goto done;
    }

    if (!file[0] && j->query_url[0]) {
        nvr_ota_meta_t meta;
        char srv[64], cur[64];
        int q = nvr_ota_query(j->query_url, &meta);
        if (q == -1) { set_st(j->chn, IPC_ST_FAILED, IPC_ERR_NET); goto done; }
        if (q != 0)  { set_st(j->chn, IPC_ST_FAILED, IPC_ERR_SERVER); goto done; }
        nvr_ota_ver_norm(meta.version, j->ch.model, srv, sizeof(srv));
        nvr_ota_ver_norm(j->cur_ver, j->ch.model, cur, sizeof(cur));
        if (!srv[0] || nvr_ota_ver_cmp(srv, cur) <= 0) {
            NVR_LOGI("ipcota", "ch%d 无新固件 srv=%s cur=%s", j->chn,
                     srv[0] ? srv : "-", cur[0] ? cur : "-");
            set_st(j->chn, IPC_ST_NONE, 0);
            goto done;
        }
        if (!meta.url[0]) { set_st(j->chn, IPC_ST_FAILED, IPC_ERR_SERVER); goto done; }
        snprintf(staging, sizeof(staging), "/tmp/ipc_ota_ch%d.bin", j->chn);
        NVR_LOGI("ipcota", "ch%d 下载 %s → %s", j->chn, meta.url, staging);
        if (nvr_ota_http_download(meta.url, staging, NULL, NULL) != 0) {
            set_st(j->chn, IPC_ST_FAILED, IPC_ERR_DOWNLOAD);
            goto done;
        }
        if (meta.checksum[0] && strlen(meta.checksum) >= 32) {
            char got[40];
            if (nvr_ota_file_md5(staging, got, sizeof(got)) != 0) {
                unlink(staging);
                set_st(j->chn, IPC_ST_FAILED, IPC_ERR_VERIFY);
                goto done;
            }
            {
                int i, same = 1;
                for (i = 0; i < 32 && meta.checksum[i] && got[i]; i++) {
                    char a = (char)tolower((unsigned char)meta.checksum[i]);
                    char b = (char)tolower((unsigned char)got[i]);
                    if (a != b) { same = 0; break; }
                }
                if (!same) {
                    NVR_LOGE("ipcota", "ch%d MD5 不符", j->chn);
                    unlink(staging);
                    set_st(j->chn, IPC_ST_FAILED, IPC_ERR_VERIFY);
                    goto done;
                }
            }
        }
        {
            struct stat st;
            if (stat(staging, &st) != 0 || st.st_size < 1024) {
                unlink(staging);
                set_st(j->chn, IPC_ST_FAILED, IPC_ERR_STORAGE);
                goto done;
            }
        }
        file = staging;
        own_file = 1;
    }

    if (!file || !file[0]) {
        set_st(j->chn, IPC_ST_FAILED, IPC_ERR_UNKNOWN);
        goto done;
    }

    if (!j->automatic) {
        set_st(j->chn, IPC_ST_DOWNLOADED, 0);
        goto done;
    }

    nvr_chan_set_fw_updating(j->cm, j->chn, 1);
    NVR_LOGI("ipcota", "ch%d 下推 %s backend=%d", j->chn, file, j->ch.backend);
    if (j->ch.backend == 0) {
        if (nop_upload(&j->ch, file) != 0) {
            set_st(j->chn, IPC_ST_FAILED, IPC_ERR_UNKNOWN);
            goto done;
        }
        /* 相机接收完给 5s 查 100%；轮询最多 ~90s。 */
        {
            int i, rate, last = 0;
            for (i = 0; i < 90; i++) {
                rate = nop_upgrade_rate(&j->ch);
                if (rate >= 0) last = rate;
                if (rate >= 100) break;
                sleep(1);
            }
            NVR_LOGI("ipcota", "ch%d camera rate=%d", j->chn, last);
        }
    } else {
        if (onvif_push(j->nop, j->chn, file) != 0) {
            set_st(j->chn, IPC_ST_FAILED, IPC_ERR_UNKNOWN);
            goto done;
        }
    }
    set_st(j->chn, IPC_ST_DOWNLOADED, 0);

done:
    nvr_chan_set_fw_updating(j->cm, j->chn, 0);
    if (own_file && staging[0]) unlink(staging);
    pthread_mutex_lock(&g_lk); g_busy = 0; pthread_mutex_unlock(&g_lk);
    free(j);
    return NULL;
}

int nvr_ipc_ota_start(const nvr_ipc_ota_job_t *job)
{
    nvr_ipc_ota_job_t *j;
    pthread_t th;
    if (!job || job->chn < 0 || job->chn >= NVR_MAX_IPC_OTA) return -1;
    if (!job->local_file[0] && !job->query_url[0]) return -1;
    pthread_mutex_lock(&g_lk);
    if (g_busy) { pthread_mutex_unlock(&g_lk); return -2; }
    g_busy = 1;
    pthread_mutex_unlock(&g_lk);

    j = calloc(1, sizeof(*j));
    if (!j) { pthread_mutex_lock(&g_lk); g_busy = 0; pthread_mutex_unlock(&g_lk); return -1; }
    *j = *job;
    set_st(j->chn, IPC_ST_DOWNLOADING, 0);
    if (pthread_create(&th, NULL, ipc_thread, j) != 0) {
        set_st(j->chn, IPC_ST_FAILED, IPC_ERR_UNKNOWN);
        pthread_mutex_lock(&g_lk); g_busy = 0; pthread_mutex_unlock(&g_lk);
        free(j);
        return -1;
    }
    pthread_detach(th);
    return 0;
}
