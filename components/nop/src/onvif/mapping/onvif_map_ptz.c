/**
 * @file onvif_map_ptz.c
 * @brief §2 PTZ handlers — one function per NOP interface, per NOPMappingONVIF.md
 *        §2. Each handler is referenced by exactly one row of g_onvif_map_table.
 *
 *   ptzMove       -> ContinuousMove   (direction + speed 0..100)
 *   ptzMoveByStep -> ContinuousMove   (direction + step 1..10)
 *   ptzMoveStop   -> Stop             (GUI 松手另发，本文件不在 ByStep 里配对 Stop)
 *   ptzGotoPreset -> GotoPreset       (int preset -> token; speed 1..10 -> 0.1..1)
 *
 * 成功：statusCode 200，无 content。失败：仍 200，content.error 写原因
 * （NOP 约定；勿回裸 400，GUI 看不到失败原因）。
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_utils.h"
#include "nop_sdk/nop_onvif.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "nop_sdk/nop_log.h"
#include "base/nop_json.h"

#include <string.h>

/* 失败走 200 + content.error，成功不带 content。 */
static nop_status_t ptz_fail(nop_response_t *resp, const char *err)
{
    const char *e = (err && err[0]) ? err : "onvif_failed";
    NOP_LOGW("onvif_map ptz fail: %s", e);
    if (!resp)
        return NOP_OK;
    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", e);
    return NOP_OK;
}

static const char *ptz_soap_err(nop_onvif_device_t *dev)
{
    const char *e = nop_onvif_device_last_error(dev);
    return (e && e[0]) ? e : "onvif_failed";
}

/* Shared: ContinuousMove. GUI 的 ptzMove / ptzMoveByStep 都走这里；Stop 另命令。 */
static nop_status_t ptz_continuous(nop_onvif_map_backend_t *be, int channel,
                                   const char *dir, float mag, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *profile;
    float            pan, tilt, zoom;

    if (!dir)
        return ptz_fail(resp, "invalid_param");
    s = onvif_session_begin(be, channel);
    if (!s)
        return ptz_fail(resp, "onvif_not_connected");
    profile = onvif_session_profile(s);
    if (!profile || !profile[0]) {
        onvif_session_end(be);
        return ptz_fail(resp, "no_ptz_profile");
    }
    onvif_map_dir_to_velocity(dir, mag, &pan, &tilt, &zoom);
    if (pan == 0.0f && tilt == 0.0f && zoom == 0.0f) {
        onvif_session_end(be);
        return ptz_fail(resp, "invalid_direction");
    }
    if (nop_onvif_ptz_continuous_move(onvif_session_dev(s), profile, pan, tilt, zoom) != 0) {
        const char *e = ptz_soap_err(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_ptzMove(nop_onvif_map_backend_t *be, int ch,
                               const nop_request_t *req, nop_response_t *resp)
{
    const char *dir = nop_json_str(req->args, "direction", NULL);
    float       mag = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 50), 100.0);
    return ptz_continuous(be, ch, dir, mag, resp);
}

nop_status_t onvif_map_ptzMoveByStep(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    const char *dir = nop_json_str(req->args, "direction", NULL);
    float       mag = onvif_map_level_to_unit(nop_json_num(req->args, "step", 1), 10.0);
    return ptz_continuous(be, ch, dir, mag, resp);
}

nop_status_t onvif_map_ptzMoveStop(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *profile;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return ptz_fail(resp, "onvif_not_connected");
    profile = onvif_session_profile(s);
    if (!profile || !profile[0]) {
        onvif_session_end(be);
        return ptz_fail(resp, "no_ptz_profile");
    }
    if (nop_onvif_ptz_stop(onvif_session_dev(s), profile, 1, 1) != 0) {
        const char *e = ptz_soap_err(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_ptzGotoPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *profile;
    char             token[32];
    int              preset;
    float            speed;

    preset = (int)nop_json_num(req->args, "preset", 0);
    /* NOP speed 1..10 -> ONVIF 0.1..1.0 (nop 1 == onvif 0.1). */
    speed  = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 0), 10.0);

    s = onvif_session_begin(be, ch);
    if (!s)
        return ptz_fail(resp, "onvif_not_connected");
    profile = onvif_session_profile(s);
    if (!profile || !profile[0]) {
        onvif_session_end(be);
        return ptz_fail(resp, "no_ptz_profile");
    }
    if (nop_onvif_ptz_goto_preset_speed(
            onvif_session_dev(s), profile,
            onvif_map_int_token(preset, token, sizeof(token)), speed) != 0) {
        const char *e = ptz_soap_err(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

#endif /* NOP_ONVIF_MAP */
