/**
 * @file onvif_adapter_ext.cpp
 * @brief Extended calling layer over the vendored Happytimesoft ONVIF client,
 *        implementing the nop_sdk/nop_onvif_ext.h C ABI. Companion to
 *        onvif_adapter.cpp (kept separate so the original adapter stays stable);
 *        both share the same nop_onvif_device_t / ONVIF_DEVICE handle and the
 *        same onvif_<svc>_<Op>(&dev, &req, &res) call convention. Vendored
 *        sources in third_party/onvif/ are used UNMODIFIED.
 */
#include "nop_sdk/nop_onvif_ext.h"
#include <ctype.h>

/* Vendored ONVIF client library headers (third_party/onvif). */
extern "C" {
#include "sys_inc.h"
#include "sys_buf.h"
#include "util.h"
#include "base64.h"
#include "onvif.h"
#include "onvif_cm.h"
#include "onvif_req.h"
#include "onvif_res.h"
#include "onvif_cln.h"
#include "onvif_api.h"
}

#include <string.h>

/* nop_onvif_device_t layout mirrors onvif_adapter.cpp's definition: the opaque
 * handle is a thin wrapper around the vendored ONVIF_DEVICE. */
struct nop_onvif_device {
    ONVIF_DEVICE dev;
};

/* Fill an onvif_PTZSpeed with a uniform normalized magnitude (matches the
 * private fill_ptz_speed in onvif_adapter.cpp). */
static void ext_fill_ptz_speed(onvif_PTZSpeed *speed, float v)
{
    memset(speed, 0, sizeof(*speed));
    speed->PanTiltFlag = 1;
    speed->PanTilt.x = v;
    speed->PanTilt.y = v;
    speed->ZoomFlag = 1;
    speed->Zoom.x = v;
}

/* ======================================================================== */
/* §2 PTZ — preset speed / home                                             */
/* ======================================================================== */

int nop_onvif_ptz_goto_preset_speed(nop_onvif_device_t *device,
                                    const char *profile_token,
                                    const char *preset_token, float speed)
{
    if (!device || !profile_token || !preset_token)
        return -1;

    ptz_GotoPreset_REQ req;
    ptz_GotoPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetToken, preset_token, sizeof(req.PresetToken) - 1);
    if (speed > 0.0f) {
        req.SpeedFlag = 1;
        ext_fill_ptz_speed(&req.Speed, speed > 1.0f ? 1.0f : speed);
    }
    return onvif_ptz_GotoPreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_set_home(nop_onvif_device_t *device, const char *profile_token)
{
    if (!device || !profile_token)
        return -1;

    ptz_SetHomePosition_REQ req;
    ptz_SetHomePosition_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    return onvif_ptz_SetHomePosition(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_img_focus_move(nop_onvif_device_t *device, const char *video_source_token,
                             float speed)
{
    if (!device || !video_source_token)
        return -1;
    img_Move_REQ req;
    img_Move_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);
    req.Focus.ContinuousFlag = 1;
    req.Focus.Continuous.Speed = speed;
    return onvif_img_Move(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_img_focus_stop(nop_onvif_device_t *device, const char *video_source_token)
{
    if (!device || !video_source_token)
        return -1;
    img_Stop_REQ req;
    img_Stop_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);
    return onvif_img_Stop(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_set_preset_ex(nop_onvif_device_t *device, const char *profile_token,
                                const char *token_in, const char *name,
                                char *out_token, int out_size)
{
    if (!device || !profile_token)
        return -1;
    ptz_SetPreset_REQ req;
    ptz_SetPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (token_in && token_in[0]) {
        req.PresetTokenFlag = 1;
        strncpy(req.PresetToken, token_in, sizeof(req.PresetToken) - 1);
    }
    if (name && name[0]) {
        req.PresetNameFlag = 1;
        strncpy(req.PresetName, name, sizeof(req.PresetName) - 1);
    }
    if (!onvif_ptz_SetPreset(&device->dev, &req, &res))
        return -2;
    if (out_token && out_size > 0) {
        strncpy(out_token, res.PresetToken, out_size - 1);
        out_token[out_size - 1] = '\0';
    }
    return 0;
}

/* ---- PTZ patrol (PresetTour) ------------------------------------------- */

static const char *tour_state_str(onvif_PTZPresetTourState st)
{
    switch (st) {
    case PTZPresetTourState_Touring: return "Touring";
    case PTZPresetTourState_Paused:  return "Paused";
    default:                         return "Idle";
    }
}

int nop_onvif_ptz_get_tours(nop_onvif_device_t *device, const char *profile_token,
                            nop_onvif_tour_t *out, int max)
{
    if (!device || !profile_token || !out || max <= 0)
        return -1;
    ptz_GetPresetTours_REQ req;
    ptz_GetPresetTours_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_ptz_GetPresetTours(&device->dev, &req, &res))
        return -2;

    int n = 0;
    for (PresetTourList *l = res.PresetTour; l && n < max; l = l->next) {
        onvif_PresetTour *pt = &l->PresetTour;
        nop_onvif_tour_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        strncpy(o->token, pt->token, sizeof(o->token) - 1);
        strncpy(o->name, pt->Name, sizeof(o->name) - 1);
        o->auto_start = pt->AutoStart ? 1 : 0;
        strncpy(o->status, tour_state_str(pt->Status.State), sizeof(o->status) - 1);
        for (PTZPresetTourSpotList *sp = pt->TourSpot;
             sp && o->spot_count < NOP_ONVIF_TOUR_MAX_SPOTS; sp = sp->next) {
            nop_onvif_tour_spot_t *os = &o->spots[o->spot_count];
            strncpy(os->preset_token, sp->PTZPresetTourSpot.PresetDetail.PresetToken,
                    sizeof(os->preset_token) - 1);
            os->dwell_s = sp->PTZPresetTourSpot.StayTime;
            o->spot_count++;
        }
        n++;
    }
    onvif_free_PresetTours(&res.PresetTour);
    return n;
}

int nop_onvif_ptz_create_tour(nop_onvif_device_t *device, const char *profile_token,
                              char *out_token, int out_size)
{
    if (!device || !profile_token)
        return -1;
    ptz_CreatePresetTour_REQ req;
    ptz_CreatePresetTour_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_ptz_CreatePresetTour(&device->dev, &req, &res))
        return -2;
    if (out_token && out_size > 0) {
        strncpy(out_token, res.PresetTourToken, out_size - 1);
        out_token[out_size - 1] = '\0';
    }
    return 0;
}

int nop_onvif_ptz_modify_tour(nop_onvif_device_t *device, const char *profile_token,
                              const nop_onvif_tour_t *tour)
{
    if (!device || !profile_token || !tour)
        return -1;
    ptz_ModifyPresetTour_REQ req;
    ptz_ModifyPresetTour_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);

    onvif_PresetTour *pt = &req.PresetTour;
    strncpy(pt->token, tour->token, sizeof(pt->token) - 1);
    strncpy(pt->Name, tour->name, sizeof(pt->Name) - 1);
    pt->AutoStart = tour->auto_start ? TRUE : FALSE;
    for (int i = 0; i < tour->spot_count; i++) {
        PTZPresetTourSpotList *sl = onvif_add_PTZPresetTourSpot(&pt->TourSpot);
        if (!sl) break;
        strncpy(sl->PTZPresetTourSpot.PresetDetail.PresetToken, tour->spots[i].preset_token,
                sizeof(sl->PTZPresetTourSpot.PresetDetail.PresetToken) - 1);
        sl->PTZPresetTourSpot.StayTimeFlag = 1;
        sl->PTZPresetTourSpot.StayTime = tour->spots[i].dwell_s;
    }
    int ok = onvif_ptz_ModifyPresetTour(&device->dev, &req, &res) ? 0 : -2;
    /* Free the spot list we built (nodes are malloc'd by onvif_add_*). */
    PTZPresetTourSpotList *sp = pt->TourSpot;
    while (sp) { PTZPresetTourSpotList *nx = sp->next; free(sp); sp = nx; }
    return ok;
}

int nop_onvif_ptz_operate_tour(nop_onvif_device_t *device, const char *profile_token,
                               const char *token, const char *op)
{
    if (!device || !profile_token || !token)
        return -1;
    ptz_OperatePresetTour_REQ req;
    ptz_OperatePresetTour_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetTourToken, token, sizeof(req.PresetTourToken) - 1);
    if (op && !strcmp(op, "Stop"))       req.Operation = PTZPresetTourOperation_Stop;
    else if (op && !strcmp(op, "Pause")) req.Operation = PTZPresetTourOperation_Pause;
    else                                 req.Operation = PTZPresetTourOperation_Start;
    return onvif_ptz_OperatePresetTour(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_remove_tour(nop_onvif_device_t *device, const char *profile_token,
                              const char *token)
{
    if (!device || !profile_token || !token)
        return -1;
    ptz_RemovePresetTour_REQ req;
    ptz_RemovePresetTour_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetTourToken, token, sizeof(req.PresetTourToken) - 1);
    return onvif_ptz_RemovePresetTour(&device->dev, &req, &res) ? 0 : -2;
}

/* ======================================================================== */
/* §7 Privacy Zone — Media2 Mask                                            */
/* ======================================================================== */

int nop_onvif_media2_get_masks(nop_onvif_device_t *device,
                               nop_onvif_mask_t *out, int max,
                               const char *config_token)
{
    if (!device || !out || max <= 0)
        return -1;

    tr2_GetMasks_REQ req;
    tr2_GetMasks_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (config_token && config_token[0]) {
        req.ConfigurationTokenFlag = 1;
        strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    }
    if (!onvif_tr2_GetMasks(&device->dev, &req, &res))
        return -2;

    int n = 0;
    for (MaskList *m = res.Masks; m && n < max; m = m->next) {
        nop_onvif_mask_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        strncpy(o->token, m->Mask.token, sizeof(o->token) - 1);
        strncpy(o->config_token, m->Mask.ConfigurationToken, sizeof(o->config_token) - 1);
        o->enabled = m->Mask.Enabled ? 1 : 0;

        uint32 pc = m->Mask.Polygon.sizePoint;
        if (pc > NOP_ONVIF_MASK_MAX_POINTS)
            pc = NOP_ONVIF_MASK_MAX_POINTS;
        for (uint32 i = 0; i < pc; i++) {
            o->x[i] = m->Mask.Polygon.Point[i].x;
            o->y[i] = m->Mask.Polygon.Point[i].y;
        }
        o->point_count = (int)pc;
        n++;
    }
    onvif_free_Masks(&res.Masks);
    if (n >= 0)
        nop_onvif_device_mask_cache_replace(device, config_token, out, n);
    return n;
}

int nop_onvif_media2_create_mask(nop_onvif_device_t *device,
                                 const char *config_token,
                                 const float *xs, const float *ys, int npoints,
                                 int enabled, const char *type,
                                 char *out_token, int out_size)
{
    if (!device || !config_token || !xs || !ys || npoints <= 0)
        return -1;
    if (npoints > (int)(sizeof(((onvif_Polygon *)0)->Point) / sizeof(onvif_Vector)))
        return -1;

    tr2_CreateMask_REQ req;
    tr2_CreateMask_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    strncpy(req.Mask.ConfigurationToken, config_token,
            sizeof(req.Mask.ConfigurationToken) - 1);
    req.Mask.Enabled = enabled ? TRUE : FALSE;
    strncpy(req.Mask.Type, (type && type[0]) ? type : "Color",
            sizeof(req.Mask.Type) - 1);
    req.Mask.Polygon.sizePoint = (uint32)npoints;
    for (int i = 0; i < npoints; i++) {
        req.Mask.Polygon.Point[i].x = xs[i];
        req.Mask.Polygon.Point[i].y = ys[i];
    }
    if (!onvif_tr2_CreateMask(&device->dev, &req, &res))
        return -2;
    if (out_token && out_size > 0) {
        strncpy(out_token, res.Token, out_size - 1);
        out_token[out_size - 1] = '\0';
    }
    return 0;
}

int nop_onvif_media2_delete_mask(nop_onvif_device_t *device, const char *token)
{
    if (!device || !token)
        return -1;

    tr2_DeleteMask_REQ req;
    tr2_DeleteMask_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.Token, token, sizeof(req.Token) - 1);
    return onvif_tr2_DeleteMask(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_media2_video_source_token(nop_onvif_device_t *device,
                                        char *out, int size)
{
    nop_onvif_source_tokens_t st;
    if (!device || !out || size <= 0)
        return -1;
    out[0] = '\0';
    if (nop_onvif_device_cached_source(device, NULL, &st) == 0 && st.vsc_token[0]) {
        strncpy(out, st.vsc_token, size - 1);
        out[size - 1] = '\0';
        return 0;
    }
    if (device->dev.media_profiles) {
        for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
            if (p->MediaProfile.Configurations.VideoSourceFlag) {
                strncpy(out, p->MediaProfile.Configurations.VideoSource.token, size - 1);
                out[size - 1] = '\0';
                return 0;
            }
        }
    }
    if (device->dev.profiles) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->v_src_cfg && p->v_src_cfg->Configuration.token[0]) {
                strncpy(out, p->v_src_cfg->Configuration.token, size - 1);
                out[size - 1] = '\0';
                return 0;
            }
        }
    }
    return -3;
}

