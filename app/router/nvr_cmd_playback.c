/***************************************************************************************
 *  nvr_cmd_playback.c — 本机回放控制 handler(GUI_playbackControl)。
 *  驱动 nvr_playback 引擎(rsdk_play → mhal_vdec 上屏)。曾走 cap_gui_playback 空桩(只回显不放)。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_playback.h"
#include <string.h>

char *cmd_GUI_playbackControl(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->pb) return nvr_resp_err("no_playback");
    const char *action = nvr_jstr(a, "action", NULL);
    if (!action) return nvr_resp_err("invalid_param");

    int ch1        = nvr_jint(a, "channel", 0);            /* 1-based;0=沿用当前 */
    uint32_t start = (uint32_t)nvr_jint(a, "startTime", 0);/* UTC epoch;0=沿用/resume */
    const char *speed = nvr_jstr(a, "speed", NULL);
    const char *dir   = nvr_jstr(a, "direction", NULL);

    if (nvr_playback_control(c->pb, action, ch1, start, speed, dir) != 0)
        return nvr_resp_err("invalid_param");

    char status[16], sp[16], d[16]; uint32_t cur = 0;
    nvr_playback_get_status(c->pb, status, sp, d, &cur);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "speed", sp);
    cJSON_AddStringToObject(o, "direction", d);
    cJSON_AddNumberToObject(o, "timestamp", cur);
    return nvr_resp_content(o);
}

/* 进入回放布局:一进回放就**黑屏**(不再显示 LiveView),等 playbackControl 起播。
 * ★ liveView / playback 由接口切换:收到 setPlaybackMode 即进入**回放独占**模式——
 *   令 preview 置 display_mode=0(关所有 live 解码 + 通道上线不再重开 live),此后所有解码只属回放;
 *   只有收到 setDeviceDisplayMode 才切回 liveView(见 cmd_GUI_setDeviceDisplayMode)。 */
char *cmd_GUI_setPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!c->pb) return nvr_resp_err("no_playback");
    int dm = nvr_jint(a, "displayMode", 1);
    int list[16], n = 0;
    cJSON *chs = a ? cJSON_GetObjectItem(a, "channels") : NULL, *it;
    if (cJSON_IsArray(chs)) cJSON_ArrayForEach(it, chs) {
        if (n < 16 && cJSON_IsNumber(it)) list[n++] = (int)cJSON_GetNumberValue(it);
    }
    if (c->pv) nvr_preview_set_mode(c->pv, 0, 0);   /* 离开 LiveView:关所有 live 解码,阻止上线重开 */
    nvr_playback_set_mode(c->pb, dm, list, n);
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "result", "OK");
    return nvr_resp_content(o);
}
/* getPlaybackCapabilities（NOP_APP 文档）:
 *   protocol   —— 支持的回放协议(本机 RTSP-over-IOTC 隧道)
 *   streamType —— 支持的音视频流类型(主/子 + 音频)
 *   channels   —— 支持回放的通道(1-based);默认**全通道** 1..capacity(不限有无设备)。 */
char *cmd_getPlaybackCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();

    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    cJSON_AddItemToArray(proto, cJSON_CreateString("rtsp-iotc-tunnel"));

    cJSON *st = cJSON_AddArrayToObject(o, "streamType");
    cJSON_AddItemToArray(st, cJSON_CreateString("audioAndVideo"));
    cJSON_AddItemToArray(st, cJSON_CreateString("audioAndSubVideo"));

    int cap = c->settings ? nvr_settings_get_int(c->settings, "system.capacity", 32) : 32;
    if (cap < 1) cap = 32;
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    for (int ch1 = 1; ch1 <= cap; ch1++)   /* 全通道 1..capacity */
        cJSON_AddItemToArray(chs, cJSON_CreateNumber(ch1));
    return nvr_resp_content(o);
}

char *cmd_GUI_getPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; if (!c->pb) return nvr_resp_err("no_playback");
    int list[16], n = 0; int dm = nvr_playback_get_mode(c->pb, list, 16, &n);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "displayMode", dm);
    cJSON *arr = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(list[i]));
    return nvr_resp_content(o);
}
