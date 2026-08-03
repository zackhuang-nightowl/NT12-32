/**
 * @file nop_envelope.h  (internal)
 * @brief NOP envelope codec.
 *   request:  { "func": "...", "args": { ... } }
 *   response: { "statusCode": 200, "statusMsg": "OK", "content": { ... }? }
 * Business errors travel inside content.error — there is no global error code.
 */
#ifndef NOP_NOP_ENVELOPE_H
#define NOP_NOP_ENVELOPE_H

#include "base/nop_json.h"
#include "nop_sdk/nop_err.h"

/** Parsed request. @c root owns the tree; @c func/@c args borrow from it. */
typedef struct nop_request {
    nop_json_t *root;
    const char *func;
    nop_json_t *args;   /* may be NULL */
} nop_request_t;

/** Response a handler fills. @c content is owned here until built. */
typedef struct nop_response {
    int         status;    /* 200 / 400 / 501 */
    const char *msg;       /* static string    */
    nop_json_t *content;   /* may be NULL      */
} nop_response_t;

/** Parse a request envelope. On success caller must nop_request_free(). */
nop_status_t nop_envelope_parse(const char *json, size_t len, nop_request_t *out_request);
void         nop_request_free(nop_request_t *request);

/** Initialize a response to a 200/OK/no-content baseline. */
void nop_response_init(nop_response_t *response);
/** Free a response's owned content (safe to call on built/consumed response). */
void nop_response_free(nop_response_t *response);

/**
 * Serialize a response envelope to a heap JSON string (caller frees with
 * nop_envelope_free_str). CONSUMES response->content (sets it to NULL).
 */
char *nop_envelope_build(nop_response_t *response);
void  nop_envelope_free_str(char *json_text);

/** Convenience: standard status messages. */
const char *nop_status_msg(int status_code);

#endif /* NOP_NOP_ENVELOPE_H */
