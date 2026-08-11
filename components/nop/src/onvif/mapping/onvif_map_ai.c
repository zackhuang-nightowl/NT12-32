/**
 * @file onvif_map_ai.c
 * @brief §9 Smart AI handlers — NOP AI_*ChannelLineCrossDetect /
 *        *FieldIntrusionDetect <-> ONVIF Analytics Line/Field rules.
 *
 *   get: analytics GetRules(Line|Field) -> rules[] with line/area points
 *        (ONVIF normalized -> NOP thousandths), direction, triggers.
 *   set: replace all rules of that type -> DeleteRules(existing) + CreateRules
 *        per enabled rule (NOP thousandths -> ONVIF normalized).
 *
 * ClassFilter <-> triggers uses the single table below (also used by §8). The
 * geometry transform is onvif_coord.*; values otherwise pass straight through.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "onvif/mapping/onvif_coord.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <string.h>
#include <stdio.h>

#define AI_MAX_RULES 16

/* ---- ClassFilter <-> NOP trigger table (shared by §8/§9) ---------------- */
static const struct { const char *onvif; const char *nop; } k_class_map[] = {
    { "Human",   "human"   },
    { "Vehicle", "vehicle" },
    { "Face",    "face"    },
    { "Animal",  "animal"  },
    { "Person",  "human"   },
};

static const char *nop_trigger_to_class(const char *nop)
{
    size_t i;
    for (i = 0; i < sizeof(k_class_map) / sizeof(k_class_map[0]); i++)
        if (!strcmp(k_class_map[i].nop, nop)) return k_class_map[i].onvif;
    return NULL;
}

static const char *class_to_nop_trigger(const char *onvif)
{
    size_t i;
    for (i = 0; i < sizeof(k_class_map) / sizeof(k_class_map[0]); i++)
        if (!strcmp(k_class_map[i].onvif, onvif)) return k_class_map[i].nop;
    return NULL;
}

/* ---- line direction: NOP AB/BA/BOTH <-> ONVIF LineDetector Left/Right/Any --
 * Per the NVR API doc: direction is defined the SAME as ONVIF. Looking from the
 * line's start point toward its end point, the LEFT side is A and the RIGHT side
 * is B; so "AB" means crossing from the left side to the right side (i.e. moving
 * rightward), and "BA" is the reverse. ONVIF's tt:Direction enum is Left/Right/
 * Any, where "Right" is the left->right crossing. Hence: AB<->Right, BA<->Left,
 * BOTH<->Any (Any is also the default when unset). Reversible. */
static const char *nop_dir_to_onvif(const char *nop)
{
    if (nop && !strcmp(nop, "AB")) return "Right";  /* left -> right crossing */
    if (nop && !strcmp(nop, "BA")) return "Left";   /* right -> left crossing */
    return "Any";                                   /* BOTH / default */
}

static const char *onvif_dir_to_nop(const char *onvif)
{
    if (onvif && !strcmp(onvif, "Right")) return "AB";
    if (onvif && !strcmp(onvif, "Left"))  return "BA";
    return "BOTH";                                  /* Any / default */
}

/* NOP triggers[] -> "Human,Vehicle" CSV. */
static void triggers_to_csv(const nop_json_t *triggers, char *out, int size)
{
    int i, n, first = 1;
    out[0] = '\0';
    if (!triggers || !nop_json_is_arr(triggers)) return;
    n = nop_json_arr_size(triggers);
    for (i = 0; i < n; i++) {
        const char *t = nop_json_as_str(nop_json_arr_at(triggers, i), NULL);
        const char *cls = t ? nop_trigger_to_class(t) : NULL;
        if (cls) {
            int off = (int)strlen(out);
            snprintf(out + off, size - off, "%s%s", first ? "" : ",", cls);
            first = 0;
        }
    }
}

/* "Human,Vehicle" CSV -> NOP triggers[] json array. */
static nop_json_t *csv_to_triggers(const char *csv)
{
    nop_json_t *arr = nop_json_arr();
    const char *p = csv;
    char        tok[32];
    while (p && *p) {
        const char *comma = strchr(p, ',');
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        const char *nop;
        if (len > 0 && len < (int)sizeof(tok)) {
            memcpy(tok, p, len); tok[len] = '\0';
            nop = class_to_nop_trigger(tok);
            if (nop) nop_json_arr_push_str(arr, nop);
        }
        if (!comma) break;
        p = comma + 1;
    }
    return arr;
}

/* ---- shared get/set over a rule type ("LineDetector"/"FieldDetector") --- */

