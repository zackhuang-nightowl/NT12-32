/**
 * @file cap_auth.c
 * @brief CAP_AUTH handlers: createCredential / deleteCredential (Authentication
 *        tag in openapi.json). N0 validates args and returns a session token
 *        skeleton; real credential storage is wired with the security module.
 */
#include "business/business.h"
#include "base/nop_json.h"

static nop_status_t handle_create_credential(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char *username = nop_json_str(request->args, "username", NULL);
    const char *password = nop_json_str(request->args, "password", NULL);
    (void)handler_context;

    if (!username || !password)
        return NOP_ERR_PARAM;

    /* TODO: validate against the credential store (security module). */
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "credential", "nop-session-token");
    return NOP_OK;
}

static nop_status_t handle_delete_credential(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char *credential = nop_json_str(request->args, "credential", NULL);
    (void)response; (void)handler_context;

    if (!credential)
        return NOP_ERR_PARAM;
    /* TODO: invalidate the session token. */
    return NOP_OK;   /* 200, no content body */
}

void cap_auth_register(nop_router_t *router)
{
    nop_router_register(router, "createCredential", CAP_AUTH, handle_create_credential);
    nop_router_register(router, "deleteCredential", CAP_AUTH, handle_delete_credential);
}
