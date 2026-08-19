/**
 * @file onvif_map_ptz_ext.c
 * @brief §2 PTZ advanced handlers — focus / presets / home / patrol. One handler
 *        per NOP interface (NOPMappingONVIF.md §2), complementing onvif_map_ptz.c.
 *
 *   ptzFocusByStep/Stop -> Imaging continuous focus Move/Stop
 *   getPtzPresets/setPtzPreset/gotoPtzPreset/removePtzPreset -> PTZ preset ops
 *   setPtzHome/gotoPtzHome -> SetHomePosition / GotoHomePosition
 *   getPtzPatrols/setPtzPatrol/operatePtzPatrol/removePtzPatrol -> PresetTour ops
 *
 * NOP failure convention (same as onvif_map_ptz.c): ONVIF failures answer
 * statusCode 200 with content.error (never a bare 400 — the GUI must see the
 * reason). NOP_ERR_PARAM stays reserved for a malformed request; NOP_ERR_NOTIMPL
 * for an unsupported feature (-> 501).
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_utils.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <string.h>

#define PTZ_MAX_PRESETS 64
#define PTZ_MAX_TOURS   16

/* Failure -> 200 + content.error (success returns NOP_OK, content set by caller). */
static nop_status_t ptz_ext_fail(nop_response_t *resp, const char *err)
{
    const char *e = (err && err[0]) ? err : "onvif_failed";
    if (!resp)
        return NOP_OK;
    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", e);
    return NOP_OK;
}

/* Best-effort ONVIF fault string for content.error. */
static const char *ptz_ext_soap(nop_onvif_device_t *dev)
{
    const char *e = nop_onvif_device_last_error(dev);
    return (e && e[0]) ? e : "onvif_failed";
}

/* ---- focus (Imaging Move) ---------------------------------------------- */

