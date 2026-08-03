/***************************************************************************************
 *  nvr_log.h — 轻量运行期日志（设备上排障用，header-only）
 *
 *  用法：NVR_LOGI("chan", "ch%d online", chn);   → [12:00:01] I/chan: ch3 online
 *  级别：E=0 W=1 I=2 D=3；运行期用环境变量 NVR_LOG_LEVEL 控制（默认 2=INFO）。
 *        例：设备上 `export NVR_LOG_LEVEL=3` 打开 DEBUG。
 *  输出：stderr（看护脚本已把子进程 stdout/stderr 收进日志/console）。
 ***************************************************************************************/
#ifndef NVR_LOG_H
#define NVR_LOG_H

#include <stdio.h>
#include <stdarg.h>
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
#define NVR_LOGD(tag, ...) NVR_LOG(NVR_LOG_D, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* NVR_LOG_H */
