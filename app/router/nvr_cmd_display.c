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
#include <stdlib.h>
#include <string.h>

char *cmd_GUI_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int mode = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(a, "displayMode"));
    int page = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(a, "displayPage"));
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
char *cmd_GUI_getSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return NULL;   /* TODO 待实现:查实际显示态(mhal 显示分辨率等)。无业务→返回 NULL→路由统一 501 notSupport */
}
char *cmd_GUI_setSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return NULL;   /* TODO 待实现:设实际显示。无业务→返回 NULL→路由 501 */
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
char *cmd_X_NightOwl_getDeviceCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON *dc = cJSON_AddArrayToObject(dev, "capabilities");
    cJSON_AddItemToArray(dc, cJSON_CreateString("displayMode"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("groupInPrimary"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("multiStorage"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("format"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("cloudRecording"));
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    int list_n = 0; nvr_channel_t list[32];
    if (c->cm) list_n = nvr_chan_list(c->cm, list, 32);
    for (int i = 0; i < list_n; i++) {
        int ch1 = list[i].chn + 1;
        /* 每通道能力:首次上线探测后写入 DB camera_capability(P2);未探到给安全默认。 */
        char caps_json[1024];
        cJSON *e = NULL;
        if (c->settings && nvr_settings_caps_get(c->settings, ch1, caps_json, sizeof(caps_json), NULL, 0) > 0)
            e = cJSON_Parse(caps_json);
        if (!e) e = cJSON_CreateObject();
        if (!cJSON_GetObjectItem(e, "channel")) cJSON_AddNumberToObject(e, "channel", ch1);
        if (!cJSON_GetObjectItem(e, "signal"))  cJSON_AddStringToObject(e, "signal", "IPC");
        if (!cJSON_GetObjectItem(e, "capabilities")) {
            cJSON *cc = cJSON_AddArrayToObject(e, "capabilities");
            cJSON_AddItemToArray(cc, cJSON_CreateString("cloudRecording"));
        }
        cJSON_AddItemToArray(chs, e);
    }
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
