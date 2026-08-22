/***************************************************************************************
 *  nvr_log.h — 轻量运行期日志（设备上排障用，header-only）
 *
 *  用法：NVR_LOGI("chan", "ch%d online", chn);   → [12:00:01] I/chan: ch3 online
 *  级别：E=0 W=1 I=2 D=3；运行期用环境变量 NVR_LOG_LEVEL 控制（默认 2=INFO）。
 *        例：设备上 `export NVR_LOG_LEVEL=3` 打开全部 DEBUG。
 *
 *  ── 分类(category)门控 ──────────────────────────────────────────────────────
 *  E/W/I 一律按级别打(重要,恒可见)。DEBUG(NVR_LOGD)的量最大,故除全局级别外再按
 *  「类别」门控:类别 == tag(如 "rtsp"/"playback"/"puller"/"onvif")。
 *    - 环境变量 NVR_LOG_CATS = 逗号分隔的类别白名单;"*"/"all" = 全开。
 *    - 某条 NVR_LOGD 打印,当 (全局级别>=D) 或 (其 tag 在 NVR_LOG_CATS 白名单) 时才输出。
 *  这样默认(级别 I、无 CATS)不打任何 DEBUG;排障某子系统时,在对应「启动方式」
 *  (/dvr/run 或 shell)里 `export NVR_LOG_CATS=rtsp,playback` 即单独打开该类别的详细日志,
 *  不淹没其它子系统、也不长期占日志空间。有意义的详细打印保留在代码里,按需开启。
 *  输出：stderr（看护脚本已把子进程 stdout/stderr 收进日志/console）。
 ***************************************************************************************/
#ifndef NVR_LOG_H
#define NVR_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NVR_LOG_E = 0, NVR_LOG_W = 1, NVR_LOG_I = 2, NVR_LOG_D = 3 };

static inline int nvr_log_level(void)
{
    static int lvl = -1;
    if (lvl < 0) {
        const char *e = getenv("NVR_LOG_LEVEL");
        lvl = e ? atoi(e) : NVR_LOG_I;
        if (lvl < 0) lvl = 0;
    }
    return lvl;
}

/* 该类别(tag)的 DEBUG 是否开启:NVR_LOG_CATS 含 "*"/"all" 或含该 tag → 开。缓存一次。 */
static inline int nvr_log_cat_enabled(const char *tag)
{
    static const char *cats = (const char *)-1;   /* -1 = 未取过 */
    if (cats == (const char *)-1) cats = getenv("NVR_LOG_CATS");
    if (!cats || !cats[0] || !tag || !tag[0]) return 0;
    if (strcmp(cats, "*") == 0 || strcmp(cats, "all") == 0) return 1;
    /* 子串匹配 + 边界检查,避免 "rtsp" 命中 "rtspx"。 */
    const char *p = cats;
    size_t tl = strlen(tag);
    while ((p = strstr(p, tag)) != NULL) {
        char before = (p == cats) ? ',' : p[-1];
        char after  = p[tl];
        if ((before == ',' || before == ' ') && (after == ',' || after == ' ' || after == '\0'))
            return 1;
        p += tl;
    }
    return 0;
}

static inline void nvr_log_emit(int lvl, const char *tag, const char *fmt, ...)
{
    static const char L[4] = { 'E', 'W', 'I', 'D' };
    char ts[16];
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
    fprintf(stderr, "[%s] %c/%s: ", ts, L[(lvl < 0 || lvl > 3) ? 2 : lvl], tag ? tag : "");
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

#define NVR_LOG(lvl, tag, ...) do { if ((lvl) <= nvr_log_level()) nvr_log_emit((lvl), (tag), __VA_ARGS__); } while (0)
#define NVR_LOGE(tag, ...) NVR_LOG(NVR_LOG_E, tag, __VA_ARGS__)
#define NVR_LOGW(tag, ...) NVR_LOG(NVR_LOG_W, tag, __VA_ARGS__)
#define NVR_LOGI(tag, ...) NVR_LOG(NVR_LOG_I, tag, __VA_ARGS__)
/* DEBUG:全局级别>=D 或 该类别(tag)在 NVR_LOG_CATS 白名单 → 打印。 */
#define NVR_LOGD(tag, ...) do { \
    if (NVR_LOG_D <= nvr_log_level() || nvr_log_cat_enabled(tag)) \
        nvr_log_emit(NVR_LOG_D, (tag), __VA_ARGS__); \
} while (0)

#ifdef __cplusplus
}
#endif
#endif /* NVR_LOG_H */
