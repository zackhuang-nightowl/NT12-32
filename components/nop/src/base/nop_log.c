#include "nop_sdk/nop_log.h"

#include <stdarg.h>
#include <stdio.h>

static nop_log_level_t g_level = NOP_LOG_INFO;
static nop_log_sink_fn g_sink  = NULL;
static void           *g_user  = NULL;

static const char *level_tag(nop_log_level_t l)
{
    switch (l) {
    case NOP_LOG_ERROR: return "E";
    case NOP_LOG_WARN:  return "W";
    case NOP_LOG_INFO:  return "I";
    case NOP_LOG_DEBUG: return "D";
    default:            return "T";
    }
}

void nop_log_set_sink(nop_log_sink_fn sink, void *user)
{
    g_sink = sink;
    g_user = user;
}

void nop_log_set_level(nop_log_level_t level)
{
    g_level = level;
}

void nop_log_emit(nop_log_level_t level, const char *fmt, ...)
{
    char    line[512];
    int     n;
    va_list ap;

    if (level > g_level)
        return;

    n = snprintf(line, sizeof(line), "[nop:%s] ", level_tag(level));
    if (n < 0 || (size_t)n >= sizeof(line))
        n = 0;

    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);

    if (g_sink) {
        g_sink(level, line, g_user);
    } else {
        fputs(line, stderr);
        fputc('\n', stderr);
    }
}
