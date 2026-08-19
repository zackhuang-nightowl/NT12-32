#include "nop/nop_envelope.h"

#include <stdlib.h>

nop_status_t nop_envelope_parse(const char *json, size_t len, nop_request_t *out_request)
{
    nop_json_t *root, *func;

    if (!json || !out_request)
        return NOP_ERR_PARAM;

    out_request->root = NULL;
    out_request->func = NULL;
    out_request->args = NULL;

    root = nop_json_parse(json, len);
    if (!root)
        return NOP_ERR_PARAM;

    func = nop_json_get(root, "func");
    if (!func || !cJSON_IsString(func) || !func->valuestring || !func->valuestring[0]) {
        nop_json_free(root);
        return NOP_ERR_PARAM;
    }

    out_request->root = root;
    out_request->func = func->valuestring;
    out_request->args = nop_json_get(root, "args");   /* borrowed; may be NULL */
    return NOP_OK;
}

void nop_request_free(nop_request_t *request)
{
    if (!request)
        return;
    nop_json_free(request->root);
    request->root = NULL;
    request->func = NULL;
    request->args = NULL;
}

void nop_response_init(nop_response_t *response)
{
    if (!response)
        return;
    response->status = 200;
    response->msg = "OK";
    response->content = NULL;
}

void nop_response_free(nop_response_t *response)
{
    if (!response)
        return;
    nop_json_free(response->content);
    response->content = NULL;
}

const char *nop_status_msg(int status_code)
{
    switch (status_code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 409: return "Conflict";
    case 501: return "NOT_SUPPORT";
    case 507: return "Insufficient Storage";
    default:  return "Error";
    }
}

char *nop_envelope_build(nop_response_t *response)
{
    nop_json_t *envelope;
    char       *json_text;

    if (!response)
        return NULL;

    envelope = nop_json_obj();
    if (!envelope)
        return NULL;

    nop_json_add_int(envelope, "statusCode", (double)response->status);
    nop_json_add_str(envelope, "statusMsg",
                     response->msg ? response->msg : nop_status_msg(response->status));
    if (response->content) {
        nop_json_add(envelope, "content", response->content);  /* ownership -> envelope */
        response->content = NULL;
    }

    json_text = nop_json_print(envelope);
    nop_json_free(envelope);
    return json_text;
}

void nop_envelope_free_str(char *json_text)
{
    /* cJSON_PrintUnformatted uses the cJSON allocator (malloc by default). */
    free(json_text);
}
