/**
 * @file channel_loopback.c
 * @brief Loopback NOP client channel — dispatches straight into a nop_app_t.
 */
#include "nop_sdk/nop_transport_loopback.h"

static int loopback_request(void *ctx, const char *request_json,
                            char **response_json, uint32_t timeout_ms)
{
    nop_app_t *app = (nop_app_t *)ctx;
    (void)timeout_ms;   /* in-process call: no timeout */
    if (!app || !request_json || !response_json)
        return -1;
    /* nop_app_dispatch heap-allocates the response with the same allocator the
     * client frees with (free()); hand the pointer straight out. */
    if (nop_app_dispatch(app, request_json, response_json) != NOP_OK)
        return -1;
    return (*response_json) ? 0 : -1;
}

void nop_channel_loopback_init(nop_client_channel_if *channel, nop_app_t *app)
{
    if (!channel)
        return;
    channel->request = loopback_request;
    channel->ctx     = app;
}
