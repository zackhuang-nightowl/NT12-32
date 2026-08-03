/**
 * @file nop_server_request.h
 * @brief Outbound "request-to-server" services — everything where the device
 *        initiates an HTTP request to a cloud/vendor server (OTA, and later
 *        push registration, cloud upload, telemetry). All such logic lives in
 *        one place: src/services/server_request.c.
 *
 * First service: OTA. The device builds an OTA URL from its provisioning
 * config (nop_config_ota_t) and GETs the firmware/descriptor, tracking a
 * status that mirrors checkFirmwareUpgradeStatus.
 *
 * Networking is a pluggable fetch: the built-in nop_server_http_get handles
 * plain http:// over raw sockets (enough for testing and intranet OTA); supply
 * an HTTPS-capable fetch via nop_ota_set_fetch for production https:// URLs so
 * the core stays free of a TLS dependency.
 */
#ifndef NOP_SDK_SERVER_REQUEST_H
#define NOP_SDK_SERVER_REQUEST_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_config.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* Shared outbound HTTP primitive                                           */
/* ======================================================================== */

/** Result of an outbound HTTP GET. Free body with nop_server_http_response_free. */
typedef struct nop_http_response {
    int    status_code;   /**< HTTP status (e.g. 200), or -1 on transport error */
    char  *body;          /**< heap, NUL-terminated response body (may be NULL) */
    size_t body_length;
} nop_http_response_t;

/**
 * Pluggable fetch function: GET @p url into @p out. @return 0 on a completed
 * HTTP exchange (inspect out->status_code), non-zero on transport failure.
 * @p ctx is opaque caller state.
 */
typedef int (*nop_http_fetch_fn)(const char *url, nop_http_response_t *out, void *ctx);

/**
 * Built-in plain-HTTP GET over raw TCP sockets. Handles "http://host[:port]/path".
 * Returns non-zero (and status_code -1) for https:// or any transport error.
 */
int  nop_server_http_get(const char *url, nop_http_response_t *out);

/** Release a response body. NULL-safe. */
void nop_server_http_response_free(nop_http_response_t *response);

/* ======================================================================== */
/* OTA                                                                      */
/* ======================================================================== */

/** OTA progress — values match the checkFirmwareUpgradeStatus "status" field. */
typedef enum nop_ota_status {
    NOP_OTA_IDLE        = 0,
    NOP_OTA_DOWNLOADING = 1,
    NOP_OTA_FAILED      = 2,
    NOP_OTA_DOWNLOADED  = 3,
    NOP_OTA_NONE        = 4
} nop_ota_status_t;

/** Opaque OTA session. */
typedef struct nop_ota nop_ota_t;

/**
 * Sink for downloaded firmware bytes (write to flash / staging file). Called
 * one or more times as the image arrives; @p offset is the byte offset. May be
 * NULL (bytes are then only validated, not stored).
 */
typedef void (*nop_ota_write_fn)(void *ctx, const unsigned char *data,
                                 size_t length, long long offset);

/**
 * Build the OTA URL from @p ota config into @p out:
 * "<url_base>/<company>/<product>/<model>" (empty segments skipped).
 * @return NOP_OK, or NOP_ERR_PARAM (bad args / buffer too small).
 */
nop_status_t nop_ota_build_url(const nop_config_ota_t *ota, char *out, size_t capacity);

/** Create an OTA session from @p ota config (copied). NULL on failure. */
nop_ota_t *nop_ota_create(const nop_config_ota_t *ota);

/** Destroy a session (joins any in-flight download). NULL-safe. */
void nop_ota_destroy(nop_ota_t *ota);

/** Override the HTTP fetch (e.g. an HTTPS implementation). Default = plain GET. */
void nop_ota_set_fetch(nop_ota_t *ota, nop_http_fetch_fn fetch, void *ctx);

/** Set the firmware byte sink. */
void nop_ota_set_writer(nop_ota_t *ota, nop_ota_write_fn writer, void *ctx);

/**
 * Begin an upgrade on a background thread: fetch @p url (NULL → build from
 * config), stream the body to the writer, and advance status
 * IDLE→DOWNLOADING→DOWNLOADED/FAILED/NONE. @p automatic is recorded for the
 * caller (apply-on-download policy is the integrator's). @return NOP_OK if the
 * download started, NOP_ERR_STATE if one is already running, else an error.
 */
nop_status_t nop_ota_start(nop_ota_t *ota, const char *url, int automatic);

/** @return current status; @p error_out (optional) gets a code when FAILED. */
nop_ota_status_t nop_ota_status_get(nop_ota_t *ota, int *error_out);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_SERVER_REQUEST_H */