/* ======================================================================== */
/* §5 OSD — Media2 OSD                                                       */
/* ======================================================================== */

/* Render a plain-C nop_onvif_osd_t into a vendored onvif_OSDConfiguration. */
static void fill_osd_config(onvif_OSDConfiguration *cfg, const nop_onvif_osd_t *o)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->token, o->token, sizeof(cfg->token) - 1);
    strncpy(cfg->VideoSourceConfigurationToken, o->config_token,
            sizeof(cfg->VideoSourceConfigurationToken) - 1);

    cfg->Position.Type = (onvif_OSDPosType)o->pos_type;
    if (o->pos_type == NOP_OSD_POS_CUSTOM) {
        cfg->Position.PosFlag = 1;
        cfg->Position.Pos.x = o->pos_x;
        cfg->Position.Pos.y = o->pos_y;
    }

    if (o->is_image) {
        cfg->Type = OSDType_Image;
        cfg->ImageFlag = 1;
        strncpy(cfg->Image.ImgPath, o->img_path, sizeof(cfg->Image.ImgPath) - 1);
    } else {
        cfg->Type = OSDType_Text;
        cfg->TextStringFlag = 1;
        cfg->TextString.Type = (onvif_OSDTextType)o->text_type;
        if (o->plain_text[0]) {
            cfg->TextString.PlainTextFlag = 1;
            strncpy(cfg->TextString.PlainText, o->plain_text,
                    sizeof(cfg->TextString.PlainText) - 1);
        }
    }
}

int nop_onvif_media2_get_osds(nop_onvif_device_t *device,
                              nop_onvif_osd_t *out, int max,
                              const char *config_token)
{
    if (!device || !out || max <= 0)
        return -1;

    tr2_GetOSDs_REQ req;
    tr2_GetOSDs_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (config_token && config_token[0]) {
        req.ConfigurationTokenFlag = 1;
        strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    }
    if (!onvif_tr2_GetOSDs(&device->dev, &req, &res))
        return -2;

    int n = 0;
    for (OSDConfigurationList *l = res.OSDs; l && n < max; l = l->next) {
        nop_onvif_osd_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        strncpy(o->token, l->OSD.token, sizeof(o->token) - 1);
        strncpy(o->config_token, l->OSD.VideoSourceConfigurationToken,
                sizeof(o->config_token) - 1);
        o->is_image  = (l->OSD.Type == OSDType_Image) ? 1 : 0;
        o->text_type = (int)l->OSD.TextString.Type;
        o->pos_type  = (int)l->OSD.Position.Type;
        o->pos_x     = l->OSD.Position.Pos.x;
        o->pos_y     = l->OSD.Position.Pos.y;
        strncpy(o->plain_text, l->OSD.TextString.PlainText, sizeof(o->plain_text) - 1);
        strncpy(o->img_path, l->OSD.Image.ImgPath, sizeof(o->img_path) - 1);
        n++;
    }
    onvif_free_OSDConfigurations(&res.OSDs);
    if (n >= 0)
        nop_onvif_device_osd_cache_replace(device, config_token, out, n);
    return n;
}

