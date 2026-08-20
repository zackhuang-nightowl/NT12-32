/**
 * @file onvif_map_internal.h  (internal)
 * @brief Contract shared between the dispatcher (onvif_map_dispatch.c), the
 *        per-channel session cache (onvif_map_session.c) and the domain mappers
 *        (onvif_map_<domain>.c). Compiled only inside the NOP_ONVIF_MAP build;
 *        never included by pure-core or business code.
 */
#ifndef NOP_ONVIF_MAP_INTERNAL_H
#define NOP_ONVIF_MAP_INTERNAL_H

#include "onvif/mapping/nop_onvif_map.h"
#include "nop_sdk/nop_onvif.h"        /* nop_onvif_device_t + client ABI */

/* ======================================================================== */
/* Per-channel ONVIF session                                                */
/* ======================================================================== */

/** One channel's live, authenticated ONVIF client handle + resolved tokens. */
typedef struct onvif_session onvif_session_t;

/**
 * Acquire (creating + authenticating + resolving profiles on first use) the
 * ONVIF session for @p channel and take the backend lock. Every begin() that
 * returns non-NULL MUST be paired with onvif_session_end(). Serializes ONVIF
 * access per backend (the vendored handle is not reentrant).
 * @return the session, or NULL if the channel is not a reachable ONVIF camera.
 */
onvif_session_t *onvif_session_begin(nop_onvif_map_backend_t *backend, int channel);

/** Release the backend lock taken by onvif_session_begin(). */
void onvif_session_end(nop_onvif_map_backend_t *backend);

/** The live ONVIF device handle for wire calls. */
nop_onvif_device_t *onvif_session_dev(onvif_session_t *session);

/** Default Media1 profile token (used for PTZ / streaming). "" if none. */
const char *onvif_session_profile(onvif_session_t *session);

/** stderr: which profile token PTZ will use and whether it came from cache or fallback. */
void onvif_session_log_profile(const onvif_session_t *session, int channel,
                               const char *cmd);

/** Default Media2 profile token (OSD / Mask / encoder); resolved lazily. "" if none. */
const char *onvif_session_media2_profile(onvif_session_t *session);

/** First video-source token (imaging). "" if none. */
const char *onvif_session_video_source(onvif_session_t *session);

/** Copy this channel's bound-source VideoSourceConfiguration token (OSD/Mask
 *  scope). @return 0 on success (0-on-success mirrors the ABI it replaces), -1
 *  when unresolved. */
int onvif_session_vsc(onvif_session_t *session, char *out, unsigned size);

/** Copy this channel's bound-source VideoAnalyticsConfiguration token (rules
 *  scope). @return 0 on success, -1 when unresolved. */
int onvif_session_analytics_cfg(onvif_session_t *session, char *out, unsigned size);

/** This channel's bound VideoSourceToken ("" == first/single source). Stable key
 *  for per-source capability lookups (nop_onvif_get_device_caps). */
const char *onvif_session_bound_source(onvif_session_t *session);

/** This channel's bound-source main/sub VideoEncoderConfiguration tokens (media
 *  scope). "" when unresolved (caller falls back to device-wide order). */
const char *onvif_session_main_venc(onvif_session_t *session);
const char *onvif_session_sub_venc(onvif_session_t *session);

/** Reverse-map a device's VideoSourceToken to the NVR channel bound to it (same
 *  host:port as @p ref_channel). @return channel index, or -1 if none. */
int onvif_backend_channel_for_source(nop_onvif_map_backend_t *be, int ref_channel,
                                     const char *source_token);

/** Bind state for graceful NOP errors (independent of session reachability):
 *  1 = channel has an ONVIF camera bound (host set), 0 = not bound. Lets a mapper
 *  emit "Camera Disconnected" vs "No Camera Binded" when a session can't open. */
int onvif_map_channel_bound(nop_onvif_map_backend_t *be, int channel);

/* ======================================================================== */
/* Domain mappers                                                           */
/* ======================================================================== */

/**
 * A domain mapper translates one NOP request (already known to target an ONVIF
 * channel) into ONVIF calls and fills @p resp->content. It runs with the
 * backend lock NOT held; it calls onvif_session_begin/end itself around wire
 * access. Return NOP_OK on success (business errors go in content), or an
 * nop_status_t error for the envelope status.
 */
typedef nop_status_t (*onvif_mapper_fn)(nop_onvif_map_backend_t *backend,
                                        int channel,
                                        const nop_request_t *req,
                                        nop_response_t *resp);

