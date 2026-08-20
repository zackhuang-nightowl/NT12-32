/**
 * @file onvif_map_motion.c
 * @brief §8 Motion handlers — NOP X_NightOwl_getChannelActivityZoneTypes
 *        / get/setChannelTriggerActivityZone <-> ONVIF CellMotionDetector.
 *
 *   Types: GetRules Type 含 Motion（或 GetSupportedRules CellMotion）→ triggers=["pixelChange"].
 *   activityZonePoints [[col,row]] <-> ActiveCells bitmask (PackBits+base64).
 *   SET: ModifyRules 已有 CellMotion（保留 Name / Enabled / delays）；无规则才 Create。
 *   sensitivity level string <-> MinCount. Grid = width x height (default 22x18).
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

/* NOP sensitivity level <-> ONVIF CellMotion MinCount (spec §8: sensitivity ↔
 * MinCount). MinCount is the minimum number of adjacent active cells needed to
 * fire, so it is INVERSE to sensitivity: high sensitivity == few cells needed.
 * (The exact curve is "待补充" in the spec; this monotone mapping is the
 * agreed placeholder.) The camera's separate Sensitivity(0..100) is left at its
 * current value. */
static int level_to_mincount(const char *lvl)
{
    if (lvl && !strcmp(lvl, "high")) return 1;   /* most sensitive */
    if (lvl && !strcmp(lvl, "low"))  return 3;   /* least sensitive */
    return 2;                                     /* "middle" / default */
}
static const char *mincount_to_level(int mc)
{
    if (mc <= 1) return "high";
    if (mc >= 3) return "low";
    return "middle";
}

/* GetRules Type 含 Motion/CellMotion，或 GetSupportedRules 宣称 CellMotionDetector。 */
static int motion_supported(nop_onvif_device_t *dev, const char *cfg)
{
    nop_onvif_rule_t    rules[MOT_MAX_RULES];
    nop_onvif_ai_caps_t ai;
    int n;

    n = nop_onvif_analytics_get_rules(dev, cfg, "Motion", rules, MOT_MAX_RULES);
    if (n > 0)
        return 1;
    memset(&ai, 0, sizeof(ai));
    if (nop_onvif_device_cached_ai(dev, cfg, &ai) == 0 && ai.motion_present)
        return 1;
    if (nop_onvif_analytics_get_ai_caps(dev, cfg, &ai) == 0 && ai.motion_present)
        return 1;
    return 0;
}

nop_status_t onvif_map_X_NightOwl_getChannelActivityZoneTypes(nop_onvif_map_backend_t *be,
                                                              int ch,
                                                              const nop_request_t *req,
                                                              nop_response_t *resp)
{
    onvif_session_t *s;
    char             cfg[100];
    int              has_motion;
    nop_json_t      *triggers;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be);
        return NOP_ERR_NOTIMPL;
    }
    has_motion = motion_supported(onvif_session_dev(s), cfg);
    onvif_session_end(be);
    if (!has_motion)
        return NOP_ERR_NOTIMPL;

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    triggers = nop_json_arr();
    nop_json_arr_push_str(triggers, "pixelChange");
    nop_json_add(resp->content, "triggers", triggers);
    return NOP_OK;
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
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }
    memset(&cm, 0, sizeof(cm));
    cm.columns = NOP_COORD_DEFAULT_W;
    cm.rows    = NOP_COORD_DEFAULT_H;
    rc = nop_onvif_analytics_get_cellmotion(onvif_session_dev(s), cfg, &cm);
    onvif_session_end(be);
    if (rc < 0) return ONVIF_MAP_FAIL;

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
    nop_json_add_str(resp->content, "sensitivity", mincount_to_level(cm.min_count));
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
    nop_coord_cell_t       cells[MOT_MAX_CELLS];
    char                   cfg[100];
    int                    ncell, i, w, h;
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
    cm.min_count   = level_to_mincount(nop_json_str(req->args, "sensitivity", "middle"));
    ncell = onvif_map_json_to_cells(nop_json_get(req->args, "activityZonePoints"),
                                    cells, MOT_MAX_CELLS);
    for (i = 0; i < ncell; i++) {
        int c = cells[i].col, r = cells[i].row;
        if (c >= 0 && c < w && r >= 0 && r < h)
            bit_set(cm.active, r * w + c);
    }

    s = onvif_session_begin(be, ch);
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }
    /* Preserve the camera's current Sensitivity(0..100): NOP sensitivity
     * string maps to MinCount; ActiveCells is the grid. Adapter ModifyRules
     * keeps the existing CellMotion Name / Enabled / delays. */
    {
        nop_onvif_cellmotion_t cur;
        memset(&cur, 0, sizeof(cur));
        cur.columns = w; cur.rows = h;
        if (nop_onvif_analytics_get_cellmotion(onvif_session_dev(s), cfg, &cur) >= 0)
            cm.sensitivity = cur.sensitivity;
    }
    if (nop_onvif_analytics_set_cellmotion(onvif_session_dev(s), cfg, &cm) != 0)
        rc = ONVIF_MAP_FAIL;
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set motion failed");
    return rc;
}

#endif /* NOP_ONVIF_MAP */
