/**
 * @file nop_wizard.h
 * @brief videoRecorder startup (out-of-box) setup wizard state machine. On a
 *        factory-state boot the device runs the wizard to create the Admin
 *        password, name the device, configure channel imaging, and (when
 *        online) check firmware, then marks setup complete.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR). This service owns the wizard STATE and
 * the Admin-password policy; the GUI/APP drives it step by step and the
 * firmware persists the completion flag + credentials (non-volatile) so the
 * wizard does not re-run after reboot. Per the spec: Admin has no ownerId, the
 * password is 6–20 chars with no special characters, and completion leaves the
 * device unlocked with P2P default password / no BLE key.
 */
#ifndef NOP_SDK_WIZARD_H
#define NOP_SDK_WIZARD_H

#include "nop_sdk/nop_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Overall wizard state. */
typedef enum nop_wizard_state {
    NOP_WIZARD_PENDING = 0,   /**< factory state — wizard must run */
    NOP_WIZARD_IN_PROGRESS,   /**< at least one step done, not complete */
    NOP_WIZARD_COMPLETE       /**< setup finished (persist this) */
} nop_wizard_state_t;

/** Ordered setup steps. */
typedef enum nop_wizard_step {
    NOP_WIZARD_STEP_PASSWORD = 0,  /**< create Admin password */
    NOP_WIZARD_STEP_DEVICE_NAME,   /**< name the device */
    NOP_WIZARD_STEP_CHANNEL_IMAGING, /**< bring channels to image */
    NOP_WIZARD_STEP_FIRMWARE,      /**< (online) firmware check/upgrade */
    NOP_WIZARD_STEP_DONE           /**< all steps satisfied */
} nop_wizard_step_t;

/** Password-policy check result. */
typedef enum nop_wizard_pw_result {
    NOP_WIZARD_PW_OK = 0,
    NOP_WIZARD_PW_TOO_SHORT,       /**< < 6 chars */
    NOP_WIZARD_PW_TOO_LONG,        /**< > 20 chars */
    NOP_WIZARD_PW_BAD_CHAR         /**< contains a special (non-alphanumeric) char */
} nop_wizard_pw_result_t;

/** Status snapshot. */
typedef struct nop_wizard_status {
    nop_wizard_state_t state;
    nop_wizard_step_t  step;          /**< next step to complete */
    int                has_password;
    int                has_device_name;
    int                channel_imaging_done;
    int                firmware_checked;
    char               device_name[48];
} nop_wizard_status_t;

/** Opaque wizard. */
typedef struct nop_wizard nop_wizard_t;

/**
 * Create the wizard. @p already_complete non-zero starts it in COMPLETE (the
 * firmware read a persisted "setup done" flag, so the wizard is skipped);
 * otherwise it starts PENDING (factory state).
 */
nop_wizard_t *nop_wizard_create(int already_complete);

/** Destroy. NULL-safe. */
void nop_wizard_destroy(nop_wizard_t *wizard);

/** Validate a candidate Admin password against the policy (6–20, alphanumeric),
 *  without storing it. */
nop_wizard_pw_result_t nop_wizard_check_password(const char *password);

/**
 * Set the Admin password (validated). On NOP_OK the wizard records that the
 * password step is done and advances. @return NOP_OK, or NOP_ERR_PARAM if the
 * password violates the policy (see nop_wizard_check_password for the reason).
 */
nop_status_t nop_wizard_set_password(nop_wizard_t *wizard, const char *password);

/** Set the device name (1..47 chars). @return NOP_OK or NOP_ERR_PARAM. */
nop_status_t nop_wizard_set_device_name(nop_wizard_t *wizard, const char *name);

/** Mark the channel-imaging step satisfied (channels brought up to image). */
void nop_wizard_mark_channel_imaging(nop_wizard_t *wizard);

/** Mark the firmware-check step satisfied (online check ran / skipped offline). */
void nop_wizard_mark_firmware_checked(nop_wizard_t *wizard);

/**
 * Finalize the wizard. Requires the password to have been set. @return NOP_OK
 * (state becomes COMPLETE), or NOP_ERR_STATE if the password step is not done.
 */
nop_status_t nop_wizard_complete(nop_wizard_t *wizard);

/** Reset to factory (PENDING), clearing all step progress. */
void nop_wizard_reset(nop_wizard_t *wizard);

/** @return non-zero if setup is complete. */
int nop_wizard_is_complete(const nop_wizard_t *wizard);

/** Fill @p out with the current status. */
void nop_wizard_status(nop_wizard_t *wizard, nop_wizard_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_WIZARD_H */
