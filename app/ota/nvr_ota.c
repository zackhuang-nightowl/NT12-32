/***************************************************************************************
 *  nvr_ota.c — NVR 固件自升级。见 nvr_ota.h。
 ***************************************************************************************/
#include "nvr_ota.h"
#include "nvr_log.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static int   g_progress = -2;         /* -2=idle -1=failed 0..100 */
static char  g_state[16] = "idle";
static int   g_busy = 0;

typedef struct { char url[512], staging[256], updater[256]; } ota_job_t;

static void set_state(const char *s, int prog)
{
    pthread_mutex_lock(&g_lk);
    snprintf(g_state, sizeof(g_state), "%s", s);
    g_progress = prog;
    pthread_mutex_unlock(&g_lk);
}

static int on_progress(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ul, curl_off_t un)
{
    (void)p; (void)ul; (void)un;
    if (dltotal > 0) set_state("downloading", (int)(dlnow * 100 / dltotal));
    return 0;
}

static void *ota_thread(void *arg)
{
    ota_job_t *j = arg;
    set_state("downloading", 0);
    NVR_LOGI("ota", "下载固件 %s → %s", j->url, j->staging);

    FILE *f = fopen(j->staging, "wb");
    if (!f) { NVR_LOGE("ota", "无法写 %s", j->staging); set_state("failed", -1); goto out; }

    CURL *c = curl_easy_init();
    int ok = 0;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, j->url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        CURLcode rc = curl_easy_perform(c);
        long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
        ok = (rc == CURLE_OK && (http == 200 || http == 0));
        curl_easy_cleanup(c);
    }
    fclose(f);
    if (!ok) { NVR_LOGE("ota", "下载失败"); set_state("failed", -1); goto out; }

    set_state("verifying", 100);
    /* TODO: 校验(大小/SHA/签名)——待接设备固件头规则 */
    NVR_LOGI("ota", "下载完成，交更新器: %s", j->updater[0] ? j->updater : "(仅暂存)");

    if (j->updater[0]) {
        set_state("updating", 100);
        char cmd[600]; snprintf(cmd, sizeof(cmd), "%s %s", j->updater, j->staging);
        int rc = system(cmd);   /* 设备更新脚本负责烧写+重启 */
        NVR_LOGW("ota", "更新器返回 %d", rc);
    }
    set_state("done", 100);

out:
    pthread_mutex_lock(&g_lk); g_busy = 0; pthread_mutex_unlock(&g_lk);
    free(j);
    return NULL;
}

int nvr_ota_start(const char *url, const char *staging, const char *updater)
{
    if (!url || !url[0]) return -1;
    pthread_mutex_lock(&g_lk);
    if (g_busy) { pthread_mutex_unlock(&g_lk); return -2; }   /* 已在升级 */
    g_busy = 1;
    pthread_mutex_unlock(&g_lk);

    ota_job_t *j = calloc(1, sizeof(*j));
    if (!j) { g_busy = 0; return -1; }
    snprintf(j->url, sizeof(j->url), "%s", url);
    snprintf(j->staging, sizeof(j->staging), "%s", staging && staging[0] ? staging : "/mnt/update.rom");
    snprintf(j->updater, sizeof(j->updater), "%s", updater ? updater : "");

    pthread_t th;
    if (pthread_create(&th, NULL, ota_thread, j) != 0) { g_busy = 0; free(j); return -1; }
    pthread_detach(th);
    return 0;
}

int nvr_ota_progress(void)
{
    pthread_mutex_lock(&g_lk); int p = g_progress; pthread_mutex_unlock(&g_lk);
    return (p == -2) ? 0 : p;
}
const char *nvr_ota_state(void) { return g_state; }
void nvr_ota_deinit(void) {}