int nop_onvif_media2_set_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd)
{
    if (!device || !osd)
        return -1;
    tr2_SetOSD_REQ req;
    tr2_SetOSD_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    fill_osd_config(&req.OSD, osd);
    return onvif_tr2_SetOSD(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_media2_create_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd,
                                char *out_token, int out_size)
{
    if (!device || !osd)
        return -1;
    tr2_CreateOSD_REQ req;
    tr2_CreateOSD_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    fill_osd_config(&req.OSD, osd);
    req.OSD.token[0] = '\0';   /* device assigns the token on create */
    if (!onvif_tr2_CreateOSD(&device->dev, &req, &res))
        return -2;
    if (out_token && out_size > 0) {
        strncpy(out_token, res.OSDToken, out_size - 1);
        out_token[out_size - 1] = '\0';
    }
    return 0;
}

int nop_onvif_media2_delete_osd(nop_onvif_device_t *device, const char *token)
{
    if (!device || !token)
        return -1;
    tr2_DeleteOSD_REQ req;
    tr2_DeleteOSD_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.OSDToken, token, sizeof(req.OSDToken) - 1);
    return onvif_tr2_DeleteOSD(&device->dev, &req, &res) ? 0 : -2;
}

/* ======================================================================== */
/* §3 Media — Media2 VideoEncoder                                           */
/* ======================================================================== */

/* Scan a range string ("1-500" or "30 25 15 1") for its min & max integers. */
static void parse_int_minmax(const char *s, int *lo, int *hi)
{
    int have = 0, v, sign;
    if (!s) return;
    while (*s) {
        if (*s == '-' && (s[1] >= '0' && s[1] <= '9')) { sign = -1; s++; }
        else if (*s >= '0' && *s <= '9') sign = 1;
        else { s++; continue; }
        v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
        v *= sign;
        if (!have) { *lo = *hi = v; have = 1; }
        else { if (v < *lo) *lo = v; if (v > *hi) *hi = v; }
    }
}

static void venc_from_cfg(nop_onvif_venc_t *o, const onvif_VideoEncoder2Configuration *c)
{
    memset(o, 0, sizeof(*o));
    strncpy(o->token, c->token, sizeof(o->token) - 1);
    strncpy(o->encoding, c->Encoding, sizeof(o->encoding) - 1);
    o->width  = c->Resolution.Width;
    o->height = c->Resolution.Height;
    o->fps          = (int)c->RateControl.FrameRateLimit;
    o->bitrate_kbps = c->RateControl.BitrateLimit;
    o->const_bitrate = c->RateControl.ConstantBitRate ? 1 : 0;
    o->gov_length   = c->GovLength;
    o->quality      = (int)c->Quality;
    o->guaranteed_framerate = c->GuaranteedFrameRate ? 1 : 0;
}

int nop_onvif_media2_get_vencs(nop_onvif_device_t *device,
                               nop_onvif_venc_t *out, int max)
{
    if (!device || !out || max <= 0)
        return -1;

    tr2_GetVideoEncoderConfigurations_REQ req;
    tr2_GetVideoEncoderConfigurations_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (!onvif_tr2_GetVideoEncoderConfigurations(&device->dev, &req, &res))
        return -2;

    int n = 0;
    for (VideoEncoder2ConfigurationList *l = res.Configurations; l && n < max; l = l->next)
        venc_from_cfg(&out[n++], &l->Configuration);
    onvif_free_VideoEncoder2Configurations(&res.Configurations);
    if (n > 0)
        nop_onvif_device_venc_cache_ingest(device, out, n);
    return n;
}

int nop_onvif_media2_get_venc_options(nop_onvif_device_t *device,
                                      const char *config_token,
                                      nop_onvif_venc_opts_t *out)
{
    if (!device || !config_token || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    tr2_GetVideoEncoderConfigurationOptions_REQ req;
    tr2_GetVideoEncoderConfigurationOptions_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    req.GetConfiguration.ConfigurationTokenFlag = 1;
    strncpy(req.GetConfiguration.ConfigurationToken, config_token,
            sizeof(req.GetConfiguration.ConfigurationToken) - 1);
    if (!onvif_tr2_GetVideoEncoderConfigurationOptions(&device->dev, &req, &res))
        return -2;

    int rc = -3;
    if (res.Options) {
        const onvif_VideoEncoder2ConfigurationOptions *o = &res.Options->Options;
        int i;
        out->quality_min = (int)o->QualityRange.Min;
        out->quality_max = (int)o->QualityRange.Max;
        out->bitrate_min = o->BitrateRange.Min;
        out->bitrate_max = o->BitrateRange.Max;
        parse_int_minmax(o->GovLengthRange, &out->gov_min, &out->gov_max);
        parse_int_minmax(o->FrameRatesSupported, &out->fps_min, &out->fps_max);
        for (i = 0; i < MAX_RES_NUMS && out->res_count < NOP_ONVIF_VENC_MAX_RES; i++) {
            if (o->ResolutionsAvailable[i].Width <= 0)
                break;
            out->res_w[out->res_count] = o->ResolutionsAvailable[i].Width;
            out->res_h[out->res_count] = o->ResolutionsAvailable[i].Height;
            out->res_count++;
        }
        rc = 0;
    }
    onvif_free_VideoEncoder2ConfigurationOptions(&res.Options);
    return rc;
}

int nop_onvif_media2_set_venc(nop_onvif_device_t *device, const nop_onvif_venc_t *c)
{
    if (!device || !c)
        return -1;

    tr2_SetVideoEncoderConfiguration_REQ req;
    tr2_SetVideoEncoderConfiguration_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    onvif_VideoEncoder2Configuration *cfg = &req.Configuration;
    strncpy(cfg->token, c->token, sizeof(cfg->token) - 1);
    strncpy(cfg->Encoding, c->encoding, sizeof(cfg->Encoding) - 1);
    cfg->Resolution.Width  = c->width;
    cfg->Resolution.Height = c->height;
    cfg->RateControlFlag = 1;
    cfg->RateControl.FrameRateLimit = (float)c->fps;
    cfg->RateControl.BitrateLimit   = c->bitrate_kbps;
    cfg->RateControl.ConstantBitRateFlag = 1;
    cfg->RateControl.ConstantBitRate = c->const_bitrate ? TRUE : FALSE;
    cfg->GovLengthFlag = 1;
    cfg->GovLength = c->gov_length;
    cfg->Quality = (float)c->quality;
    cfg->GuaranteedFrameRate = c->guaranteed_framerate ? 1 : 0;
    return onvif_tr2_SetVideoEncoderConfiguration(&device->dev, &req, &res) ? 0 : -2;
}

/* ======================================================================== */
/* §9/§8 Analytics — rules                                                  */
/*                                                                          */
/* The rule geometry (line Segments / field Polygon) and ClassFilter live   */
/* as XML inside onvif_Config Parameters.ElementItem.Any. The two XML        */
/* helpers below are the single place to tune the exact element/namespace   */
/* shape against a real camera (per the mapping plan's hardware validation). */
/* ======================================================================== */

/* Skip a leading "tt:"/"tns1:" style namespace prefix. */
static const char *strip_ns(const char *s)
{
    const char *c = strchr(s, ':');
    return c ? c + 1 : s;
}

/* Build "<tt:Polyline>|<tt:Polygon> <tt:Point .../> ... </...>" (heap). */
static char *build_geom_xml(int is_line, const float *xs, const float *ys, int n)
{
    const char *tag = is_line ? "Polyline" : "Polygon";
    char  *buf = (char *)malloc(64 + n * 48);
    int    off, i;
    if (!buf) return NULL;
    off = snprintf(buf, 64, "<tt:%s>", tag);
    for (i = 0; i < n; i++)
        off += snprintf(buf + off, 48, "<tt:Point x=\"%.6f\" y=\"%.6f\"/>", xs[i], ys[i]);
    snprintf(buf + off, 32, "</tt:%s>", tag);
    return buf;
}

/* Build "<tt:ClassFilter>...<tt:Type>Human</tt:Type>...</tt:ClassFilter>". */
static char *build_class_xml(const char *csv)
{
    char  *buf = (char *)malloc(64 + strlen(csv) * 4);
    int    off;
    const char *p = csv;
    if (!buf) return NULL;
    off = snprintf(buf, 32, "<tt:ClassFilter>");
    while (*p) {
        const char *comma = strchr(p, ',');
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 0)
            off += snprintf(buf + off, 32 + len, "<tt:Type>%.*s</tt:Type>", len, p);
        if (!comma) break;
        p = comma + 1;
    }
    snprintf(buf + off, 32, "</tt:ClassFilter>");
    return buf;
}

