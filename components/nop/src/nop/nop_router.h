/**
 * @file nop_router.h  (internal)
 * @brief Command dispatcher: {func -> (cap, handler)} exact-match registry with
 *        capability gating, batchCmd recursion, and 501 fallback.
 */
#ifndef NOP_NOP_ROUTER_H
#define NOP_NOP_ROUTER_H

#include "nop/nop_envelope.h"
#include "capability/cap_registry.h"
#include "nop_sdk/nop_caps.h"
#include "nop_sdk/nop_err.h"

typedef struct nop_router nop_router_t;

/**
 * Business handler: validate req->args, do the work (via HAL/orchestration),
 * fill resp->content with the return fields. Return NOP_OK on success (even for
 * business errors, which go in content.error), NOP_ERR_PARAM for 400, etc.
 */
typedef nop_status_t (*nop_handler_fn)(const nop_request_t *req,
                                       nop_response_t *resp,
                                       void *ctx);

nop_router_t *nop_router_create(cap_registry_t *capability_registry);
void          nop_router_destroy(nop_router_t *router);

/** Handlers receive this context as their last argument. */
void nop_router_set_handler_ctx(nop_router_t *router, void *handler_context);

/** Register an exact command name -> capability + handler. */
nop_status_t nop_router_register(nop_router_t *router, const char *func,
                                 nop_cap_id_t capability, nop_handler_fn handler);

/** @return number of registered commands. */
size_t nop_router_count(const nop_router_t *router);

/**
 * Dispatch one request envelope (JSON in) to a response envelope (JSON out).
 * @p json_out is heap-allocated; release with nop_router_free_response().
 * Always produces a valid envelope (400 on parse error, 501 when gated).
 */
nop_status_t nop_router_dispatch(nop_router_t *router, const char *json_in,
                                 size_t in_len, char **json_out);

void nop_router_free_response(char *json_out);

#endif /* NOP_NOP_ROUTER_H */
