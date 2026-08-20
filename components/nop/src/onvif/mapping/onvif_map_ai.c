/**
 * @file onvif_map_ai.c
 * @brief §9 Smart AI handlers — NOP AI_*ChannelLineCrossDetect /
 *        *FieldIntrusionDetect <-> ONVIF Analytics Line/Field rules.
 *
 *   get: GetRules → rules[]（Enabled 缺省 true；几何 ONVIF [-1,1] → NOP 千分位；
 *        各点均为 (-1,1) 或未配置 → line/area 回 []，见 nop_coord_*_unconfigured）
 *   set: 按 Name ModifyRules（写 Enabled/ClassFilter/几何）；没有则 Create。
 *        不 DeleteRules。空 line/area → 各点 (-1,1)（nop_coord_fill_unconfigured）。
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
    { "Package", "package" },
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
        const char *cls;
        char        buf[32];
        int         off;
        if (!t || !t[0]) continue;
        cls = nop_trigger_to_class(t);
        if (!cls) {
            /* Unknown NOP trigger -> pass through with a capitalized first char
             * (inverse of csv_to_triggers' lowercasing) so vendor classes
             * round-trip on write instead of being silently dropped. */
            snprintf(buf, sizeof(buf), "%s", t);
            if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 'a' + 'A');
            cls = buf;
        }
        off = (int)strlen(out);
        snprintf(out + off, size - off, "%s%s", first ? "" : ",", cls);
        first = 0;
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
            if (!nop) {   /* 未知 ONVIF 类 → 首字母小写透传(通用:反映设备真实类别,不丢) */
                if (tok[0] >= 'A' && tok[0] <= 'Z') tok[0] = (char)(tok[0] - 'A' + 'a');
                nop = tok;
            }
            if (nop && nop[0]) nop_json_arr_push_str(arr, nop);
        }
        if (!comma) break;
        p = comma + 1;
    }
    return arr;
}

static void default_fullframe_geom(nop_onvif_rule_t *r, int is_line)
{
    r->point_count = is_line ? 2 : 4;
    nop_coord_fill_unconfigured(r->x, r->y, r->point_count);
}

static int find_rule_by_name(const nop_onvif_rule_t *rules, int n, const char *name)
{
    int i;
    if (!name || !name[0]) return -1;
    for (i = 0; i < n; i++)
        if (!strcmp(rules[i].name, name))
            return i;
    return -1;
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
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, onvif_type,
                                      rules, AI_MAX_RULES);
    onvif_session_end(be);
    if (n < 0) return ONVIF_MAP_FAIL;

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e = nop_json_obj();
        nop_json_add_bool(e, "enable", rules[i].enabled != 0);
        nop_json_add_str(e, "name", rules[i].name);
        if (nop_coord_norm_points_unconfigured(rules[i].x, rules[i].y, rules[i].point_count)) {
            nop_json_add(e, points_key, nop_json_arr());
        } else {
            nop_json_t *pts = nop_json_arr();
            for (k = 0; k < rules[i].point_count; k++) {
                nop_json_t *pt = nop_json_obj();
                int nx, ny;
                nop_coord_norm_to_thousandths(rules[i].x[k], rules[i].y[k], &nx, &ny);
                nop_json_add_int(pt, "x", nx);
                nop_json_add_int(pt, "y", ny);
                nop_json_arr_push(pts, pt);
            }
            nop_json_add(e, points_key, pts);
        }
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

