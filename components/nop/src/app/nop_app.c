/**
 * @file nop_app.c
 * @brief One-stop façade: builds capability registry + router, registers all
 *        built-in handlers, and lights up capabilities from registered HALs.
 */
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_version.h"
#include "nop_sdk/nop_caps.h"
#include "nop_sdk/hal/hal_registry.h"

#include "base/nop_mem.h"
#include "nop/nop_router.h"
#include "nop/nop_longpoll.h"
#include "capability/cap_registry.h"
#include "business/business.h"
#include "nop_sdk/nop_event.h"

#include <string.h>

struct nop_app {
    cap_registry_t           *capability_registry;
    nop_router_t             *router;
    nop_business_context_t    business_context;
    nop_longpoll_t           *longpoll;            /* GUI_longPolling wakeups */
    nop_event_hub_t          *bridged_hub;         /* hub the bridge is attached to */
    nop_event_subscription_t *event_subscription;  /* event->longpoll bridge */
};

/* Event->longpoll bridge: wake GUI_longPolling clients on any detection. */
static void nop_app_event_to_longpoll(void *ctx, const nop_event_t *event)
{
    nop_app_t *app = (nop_app_t *)ctx;
    (void)event;
    if (app->longpoll)
        nop_longpoll_signal(app->longpoll, 1u);
}

const char *nop_sdk_version(void)
{
    return NOP_SDK_VERSION_STR;
}

/* Light up capabilities whose backing HAL is registered. */
static void auto_light_capabilities(cap_registry_t *capability_registry)
{
    if (hal_registry_has(HAL_VIDEO))
        cap_registry_enable(capability_registry, CAP_STREAM, true);
    if (hal_registry_has(HAL_PTZ))
        cap_registry_enable(capability_registry, CAP_PTZ, true);
    if (hal_registry_has(HAL_LIGHT))
        cap_registry_enable(capability_registry, CAP_LIGHT, true);
    if (hal_registry_has(HAL_STORAGE))
        cap_registry_enable(capability_registry, CAP_STORAGE, true);
    if (hal_registry_has(HAL_RECORD))
        cap_registry_enable(capability_registry, CAP_RECORD, true);
    if (hal_registry_has(HAL_AUDIO))
        cap_registry_enable(capability_registry, CAP_AUDIO, true);
    if (hal_registry_has(HAL_OTA))
        cap_registry_enable(capability_registry, CAP_OTA, true);
}

nop_app_t *nop_app_create(const nop_app_config_t *config)
{
    nop_app_t *app = (nop_app_t *)nop_calloc(1, sizeof(*app));
    int        auto_capabilities = config ? config->auto_caps : 1;
    nop_role_t role = config ? config->role : NOP_ROLE_IPC;
    if (!app)
        return NULL;

    /* default config: auto-light capabilities unless explicitly disabled */
    if (!config)
        auto_capabilities = 1;

    app->capability_registry = cap_registry_create();
    if (!app->capability_registry)
        goto fail;
    app->router = nop_router_create(app->capability_registry);
    if (!app->router)
        goto fail;

    app->longpoll = nop_longpoll_create();   /* NULL-tolerant downstream */
    app->business_context.capability_registry = app->capability_registry;
    app->business_context.longpoll = app->longpoll;
    nop_router_set_handler_ctx(app->router, &app->business_context);
    nop_business_register_all(app->router, role);

    if (auto_capabilities)
        auto_light_capabilities(app->capability_registry);

    return app;

fail:
    nop_app_destroy(app);
    return NULL;
}

void nop_app_destroy(nop_app_t *app)
{
    if (!app)
        return;
    if (app->bridged_hub && app->event_subscription)
        nop_event_unsubscribe(app->bridged_hub, app->event_subscription);
    nop_longpoll_destroy(app->longpoll);
    nop_router_destroy(app->router);
    cap_registry_destroy(app->capability_registry);
    nop_free(app);
}

nop_status_t nop_app_dispatch(nop_app_t *app, const char *json_in, char **json_out)
{
    if (!app || !json_in || !json_out)
        return NOP_ERR_PARAM;
    return nop_router_dispatch(app->router, json_in, strlen(json_in), json_out);
}

void nop_app_free_response(char *json_out)
{
    nop_router_free_response(json_out);
}

nop_status_t nop_app_set_capability(nop_app_t *app, int capability_id, int enable)
{
    if (!app || capability_id < 0 || capability_id >= NOP_CAP_MAX)
        return NOP_ERR_PARAM;
    return cap_registry_enable(app->capability_registry,
                               (nop_cap_id_t)capability_id, enable ? true : false);
}

nop_status_t nop_app_set_device_config(nop_app_t *app, const void *device_config)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.device_config =
        (const struct nop_device_config *)device_config;
    return NOP_OK;
}

nop_status_t nop_app_set_event_hub(nop_app_t *app, void *event_hub)
{
    if (!app)
        return NOP_ERR_PARAM;
    /* Detach a prior bridge. */
    if (app->bridged_hub && app->event_subscription) {
        nop_event_unsubscribe(app->bridged_hub, app->event_subscription);
        app->event_subscription = NULL;
        app->bridged_hub = NULL;
    }
    app->business_context.event_hub = (struct nop_event_hub *)event_hub;
    /* Attach the event->longpoll bridge so GUI_longPolling wakes on events. */
    if (event_hub) {
        app->bridged_hub = (nop_event_hub_t *)event_hub;
        app->event_subscription =
            nop_event_subscribe(app->bridged_hub, nop_app_event_to_longpoll, app);
    }
    return NOP_OK;
}

nop_status_t nop_app_set_nvr_addcamera(nop_app_t *app, void *addcamera)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.nvr_addcamera = (struct nop_nvr_addcamera *)addcamera;
    return NOP_OK;
}

nop_status_t nop_app_set_ptz_patrol(nop_app_t *app, void *ptz_patrol)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.ptz_patrol = (struct nop_ptz_patrol *)ptz_patrol;
    return NOP_OK;
}

nop_status_t nop_app_set_chime(nop_app_t *app, void *chime)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.chime = (struct nop_chime *)chime;
    return NOP_OK;
}

nop_status_t nop_app_set_nvr_channels(nop_app_t *app, void *channels)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.channels = (struct nop_nvr_channels *)channels;
    return NOP_OK;
}

nop_status_t nop_app_set_onvif_backend(nop_app_t *app, void *onvif_backend)
{
    if (!app)
        return NOP_ERR_PARAM;
    app->business_context.onvif_backend = (struct nop_onvif_map_backend *)onvif_backend;
    return NOP_OK;
}
