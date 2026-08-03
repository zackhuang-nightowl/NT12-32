/**
 * @file nop_client.h
 * @brief NOP client — the request-issuing side of the protocol.
 *
 * The NVR backend is the NOP *server* (nop_app_dispatch answers ~213 commands).
 * A separate process — e.g. the on-device LVGL UI — drives it as a NOP *client*:
 * it builds a request envelope, sends it over a channel, and parses the response.
 *
 * Like the server, the client does no networking itself: it is handed a
 * nop_client_channel_if and is agnostic to whether that channel is an in-process
 * loopback to a nop_app_t (see nop_transport_loopback.h) or a Unix-domain socket
 * to another process (see nop_transport_unix.h).
 *
 * Everything crosses the ABI as JSON strings (cJSON stays internal), exactly
 * mirroring nop_app_dispatch's char* in / char* out.
 *
 * Typical embed (LVGL UI process):
 *   nop_client_channel_if channel;
 *   nop_channel_unix_connect(&channel, "/run/nop.sock");
 *   nop_client_t *client = nop_client_create(&channel);
 *   int status; char *content = NULL;
 *   nop_client_call_status(client, "getDeviceInfo", NULL, &status, &content);
 *   // ... use content (JSON string) ...
 *   nop_client_free_response(content);
 *   nop_client_destroy(client);
 *   nop_channel_unix_close(&channel);
 */
#ifndef NOP_SDK_CLIENT_H
#define NOP_SDK_CLIENT_H

#include "nop_sdk/nop_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Message-level request/response channel the client sends through. A call
 * hands @c request_json (a full request envelope) and must yield the response
 * envelope in @c *response_json, heap-allocated and freed by the SDK. Returns 0
 * on success, non-zero on transport failure. @c ctx is opaque backend state.
 */
typedef struct nop_client_channel_if {
    int (*request)(void *ctx, const char *request_json,
                   char **response_json, uint32_t timeout_ms);
    void *ctx;
} nop_client_channel_if;

/** Opaque client handle. */
typedef struct nop_client nop_client_t;

/**
 * Create a client over @p channel (stored by pointer; must outlive the client).
 * Returns NULL on allocation failure.
 */
nop_client_t *nop_client_create(const nop_client_channel_if *channel);

/** Destroy a client. NULL-safe. (Does not close the underlying channel.) */
void nop_client_destroy(nop_client_t *client);

/**
 * Issue @p func with @p args_json (a JSON object string, or NULL for "{}").
 * Builds {"func":...,"args":...}, sends it over the channel, and returns the
 * raw response envelope in @p response_json (heap; free with
 * nop_client_free_response). @return NOP_OK on a completed round-trip (even if
 * the server replied with an error status — inspect the envelope), or an error
 * if the request could not be built/sent.
 */
nop_status_t nop_client_call(nop_client_t *client, const char *func,
                             const char *args_json, char **response_json);

/**
 * Like nop_client_call but also parses the envelope: @p status_code_out gets the
 * NOP statusCode (e.g. 200/400/501) and @p content_json_out gets the "content"
 * object as a heap JSON string ("{}" if absent). Either out-pointer may be NULL.
 * @p content_json_out (when non-NULL and set) is freed with
 * nop_client_free_response.
 */
nop_status_t nop_client_call_status(nop_client_t *client, const char *func,
                                    const char *args_json, int *status_code_out,
                                    char **content_json_out);

/** Release a string returned by the client. NULL-safe. */
void nop_client_free_response(char *json);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_CLIENT_H */