static nop_status_t ai_get_rules(nop_onvif_map_backend_t *be, int ch,
                                 const char *onvif_type, const char *points_key,
                                 nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_rule_t rules[AI_MAX_RULES];
    char             cfg[100];
    nop_json_t      *arr;
    int              n, i, k;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, onvif_type,
                                      rules, AI_MAX_RULES);
    onvif_session_end(be);
    if (n < 0) return NOP_ERR_IO;

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_t *pts = nop_json_arr();
        nop_json_add_bool(e, "enable", true);   /* present rule == enabled */
        nop_json_add_str(e, "name", rules[i].name);
        for (k = 0; k < rules[i].point_count; k++) {
            nop_json_t *pt = nop_json_obj();
            int nx, ny;
            nop_coord_norm_to_thousandths(rules[i].x[k], rules[i].y[k], &nx, &ny);
            nop_json_add_int(pt, "x", nx);
            nop_json_add_int(pt, "y", ny);
            nop_json_arr_push(pts, pt);
        }
        nop_json_add(e, points_key, pts);
        if (rules[i].direction[0])
            nop_json_add_str(e, "direction", onvif_dir_to_nop(rules[i].direction));
        nop_json_add(e, "triggers", csv_to_triggers(rules[i].class_filter));
        nop_json_arr_push(arr, e);
    }

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    nop_json_add_int(resp->content, "channel", ch);
    nop_json_add(resp->content, "rules", arr);
    return NOP_OK;
}

static nop_status_t ai_set_rules(nop_onvif_map_backend_t *be, int ch,
                                 const char *onvif_type, const char *points_key,
                                 int is_line, const nop_request_t *req,
                                 nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_rule_t  existing[AI_MAX_RULES];
    const nop_json_t *rules;
    char              cfg[100];
    int               n, i, nr;
    nop_status_t      rc = NOP_OK;

    rules = nop_json_get(req->args, "rules");
    if (!rules || !nop_json_is_arr(rules))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }

    /* Replace-all: delete every existing rule of this type first. */
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, onvif_type,
                                      existing, AI_MAX_RULES);
    for (i = 0; i < n; i++)
        nop_onvif_analytics_delete_rule(onvif_session_dev(s), cfg, existing[i].name);

    nr = nop_json_arr_size(rules);
    for (i = 0; i < nr; i++) {
        const nop_json_t *ri = nop_json_arr_at(rules, i);
        const nop_json_t *pts;
        nop_onvif_rule_t  rule;
        int               np, k;
        if (!ri || !nop_json_bool(ri, "enable", true))
            continue;                       /* disabled -> not created */
        pts = nop_json_get(ri, points_key);
        np  = (pts && nop_json_is_arr(pts)) ? nop_json_arr_size(pts) : 0;

        memset(&rule, 0, sizeof(rule));
        snprintf(rule.name, sizeof(rule.name), "%s", nop_json_str(ri, "name", "rule"));
        snprintf(rule.type, sizeof(rule.type), "%s", onvif_type);
        if (is_line)
            snprintf(rule.direction, sizeof(rule.direction), "%s",
                     nop_dir_to_onvif(nop_json_str(ri, "direction", "BOTH")));
        triggers_to_csv(nop_json_get(ri, "triggers"), rule.class_filter,
                        sizeof(rule.class_filter));

        if (np == 0) {
            /* NOPMappingONVIF.md §9 ps: empty line/area -> default the geometry
             * to the full-frame [-1,1] extents so a valid ONVIF rule is created
             * (line = TL->BR diagonal; field = the four frame corners). */
            if (is_line) {
                rule.x[0] = -1.0f; rule.y[0] =  1.0f;
                rule.x[1] =  1.0f; rule.y[1] = -1.0f;
                rule.point_count = 2;
            } else {
                rule.x[0] = -1.0f; rule.y[0] =  1.0f;
                rule.x[1] =  1.0f; rule.y[1] =  1.0f;
                rule.x[2] =  1.0f; rule.y[2] = -1.0f;
                rule.x[3] = -1.0f; rule.y[3] = -1.0f;
                rule.point_count = 4;
            }
        } else {
            if (np > NOP_ONVIF_RULE_MAX_PTS) np = NOP_ONVIF_RULE_MAX_PTS;
            for (k = 0; k < np; k++) {
                const nop_json_t *pt = nop_json_arr_at(pts, k);
                int nx = (int)nop_json_num(pt, "x", 0);
                int ny = (int)nop_json_num(pt, "y", 0);
                nop_coord_thousandths_to_norm(nx, ny, &rule.x[k], &rule.y[k]);
            }
            rule.point_count = np;
        }

        if (nop_onvif_analytics_create_rule(onvif_session_dev(s), cfg, &rule) != 0)
            rc = NOP_ERR_IO;
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set rule failed");
    return rc;
}

