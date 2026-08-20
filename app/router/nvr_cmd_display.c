/***************************************************************************************
 *  nvr_cmd_display.c — display 出图域 handler:布局/映射/自定义窗/系统显示/通道状态/
 *  longPolling/设备能力/数字变焦。见 nvr_cmd_internal.h。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_streaming.h"  /* nvr_stream_recording_mask:RecordStatus */
#include "nvr_event.h"      /* nvr_evt_masks:Motion/Human/Face/Car 位图 */
#include "nvr_gui_notify.h"
#include <stdint.h>
#include "nvr_chan_status.h"
#include "nvr_gui_config.h"
#include "nvr_defaults.h"
#include "nvr_display_modes.h"  /* NVR_DISPLAY_MODES 单一档位来源 */
#include "nvr_playback.h"       /* setDeviceDisplayMode 切回 live 前先停回放 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

char *cmd_GUI_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int mode = NVR_DEF_GUI_MODE, page = NVR_DEF_GUI_PAGE;
    nvr_gui_config_get_display(&mode, &page);
    if (nvr_jhas(a, "displayMode")) mode = nvr_jint(a, "displayMode", mode);
    if (nvr_jhas(a, "displayPage")) page = nvr_jint(a, "displayPage", page);
    if (page < 1) page = 1;
    /* App 可选字段:轮巡秒数 / 事件弹窗 / 首页首格通道。文档允许不支持 autoSelectEventCamera。 */
    if (c->settings) {
        if (nvr_jhas(a, "firstView"))
            nvr_settings_set_int(c->settings, "display.first_view", nvr_jint(a, "firstView", 1));
        if (nvr_jhas(a, "autoChangeCamera"))
            nvr_settings_set_int(c->settings, "display.auto_change", nvr_jint(a, "autoChangeCamera", -1));
        if (nvr_jhas(a, "autoSelectEventCamera"))
            nvr_settings_set_int(c->settings, "display.auto_event", nvr_jint(a, "autoSelectEventCamera", 0));
    }
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
    (void)a;
    int m = NVR_DEF_GUI_MODE, pg = NVR_DEF_GUI_PAGE;
    nvr_gui_config_get_display(&m, &pg);
    int modes[32];
    int nm = nvr_gui_config_get_live_modes(modes, 32);
    int first = c->settings ? nvr_settings_get_int(c->settings, "display.first_view", 1) : 1;
    int auto_ch = c->settings ? nvr_settings_get_int(c->settings, "display.auto_change", -1) : -1;
    int auto_ev = c->settings ? nvr_settings_get_int(c->settings, "display.auto_event", 0) : 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "displayMode", m);
    cJSON_AddNumberToObject(o, "displayPage", pg);
    cJSON *arr = cJSON_AddArrayToObject(o, "allDisplayModes");
    for (int i = 0; i < nm; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(modes[i]));
    cJSON_AddNumberToObject(o, "firstView", first > 0 ? first : 1);
    cJSON_AddNumberToObject(o, "autoChangeCamera", auto_ch);
    cJSON_AddNumberToObject(o, "autoSelectEventCamera", auto_ev);
    return nvr_resp_content(o);
}
char *cmd_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    return cmd_GUI_setDeviceDisplayMode(a, c);
}
char *cmd_getDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    return cmd_GUI_getDeviceDisplayMode(a, c);
}
char *cmd_GUI_setChannelMapping(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *arr = cJSON_GetObjectItem(a, "ChannelMapping");
    if (!cJSON_IsArray(arr)) return nvr_resp_result("input channel num exception");
    int n = cJSON_GetArraySize(arr), map[32]; if (n > 32) n = 32;
    for (int i = 0; i < n; i++) map[i] = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(arr, i));
    nvr_preview_set_mapping(c->pv, map, n);
    if (c->persist) nvr_chan_persist_set_mapping(c->persist, map, n);
    nvr_preview_wait_ready(c->pv, NVR_DEF_WAIT_READY_MS);
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
    if (!cJSON_IsArray(arr)) return nvr_resp_err("Parameter Error");
    int n = cJSON_GetArraySize(arr);
    if (n > 16) return nvr_resp_err("Exceeded Device Capability Error");
    nvr_pv_ext_t b[16];
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(e)) return nvr_resp_err("Parameter Error");
        int ch1 = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "channel"));
        int x = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "x"));
        int y = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "y"));
        int w = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "w"));
        int h = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "h"));
        const char *st = cJSON_GetStringValue(cJSON_GetObjectItem(e, "streamType"));
        if (ch1 < 1 || ch1 > 32) return nvr_resp_err("Parameter Error");
        if (x < 0 || x > 1000 || y < 0 || y > 1000) return nvr_resp_err("Parameter Error");
        if (w <= 0 || w > 1000 || h <= 0 || h > 1000) return nvr_resp_err("Parameter Error");
        if (!st || (strcmp(st, "sub") != 0 && strcmp(st, "main") != 0))
            return nvr_resp_err("Parameter Error");
        b[i].chn0 = ch1 - 1;
        b[i].x = x; b[i].y = y; b[i].w = w; b[i].h = h;
        b[i].stream = (strcmp(st, "sub") == 0) ? NVR_STREAM_SUB : NVR_STREAM_MAIN;
    }
    int rc = nvr_preview_set_ext(c->pv, n ? b : NULL, n);
    if (rc == -2) return nvr_resp_err("Exceeded Device Capability Error");
    if (rc != 0)  return nvr_resp_err("Unknow Error");
    return nvr_resp_ok();
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
/* longPolling:规范 — 仅回**相对进入本次等待前**有变化的字段;refresh:true 全量同步。
 * 有 ChannelStatusNotify / 事件位 / RecordStatus / APPNotifySetupStatus 变化则立刻回;
 * 否则挂起最多 25s(超时 content 可为空)。 */