static void fill_rule_from_json(nop_onvif_rule_t *rule, const nop_json_t *ri,
                                const char *onvif_type, const char *points_key,
                                int is_line)
{
    const nop_json_t *pts;
    int np, k;

    memset(rule, 0, sizeof(*rule));
    snprintf(rule->name, sizeof(rule->name), "%s", nop_json_str(ri, "name", "rule"));
    snprintf(rule->type, sizeof(rule->type), "%s", onvif_type);
    rule->enabled = nop_json_bool(ri, "enable", true) ? 1 : 0;
    if (is_line)
        snprintf(rule->direction, sizeof(rule->direction), "%s",
                 nop_dir_to_onvif(nop_json_str(ri, "direction", "BOTH")));
    triggers_to_csv(nop_json_get(ri, "triggers"), rule->class_filter,
                    sizeof(rule->class_filter));

    pts = nop_json_get(ri, points_key);
    np  = (pts && nop_json_is_arr(pts)) ? nop_json_arr_size(pts) : 0;
    if (np == 0) {
        default_fullframe_geom(rule, is_line);
    } else {
        if (np > NOP_ONVIF_RULE_MAX_PTS) np = NOP_ONVIF_RULE_MAX_PTS;
        for (k = 0; k < np; k++) {
            const nop_json_t *pt = nop_json_arr_at(pts, k);
            int nx = (int)nop_json_num(pt, "x", 0);
            int ny = (int)nop_json_num(pt, "y", 0);
            nop_coord_thousandths_to_norm(nx, ny, &rule->x[k], &rule->y[k]);
        }
        rule->point_count = np;
    }
}