nop_status_t onvif_map_AI_getChannelLineCrossDetect(nop_onvif_map_backend_t *be, int ch,
                                                    const nop_request_t *req, nop_response_t *resp)
{
    (void)req;
    return ai_get_rules(be, ch, "LineDetector", "line", resp);
}

nop_status_t onvif_map_AI_setChannelLineCrossDetect(nop_onvif_map_backend_t *be, int ch,
                                                    const nop_request_t *req, nop_response_t *resp)
{
    return ai_set_rules(be, ch, "LineDetector", "line", 1, req, resp);
}

nop_status_t onvif_map_AI_getChannelFieldIntrusionDetect(nop_onvif_map_backend_t *be, int ch,
                                                         const nop_request_t *req, nop_response_t *resp)
{
    (void)req;
    return ai_get_rules(be, ch, "FieldDetector", "area", resp);
}

nop_status_t onvif_map_AI_setChannelFieldIntrusionDetect(nop_onvif_map_backend_t *be, int ch,
                                                         const nop_request_t *req, nop_response_t *resp)
{
    return ai_set_rules(be, ch, "FieldDetector", "area", 0, req, resp);
}

/* ---- §8 Object detection: sensor config <-> ObjectDetection rules ------- */
/* One ObjectDetection rule per class (ClassFilter). enable => rule present. */

/* Object-detection sensor classes we expose (pixelChange == motion, handled
 * by the activity-zone/CellMotion path, not here). */
static const char *k_obj_sensors[] = { "human", "vehicle", "animal", "face" };

/* Is class @p onvif_class present in any fetched ObjectDetection rule? */
static int class_enabled(const nop_onvif_rule_t *rules, int n, const char *onvif_class)
{
    int i;
    for (i = 0; i < n; i++)
        if (strstr(rules[i].class_filter, onvif_class))
            return 1;
    return 0;
}

nop_status_t onvif_map_getChannelSensorConfig(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_rule_t rules[AI_MAX_RULES];
    char             cfg[100];
    nop_json_t      *arr;
    int              n, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                      rules, AI_MAX_RULES);
    onvif_session_end(be);
    if (n < 0) return NOP_ERR_IO;

    arr = nop_json_arr();
    for (i = 0; i < (int)(sizeof(k_obj_sensors) / sizeof(k_obj_sensors[0])); i++) {
        const char *cls = nop_trigger_to_class(k_obj_sensors[i]);
        nop_json_t *e = nop_json_obj();
        nop_json_add_str(e, "sensor", k_obj_sensors[i]);
        nop_json_add_bool(e, "enable", cls ? class_enabled(rules, n, cls) : false);
        nop_json_add_int(e, "eventInterval", 30);
        nop_json_arr_push(arr, e);
    }

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;
    nop_json_add_int(resp->content, "channel", ch);
    nop_json_add(resp->content, "sensors", arr);
    return NOP_OK;
}

nop_status_t onvif_map_setChannelSensorConfig(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_rule_t  existing[AI_MAX_RULES];
    const nop_json_t *sensors;
    char              cfg[100];
    int               n, i, ns;
    nop_status_t      rc = NOP_OK;

    sensors = nop_json_get(req->args, "sensors");
    if (!sensors || !nop_json_is_arr(sensors))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                      existing, AI_MAX_RULES);

    ns = nop_json_arr_size(sensors);
    for (i = 0; i < ns; i++) {
        const nop_json_t *si = nop_json_arr_at(sensors, i);
        const char       *sensor = si ? nop_json_str(si, "sensor", NULL) : NULL;
        const char       *cls;
        int               enable, present, k, found = -1;
        if (!sensor || !strcmp(sensor, "pixelChange"))
            continue;                 /* motion handled by activity-zone path */
        cls = nop_trigger_to_class(sensor);
        if (!cls)
            continue;
        enable  = nop_json_bool(si, "enable", true) ? 1 : 0;
        for (k = 0; k < n; k++)
            if (strstr(existing[k].class_filter, cls)) { found = k; break; }
        present = found >= 0;

        if (enable && !present) {
            nop_onvif_rule_t r;
            memset(&r, 0, sizeof(r));
            snprintf(r.name, sizeof(r.name), "ObjectDetect_%s", cls);
            snprintf(r.type, sizeof(r.type), "ObjectDetection");
            snprintf(r.class_filter, sizeof(r.class_filter), "%s", cls);
            if (nop_onvif_analytics_create_rule(onvif_session_dev(s), cfg, &r) != 0)
                rc = NOP_ERR_IO;
        } else if (!enable && present) {
            if (nop_onvif_analytics_delete_rule(onvif_session_dev(s), cfg,
                                                existing[found].name) != 0)
                rc = NOP_ERR_IO;
        }
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set sensor failed");
    return rc;
}

