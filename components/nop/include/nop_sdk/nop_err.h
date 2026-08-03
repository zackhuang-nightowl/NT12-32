/**
 * @file nop_err.h
 * @brief Unified SDK status codes (internal). These are mapped to the NOP
 *        business status codes (200/400/501) at the nop/ envelope boundary.
 */
#ifndef NOP_SDK_ERR_H
#define NOP_SDK_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal status codes returned across the SDK. Never put these on the wire
 * directly: nop/ maps them to NOP protocol statusCode (see nop_status_to_http).
 */
typedef enum nop_status {
    NOP_OK = 0,        /**< success (=> 200)                    */
    NOP_ERR_PARAM,     /**< bad/missing argument (=> 400)       */
    NOP_ERR_NOTIMPL,   /**< capability not available (=> 501)   */
    NOP_ERR_IO,        /**< transport / I/O failure             */
    NOP_ERR_NOMEM,     /**< out of memory                       */
    NOP_ERR_TIMEOUT,   /**< operation timed out                 */
    NOP_ERR_AUTH,      /**< authentication/authorization failed */
    NOP_ERR_NOTFOUND,  /**< command/resource not found (=> 501) */
    NOP_ERR_STATE,     /**< invalid state for the operation     */
    NOP_ERR_INTERNAL,  /**< unexpected internal error           */
    NOP_ERR_CONFLICT,     /**< conflicts with current state / busy (=> 409)  */
    NOP_ERR_STORAGE_FULL, /**< insufficient storage for the op    (=> 507)   */
    NOP_STATUS_MAX
} nop_status_t;

/** @return static human-readable name for a status code. */
const char *nop_strerror(nop_status_t s);

/**
 * Map an internal status to a NOP protocol statusCode (the business-layer set
 * used by the spec): NOP_OK->200, NOP_ERR_PARAM->400, NOP_ERR_CONFLICT->409,
 * NOP_ERR_NOTIMPL/NOTFOUND->501, NOP_ERR_STORAGE_FULL->507, else 400.
 */
int nop_status_to_http(nop_status_t s);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ERR_H */
