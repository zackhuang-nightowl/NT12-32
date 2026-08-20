/**
 * @file onvif_map_privacy.c
 * @brief §7 Privacy Zone handlers — NOP macroblock grid <-> ONVIF Media2 Mask.
 *
 *   X_NightOwl_getChannelPrivacyZone -> GetMasks; each mask polygon (normalized)
 *       -> AABB -> grid cells; union -> privacyZonePoints [[col,row],...].
 *       Polygon 各点均为 (-1,1) → 该组不出现在 privacyZonePoints（NOP []）。
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

/* privacyZonePoints has two documented shapes: a list of rectangle groups
 * ([[[col,row],...],...], 3-level) or a single flat rectangle ([[col,row],...],
 * 2-level; X_NightOwl_setChannelPrivacyZone.txt Request 2). Detect the flat form
 * by checking whether the first element's first element is a scalar (a point),
 * not an array (a point list). */
static int privacy_points_flat(const nop_json_t *groups)
{
    const nop_json_t *g0 = nop_json_arr_at(groups, 0);
    const nop_json_t *e0;
    if (!g0 || !nop_json_is_arr(g0))
        return 0;
    e0 = nop_json_arr_at(g0, 0);
    return (e0 && !nop_json_is_arr(e0)) ? 1 : 0;   /* [col,row] pair -> flat */
}

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
        return ONVIF_MAP_FAIL;
    has_vsc = (onvif_session_vsc(s, myvsc, sizeof(myvsc)) == 0);
    n = nop_onvif_media2_get_masks(onvif_session_dev(s), masks, PRIV_MAX_MASK,
                                  has_vsc ? myvsc : NULL);
    onvif_session_end(be);
    if (n < 0)
        return ONVIF_MAP_FAIL;

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
        if (nop_json_arr_size(groups) >= PRIV_MAX_ZONES)
            break;                           /* contract caps privacyZonePoints at 4 */
        if (has_vsc && strcmp(masks[i].config_token, myvsc) != 0)
            continue;                        /* mask belongs to another source */
        if (!masks[i].enabled || masks[i].point_count <= 0)
            continue;
        for (k = 0; k < masks[i].point_count; k++) {
            pts[k].x = masks[i].x[k];
            pts[k].y = masks[i].y[k];
        }
        if (nop_coord_points_unconfigured(pts, masks[i].point_count))
            continue;
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
        return ONVIF_MAP_FAIL;

    /* Bind to THIS source's VideoSourceConfiguration; clear only this source's
     * masks (§10 — must not wipe other sources' privacy zones). */
    config_token[0] = '\0';
    onvif_session_vsc(s, config_token, sizeof(config_token));
    nmask = nop_onvif_device_cached_masks(onvif_session_dev(s),
                                          config_token[0] ? config_token
                                                          : onvif_session_bound_source(s),
                                          existing, PRIV_MAX_MASK);
    if (nmask < 0)
        nmask = nop_onvif_media2_get_masks(onvif_session_dev(s), existing, PRIV_MAX_MASK,
                                           config_token[0] ? config_token : NULL);
    if (config_token[0] == '\0' && nmask > 0)   /* fallback if source VSC unresolved */
        snprintf(config_token, sizeof(config_token), "%s", existing[0].config_token);
    for (i = 0; i < nmask; i++)
        if (config_token[0] == '\0' || strcmp(existing[i].config_token, config_token) == 0)
            nop_onvif_media2_delete_mask(onvif_session_dev(s), existing[i].token);

    if (config_token[0] == '\0') {
        /* No config token -> cannot create; report failure rather than a silent
         * "privacy cleared" (the delete-all above already ran). */
        onvif_session_end(be);
        return ONVIF_MAP_FAIL;
    }

    /* Each group is one rectangle -> one axis-aligned Mask. Empty list == all
     * groups removed == privacy off (handled by the delete-all above). The flat
     * single-rectangle form (Request 2) is one group == the whole point list. */
    {
        int flat = privacy_points_flat(groups);
        ngroup = flat ? 1 : nop_json_arr_size(groups);
        if (ngroup > PRIV_MAX_ZONES)
            ngroup = PRIV_MAX_ZONES;
        nop_onvif_mask_t created[PRIV_MAX_ZONES];
        int ncreated = 0;
        for (i = 0; i < ngroup; i++) {
            const nop_json_t  *grp = flat ? groups : nop_json_arr_at(groups, i);
            nop_coord_cell_t   cells[PRIV_MAX_CELL];
            nop_coord_pointf_t poly[4];
            float xs[4], ys[4];
            char  tok[100];
            int   ncell, c0, c1, r0, r1, k;
            ncell = onvif_map_json_to_cells(grp, cells, PRIV_MAX_CELL);
            if (ncell <= 0 ||
                nop_coord_cells_bounds(cells, ncell, &c0, &c1, &r0, &r1) != 0)
                continue;
            nop_coord_grid_to_aabb(c0, c1, r0, r1, PRIV_W, PRIV_H, poly);
            for (k = 0; k < 4; k++) { xs[k] = poly[k].x; ys[k] = poly[k].y; }
            tok[0] = '\0';
            if (nop_onvif_media2_create_mask(onvif_session_dev(s), config_token,
                                             xs, ys, 4, 1, "Color",
                                             tok, sizeof(tok)) != 0) {
                rc = ONVIF_MAP_FAIL;
                continue;
            }
            if (ncreated < PRIV_MAX_ZONES) {
                nop_onvif_mask_t *m = &created[ncreated++];
                memset(m, 0, sizeof(*m));
                snprintf(m->token, sizeof(m->token), "%s", tok);
                snprintf(m->config_token, sizeof(m->config_token), "%s", config_token);
                m->enabled = 1;
                m->point_count = 4;
                for (k = 0; k < 4; k++) { m->x[k] = xs[k]; m->y[k] = ys[k]; }
            }
        }
        nop_onvif_device_mask_cache_replace(onvif_session_dev(s), config_token,
                                            created, ncreated);
    }

    onvif_session_end(be);
    return rc;
}

#endif /* NOP_ONVIF_MAP */
