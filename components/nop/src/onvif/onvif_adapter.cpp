/**
 * @file onvif_adapter.cpp
 * @brief Calling layer over the vendored Happytimesoft ONVIF client library.
 *        Compiled as C++ (the library is C++), exposes a pure C ABI
 *        (nop_sdk/nop_onvif.h). The vendored sources in third_party/onvif/ are
 *        used UNMODIFIED — this file only orchestrates their public API.
 *
 * Coverage: runtime, discovery/monitor, device handle, device service
 * (info/capabilities/date-time/reboot/reset), media (profiles/stream &
 * snapshot URIs/snapshot/video sources), PTZ (move/stop/presets/home),
 * imaging (get/set), events (pull-point), firmware/system maintenance.
 */
#include "nop_sdk/nop_onvif.h"
#include "nop_sdk/nop_log.h"
#include "nop_sdk/osal/osal.h"

/* Vendored ONVIF client library headers (third_party/onvif). */
extern "C" {
#include "sys_inc.h"
#include "sys_buf.h"
#include "util.h"
#include "sys_log.h"
#include "http_parse.h"
#include "onvif.h"
#include "onvif_ver.h"
#include "onvif_cm.h"
#include "onvif_req.h"
#include "onvif_res.h"
#include "onvif_cln.h"
#include "onvif_api.h"
#include "onvif_probe.h"
}

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifndef MAX_DEV_NUMS
#  define MAX_DEV_NUMS 64
#endif

struct nop_onvif_device {
    ONVIF_DEVICE dev;
};

static int           g_onvif_ready = 0;
static osal_mutex_t *g_onvif_mutex = NULL;

/* Discovery/monitor trampoline state (one probe/monitor at a time). */
static nop_onvif_discovery_cb g_user_callback = NULL;
static void                  *g_user_data     = NULL;
static int                    g_discovered    = 0;

/* ======================================================================== */
/* Runtime                                                                  */
/* ======================================================================== */

int nop_onvif_global_init(void)
{
    if (g_onvif_ready)
        return 0;

    network_init();
    /* 不调 log_init()：vendored logger 在本平台 log_init 路径会崩(原作者注)，
     * 且 NULL fp 时 log_print_ex 自动 no-op，保持安静。排查取流用 nvr_onvif.c 里
     * 的 [onvif] get_url 摘要日志即可。 */
    sys_buf_init(10 * MAX_DEV_NUMS);
    http_msg_buf_init(10 * MAX_DEV_NUMS);

    g_onvif_mutex = osal_mutex_create();
    g_onvif_ready = 1;
    NOP_LOGI("onvif: client runtime initialized (%s)", ONVIF_CLIENT_VERSION);
    return 0;
}

void nop_onvif_global_cleanup(void)
{
    if (!g_onvif_ready)
        return;
    osal_mutex_destroy(g_onvif_mutex);
    g_onvif_mutex = NULL;
    g_onvif_ready = 0;
}

/* ======================================================================== */
/* Discovery                                                                */
/* ======================================================================== */

static void copy_scopes(const DEVICE_BINFO *info, char *out, size_t out_size)
{
    size_t used = 0;
    out[0] = '\0';
    for (uint32 scope_index = 0; scope_index < info->sizeScopes; scope_index++) {
        const char *item = info->scopes[scope_index].ScopeItem;
        size_t      len = strlen(item);
        if (used + len + 2 >= out_size)
            break;
        if (used > 0)
            out[used++] = ' ';
        memcpy(out + used, item, len);
        used += len;
        out[used] = '\0';
    }
}

