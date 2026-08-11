/**
 * @file onvif_map_osd.c
 * @brief §5 OSD handlers — NOP X_NightOwl_get/setOSD <-> ONVIF Media2 OSD.
 *
 *   osdToken <-> OSD identity (NOPMappingONVIF.md §5):
 *     OSD_Name     <-> Text/Plain      OSD_DateTime <-> Text/DateAndTime
 *     OSD_WaterMark<-> Image
 *   positionType  <-> OSD position (TopLeft->UpperLeft, ...; Custom passes
 *     positionX/Y straight through as ONVIF normalized [-1,1]; Top/BottomMiddle
 *     have no ONVIF preset so map to Custom at x=0). enable=false -> DeleteOSD.
 *
 * Values are passed to the ONVIF ABI directly (no coordinate transform needed:
 * OSD custom coords are already ONVIF-normalized per the spec).
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <string.h>

#define OSD_MAX 16

/* osdToken -> OSD kind. @return 1 if recognized. */
static int token_to_kind(const char *token, int *is_image, int *text_type)
{
    if (!token) return 0;
    if (!strcmp(token, "OSD_WaterMark")) { *is_image = 1; *text_type = NOP_OSD_TEXT_PLAIN; return 1; }
    if (!strcmp(token, "OSD_DateTime"))  { *is_image = 0; *text_type = NOP_OSD_TEXT_DATETIME; return 1; }
    if (!strcmp(token, "OSD_Name"))      { *is_image = 0; *text_type = NOP_OSD_TEXT_PLAIN; return 1; }
    return 0;
}

/* OSD kind -> osdToken. */
static const char *kind_to_token(int is_image, int text_type)
{
    if (is_image) return "OSD_WaterMark";
    if (text_type != NOP_OSD_TEXT_PLAIN) return "OSD_DateTime";  /* Date/Time/DateAndTime */
    return "OSD_Name";
}

/* NOP positionType name -> ONVIF pos_type (+ custom x/y). */
static void name_to_pos(const char *name, int *pos_type, float *x, float *y)
{
    *pos_type = NOP_OSD_POS_UPPER_RIGHT; *x = 0.0f; *y = 0.0f;
    if (!name) return;
    if      (!strcmp(name, "TopLeft"))      *pos_type = NOP_OSD_POS_UPPER_LEFT;
    else if (!strcmp(name, "TopRight"))     *pos_type = NOP_OSD_POS_UPPER_RIGHT;
    else if (!strcmp(name, "BottomLeft"))   *pos_type = NOP_OSD_POS_LOWER_LEFT;
    else if (!strcmp(name, "BottomRight"))  *pos_type = NOP_OSD_POS_LOWER_RIGHT;
    else if (!strcmp(name, "TopMiddle"))    { *pos_type = NOP_OSD_POS_CUSTOM; *x = 0.0f; *y =  0.9f; }
    else if (!strcmp(name, "BottomMiddle")) { *pos_type = NOP_OSD_POS_CUSTOM; *x = 0.0f; *y = -0.9f; }
    else if (!strcmp(name, "Custom"))       *pos_type = NOP_OSD_POS_CUSTOM;
}

/* ONVIF pos_type -> NOP positionType name. */
static const char *pos_to_name(int pos_type)
{
    switch (pos_type) {
    case NOP_OSD_POS_UPPER_LEFT:  return "TopLeft";
    case NOP_OSD_POS_UPPER_RIGHT: return "TopRight";
    case NOP_OSD_POS_LOWER_LEFT:  return "BottomLeft";
    case NOP_OSD_POS_LOWER_RIGHT: return "BottomRight";
    default:                      return "Custom";
    }
}

nop_status_t onvif_map_X_NightOwl_getOSD(nop_onvif_map_backend_t *be, int ch,
                                         const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_osd_t  osds[OSD_MAX];
    nop_json_t      *arr;
    int              n, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    n = nop_onvif_media2_get_osds(onvif_session_dev(s), osds, OSD_MAX);
    onvif_session_end(be);
    if (n < 0)
        return NOP_ERR_IO;

    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_add_str(e, "osdToken", kind_to_token(osds[i].is_image, osds[i].text_type));
        nop_json_add_bool(e, "enable", true);
        nop_json_add_str(e, "positionType", pos_to_name(osds[i].pos_type));
        if (osds[i].pos_type == NOP_OSD_POS_CUSTOM) {
            nop_json_add_int(e, "positionX", osds[i].pos_x);
            nop_json_add_int(e, "positionY", osds[i].pos_y);
        }
        nop_json_arr_push(arr, e);
    }
    nop_json_add(resp->content, "OSDConfigs", arr);
    return NOP_OK;
}

