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

nop_status_t onvif_map_X_NightOwl_getChannelPrivacyZone(nop_onvif_map_backend_t *be,
                                                        int ch,
                                                        const nop_request_t *req,
                                                        nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_mask_t masks[PRIV_MAX_MASK];
    unsigned char    grid[PRIV_H][PRIV_W];
    nop_coord_cell_t cells[PRIV_MAX_CELL];
    int              n, i, r, c, ncell = 0;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    n = nop_onvif_media2_get_masks(onvif_session_dev(s), masks, PRIV_MAX_MASK);
    onvif_session_end(be);
    if (n < 0)
        return NOP_ERR_IO;

    /* Union every mask's cell rectangle into one 22x18 grid (dedups overlaps). */
    memset(grid, 0, sizeof(grid));
    for (i = 0; i < n; i++) {
        float xmin, xmax, ymin, ymax;
        int   c0, c1, r0, r1;
        if (!masks[i].enabled || masks[i].point_count <= 0)
            continue;
        /* masks[i].x/y are an onvif_coord_pointf laid out as parallel arrays. */
        {
            nop_coord_pointf_t pts[NOP_ONVIF_MASK_MAX_POINTS];
            int k;
            for (k = 0; k < masks[i].point_count; k++) {
                pts[k].x = masks[i].x[k];
                pts[k].y = masks[i].y[k];
            }
            if (nop_coord_points_bounds(pts, masks[i].point_count,
                                        &xmin, &xmax, &ymin, &ymax) != 0)
                continue;
        }
        nop_coord_aabb_to_grid(xmin, xmax, ymin, ymax, PRIV_W, PRIV_H,
                               &c0, &c1, &r0, &r1);
        for (r = r0; r <= r1; r++)
            for (c = c0; c <= c1; c++)
                if (r >= 0 && r < PRIV_H && c >= 0 && c < PRIV_W)
                    grid[r][c] = 1;
    }

    /* Emit set cells row-major as [[col,row],...] (matches the spec ordering). */
    for (r = 0; r < PRIV_H; r++)
        for (c = 0; c < PRIV_W; c++)
            if (grid[r][c] && ncell < PRIV_MAX_CELL) {
                cells[ncell].col = c;
                cells[ncell].row = r;
                ncell++;
            }

    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    nop_json_add_int(resp->content, "channel", ch);
    nop_json_add_int(resp->content, "width", PRIV_W);
    nop_json_add_int(resp->content, "height", PRIV_H);
    nop_json_add(resp->content, "privacyZonePoints",
                 onvif_map_cells_to_json(cells, ncell));
    return NOP_OK;
}

nop_status_t onvif_map_X_NightOwl_setChannelPrivacyZone(nop_onvif_map_backend_t *be,
                                                        int ch,
                                                        const nop_request_t *req,
                                                        nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_mask_t existing[PRIV_MAX_MASK];
    nop_coord_cell_t cells[PRIV_MAX_CELL];
    char             config_token[100];
    int              ncell, nmask, i, c0, c1, r0, r1;
    nop_status_t     rc = NOP_OK;
    (void)resp;

    if (!nop_json_has(req->args, "privacyZonePoints"))
        return NOP_ERR_PARAM;
    ncell = onvif_map_json_to_cells(nop_json_get(req->args, "privacyZonePoints"),
                                    cells, PRIV_MAX_CELL);

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;

    /* Reuse an existing mask's ConfigurationToken; else resolve from the
     * video-source config. Then clear all masks before (re)creating. */
    config_token[0] = '\0';
    nmask = nop_onvif_media2_get_masks(onvif_session_dev(s), existing, PRIV_MAX_MASK);
    if (nmask > 0)
        snprintf(config_token, sizeof(config_token), "%s", existing[0].config_token);
    else
        nop_onvif_media2_video_source_token(onvif_session_dev(s),
                                            config_token, sizeof(config_token));
    for (i = 0; i < nmask; i++)
        nop_onvif_media2_delete_mask(onvif_session_dev(s), existing[i].token);

    /* Non-empty selection -> one axis-aligned mask. Empty -> privacy off. */
    if (ncell > 0 && config_token[0] != '\0' &&
        nop_coord_cells_bounds(cells, ncell, &c0, &c1, &r0, &r1) == 0) {
        nop_coord_pointf_t poly[4];
        float xs[4], ys[4];
        int   k;
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
