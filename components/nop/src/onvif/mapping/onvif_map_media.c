/**
 * @file onvif_map_media.c
 * @brief §3 Media handlers — NOP GUI_get/setChannelMediaProfiles <-> ONVIF
 *        Media2 VideoEncoder configuration (+ options ranges).
 *
 *   get: GetVideoEncoderConfigurations + GetVideoEncoderConfigurationOptions
 *        （现值 + 相机 Options：ResolutionsAvailable / BitrateRange /
 *        QualityRange / GovLengthRange / FrameRatesSupported）。
 *        Min/Max/options 只填相机回的，不写默认值。
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

/* {"current":cur} plus Min/Max only when the camera supplied a range. */
static nop_json_t *range_current(int cur)
{
    nop_json_t *o = nop_json_obj();
    nop_json_add_int(o, "current", cur);
    return o;
}
static nop_json_t *range_obj(int cur, int lo, int hi)
{
    nop_json_t *o = range_current(cur);
    nop_json_add_int(o, "Min", lo);
    nop_json_add_int(o, "Max", hi);
    return o;
}
static nop_json_t *range_from_opt(int cur, int have, int lo, int hi)
{
    return have ? range_obj(cur, lo, hi) : range_current(cur);
}

/* Graceful "no data" response per the API doc (statusCode 200 + content.error):
 * "No Camera Binded" when the channel has no ONVIF camera, else "Camera
 * Disconnected". Returns NOP_OK so the router emits 200, not a bare error. */
static nop_status_t media_no_data(nop_onvif_map_backend_t *be, int ch,
                                  nop_response_t *resp)
{
    resp->content = nop_json_obj();
    if (!resp->content)
        return NOP_ERR_NOMEM;
    nop_json_add_str(resp->content, "error",
                     onvif_map_channel_bound(be, ch) ? "Camera Disconnected"
                                                     : "No Camera Binded");
    return NOP_OK;
}

nop_status_t onvif_map_GUI_getChannelMediaProfiles(nop_onvif_map_backend_t *be, int ch,
                                                   const nop_request_t *req,
                                                   nop_response_t *resp)
{
    onvif_session_t    *s;
    nop_onvif_device_t *dev;
    nop_onvif_venc_t    vencs[VENC_MAX];
    nop_json_t         *arr;
    const char         *mv, *sv;
    int                 per_source, n, i;
    (void)req;

    s = onvif_session_begin(be, ch);
    if (!s)
        return media_no_data(be, ch, resp);
    dev = onvif_session_dev(s);
    n = nop_onvif_media2_get_vencs(dev, vencs, VENC_MAX);
    if (n < 0) { onvif_session_end(be); return media_no_data(be, ch, resp); }

    /* Scope to THIS source's encoders, keyed by VideoEncoderToken (§10). When
     * the source's encoders are unresolved, fall back to device-wide order. */
    mv = onvif_session_main_venc(s);
    sv = onvif_session_sub_venc(s);
    per_source = (mv[0] || sv[0]);

    arr = nop_json_arr();
    for (i = 0; i < n; i++) {
        nop_json_t *e, *encoding, *resolution, *res_opts;
        nop_onvif_venc_opts_t opt;
        int         have_opt, k;
        char wh[32];
        const char *name;

        if (per_source) {
            if (mv[0] && !strcmp(vencs[i].token, mv))      name = "main";
            else if (sv[0] && !strcmp(vencs[i].token, sv)) name = "sub";
            else continue;                    /* encoder of another source */
        } else {
            name = index_to_name(i);
        }

        /* Ranges come from GetVideoEncoderConfigurationOptions (spec §3).
         * Missing Options → only current; never invent Min/Max. */
        have_opt = (nop_onvif_media2_get_venc_options(dev, vencs[i].token,
                                                      vencs[i].encoding, &opt) == 0);

        e = nop_json_obj();
        encoding = nop_json_obj();
        resolution = nop_json_obj();
        res_opts = nop_json_arr();

        nop_json_add_str(e, "name", name);
        nop_json_add_str(e, "VideoEncoderToken", vencs[i].token);

        nop_json_add_str(encoding, "current", vencs[i].encoding);
        nop_json_add(e, "VideoEncoderEncoding", encoding);

        snprintf(wh, sizeof(wh), "%dx%d", vencs[i].width, vencs[i].height);
        nop_json_add_str(resolution, "current", wh);
        if (have_opt)
            for (k = 0; k < opt.res_count && k < NOP_ONVIF_VENC_MAX_RES; k++) {
                char rs[32];
                snprintf(rs, sizeof(rs), "%dx%d", opt.res_w[k], opt.res_h[k]);
                nop_json_arr_push_str(res_opts, rs);
            }
        nop_json_add(resolution, "options", res_opts);
        nop_json_add(e, "VideoEncoderResolution", resolution);

        nop_json_add(e, "VideoEncoderGovLength",
                     range_from_opt(vencs[i].gov_length, have_opt && opt.have_gov,
                                    opt.gov_min, opt.gov_max));
        nop_json_add_bool(e, "VideoEncoderGuaranteedFrameRate", vencs[i].guaranteed_framerate != 0);
        nop_json_add_bool(e, "VideoEncoderConstantBitRate", vencs[i].const_bitrate != 0);
        nop_json_add(e, "VideoEncoderFrameRateLimit",
                     range_from_opt(vencs[i].fps, have_opt && opt.have_fps,
                                    opt.fps_min, opt.fps_max));
        nop_json_add(e, "VideoEncoderBitrateLimit",
                     range_from_opt(vencs[i].bitrate_kbps, have_opt && opt.have_bitrate,
                                    opt.bitrate_min, opt.bitrate_max));
        nop_json_add(e, "VideoEncoderQuality",
                     range_from_opt(vencs[i].quality, have_opt && opt.have_quality,
                                    opt.quality_min, opt.quality_max));
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
    int               restore_unsupported = 0;
    nop_status_t      rc = NOP_OK;

    profiles = nop_json_get(req->args, "profiles");
    if (!profiles || !nop_json_is_arr(profiles))
        return NOP_ERR_PARAM;

    s = onvif_session_begin(be, ch);
    if (!s)
        return media_no_data(be, ch, resp);
    n = nop_onvif_device_cached_vencs(onvif_session_dev(s),
                                      onvif_session_bound_source(s), base, VENC_MAX);
    if (n < 0)
        n = nop_onvif_media2_get_vencs(onvif_session_dev(s), base, VENC_MAX);
    if (n < 0) { onvif_session_end(be); return media_no_data(be, ch, resp); }

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
        /* restore=true means factory-reset this profile (all other fields
         * ignored per the API doc). Our ONVIF ABI has no per-profile reset, so
         * report it explicitly instead of silently re-applying current values. */
        if (nop_json_bool(prof, "restore", false)) {
            restore_unsupported = 1;
            continue;
        }
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
        else
            nop_onvif_device_venc_cache_put(onvif_session_dev(s), &cfg);
    }
    onvif_session_end(be);

    resp->content = nop_json_obj();
    if (resp->content) {
        const char *err = "";
        if (rc != NOP_OK)            err = "onvif set encoder failed";
        else if (restore_unsupported) err = "unsupported of restore";
        nop_json_add_str(resp->content, "error", err);
    }
    return rc;
}

#endif /* NOP_ONVIF_MAP */
