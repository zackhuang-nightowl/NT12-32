/**
 * @file nop_caps.h
 * @brief Capability ids — the single source of truth that drives command
 *        routing, getDeviceCapabilities, and (later) ONVIF gating. ~19 groups
 *        per the implementation plan. Values are part of the ABI.
 */
#ifndef NOP_SDK_CAPS_H
#define NOP_SDK_CAPS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nop_cap_id {
    CAP_DEVICE = 0,  /**< device info / capability query        */
    CAP_AUTH,        /**< login / credentials                   */
    CAP_SYSTEM,      /**< reboot / restore / logs               */
    CAP_STREAM,      /**< live & playback                       */
    CAP_PTZ,         /**< pan/tilt/zoom                         */
    CAP_LIGHT,       /**< flood / IR light                      */
    CAP_AI,          /**< smart detection                       */
    CAP_AUDIO,       /**< two-way audio                         */
    CAP_STORAGE,     /**< SD / disk                             */
    CAP_RECORD,      /**< recording schedules                   */
    CAP_CLOUD,       /**< cloud recording                       */
    CAP_OTA,         /**< firmware upgrade                      */
    CAP_PUSH,        /**< push notifications                    */
    CAP_NETWORK,     /**< wifi / ddns / network config          */
    CAP_BIND,        /**< NVR<->IPC binding / discovery         */
    CAP_NOTIFY,      /**< events / longPolling                  */
    CAP_MISC,        /**< everything else                       */
    NOP_CAP_MAX
} nop_cap_id_t;

/** @return stable lowercase id string for @p cap, or "" if out of range. */
const char *nop_cap_name(nop_cap_id_t cap);

/** @return capability id for lowercase @p name, or NOP_CAP_MAX if unknown. */
nop_cap_id_t nop_cap_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_CAPS_H */
