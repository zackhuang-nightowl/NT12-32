/**
 * @file hal_record.h
 * @brief Recording HAL: per-channel record enable + schedules. Firmware
 *        registers this under HAL_RECORD; backs CAP_RECORD commands
 *        (X_NightOwl_get/setChannelRecordingSwitch,
 *        X_NightOwl_get/setChannelContinuousRecordingSchedule).
 */
#ifndef NOP_SDK_HAL_RECORD_H
#define NOP_SDK_HAL_RECORD_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max schedule rules the HAL carries per channel; handler clamps to this. */
#define HAL_RECORD_MAX_RULES 16

/**
 * One recording-schedule rule. @p weekday_mask uses bit 0 = Monday .. bit 6 =
 * Sunday (the handler maps to/from the JSON 1..7 weekday array). Times are
 * "HHMMSS" zero-terminated strings, e.g. "000000".."235959".
 */
typedef struct hal_record_rule {
    char     id[32];
    unsigned weekday_mask;     /**< bit0=Mon .. bit6=Sun */
    char     start_time[8];    /**< "HHMMSS" */
    char     end_time[8];      /**< "HHMMSS" */
} hal_record_rule_t;

/** A channel's continuous-recording schedule. */
typedef struct hal_record_schedule {
    int               rule_count;                   /**< valid entries in rules[] */
    hal_record_rule_t rules[HAL_RECORD_MAX_RULES];
} hal_record_schedule_t;

typedef struct hal_record_if {
    /** Write @p channel recording on/off state into @p *enabled_out (0/1). */
    nop_status_t (*get_recording_enabled)(void *ctx, int channel, int *enabled_out);
    /** Enable (@p enabled=1) or disable recording on @p channel. */
    nop_status_t (*set_recording_enabled)(void *ctx, int channel, int enabled);
    /** Read @p channel continuous-recording schedule into @p out. May be NULL. */
    nop_status_t (*get_continuous_schedule)(void *ctx, int channel, hal_record_schedule_t *out);
    /** Apply @p schedule to @p channel. May be NULL if schedules unsupported. */
    nop_status_t (*set_continuous_schedule)(void *ctx, int channel, const hal_record_schedule_t *schedule);
    void *ctx;
} hal_record_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_HAL_RECORD_H */