static void probe_trampoline(DEVICE_BINFO *p_res, int msgtype, void *pdata)
{
    (void)pdata;
    if (msgtype != PROBE_MSGTYPE_MATCH && msgtype != PROBE_MSGTYPE_HELLO)
        return;

    nop_onvif_device_info_t device;
    memset(&device, 0, sizeof(device));
    strncpy(device.endpoint_reference, p_res->EndpointReference, sizeof(device.endpoint_reference) - 1);
    strncpy(device.host, p_res->XAddr.host, sizeof(device.host) - 1);
    device.port = p_res->XAddr.port;
    strncpy(device.service_url, p_res->XAddr.url, sizeof(device.service_url) - 1);
    copy_scopes(p_res, device.scopes, sizeof(device.scopes));

    g_discovered++;
    if (g_user_callback)
        g_user_callback(&device, g_user_data);
}

int nop_onvif_discover(const char *local_ip, int seconds,
                       nop_onvif_discovery_cb callback, void *user)
{
    int found;

    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return -1;
    if (seconds <= 0)
        seconds = 5;

    osal_mutex_lock(g_onvif_mutex);
    g_user_callback = callback;
    g_user_data     = user;
    g_discovered    = 0;

    set_probe_cb(probe_trampoline, NULL);
    start_probe(local_ip, seconds);
#ifdef _WIN32
    Sleep((DWORD)seconds * 1000);
#else
    sleep((unsigned)seconds);
#endif
    stop_probe();

    found = g_discovered;
    g_user_callback = NULL;
    g_user_data     = NULL;
    osal_mutex_unlock(g_onvif_mutex);
    return found;
}

int nop_onvif_monitor_start(nop_onvif_discovery_cb callback, void *user)
{
    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return -1;
    g_user_callback = callback;
    g_user_data     = user;
    set_probe_cb(probe_trampoline, NULL);
    start_probe(NULL, 30);
    return 0;
}

void nop_onvif_monitor_stop(void)
{
    stop_probe();
    g_user_callback = NULL;
    g_user_data     = NULL;
}

/* ======================================================================== */
/* Device handle                                                            */
/* ======================================================================== */

nop_onvif_device_t *nop_onvif_device_create(const char *host, int port,
                                            const char *service_url, int https)
{
    nop_onvif_device_t *handle;
    if (!host)
        return NULL;
    if (!g_onvif_ready && nop_onvif_global_init() != 0)
        return NULL;

    handle = (nop_onvif_device_t *)calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;

    onvif_initDevice(&handle->dev, host, port > 0 ? port : 80, https ? 1 : 0);
    if (service_url && service_url[0]) {
        strncpy(handle->dev.binfo.XAddr.url, service_url,
                sizeof(handle->dev.binfo.XAddr.url) - 1);
        handle->dev.binfo.XAddr.url[sizeof(handle->dev.binfo.XAddr.url) - 1] = '\0';
    }
    onvif_SetAuthMethod(&handle->dev, AuthMethod_UsernameToken);
    onvif_SetReqTimeout(&handle->dev, 5000);
    handle->dev.events.init_term_time = 60;
    return handle;
}

void nop_onvif_device_set_auth(nop_onvif_device_t *device,
                               const char *username, const char *password)
{
    if (!device)
        return;
    onvif_SetAuthInfo(&device->dev, username ? username : "", password ? password : "");
}

void nop_onvif_device_set_timeout(nop_onvif_device_t *device, int timeout_ms)
{
    if (device)
        onvif_SetReqTimeout(&device->dev, timeout_ms);
}

void nop_onvif_device_destroy(nop_onvif_device_t *device)
{
    if (!device)
        return;
    onvif_free_device(&device->dev);
    free(device);
}

const char *nop_onvif_device_last_error(nop_onvif_device_t *device)
{
    if (!device)
        return "";
    const char *err = onvif_GetErrString(&device->dev);
    return err ? err : "";
}

/* ======================================================================== */
/* Device service                                                           */
/* ======================================================================== */