/** One row of the func-name -> handler table (one handler per NOP interface). */
typedef struct onvif_map_entry {
    const char      *func;    /**< exact NOP func name */
    onvif_mapper_fn  fn;      /**< handler that maps just this interface */
} onvif_map_entry_t;

/**
 * The authoritative NOP-func -> ONVIF-handler table, defined in
 * onvif_map_table.c. Each row maps one NOP interface to the single handler that
 * translates it per NOPMappingONVIF.md. Audit the doc against this one array.
 */
extern const onvif_map_entry_t g_onvif_map_table[];
extern const int               g_onvif_map_table_len;

/* ======================================================================== */
/* Per-interface handlers, grouped by spec domain. One function == one NOP    */
/* interface. Defined in the matching onvif_map_<domain>.c.                    */
/* ======================================================================== */

/* §2 PTZ — onvif_map_ptz.c */
nop_status_t onvif_map_ptzMove(nop_onvif_map_backend_t *be, int ch,
                               const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_ptzMoveByStep(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_ptzMoveStop(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_ptzGotoPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp);

/* §2 PTZ advanced — onvif_map_ptz_ext.c */
nop_status_t onvif_map_ptzFocusByStep(nop_onvif_map_backend_t *be, int ch,
                                      const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_ptzFocusStop(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_getPtzCapabilities(nop_onvif_map_backend_t *be, int ch,
                                          const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_getPtzPresets(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_setPtzPreset(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_gotoPtzPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_removePtzPreset(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_setPtzHome(nop_onvif_map_backend_t *be, int ch,
                                  const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_gotoPtzHome(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_getPtzPatrols(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_setPtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_operatePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                        const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_removePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp);

/* §7 Privacy Zone — onvif_map_privacy.c */
nop_status_t onvif_map_X_NightOwl_getChannelPrivacyZone(nop_onvif_map_backend_t *be, int ch,
                                                        const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_X_NightOwl_setChannelPrivacyZone(nop_onvif_map_backend_t *be, int ch,
                                                        const nop_request_t *req, nop_response_t *resp);

/* §5 OSD — onvif_map_osd.c */
nop_status_t onvif_map_X_NightOwl_getOSD(nop_onvif_map_backend_t *be, int ch,
                                         const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_X_NightOwl_setOSD(nop_onvif_map_backend_t *be, int ch,
                                         const nop_request_t *req, nop_response_t *resp);

/* §3 Media — onvif_map_media.c */
nop_status_t onvif_map_GUI_getChannelMediaProfiles(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_GUI_setChannelMediaProfiles(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req, nop_response_t *resp);

/* §1/§4 device capabilities — onvif_map_ai.c */
nop_status_t onvif_map_X_NightOwl_getDeviceCapabilities(nop_onvif_map_backend_t *be, int ch,
                                                        const nop_request_t *req, nop_response_t *resp);

/* §9 Smart AI line/field — onvif_map_ai.c */
nop_status_t onvif_map_AI_getChannelAICapabilities(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_AI_getChannelLineCrossDetect(nop_onvif_map_backend_t *be, int ch,
                                                    const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_AI_setChannelLineCrossDetect(nop_onvif_map_backend_t *be, int ch,
                                                    const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_AI_getChannelFieldIntrusionDetect(nop_onvif_map_backend_t *be, int ch,
                                                         const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_AI_setChannelFieldIntrusionDetect(nop_onvif_map_backend_t *be, int ch,
                                                         const nop_request_t *req, nop_response_t *resp);

/* §8 Object detection (sensor config) — onvif_map_ai.c */
nop_status_t onvif_map_getChannelSensorConfig(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_setChannelSensorConfig(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp);

/* §6 Firmware — onvif_map_firmware.c */
nop_status_t onvif_map_upgradeChannelFirmware(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp);

/* §8 Motion activity zone (CellMotion) — onvif_map_motion.c */
nop_status_t onvif_map_X_NightOwl_getChannelActivityZoneTypes(nop_onvif_map_backend_t *be, int ch,
                                                              const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_X_NightOwl_getChannelTriggerActivityZone(nop_onvif_map_backend_t *be, int ch,
                                                                const nop_request_t *req, nop_response_t *resp);
nop_status_t onvif_map_X_NightOwl_setChannelTriggerActivityZone(nop_onvif_map_backend_t *be, int ch,
                                                                const nop_request_t *req, nop_response_t *resp);

#endif /* NOP_ONVIF_MAP_INTERNAL_H */
