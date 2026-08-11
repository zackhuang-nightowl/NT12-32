/**
 * @file onvif_map_motion.c
 * @brief §8 Motion handlers — NOP X_NightOwl_get/setChannelTriggerActivityZone
 *        <-> ONVIF CellMotionDetector rule (ActiveCells bitmap + Sensitivity).
 *
 *   activityZonePoints [[col,row]] <-> ActiveCells bitmap (row*cols+col bit).
 *   sensitivity level string <-> ONVIF Sensitivity 0..100. Grid = width x height
 *   (default 22x18). Values otherwise pass straight through.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_map_json.h"
#include "onvif/mapping/onvif_coord.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <string.h>

#define MOT_MAX_CELLS 4096
#define MOT_MAX_RULES 16

static int bit_get(const unsigned char *b, int idx) { return (b[idx >> 3] >> (7 - (idx & 7))) & 1; }
static void bit_set(unsigned char *b, int idx) { b[idx >> 3] |= (unsigned char)(0x80 >> (idx & 7)); }

/* NOP sensitivity level string <-> ONVIF 0..100. */
static int level_to_sens(const char *lvl)
{
    if (lvl && !strcmp(lvl, "low"))  return 30;
    if (lvl && !strcmp(lvl, "high")) return 80;
    return 50;   /* "middle" / default */
}
static const char *sens_to_level(int s)
{
    if (s < 40) return "low";
    if (s < 70) return "middle";
    return "high";
}

nop_status_t onvif_map_X_NightOwl_getChannelTriggerActivityZone(nop_onvif_map_backend_t *be,
                                                                int ch,
                                                                const nop_request_t *req,
                                                                nop_response_t *resp)
{
    onvif_session_t      *s;
    nop_onvif_cellmotion_t cm;
    nop_coord_cell_t       cells[MOT_MAX_CELLS];
    char                   cfg[100];
    int                    r, c, ncell = 0, rc;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    memset(&cm, 0, sizeof(cm));
    cm.columns = NOP_COORD_DEFAULT_W;
    cm.rows    = NOP_COORD_DEFAULT_H;
    rc = nop_onvif_analytics_get_cellmotion(onvif_session_dev(s), cfg, &cm);
    onvif_session_end(be);
    if (rc < 0) return NOP_ERR_IO;

    for (r = 0; r < cm.rows; r++)
        for (c = 0; c < cm.columns; c++)
            if (bit_get(cm.active, r * cm.columns + c) && ncell < MOT_MAX_CELLS) {
                cells[ncell].col = c;
                cells[ncell].row = r;
                ncell++;
            }

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    nop_json_add_int(resp->content, "channel", ch);
    nop_json_add_int(resp->content, "width", cm.columns);
    nop_json_add_int(resp->content, "height", cm.rows);
    nop_json_add_str(resp->content, "sensitivity", sens_to_level(cm.sensitivity));
    nop_json_add(resp->content, "activityZonePoints", onvif_map_cells_to_json(cells, ncell));
    return NOP_OK;
}

nop_status_t onvif_map_X_NightOwl_setChannelTriggerActivityZone(nop_onvif_map_backend_t *be,
                                                                int ch,
                                                                const nop_request_t *req,
                                                                nop_response_t *resp)
{
    onvif_session_t       *s;
    nop_onvif_cellmotion_t cm;
    nop_onvif_rule_t       existing[MOT_MAX_RULES];
    nop_coord_cell_t       cells[MOT_MAX_CELLS];
    char                   cfg[100];
    int                    ncell, n, i, w, h;
    nop_status_t           rc = NOP_OK;

    if (!nop_json_has(req->args, "activityZonePoints"))
        return NOP_ERR_PARAM;
    w = (int)nop_json_num(req->args, "width", NOP_COORD_DEFAULT_W);
    h = (int)nop_json_num(req->args, "height", NOP_COORD_DEFAULT_H);
    if (w <= 0 || h <= 0 || w * h > NOP_ONVIF_CELLS_MAX_BITS)
        return NOP_ERR_PARAM;

    memset(&cm, 0, sizeof(cm));
    cm.columns     = w;
    cm.rows        = h;
    cm.sensitivity = level_to_sens(nop_json_str(req->args, "sensitivity", "middle"));
    cm.min_count   = 1;
    ncell = onvif_map_json_to_cells(nop_json_get(req->args, "activityZonePoints"),
                                    cells, MOT_MAX_CELLS);
    for (i = 0; i < ncell; i++) {
        int c = cells[i].col, r = cells[i].row;
        if (c >= 0 && c < w && r >= 0 && r < h)
            bit_set(cm.active, r * w + c);
    }

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    /* Replace: drop existing CellMotion rules, then create the new one. */
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "CellMotion",
                                      existing, MOT_MAX_RULES);
    for (i = 0; i < n; i++)
        nop_onvif_analytics_delete_rule(onvif_session_dev(s), cfg, existing[i].name);
    if (nop_onvif_analytics_set_cellmotion(onvif_session_dev(s), cfg, &cm) != 0)
        rc = NOP_ERR_IO;
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set motion failed");
    return rc;
}

#endif /* NOP_ONVIF_MAP */
