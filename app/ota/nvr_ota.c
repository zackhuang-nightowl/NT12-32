/***************************************************************************************
 *  nvr_ota.c — 下载/本地包 → MD5 + 版本校验 → 交更新器烧非活动 A/B 槽。
 ***************************************************************************************/
#include "nvr_ota.h"
#include "nvr_defaults.h"
#include "nvr_log.h"

#include <ctype.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OTA_MIN_BYTES   (256 * 1024)
#define OTA_HDR_MAX     1024

static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static int   g_progress = -2;         /* -2=idle -1=failed 0..100 */
static char  g_state[16] = "idle";
static int   g_busy = 0;

typedef struct {
    char url[512], staging[256], updater[256];
    char md5[40], cur_ver[40], pkg_ver[40];
    int  force;
    int  local;                       /* 1=src 已是本地文件 */
} ota_job_t;

typedef struct {
    int  has_magic;
    int  force;
    int  header_len;
    char version[40];
    char md5[40];
} ota_hdr_t;

static void set_state(const char *s, int prog)
{
    pthread_mutex_lock(&g_lk);
    snprintf(g_state, sizeof(g_state), "%s", s);
    g_progress = prog;
    pthread_mutex_unlock(&g_lk);
}

static int on_progress(void *p, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ul, curl_off_t un)
{
    (void)p; (void)ul; (void)un;
    if (dltotal > 0) set_state("downloading", (int)(dlnow * 100 / dltotal));
    return 0;
}

static int is_http(const char *s)
{
    return s && (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8));
}

static int hex_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    size_t i = 0;
    for (; a[i] && b[i]; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'F') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'F') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return a[i] == 0 && b[i] == 0;
}

static int ver_token(const char **p)
{
    const char *s = *p;
    while (*s && !isdigit((unsigned char)*s)) s++;
    int v = 0;
    if (!*s) { *p = s; return 0; }
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    *p = s;
    return v;
}

/* >0 : a>b；0 : 相等；<0 : a<b。只比数字段（1.0.10 > 1.0.9）。 */
static int ver_cmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    for (int i = 0; i < 4; i++) {
        int va = ver_token(&a), vb = ver_token(&b);
        if (va != vb) return va - vb;
    }
    return 0;
}

static int file_md5_hex(const char *path, long offset, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (offset > 0 && fseek(f, offset, SEEK_SET) != 0) { fclose(f); return -1; }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { fclose(f); return -1; }
    int rc = -1;
    unsigned char md[16];
    unsigned n = 0;
    if (EVP_DigestInit_ex(ctx, EVP_md5(), NULL) != 1) goto done;
    unsigned char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (EVP_DigestUpdate(ctx, buf, r) != 1) goto done;
    }
    if (EVP_DigestFinal_ex(ctx, md, &n) != 1 || n != 16) goto done;
    static const char *H = "0123456789abcdef";
    if (cap < 33) goto done;
    for (unsigned i = 0; i < 16; i++) {
        out[2 * i]     = H[(md[i] >> 4) & 0xF];
        out[2 * i + 1] = H[md[i] & 0xF];
    }
    out[32] = 0;
    rc = 0;
done:
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return rc;
}

static void trim_crlf(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = 0;
}

static int parse_force_val(const char *v)
{
    if (!v || !v[0]) return 0;
    if (v[0] == '1' && !v[1]) return 1;
    if (!strcmp(v, "true") || !strcmp(v, "TRUE") || !strcmp(v, "yes")) return 1;
    return 0;
}

