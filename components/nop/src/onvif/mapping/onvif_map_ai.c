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
    if (nop_onvif_analytics_config_token(onvif_session_dev(s), cfg, sizeof(cfg)) != 0) {
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
            nop_json_add_str(e, "direction", rules[i].direction);
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
    if (nop_onvif_analytics_config_token(onvif_session_dev(s), cfg, sizeof(cfg)) != 0) {
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
        if (!pts || !nop_json_is_arr(pts) || nop_json_arr_size(pts) == 0)
            continue;                       /* empty geometry -> skip */

        memset(&rule, 0, sizeof(rule));
        snprintf(rule.name, sizeof(rule.name), "%s", nop_json_str(ri, "name", "rule"));
        snprintf(rule.type, sizeof(rule.type), "%s", onvif_type);
        if (is_line)
            snprintf(rule.direction, sizeof(rule.direction), "%s",
                     nop_json_str(ri, "direction", "Any"));
        triggers_to_csv(nop_json_get(ri, "triggers"), rule.class_filter,
                        sizeof(rule.class_filter));

        np = nop_json_arr_size(pts);
        if (np > NOP_ONVIF_RULE_MAX_PTS) np = NOP_ONVIF_RULE_MAX_PTS;
        for (k = 0; k < np; k++) {
            const nop_json_t *pt = nop_json_arr_at(pts, k);
            int nx = (int)nop_json_num(pt, "x", 0);
            int ny = (int)nop_json_num(pt, "y", 0);
            nop_coord_thousandths_to_norm(nx, ny, &rule.x[k], &rule.y[k]);
        }
        rule.point_count = np;

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
    if (nop_onvif_analytics_config_token(onvif_session_dev(s), cfg, sizeof(cfg)) != 0) {
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
    if (nop_onvif_analytics_config_token(onvif_session_dev(s), cfg, sizeof(cfg)) != 0) {
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

#endif /* NOP_ONVIF_MAP */
