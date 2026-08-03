#include "nop/nop_router.h"
#include "base/nop_map.h"
#include "base/nop_mem.h"
#include "nop_sdk/nop_log.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>

typedef struct route_entry {
    nop_cap_id_t   capability;
    nop_handler_fn handler;
} route_entry_t;

struct nop_router {
    nop_map_t      *routes_by_func;     /* func name -> route_entry_t* */
    cap_registry_t *capability_registry;
    void           *handler_context;
    osal_mutex_t   *mutex;
};

#define NOP_BATCH_FUNC "batchCmd"

nop_router_t *nop_router_create(cap_registry_t *capability_registry)
{
    nop_router_t *router = (nop_router_t *)nop_calloc(1, sizeof(*router));
    if (!router)
        return NULL;
    router->routes_by_func = nop_map_create(256);
    if (!router->routes_by_func) {
        nop_free(router);
        return NULL;
    }
    router->capability_registry = capability_registry;
    router->mutex = osal_mutex_create();
    return router;
}

static int free_route_entry_cb(const char *key, void *value, void *user)
{
    (void)key; (void)user;
    nop_free(value);
    return 0;
}

void nop_router_destroy(nop_router_t *router)
{
    if (!router)
        return;
    nop_map_foreach(router->routes_by_func, free_route_entry_cb, NULL);
    nop_map_destroy(router->routes_by_func);
    osal_mutex_destroy(router->mutex);
    nop_free(router);
}

void nop_router_set_handler_ctx(nop_router_t *router, void *handler_context)
{
    if (router)
        router->handler_context = handler_context;
}

nop_status_t nop_router_register(nop_router_t *router, const char *func,
                                 nop_cap_id_t capability, nop_handler_fn handler)
{
    route_entry_t *route;
    if (!router || !func || !handler)
        return NOP_ERR_PARAM;

    osal_mutex_lock(router->mutex);
    route = (route_entry_t *)nop_map_get(router->routes_by_func, func);
    if (!route) {
        route = (route_entry_t *)nop_malloc(sizeof(*route));
        if (!route) {
            osal_mutex_unlock(router->mutex);
            return NOP_ERR_NOMEM;
        }
        if (nop_map_put(router->routes_by_func, func, route) != 0) {
            nop_free(route);
            osal_mutex_unlock(router->mutex);
            return NOP_ERR_NOMEM;
        }
    }
    route->capability = capability;
    route->handler    = handler;
    osal_mutex_unlock(router->mutex);
    return NOP_OK;
}

size_t nop_router_count(const nop_router_t *router)
{
    return router ? nop_map_size(router->routes_by_func) : 0;
}

/* Run a single command (func + borrowed args) and fill response. Never frees
 * args. Always leaves response in a valid state (status + optional content). */
static void dispatch_one(nop_router_t *router, const char *func,
                         nop_json_t *args, nop_response_t *response)
{
    route_entry_t *route;
    nop_request_t  request;
    nop_status_t   handler_status;

    nop_response_init(response);

    osal_mutex_lock(router->mutex);
    route = (route_entry_t *)nop_map_get(router->routes_by_func, func);
    osal_mutex_unlock(router->mutex);

    if (!route) {
        response->status = 501;
        response->msg = nop_status_msg(501);
        NOP_LOGD("dispatch: unknown func '%s' -> 501", func);
        return;
    }

    if (!cap_registry_is_enabled(router->capability_registry, route->capability)) {
        response->status = 501;
        response->msg = nop_status_msg(501);
        NOP_LOGD("dispatch: '%s' gated (capability %s off) -> 501",
                 func, nop_cap_name(route->capability));
        return;
    }

    /* Borrow a request view for the handler (no root ownership). */
    request.root = NULL;
    request.func = func;
    request.args = args;

    handler_status = route->handler(&request, response, router->handler_context);
    if (handler_status != NOP_OK) {
        response->status = nop_status_to_http(handler_status);
        response->msg = nop_status_msg(response->status);
        nop_response_free(response);   /* discard any partial content */
    }
}

/* batchCmd: args.cmds = [ {func, args}, ... ] -> content.results[]. */
static void dispatch_batch(nop_router_t *router, nop_json_t *args,
                           nop_response_t *response)
{
    nop_json_t *commands, *results;
    int         command_count, command_index;

    nop_response_init(response);
    commands = args ? nop_json_get(args, "cmds") : NULL;
    if (!commands || !nop_json_is_arr(commands)) {
        response->status = 400;
        response->msg = nop_status_msg(400);
        return;
    }

    results = nop_json_arr();
    command_count = cJSON_GetArraySize(commands);
    for (command_index = 0; command_index < command_count; command_index++) {
        nop_json_t    *command, *sub_func, *sub_args, *result_entry;
        nop_response_t sub_response;

        command  = cJSON_GetArrayItem(commands, command_index);
        sub_func = nop_json_get(command, "func");
        sub_args = nop_json_get(command, "args");

        result_entry = nop_json_obj();
        if (!sub_func || !cJSON_IsString(sub_func) || !sub_func->valuestring) {
            nop_json_add_str(result_entry, "func", "");
            nop_json_add_int(result_entry, "statusCode", 400);
            nop_json_arr_push(results, result_entry);
            continue;
        }
        if (strcmp(sub_func->valuestring, NOP_BATCH_FUNC) == 0) {
            /* batches do not nest */
            nop_json_add_str(result_entry, "func", sub_func->valuestring);
            nop_json_add_int(result_entry, "statusCode", 400);
            nop_json_arr_push(results, result_entry);
            continue;
        }

        dispatch_one(router, sub_func->valuestring, sub_args, &sub_response);
        nop_json_add_str(result_entry, "func", sub_func->valuestring);
        nop_json_add_int(result_entry, "statusCode", (double)sub_response.status);
        if (sub_response.content) {
            nop_json_add(result_entry, "content", sub_response.content);
            sub_response.content = NULL;
        }
        nop_json_arr_push(results, result_entry);
        nop_response_free(&sub_response);
    }

    response->content = nop_json_obj();
    nop_json_add(response->content, "results", results);
}

nop_status_t nop_router_dispatch(nop_router_t *router, const char *json_in,
                                 size_t in_len, char **json_out)
{
    nop_request_t  request;
    nop_response_t response;
    nop_status_t   parse_status;

    if (!router || !json_in || !json_out)
        return NOP_ERR_PARAM;
    *json_out = NULL;

    parse_status = nop_envelope_parse(json_in, in_len, &request);
    if (parse_status != NOP_OK) {
        nop_response_init(&response);
        response.status = 400;
        response.msg = nop_status_msg(400);
        *json_out = nop_envelope_build(&response);
        nop_response_free(&response);
        return NOP_OK;   /* a valid 400 envelope was produced */
    }

    if (strcmp(request.func, NOP_BATCH_FUNC) == 0)
        dispatch_batch(router, request.args, &response);
    else
        dispatch_one(router, request.func, request.args, &response);

    *json_out = nop_envelope_build(&response);
    nop_response_free(&response);
    nop_request_free(&request);
    return *json_out ? NOP_OK : NOP_ERR_NOMEM;
}

void nop_router_free_response(char *json_out)
{
    nop_envelope_free_str(json_out);
}