static int parse_header(const char *path, ota_hdr_t *h)
{
    memset(h, 0, sizeof(*h));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char buf[OTA_HDR_MAX + 1];
    size_t n = fread(buf, 1, OTA_HDR_MAX, f);
    fclose(f);
    if (n < 10 || memcmp(buf, NVR_OTA_MAGIC, 10) != 0) return 0;
    h->has_magic = 1;

    /* 魔数后可跟可选换行，再跟 squashfs / 文本键值 / 二进制头 */
    int skip = 10;
    if (skip < (int)n && buf[skip] == '\r') skip++;
    if (skip < (int)n && buf[skip] == '\n') skip++;

    /* nopUpgrade[+换行]+hsqs → 工厂强制包，payload 从 squashfs 起 */
    if (skip + 4 <= (int)n && memcmp(buf + skip, "hsqs", 4) == 0) {
        h->header_len = skip;
        h->force = 1;
        return 0;
    }

    /* 文本头: version=/md5=/force= 直到空行 */
    if (skip < (int)n && (memcmp(buf + skip, "version=", 8) == 0 ||
                          memcmp(buf + skip, "md5=", 4) == 0 ||
                          memcmp(buf + skip, "MD5=", 4) == 0 ||
                          memcmp(buf + skip, "force=", 6) == 0)) {
        buf[n] = 0;
        char *base = (char *)buf;
        char *p = base + skip;
        char *end = base + n;
        while (p < end) {
            if (*p == '\n' && (p[1] == '\n' || p[1] == '\0')) {
                h->header_len = (int)((p + 2) - base);
                break;
            }
            if (*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                h->header_len = (int)((p + 4) - base);
                break;
            }
            char *eol = strpbrk(p, "\r\n");
            if (!eol) break;
            char line[128];
            size_t ln = (size_t)(eol - p);
            if (ln >= sizeof(line)) ln = sizeof(line) - 1;
            memcpy(line, p, ln);
            line[ln] = 0;
            trim_crlf(line);
            p = eol + 1;
            if (*p == '\n' && eol[0] == '\r') p++;
            if (!line[0]) {
                h->header_len = (int)(p - base);
                break;
            }
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq++ = 0;
            if (!strcmp(line, "version"))
                snprintf(h->version, sizeof(h->version), "%s", eq);
            else if (!strcmp(line, "md5") || !strcmp(line, "MD5"))
                snprintf(h->md5, sizeof(h->md5), "%s", eq);
            else if (!strcmp(line, "force"))
                h->force = parse_force_val(eq);
        }
        if (h->header_len <= 0) h->header_len = skip;
        if (!h->version[0]) h->force = 1;   /* 有魔数无版本 = 强制 */
        return 0;
    }

    /* 二进制头: magic(10) + flags(1) + version(32) + md5(32) */
    unsigned char c = (n > 10) ? buf[10] : 0;
    if (n >= 75 && (c == 0 || c == 1)) {
        h->force = (c & 1) ? 1 : 0;
        memcpy(h->version, buf + 11, 32);
        h->version[32] = 0;
        memcpy(h->md5, buf + 43, 32);
        h->md5[32] = 0;
        h->header_len = 75;
        if (!h->version[0]) h->force = 1;
        return 0;
    }

    /* 仅魔数，后面直接是 payload */
    h->header_len = 10;
    h->force = 1;
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    if (!src || !dst) return -1;
    if (strcmp(src, dst) == 0) return 0;
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[8192];
    size_t r;
    int rc = 0;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) { rc = -1; break; }
    }
    fclose(out);
    fclose(in);
    return rc;
}

static int extract_payload(const char *src, int off, const char *dst)
{
    if (off <= 0) return copy_file(src, dst);
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    if (fseek(in, off, SEEK_SET) != 0) { fclose(in); return -1; }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[8192];
    size_t r;
    int rc = 0;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) { rc = -1; break; }
    }
    fclose(out);
    fclose(in);
    return rc;
}

static int download_url(const char *url, const char *dst)
{
    FILE *f = fopen(dst, "wb");
    if (!f) { NVR_LOGE("ota", "无法写 %s", dst); return -1; }
    CURL *c = curl_easy_init();
    int ok = 0;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        CURLcode rc = curl_easy_perform(c);
        long http = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
        ok = (rc == CURLE_OK && (http == 200 || http == 0));
        curl_easy_cleanup(c);
    }
    fclose(f);
    return ok ? 0 : -1;
}