static nop_status_t ai_set_rules(nop_onvif_map_backend_t *be, int ch,
                                 const char *onvif_type, const char *points_key,
                                 int is_line, const nop_request_t *req,
                                 nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_rule_t  existing[AI_MAX_RULES];
    char              touched[AI_MAX_RULES];
    const nop_json_t *rules;
    char              cfg[100];
    int               n, i, nr;
    nop_status_t      rc = NOP_OK;

    rules = nop_json_get(req->args, "rules");
    if (!rules || !nop_json_is_arr(rules))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }

    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, onvif_type,
                                      existing, AI_MAX_RULES);
    if (n < 0) n = 0;
    memset(touched, 0, sizeof(touched));

    /* 按 Name 匹配：已有 → ModifyRules（含 Enabled）；没有 → CreateRules。
     * 不 DeleteRules：NightOwl @fixed 与预创建实例靠 Enabled 开关。 */
    nr = nop_json_arr_size(rules);
    for (i = 0; i < nr; i++) {
        const nop_json_t *ri = nop_json_arr_at(rules, i);
        nop_onvif_rule_t  rule;
        int               found;
        if (!ri) continue;
        fill_rule_from_json(&rule, ri, onvif_type, points_key, is_line);
        found = find_rule_by_name(existing, n, rule.name);
        if (found < 0 && i < n && (!rule.name[0] || !strcmp(rule.name, "rule"))) {
            found = i;
            snprintf(rule.name, sizeof(rule.name), "%s", existing[i].name);
        }
        if (found >= 0) {
            touched[found] = 1;
            if (nop_onvif_analytics_modify_rule(onvif_session_dev(s), cfg, &rule) != 0)
                rc = ONVIF_MAP_FAIL;
        } else {
            if (nop_onvif_analytics_create_rule(onvif_session_dev(s), cfg, &rule) != 0)
                rc = ONVIF_MAP_FAIL;
        }
    }
    /* 请求未点名的已有规则：只关 Enabled，不删。 */
    for (i = 0; i < n; i++) {
        if (touched[i]) continue;
        existing[i].enabled = 0;
        if (nop_onvif_analytics_modify_rule(onvif_session_dev(s), cfg, &existing[i]) != 0)
            rc = ONVIF_MAP_FAIL;
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

/* Is class @p onvif_class present AND Enabled? 无 Enabled 栏位时 adapter 已默认 1。 */
static int class_enabled(const nop_onvif_rule_t *rules, int n, const char *onvif_class)
{
    int i;
    for (i = 0; i < n; i++)
        if (strstr(rules[i].class_filter, onvif_class))
            return rules[i].enabled ? 1 : 0;
    return 0;
}

nop_status_t onvif_map_getChannelSensorConfig(nop_onvif_map_backend_t *be, int ch,
                                              const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t           *s;
    nop_onvif_rule_t           obj_rules[AI_MAX_RULES], mot_rules[AI_MAX_RULES];
    nop_onvif_analytics_caps_t supp;
    char                       cfg[100];
    nop_json_t                *arr;
    int                        nobj, nmot, have_supp, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s) return ONVIF_MAP_FAIL;
    /* NOPMappingONVIF.md「sensor 靠 getSupportRules 去映射」:sensor 清单必须按设备真实能力构建。
     * GetSupportedRules/AnalyticsModules 无 CellMotion → 不列 motion;无 ObjectDetection → 不列
     * human/vehicle/animal/face。enable 再按 GetRules 的 ClassFilter 判定。
     * 无 Analytics 配置(相机不支持分析)→ 不报错,回空 sensors(与 getDeviceCapabilities 一致)。 */
    have_supp = 0; nobj = 0; nmot = 0;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) == 0) {
        have_supp = (nop_onvif_analytics_get_supported(onvif_session_dev(s), &supp) == 0);
        nobj = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                             obj_rules, AI_MAX_RULES);
        nmot = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "CellMotion",
                                             mot_rules, AI_MAX_RULES);
        if (nobj < 0) nobj = 0;
        if (nmot < 0) nmot = 0;
    }
    onvif_session_end(be);

    arr = nop_json_arr();

    /* CellMotion → NOP getChannelSensorConfig.sensors[].sensor=pixelChange。 */
    if (have_supp && supp.motion) {
        nop_json_t *e = nop_json_obj();
        int mot_on = 0;
        for (i = 0; i < nmot; i++)
            if (mot_rules[i].enabled) { mot_on = 1; break; }
        nop_json_add_str(e, "sensor", "pixelChange");
        nop_json_add_bool(e, "enable", mot_on);
        nop_json_add_int(e, "eventInterval", 30);
        nop_json_arr_push(arr, e);
    }

    /* ObjectDetection 各类:仅列设备真支持的类(GetSupportedRules 含该类),enable 按 ClassFilter。 */
    {
        struct { const char *sensor; const char *cls; int supported; } objs[] = {
            { "human",   "Human",   have_supp && supp.obj_human   },
            { "vehicle", "Vehicle", have_supp && supp.obj_vehicle },
            { "animal",  "Animal",  have_supp && supp.obj_animal  },
            { "face",    "Face",    have_supp && supp.obj_face    },
        };
        for (i = 0; i < (int)(sizeof(objs) / sizeof(objs[0])); i++) {
            nop_json_t *e;
            if (!objs[i].supported) continue;
            e = nop_json_obj();
            nop_json_add_str(e, "sensor", objs[i].sensor);
            nop_json_add_bool(e, "enable", class_enabled(obj_rules, nobj, objs[i].cls));
            nop_json_add_int(e, "eventInterval", 30);
            nop_json_arr_push(arr, e);
        }
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
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                      existing, AI_MAX_RULES);

    ns = nop_json_arr_size(sensors);
    for (i = 0; i < ns; i++) {
        const nop_json_t *si = nop_json_arr_at(sensors, i);
        const char       *sensor = si ? nop_json_str(si, "sensor", NULL) : NULL;
        const char       *cls;
        int               enable, present, k, found = -1;
        if (!sensor || !strcmp(sensor, "pixelChange") || !strcmp(sensor, "motion"))
            continue;                 /* motion handled by activity-zone path */
        cls = nop_trigger_to_class(sensor);
        if (!cls)
            continue;
        enable  = nop_json_bool(si, "enable", true) ? 1 : 0;
        for (k = 0; k < n; k++)
            if (strstr(existing[k].class_filter, cls)) { found = k; break; }
        present = found >= 0;

        if (present) {
            existing[found].enabled = enable;
            if (nop_onvif_analytics_modify_rule(onvif_session_dev(s), cfg,
                                                &existing[found]) != 0)
                rc = ONVIF_MAP_FAIL;
        } else if (enable) {
            nop_onvif_rule_t r;
            memset(&r, 0, sizeof(r));
            snprintf(r.name, sizeof(r.name), "ObjectDetect_%s", cls);
            snprintf(r.type, sizeof(r.type), "ObjectDetection");
            snprintf(r.class_filter, sizeof(r.class_filter), "%s", cls);
            r.enabled = 1;
            if (nop_onvif_analytics_create_rule(onvif_session_dev(s), cfg, &r) != 0)
                rc = ONVIF_MAP_FAIL;
        }
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set sensor failed");
    return rc;
}

