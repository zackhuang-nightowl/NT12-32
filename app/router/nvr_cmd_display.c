/***************************************************************************************
 *  nvr_cmd_display.c — display 出图域 handler:布局/映射/自定义窗/系统显示/通道状态/
 *  longPolling/设备能力/数字变焦。见 nvr_cmd_internal.h。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_streaming.h"  /* nvr_stream_recording_mask:RecordStatus */
#include "nvr_event.h"      /* nvr_evt_masks:Motion/Human/Face/Car 位图 */
#include <stdint.h>
#include "nvr_chan_status.h"
#include "nvr_gui_config.h"
#include "nvr_defaults.h"
#include "nvr_display_modes.h"  /* NVR_DISPLAY_MODES 单一档位来源 */
#include "nvr_playback.h"       /* setDeviceDisplayMode 切回 live 前先停回放 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *cmd_GUI_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int mode = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(a, "displayMode"));
    int page = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(a, "displayPage"));
    /* ★ 切回 liveView:先**停回放**(关回放独占解码器),再按 mode 重开 live 解码。
     *   回放模式期间 live 一直不解码,只有这里收到 setDeviceDisplayMode 才恢复 live。 */
    if (c->pb) nvr_playback_control(c->pb, "stop", 0, 0, NULL, NULL);
    nvr_preview_set_mode(c->pv, mode, page);
    /* 持久化到 GUI_CONFIG.json(开机据此出图);mode==0 为退出 Liveview 瞬时态,helper 内部不写。 */
    nvr_gui_config_set_display(mode, page);
    /* ★ 等图出了再回复:阻塞到**任一可见格出图**或最多 2s,LVGL 等待期间放加载动画,
     * 避免"回复了但画面还没出/还在黑闪"的体验。 */
    nvr_preview_wait_ready(c->pv, NVR_DEF_WAIT_READY_MS);
    return nvr_resp_result("OK");
}
char *cmd_GUI_getDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c; int m = NVR_DEF_GUI_MODE, pg = NVR_DEF_GUI_PAGE; nvr_gui_config_get_display(&m, &pg);  /* 从 GUI_CONFIG.json 读 */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "displayMode", m); cJSON_AddNumberToObject(o, "displayPage", pg);
    return nvr_resp_content(o);
}
char *cmd_GUI_setChannelMapping(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *arr = cJSON_GetObjectItem(a, "ChannelMapping");
    if (!cJSON_IsArray(arr)) return nvr_resp_result("input channel num exception");
    int n = cJSON_GetArraySize(arr), map[32]; if (n > 32) n = 32;
    for (int i = 0; i < n; i++) map[i] = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(arr, i));
    nvr_preview_set_mapping(c->pv, map, n);
    if (c->persist) nvr_chan_persist_set_mapping(c->persist, map, n);
    return nvr_resp_result("OK");
}
char *cmd_GUI_getChannelMapping(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; int map[32]; int n = c->persist ? nvr_chan_persist_get_mapping(c->persist, map, 32) : 0;
    cJSON *o = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(o, "ChannelMapping");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(map[i]));
    return nvr_resp_content(o);
}
char *cmd_GUI_setDeviceDisplayExt(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *arr = cJSON_GetObjectItem(a, "channels");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    nvr_pv_ext_t b[16]; if (n > 16) n = 16;
    for (int i = 0; i < n; i++) { cJSON *e = cJSON_GetArrayItem(arr, i);
        b[i].chn0 = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "channel")) - 1;
        b[i].x = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "x"));
        b[i].y = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "y"));
        b[i].w = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "w"));
        b[i].h = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "h"));
        const char *st = cJSON_GetStringValue(cJSON_GetObjectItem(e, "streamType"));
        b[i].stream = (st && !strcmp(st, "sub")) ? NVR_STREAM_SUB : NVR_STREAM_MAIN;
    }
    nvr_preview_set_ext(c->pv, n ? b : NULL, n);
    return nvr_resp_content(NULL);
}
char *cmd_GUI_getDeviceDisplayExt(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; nvr_pv_ext_t b[16]; int n = nvr_preview_get_ext(c->pv, b, 16);
    cJSON *o = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < n; i++) { cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "channel", b[i].chn0 + 1);
        cJSON_AddNumberToObject(e, "x", b[i].x); cJSON_AddNumberToObject(e, "y", b[i].y);
        cJSON_AddNumberToObject(e, "w", b[i].w); cJSON_AddNumberToObject(e, "h", b[i].h);
        cJSON_AddStringToObject(e, "streamType", b[i].stream == NVR_STREAM_SUB ? "sub" : "main");
        cJSON_AddItemToArray(arr, e); }
    return nvr_resp_content(o);
}
static int resolution_supported(const char *s)
{
    if (!s) return 0;
#define X(w, h, label) if (strcmp(s, label) == 0) return 1;
    NVR_DISPLAY_MODES(X)
#undef X
    return 0;
}
char *cmd_GUI_getSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char cur[32];
    if (c->settings)
        nvr_settings_get_str(c->settings, "display.resolution", cur, sizeof(cur), "1920x1080");
    else
        snprintf(cur, sizeof(cur), "1920x1080");
    cJSON *o = cJSON_CreateObject();
    cJSON *rl = cJSON_AddArrayToObject(o, "resolutionList");   /* 设备支持的输出档(单一来源) */
