/**
 * @file cap_storage.c
 * @brief CAP_STORAGE handlers: getCurrentStorage / X_NightOwl_getStorageInfo /
 *        getAllDisksHealth / formatStorage. Gated by CAP_STORAGE, backed by
 *        HAL_STORAGE.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_storage.h"

#include <string.h>

static const char *storage_status_name(hal_storage_status_t status)
{
    switch (status) {
    case HAL_STORAGE_STATUS_READY:       return "ready";
    case HAL_STORAGE_STATUS_FORMATTING:  return "formatting";
    case HAL_STORAGE_STATUS_UNFORMATTED: return "unformatted";
    case HAL_STORAGE_STATUS_ERROR:       return "error";
    case HAL_STORAGE_STATUS_ABSENT:      return "absent";
    default:                             return "unknown";
    }
}

static nop_status_t handle_get_current_storage(const nop_request_t *request,
                                               nop_response_t *response,
                                               void *handler_context)
{
    const hal_storage_if *storage = (const hal_storage_if *)hal_registry_get(HAL_STORAGE);
    char name[16];
    (void)request; (void)handler_context;

    if (!storage)
        return NOP_ERR_NOTIMPL;

    name[0] = '\0';
    if (storage->get_current_volume_name) {
        if (storage->get_current_volume_name(storage->ctx, name, (int)sizeof(name)) != NOP_OK)
            name[0] = '\0';
    }
    /* Fall back to the first volume when the HAL has no explicit "current". */
    if (!name[0] && storage->volume_count && storage->get_volume_info &&
        storage->volume_count(storage->ctx) > 0) {
        hal_storage_volume_t volume;
        if (storage->get_volume_info(storage->ctx, 0, &volume) == NOP_OK) {
            strncpy(name, volume.name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }
    }

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "value", name);
    return NOP_OK;
}

static nop_status_t handle_get_storage_info(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const hal_storage_if *storage = (const hal_storage_if *)hal_registry_get(HAL_STORAGE);
    nop_json_t           *list;
    int                   count, i;
    (void)request; (void)handler_context;

    if (!storage || !storage->volume_count || !storage->get_volume_info)
        return NOP_ERR_NOTIMPL;

    count = storage->volume_count(storage->ctx);
    response->content = nop_json_obj();
    list = nop_json_arr();
    for (i = 0; i < count; i++) {
        hal_storage_volume_t volume;
        nop_json_t          *entry;
        if (storage->get_volume_info(storage->ctx, i, &volume) != NOP_OK)
            continue;
        entry = nop_json_obj();
        nop_json_add_str(entry, "name", volume.name);
        nop_json_add_int(entry, "totalSize", (double)volume.total_size_kilobytes);
        nop_json_add_int(entry, "freeSize", (double)volume.free_size_kilobytes);
        nop_json_add_str(entry, "status", storage_status_name(volume.status));
        nop_json_arr_push(list, entry);
    }
    nop_json_add(response->content, "list", list);
    return NOP_OK;
}

static nop_status_t handle_get_all_disks_health(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    const hal_storage_if *storage = (const hal_storage_if *)hal_registry_get(HAL_STORAGE);
    nop_json_t           *list;
    int                   count, i;
    (void)request; (void)handler_context;

    if (!storage || !storage->volume_count || !storage->get_volume_info)
        return NOP_ERR_NOTIMPL;

    count = storage->volume_count(storage->ctx);
    response->content = nop_json_obj();
    list = nop_json_arr();
    for (i = 0; i < count; i++) {
        hal_storage_volume_t volume;
        nop_json_t          *entry;
        const char          *health;
        if (storage->get_volume_info(storage->ctx, i, &volume) != NOP_OK)
            continue;
        health = (volume.status == HAL_STORAGE_STATUS_ERROR) ? "bad" : "good";
        entry = nop_json_obj();
        nop_json_add_str(entry, "name", volume.name);
        nop_json_add_str(entry, "status", health);
        nop_json_arr_push(list, entry);
    }
    nop_json_add(response->content, "list", list);
    return NOP_OK;
}

static nop_status_t handle_format_storage(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const hal_storage_if *storage = (const hal_storage_if *)hal_registry_get(HAL_STORAGE);
    const char           *volume_name;
    (void)response; (void)handler_context;

    volume_name = nop_json_str(request->args, "value", NULL);
    if (!volume_name)
        return NOP_ERR_PARAM;
    if (!storage || !storage->format)
        return NOP_ERR_NOTIMPL;
    return storage->format(storage->ctx, volume_name);
}

void cap_storage_register(nop_router_t *router)
{
    nop_router_register(router, "getCurrentStorage", CAP_STORAGE, handle_get_current_storage);
    nop_router_register(router, "X_NightOwl_getStorageInfo", CAP_STORAGE, handle_get_storage_info);
    nop_router_register(router, "getAllDisksHealth", CAP_STORAGE, handle_get_all_disks_health);
    nop_router_register(router, "formatStorage", CAP_STORAGE, handle_format_storage);
}