/* Pull "x=\"..\" y=\"..\"" pairs out of a geometry XML fragment. */
static int parse_points(const char *xml, float *xs, float *ys, int max)
{
    int n = 0;
    const char *p = xml;
    if (!xml) return 0;
    while (n < max && (p = strstr(p, "x=\"")) != NULL) {
        const char *yp;
        xs[n] = (float)atof(p + 3);
        yp = strstr(p, "y=\"");
        if (!yp) break;
        ys[n] = (float)atof(yp + 3);
        n++;
        p = yp + 3;
    }
    return n;
}

/* Append known class names found in a ClassFilter fragment to a CSV buffer. */
/* 从相机 GetRuleOptions 的 tt:StringList 里提取**真实** token(空格/逗号分隔)为 CSV。通用——
 * 按设备实际返回,不再硬编码固定清单。相机文档 nightow_onvif_RuleDescription.md:
 *   <tan:RuleOptions Name="ClassFilter" ...><tt:StringList>Human Vehicle Face</tt:StringList></...>
 * 既用于 ClassFilter(类名) 也用于 Direction(Left/Right/Any)。 */
static void parse_stringlist(const char *xml, char *out, int size)
{
    out[0] = '\0';
    if (!xml || size <= 1) return;
    const char *p = strstr(xml, "StringList");
    if (p) { p = strchr(p, '>'); if (p) p++; }   /* 跳到 <tt:StringList> 之后 */
    else   p = xml;                               /* 无该标签:退化为整段(取标签间文本) */
    int first = 1;
    while (p && *p) {
        if (*p == '<') {                          /* 遇到标签:若是 StringList 内容结束则停;否则跳过该标签 */
            if (p[1] == '/') break;               /* </...StringList> 结束 */
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }
        while (*p && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p==',')) p++;
        if (!*p || *p == '<') continue;
        const char *st = p;
        while (*p && *p!=' ' && *p!='\t' && *p!='\r' && *p!='\n' && *p!=',' && *p!='<') p++;
        int len = (int)(p - st);
        if (len > 0 && len < 40 && (isalpha((unsigned char)st[0]))) {
            int off = (int)strlen(out);
            if (off + len + 2 < size) {
                snprintf(out + off, size - off, "%s%.*s", first ? "" : ",", len, st);
                first = 0;
            }
        }
    }
}
static void parse_classes(const char *xml, char *out, int size)    { parse_stringlist(xml, out, size); }
static void parse_directions(const char *xml, char *out, int size) { parse_stringlist(xml, out, size); }

/* Read the integer inside the first "...Max>NN</...Max>" of an IntRange frag. */
static int parse_range_max(const char *xml)
{
    const char *p;
    if (!xml) return 0;
    p = strstr(xml, "Max>");
    return p ? atoi(p + 4) : 0;
}

int nop_onvif_analytics_get_ai_caps(nop_onvif_device_t *device,
                                    const char *config_token,
                                    nop_onvif_ai_caps_t *out)
{
    if (!device || !config_token || !out)
        return -1;
    if (nop_onvif_device_cached_ai(device, config_token, out) == 0)
        return 0;
    memset(out, 0, sizeof(*out));

    /* GetSupportedRules -> presence + @maxInstances per rule type. */
    {
        tan_GetSupportedRules_REQ req;
        tan_GetSupportedRules_RES res;
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
        if (!onvif_tan_GetSupportedRules(&device->dev, &req, &res))
            return -2;
        for (ConfigDescriptionList *l = res.SupportedRules.RuleDescription; l; l = l->next) {
            onvif_ConfigDescription *d = &l->ConfigDescription;
            const char *nm = strip_ns(d->Name);
            int mi = d->maxInstancesFlag ? d->maxInstances : 0;
            if (!strcmp(nm, "LineDetector"))        { out->line_present = 1;   out->line_max_instances = mi; }
            else if (!strcmp(nm, "FieldDetector"))  { out->field_present = 1;  out->field_max_instances = mi; }
            else if (!strcmp(nm, "ObjectDetection")){ out->object_present = 1; out->object_max_instances = mi; }
            else if (!strcmp(nm, "CellMotionDetector")) out->motion_present = 1;
        }
        onvif_free_ConfigDescriptions(&res.SupportedRules.RuleDescription);
    }

    /* GetRuleOptions -> option ranges/lists, parsed from Options.any raw XML. */
    {
        tan_GetRuleOptions_REQ req;
        tan_GetRuleOptions_RES res;
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
        if (onvif_tan_GetRuleOptions(&device->dev, &req, &res)) {
            for (ConfigOptionsList *l = res.RuleOptions; l; l = l->next) {
                onvif_ConfigOptions *o = &l->Options;
                const char *rt  = strip_ns(o->RuleType);
                const char *nm  = o->Name;
                const char *any = o->any;
                if (!strcmp(rt, "LineDetector")) {
                    if (!strcmp(nm, "Segments"))         out->line_max_points = parse_range_max(any);
                    else if (!strcmp(nm, "Direction"))   parse_directions(any, out->line_directions, sizeof(out->line_directions));
                    else if (!strcmp(nm, "ClassFilter")) parse_classes(any, out->line_classes, sizeof(out->line_classes));
                } else if (!strcmp(rt, "FieldDetector")) {
                    if (!strcmp(nm, "Field"))            out->field_max_vertices = parse_range_max(any);
                    else if (!strcmp(nm, "ClassFilter")) parse_classes(any, out->field_classes, sizeof(out->field_classes));
                } else if (!strcmp(rt, "ObjectDetection")) {
                    if (!strcmp(nm, "ClassFilter"))      parse_classes(any, out->object_classes, sizeof(out->object_classes));
                }
            }
            onvif_free_ConfigOptions(&res.RuleOptions);
        }
    }
    nop_onvif_device_ai_cache_put(device, config_token, out);
    return 0;
}

