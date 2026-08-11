/**
 * @file onvif_map_media.c
 * @brief §3 Media handlers — NOP GUI_get/setChannelMediaProfiles <-> ONVIF
 *        Media2 VideoEncoder configuration (+ options ranges).
 *
 *   get: GetVideoEncoderConfigurations (+ Options) -> profiles[] with
 *        {current, Min, Max} + resolution options, name = main/sub by order.
 *   set: profiles[] -> apply onto the current config baseline ->
 *        SetVideoEncoderConfiguration.
 *
 * Field map (NOPMappingONVIF.md §3): Resolution<->Resolution,
 * FrameRateLimit<->RateControl.FrameRateLimit, BitrateLimit<->BitrateLimit,
 * GovLength<->GovLength, Quality<->Quality. Values pass straight through.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <string.h>

#define VENC_MAX 8

static const char *index_to_name(int i)
{
    if (i == 0) return "main";
    if (i == 1) return "sub";
    return "stream";
}

static int name_to_index(const char *name)
{
    if (name && !strcmp(name, "sub")) return 1;
    return 0;  /* "main" and default */
}

/* {"current":cur,"Min":lo,"Max":hi} */
static nop_json_t *range_obj(int cur, int lo, int hi)
{
    nop_json_t *o = nop_json_obj();
    nop_json_add_int(o, "current", cur);
    nop_json_add_int(o, "Min", lo);
    nop_json_add_int(o, "Max", hi);
    return o;
}

nop_status_t onvif_map_GUI_getChannelMediaProfiles(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req,
                                                   nop_response_t *resp)
{
    onvif_session_t *s;
    nop_onvif_venc_t vencs[VENC_MAX];
    nop_json_t      *arr;
    const char      *mv, *sv;
    int              per_source, n, i, k;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    n = nop_onvif_media2_get_vencs(onvif_session_dev(s), vencs, VENC_MAX);
    if (n < 0) { onvif_session_end(be); return NOP_ERR_IO; }

    /* Scope to THIS source's encoders, keyed by VideoEncoderToken (§10). When
     * the source's encoders are unresolved, fall back to device-wide order. */
    mv = onvif_session_main_venc(s);
    sv = onvif_session_sub_venc(s);
    per_source = (mv[0] || sv[0]);

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_onvif_venc_opts_t opts;
        nop_json_t *e, *encoding, *resolution, *res_opts;
        char wh[32];
        const char *name;
        int  have_opts;

        if (per_source) {
            if (mv[0] && !strcmp(vencs[i].token, mv))      name = "main";
            else if (sv[0] && !strcmp(vencs[i].token, sv)) name = "sub";
            else continue;                    /* encoder of another source */
        } else {
            name = index_to_name(i);
        }

        e = nop_json_obj();
        encoding = nop_json_obj();
        resolution = nop_json_obj();
        res_opts = nop_json_arr();
        have_opts = (nop_onvif_media2_get_venc_options(
                         onvif_session_dev(s), vencs[i].token, &opts) == 0);

        nop_json_add_str(e, "name", name);
        nop_json_add_str(e, "VideoEncoderToken", vencs[i].token);

        nop_json_add_str(encoding, "current", vencs[i].encoding);
        nop_json_add(e, "VideoEncoderEncoding", encoding);

        snprintf(wh, sizeof(wh), "%dx%d", vencs[i].width, vencs[i].height);
        nop_json_add_str(resolution, "current", wh);
        for (k = 0; have_opts && k < opts.res_count; k++) {
            char o[32];
            snprintf(o, sizeof(o), "%dx%d", opts.res_w[k], opts.res_h[k]);
            nop_json_arr_push_str(res_opts, o);
        }
        nop_json_add(resolution, "options", res_opts);
        nop_json_add(e, "VideoEncoderResolution", resolution);

        nop_json_add(e, "VideoEncoderGovLength",
                     range_obj(vencs[i].gov_length,
                               have_opts ? opts.gov_min : vencs[i].gov_length,
                               have_opts ? opts.gov_max : vencs[i].gov_length));
        nop_json_add_bool(e, "VideoEncoderGuaranteedFrameRate", vencs[i].guaranteed_framerate != 0);
        nop_json_add_bool(e, "VideoEncoderConstantBitRate", vencs[i].const_bitrate != 0);
        nop_json_add(e, "VideoEncoderFrameRateLimit",
                     range_obj(vencs[i].fps,
                               have_opts ? opts.fps_min : vencs[i].fps,
                               have_opts ? opts.fps_max : vencs[i].fps));
        nop_json_add(e, "VideoEncoderBitrateLimit",
                     range_obj(vencs[i].bitrate_kbps,
                               have_opts ? opts.bitrate_min : vencs[i].bitrate_kbps,
                               have_opts ? opts.bitrate_max : vencs[i].bitrate_kbps));
        nop_json_add(e, "VideoEncoderQuality",
                     range_obj(vencs[i].quality,
                               have_opts ? opts.quality_min : 0,
                               have_opts ? opts.quality_max : 100));
        nop_json_arr_push(arr, e);
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    nop_json_add(resp->content, "profiles", arr);
    nop_json_add_str(resp->content, "error", "");
    return NOP_OK;
}

