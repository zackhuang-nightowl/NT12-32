#include "nop_sdk/hal/hal_registry.h"

#include <stddef.h>

/* Process-wide HAL table. Firmware registers once at startup; reads are
 * lock-free (set-once before serving traffic is the expected usage). */
static const void *g_hal[HAL_ID_MAX];

nop_status_t hal_register(hal_id_t id, const void *vtable)
{
    if (id < 0 || id >= HAL_ID_MAX)
        return NOP_ERR_PARAM;
    g_hal[id] = vtable;
    return NOP_OK;
}

const void *hal_registry_get(hal_id_t id)
{
    if (id < 0 || id >= HAL_ID_MAX)
        return NULL;
    return g_hal[id];
}

int hal_registry_has(hal_id_t id)
{
    return (id >= 0 && id < HAL_ID_MAX && g_hal[id] != NULL) ? 1 : 0;
}