#define X(w, h, label) cJSON_AddItemToArray(rl, cJSON_CreateString(label));
    NVR_DISPLAY_MODES(X)
#undef X
    cJSON_AddStringToObject(o, "resolution", cur);             /* 当前记录/生效分辨率 */
    cJSON_AddNumberToObject(o, "displayOpacity", 255);
    cJSON_AddStringToObject(o, "fb", "fb1");
    return nvr_resp_content(o);
}
char *cmd_GUI_setSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *res = nvr_jstr(a, "resolution", NULL);
    cJSON *o = cJSON_CreateObject();
    if (!res || !resolution_supported(res)) {
        cJSON_AddStringToObject(o, "result", res ? "unsupported resolution" : "OK");
        cJSON_AddStringToObject(o, "fb", "fb1");
        return nvr_resp_content(o);
    }
    /* 记录(生效值由热切回调按实际降级回写覆盖) */
    if (c->settings) nvr_settings_set_str(c->settings, "display.resolution", res);
    /* 热切:切 HDMI 输出 + 重排出图 + 回写生效值 + 重启 LVGL(由 app 回调实现)。 */
    int w = 0, h = 0;
    if (c->on_set_resolution && sscanf(res, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
        c->on_set_resolution(c->disp_user, w, h);
    cJSON_AddStringToObject(o, "result", "OK");
    cJSON_AddStringToObject(o, "fb", "fb1");
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_getChannelStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(a, "channel"));
    int code = c->cm ? nvr_chan_status_code_of(c->cm, ch1 - 1) : 0;
    cJSON *o = cJSON_CreateObject(); cJSON_AddNumberToObject(o, "status", code);
    return nvr_resp_content(o);
}
/* longPolling:返回通道状态变化位图。gui 收到 ChannelStatusNotify!=0 → 重拉 getChannelStatus。 */
char *cmd_GUI_longPolling(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int refresh = a ? nvr_jbool(a, "refresh", 0) : 0;
    unsigned notify = c->cm ? nvr_chan_drain_notify(c->cm) : 0;
    /* refresh:true → 全量重同步:置所有(容量内)通道位,GUI 据此重拉每通道 getChannelStatus。 */
    if (refresh) {
        int cap = c->settings ? nvr_settings_get_int(c->settings, "system.capacity", 32) : 32;
        notify = (cap >= 32) ? 0xFFFFFFFFu : ((1u << cap) - 1u);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "ChannelStatusNotify", (double)notify);
    /* Motion/Human/Face/Car:32 位,bit chn=通道 chn+1 近期有该类事件(事件中枢按类型聚合)。 */
    uint32_t mo = 0, hu = 0, fa = 0, car = 0;
    if (c->eh) nvr_evt_masks(c->eh, &mo, &hu, &fa, &car);
    cJSON_AddNumberToObject(o, "MotionStatus", (double)mo);
    cJSON_AddNumberToObject(o, "FaceStatus", (double)fa);
    cJSON_AddNumberToObject(o, "HumanStatus", (double)hu);
    cJSON_AddNumberToObject(o, "CarStatus", (double)car);
    /* RecordStatus:32位,bit chn=通道 chn+1 正在录像(writer 开)。GUI 据此显示录像图标。 */
    unsigned rec = c->sm ? nvr_stream_recording_mask(c->sm) : 0;
    cJSON_AddNumberToObject(o, "RecordStatus", (double)rec);
    return nvr_resp_content(o);
}
/* 通道能力对象是否含某能力(capabilities 数组里有 name)。 */
static int caps_has(cJSON *e, const char *name)
{
    cJSON *caps = cJSON_GetObjectItem(e, "capabilities"); cJSON *it;
    if (!cJSON_IsArray(caps)) return 0;
    cJSON_ArrayForEach(it, caps) if (cJSON_IsString(it) && strcmp(it->valuestring, name) == 0) return 1;
    return 0;
}
char *cmd_X_NightOwl_getDeviceCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    /* 先建 channels(只含**有真实设备**的通道:status!=0),同时统计设备级 ptz/ai。 */
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    int any_ptz = 0, any_ai = 0;
    int list_n = 0; nvr_channel_t list[32];
    if (c->cm) list_n = nvr_chan_list(c->cm, list, 32);
    for (int i = 0; i < list_n; i++) {
        int ch1 = list[i].chn + 1;
        /* channel没有设备的不进 getDeviceCapabilities(status_code 0 = 空口/未添加)。 */
        if (!c->cm || nvr_chan_status_code_of(c->cm, list[i].chn) == 0) continue;
        /* 每通道能力:上线按设备取回(NOP=直接 getDeviceCapabilities;ONVIF=映射)写 DB。
         * 存的是设备自视角对象 → 聚合时覆盖 channel 为本机通道号。 */
        char caps_json[2048];
        cJSON *e = NULL;
        if (c->settings && nvr_settings_caps_get(c->settings, ch1, caps_json, sizeof(caps_json), NULL, 0) > 0)
            e = cJSON_Parse(caps_json);
        if (!e) e = cJSON_CreateObject();
        cJSON_DeleteItemFromObject(e, "channel");
        cJSON_AddNumberToObject(e, "channel", ch1);
        cJSON_DeleteItemFromObject(e, "_ai");   /* 内部 AI 明细不进 getDeviceCapabilities(走 AI_getChannelAICapabilities) */
        if (!cJSON_GetObjectItem(e, "signal")) cJSON_AddStringToObject(e, "signal", "IPC");
        if (caps_has(e, "ptz")) any_ptz = 1;
        if (caps_has(e, "ai"))  any_ai  = 1;
        cJSON_AddItemToArray(chs, e);
    }
    /* device 级能力(NT12-32 实际支持):核心 + BLE + AI;任一通道有 PTZ → 设备级也报 ptz。 */
    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON *dc = cJSON_AddArrayToObject(dev, "capabilities");
    cJSON_AddItemToArray(dc, cJSON_CreateString("displayMode"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("groupInPrimary"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("multiStorage"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("format"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("cloudRecording"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("bluetooth"));   /* BLE 配网 */
    if (any_ai)  cJSON_AddItemToArray(dc, cJSON_CreateString("ai"));
    if (any_ptz) cJSON_AddItemToArray(dc, cJSON_CreateString("ptz"));
    return nvr_resp_content(o);
}
/* AI_getChannelAICapabilities:该通道 AI 能力(objectDetection 类别 + ruledDetection 越线/区域入侵)。
 * ONVIF:取入库 camera_capability 里的内部 _ai(GetRuleOptions/GetSupportedRules 映射);
 * NOP:从 sensors[] 的 objectDetection.modes 提取类别。 */
char *cmd_AI_getChannelAICapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    /* NOP 设备:透传到相机的 AI_getChannelAICapabilities(设备自报,最全);ONVIF:本地映射(下方)。 */
    if (c->cm) {
        nvr_channel_t ch;
        if (nvr_chan_get(c->cm, ch1 - 1, &ch) == 0 && ch.backend == 0) {
            char *fwd = nvr_chan_dev_post(c->cm, ch1 - 1, "AI_getChannelAICapabilities", NULL);
            if (fwd) return fwd;   /* 透传应答直接返回 */
        }
    }
    char caps_json[2048]; cJSON *e = NULL;
    if (c->settings && nvr_settings_caps_get(c->settings, ch1, caps_json, sizeof(caps_json), NULL, 0) > 0)
        e = cJSON_Parse(caps_json);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "channel", ch1);
    cJSON *ai = e ? cJSON_GetObjectItem(e, "_ai") : NULL;

    /* objectDetection 类别 */
    cJSON *od = ai ? cJSON_GetObjectItem(ai, "objectDetection") : NULL;
    if (!od && e) {   /* NOP:从 sensors[] 里的 objectDetection.modes 取 */
        cJSON *sensors = cJSON_GetObjectItem(e, "sensors"), *s;
        cJSON_ArrayForEach(s, sensors) {
            const char *sn = cJSON_GetStringValue(cJSON_GetObjectItem(s, "sensor"));
            if (sn && strcmp(sn, "objectDetection") == 0) { od = cJSON_GetObjectItem(s, "modes"); break; }
        }
    }
    if (od) cJSON_AddItemToObject(o, "objectDetection", cJSON_Duplicate(od, 1));

    /* ruledDetection(越线/区域入侵) */
    cJSON *rd = ai ? cJSON_GetObjectItem(ai, "ruledDetection") : NULL;
    if (rd) cJSON_AddItemToObject(o, "ruledDetection", cJSON_Duplicate(rd, 1));

    if (e) cJSON_Delete(e);
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_setChannelZoomPan(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0   = nvr_jint(a, "channel", 1) - 1;         /* 1-based → 0-based */
    int enable = nvr_jbool(a, "enable", 1);
    int cx     = nvr_jint(a, "CenterPointX", 500);
    int cy     = nvr_jint(a, "CenterPointY", 500);
    int fx     = nvr_jint(a, "FocusPointX", 500);
    int fy     = nvr_jint(a, "FocusPointY", 500);
    int ratio  = nvr_jint(a, "ZoomRatio", 100);
    const char *result = "OK";
    if (c->pv && chn0 >= 0)
        nvr_preview_set_zoom(c->pv, chn0, enable, cx, cy, fx, fy, ratio, &cx, &cy, &ratio, &result);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "result", result);        /* OK / Exceeds the zoom range / Exceeds zoom capabilities */
    cJSON_AddNumberToObject(o, "CenterPointX", cx);       /* 实际生效值(被夹取后回填) */
    cJSON_AddNumberToObject(o, "CenterPointY", cy);
    cJSON_AddNumberToObject(o, "ZoomRatio", ratio);
    return nvr_resp_content(o);
}
char *cmd_X_NightOwl_getChannelZoomPan(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0 = nvr_jint(a, "channel", 1) - 1;
    int enable = 1, cx = 500, cy = 500, ratio = 100;
    if (c->pv && chn0 >= 0)
        nvr_preview_get_zoom(c->pv, chn0, &enable, &cx, &cy, &ratio);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enable", enable);
    cJSON_AddNumberToObject(o, "CenterPointX", cx);
    cJSON_AddNumberToObject(o, "CenterPointY", cy);
    cJSON_AddNumberToObject(o, "ZoomRatio", ratio);
    return nvr_resp_content(o);
}
