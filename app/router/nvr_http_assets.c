/***************************************************************************************
 *  nvr_http_assets.c — 抓拍缓存 + 事件 MP4 导出任务 + 8089 GET 下发。
 ***************************************************************************************/
#include "nvr_http_assets.h"
#include "rsdk_backup.h"
#include "nop_sdk/nop_http_server.h"
#include "nvr_log.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NVR_MAX_CH 32
#define DL_MAX     4
#define DL_DIR     "/tmp/nvr_dl"

typedef struct {
    uint8_t *data;
    int      len;
} jpeg_slot_t;

typedef struct {
    int               used;
    volatile int      running;
    int               percent;     /* 0..100; -1 失败 */
    int               filesize;
    int               chn0, stream;
    uint32_t          t0, t1;
    struct rsdk_group *group;
    char              process_id[96];
    char              token[80];
    char              path[256];
} dl_job_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static jpeg_slot_t     g_jpeg[NVR_MAX_CH];
static dl_job_t        g_dl[DL_MAX];

static void sanitize_token(const char *in, char *out, int cap)
{
    int j = 0;
    if (!in) { if (cap > 0) out[0] = 0; return; }
    for (; *in && j < cap - 1; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '_' || c == '-' || c == '.') out[j++] = (char)c;
        else if (c == ' ') out[j++] = '_';
    }
    out[j] = 0;
}

int nvr_http_assets_put_jpeg(int chn0, const void *jpeg, int len)
{
    if (chn0 < 0 || chn0 >= NVR_MAX_CH || !jpeg || len <= 0) return -1;
    uint8_t *p = (uint8_t *)malloc((size_t)len);
    if (!p) return -1;
    memcpy(p, jpeg, (size_t)len);
    pthread_mutex_lock(&g_mu);
    free(g_jpeg[chn0].data);
    g_jpeg[chn0].data = p;
    g_jpeg[chn0].len = len;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int nvr_http_assets_get_jpeg(int chn0, const void **jpeg, int *len)
{
    if (chn0 < 0 || chn0 >= NVR_MAX_CH) return -1;
    pthread_mutex_lock(&g_mu);
    if (!g_jpeg[chn0].data || g_jpeg[chn0].len <= 0) {
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    if (jpeg) *jpeg = g_jpeg[chn0].data;
    if (len) *len = g_jpeg[chn0].len;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

static void *dl_thread(void *arg)
{
    dl_job_t *j = (dl_job_t *)arg;
    rsdk_export_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.fmt = RSDK_EXPORT_MP4;
    mkdir(DL_DIR, 0755);
    pthread_mutex_lock(&g_mu);
    j->percent = 1;
    pthread_mutex_unlock(&g_mu);
    rsdk_err_t rc = rsdk_backup_export_stream(j->group, j->t0, j->t1, j->chn0,
                                              j->stream, &opt, j->path);
    pthread_mutex_lock(&g_mu);
    if (rc != RSDK_OK) {
        j->percent = -1;
        j->filesize = 0;
        unlink(j->path);
        NVR_LOGW("dl", "export fail ch%d %u-%u rc=%d", j->chn0, j->t0, j->t1, (int)rc);
    } else {
        struct stat st;
        off_t sz = (stat(j->path, &st) == 0) ? st.st_size : 0;
        j->filesize = (sz > 0 && sz <= 0x7FFFFFFF) ? (int)sz : (sz > 0x7FFFFFFF ? 0x7FFFFFFF : 0);
        j->percent = 100;
    }
    j->running = 0;
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

int nvr_http_dl_start(struct rsdk_group *group, const char *process_id,
                      int chn0, uint32_t t0, uint32_t t1, int stream)
{
    if (!group || !process_id || !process_id[0]) return -1;
    pthread_mutex_lock(&g_mu);
    int same = -1, unused = -1, done = -1;
    for (int i = 0; i < DL_MAX; i++) {
        if (g_dl[i].used && strcmp(g_dl[i].process_id, process_id) == 0) {
            if (g_dl[i].running) { pthread_mutex_unlock(&g_mu); return 0; }
            same = i;
            break;
        }
        if (!g_dl[i].used && unused < 0) unused = i;
        else if (g_dl[i].used && !g_dl[i].running && done < 0) done = i;
    }
    int idx = (same >= 0) ? same : (unused >= 0 ? unused : done);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mu);
        return -2;
    }
    if (g_dl[idx].path[0]) unlink(g_dl[idx].path);
    memset(&g_dl[idx], 0, sizeof(g_dl[idx]));
    dl_job_t *slot = &g_dl[idx];
    slot->used = 1;
    slot->running = 1;
    slot->percent = 0;
    slot->chn0 = chn0;
    slot->stream = stream;
    slot->t0 = t0;
    slot->t1 = t1;
    slot->group = group;
    snprintf(slot->process_id, sizeof(slot->process_id), "%s", process_id);
    sanitize_token(process_id, slot->token, (int)sizeof(slot->token));
    snprintf(slot->path, sizeof(slot->path), "%s/%s.mp4", DL_DIR, slot->token);
    pthread_mutex_unlock(&g_mu);

    pthread_t tid;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &at, dl_thread, slot) != 0) {
        pthread_mutex_lock(&g_mu);
        slot->running = 0;
        slot->percent = -1;
        pthread_mutex_unlock(&g_mu);
        pthread_attr_destroy(&at);
        return -1;
    }
    pthread_attr_destroy(&at);
    return 0;
}