static void lp_add_u32_if(cJSON *o, const char *key, uint32_t val, int emit)
{
    if (emit) cJSON_AddNumberToObject(o, key, (double)val);
}
static int lp_masks_eq(const nvr_evt_mask_set_t *a, const nvr_evt_mask_set_t *b)
{
    return a->motion == b->motion && a->human == b->human && a->face == b->face &&
           a->car == b->car && a->animal == b->animal && a->package == b->package &&
           a->linecross == b->linecross && a->field == b->field;
}
/* 八类事件电平位图全量落 JSON(GUI_longPolling.txt 8 个 *Status 字段)。 */
static void lp_add_event_masks(cJSON *o, const nvr_evt_mask_set_t *m)
{
    lp_add_u32_if(o, "MotionStatus",         m->motion,    1);
    lp_add_u32_if(o, "FaceStatus",           m->face,      1);
    lp_add_u32_if(o, "HumanStatus",          m->human,     1);
    lp_add_u32_if(o, "CarStatus",            m->car,       1);
    lp_add_u32_if(o, "AnimalStatus",         m->animal,    1);
    lp_add_u32_if(o, "PackageStatus",        m->package,   1);
    lp_add_u32_if(o, "LineCrossStatus",      m->linecross, 1);
    lp_add_u32_if(o, "FieldIntrusionStatus", m->field,     1);
}