/* ---- §9 capability query: AI_getChannelAICapabilities ------------------- */

/* Product capability sub-shape for one object-detection class. These are NVR
 * product flags (not camera-provided), mirroring the native NOP contract. */
static nop_json_t *make_obj_caps(void)
{
    nop_json_t *e = nop_json_obj();
    nop_json_t *draw_in = nop_json_arr();
    nop_json_t *meta = nop_json_obj();
    nop_json_add_bool(e, "drawRegion", true);
    nop_json_add_bool(e, "drawText", true);
    nop_json_arr_push_str(draw_in, "main");
    nop_json_arr_push_str(draw_in, "sub");
    nop_json_add(e, "drawIn", draw_in);
    nop_json_add(e, "minMaxFilter", nop_json_obj());
    nop_json_add(e, "threshold", nop_json_obj());
    nop_json_add_bool(meta, "eventExtInfo", true);
    nop_json_add(e, "metaData", meta);
    return e;
}

/* ONVIF direction CSV ("Left,Right,Any") -> NOP direction json array. */
static nop_json_t *onvif_dirs_to_nop_arr(const char *csv)
{
    nop_json_t *arr = nop_json_arr();
    const char *p = csv;
    char        tok[16];
    while (p && *p) {
        const char *comma = strchr(p, ',');
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 0 && len < (int)sizeof(tok)) {
            memcpy(tok, p, len); tok[len] = '\0';
            nop_json_arr_push_str(arr, onvif_dir_to_nop(tok));
        }
        if (!comma) break;
        p = comma + 1;
    }
    return arr;
}

/* Add one objectDetection entry per ONVIF-advertised class (CSV of ONVIF names). */
static void add_object_caps(nop_json_t *obj, const char *onvif_classes_csv)
{
    const char *p = onvif_classes_csv;
    char        tok[32];
    while (p && *p) {
        const char *comma = strchr(p, ',');
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 0 && len < (int)sizeof(tok)) {
            const char *nopname;
            memcpy(tok, p, len); tok[len] = '\0';
            nopname = class_to_nop_trigger(tok);
            if (nopname) nop_json_add(obj, nopname, make_obj_caps());
        }
        if (!comma) break;
        p = comma + 1;
    }
}

nop_status_t onvif_map_AI_getChannelAICapabilities(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t     *s;
    nop_onvif_ai_caps_t  caps;
    char                 cfg[100];
    nop_json_t          *obj, *ruled;
    int                  rc;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return NOP_ERR_IO;
    }
    rc = nop_onvif_analytics_get_ai_caps(onvif_session_dev(s), cfg, &caps);
    onvif_session_end(be);
    if (rc != 0) return NOP_ERR_IO;

    resp->content = nop_json_obj();
    if (!resp->content) return NOP_ERR_NOMEM;

    /* objectDetection: one entry per ONVIF-advertised class. */
    obj = nop_json_obj();
    add_object_caps(obj, caps.object_classes);
    nop_json_add(resp->content, "objectDetection", obj);

    /* ruledDetection: lineCross + fieldIntrusion (ObjectMissing has no ONVIF rule). */
    ruled = nop_json_obj();
    if (caps.line_present) {
        nop_json_t *line = nop_json_obj();
        nop_json_add_int(line, "maxLineCount", caps.line_max_instances);
        if (caps.line_max_points > 0)
            nop_json_add_int(line, "maxPointsPerLine", caps.line_max_points);
        nop_json_add(line, "classFilter", csv_to_triggers(caps.line_classes));
        nop_json_add(line, "direction", onvif_dirs_to_nop_arr(caps.line_directions));
        nop_json_add(ruled, "lineCross", line);
    }
    if (caps.field_present) {
        nop_json_t *field = nop_json_obj();
        nop_json_add_int(field, "maxFieldCount", caps.field_max_instances);
        if (caps.field_max_vertices > 0)
            nop_json_add_int(field, "maxVerticesPerField", caps.field_max_vertices);
        nop_json_add(field, "classFilter", csv_to_triggers(caps.field_classes));
        nop_json_add(ruled, "fieldIntrusion", field);
    }
    nop_json_add(resp->content, "ruledDetection", ruled);
    return NOP_OK;
}

/* ---- §1/§4 device capabilities (single ONVIF camera) ------------------- */
/* Per user scope: for an ONVIF channel this returns ONLY that camera's channel
 * capability entry (device-level aggregation stays in the native layer). Fields
 * ONVIF cannot advertise (light/audioAlert/panic — NOP-private; signal/battery)
 * are omitted, never fabricated. */