int nop_onvif_get_device_information(nop_onvif_device_t *device,
                                     nop_onvif_device_information_t *out)
{
    if (!device || !out)
        return -1;
    if (!GetDeviceInformation(&device->dev))
        return -2;
    const onvif_DeviceInformation *info = &device->dev.DeviceInformation;
    memset(out, 0, sizeof(*out));
    strncpy(out->manufacturer,     info->Manufacturer,    sizeof(out->manufacturer) - 1);
    strncpy(out->model,            info->Model,           sizeof(out->model) - 1);
    strncpy(out->firmware_version, info->FirmwareVersion, sizeof(out->firmware_version) - 1);
    strncpy(out->serial_number,    info->SerialNumber,    sizeof(out->serial_number) - 1);
    strncpy(out->hardware_id,      info->HardwareId,      sizeof(out->hardware_id) - 1);
    return 0;
}

int nop_onvif_get_capabilities(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return GetCapabilities(&device->dev) ? 0 : -2;
}

/* 最近一次 SOAP 调用的底层错误码（ONVIF_ERR_*：-1 连接失败 -4 收超时 -6 空内容 -7 解析失败）。 */
int nop_onvif_last_error(nop_onvif_device_t *device)
{
    return device ? device->dev.errCode : -100;
}

int nop_onvif_get_services(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return GetServices(&device->dev) ? 0 : -2;
}

int nop_onvif_get_system_datetime(nop_onvif_device_t *device, nop_onvif_datetime_t *out)
{
    if (!device || !out)
        return -1;
    if (!GetSystemDateAndTime(&device->dev))
        return -2;
    const onvif_DateTime *dt = &device->dev.devTime;
    out->year   = dt->Date.Year;
    out->month  = dt->Date.Month;
    out->day    = dt->Date.Day;
    out->hour   = dt->Time.Hour;
    out->minute = dt->Time.Minute;
    out->second = dt->Time.Second;
    return 0;
}

int nop_onvif_set_system_datetime_now(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return SetSystemDateAndTime(&device->dev) ? 0 : -2;
}

int nop_onvif_reboot(nop_onvif_device_t *device)
{
    if (!device) return -1;
    tds_SystemReboot_REQ req;
    tds_SystemReboot_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    return onvif_tds_SystemReboot(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_factory_reset(nop_onvif_device_t *device, int hard)
{
    if (!device) return -1;
    tds_SetSystemFactoryDefault_REQ req;
    memset(&req, 0, sizeof(req));
    req.FactoryDefault = hard ? FactoryDefaultType_Hard : FactoryDefaultType_Soft;
    return onvif_tds_SetSystemFactoryDefault(&device->dev, &req, NULL) ? 0 : -2;
}

/* ======================================================================== */
/* Media                                                                    */
/* ======================================================================== */

int nop_onvif_get_profiles(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!GetProfiles(&device->dev))
        return -2;
    /* Resolve RTSP URIs so cached profiles carry a usable stream address. */
    GetStreamUris(&device->dev, TransportProtocol_RTSP);
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next)
        count++;
    return count;
}

int nop_onvif_get_profile(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out)
{
    int i = 0;
    if (!device || !out || index < 0)
        return -1;
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next, i++) {
        if (i == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->token, p->token, sizeof(out->token) - 1);
            strncpy(out->name, p->name, sizeof(out->name) - 1);
            strncpy(out->stream_uri, p->stream_uri, sizeof(out->stream_uri) - 1);
            /* 该 profile 归属的物理视频源(多源区分):取 VideoSourceConfiguration.SourceToken */
            if (p->v_src_cfg)
                strncpy(out->source_token, p->v_src_cfg->Configuration.SourceToken,
                        sizeof(out->source_token) - 1);
            return 0;
        }
    }
    return -2;
}

int nop_onvif_get_stream_uri(nop_onvif_device_t *device, const char *profile_token,
                             nop_onvif_transport_t proto, char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    if (!GetStreamUris(&device->dev, (onvif_TransportProtocol)proto))
        return -2;
    for (ONVIF_PROFILE *p = device->dev.profiles; p; p = p->next) {
        if (strcmp(p->token, profile_token) == 0) {
            strncpy(out_uri, p->stream_uri, out_size - 1);
            out_uri[out_size - 1] = '\0';
            return 0;
        }
    }
    return -3;
}