int nop_onvif_get_device_caps(nop_onvif_device_t *device, const char *source_token,
                              nop_onvif_dev_caps_t *out)
{
    if (!device || !out)
        return -1;
    if (nop_onvif_device_cached_caps(device, source_token, out) == 0)
        return 0;
    memset(out, 0, sizeof(*out));

    char target[100] = "";   /* resolved VideoSource token (imaging/focus probe 用) */

    if (source_token && source_token[0])
        strncpy(target, source_token, sizeof(target) - 1);

    /* 连接时已拉过的 profiles 直接读，不再每次 GetProfiles。 */
    if (device->dev.media_profiles) {
        for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
            onvif_MediaProfile *mp = &p->MediaProfile;
            const char *src;
            if (!mp->Configurations.VideoSourceFlag)
                continue;
            src = mp->Configurations.VideoSource.SourceToken;
            if (target[0] == '\0')
                strncpy(target, src, sizeof(target) - 1);
            if (strcmp(src, target) != 0)
                continue;
            if (mp->Configurations.PTZFlag)         out->has_ptz = 1;
            if (mp->Configurations.AudioSourceFlag) out->has_mic = 1;
            if (mp->Configurations.AudioOutputFlag) out->has_speaker = 1;
            if (mp->Configurations.AnalyticsFlag)   out->has_analytics = 1;
            if (mp->Configurations.MetadataFlag)    out->has_metadata = 1;
        }
    }
    if (device->dev.profiles) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->a_src_cfg || p->a_enc_cfg) out->has_mic = 1;
            if (p->va_cfg)                    out->has_analytics = 1;
        }
    }

    /* focus: Imaging Move 支持。无 VideoSourceToken 则跳过，不空等。 */
    if (target[0]) {
        img_GetMoveOptions_REQ mreq;
        img_GetMoveOptions_RES mres;
        memset(&mreq, 0, sizeof(mreq));
        memset(&mres, 0, sizeof(mres));
        strncpy(mreq.VideoSourceToken, target, sizeof(mreq.VideoSourceToken) - 1);
        if (onvif_img_GetMoveOptions(&device->dev, &mreq, &mres)) {
            onvif_MoveOptions20 *mo = &mres.MoveOptions;
            out->ptz_focus = (mo->AbsoluteFlag || mo->RelativeFlag || mo->ContinuousFlag) ? 1 : 0;
        }
    }

    /* PTZ 节点：已缓存则不再 GetNodes。 */
    if (!device->dev.ptz_node)
        GetNodes(&device->dev);
    if (device->dev.ptz_node) {
        out->has_ptz = 1;
        onvif_PTZNode   *nd = &device->dev.ptz_node->PTZNode;
        onvif_PTZSpaces *sp = &nd->SupportedPTZSpaces;
        out->ptz_pan  = out->ptz_tilt = sp->ContinuousPanTiltVelocitySpaceFlag ? 1 : 0;
        out->ptz_zoom = sp->ContinuousZoomVelocitySpaceFlag ? 1 : 0;
        if (!out->ptz_pan && !out->ptz_zoom)
            out->ptz_pan = out->ptz_tilt = out->ptz_zoom = 1;
        out->ptz_max_presets = nd->MaximumNumberOfPresets;
        out->ptz_preset      = nd->MaximumNumberOfPresets > 0 ? 1 : 0;
        out->ptz_home        = nd->HomeSupported ? 1 : 0;
        if (nd->Extension.SupportedPresetTourFlag &&
            nd->Extension.SupportedPresetTour.MaximumNumberOfPresetTours > 0) {
            out->ptz_patrol    = 1;
            out->ptz_max_tours = nd->Extension.SupportedPresetTour.MaximumNumberOfPresetTours;
        }
    }

    /* hdTrack: 仅在已判定有 PTZ 时探一次。 */
    if (out->has_ptz) {
        ptz_GetServiceCapabilities_REQ req;
        ptz_GetServiceCapabilities_RES res;
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        if (onvif_ptz_GetServiceCapabilities(&device->dev, &req, &res))
            out->ptz_hdtrack = res.Capabilities.MoveAndTrack[0] ? 1 : 0;
    }
    nop_onvif_device_caps_cache_put(device, source_token, out);
    return 0;
}

static void resolve_rank_venc(nop_onvif_source_tokens_t *out, const char *token, int area,
                              int *main_area, int *sub_area)
{
    if (!token || !token[0])
        return;
    if (area > *main_area) {
        strncpy(out->sub_venc, out->main_venc, sizeof(out->sub_venc) - 1);
        *sub_area = *main_area;
        strncpy(out->main_venc, token, sizeof(out->main_venc) - 1);
        *main_area = area;
    } else if (area > *sub_area && strcmp(token, out->main_venc) != 0) {
        strncpy(out->sub_venc, token, sizeof(out->sub_venc) - 1);
        *sub_area = area;
    }
}

static int resolve_from_media2_list(MediaProfileList *head, const char *source_token,
                                    nop_onvif_source_tokens_t *out)
{
    char target[100] = "";
    int found = 0, main_area = -1, sub_area = -1;
    if (source_token && source_token[0])
        strncpy(target, source_token, sizeof(target) - 1);
    for (MediaProfileList *p = head; p; p = p->next) {
        onvif_MediaProfile *mp = &p->MediaProfile;
        const char *src;
        if (!mp->Configurations.VideoSourceFlag)
            continue;
        src = mp->Configurations.VideoSource.SourceToken;
        if (target[0] == '\0')
            strncpy(target, src, sizeof(target) - 1);
        if (strcmp(src, target) != 0)
            continue;
        if (!found) {
            strncpy(out->source_token, src, sizeof(out->source_token) - 1);
            strncpy(out->profile, mp->token, sizeof(out->profile) - 1);
            strncpy(out->vsc_token, mp->Configurations.VideoSource.token,
                    sizeof(out->vsc_token) - 1);
            found = 1;
        }
        if (mp->Configurations.AnalyticsFlag && out->analytics_cfg[0] == '\0')
            strncpy(out->analytics_cfg, mp->Configurations.Analytics.token,
                    sizeof(out->analytics_cfg) - 1);
        if (mp->Configurations.VideoEncoderFlag) {
            const onvif_VideoEncoder2Configuration *ve = &mp->Configurations.VideoEncoder;
            resolve_rank_venc(out, ve->token, ve->Resolution.Width * ve->Resolution.Height,
                              &main_area, &sub_area);
        }
    }
    return found ? 0 : -1;
}

static int resolve_from_media1_list(ONVIF_PROFILE *head, const char *source_token,
                                    nop_onvif_source_tokens_t *out)
{
    char target[100] = "";
    int found = 0, main_area = -1, sub_area = -1;
    if (source_token && source_token[0])
        strncpy(target, source_token, sizeof(target) - 1);
    for (ONVIF_PROFILE *p = head; p; p = p->next) {
        const char *src;
        if (!p->v_src_cfg)
            continue;
        src = p->v_src_cfg->Configuration.SourceToken;
        if (!src[0])
            continue;
        if (target[0] == '\0')
            strncpy(target, src, sizeof(target) - 1);
        if (strcmp(src, target) != 0)
            continue;
        if (!found) {
            strncpy(out->source_token, src, sizeof(out->source_token) - 1);
            strncpy(out->profile, p->token, sizeof(out->profile) - 1);
            strncpy(out->vsc_token, p->v_src_cfg->Configuration.token,
                    sizeof(out->vsc_token) - 1);
            found = 1;
        }
        if (p->va_cfg && out->analytics_cfg[0] == '\0')
            strncpy(out->analytics_cfg, p->va_cfg->Configuration.token,
                    sizeof(out->analytics_cfg) - 1);
        if (p->v_enc_cfg) {
            const onvif_VideoEncoderConfiguration *ve = &p->v_enc_cfg->Configuration;
            resolve_rank_venc(out, ve->token, ve->Resolution.Width * ve->Resolution.Height,
                              &main_area, &sub_area);
        }
    }
    return found ? 0 : -1;
}