char *cmd_GUI_longPolling(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int refresh = a ? nvr_jbool(a, "refresh", 0) : 0;
    unsigned notify = 0;
    nvr_evt_mask_set_t m0 = {0}, m = {0};
    uint32_t rec0 = 0, rec = 0;
    int setup0 = nvr_gui_setup_gen();
    int setup_has = 0, setup_now = 0;

    if (c->eh) nvr_evt_masks(c->eh, &m0);
    rec0 = c->sm ? nvr_stream_recording_mask(c->sm) : 0;
    setup_now = nvr_gui_get_setup_status(&setup_has);

    if (refresh) {
        int cap = c->settings ? nvr_settings_get_int(c->settings, "system.capacity", 32) : 32;
        notify = (cap >= 32) ? 0xFFFFFFFFu : ((1u << cap) - 1u);
        (void)nvr_chan_drain_notify(c->cm);
    } else {
        int wait_s = a ? nvr_jint(a, "timeout", 25) : 25;
        if (wait_s < 1) wait_s = 1;
        if (wait_s > 30) wait_s = 30;
        notify = c->cm ? nvr_chan_drain_notify(c->cm) : 0;
        if (!notify && nvr_gui_setup_gen() == setup0) {
            struct timespec ts;
            long t0, deadline;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            t0 = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
            deadline = t0 + (long)wait_s * 1000L;
            while (!notify) {
                long now, left;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                now = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
                left = deadline - now;
                if (left <= 0) break;
                if (left > 200) left = 200;
                notify = c->cm ? nvr_chan_wait_notify(c->cm, (int)left) : 0;
                if (c->eh) nvr_evt_masks(c->eh, &m);
                rec = c->sm ? nvr_stream_recording_mask(c->sm) : 0;
                if (notify || !lp_masks_eq(&m, &m0) || rec != rec0)
                    break;
                if (nvr_gui_setup_gen() != setup0)
                    break;
            }
        }
    }
    if (c->eh) nvr_evt_masks(c->eh, &m);
    rec = c->sm ? nvr_stream_recording_mask(c->sm) : 0;
    setup_now = nvr_gui_get_setup_status(&setup_has);

    cJSON *o = cJSON_CreateObject();
    if (refresh) {
        lp_add_u32_if(o, "ChannelStatusNotify", notify, 1);
        lp_add_event_masks(o, &m);            /* 8 类事件位全量 */
        lp_add_u32_if(o, "RecordStatus", rec, 1);
        if (setup_has) cJSON_AddNumberToObject(o, "APPNotifySetupStatus", setup_now);
    } else {
        /* ChannelStatusNotify 是**边沿**(drain 消费),按变化回。 */
        lp_add_u32_if(o, "ChannelStatusNotify", notify, notify != 0);
        /* 事件位/录像位是**电平/衰减态**:longPolling 无客户端游标是无状态的,若只在
         * "相对本次进入前有变化" 才回,事件在两次 poll 之间衰减清零(icon decay)会丢失
         * "事件结束" 这次跳变 → GUI 图标永不消失。故电平态**恒回当前值**保持同步。 */
        lp_add_event_masks(o, &m);            /* 8 类事件位全量 */
        lp_add_u32_if(o, "RecordStatus", rec, 1);
        if (setup_has && nvr_gui_setup_gen() != setup0)
            cJSON_AddNumberToObject(o, "APPNotifySetupStatus", setup_now);
    }
    return nvr_resp_content(o);
}

/* App 向导:0 BLE 配对页 / 1 P2P 配对页 / 2 关向导进 liveview / 3 关登录窗。
 * 写入后立刻唤醒 GUI_longPolling,GUI 按 APPNotifySetupStatus 切页。 */
