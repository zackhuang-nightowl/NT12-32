/**
 * @file nop_client.c
 * @brief NOP client core: build a request envelope, send it over the injected
 *        channel, hand back / parse the response. Channel-agnostic (loopback,
 *        Unix socket, …). All strings cross the ABI as JSON text; cJSON stays
 *        behind the nop_json facade.
 */
#include "nop_sdk/nop_client.h"
#include "base/nop_json.h"
#include "base/nop_mem.h"

#include <string.h>
#include <stdlib.h>

#define NOP_CLIENT_DEFAULT_TIMEOUT_MS 5000u

struct nop_client {
    nop_client_channel_if channel;   /* copied by value at create */
};

nop_client_t *nop_client_create(const nop_client_channel_if *channel)
{
    nop_client_t *client;
    if (!channel || !channel->request)
        return NULL;
    client = (nop_client_t *)nop_calloc(1, sizeof(*client));
    if (!client)
        return NULL;
    client->channel = *channel;
    return client;
}

void nop_client_destroy(nop_client_t *client)
{
    if (client)
        nop_free(client);
}

nop_status_t nop_client_call(nop_client_t *client, const char *func,
                             const char *args_json, char **response_json)
{
    nop_json_t *envelope, *args;
    char       *request_text;
    int         transport_rc;

    if (!client || !func || !response_json)
        return NOP_ERR_PARAM;
    *response_json = NULL;

    /* Build {"func":func,"args":<args or {}>}. */
    envelope = nop_json_obj();
    if (!envelope)
        return NOP_ERR_NOMEM;
    nop_json_add_str(envelope, "func", func);
    if (args_json && args_json[0]) {
        args = nop_json_parse(args_json, strlen(args_json));
        if (!args)
            args = nop_json_obj();   /* tolerate malformed args as empty */
    } else {
        args = nop_json_obj();
    }
    nop_json_add(envelope, "args", args);

    request_text = nop_json_print(envelope);
    nop_json_free(envelope);
    if (!request_text)
        return NOP_ERR_NOMEM;

    transport_rc = client->channel.request(client->channel.ctx, request_text,
                                            response_json,
                                            NOP_CLIENT_DEFAULT_TIMEOUT_MS);
    free(request_text);

    if (transport_rc != 0) {
        if (*response_json) {
            free(*response_json);
            *response_json = NULL;
        }
        return NOP_ERR_IO;
    }
    if (!*response_json)
        return NOP_ERR_IO;
    return NOP_OK;
}

nop_status_t nop_client_call_status(nop_client_t *client, const char *func,
                                    const char *args_json, int *status_code_out,
                                    char **content_json_out)
{
    char         *response_text = NULL;
    nop_json_t   *envelope, *content;
    nop_status_t  status;

    if (status_code_out)
        *status_code_out = 0;
    if (content_json_out)
        *content_json_out = NULL;

    status = nop_client_call(client, func, args_json, &response_text);
    if (status != NOP_OK)
        return status;

    envelope = nop_json_parse(response_text, strlen(response_text));
    free(response_text);
    if (!envelope)
        return NOP_ERR_INTERNAL;

    if (status_code_out)
        *status_code_out = (int)nop_json_num(envelope, "statusCode", 0);
    if (content_json_out) {
        content = nop_json_get(envelope, "content");
        if (content) {
            *content_json_out = nop_json_print(content);
        } else {
            nop_json_t *empty = nop_json_obj();
            *content_json_out = nop_json_print(empty);
            nop_json_free(empty);
        }
    }
    nop_json_free(envelope);
    return NOP_OK;
}

void nop_client_free_response(char *json)
{
    /* All client-returned strings come from nop_json_print (cJSON allocator =
     * malloc) or a channel buffer allocated the same way → plain free(). */
    if (json)
        free(json);
}