int nop_onvif_resolve_source(nop_onvif_device_t *device, const char *source_token,
                             nop_onvif_source_tokens_t *out)
{
    if (!device || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    if (device->dev.media_profiles &&
        resolve_from_media2_list(device->dev.media_profiles, source_token, out) == 0)
        return 0;
    if (device->dev.profiles &&
        resolve_from_media1_list(device->dev.profiles, source_token, out) == 0)
        return 0;

    /* 连接前偶发调用：handle 上还没有 profiles 时才上线 GetProfiles。 */
    tr2_GetProfiles_REQ req;
    tr2_GetProfiles_RES res;
    int rc;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (!onvif_tr2_GetProfiles(&device->dev, &req, &res))
        return -2;
    rc = resolve_from_media2_list(res.Profiles, source_token, out);
    onvif_free_MediaProfiles(&res.Profiles);
    return rc == 0 ? 0 : -3;
}

static int list_push_src(char tokens[][100], int *n, int max, const char *src)
{
    int i;
    if (!src || !src[0] || *n >= max)
        return *n;
    for (i = 0; i < *n; i++)
        if (!strcmp(tokens[i], src))
            return *n;
    strncpy(tokens[*n], src, 99);
    tokens[*n][99] = '\0';
    (*n)++;
    return *n;
}

int nop_onvif_list_sources(nop_onvif_device_t *device, char tokens[][100], int max)
{
    int n = 0;
    if (!device || !tokens || max <= 0)
        return -1;

    /* 已在 handle 上的 VideoSources：连接时 GetVideoSources 过就不再打。 */
    if (device->dev.v_src) {
        for (VideoSourceList *v = device->dev.v_src; v && n < max; v = v->next)
            list_push_src(tokens, &n, max, v->VideoSource.token);
        if (n > 0)
            return n;
    }
    if (GetVideoSources(&device->dev)) {
        for (VideoSourceList *v = device->dev.v_src; v && n < max; v = v->next)
            list_push_src(tokens, &n, max, v->VideoSource.token);
        if (n > 0)
            return n;
    }
    if (device->dev.media_profiles) {
        for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
            if (p->MediaProfile.Configurations.VideoSourceFlag)
                list_push_src(tokens, &n, max,
                              p->MediaProfile.Configurations.VideoSource.SourceToken);
        }
        if (n > 0)
            return n;
    }
    if (device->dev.profiles) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->v_src_cfg)
                list_push_src(tokens, &n, max, p->v_src_cfg->Configuration.SourceToken);
        }
        if (n > 0)
            return n;
    }

    tr2_GetProfiles_REQ req;
    tr2_GetProfiles_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (!onvif_tr2_GetProfiles(&device->dev, &req, &res))
        return -2;
    for (MediaProfileList *p = res.Profiles; p; p = p->next) {
        if (p->MediaProfile.Configurations.VideoSourceFlag)
            list_push_src(tokens, &n, max,
                          p->MediaProfile.Configurations.VideoSource.SourceToken);
    }
    onvif_free_MediaProfiles(&res.Profiles);
    return n;
}

int nop_onvif_analytics_config_token(nop_onvif_device_t *device, char *out, int size)
{
    nop_onvif_source_tokens_t st;
    if (!device || !out || size <= 0)
        return -1;
    out[0] = '\0';
    if (nop_onvif_device_cached_source(device, NULL, &st) == 0 && st.analytics_cfg[0]) {
        strncpy(out, st.analytics_cfg, size - 1);
        out[size - 1] = '\0';
        return 0;
    }
    if (device->dev.media_profiles) {
        for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
            if (p->MediaProfile.Configurations.AnalyticsFlag) {
                strncpy(out, p->MediaProfile.Configurations.Analytics.token, size - 1);
                out[size - 1] = '\0';
                return 0;
            }
        }
    }
    if (device->dev.profiles) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->va_cfg && p->va_cfg->Configuration.token[0]) {
                strncpy(out, p->va_cfg->Configuration.token, size - 1);
                out[size - 1] = '\0';
                return 0;
            }
        }
    }
    return -3;
}

int nop_onvif_analytics_get_rules(nop_onvif_device_t *device, const char *config_token,
                                  const char *type_substr, nop_onvif_rule_t *out, int max)
{
    if (!device || !config_token || !out || max <= 0)
        return -1;
    tan_GetRules_REQ req;
    tan_GetRules_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    if (!onvif_tan_GetRules(&device->dev, &req, &res))
        return -2;

    int n = 0;
    for (ConfigList *l = res.Rule; l && n < max; l = l->next) {
        onvif_Config *c = &l->Config;
        if (type_substr && !strstr(c->Type, type_substr))
            continue;
        nop_onvif_rule_t *r = &out[n];
        memset(r, 0, sizeof(*r));
        strncpy(r->name, c->Name, sizeof(r->name) - 1);
        strncpy(r->type, strip_ns(c->Type), sizeof(r->type) - 1);
        for (SimpleItemList *si = c->Parameters.SimpleItem; si; si = si->next) {
            if (!strcmp(si->SimpleItem.Name, "Direction"))
                strncpy(r->direction, si->SimpleItem.Value, sizeof(r->direction) - 1);
        }
        for (ElementItemList *ei = c->Parameters.ElementItem; ei; ei = ei->next) {
            const char *nm = ei->ElementItem.Name;
            const char *any = ei->ElementItem.Any;
            if (!any) continue;
            if (strstr(nm, "Segment") || strstr(nm, "Field") || strstr(nm, "Polygon"))
                r->point_count = parse_points(any, r->x, r->y, NOP_ONVIF_RULE_MAX_PTS);
            else if (strstr(nm, "ClassFilter") || strstr(nm, "Class"))
                parse_classes(any, r->class_filter, sizeof(r->class_filter));
        }
        n++;
    }
    onvif_free_Configs(&res.Rule);
    return n;
}

/* Build a one-rule ConfigList from a plain-C rule (caller frees via free_Configs). */
static ConfigList *build_rule_config(const nop_onvif_rule_t *r)
{
    ConfigList *list = NULL;
    ConfigList *node = onvif_add_Config(&list);
    if (!node) return NULL;
    onvif_Config *c = &node->Config;
    strncpy(c->Name, r->name, sizeof(c->Name) - 1);
    snprintf(c->Type, sizeof(c->Type), "tt:%s", r->type);

    int is_line = (strstr(r->type, "Line") != NULL);
    if (r->direction[0]) {
        SimpleItemList *si = onvif_add_SimpleItem(&c->Parameters.SimpleItem);
        if (si) {
            strncpy(si->SimpleItem.Name, "Direction", sizeof(si->SimpleItem.Name) - 1);
            strncpy(si->SimpleItem.Value, r->direction, sizeof(si->SimpleItem.Value) - 1);
        }
    }
    if (r->point_count > 0) {
        ElementItemList *ei = onvif_add_ElementItem(&c->Parameters.ElementItem);
        if (ei) {
            strncpy(ei->ElementItem.Name, is_line ? "Segments" : "Field",
                    sizeof(ei->ElementItem.Name) - 1);
            ei->ElementItem.AnyFlag = 1;
            ei->ElementItem.Any = build_geom_xml(is_line, r->x, r->y, r->point_count);
        }
    }
    if (r->class_filter[0]) {
        ElementItemList *ei = onvif_add_ElementItem(&c->Parameters.ElementItem);
        if (ei) {
            strncpy(ei->ElementItem.Name, "ClassFilter", sizeof(ei->ElementItem.Name) - 1);
            ei->ElementItem.AnyFlag = 1;
            ei->ElementItem.Any = build_class_xml(r->class_filter);
        }
    }
    return list;
}

int nop_onvif_analytics_create_rule(nop_onvif_device_t *device, const char *config_token,
                                    const nop_onvif_rule_t *rule)
{
    if (!device || !config_token || !rule)
        return -1;
    tan_CreateRules_REQ req;
    tan_CreateRules_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    req.Rule = build_rule_config(rule);
    int ok = onvif_tan_CreateRules(&device->dev, &req, &res) ? 0 : -2;
    onvif_free_Configs(&req.Rule);
    return ok;
}

int nop_onvif_analytics_modify_rule(nop_onvif_device_t *device, const char *config_token,
                                    const nop_onvif_rule_t *rule)
{
    if (!device || !config_token || !rule)
        return -1;
    tan_ModifyRules_REQ req;
    tan_ModifyRules_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    req.Rule = build_rule_config(rule);
    int ok = onvif_tan_ModifyRules(&device->dev, &req, &res) ? 0 : -2;
    onvif_free_Configs(&req.Rule);
    return ok;
}

