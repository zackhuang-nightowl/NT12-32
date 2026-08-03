/**
 * @file onvif_map_ptz.c
 * @brief §2 PTZ handlers — one function per NOP interface, per NOPMappingONVIF.md
 *        §2. Each handler is referenced by exactly one row of g_onvif_map_table.
 *
 *   ptzMove       -> ContinuousMove   (direction + speed 0..100)
 *   ptzMoveByStep -> ContinuousMove   (direction + step 1..10), paired w/ Stop
 *   ptzMoveStop   -> Stop
 *   ptzGotoPreset -> GotoPreset       (int preset -> token; speed 1..10 -> 0.1..1)
 *
 * Reuses the existing client ABI (nop_onvif.h) for move/stop and the extended
 * ABI (nop_onvif_ext.h) for GotoPreset-with-speed. Shared conversions come from
 * onvif_map_utils.h — no ad-hoc math here.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_utils.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

/* Shared: issue a ContinuousMove from a direction + normalized magnitude. */
static nop_status_t ptz_continuous(nop_onvif_map_backend_t *be, int channel,
                                   const char *dir, float mag)
{
    onvif_session_t *s;
    float            pan, tilt, zoom;
    nop_status_t     rc;

    if (!dir)
        return NOP_ERR_PARAM;
    s = onvif_session_begin(be, channel);
    if (!s)
        return NOP_ERR_IO;
    onvif_map_dir_to_velocity(dir, mag, &pan, &tilt, &zoom);
    rc = onvif_map_rc(nop_onvif_ptz_continuous_move(onvif_session_dev(s),
                                                    onvif_session_profile(s),
                                                    pan, tilt, zoom));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_ptzMove(nop_onvif_map_backend_t *be, int ch,
                               const nop_request_t *req, nop_response_t *resp)
{
    const char *dir = nop_json_str(req->args, "direction", NULL);
    float       mag = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 50), 100.0);
    (void)resp;
    return ptz_continuous(be, ch, dir, mag);
}

nop_status_t onvif_map_ptzMoveByStep(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    const char *dir = nop_json_str(req->args, "direction", NULL);
    float       mag = onvif_map_level_to_unit(nop_json_num(req->args, "step", 1), 10.0);
    (void)resp;
    return ptz_continuous(be, ch, dir, mag);
}

nop_status_t onvif_map_ptzMoveStop(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_status_t     rc;
    (void)req; (void)resp;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_stop(onvif_session_dev(s),
                                         onvif_session_profile(s), 1, 1));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_ptzGotoPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    char             token[32];
    int              preset;
    float            speed;
    nop_status_t     rc;
    (void)resp;

    preset = (int)nop_json_num(req->args, "preset", 0);
    /* NOP speed 1..10 -> ONVIF 0.1..1.0 (nop 1 == onvif 0.1). */
    speed  = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 0), 10.0);

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_goto_preset_speed(
            onvif_session_dev(s), onvif_session_profile(s),
            onvif_map_int_token(preset, token, sizeof(token)), speed));
    onvif_session_end(be);
    return rc;
}

#endif /* NOP_ONVIF_MAP */