static int verify_and_extract(const ota_job_t *j, char *img, size_t img_cap)
{
    struct stat st;
    if (stat(j->staging, &st) != 0 || st.st_size < OTA_MIN_BYTES) {
        NVR_LOGE("ota", "固件过小或不可读(%s size=%lld)", j->staging,
                 (long long)st.st_size);
        return -1;
    }

    ota_hdr_t h;
    if (parse_header(j->staging, &h) != 0) {
        NVR_LOGE("ota", "读包头失败");
        return -1;
    }

    int force = j->force || h.force;
    /* 以 nopUpgrade 起头但头里没有 version → 工厂强制包，跳过版本比较 */
    if (h.has_magic && !h.version[0] && !j->pkg_ver[0]) force = 1;

    const char *expect_md5 = h.md5[0] ? h.md5 : j->md5;
    if (!expect_md5[0]) {
        NVR_LOGE("ota", "缺少 MD5（包头或命令 args.md5）");
        return -1;
    }

    char got[40];
    if (file_md5_hex(j->staging, h.header_len, got, sizeof(got)) != 0) {
        NVR_LOGE("ota", "MD5 计算失败");
        return -1;
    }
    NVR_LOGI("ota", "payload %lld bytes md5=%s force=%d",
             (long long)st.st_size - h.header_len, got, force);
    if (!hex_eq(expect_md5, got)) {
        NVR_LOGE("ota", "MD5 不符 expect=%s got=%s", expect_md5, got);
        return -1;
    }

    const char *pkg_ver = h.version[0] ? h.version : j->pkg_ver;
    if (!force) {
        if (!pkg_ver[0]) {
            NVR_LOGE("ota", "缺少版本号（非强制升级必须更高版本）");
            return -1;
        }
        if (ver_cmp(pkg_ver, j->cur_ver) <= 0) {
            NVR_LOGE("ota", "版本不高于当前 pkg=%s cur=%s（非强制）",
                     pkg_ver, j->cur_ver[0] ? j->cur_ver : "?");
            return -1;
        }
    } else {
        NVR_LOGI("ota", "强制升级，跳过版本比较 pkg=%s cur=%s",
                 pkg_ver[0] ? pkg_ver : "-", j->cur_ver);
    }

    if (h.header_len > 0) {
        snprintf(img, img_cap, "%s.img", j->staging);
        if (extract_payload(j->staging, h.header_len, img) != 0) {
            NVR_LOGE("ota", "抽出 payload 失败");
            return -1;
        }
    } else {
        snprintf(img, img_cap, "%s", j->staging);
    }
    return 0;
}

static void *ota_thread(void *arg)
{
    ota_job_t *j = arg;
    char img[280];
    img[0] = 0;

    set_state("downloading", 0);
    if (j->local) {
        NVR_LOGI("ota", "本地固件 %s → %s", j->url, j->staging);
        if (copy_file(j->url, j->staging) != 0) {
            NVR_LOGE("ota", "拷贝失败");
            set_state("failed", -1);
            goto out;
        }
        set_state("downloading", 80);
    } else {
        NVR_LOGI("ota", "下载固件 %s → %s", j->url, j->staging);
        if (download_url(j->url, j->staging) != 0) {
            NVR_LOGE("ota", "下载失败");
            unlink(j->staging);
            set_state("failed", -1);
            goto out;
        }
    }

    set_state("verifying", 90);
    if (verify_and_extract(j, img, sizeof(img)) != 0) {
        unlink(j->staging);
        if (img[0] && strcmp(img, j->staging) != 0) unlink(img);
        set_state("failed", -1);
        goto out;
    }

    if (!j->updater[0] || access(j->updater, X_OK) != 0) {
        NVR_LOGE("ota", "无更新器或不可执行(%s)，拒绝只暂存", j->updater);
        set_state("failed", -1);
        goto out;
    }

    set_state("updating", 100);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "%s %s", j->updater, img);
    NVR_LOGI("ota", "交更新器(A/B): %s", cmd);
    int rc = system(cmd);
    if (rc != 0) {
        NVR_LOGE("ota", "更新器返回 %d", rc);
        set_state("failed", -1);
        goto out;
    }
    set_state("done", 100);

out:
    pthread_mutex_lock(&g_lk); g_busy = 0; pthread_mutex_unlock(&g_lk);
    free(j);
    return NULL;
}

int nvr_ota_start(const char *src, const char *staging, const char *updater,
                  const char *md5_hex, const char *cur_version,
                  const char *pkg_version, int force)
{
    if (!src || !src[0]) return -1;
    pthread_mutex_lock(&g_lk);
    if (g_busy) { pthread_mutex_unlock(&g_lk); return -2; }
    g_busy = 1;
    pthread_mutex_unlock(&g_lk);

    ota_job_t *j = calloc(1, sizeof(*j));
    if (!j) { g_busy = 0; return -1; }
    snprintf(j->url, sizeof(j->url), "%s", src);
    snprintf(j->staging, sizeof(j->staging), "%s",
             staging && staging[0] ? staging : NVR_DEF_OTA_STAGING);
    snprintf(j->updater, sizeof(j->updater), "%s",
             updater && updater[0] ? updater : NVR_DEF_OTA_UPDATER);
    if (md5_hex && md5_hex[0]) snprintf(j->md5, sizeof(j->md5), "%s", md5_hex);
    if (cur_version && cur_version[0])
        snprintf(j->cur_ver, sizeof(j->cur_ver), "%s", cur_version);
    else
        snprintf(j->cur_ver, sizeof(j->cur_ver), "%s", NVR_DEF_FW_VERSION);
    if (pkg_version && pkg_version[0])
        snprintf(j->pkg_ver, sizeof(j->pkg_ver), "%s", pkg_version);
    j->force = force ? 1 : 0;
    j->local = is_http(src) ? 0 : 1;

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