int nop_onvif_analytics_delete_rule(nop_onvif_device_t *device, const char *config_token,
                                    const char *name)
{
    if (!device || !config_token || !name)
        return -1;
    tan_DeleteRules_REQ req;
    tan_DeleteRules_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    req.sizeRuleName = 1;
    strncpy(req.RuleName[0], name, ONVIF_NAME_LEN - 1);
    return onvif_tan_DeleteRules(&device->dev, &req, &res) ? 0 : -2;
}

/* ---- §8 CellMotion (ActiveCells base64) -------------------------------- */
/* ActiveCells layout note (tune vs a real camera): row-major, one bit per
 * cell, bit index = row*columns + col, MSB-first within each byte, base64 of
 * ceil(columns*rows/8) bytes. Carried as a SimpleItem "ActiveCells". */

int nop_onvif_analytics_get_cellmotion(nop_onvif_device_t *device, const char *config_token,
                                       nop_onvif_cellmotion_t *io)
{
    if (!device || !config_token || !io)
        return -1;
    memset(io->active, 0, sizeof(io->active));
    io->sensitivity = 0;
    io->min_count = 0;

    tan_GetRules_REQ req;
    tan_GetRules_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ConfigurationToken, config_token, sizeof(req.ConfigurationToken) - 1);
    if (!onvif_tan_GetRules(&device->dev, &req, &res))
        return -2;

    int found = 0;
    for (ConfigList *l = res.Rule; l && !found; l = l->next) {
        if (!strstr(l->Config.Type, "CellMotion"))
            continue;
        for (SimpleItemList *si = l->Config.Parameters.SimpleItem; si; si = si->next) {
            const char *nm = si->SimpleItem.Name;
            const char *val = si->SimpleItem.Value;
            if (!strcmp(nm, "Sensitivity"))
                io->sensitivity = atoi(val);
            else if (!strcmp(nm, "MinCount"))
                io->min_count = atoi(val);
            else if (!strcmp(nm, "ActiveCells"))
                base64_decode(val, (uint32)strlen(val), (uint8 *)io->active,
                              (uint32)sizeof(io->active));
        }
        found = 1;
    }
    onvif_free_Configs(&res.Rule);
    return found ? 1 : 0;
}

int nop_onvif_analytics_set_cellmotion(nop_onvif_device_t *device, const char *config_token,
                                       const nop_onvif_cellmotion_t *in)
{
    if (!device || !config_token || !in)
        return -1;
    int nbits  = in->columns * in->rows;
    int nbytes = (nbits + 7) / 8;
    if (nbytes <= 0 || nbytes > (int)sizeof(in->active))
        return -1;

    char b64[NOP_ONVIF_CELLS_MAX_BITS / 8 * 2 + 8];
    base64_encode((uint8 *)in->active, (uint32)nbytes, b64, (uint32)sizeof(b64));

    char sens[16], mincnt[16];
    snprintf(sens, sizeof(sens), "%d", in->sensitivity);
    snprintf(mincnt, sizeof(mincnt), "%d", in->min_count);

    ConfigList *list = NULL;
    ConfigList *node = onvif_add_Config(&list);
    if (!node) return -3;
    onvif_Config *c = &node->Config;
    strncpy(c->Name, "CellMotion", sizeof(c->Name) - 1);
    strncpy(c->Type, "tt:CellMotionDetector", sizeof(c->Type) - 1);
    struct { const char *n, *v; } items[] = {
        { "Sensitivity", sens }, { "MinCount", mincnt }, { "ActiveCells", b64 },
    };
    for (unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        SimpleItemList *si = onvif_add_SimpleItem(&c->Parameters.SimpleItem);
        if (si) {
            strncpy(si->SimpleItem.Name, items[i].n, sizeof(si->SimpleItem.Name) - 1);
            strncpy(si->SimpleItem.Value, items[i].v, sizeof(si->SimpleItem.Value) - 1);
        }
    }

    tan_CreateRules_REQ creq;
    tan_CreateRules_RES cres;
    memset(&creq, 0, sizeof(creq));
    memset(&cres, 0, sizeof(cres));
    strncpy(creq.ConfigurationToken, config_token, sizeof(creq.ConfigurationToken) - 1);
    creq.Rule = list;
    int ok = onvif_tan_CreateRules(&device->dev, &creq, &cres) ? 0 : -2;
    onvif_free_Configs(&creq.Rule);
    return ok;
}

/* ======================================================================== */
/* §1 Events — structured PullMessages                                      */
/* ======================================================================== */

int nop_onvif_events_pull_msgs(nop_onvif_device_t *device, int timeout_s, int max,
                               nop_onvif_event_msg_t *out)
{
    if (!device || !out || max <= 0)
        return -1;
    if (timeout_s <= 0) timeout_s = 1;

    tev_PullMessages_RES res;
    memset(&res, 0, sizeof(res));
    if (!PullMessages(&device->dev, timeout_s, max, &res))
        return -2;

    int n = 0;
    for (NotificationMessageList *m = res.NotifyMessages; m && n < max; m = m->next) {
        onvif_NotificationMessage *nm = &m->NotificationMessage;
        memset(&out[n], 0, sizeof(out[n]));
        strncpy(out[n].topic, nm->Topic, sizeof(out[n].topic) - 1);
        /* Source: which video source produced it (multi-source routing) + Rule. */
        for (SimpleItemList *si = nm->Message.Source.SimpleItem; si; si = si->next) {
            if (!strcmp(si->SimpleItem.Name, "VideoSourceConfigurationToken"))
                strncpy(out[n].source_vsct, si->SimpleItem.Value, sizeof(out[n].source_vsct) - 1);
        }
        /* Data: object class(es) + crossing direction. The ObjectDetection topic
         * carries no class; line-cross events report the exact Direction here. */
        for (SimpleItemList *di = nm->Message.Data.SimpleItem; di; di = di->next) {
            if (!strcmp(di->SimpleItem.Name, "ClassTypes") ||
                !strcmp(di->SimpleItem.Name, "ClassType"))
                strncpy(out[n].class_types, di->SimpleItem.Value, sizeof(out[n].class_types) - 1);
            else if (!strcmp(di->SimpleItem.Name, "Direction"))
                strncpy(out[n].direction, di->SimpleItem.Value, sizeof(out[n].direction) - 1);
        }
        n++;
    }
    onvif_free_NotificationMessages(&res.NotifyMessages);
    return n;
}

/* Media2 GetProfiles → 汇总各 profile 的 ConfigurationSet 标志位(ConfigurationsSupported):
 *   AudioSource→mic、AudioOutput→speaker 声明、AudioDecoder→backchannel、PTZ→ptz、Analytics→sensor。 */
int nop_onvif_get_media_caps(nop_onvif_device_t *device, nop_onvif_media_caps_t *out)
{
    if (!device || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    if (device->dev.media_profiles) {
        for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
            onvif_ConfigurationSet *c = &p->MediaProfile.Configurations;
            if (c->AudioSourceFlag)  out->mic = 1;
            if (c->AudioOutputFlag)  out->audio_out = 1;
            if (c->AudioDecoderFlag) out->audio_dec = 1;
            if (c->PTZFlag)          out->ptz = 1;
            if (c->AnalyticsFlag)    out->analytics = 1;
        }
    }
    if (device->dev.profiles) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->a_src_cfg || p->a_enc_cfg) out->mic = 1;
            if (p->va_cfg)                    out->analytics = 1;
            if (p->ptz_cfg)                   out->ptz = 1;
        }
    }
    if (out->mic || out->ptz || out->analytics || out->audio_out || out->audio_dec)
        return 0;

    /* 连接前偶发：handle 上还没有 profiles 时才上线。 */
    tr2_GetProfiles_REQ req;
    tr2_GetProfiles_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (onvif_tr2_GetProfiles(&device->dev, &req, &res)) {
        for (MediaProfileList *p = res.Profiles; p; p = p->next) {
            onvif_ConfigurationSet *c = &p->MediaProfile.Configurations;
            if (c->AudioSourceFlag)  out->mic = 1;
            if (c->AudioOutputFlag)  out->audio_out = 1;
            if (c->AudioDecoderFlag) out->audio_dec = 1;
            if (c->PTZFlag)          out->ptz = 1;
            if (c->AnalyticsFlag)    out->analytics = 1;
        }
        onvif_free_MediaProfiles(&res.Profiles);
    }
    if (GetProfiles(&device->dev)) {
        for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
            if (p->a_src_cfg || p->a_enc_cfg) out->mic = 1;
            if (p->va_cfg)                    out->analytics = 1;
            if (p->ptz_cfg)                   out->ptz = 1;
        }
    }
    return 0;
}