nop_status_t onvif_map_X_NightOwl_getDeviceCapabilities(nop_onvif_map_backend_t *be, int ch,
                                                        const nop_request_t *req,
                                                        nop_response_t *resp)
{
    onvif_session_t    *s;
    nop_onvif_device_t *dev;
    char                srcs[NOP_ONVIF_MAX_SOURCES][100];
    int                 nsrc, i;
    nop_json_t         *channels;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return NOP_ERR_IO;
    dev  = onvif_session_dev(s);
    /* One capability entry PER VIDEO SOURCE (§10): a dual-source camera reports
     * two channels, each with that source's tokens/caps. Fall back to a single
     * (first) source when the device advertises none distinctly. */
    nsrc = nop_onvif_list_sources(dev, srcs, NOP_ONVIF_MAX_SOURCES);
    if (nsrc <= 0) { srcs[0][0] = '\0'; nsrc = 1; }

    resp->content = nop_json_obj();
    if (!resp->content) { onvif_session_end(be); return NOP_ERR_NOMEM; }
    channels = nop_json_arr();

    for (i = 0; i < nsrc; i++) {
        nop_onvif_dev_caps_t       dc;
        nop_onvif_ai_caps_t        ai;
        nop_onvif_source_tokens_t  st;
        int                        have_ai = 0;
        nop_json_t                *chan, *caps, *ptz, *sensors;

        if (nop_onvif_get_device_caps(dev, srcs[i], &dc) != 0)
            continue;
        if (nop_onvif_resolve_source(dev, srcs[i], &st) == 0 && st.analytics_cfg[0])
            have_ai = (nop_onvif_analytics_get_ai_caps(dev, st.analytics_cfg, &ai) == 0);

        chan = nop_json_obj();
        /* channel = the NVR channel bound to this source (‑1 if not yet added);
         * videoSourceToken is the stable per-source key for the caller to map. */
        nop_json_add_int(chan, "channel", onvif_backend_channel_for_source(be, ch, srcs[i]));
        if (srcs[i][0])
            nop_json_add_str(chan, "videoSourceToken", srcs[i]);

        caps = nop_json_obj();
        nop_json_add_bool(caps, "ptz", dc.has_ptz != 0);
        nop_json_add_bool(caps, "mic", dc.has_mic != 0);
        nop_json_add_bool(caps, "speaker", dc.has_speaker != 0);
        nop_json_add_bool(caps, "full_duplex", (dc.has_mic && dc.has_speaker) != 0);
        nop_json_add_bool(caps, "sensor", dc.has_analytics != 0);
        nop_json_add(chan, "capabilities", caps);

        /* ptz[]: supported feature names (NOPMappingONVIF.md §2 判定用). */
        ptz = nop_json_arr();
        if (dc.ptz_pan)     nop_json_arr_push_str(ptz, "pan");
        if (dc.ptz_tilt)    nop_json_arr_push_str(ptz, "tilt");
        if (dc.ptz_zoom)    nop_json_arr_push_str(ptz, "zoom");
        if (dc.ptz_preset)  nop_json_arr_push_str(ptz, "preset");
        if (dc.ptz_home)    nop_json_arr_push_str(ptz, "home");
        if (dc.ptz_patrol)  nop_json_arr_push_str(ptz, "patrol");
        if (dc.ptz_hdtrack) nop_json_arr_push_str(ptz, "hdTrack");
        nop_json_add(chan, "ptz", ptz);

        /* sensors[]: motion + objectDetection with the classes this source reports. */
        sensors = nop_json_arr();
        if (have_ai && ai.motion_present) {
            nop_json_t *e = nop_json_obj();
            nop_json_t *modes = nop_json_arr();
            nop_json_add_str(e, "sensor", "motion");
            nop_json_arr_push_str(modes, "pixelChange");
            nop_json_add(e, "modes", modes);
            nop_json_arr_push(sensors, e);
        }
        if (have_ai && ai.object_present && ai.object_classes[0]) {
            nop_json_t *e = nop_json_obj();
            nop_json_add_str(e, "sensor", "objectDetection");
            nop_json_add(e, "modes", csv_to_triggers(ai.object_classes));
            nop_json_arr_push(sensors, e);
        }
        nop_json_add(chan, "sensors", sensors);

        nop_json_arr_push(channels, chan);
    }
    onvif_session_end(be);

    nop_json_add(resp->content, "channels", channels);
    return NOP_OK;
}

#endif /* NOP_ONVIF_MAP */