/* ---- §8 Detect threshold <-> ObjectDetection ConfidenceLevel ------------ */

static int find_obj_rule_by_class(const nop_onvif_rule_t *rules, int n, const char *onvif_class)
{
    int i;
    if (!onvif_class || !onvif_class[0])
        return -1;
    for (i = 0; i < n; i++)
        if (strstr(rules[i].class_filter, onvif_class))
            return i;
    return -1;
}

/* 仅当 rule 含 ConfidenceLevel 时，按 ClassFilter 输出 sensors[] 项（无则跳过）。 */
static void threshold_emit_from_rule(nop_json_t *arr, const nop_onvif_rule_t *rule)
{
    const char *p;
    char        tok[32];
    int         threshold;

    if (!arr || !rule || !rule->has_confidence || !rule->class_filter[0])
        return;
    threshold = (int)(rule->confidence_level * 100.0f + 0.5f);
    if (threshold < 0)   threshold = 0;
    if (threshold > 100) threshold = 100;

    p = rule->class_filter;
    while (p && *p) {
        const char *comma = strchr(p, ',');
        const char *nopname;
        nop_json_t *e;
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 0 && len < (int)sizeof(tok)) {
            memcpy(tok, p, len);
            tok[len] = '\0';
            nopname = class_to_nop_trigger(tok);
            if (nopname) {
                e = nop_json_obj();
                nop_json_add_str(e, "sensor", nopname);
                nop_json_add_int(e, "threshold", threshold);
                nop_json_arr_push(arr, e);
            }
        }
        if (!comma)
            break;
        p = comma + 1;
    }
}

nop_status_t onvif_map_AI_getDetectThreshold(nop_onvif_map_backend_t *be, int ch,
                                             const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_rule_t  rules[AI_MAX_RULES];
    char              cfg[100];
    nop_json_t       *arr;
    int               nrules, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be);
        return ONVIF_MAP_FAIL;
    }
    nrules = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                           rules, AI_MAX_RULES);
    onvif_session_end(be);
    if (nrules < 0)
        return ONVIF_MAP_FAIL;

    arr = nop_json_arr();
    for (i = 0; i < nrules; i++)
        threshold_emit_from_rule(arr, &rules[i]);

    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    nop_json_add(resp->content, "sensors", arr);
    return NOP_OK;
}