nop_status_t onvif_map_X_NightOwl_setOSD(nop_onvif_map_backend_t *be, int ch,
                                         const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_osd_t  osds[OSD_MAX], target;
    const char      *token, *pos_name;
    char             myvsc[100];
    int              is_image, text_type, n, i, found = -1, has_vsc;
    int              enable;
    nop_status_t     rc = NOP_OK;

    token = nop_json_str(req->args, "osdToken", NULL);
    if (!token_to_kind(token, &is_image, &text_type))
        return NOP_ERR_PARAM;
    enable   = nop_json_bool(req->args, "enable", true) ? 1 : 0;
    pos_name = nop_json_str(req->args, "positionType", "TopRight");

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;

    /* This source's VSC token: restricts match/create to THIS source's OSDs
     * (§10 — GetOSDs returns every source's OSDs on a multi-source device). */
    has_vsc = (onvif_session_vsc(s, myvsc, sizeof(myvsc)) == 0);

    n = nop_onvif_media2_get_osds(onvif_session_dev(s), osds, OSD_MAX);
    if (n < 0) { onvif_session_end(be); return NOP_ERR_IO; }

    /* Match the existing OSD carrying this osdToken's identity on THIS source. */
    for (i = 0; i < n; i++) {
        int same;
        if (has_vsc && strcmp(osds[i].config_token, myvsc) != 0)
            continue;                        /* OSD belongs to another source */
        if (is_image)
            same = osds[i].is_image;
        else if (text_type == NOP_OSD_TEXT_PLAIN)
            same = !osds[i].is_image && osds[i].text_type == NOP_OSD_TEXT_PLAIN;
        else
            same = !osds[i].is_image && osds[i].text_type != NOP_OSD_TEXT_PLAIN;
        if (same) { found = i; break; }
    }

    if (!enable) {
        /* Disable == remove the OSD (ONVIF has no per-OSD enable flag). */
        if (found >= 0 &&
            nop_onvif_media2_delete_osd(onvif_session_dev(s), osds[found].token) != 0)
            rc = NOP_ERR_IO;
        onvif_session_end(be);
        return rc;
    }

    memset(&target, 0, sizeof(target));
    target.is_image  = is_image;
    target.text_type = text_type;
    name_to_pos(pos_name, &target.pos_type, &target.pos_x, &target.pos_y);
    if (target.pos_type == NOP_OSD_POS_CUSTOM && !strcmp(pos_name, "Custom")) {
        target.pos_x = (float)nop_json_num(req->args, "positionX", 0);
        target.pos_y = (float)nop_json_num(req->args, "positionY", 0);
    }

    if (found >= 0) {
        /* Update in place: keep token/config/content, change position. */
        snprintf(target.token, sizeof(target.token), "%s", osds[found].token);
        snprintf(target.config_token, sizeof(target.config_token), "%s", osds[found].config_token);
        snprintf(target.plain_text, sizeof(target.plain_text), "%s", osds[found].plain_text);
        snprintf(target.img_path, sizeof(target.img_path), "%s", osds[found].img_path);
        rc = (nop_onvif_media2_set_osd(onvif_session_dev(s), &target) == 0) ? NOP_OK : NOP_ERR_IO;
    } else {
        /* Create new on THIS source's VSC (fall back to an existing OSD's config
         * token only when the source VSC is unresolved). */
        if (has_vsc)
            snprintf(target.config_token, sizeof(target.config_token), "%s", myvsc);
        else if (n > 0)
            snprintf(target.config_token, sizeof(target.config_token), "%s", osds[0].config_token);
        rc = (nop_onvif_media2_create_osd(onvif_session_dev(s), &target) == 0) ? NOP_OK : NOP_ERR_IO;
    }

    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set osd failed");
    return rc;
}

#endif /* NOP_ONVIF_MAP */
