/**
 * @file business.h  (internal)
 * @brief Shared context + self-registration entry points for business handlers.
 *        Each cap_*.c exposes cap_<group>_register(router) and registers its
 *        commands so adding commands never touches the protocol core.
 */
#ifndef NOP_BUSINESS_BUSINESS_H
#define NOP_BUSINESS_BUSINESS_H

#include "nop/nop_router.h"
#include "capability/cap_registry.h"
#include "nop_sdk/nop_types.h"   /* nop_role_t */

struct nop_device_config;   /* nop_sdk/nop_config.h — optional provisioning */
struct nop_event_hub;       /* nop_sdk/nop_event.h — optional event source */
struct nop_longpoll;        /* nop/nop_longpoll.h — GUI_longPolling wakeups */
struct nop_nvr_addcamera;   /* nop_sdk/nop_nvr_addcamera.h — AddWirelessCameras engine */
struct nop_ptz_patrol;      /* nop_sdk/nop_ptz_patrol.h — PTZ patrol execution engine */
struct nop_chime;           /* nop_sdk/nop_chime.h — wireless chime registry/pairing */
struct nop_nvr_channels;    /* nop_sdk/nop_nvr_channels.h — bound-camera registry */
struct nop_onvif_map_backend; /* onvif/mapping/nop_onvif_map.h — ONVIF client mapping backend */

/** Context handed to every handler via the router's handler context. */
typedef struct nop_business_context {
    cap_registry_t                 *capability_registry;
    const struct nop_device_config *device_config;  /* NULL until provisioned */
    struct nop_event_hub           *event_hub;      /* NULL until attached */
    struct nop_longpoll            *longpoll;       /* GUI_longPolling pending mask */
    struct nop_nvr_addcamera       *nvr_addcamera;  /* AddWirelessCameras engine (NVR); NULL until attached */
    struct nop_ptz_patrol          *ptz_patrol;     /* PTZ patrol execution engine; NULL until attached */
    struct nop_chime               *chime;          /* wireless chime registry/pairing; NULL until attached */
    struct nop_nvr_channels        *channels;       /* bound-camera registry (NVR); NULL until attached */
    struct nop_onvif_map_backend   *onvif_backend;  /* ONVIF mapping backend; NULL until attached */
} nop_business_context_t;

/* Batch 1: pure protocol, no hardware. */
void cap_device_register(nop_router_t *router);
void cap_auth_register(nop_router_t *router);
void cap_system_register(nop_router_t *router);

/* Batch 2: HAL-backed (gated by capability + HAL presence). */
void cap_stream_register(nop_router_t *router);
void cap_ptz_register(nop_router_t *router);
void cap_light_register(nop_router_t *router);
void cap_storage_register(nop_router_t *router);
void cap_record_register(nop_router_t *router);
void cap_audio_register(nop_router_t *router);
void cap_ota_register(nop_router_t *router);

/* Batch 3: full NOP API surface (config/query, in-memory state). */
void cap_floodlight_register(nop_router_t *router);
void cap_video_register(nop_router_t *router);
void cap_network_register(nop_router_t *router);
void cap_push_register(nop_router_t *router);
void cap_event_register(nop_router_t *router);
void cap_ai_register(nop_router_t *router);
void cap_cloud_register(nop_router_t *router);
void cap_doorbell_register(nop_router_t *router);
void cap_misc_register(nop_router_t *router);

/* Batch 4: full nop_api coverage — GUI (local UI), PTZ patrol, advanced AI,
 * agent/diagnostics, misc extras. */
void cap_gui_account_register(nop_router_t *router);
void cap_gui_network_register(nop_router_t *router);
void cap_gui_system_register(nop_router_t *router);
void cap_gui_lan_register(nop_router_t *router);
void cap_gui_playback_register(nop_router_t *router);
void cap_ptz_patrol_register(nop_router_t *router);
void cap_ai_advanced_register(nop_router_t *router);
void cap_agent_register(nop_router_t *router);
void cap_misc_ext_register(nop_router_t *router);

/** Register the built-in capability handlers for @p role on @p router
 *  (shared set + role-specific set). */
void nop_business_register_all(nop_router_t *router, nop_role_t role);

#endif /* NOP_BUSINESS_BUSINESS_H */