nop_status_t onvif_map_AI_setDetectThreshold(nop_onvif_map_backend_t *be, int ch,
                                             const nop_request_t *req, nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_rule_t  existing[AI_MAX_RULES];
    const nop_json_t *sensors;
    char              cfg[100];
    int               n, i, ns;
    nop_status_t      rc = NOP_OK;
    (void)resp;

    sensors = nop_json_get(req->args, "sensors");
    if (!sensors || !nop_json_is_arr(sensors))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s)
        return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be);
        return ONVIF_MAP_FAIL;
    }
    n = nop_onvif_analytics_get_rules(onvif_session_dev(s), cfg, "ObjectDetection",
                                      existing, AI_MAX_RULES);
    if (n < 0)
        n = 0;

    ns = nop_json_arr_size(sensors);
    for (i = 0; i < ns; i++) {
        const nop_json_t *si = nop_json_arr_at(sensors, i);
        const char       *sensor = si ? nop_json_str(si, "sensor", NULL) : NULL;
        const char       *cls;
        int               threshold, found;

        if (!sensor)
            continue;
        threshold = (int)nop_json_num(si, "threshold", 0);
        if (threshold <= 0)   /* 0 = 不改（API） */
            continue;
        if (threshold > 100)
            threshold = 100;
        cls = nop_trigger_to_class(sensor);
        if (!cls)
            continue;

        found = find_obj_rule_by_class(existing, n, cls);
        if (found >= 0) {
            existing[found].confidence_level = threshold / 100.0f;
            existing[found].has_confidence = 1;
            if (nop_onvif_analytics_modify_rule(onvif_session_dev(s), cfg,
                                                &existing[found]) != 0)
                rc = ONVIF_MAP_FAIL;
        }
        /* 无对应 ObjectDetection rule → 跳过，不 CreateRules */
    }
    onvif_session_end(be);
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
    // nop_json_add_bool(e, "drawRegion", true);
    // nop_json_add_bool(e, "drawText", true);
    // nop_json_arr_push_str(draw_in, "main");
    // nop_json_arr_push_str(draw_in, "sub");
    // nop_json_add(e, "drawIn", draw_in);
    // nop_json_add(e, "minMaxFilter", nop_json_obj());
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
    if (!s) return ONVIF_MAP_FAIL;
    if (onvif_session_analytics_cfg(s, cfg, sizeof(cfg)) != 0) {
        onvif_session_end(be); return ONVIF_MAP_FAIL;
    }
    rc = nop_onvif_analytics_get_ai_caps(onvif_session_dev(s), cfg, &caps);
    onvif_session_end(be);
    if (rc != 0) return ONVIF_MAP_FAIL;

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
    if (!s) return ONVIF_MAP_FAIL;
    dev  = onvif_session_dev(s);
    /* 源列表用连接时缓存，不再 GetVideoSources/GetProfiles。 */
    nsrc = nop_onvif_device_cached_nsrc(dev);
    if (nsrc > NOP_ONVIF_MAX_SOURCES)
        nsrc = NOP_ONVIF_MAX_SOURCES;
    for (i = 0; i < nsrc; i++) {
        nop_onvif_source_tokens_t st;
        srcs[i][0] = '\0';
        if (nop_onvif_device_cached_source_at(dev, i, &st) == 0)
            snprintf(srcs[i], sizeof(srcs[i]), "%s", st.source_token);
    }
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

        /* ★ best-effort:探测失败也**照常加这条 channel**(至少 signal/videoSourceToken),
         * 能拿到的能力字段就带、拿不到就空 —— 而不是整条跳过导致 channels 空、GUI 能力集空。 */
        nop_onvif_media_caps_t     mc;
        memset(&dc, 0, sizeof(dc));
        memset(&mc, 0, sizeof(mc));
        nop_onvif_get_device_caps(dev, srcs[i], &dc);
        nop_onvif_get_media_caps(dev, &mc);
        /* 分析 token 用连接缓存；不再 resolve_source（又一次 GetProfiles）。 */
        nop_onvif_analytics_caps_t supp;
        memset(&supp, 0, sizeof(supp));
        memset(&st, 0, sizeof(st));
        int have_supp = 0;
        if (nop_onvif_device_cached_source(dev, srcs[i], &st) == 0 && st.analytics_cfg[0])
            have_ai = (nop_onvif_analytics_get_ai_caps(dev, st.analytics_cfg, &ai) == 0);
        if (!have_ai && nop_onvif_device_cached_ai(dev, srcs[i], &ai) == 0)
            have_ai = 1;
        /* 连接时已探过能力：无 analytics 就不要再 GetSupportedRules。 */
        if (!have_ai && !dc.has_analytics)
            have_supp = 0;
        else if (!have_ai)
            have_supp = (nop_onvif_analytics_get_supported(dev, &supp) == 0 &&
                         (supp.motion || supp.objdet));
        if (have_ai || have_supp) dc.has_analytics = 1;

        chan = nop_json_obj();
        /* channel = the NVR channel bound to this source (‑1 if not yet added);
         * videoSourceToken is the stable per-source key for the caller to map. */
        nop_json_add_int(chan, "channel", onvif_backend_channel_for_source(be, ch, srcs[i]));
        if (srcs[i][0])
            nop_json_add_str(chan, "videoSourceToken", srcs[i]);
        /* signal: ONVIF/IP 相机固定 IPC(规范 required,枚举 TVI/AHD/CVI/IPC)。 */
        nop_json_add_str(chan, "signal", "IPC");

        /* capabilities[]: 规范是字符串数组(enum)。light/audioAlert 由 nopOnvif 发现口
         * GET 探测(nvr_channel.c);纯 ONVIF 无标准位。映射见 NOPMappingONVIF.md。 */
        caps = nop_json_arr();
        if (dc.has_mic || mc.mic)         nop_json_arr_push_str(caps, "mic");
        if (mc.audio_out)                 nop_json_arr_push_str(caps, "speaker");
        if ((dc.has_mic || mc.mic) && mc.audio_out && mc.audio_dec)
            nop_json_arr_push_str(caps, "full_duplex");   /* 双向=mic+AudioOutput+AudioDecoder */
        if (dc.has_analytics)             nop_json_arr_push_str(caps, "sensor");
        if (dc.has_ptz)                   nop_json_arr_push_str(caps, "ptz");
        nop_json_add(chan, "capabilities", caps);

        /* ptz[]: 规范枚举 pan/tilt/zoom/focus/nodes/preset/patrol/home/hdTrack。 */
        ptz = nop_json_arr();
        if (dc.ptz_pan)     nop_json_arr_push_str(ptz, "pan");
        if (dc.ptz_tilt)    nop_json_arr_push_str(ptz, "tilt");
        if (dc.ptz_pan && dc.ptz_tilt)    nop_json_arr_push_str(ptz, "diagonal");
        if (dc.ptz_zoom)    nop_json_arr_push_str(ptz, "zoom");
        if (dc.ptz_focus)   nop_json_arr_push_str(ptz, "focus");   /* Imaging 对焦 */
        if (dc.ptz_preset)  nop_json_arr_push_str(ptz, "nodes");
        if (dc.ptz_preset)  nop_json_arr_push_str(ptz, "preset");
        if (dc.ptz_patrol)  nop_json_arr_push_str(ptz, "patrol");
        if (dc.ptz_home)    nop_json_arr_push_str(ptz, "home");     /* GetNodes.HomeSupported */
        if (dc.ptz_hdtrack) nop_json_arr_push_str(ptz, "hdTrack");
        nop_json_add(chan, "ptz", ptz);

        /* sensors[]: motion + objectDetection + schedule(continuous)。 */
        sensors = nop_json_arr();
        int has_motion = (have_ai && ai.motion_present) || (have_supp && supp.motion);
        int has_objdet = (have_ai && ai.object_present && ai.object_classes[0]) || (have_supp && supp.objdet);
        if (has_motion) {
            nop_json_t *e = nop_json_obj();
            nop_json_t *modes = nop_json_arr();
            nop_json_add_str(e, "sensor", "motion");
            nop_json_arr_push_str(modes, "pixelChange");
            if (have_supp && supp.motion_pir)
                nop_json_arr_push_str(modes, "pir");
            nop_json_add(e, "modes", modes);
            nop_json_arr_push(sensors, e);
        }
        if (has_objdet) {
            nop_json_t *e = nop_json_obj();
            nop_json_add_str(e, "sensor", "objectDetection");
            nop_json_t *modes;
            if (have_ai && ai.object_classes[0]) {
                modes = csv_to_triggers(ai.object_classes);
            } else {   /* Media1:按 GetSupportedRules 归类的类别构建 modes */
                modes = nop_json_arr();
                if (supp.obj_human)   nop_json_arr_push_str(modes, "human");
                if (supp.obj_vehicle) nop_json_arr_push_str(modes, "vehicle");
                if (supp.obj_animal)  nop_json_arr_push_str(modes, "animal");
                if (supp.obj_face)    nop_json_arr_push_str(modes, "face");
            }
            nop_json_add(e, "modes", modes);
            nop_json_arr_push(sensors, e);
        }
        if (dc.has_analytics) {
            nop_json_t *e = nop_json_obj();
            nop_json_t *modes = nop_json_arr();
            nop_json_add_str(e, "sensor", "schedule");
            nop_json_arr_push_str(modes, "continuous");
            nop_json_add(e, "modes", modes);
            nop_json_arr_push(sensors, e);
        }
        nop_json_add(chan, "sensors", sensors);

        /* hasBattery: 规范 required(bool);ONVIF 无直接对应 → 恒 false。 */
        nop_json_add_bool(chan, "hasBattery", 0);

        nop_json_arr_push(channels, chan);
    }
    onvif_session_end(be);

    nop_json_add(resp->content, "channels", channels);
    return NOP_OK;
}

#endif /* NOP_ONVIF_MAP */
