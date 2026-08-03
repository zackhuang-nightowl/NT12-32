/**
 * @file onvif_map_ptz_ext.c
 * @brief §2 PTZ advanced handlers — focus / presets / home. One handler per NOP
 *        interface (NOPMappingONVIF.md §2), complementing onvif_map_ptz.c.
 *
 *   ptzFocusByStep/Stop -> Imaging continuous focus Move/Stop
 *   getPtzPresets/setPtzPreset/gotoPtzPreset/removePtzPreset -> PTZ preset ops
 *   setPtzHome/gotoPtzHome -> SetHomePosition / GotoHomePosition
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

/* ---- focus (Imaging Move) ---------------------------------------------- */

nop_status_t onvif_map_ptzFocusByStep(nop_onvif_map_backend_t *be, int ch,
                                      const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *dir;
    char             vs[100];
    float            speed;
    nop_status_t     rc;
    (void)resp;

    dir = nop_json_str(req->args, "direction", NULL);
    if (!dir)
        return NOP_ERR_PARAM;
    speed = onvif_map_level_to_unit(nop_json_num(req->args, "step", 1), 10.0);
    if (strcmp(dir, "focusIn") != 0)
        speed = -speed;   /* focusOut / near */

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (nop_onvif_media2_video_source_token(onvif_session_dev(s), vs, sizeof(vs)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    rc = onvif_map_rc(nop_onvif_img_focus_move(onvif_session_dev(s), vs, speed));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_ptzFocusStop(nop_onvif_map_backend_t *be, int ch,
                                    const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    char             vs[100];
    nop_status_t     rc;
    (void)req; (void)resp;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (nop_onvif_media2_video_source_token(onvif_session_dev(s), vs, sizeof(vs)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    rc = onvif_map_rc(nop_onvif_img_focus_stop(onvif_session_dev(s), vs));
    onvif_session_end(be);
    return rc;
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
    if (!s) return NOP_ERR_IO;
    n = nop_onvif_ptz_get_presets(onvif_session_dev(s), onvif_session_profile(s),
                                  presets, PTZ_MAX_PRESETS);
    onvif_session_end(be);
    if (n < 0) return NOP_ERR_IO;

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
    nop_status_t     rc;

    token = nop_json_str(req->args, "token", "");
    name  = nop_json_str(req->args, "name", "");

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_set_preset_ex(onvif_session_dev(s),
                                                  onvif_session_profile(s),
                                                  token, name, out_token, sizeof(out_token)));
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "token", out_token);
    return rc;
}

nop_status_t onvif_map_gotoPtzPreset(nop_onvif_map_backend_t *be, int ch,
                                     const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;
    float            speed;
    nop_status_t     rc;
    (void)resp;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    speed = onvif_map_level_to_unit(nop_json_num(req->args, "speed", 0), 10.0);

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_goto_preset_speed(onvif_session_dev(s),
                                                      onvif_session_profile(s), token, speed));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_removePtzPreset(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;
    nop_status_t     rc;
    (void)resp;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_remove_preset(onvif_session_dev(s),
                                                  onvif_session_profile(s), token));
    onvif_session_end(be);
    return rc;
}

/* ---- home -------------------------------------------------------------- */

nop_status_t onvif_map_setPtzHome(nop_onvif_map_backend_t *be, int ch,
                                  const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_status_t     rc;
    (void)req; (void)resp;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_set_home(onvif_session_dev(s), onvif_session_profile(s)));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_gotoPtzHome(nop_onvif_map_backend_t *be, int ch,
                                   const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_status_t     rc;
    (void)req; (void)resp;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_goto_home(onvif_session_dev(s), onvif_session_profile(s)));
    onvif_session_end(be);
    return rc;
}

/* ---- patrol (PresetTour) ----------------------------------------------- */

#define PTZ_MAX_TOURS 16

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
        os->dwell_s = (int)nop_json_num(sp, "dwellTime", 5);
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
    if (!s) return NOP_ERR_IO;
    n = nop_onvif_ptz_get_tours(onvif_session_dev(s), onvif_session_profile(s),
                                tours, PTZ_MAX_TOURS);
    onvif_session_end(be);
    if (n < 0) return NOP_ERR_IO;

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_t *spots = nop_json_arr();
        nop_json_add_str(e, "token", tours[i].token);
        nop_json_add_str(e, "name", tours[i].name);
        nop_json_add_str(e, "status", tours[i].status);
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

nop_status_t onvif_map_createPtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_tour_t tour;
    char             token[100] = "";
    nop_status_t     rc = NOP_OK;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    /* ONVIF: CreatePresetTour to obtain a token, then ModifyPresetTour body. */
    if (nop_onvif_ptz_create_tour(onvif_session_dev(s), onvif_session_profile(s),
                                  token, sizeof(token)) != 0) {
        onvif_session_end(be);
        return NOP_ERR_IO;
    }
    memset(&tour, 0, sizeof(tour));
    snprintf(tour.token, sizeof(tour.token), "%s", token);
    snprintf(tour.name, sizeof(tour.name), "%s", nop_json_str(req->args, "name", ""));
    tour.auto_start = nop_json_bool(req->args, "autoStart", false) ? 1 : 0;
    parse_spots(nop_json_get(req->args, "spots"), &tour);
    if (nop_onvif_ptz_modify_tour(onvif_session_dev(s), onvif_session_profile(s), &tour) != 0)
        rc = NOP_ERR_IO;
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "token", token);
    return rc;
}

nop_status_t onvif_map_modifyPtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_tour_t tour;
    const char      *token;
    nop_status_t     rc;
    (void)resp;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    memset(&tour, 0, sizeof(tour));
    snprintf(tour.token, sizeof(tour.token), "%s", token);
    snprintf(tour.name, sizeof(tour.name), "%s", nop_json_str(req->args, "name", ""));
    tour.auto_start = nop_json_bool(req->args, "autoStart", false) ? 1 : 0;
    parse_spots(nop_json_get(req->args, "spots"), &tour);

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_modify_tour(onvif_session_dev(s),
                                                onvif_session_profile(s), &tour));
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif modify patrol failed");
    return rc;
}

nop_status_t onvif_map_operatePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                        const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token, *op;
    nop_status_t     rc;
    (void)resp;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    op = nop_json_str(req->args, "op", "Start");

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_operate_tour(onvif_session_dev(s),
                                                 onvif_session_profile(s), token, op));
    onvif_session_end(be);
    return rc;
}

nop_status_t onvif_map_removePtzPatrol(nop_onvif_map_backend_t *be, int ch,
                                       const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    const char      *token;
    nop_status_t     rc;
    (void)resp;

    token = nop_json_str(req->args, "token", NULL);
    if (!token)
        return NOP_ERR_PARAM;
    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    rc = onvif_map_rc(nop_onvif_ptz_remove_tour(onvif_session_dev(s),
                                                onvif_session_profile(s), token));
    onvif_session_end(be);
    return rc;
}

#endif /* NOP_ONVIF_MAP */
