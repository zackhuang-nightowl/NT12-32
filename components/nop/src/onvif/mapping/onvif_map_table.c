/**
 * @file onvif_map_table.c
 * @brief The single authoritative NOP-func -> ONVIF-handler mapping table.
 *        One row per NOP interface, grouped by NOPMappingONVIF.md domain, each
 *        pointing at the one handler that translates that interface. To audit
 *        coverage, read this array against the spec's domain sections.
 *
 * Handlers are defined in the per-domain onvif_map_<domain>.c files and declared
 * in onvif_map_internal.h. Compiled only in the NOP_ONVIF_MAP build.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"

const onvif_map_entry_t g_onvif_map_table[] = {
    /* ---- §2 PTZ (Pan-Tilt-Zoom) ---------------------------------------- */
    { "getPtzCapabilities", onvif_map_getPtzCapabilities }, /* -> PTZ Node numeric limits */
    { "ptzMove",         onvif_map_ptzMove },        /* -> ContinuousMove       */
    { "ptzMoveByStep",   onvif_map_ptzMoveByStep },  /* -> ContinuousMove       */
    { "ptzMoveStop",     onvif_map_ptzMoveStop },    /* -> Stop                 */
    { "ptzGotoPreset",   onvif_map_ptzGotoPreset },  /* -> GotoPreset (+speed)  */
    { "ptzFocusByStep",  onvif_map_ptzFocusByStep }, /* -> Imaging Focus Move   */
    { "ptzFocusStop",    onvif_map_ptzFocusStop },   /* -> Imaging Focus Stop   */
    { "getPtzPresets",   onvif_map_getPtzPresets },  /* -> GetPresets           */
    { "setPtzPreset",    onvif_map_setPtzPreset },   /* -> SetPreset            */
    { "gotoPtzPreset",   onvif_map_gotoPtzPreset },  /* -> GotoPreset (token)   */
    { "removePtzPreset", onvif_map_removePtzPreset },/* -> RemovePreset         */
    { "setPtzHome",      onvif_map_setPtzHome },     /* -> SetHomePosition      */
    { "gotoPtzHome",     onvif_map_gotoPtzHome },    /* -> GotoHomePosition     */
    { "getPtzPatrols",   onvif_map_getPtzPatrols },  /* -> GetPresetTours       */
    { "setPtzPatrol",    onvif_map_setPtzPatrol },   /* -> Create/ModifyPresetTour (upsert) */
    { "operatePtzPatrol",onvif_map_operatePtzPatrol},/* -> OperatePresetTour    */
    { "removePtzPatrol", onvif_map_removePtzPatrol },/* -> RemovePresetTour     */

    /* ---- §7 Privacy Zone (Media2 Mask) --------------------------------- */
    { "X_NightOwl_getChannelPrivacyZone", onvif_map_X_NightOwl_getChannelPrivacyZone }, /* -> GetMasks  */
    { "X_NightOwl_setChannelPrivacyZone", onvif_map_X_NightOwl_setChannelPrivacyZone }, /* -> Create/DeleteMask */

    /* ---- §5 OSD (Media2 OSD) ------------------------------------------- */
    { "X_NightOwl_getOSD", onvif_map_X_NightOwl_getOSD },  /* -> GetOSDs           */
    { "X_NightOwl_setOSD", onvif_map_X_NightOwl_setOSD },  /* -> Set/Create/DeleteOSD */

    /* ---- §3 Media (Media2 VideoEncoder) -------------------------------- */
    { "GUI_getChannelMediaProfiles", onvif_map_GUI_getChannelMediaProfiles }, /* -> GetVideoEncoderConfigurations */
    { "GUI_setChannelMediaProfiles", onvif_map_GUI_setChannelMediaProfiles }, /* -> SetVideoEncoderConfiguration */

    /* ---- §1/§4 Device capabilities (single ONVIF camera) --------------- */
    { "X_NightOwl_getDeviceCapabilities", onvif_map_X_NightOwl_getDeviceCapabilities }, /* -> 连接缓存 caps */

    /* ---- §9 Smart AI: capabilities + line-cross / field-intrusion (Analytics rules) --- */
    { "AI_getChannelAICapabilities",       onvif_map_AI_getChannelAICapabilities },       /* -> 连接缓存 AI caps */
    { "AI_getChannelLineCrossDetect",      onvif_map_AI_getChannelLineCrossDetect },      /* -> GetRules(LineDetector)  */
    { "AI_setChannelLineCrossDetect",      onvif_map_AI_setChannelLineCrossDetect },      /* -> Create/DeleteRules      */
    { "AI_getChannelFieldIntrusionDetect", onvif_map_AI_getChannelFieldIntrusionDetect }, /* -> GetRules(FieldDetector) */
    { "AI_setChannelFieldIntrusionDetect", onvif_map_AI_setChannelFieldIntrusionDetect }, /* -> Create/DeleteRules      */

    /* ---- §8 Object detection (ObjectDetection rules by ClassFilter) ----- */
    { "getChannelSensorConfig", onvif_map_getChannelSensorConfig }, /* -> GetRules(ObjectDetection)      */
    { "setChannelSensorConfig", onvif_map_setChannelSensorConfig }, /* -> Create/DeleteRules per class   */

    /* ---- §6 Firmware upgrade ------------------------------------------- */
    { "GUI_upgradeChannelFirmware",        onvif_map_upgradeChannelFirmware }, /* -> StartFirmwareUpgrade */
    { "X_NightOwl_upgradeChannelFirmware", onvif_map_upgradeChannelFirmware }, /* -> StartFirmwareUpgrade */

    /* ---- §8 Motion activity zone (CellMotionDetector) ------------------ */
    { "X_NightOwl_getChannelActivityZoneTypes",   onvif_map_X_NightOwl_getChannelActivityZoneTypes },   /* -> GetRules 含 Motion → pixelChange */
    { "X_NightOwl_getChannelTriggerActivityZone", onvif_map_X_NightOwl_getChannelTriggerActivityZone }, /* -> GetRules(CellMotion) */
    { "X_NightOwl_setChannelTriggerActivityZone", onvif_map_X_NightOwl_setChannelTriggerActivityZone }, /* -> Create/DeleteRules  */
};

const int g_onvif_map_table_len =
    (int)(sizeof(g_onvif_map_table) / sizeof(g_onvif_map_table[0]));

#endif /* NOP_ONVIF_MAP */
