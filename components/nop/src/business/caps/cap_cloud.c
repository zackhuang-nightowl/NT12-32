/**
 * @file cap_cloud.c
 * @brief Cloud/recording/playback — NVR implements LOCAL handlers in app/router.
 *
 * Standalone-camera memory stubs removed. cap_cloud_register is a no-op so
 * nop_app_dispatch falls through to 501 for any command not in g_nvr_cmd_table.
 */
#include "business/business.h"

void cap_cloud_register(nop_router_t *router)
{
    (void)router;
}