int nvr_http_dl_progress(const char *process_id, int *percent,
                         char *url_token, int token_cap, int *filesize)
{
    if (!process_id || !process_id[0]) return -1;
    pthread_mutex_lock(&g_mu);
    dl_job_t *j = NULL;
    for (int i = 0; i < DL_MAX; i++)
        if (g_dl[i].used && strcmp(g_dl[i].process_id, process_id) == 0)
            { j = &g_dl[i]; break; }
    if (!j) {
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    if (percent) *percent = j->percent;
    if (url_token && token_cap > 0)
        snprintf(url_token, (size_t)token_cap, "%s", j->token);
    if (filesize) *filesize = (j->percent == 100) ? j->filesize : 0;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

static int serve_file(int fd, const char *path, const char *ctype)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f);
        nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
        return 1; }
    long sz = ftell(f);
    if (sz < 0 || sz > 256 * 1024 * 1024) { fclose(f);
        nop_http_send_response(fd, 500, "Error", "text/plain", "too large", 9);
        return 1; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f);
        nop_http_send_response(fd, 500, "Error", "text/plain", "oom", 3);
        return 1; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    nop_http_send_response(fd, 200, "OK", ctype, buf, n);
    free(buf);
    return 1;
}

int nvr_http_assets_serve(int fd, const char *uri)
{
    if (!uri) return 0;
    if (strncmp(uri, "/snapshot/ch", 12) == 0) {
        int ch1 = atoi(uri + 12);
        if (ch1 < 1) {
            nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
            return 1;
        }
        pthread_mutex_lock(&g_mu);
        int chn0 = ch1 - 1;
        uint8_t *copy = NULL;
        int len = 0;
        if (chn0 >= 0 && chn0 < NVR_MAX_CH && g_jpeg[chn0].data && g_jpeg[chn0].len > 0) {
            len = g_jpeg[chn0].len;
            copy = (uint8_t *)malloc((size_t)len);
            if (copy) memcpy(copy, g_jpeg[chn0].data, (size_t)len);
        }
        pthread_mutex_unlock(&g_mu);
        if (!copy) {
            nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
            return 1;
        }
        nop_http_send_response(fd, 200, "OK", "image/jpeg", copy, (size_t)len);
        free(copy);
        return 1;
    }
    if (strncmp(uri, "/download/", 10) == 0) {
        char token[80];
        snprintf(token, sizeof(token), "%s", uri + 10);
        char *q = strchr(token, '?');
        if (q) *q = 0;
        size_t n = strlen(token);
        if (n > 4 && strcmp(token + n - 4, ".mp4") == 0)
            token[n - 4] = 0;
        char path[256] = {0};
        pthread_mutex_lock(&g_mu);
        for (int i = 0; i < DL_MAX; i++) {
            if (g_dl[i].used && strcmp(g_dl[i].token, token) == 0 && g_dl[i].percent == 100) {
                snprintf(path, sizeof(path), "%s", g_dl[i].path);
                break;
            }
        }
        pthread_mutex_unlock(&g_mu);
        if (!path[0]) {
            nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
            return 1;
        }
        return serve_file(fd, path, "video/mp4");
    }
    return 0;
}
