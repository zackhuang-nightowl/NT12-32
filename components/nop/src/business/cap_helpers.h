/**
 * @file cap_helpers.h  (internal, shared by capability handlers)
 * @brief Cross-handler helpers so per-domain cap_*.c files don't each reinvent
 *        (or hardcode) the same logic. Currently: the device's supported
 *        detection-type set, sourced from the provisioned device config and
 *        falling back to a built-in default — used by AI / push / cloud
 *        handlers to fill their default trigger/sensor lists.
 */
#ifndef NOP_BUSINESS_CAP_HELPERS_H
#define NOP_BUSINESS_CAP_HELPERS_H

#include "base/nop_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * For each detection type the device supports (from the provisioned device
 * config carried in @p handler_context, else a built-in default), call
 * @p emit(arr, wire_name). Lets a handler build domain-specific entries
 * (e.g. {sensor,threshold}) over the shared type set.
 */
void cap_for_each_detection_type(void *handler_context,
                                 void (*emit)(nop_json_t *arr, const char *type_name),
                                 nop_json_t *arr);

/** Push each supported detection type's wire name into @p arr as a string. */
void cap_emit_detection_type_names(void *handler_context, nop_json_t *arr);

#ifdef __cplusplus
}
#endif

#endif /* NOP_BUSINESS_CAP_HELPERS_H */
