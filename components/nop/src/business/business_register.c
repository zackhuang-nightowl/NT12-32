/**
 * @file business_register.c
 * @brief Role-aware self-registration of built-in capability handlers.
 *
 * Device role decides which command set the firmware exposes:
 *  - SHARED: device features any role serves (info/auth/system/stream/ptz/
 *    light/audio/ai/storage/record/ota/network/push/event/cloud/doorbell/misc).
 *  - CAMERA (NOP_ROLE_IPC): a standalone camera/doorbell — its camera services
 *    (8012 event-center *server*, local SD record, light linkage) attach on top
 *    (see services/, P0+). No NVR local-UI commands.
 *  - VIDEO RECORDER (NOP_ROLE_NVR): adds the GUI_* local-UI command family
 *    (account/network/system/lan/playback) the on-device LVGL UI drives; it is
 *    the 8012 event-center *client* and manages attached cameras.
 *
 * A camera that receives an NVR-only command (or vice versa) gets 501 — correct
 * per role.
 */
#include "business/business.h"

/* Device features served by both camera and videoRecorder roles. */
static void register_shared(nop_router_t *router)
{
    cap_device_register(router);
    cap_auth_register(router);
    cap_system_register(router);

    cap_stream_register(router);
    cap_ptz_register(router);
    cap_light_register(router);
    cap_storage_register(router);
    cap_record_register(router);
    cap_audio_register(router);
    cap_ota_register(router);

    cap_floodlight_register(router);
    cap_video_register(router);
    cap_network_register(router);
    cap_push_register(router);
    cap_event_register(router);
    cap_ai_register(router);
    cap_cloud_register(router);
    cap_doorbell_register(router);
    cap_misc_register(router);

    cap_ptz_patrol_register(router);
    cap_ai_advanced_register(router);
    cap_agent_register(router);
    cap_misc_ext_register(router);
}

/* videoRecorder (NVR) local-UI command family — the GUI_* set. */
static void register_video_recorder(nop_router_t *router)
{
    cap_gui_account_register(router);
    cap_gui_network_register(router);
    cap_gui_system_register(router);
    cap_gui_lan_register(router);
    cap_gui_playback_register(router);
}

/* camera (IPC) role-specific command handlers (none distinct today; camera
 * behavior is delivered by the camera services layer, see services/). */
static void register_camera(nop_router_t *router)
{
    (void)router;
}

void nop_business_register_all(nop_router_t *router, nop_role_t role)
{
    register_shared(router);
    if (role == NOP_ROLE_NVR)
        register_video_recorder(router);
    else
        register_camera(router);
}
