/**
 * @file nop_log.h
 * @brief Leveled logging. Output is delivered to a sink (default: stderr),
 *        which a port/firmware may override to route into its own logger.
 */
#ifndef NOP_SDK_LOG_H
#define NOP_SDK_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nop_log_level {
    NOP_LOG_ERROR = 0,
    NOP_LOG_WARN,
    NOP_LOG_INFO,
    NOP_LOG_DEBUG,
    NOP_LOG_TRACE
} nop_log_level_t;

/** Sink receives a fully-formatted, newline-terminated line. */
typedef void (*nop_log_sink_fn)(nop_log_level_t level, const char *line, void *user);

/** Install a custom sink (NULL restores the default stderr sink). */
void nop_log_set_sink(nop_log_sink_fn sink, void *user);

/** Drop messages above @p level (default NOP_LOG_INFO). */
void nop_log_set_level(nop_log_level_t level);

/** Printf-style log entry; usually invoked via the NOP_LOG* macros. */
void nop_log_emit(nop_log_level_t level, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#define NOP_LOGE(...) nop_log_emit(NOP_LOG_ERROR, __VA_ARGS__)
#define NOP_LOGW(...) nop_log_emit(NOP_LOG_WARN,  __VA_ARGS__)
#define NOP_LOGI(...) nop_log_emit(NOP_LOG_INFO,  __VA_ARGS__)
#define NOP_LOGD(...) nop_log_emit(NOP_LOG_DEBUG, __VA_ARGS__)
#define NOP_LOGT(...) nop_log_emit(NOP_LOG_TRACE, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_LOG_H */