/* Read {"current":X} of a nested object, defaulting to @p dflt. */
static double sub_current(const nop_json_t *profile, const char *key, double dflt)
{
    const nop_json_t *o = nop_json_get(profile, key);
    return o ? nop_json_num(o, "current", dflt) : dflt;
}

nop_status_t onvif_map_GUI_setChannelMediaProfiles(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req,
                                                   nop_response_t *resp)
{
    onvif_session_t  *s;
    nop_onvif_venc_t  base[VENC_MAX];
    const nop_json_t *profiles;
    const char       *mv, *sv;
    int               per_source, n, p, np;
    nop_status_t      rc = NOP_OK;

    profiles = nop_json_get(req->args, "profiles");
    if (!profiles || !nop_json_is_arr(profiles))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s)
        return NOP_ERR_IO;
    n = nop_onvif_media2_get_vencs(onvif_session_dev(s), base, VENC_MAX);
    if (n < 0) { onvif_session_end(be); return NOP_ERR_IO; }

    mv = onvif_session_main_venc(s);
    sv = onvif_session_sub_venc(s);
    per_source = (mv[0] || sv[0]);

    np = nop_json_arr_size(profiles);
    for (p = 0; p < np; p++) {
        const nop_json_t *prof = nop_json_arr_at(profiles, p);
        nop_onvif_venc_t  cfg;
        const nop_json_t *res;
        const char       *pname, *want;
        int               idx = -1, k;
        if (!prof)
            continue;
        pname = nop_json_str(prof, "name", "main");
        /* Target this source's encoder by VideoEncoderToken (explicit token wins,
         * else main/sub -> the source's resolved tokens). */
        want = nop_json_str(prof, "VideoEncoderToken", NULL);
        if (per_source) {
            if (!want) want = (!strcmp(pname, "sub")) ? sv : mv;
            if (!want || !want[0]) continue;
            for (k = 0; k < n; k++)
                if (!strcmp(base[k].token, want)) { idx = k; break; }
            if (idx < 0) continue;             /* not this source's encoder */
        } else if (want) {
            for (k = 0; k < n; k++)
                if (!strcmp(base[k].token, want)) { idx = k; break; }
            if (idx < 0) continue;
        } else {
            idx = name_to_index(pname);
            if (idx >= n) continue;
        }
        cfg = base[idx];   /* start from current config, override what's set */

        res = nop_json_get(prof, "VideoEncoderResolution");
        if (res) {
            const char *whs = nop_json_str(res, "current", NULL);
            int w, h;
            if (whs && sscanf(whs, "%dx%d", &w, &h) == 2) { cfg.width = w; cfg.height = h; }
        }
        if (nop_json_get(prof, "VideoEncoderGovLength"))
            cfg.gov_length = (int)sub_current(prof, "VideoEncoderGovLength", cfg.gov_length);
        if (nop_json_get(prof, "VideoEncoderFrameRateLimit"))
            cfg.fps = (int)sub_current(prof, "VideoEncoderFrameRateLimit", cfg.fps);
        if (nop_json_get(prof, "VideoEncoderBitrateLimit"))
            cfg.bitrate_kbps = (int)sub_current(prof, "VideoEncoderBitrateLimit", cfg.bitrate_kbps);
        if (nop_json_get(prof, "VideoEncoderQuality"))
            cfg.quality = (int)sub_current(prof, "VideoEncoderQuality", cfg.quality);
        if (nop_json_has(prof, "VideoEncoderGuaranteedFrameRate"))
            cfg.guaranteed_framerate = nop_json_bool(prof, "VideoEncoderGuaranteedFrameRate", false) ? 1 : 0;
        if (nop_json_has(prof, "VideoEncoderConstantBitRate"))
            cfg.const_bitrate = nop_json_bool(prof, "VideoEncoderConstantBitRate", false) ? 1 : 0;

        if (nop_onvif_media2_set_venc(onvif_session_dev(s), &cfg) != 0)
            rc = NOP_ERR_IO;
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content)
        nop_json_add_str(resp->content, "error", rc == NOP_OK ? "" : "onvif set encoder failed");
    return rc;
}

#endif /* NOP_ONVIF_MAP */