/* ---- Media2 (ver20 / Profile T)：部分相机只在 media2 返回 profiles/stream uri ---- */
int nop_onvif_get_profiles2(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!tr2_GetProfiles(&device->dev))
        return -2;
    /* 预解析 RTSP 地址到 media_profiles[].stream_uri */
    tr2_GetStreamUris(&device->dev, "RtspUnicast");
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next)
        count++;
    return count;
}

int nop_onvif_get_profile2(nop_onvif_device_t *device, int index, nop_onvif_profile_t *out)
{
    int i = 0;
    if (!device || !out || index < 0)
        return -1;
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next, i++) {
        if (i == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->token, p->MediaProfile.token, sizeof(out->token) - 1);
            strncpy(out->name, p->MediaProfile.Name, sizeof(out->name) - 1);
            strncpy(out->stream_uri, p->MediaProfile.stream_uri, sizeof(out->stream_uri) - 1);
            return 0;
        }
    }
    return -2;
}

int nop_onvif_get_stream_uri2(nop_onvif_device_t *device, const char *profile_token,
                              char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    if (!tr2_GetStreamUris(&device->dev, "RtspUnicast"))
        return -2;
    for (MediaProfileList *p = device->dev.media_profiles; p; p = p->next) {
        if (strcmp(p->MediaProfile.token, profile_token) == 0) {
            strncpy(out_uri, p->MediaProfile.stream_uri, out_size - 1);
            out_uri[out_size - 1] = '\0';
            return 0;
        }
    }
    return -3;
}

int nop_onvif_get_snapshot_uri(nop_onvif_device_t *device, const char *profile_token,
                               char *out_uri, size_t out_size)
{
    if (!device || !profile_token || !out_uri || out_size == 0)
        return -1;
    trt_GetSnapshotUri_REQ req;
    trt_GetSnapshotUri_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_trt_GetSnapshotUri(&device->dev, &req, &res))
        return -2;
    strncpy(out_uri, res.Uri, out_size - 1);
    out_uri[out_size - 1] = '\0';
    return 0;
}

int nop_onvif_get_snapshot(nop_onvif_device_t *device, const char *profile_token,
                           unsigned char **out_buf, int *out_len)
{
    if (!device || !profile_token || !out_buf || !out_len)
        return -1;
    return GetSnapshot(&device->dev, profile_token, out_buf, out_len) ? 0 : -2;
}

void nop_onvif_free_buffer(void *buf)
{
    if (buf)
        FreeBuff(buf);
}

int nop_onvif_get_video_source_count(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!GetVideoSources(&device->dev))
        return -2;
    for (VideoSourceList *v = device->dev.v_src; v; v = v->next)
        count++;
    return count;
}

/* ======================================================================== */
/* PTZ                                                                      */
/* ======================================================================== */

int nop_onvif_ptz_get_node_count(nop_onvif_device_t *device)
{
    int count = 0;
    if (!device)
        return -1;
    if (!GetNodes(&device->dev))
        return -2;
    for (PTZNodeList *n = device->dev.ptz_node; n; n = n->next)
        count++;
    return count;
}

static void fill_ptz_speed(onvif_PTZSpeed *speed, float pan, float tilt, float zoom)
{
    memset(speed, 0, sizeof(*speed));
    speed->PanTiltFlag = 1;
    speed->PanTilt.x = pan;
    speed->PanTilt.y = tilt;
    speed->ZoomFlag = 1;
    speed->Zoom.x = zoom;
}

static void fill_ptz_vector(onvif_PTZVector *vector, float pan, float tilt, float zoom)
{
    memset(vector, 0, sizeof(*vector));
    vector->PanTiltFlag = 1;
    vector->PanTilt.x = pan;
    vector->PanTilt.y = tilt;
    vector->ZoomFlag = 1;
    vector->Zoom.x = zoom;
}

