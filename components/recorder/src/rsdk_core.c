/* Copyright (C) 2025-2026, Nightowl DG. RSDK 版本/错误串. */
#include "rsdk.h"

const char *rsdk_version(void) { return RSDK_VERSION_STR; }

const char *rsdk_strerror(rsdk_err_t e)
{
    switch (e) {
    case RSDK_OK: return "ok";
    case RSDK_E_IO: return "io error";
    case RSDK_E_PARAM: return "bad param";
    case RSDK_E_NOTFOUND: return "not found";
    case RSDK_E_CORRUPT: return "corrupt";
    case RSDK_E_NOSPACE: return "no space";
    case RSDK_E_DB: return "db error";
    case RSDK_E_CRYPTO: return "crypto error";
    case RSDK_E_FORMAT: return "not formatted";
    case RSDK_E_BUSY: return "busy";
    case RSDK_E_SECTORSIZE: return "unsupported logical sector size (need 512)";
    default: return "unknown";
    }
}
