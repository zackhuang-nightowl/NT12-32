/**
 * @file nop_config.h
 * @brief Device provisioning config — a JSON file that sets the device's
 *        identity (model, etc.), capability level, and the parameters other
 *        subsystems consume (OTA URL construction, TUTK P2P connection).
 *
 * This is static provisioning, distinct from the runtime HAL: the HAL reports
 * live hardware state (IP, MAC, channel status), while this file pins what the
 * product *is* and what it advertises. Load it at startup, apply the capability
 * level to the app, and hand the ota/tutk sections to those modules.
 *
 *   nop_device_config_t cfg;
 *   nop_device_config_init(&cfg);
 *   nop_device_config_load_file("/etc/nop/device.json", &cfg);
 *   nop_app_t *app = nop_app_create(&app_cfg);
 *   nop_device_config_apply_capabilities(app, &cfg);   // capability level
 *   // user's OTA code reads cfg.ota.*, TUTK code reads cfg.tutk.*
 */
#ifndef NOP_SDK_CONFIG_H
#define NOP_SDK_CONFIG_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_caps.h"
#include "nop_sdk/nop_detect_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** OTA provisioning — feeds firmware-upgrade URL construction. */
typedef struct nop_config_ota {
    char company[64];     /**< vendor segment of the OTA URL */
    char product[64];     /**< product line segment */
    char model[64];       /**< model segment (may differ from device model) */
    char url_base[256];   /**< OTA service base, e.g. https://kota.kalayservice.com/ota/GET/i */
    int  auto_update;     /**< 1 = apply automatically once downloaded */
} nop_config_ota_t;

/** TUTK P2P provisioning — feeds the TUTK connection (user's transport). */
typedef struct nop_config_tutk {
    int  enable;          /**< 1 = bring up the TUTK P2P backend */
    char uid[24];         /**< 20-char device UID */
    char auth_key[64];    /**< AV authentication key */
    char license_key[128];/**< TUTK SDK license / init key */
} nop_config_tutk_t;

/** Full device provisioning config. Zero-init via nop_device_config_init. */
typedef struct nop_device_config {
    /* --- identity (what the product is) --- */
    char manufacturer[64];
    char model[64];
    char firmware_version[64];
    char serial_number[32];
    char hardware_id[64];
    char type[32];               /**< standaloneIpCamera|videoRecorder|doorbell... */
    int  max_channel_count;

    /* --- service endpoint --- */
    int  http_port;              /**< inbound NOP-over-HTTP port (default 8089) */

    /* --- capability level: which capabilities the device advertises/serves --- */
    char capabilities[NOP_CAP_MAX][24];
    int  capability_count;

    /* --- supported detection types (drives AI handler defaults) --- */
    nop_detect_type_t detection_types[NOP_DETECT_TYPE_MAX];
    int               detection_type_count;

    /* --- subsystem provisioning --- */
    nop_config_ota_t  ota;
    nop_config_tutk_t tutk;

    void *reserved[4];
} nop_device_config_t;

/** Fill @p config with defaults (http_port 8089, type standaloneIpCamera, 1 ch). */
void nop_device_config_init(nop_device_config_t *config);

/** Parse a config from a JSON buffer. @return NOP_OK or NOP_ERR_PARAM. */
nop_status_t nop_device_config_load_json(const char *json_text, size_t length,
                                         nop_device_config_t *config);

/** Read and parse a config file. @return NOP_OK, NOP_ERR_IO, or NOP_ERR_PARAM. */
nop_status_t nop_device_config_load_file(const char *path, nop_device_config_t *config);

/**
 * Apply the configured capability level to @p app: each named capability is
 * enabled, and (when @p exclusive) every other non-core capability is disabled,
 * so the file is the authoritative source of what the device exposes. @return
 * NOP_OK, or NOP_ERR_PARAM.
 */
nop_status_t nop_device_config_apply_capabilities(nop_app_t *app,
                                                  const nop_device_config_t *config,
                                                  int exclusive);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_CONFIG_H */