int nop_onvif_ptz_continuous_move(nop_onvif_device_t *device, const char *profile_token,
                                  float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_ContinuousMove_REQ req;
    ptz_ContinuousMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_speed(&req.Velocity, pan, tilt, zoom);
    return onvif_ptz_ContinuousMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_stop(nop_onvif_device_t *device, const char *profile_token,
                       int stop_pan_tilt, int stop_zoom)
{
    if (!device || !profile_token) return -1;
    ptz_Stop_REQ req;
    ptz_Stop_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    req.PanTiltFlag = 1; req.PanTilt = stop_pan_tilt ? TRUE : FALSE;
    req.ZoomFlag = 1;    req.Zoom = stop_zoom ? TRUE : FALSE;
    return onvif_ptz_Stop(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_relative_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_RelativeMove_REQ req;
    ptz_RelativeMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_vector(&req.Translation, pan, tilt, zoom);
    return onvif_ptz_RelativeMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_absolute_move(nop_onvif_device_t *device, const char *profile_token,
                                float pan, float tilt, float zoom)
{
    if (!device || !profile_token) return -1;
    ptz_AbsoluteMove_REQ req;
    ptz_AbsoluteMove_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    fill_ptz_vector(&req.Position, pan, tilt, zoom);
    return onvif_ptz_AbsoluteMove(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_home(nop_onvif_device_t *device, const char *profile_token)
{
    if (!device || !profile_token) return -1;
    ptz_GotoHomePosition_REQ req;
    ptz_GotoHomePosition_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    return onvif_ptz_GotoHomePosition(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_home_speed(nop_onvif_device_t *device,
                                  const char *profile_token, float speed)
{
    if (!device || !profile_token) return -1;
    ptz_GotoHomePosition_REQ req;
    ptz_GotoHomePosition_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (speed > 0.0f) {
        if (speed > 1.0f) speed = 1.0f;
        req.SpeedFlag       = 1;
        req.Speed.PanTiltFlag = 1;
        req.Speed.PanTilt.x = speed;
        req.Speed.PanTilt.y = speed;
        req.Speed.ZoomFlag  = 1;
        req.Speed.Zoom.x    = speed;
    }
    return onvif_ptz_GotoHomePosition(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_goto_preset(nop_onvif_device_t *device, const char *profile_token,
                              const char *preset_token)
{
    if (!device || !profile_token || !preset_token) return -1;
    ptz_GotoPreset_REQ req;
    ptz_GotoPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetToken, preset_token, sizeof(req.PresetToken) - 1);
    return onvif_ptz_GotoPreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_set_preset(nop_onvif_device_t *device, const char *profile_token,
                             const char *preset_name)
{
    if (!device || !profile_token) return -1;
    ptz_SetPreset_REQ req;
    ptz_SetPreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (preset_name && preset_name[0]) {
        req.PresetNameFlag = 1;
        strncpy(req.PresetName, preset_name, sizeof(req.PresetName) - 1);
    }
    return onvif_ptz_SetPreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_remove_preset(nop_onvif_device_t *device, const char *profile_token,
                                const char *preset_token)
{
    if (!device || !profile_token || !preset_token) return -1;
    ptz_RemovePreset_REQ req;
    ptz_RemovePreset_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    strncpy(req.PresetToken, preset_token, sizeof(req.PresetToken) - 1);
    return onvif_ptz_RemovePreset(&device->dev, &req, &res) ? 0 : -2;
}

int nop_onvif_ptz_get_presets(nop_onvif_device_t *device, const char *profile_token,
                              nop_onvif_preset_t *out_array, int max)
{
    if (!device || !profile_token || !out_array || max <= 0)
        return -1;
    ptz_GetPresets_REQ req;
    ptz_GetPresets_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.ProfileToken, profile_token, sizeof(req.ProfileToken) - 1);
    if (!onvif_ptz_GetPresets(&device->dev, &req, &res))
        return -2;

    int count = 0;
    for (PTZPresetList *node = res.PTZPresets; node && count < max; node = node->next) {
        memset(&out_array[count], 0, sizeof(out_array[count]));
        strncpy(out_array[count].token, node->PTZPreset.token, sizeof(out_array[count].token) - 1);
        strncpy(out_array[count].name, node->PTZPreset.Name, sizeof(out_array[count].name) - 1);
        count++;
    }
    onvif_free_PTZPresets(&res.PTZPresets);
    return count;
}

/* ======================================================================== */
/* Imaging                                                                  */
/* ======================================================================== */

int nop_onvif_get_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   nop_onvif_imaging_t *out)
{
    if (!device || !video_source_token || !out)
        return -1;
    img_GetImagingSettings_REQ req;
    img_GetImagingSettings_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);
    if (!onvif_img_GetImagingSettings(&device->dev, &req, &res))
        return -2;

    const onvif_ImagingSettings *s = &res.ImagingSettings;
    memset(out, 0, sizeof(*out));
    out->has_brightness       = s->BrightnessFlag;       out->brightness       = s->Brightness;
    out->has_contrast         = s->ContrastFlag;         out->contrast         = s->Contrast;
    out->has_color_saturation = s->ColorSaturationFlag;  out->color_saturation = s->ColorSaturation;
    out->has_sharpness        = s->SharpnessFlag;        out->sharpness        = s->Sharpness;
    return 0;
}

int nop_onvif_set_imaging_settings(nop_onvif_device_t *device, const char *video_source_token,
                                   const nop_onvif_imaging_t *in)
{
    if (!device || !video_source_token || !in)
        return -1;
    img_SetImagingSettings_REQ req;
    img_SetImagingSettings_RES res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    strncpy(req.VideoSourceToken, video_source_token, sizeof(req.VideoSourceToken) - 1);

    onvif_ImagingSettings *s = &req.ImagingSettings;
    if (in->has_brightness)       { s->BrightnessFlag = 1;      s->Brightness = in->brightness; }
    if (in->has_contrast)         { s->ContrastFlag = 1;        s->Contrast = in->contrast; }
    if (in->has_color_saturation) { s->ColorSaturationFlag = 1; s->ColorSaturation = in->color_saturation; }
    if (in->has_sharpness)        { s->SharpnessFlag = 1;       s->Sharpness = in->sharpness; }
    return onvif_img_SetImagingSettings(&device->dev, &req, &res) ? 0 : -2;
}

/* ======================================================================== */
/* Events (pull-point)                                                      */
/* ======================================================================== */

int nop_onvif_events_create_pullpoint(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return CreatePullPointSubscription(&device->dev) ? 0 : -2;
}

int nop_onvif_events_pull(nop_onvif_device_t *device, int timeout_s, int max_messages)
{
    if (!device) return -1;
    if (timeout_s <= 0)   timeout_s = 1;
    if (max_messages <= 0) max_messages = 10;

    tev_PullMessages_RES res;
    memset(&res, 0, sizeof(res));
    if (!PullMessages(&device->dev, timeout_s, max_messages, &res))
        return -2;

    int count = 0;
    for (NotificationMessageList *m = res.NotifyMessages; m; m = m->next)
        count++;
    onvif_free_NotificationMessages(&res.NotifyMessages);
    return count;
}

int nop_onvif_events_unsubscribe(nop_onvif_device_t *device)
{
    if (!device) return -1;
    return Unsubscribe(&device->dev) ? 0 : -2;
}

/* ======================================================================== */
/* Firmware / system maintenance                                            */
/* ======================================================================== */

int nop_onvif_firmware_upgrade(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return FirmwareUpgrade(&device->dev, filename) ? 0 : -2;
}

int nop_onvif_system_backup(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return SystemBackup(&device->dev, filename) ? 0 : -2;
}

int nop_onvif_system_restore(nop_onvif_device_t *device, const char *filename)
{
    if (!device || !filename) return -1;
    return SystemRestore(&device->dev, filename) ? 0 : -2;
}