/* 名称扫描:把 GetSupportedRules/AnalyticsModules 的 ConfigDescription.Name 归类到 sensor 类型。 */
static void analytics_scan_name(const char *name, nop_onvif_analytics_caps_t *o)
{
    if (!name || !name[0]) return;
    if (strstr(name, "CellMotion") || strstr(name, "Motion"))                    o->motion = 1;
    if (strstr(name, "ObjectDetection") || strstr(name, "ObjectInField") ||
        strstr(name, "FieldDetector"))                                            o->objdet = 1;
    if (strstr(name, "Human") || strstr(name, "People") || strstr(name, "Person")) { o->objdet = 1; o->obj_human   = 1; }
    if (strstr(name, "Vehicle") || strstr(name, "Car"))                          { o->objdet = 1; o->obj_vehicle = 1; }
    if (strstr(name, "Animal") || strstr(name, "Pet"))                           { o->objdet = 1; o->obj_animal  = 1; }
    if (strstr(name, "Face"))                                                    { o->objdet = 1; o->obj_face    = 1; }
}

int nop_onvif_analytics_get_supported(nop_onvif_device_t *device, nop_onvif_analytics_caps_t *out)
{
    if (!device || !out) return -1;
    memset(out, 0, sizeof(*out));

    /* 解析分析配置 token:media2 优先,media1 va_cfg 兜底。 */
    char token[ONVIF_TOKEN_LEN] = {0};
    if (nop_onvif_analytics_config_token(device, token, sizeof(token)) != 0 || !token[0]) {
        if (device->dev.profiles) {
            for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
                if (p->va_cfg && p->va_cfg->Configuration.token[0]) {
                    strncpy(token, p->va_cfg->Configuration.token, sizeof(token) - 1);
                    break;
                }
            }
        }
    }
    if (!token[0]) return -2;

    /* GetSupportedRules ∪ GetSupportedAnalyticsModules(两者组合,NOPMappingONVIF.md)。
     * 同时抓 LineDetector/FieldDetector 的 maxInstances(供 AI_getChannelAICapabilities 的
     * maxLineCount/maxFieldCount)。 */
    tan_GetSupportedRules_REQ rq; tan_GetSupportedRules_RES rs;
    memset(&rq, 0, sizeof(rq)); memset(&rs, 0, sizeof(rs));
    strncpy(rq.ConfigurationToken, token, sizeof(rq.ConfigurationToken) - 1);
    if (onvif_tan_GetSupportedRules(&device->dev, &rq, &rs)) {
        for (ConfigDescriptionList *d = rs.SupportedRules.RuleDescription; d; d = d->next) {
            const char *nm = d->ConfigDescription.Name;
            analytics_scan_name(nm, out);
            int mi = d->ConfigDescription.maxInstancesFlag ? d->ConfigDescription.maxInstances : 0;
            if (nm && strstr(nm, "LineDetector"))  { out->line_cross = 1;      out->line_max  = mi; }
            if (nm && strstr(nm, "FieldDetector")) { out->field_intrusion = 1; out->field_max = mi; }
        }
        onvif_free_ConfigDescriptions(&rs.SupportedRules.RuleDescription);
    }
    tan_GetSupportedAnalyticsModules_REQ mq; tan_GetSupportedAnalyticsModules_RES ms;
    memset(&mq, 0, sizeof(mq)); memset(&ms, 0, sizeof(ms));
    strncpy(mq.ConfigurationToken, token, sizeof(mq.ConfigurationToken) - 1);
    if (onvif_tan_GetSupportedAnalyticsModules(&device->dev, &mq, &ms)) {
        for (ConfigDescriptionList *d = ms.SupportedAnalyticsModules.AnalyticsModuleDescription; d; d = d->next)
            analytics_scan_name(d->ConfigDescription.Name, out);
        onvif_free_ConfigDescriptions(&ms.SupportedAnalyticsModules.AnalyticsModuleDescription);
    }

    /* GetRuleOptions(RuleType=NULL → 全部):objectDetection 的 ClassFilter 支持类别 + 线/场点数上限。
     * ClassFilter 允许值多在 ConfigOptions 的 Name/RuleType/any 原始串里,按名扫描归类。 */
    tan_GetRuleOptions_REQ oq; tan_GetRuleOptions_RES os;
    memset(&oq, 0, sizeof(oq)); memset(&os, 0, sizeof(os));
    strncpy(oq.ConfigurationToken, token, sizeof(oq.ConfigurationToken) - 1);
    if (onvif_tan_GetRuleOptions(&device->dev, &oq, &os)) {
        for (ConfigOptionsList *d = os.RuleOptions; d; d = d->next) {
            onvif_ConfigOptions *co = &d->Options;
            /* ClassFilter 类别:扫 Name / RuleType / any(原始 XML,含枚举值) */
            if ((co->Name && strstr(co->Name, "ClassFilter")) ||
                (co->RuleType[0] && strstr(co->RuleType, "ObjectDetection")) ||
                (co->Name && strstr(co->Name, "Class"))) {
                analytics_scan_name(co->any, out);   /* any 里含 Human/Vehicle/... 枚举 */
                analytics_scan_name(co->Name, out);
            }
            /* 线/场点数(有些设备在 RuleOptions 报 MinCount/MaxCount) */
            if (co->any) {
                if (strstr(co->any, "LineDetector") || strstr(co->RuleType, "LineDetector")) out->line_max_points  = 2;
                if (strstr(co->any, "FieldDetector") || strstr(co->RuleType, "FieldDetector")) out->field_max_verts = 10;
            }
        }
        /* ConfigOptions.any 是 malloc 的原始串,happytime 无专用 free,随 device 生命周期;此处不单独释放。 */
    }
    return 0;
}

int nop_onvif_get_network_mac(nop_onvif_device_t *device, const char *want_ip,
                              char *out, int cap)
{
    tds_GetNetworkInterfaces_REQ req;
    tds_GetNetworkInterfaces_RES res;
    NetworkInterfaceList *p;
    const char *ip_match = NULL, *enabled = NULL, *any = NULL;

    if (!device || !out || cap <= 0)
        return -1;
    out[0] = '\0';
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    if (!onvif_tds_GetNetworkInterfaces(&device->dev, &req, &res))
        return -2;

    for (p = res.NetworkInterfaces; p; p = p->next) {
        const onvif_NetworkInterface *ni = &p->NetworkInterface;
        const char *hw = (ni->InfoFlag && ni->Info.HwAddress[0]) ? ni->Info.HwAddress : NULL;
        uint32 i;
        if (!hw)
            continue;
        if (!any)
            any = hw;
        if (ni->Enabled && !enabled)
            enabled = hw;
        if (!(want_ip && want_ip[0] && ni->Enabled && ni->IPv4Flag && ni->IPv4.Enabled))
            continue;
        for (i = 0; i < ni->IPv4.Config.sizeAddress; i++) {
            if (strcmp(ni->IPv4.Config.Address[i].Address, want_ip) == 0) {
                ip_match = hw;
                break;
            }
        }
        if (ip_match)
            break;
    }
    {
        const char *pick = ip_match ? ip_match : (enabled ? enabled : any);
        if (pick) {
            strncpy(out, pick, (size_t)cap - 1);
            out[cap - 1] = '\0';
        }
    }
    onvif_free_NetworkInterfaces(&res.NetworkInterfaces);
    return out[0] ? 0 : -3;
}
