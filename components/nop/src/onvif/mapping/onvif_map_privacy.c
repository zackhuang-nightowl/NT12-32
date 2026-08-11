/**
 * @file onvif_map_privacy.c
 * @brief §7 Privacy Zone handlers — NOP macroblock grid <-> ONVIF Media2 Mask.
 *
 *   X_NightOwl_getChannelPrivacyZone -> GetMasks; each mask polygon (normalized)
 *       -> AABB -> grid cells; union -> privacyZonePoints [[col,row],...].
 *   X_NightOwl_setChannelPrivacyZone -> privacyZonePoints -> cell AABB -> ONVIF
 *       4-corner polygon; replace masks (delete existing, create one). Empty
 *       point list disables privacy (delete only), never a zero-area polygon.
 *
 * Coordinate math is onvif_coord.*; JSON<->cells is onvif_map_json.*; ONVIF wire
 * calls are nop_onvif_ext.h. Grid is the spec default 22x18.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_json.h"
#include "onvif/mapping/onvif_coord.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <string.h>

#define PRIV_W        NOP_COORD_DEFAULT_W
#define PRIV_H        NOP_COORD_DEFAULT_H
#define PRIV_MAX_MASK 16
#define PRIV_MAX_CELL (PRIV_W * PRIV_H)
/* Client draws at most 4 rectangles; each maps 1:1 to one ONVIF Mask. */
#define PRIV_MAX_ZONES 4

nop_status_t onvif_map_X_NightOwl_getChannelPrivacyZone(nop_onvif_map_backend_t *be,
                                                        int ch,
                                                        const nop_request_t *req,
                                                        nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_mask_t masks[PRIV_MAX_MASK];
    nop_json_t      *groups;
    char             myvsc[100];
    int              n, i, has_vsc;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    has_vsc = (onvif_session_vsc(s, myvsc, sizeof(myvsc)) == 0);
    n = nop_onvif_media2_get_masks(onvif_session_dev(s), masks, PRIV_MAX_MASK);
    onvif_session_end(be);
    if (n < 0)
        return NOP_ERR_IO;

    /* One ONVIF Mask == one privacy rectangle == one group of cells. Emit each
     * mask as its own [[col,row],...] group so the client keeps them distinct
     * (privacyZonePoints is an array of up to 4 rectangle groups). Only this
     * source's masks (§10 — GetMasks returns every source's on multi-source). */
    groups = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_coord_pointf_t pts[NOP_ONVIF_MASK_MAX_POINTS];
        nop_coord_cell_t   cells[PRIV_MAX_CELL];
        float xmin, xmax, ymin, ymax;
        int   c0, c1, r0, r1, r, c, ncell = 0, k;
        if (has_vsc && strcmp(masks[i].config_token, myvsc) != 0)
            continue;                        /* mask belongs to another source */
        if (!masks[i].enabled || masks[i].point_count <= 0)
            continue;
        for (k = 0; k < masks[i].point_count; k++) {
            pts[k].x = masks[i].x[k];
            pts[k].y = masks[i].y[k];
        }
        if (nop_coord_points_bounds(pts, masks[i].point_count,
                                    &xmin, &xmax, &ymin, &ymax) != 0)
            continue;
        nop_coord_aabb_to_grid(xmin, xmax, ymin, ymax, PRIV_W, PRIV_H,
                               &c0, &c1, &r0, &r1);
        for (r = r0; r <= r1; r++)
            for (c = c0; c <= c1; c++)
                if (r >= 0 && r < PRIV_H && c >= 0 && c < PRIV_W && ncell < PRIV_MAX_CELL) {
                    cells[ncell].col = c;
                    cells[ncell].row = r;
                    ncell++;
                }
        if (ncell > 0)
            nop_json_arr_push(groups, onvif_map_cells_to_json(cells, ncell));
    }

    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    nop_json_add_int(resp->content, "channel", ch);
    nop_json_add_int(resp->content, "width", PRIV_W);
    nop_json_add_int(resp->content, "height", PRIV_H);
    nop_json_add(resp->content, "privacyZonePoints", groups);
    return NOP_OK;
}

nop_status_t onvif_map_X_NightOwl_setChannelPrivacyZone(nop_onvif_map_backend_t *be,
                                                        int ch,
                                                        const nop_request_t *req,
                                                        nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_mask_t  existing[PRIV_MAX_MASK];
    const nop_json_t *groups;
    char              config_token[100];
    int               nmask, i, ngroup;
    nop_status_t      rc = NOP_OK;
    (void)resp;

    if (!nop_json_has(req->args, "privacyZonePoints"))
        return NOP_ERR_PARAM;
    groups = nop_json_get(req->args, "privacyZonePoints");
    if (!groups || !nop_json_is_arr(groups))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;

    /* Bind to THIS source's VideoSourceConfiguration; clear only this source's
     * masks (§10 — must not wipe other sources' privacy zones). */
    config_token[0] = '\0';
    onvif_session_vsc(s, config_token, sizeof(config_token));
    nmask = nop_onvif_media2_get_masks(onvif_session_dev(s), existing, PRIV_MAX_MASK);
    if (config_token[0] == '\0' && nmask > 0)   /* fallback if source VSC unresolved */
        snprintf(config_token, sizeof(config_token), "%s", existing[0].config_token);
    for (i = 0; i < nmask; i++)
        if (config_token[0] == '\0' || strcmp(existing[i].config_token, config_token) == 0)
            nop_onvif_media2_delete_mask(onvif_session_dev(s), existing[i].token);

    if (config_token[0] == '\0') {
        /* No config token -> cannot create; report failure rather than a silent
         * "privacy cleared" (the delete-all above already ran). */
        onvif_session_end(be);
        return NOP_ERR_IO;
    }

    /* Each group is one rectangle -> one axis-aligned Mask. Empty list == all
     * groups removed == privacy off (handled by the delete-all above). */
    ngroup = nop_json_arr_size(groups);
    if (ngroup > PRIV_MAX_ZONES)
        ngroup = PRIV_MAX_ZONES;
    for (i = 0; i < ngroup; i++) {
        nop_coord_cell_t   cells[PRIV_MAX_CELL];
        nop_coord_pointf_t poly[4];
        float xs[4], ys[4];
        int   ncell, c0, c1, r0, r1, k;
        ncell = onvif_map_json_to_cells(nop_json_arr_at(groups, i), cells, PRIV_MAX_CELL);
        if (ncell <= 0 ||
            nop_coord_cells_bounds(cells, ncell, &c0, &c1, &r0, &r1) != 0)
            continue;
        nop_coord_grid_to_aabb(c0, c1, r0, r1, PRIV_W, PRIV_H, poly);
        for (k = 0; k < 4; k++) { xs[k] = poly[k].x; ys[k] = poly[k].y; }
        if (nop_onvif_media2_create_mask(onvif_session_dev(s), config_token,
                                         xs, ys, 4, 1, "Color") != 0)
            rc = NOP_ERR_IO;
    }

    onvif_session_end(be);
    return rc;
}

#endif /* NOP_ONVIF_MAP */
