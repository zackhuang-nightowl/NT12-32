/**
 * @file cap_record.c
 * @brief CAP_RECORD handlers: X_NightOwl_get/setChannelRecordingSwitch and
 *        X_NightOwl_get/setChannelContinuousRecordingSchedule. Gated by
 *        CAP_RECORD, backed by HAL_RECORD.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_record.h"

#include <string.h>
#include <stdio.h>

static nop_status_t handle_get_recording_switch(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    const hal_record_if *record = (const hal_record_if *)hal_registry_get(HAL_RECORD);
    int channel, enabled = 0;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (!record || !record->get_recording_enabled)
        return NOP_ERR_NOTIMPL;
    if (record->get_recording_enabled(record->ctx, channel, &enabled) != NOP_OK)
        return NOP_ERR_INTERNAL;

    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "value", enabled ? true : false);
    return NOP_OK;
}

static nop_status_t handle_set_recording_switch(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    const hal_record_if *record = (const hal_record_if *)hal_registry_get(HAL_RECORD);
    int channel, enabled;
    (void)response; (void)handler_context;

    if (!nop_json_has(request->args, "channel") || !nop_json_has(request->args, "value"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    enabled = nop_json_bool(request->args, "value", false) ? 1 : 0;

    if (!record || !record->set_recording_enabled)
        return NOP_ERR_NOTIMPL;
    return record->set_recording_enabled(record->ctx, channel, enabled);
}

static nop_status_t handle_get_continuous_schedule(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    const hal_record_if  *record = (const hal_record_if *)hal_registry_get(HAL_RECORD);
    hal_record_schedule_t schedule;
    nop_json_t           *rules;
    int                   channel, i, weekday;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (!record || !record->get_continuous_schedule)
        return NOP_ERR_NOTIMPL;
    memset(&schedule, 0, sizeof(schedule));
    if (record->get_continuous_schedule(record->ctx, channel, &schedule) != NOP_OK)
        return NOP_ERR_INTERNAL;

    response->content = nop_json_obj();
    rules = nop_json_arr();
    for (i = 0; i < schedule.rule_count && i < HAL_RECORD_MAX_RULES; i++) {
        const hal_record_rule_t *rule = &schedule.rules[i];
        nop_json_t              *entry = nop_json_obj();
        nop_json_t              *weekdays = nop_json_arr();
        nop_json_add_str(entry, "id", rule->id);
        /* weekday_mask bit0=Mon..bit6=Sun → JSON weekdays array of 1..7 */
        for (weekday = 0; weekday < 7; weekday++)
            if (rule->weekday_mask & (1u << weekday))
                nop_json_arr_push_int(weekdays, weekday + 1);
        nop_json_add(entry, "weekdays", weekdays);
        nop_json_add_str(entry, "startTime", rule->start_time);
        nop_json_add_str(entry, "endTime", rule->end_time);
        nop_json_arr_push(rules, entry);
    }
    nop_json_add(response->content, "rules", rules);
    return NOP_OK;
}

void cap_record_register(nop_router_t *router)
{
    nop_router_register(router, "X_NightOwl_getChannelRecordingSwitch", CAP_RECORD,
                        handle_get_recording_switch);
    nop_router_register(router, "X_NightOwl_setChannelRecordingSwitch", CAP_RECORD,
                        handle_set_recording_switch);
    nop_router_register(router, "X_NightOwl_getChannelContinuousRecordingSchedule", CAP_RECORD,
                        handle_get_continuous_schedule);
}