char *cmd_notify_appSetupStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!nvr_jhas(a, "status")) return nvr_resp_err("invalid_param");
    int status = nvr_jint(a, "status", 0);
    if (status < 0 || status > 3) return nvr_resp_err("invalid_param");
    nvr_gui_set_setup_status(status);
    if (c && c->cm) nvr_chan_poke_longpoll(c->cm);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "func", "notify_appSetupStatus");
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
        /* 每通道能力:NVR 只从 DB 聚合(收集在上线/添加时一次性完成,见 nvr_channel.c —
         * NOP=设备自报透传;其余 ONVIF=经 nop 映射收集)。存的是设备自视角对象 → 覆盖 channel。 */
        cJSON *e = NULL;
        char caps_json[2048];
        if (c->settings && nvr_settings_caps_get(c->settings, list[i].chn, caps_json, sizeof(caps_json), NULL, 0) > 0)  /* 0-based key */
            e = cJSON_Parse(caps_json);
        if (!e) e = cJSON_CreateObject();
        cJSON_DeleteItemFromObject(e, "channel");
        cJSON_AddNumberToObject(e, "channel", ch1);
        cJSON_DeleteItemFromObject(e, "_ai");             /* 内部 AI 明细走 AI_getChannelAICapabilities */
        cJSON_DeleteItemFromObject(e, "videoSourceToken"); /* map 内部字段,不入规范应答 */
        if (!cJSON_GetObjectItem(e, "signal")) cJSON_AddStringToObject(e, "signal", "IPC");
        if (caps_has(e, "ptz")) any_ptz = 1;
        if (caps_has(e, "ai"))  any_ai  = 1;
        cJSON_AddItemToArray(chs, e);
    }
    /* device 级能力(NT12-32 实际支持):核心 + BLE + AI;任一通道有 PTZ → 设备级也报 ptz。 */
    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON *dc = cJSON_AddArrayToObject(dev, "capabilities");
    cJSON_AddItemToArray(dc, cJSON_CreateString("displayMode"));
    /* 有空闲 LAN 槽才报可被 App attach；加满后不再报(AttachToMaster_InLan)。 */
    {
        int poe_n = 16, lan_n = 16, free_lan = 0;
        nvr_gui_config_get_channels(&poe_n, &lan_n);
        int lan_cap = NVR_IP_CH_BASE + lan_n;
        if (lan_cap > NVR_MAX_CH) lan_cap = NVR_MAX_CH;
        nvr_channel_t list[NVR_MAX_CH];
        int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
        int occ[NVR_MAX_CH] = {0};
        for (int i = 0; i < n; i++)
            if (list[i].enabled && list[i].chn >= 0 && list[i].chn < NVR_MAX_CH)
                occ[list[i].chn] = 1;
        for (int i = NVR_IP_CH_BASE; i < lan_cap; i++) if (!occ[i]) free_lan++;
        if (free_lan > 0)
            cJSON_AddItemToArray(dc, cJSON_CreateString("groupInPrimary"));
    }
    cJSON_AddItemToArray(dc, cJSON_CreateString("multiStorage"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("format"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("cloudRecording"));
    cJSON_AddItemToArray(dc, cJSON_CreateString("snooze"));
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
    /* NOP:透传相机自报;其余(ONVIF):读 DB 里收集好的 _ai(经 nop 映射,见 nvr_channel.c)。 */
    if (c->cm) {
        nvr_channel_t ch;
        if (nvr_chan_get(c->cm, ch1 - 1, &ch) == 0 && ch.backend == 0) {
            char *fwd = nvr_chan_dev_post(c->cm, ch1 - 1, "AI_getChannelAICapabilities", NULL);
            if (fwd) return fwd;   /* NOP:透传应答直接返回 */
        }
    }
    char caps_json[2048]; cJSON *e = NULL;
    if (c->settings && nvr_settings_caps_get(c->settings, ch1 - 1, caps_json, sizeof(caps_json), NULL, 0) > 0)  /* 0-based key */
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
/* ZoomRatio: 接口 0~1000，100=1.00x。channel / 坐标允许数字或数字串。 */
static int zoom_jnum(cJSON *a, const char *k, int d)
{
    const cJSON *v = a ? cJSON_GetObjectItem(a, k) : NULL;
    if (!v) return d;
    double x = 0;
    if (cJSON_IsNumber(v)) x = v->valuedouble;
    else if (cJSON_IsString(v) && v->valuestring) x = atof(v->valuestring);
    else return d;
    return (int)(x + (x >= 0 ? 0.5 : -0.5));
}

char *cmd_X_NightOwl_setChannelZoomPan(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int chn0   = zoom_jnum(a, "channel", 1) - 1;         /* 1-based → 0-based */
    int enable = nvr_jbool(a, "enable", 1);
    int cx     = zoom_jnum(a, "CenterPointX", 500);
    int cy     = zoom_jnum(a, "CenterPointY", 500);
    int fx     = zoom_jnum(a, "FocusPointX", 500);
    int fy     = zoom_jnum(a, "FocusPointY", 500);
    int ratio  = zoom_jnum(a, "ZoomRatio", 100);
    const char *result = "OK";
    if (!c->pv) return nvr_resp_err("preview_not_ready");
    if (chn0 < 0) return nvr_resp_err("invalid_channel");
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
    int chn0 = zoom_jnum(a, "channel", 1) - 1;
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