nop_status_t onvif_map_ptzFocusByStep(nop_onvif_map_backend_t *be, int ch,
                                      const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *dir;
    char             vs[100];
    float            speed;

    dir = nop_json_str(req->args, "direction", NULL);
    if (!dir)
        return NOP_ERR_PARAM;
    speed = onvif_map_level_to_unit(nop_json_num(req->args, "step", 1), 10.0);
    if (strcmp(dir, "focusIn") != 0)
        speed = -speed;   /* focusOut / near */

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    /* Imaging Move 要 VideoSourceToken（物理源），不是 VSC token */
    {
        const char *src = onvif_session_video_source(s);
        if (!src || !src[0]) src = onvif_session_bound_source(s);
        if (!src || !src[0]) { onvif_session_end(be); return ptz_ext_fail(resp, "no_video_source"); }
        snprintf(vs, sizeof(vs), "%s", src);
    }
    if (nop_onvif_img_focus_move(onvif_session_dev(s), vs, speed) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_ptzFocusStop(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    char             vs[100];
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    {
        const char *src = onvif_session_video_source(s);
        if (!src || !src[0]) src = onvif_session_bound_source(s);
        if (!src || !src[0]) { onvif_session_end(be); return ptz_ext_fail(resp, "no_video_source"); }
        snprintf(vs, sizeof(vs), "%s", src);
    }
    if (nop_onvif_img_focus_stop(onvif_session_dev(s), vs) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

/* ---- capabilities (numeric limits) ------------------------------------- */

/* getPtzCapabilities: per-channel PTZ numeric limits only (feature *flags* live
 * in X_NightOwl_getDeviceCapabilities channels[].ptz). Sourced from the PTZ Node
 * (MaximumNumberOfPresets / MaximumNumberOfPresetTours). `home` has no numeric
 * limit so it never appears; `focus` likewise. 501 when the channel has no PTZ
 * feature at all. maxSpotCount / dwell bounds aren't exposed by the ONVIF node,
 * so they are omitted (Optional per the API doc). */
nop_status_t onvif_map_getPtzCapabilities(nop_onvif_map_backend_t *be, int ch,
                                          const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t     *s;
    nop_onvif_dev_caps_t dc;
    int                  rc;
    nop_json_t          *arr;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    memset(&dc, 0, sizeof(dc));
    rc = nop_onvif_get_device_caps(onvif_session_dev(s), onvif_session_bound_source(s), &dc);
    onvif_session_end(be);
    fprintf(stderr, "[onvif_map] getPtzCapabilities ch=%d get_caps rc=%d has_ptz=%d preset=%d(max=%d) "
                    "patrol=%d(max=%d) focus=%d\n",
            ch, rc, dc.has_ptz, dc.ptz_preset, dc.ptz_max_presets, dc.ptz_patrol, dc.ptz_max_tours, dc.ptz_focus);
    if (rc != 0) return NOP_ERR_IO;

    /* No PTZ feature at all → 501 NOT_SUPPORT (per doc Response 2). */
    if (!dc.has_ptz && !dc.ptz_pan && !dc.ptz_tilt && !dc.ptz_zoom &&
        !dc.ptz_preset && !dc.ptz_home && !dc.ptz_patrol && !dc.ptz_focus && !dc.ptz_hdtrack)
        return NOP_ERR_NOTIMPL;

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    arr = nop_json_arr();

    if (dc.ptz_preset) {
        nop_json_t *e = nop_json_obj();
        nop_json_add_str(e, "type", "preset");
        if (dc.ptz_max_presets > 0)
            nop_json_add_int(e, "maxCount", dc.ptz_max_presets);
        nop_json_arr_push(arr, e);
    }
    if (dc.ptz_patrol) {
        nop_json_t *e   = nop_json_obj();
        nop_json_t *ops = nop_json_arr();
        nop_json_add_str(e, "type", "patrol");
        if (dc.ptz_max_tours > 0)
            nop_json_add_int(e, "maxCount", dc.ptz_max_tours);
        /* ops = operatePtzPatrol vocabulary our tour facade maps (Start/Stop/Pause). */
        nop_json_arr_push_str(ops, "start");
        nop_json_arr_push_str(ops, "stop");
        nop_json_arr_push_str(ops, "pause");
        nop_json_add(e, "ops", ops);
        nop_json_arr_push(arr, e);
    }

    nop_json_add(resp->content, "ptz", arr);
    return NOP_OK;
}

/* ---- presets ----------------------------------------------------------- */

nop_status_t onvif_map_getPtzPresets(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t   *s;
    nop_onvif_preset_t presets[PTZ_MAX_PRESETS];
    nop_json_t        *arr;
    int                n, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    n = nop_onvif_ptz_get_presets(onvif_session_dev(s), onvif_session_profile(s),
                                  presets, PTZ_MAX_PRESETS);
    if (n < 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_add_str(e, "token", presets[i].token);
        nop_json_add_str(e, "name", presets[i].name);
        nop_json_arr_push(arr, e);
    }
    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    nop_json_add(resp->content, "presets", arr);
    return NOP_OK;
}

nop_status_t onvif_map_setPtzPreset(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    char             out_token[100] = "";
    const char      *token, *name;

    token = nop_json_str(req->args, "token", "");
    name  = nop_json_str(req->args, "name", "");

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_set_preset_ex(onvif_session_dev(s), onvif_session_profile(s),
                                    token, name, out_token, sizeof(out_token)) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);   /* failure: content.error, no token */
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "token", out_token);
    return NOP_OK;
}

nop_status_t onvif_map_gotoPtzPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;
    float            speed;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    speed = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 0), 10.0);

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_goto_preset_speed(onvif_session_dev(s),
                                        onvif_session_profile(s), token, speed) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_removePtzPreset(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_remove_preset(onvif_session_dev(s),
                                    onvif_session_profile(s), token) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

/* ---- home -------------------------------------------------------------- */

nop_status_t onvif_map_setPtzHome(nop_onvif_map_backend_t *be, int ch,
                                  const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_set_home(onvif_session_dev(s), onvif_session_profile(s)) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_gotoPtzHome(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    float            speed;

    /* NOP gotoPtzHome.speed 1..10 -> ONVIF 0.1..1.0 (nop 1 == onvif 0.1). */
    speed = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 0), 10.0);

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_goto_home_speed(onvif_session_dev(s),
                                      onvif_session_profile(s), speed) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

/* ---- patrol (PresetTour) ----------------------------------------------- */

/* ONVIF PresetTour Status.State (Idle/Touring/Paused) -> NOP status (lowercase
 * idle/touring/paused per getPtzPatrols.txt; the field is read-only). */
static const char *tour_status_to_nop(const char *st)
{
    if (st && !strcmp(st, "Touring")) return "touring";
    if (st && !strcmp(st, "Paused"))  return "paused";
    return "idle";                                  /* Idle / unknown / empty */
}

/* Parse NOP spots [{presetToken,dwellTime},...] into a tour. */
static void parse_spots(const nop_json_t *arr, nop_onvif_tour_t *t)
{
    int i, n;
    t->spot_count = 0;
    if (!arr || !nop_json_is_arr(arr))
        return;
    n = nop_json_arr_size(arr);
    for (i = 0; i < n && t->spot_count < NOP_ONVIF_TOUR_MAX_SPOTS; i++) {
        const nop_json_t *sp = nop_json_arr_at(arr, i);
        nop_onvif_tour_spot_t *os = &t->spots[t->spot_count];
        if (!sp) continue;
        snprintf(os->preset_token, sizeof(os->preset_token), "%s",
                 nop_json_str(sp, "presetToken", ""));
        if (!os->preset_token[0]) {
            /* 兼容旧栏位 preset（字符串 token，不是 HAL 数字下标）。 */
            snprintf(os->preset_token, sizeof(os->preset_token), "%s",
                     nop_json_str(sp, "preset", ""));
        }
        if (!os->preset_token[0])
            continue;
        os->dwell_s = (int)nop_json_num(sp, "dwellTime",
                                        nop_json_num(sp, "dwell", 5));
        t->spot_count++;
    }
}

nop_status_t onvif_map_getPtzPatrols(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_tour_t tours[PTZ_MAX_TOURS];
    nop_json_t      *arr;
    int              n, i, k;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    n = nop_onvif_ptz_get_tours(onvif_session_dev(s), onvif_session_profile(s),
                                tours, PTZ_MAX_TOURS);
    if (n < 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_t *spots = nop_json_arr();
        nop_json_add_str(e, "token", tours[i].token);
        if (tours[i].name[0])
            nop_json_add_str(e, "name", tours[i].name);
        nop_json_add_str(e, "status", tour_status_to_nop(tours[i].status));
        nop_json_add_bool(e, "autoStart", tours[i].auto_start != 0);
        for (k = 0; k < tours[i].spot_count; k++) {
            nop_json_t *sp = nop_json_obj();
            nop_json_add_str(sp, "presetToken", tours[i].spots[k].preset_token);
            nop_json_add_int(sp, "dwellTime", tours[i].spots[k].dwell_s);
            nop_json_arr_push(spots, sp);
        }
        nop_json_add(e, "spots", spots);
        nop_json_arr_push(arr, e);
    }
    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    nop_json_add(resp->content, "patrols", arr);
    return NOP_OK;
}

/* After a tour is set autoStart=true, clear autoStart on the channel's OTHER
 * tours (setPtzPatrol.txt: at most one true per channel, last write wins). */
static void patrol_clear_other_autostart(nop_onvif_device_t *dev, const char *profile,
                                         const char *keep_token)
{
    nop_onvif_tour_t all[PTZ_MAX_TOURS];
    int na = nop_onvif_ptz_get_tours(dev, profile, all, PTZ_MAX_TOURS);
    int i;
    for (i = 0; i < na; i++) {
        if (!strcmp(all[i].token, keep_token) || !all[i].auto_start)
            continue;
        all[i].auto_start = 0;
        all[i].status[0]  = '\0';               /* status is get-only */
        nop_onvif_ptz_modify_tour(dev, profile, &all[i]);
    }
}

/* setPtzPatrol: upsert (setPtzPatrol.txt). token absent -> CreatePresetTour to
 * mint patrol_<n> then ModifyPresetTour; token present -> full-replacement
 * ModifyPresetTour (invalid_token if it doesn't exist). name/autoStart optional
 * (keep current on update). Returns content.token, or content.error on failure. */
nop_status_t onvif_map_setPtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t    *s;
    nop_onvif_device_t *dev;
    nop_onvif_tour_t    tour;
    const char         *token, *profile;
    char                created[100] = "";
    int                 has_token;

    token     = nop_json_str(req->args, "token", NULL);
    has_token = (token && token[0]);
    if (!nop_json_has(req->args, "spots") || !nop_json_is_arr(nop_json_get(req->args, "spots")))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    dev     = onvif_session_dev(s);
    profile = onvif_session_profile(s);
    if (!profile || !profile[0]) {
        onvif_session_end(be);
        return ptz_ext_fail(resp, "no_ptz_profile");
    }

    memset(&tour, 0, sizeof(tour));
    if (has_token) {
        /* 有 token：全量 ModifyPresetTour。name/autoStart 缺席则保持现值。 */
        nop_onvif_tour_t cur[PTZ_MAX_TOURS];
        int nt = nop_onvif_ptz_get_tours(dev, profile, cur, PTZ_MAX_TOURS);
        int i, found = 0;
        for (i = 0; i < nt; i++)
            if (!strcmp(cur[i].token, token)) { tour = cur[i]; found = 1; break; }
        if (!found) {
            onvif_session_end(be);
            return ptz_ext_fail(resp, "invalid_token");
        }
        snprintf(tour.token, sizeof(tour.token), "%s", token);
    } else {
        /* 无 token：CreatePresetTour 拿设备 token，再 Modify 写入 name/spots。 */
        if (nop_onvif_ptz_create_tour(dev, profile, created, sizeof(created)) != 0) {
            const char *e = ptz_ext_soap(dev);
            onvif_session_end(be);
            return ptz_ext_fail(resp, e);
        }
        snprintf(tour.token, sizeof(tour.token), "%s", created);
    }

    if (nop_json_has(req->args, "name"))
        snprintf(tour.name, sizeof(tour.name), "%s", nop_json_str(req->args, "name", ""));
    if (nop_json_has(req->args, "autoStart"))
        tour.auto_start = nop_json_bool(req->args, "autoStart", false) ? 1 : 0;
    parse_spots(nop_json_get(req->args, "spots"), &tour);
    tour.status[0] = '\0';                       /* status is get-only, never sent */

    if (nop_onvif_ptz_modify_tour(dev, profile, &tour) != 0) {
        const char *e = ptz_ext_soap(dev);
        if (!has_token && tour.token[0])
            nop_onvif_ptz_remove_tour(dev, profile, tour.token);
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    if (tour.auto_start)
        patrol_clear_other_autostart(dev, profile, tour.token);
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "token", tour.token);
    return NOP_OK;
}

/* NOP `op` is lowercase (start/stop/pause); the ONVIF OperatePresetTour
 * Operation enum is capitalized (Start/Stop/Pause) and the adapter matches it
 * case-sensitively — translate so stop/pause are not silently taken as Start. */
static const char *nop_op_to_onvif(const char *op)
{
    if (op && !strcmp(op, "stop"))  return "Stop";
    if (op && !strcmp(op, "pause")) return "Pause";
    return "Start";                            /* start / default */
}

nop_status_t onvif_map_operatePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                        const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_operate_tour(onvif_session_dev(s), onvif_session_profile(s), token,
                                   nop_op_to_onvif(nop_json_str(req->args, "op", "start"))) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

nop_status_t onvif_map_removePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    s = onvif_session_begin(be, ch);
    if (!s) return ptz_ext_fail(resp, "onvif_not_connected");
    if (nop_onvif_ptz_remove_tour(onvif_session_dev(s),
                                  onvif_session_profile(s), token) != 0) {
        const char *e = ptz_ext_soap(onvif_session_dev(s));
        onvif_session_end(be);
        return ptz_ext_fail(resp, e);
    }
    onvif_session_end(be);
    return NOP_OK;
}

#endif /* NOP_ONVIF_MAP */
