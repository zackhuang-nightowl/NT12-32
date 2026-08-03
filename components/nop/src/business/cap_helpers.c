/**
 * @file cap_helpers.c
 * @brief Shared capability-handler helpers (see cap_helpers.h).
 */
#include "business/cap_helpers.h"
#include "business/business.h"
#include "nop_sdk/nop_config.h"
#include "nop_sdk/nop_detect_types.h"

#include <stddef.h>

/* Built-in default detection set, used only when the device is unprovisioned. */
static const nop_detect_type_t k_default_detection_types[] = {
    NOP_DETECT_HUMAN, NOP_DETECT_VEHICLE, NOP_DETECT_ANIMAL, NOP_DETECT_PACKAGE
};

void cap_for_each_detection_type(void *handler_context,
                                 void (*emit)(nop_json_t *, const char *),
                                 nop_json_t *arr)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    const nop_device_config_t    *config  = context ? context->device_config : NULL;
    if (!emit || !arr)
        return;
    if (config && config->detection_type_count > 0) {
        int i;
        for (i = 0; i < config->detection_type_count; i++)
            emit(arr, nop_detect_type_name(config->detection_types[i]));
    } else {
        size_t i;
        for (i = 0; i < sizeof(k_default_detection_types) / sizeof(k_default_detection_types[0]); i++)
            emit(arr, nop_detect_type_name(k_default_detection_types[i]));
    }
}

static void emit_name_as_string(nop_json_t *arr, const char *type_name)
{
    nop_json_arr_push_str(arr, type_name);
}

void cap_emit_detection_type_names(void *handler_context, nop_json_t *arr)
{
    cap_for_each_detection_type(handler_context, emit_name_as_string, arr);
}
