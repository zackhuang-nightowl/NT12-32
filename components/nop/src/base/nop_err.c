#include "nop_sdk/nop_err.h"

const char *nop_strerror(nop_status_t s)
{
    switch (s) {
    case NOP_OK:           return "OK";
    case NOP_ERR_PARAM:    return "bad parameter";
    case NOP_ERR_NOTIMPL:  return "NOT_SUPPORT";
    case NOP_ERR_IO:       return "I/O error";
    case NOP_ERR_NOMEM:    return "out of memory";
    case NOP_ERR_TIMEOUT:  return "timeout";
    case NOP_ERR_AUTH:     return "authentication failed";
    case NOP_ERR_NOTFOUND: return "not found";
    case NOP_ERR_STATE:        return "invalid state";
    case NOP_ERR_INTERNAL:     return "internal error";
    case NOP_ERR_CONFLICT:     return "conflict";
    case NOP_ERR_STORAGE_FULL: return "insufficient storage";
    default:                   return "unknown";
    }
}

int nop_status_to_http(nop_status_t s)
{
    switch (s) {
    case NOP_OK:               return 200;
    case NOP_ERR_PARAM:        return 400;
    case NOP_ERR_CONFLICT:     return 409;
    case NOP_ERR_NOTIMPL:      return 501;
    case NOP_ERR_NOTFOUND:     return 501;
    case NOP_ERR_STORAGE_FULL: return 507;
    default:                   return 400;
    }
}
