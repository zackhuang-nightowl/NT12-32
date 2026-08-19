/**
 * @file nop_error_str.h
 * @brief Canonical NOP command-level error / result strings — the single home
 *        for the "error" and "result" field VALUES that commands put in their
 *        response content. These are distinct from the protocol statusCode
 *        (nop_err.h): per the API docs ("具体命令错误信息统一通过 error 字段返回"),
 *        a command returns statusCode 200 and conveys success/failure through a
 *        "result" or "error" string. Handlers must use these constants instead
 *        of scattering string literals.
 *
 * Sourced from nop_api/errorCode.memo + the per-command specs. The exact
 * spelling (incl. the doc's "NetWork Error") is preserved so it matches the
 * wire contract byte-for-byte.
 */
#ifndef NOP_SDK_ERROR_STR_H
#define NOP_SDK_ERROR_STR_H

/* ---- generic ------------------------------------------------------------- */
#define NOP_RESULT_OK                        "OK"     /* "result":"OK" */
#define NOP_ERRSTR_NONE                      ""       /* "error":"" (= success) */

/* ---- login / account (GUI_login, GUI_LanAddDevice, GUI_forgetPassword,
 *      GUI_setPassword, GUI_setDeviceDisplayMode, ...) "result" values ------- */
#define NOP_RESULT_FAIL_NETWORK              "Failed for network Problem"
#define NOP_RESULT_FAIL_CREDENTIALS          "Failed for username or password not valid"
#define NOP_RESULT_FAIL_LOCKED_10MIN         "Failed for locked 10min"
#define NOP_RESULT_FAIL_OTHER                "Failed for other reason"
#define NOP_RESULT_FAIL_ADD_TO_ACCOUNT       "Failed Add to Account"   /* append ":<accountId>" */
#define NOP_RESULT_FAIL_RESET_CODE           "Failed for not valid localResetCode"
#define NOP_RESULT_FAIL_USERNAME             "Failed for username not valid"
#define NOP_RESULT_FAIL_NO_FREE_CHANNEL      "Failed for no free channel"
#define NOP_RESULT_FAIL_TIMEOUT              "Failed for timeout"
#define NOP_RESULT_FAIL_REASON               "Fail Reason"             /* GUI_setPlaybackMode */
#define NOP_RESULT_ALREADY_BOUND             "AlreadyBound"            /* setRemoteAccessState */

/* ---- firmware check (GUI_checkServerFirmware / checkChannelServerFirmware)
 *      "error" values ------------------------------------------------------- */
#define NOP_ERRSTR_SERVER                    "Server Error"
#define NOP_ERRSTR_NETWORK                   "NetWork Error"           /* doc spelling */

/* ---- channel / device-specific "error" values --------------------------- */
#define NOP_ERRSTR_NO_FREE_CHANNEL           "NoFreeChannel"
#define NOP_ERRSTR_ALREADY_ACTIVE            "already_active_error"    /* X_NightOwl_setDeviceActive */
#define NOP_ERRSTR_RANDOM_EMPTY              "random_empty_error"      /* EnhancedSecurity */

#define NOP_ERRSTR_CAM_NOT_BINDED            "camera not binded"
#define NOP_ERRSTR_CAM_DISCONNECTED          "camera disconnected"

#endif /* NOP_SDK_ERROR_STR_H */
