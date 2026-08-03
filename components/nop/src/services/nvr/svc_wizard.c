/**
 * @file svc_wizard.c
 * @brief Startup setup-wizard state machine + Admin-password policy.
 *        See nop_sdk/nop_wizard.h.
 *
 * The wizard does not retain the plaintext password — it validates it and
 * records only that the password step is done; the caller (which already holds
 * the password) persists/hashes it via the credential layer.
 */
#include "nop_sdk/nop_wizard.h"

#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <stdio.h>
#include <string.h>

#define NOP_WIZARD_PW_MIN 6
#define NOP_WIZARD_PW_MAX 20

struct nop_wizard {
    osal_mutex_t      *mutex;
    nop_wizard_state_t state;
    int                has_password;
    int                has_device_name;
    int                channel_imaging_done;
    int                firmware_checked;
    char               device_name[48];
};

static int is_alphanumeric(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

nop_wizard_pw_result_t nop_wizard_check_password(const char *password)
{
    size_t len, i;
    if (!password)
        return NOP_WIZARD_PW_TOO_SHORT;
    len = strlen(password);
    if (len < NOP_WIZARD_PW_MIN)
        return NOP_WIZARD_PW_TOO_SHORT;
    if (len > NOP_WIZARD_PW_MAX)
        return NOP_WIZARD_PW_TOO_LONG;
    for (i = 0; i < len; i++)
        if (!is_alphanumeric(password[i]))
            return NOP_WIZARD_PW_BAD_CHAR;   /* no special characters allowed */
    return NOP_WIZARD_PW_OK;
}

nop_wizard_t *nop_wizard_create(int already_complete)
{
    nop_wizard_t *wizard = (nop_wizard_t *)nop_calloc(1, sizeof(*wizard));
    if (!wizard)
        return NULL;
    wizard->mutex = osal_mutex_create();
    if (!wizard->mutex) {
        nop_free(wizard);
        return NULL;
    }
    wizard->state = already_complete ? NOP_WIZARD_COMPLETE : NOP_WIZARD_PENDING;
    return wizard;
}

void nop_wizard_destroy(nop_wizard_t *wizard)
{
    if (!wizard)
        return;
    osal_mutex_destroy(wizard->mutex);
    nop_free(wizard);
}

/* Move PENDING -> IN_PROGRESS on the first completed step. Caller holds lock. */
static void wizard_touch_locked(nop_wizard_t *wizard)
{
    if (wizard->state == NOP_WIZARD_PENDING)
        wizard->state = NOP_WIZARD_IN_PROGRESS;
}

nop_status_t nop_wizard_set_password(nop_wizard_t *wizard, const char *password)
{
    if (!wizard)
        return NOP_ERR_PARAM;
    if (nop_wizard_check_password(password) != NOP_WIZARD_PW_OK)
        return NOP_ERR_PARAM;
    osal_mutex_lock(wizard->mutex);
    wizard->has_password = 1;
    wizard_touch_locked(wizard);
    osal_mutex_unlock(wizard->mutex);
    return NOP_OK;
}

nop_status_t nop_wizard_set_device_name(nop_wizard_t *wizard, const char *name)
{
    if (!wizard || !name || name[0] == '\0')
        return NOP_ERR_PARAM;
    osal_mutex_lock(wizard->mutex);
    snprintf(wizard->device_name, sizeof(wizard->device_name), "%s", name);
    wizard->has_device_name = 1;
    wizard_touch_locked(wizard);
    osal_mutex_unlock(wizard->mutex);
    return NOP_OK;
}

void nop_wizard_mark_channel_imaging(nop_wizard_t *wizard)
{
    if (!wizard)
        return;
    osal_mutex_lock(wizard->mutex);
    wizard->channel_imaging_done = 1;
    wizard_touch_locked(wizard);
    osal_mutex_unlock(wizard->mutex);
}

void nop_wizard_mark_firmware_checked(nop_wizard_t *wizard)
{
    if (!wizard)
        return;
    osal_mutex_lock(wizard->mutex);
    wizard->firmware_checked = 1;
    wizard_touch_locked(wizard);
    osal_mutex_unlock(wizard->mutex);
}

nop_status_t nop_wizard_complete(nop_wizard_t *wizard)
{
    nop_status_t status = NOP_OK;
    if (!wizard)
        return NOP_ERR_PARAM;
    osal_mutex_lock(wizard->mutex);
    if (!wizard->has_password)
        status = NOP_ERR_STATE;          /* Admin password is mandatory */
    else
        wizard->state = NOP_WIZARD_COMPLETE;
    osal_mutex_unlock(wizard->mutex);
    return status;
}

void nop_wizard_reset(nop_wizard_t *wizard)
{
    if (!wizard)
        return;
    osal_mutex_lock(wizard->mutex);
    wizard->state                = NOP_WIZARD_PENDING;
    wizard->has_password         = 0;
    wizard->has_device_name      = 0;
    wizard->channel_imaging_done = 0;
    wizard->firmware_checked     = 0;
    wizard->device_name[0]       = '\0';
    osal_mutex_unlock(wizard->mutex);
}

int nop_wizard_is_complete(const nop_wizard_t *wizard)
{
    int complete;
    if (!wizard)
        return 0;
    osal_mutex_lock(wizard->mutex);
    complete = (wizard->state == NOP_WIZARD_COMPLETE);
    osal_mutex_unlock(wizard->mutex);
    return complete;
}

void nop_wizard_status(nop_wizard_t *wizard, nop_wizard_status_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!wizard)
        return;
    osal_mutex_lock(wizard->mutex);
    out->state                = wizard->state;
    out->has_password         = wizard->has_password;
    out->has_device_name      = wizard->has_device_name;
    out->channel_imaging_done = wizard->channel_imaging_done;
    out->firmware_checked     = wizard->firmware_checked;
    snprintf(out->device_name, sizeof(out->device_name), "%s", wizard->device_name);
    /* Next step to complete, in the published order. */
    if (wizard->state == NOP_WIZARD_COMPLETE)
        out->step = NOP_WIZARD_STEP_DONE;
    else if (!wizard->has_password)
        out->step = NOP_WIZARD_STEP_PASSWORD;
    else if (!wizard->has_device_name)
        out->step = NOP_WIZARD_STEP_DEVICE_NAME;
    else if (!wizard->channel_imaging_done)
        out->step = NOP_WIZARD_STEP_CHANNEL_IMAGING;
    else if (!wizard->firmware_checked)
        out->step = NOP_WIZARD_STEP_FIRMWARE;
    else
        out->step = NOP_WIZARD_STEP_DONE;
    osal_mutex_unlock(wizard->mutex);
}
