/***************************************************************************************
 *  nvr_cmd_p2p.c — NOP APP(经 TUTK agent/tunnel)远程媒体命令:
 *    getLiveCapabilities / startLiveStream / stopLiveStream / getSpeakerCapabilities /
 *    startSpeaker / stopSpeaker / buildTunnel。契约见 nop_api_doc/NOP_APP/*.txt。
 *
 *  直播:startLiveStream 返回 rtsp://iotc-tunnel:8554/live/chN;host=iotc-tunnel 由 APP 端
 *  P2PTunnel 映射到本机端口。设备侧 RTSP 服务 = nvr_rtsp_live(streaming 已 feed 子码流),
 *  按 chN 选通道。对讲本机 127.0.0.1:7000，外网由 TUTK 映射 iotc-tunnel:7000。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_channel.h"
#include "nvr_rtsp_live.h"
#include "nvr_talk.h"
#include "nvr_dev_classify.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define NVR_TUNNEL_RTSP_PORT 8554   /* 与 profile.txt p2pProtocols "iotc-tunnel:8554" 一致 */

/* 在线通道(status!=0)→ channels[](1-based);可选 channelsStatus[]。返回个数。 */
static int add_online_channels(const nvr_cmd_ctx_t *c, cJSON *chs, cJSON *status_arr)
{
    if (!c || !c->cm) return 0;
    nvr_channel_t list[32];
    int n = nvr_chan_list(c->cm, list, 32), cnt = 0;
    for (int i = 0; i < n; i++) {
        if (nvr_chan_status_code_of(c->cm, list[i].chn) == 0) continue;
        int ch1 = list[i].chn + 1;
        if (chs) cJSON_AddItemToArray(chs, cJSON_CreateNumber(ch1));
        cnt++;
        if (status_arr) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "channel", ch1);
            cJSON_AddStringToObject(e, "name", list[i].name[0] ? list[i].name : "");
            cJSON_AddStringToObject(e, "signalStatus", "good");
            cJSON_AddItemToArray(status_arr, e);
        }
    }
    return cnt;
}

char *cmd_getLiveCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    cJSON_AddItemToArray(proto, cJSON_CreateString("rtsp-iotc-tunnel"));
    cJSON *st = cJSON_AddArrayToObject(o, "streamType");
    const char *types[] = { "video", "subVideo", "audioAndVideo", "audioAndSubVideo" };
    for (int i = 0; i < 4; i++) cJSON_AddItemToArray(st, cJSON_CreateString(types[i]));
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    cJSON *cst = cJSON_AddArrayToObject(o, "channelsStatus");
    add_online_channels(c, chs, cst);
    return nvr_resp_content(o);
}

char *cmd_startLiveStream(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    if (c->cm && nvr_chan_status_code_of(c->cm, chn0) == 0)
        return nvr_resp_err("channel_offline");
    /* streaming 常拉且 stream_router 已 feed nvr_rtsp_live;此处仅选定要输出的通道。 */
    nvr_rtsp_live_select(chn0);
    char url[96];
    snprintf(url, sizeof(url), "rtsp://iotc-tunnel:%d/live/ch%d", NVR_TUNNEL_RTSP_PORT, ch1);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", url);
    return nvr_resp_content(o);
}

char *cmd_stopLiveStream(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    nvr_rtsp_live_select(-1);
    return nvr_resp_ok();
}

char *cmd_getSpeakerCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    cJSON_AddItemToArray(proto, cJSON_CreateString("https"));
    cJSON_AddItemToArray(proto, cJSON_CreateString("iotc-av"));
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    add_online_channels(c, chs, NULL);
    cJSON *fmts = cJSON_AddArrayToObject(o, "supportAudioFormats");
    const char *codecs[] = { "g711u", "pcm" };
    for (int i = 0; i < 2; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "codec", codecs[i]);
        cJSON_AddNumberToObject(f, "sampleRate", 8000);
        cJSON_AddNumberToObject(f, "dataBit", 16);
        cJSON_AddStringToObject(f, "audioChannel", "mono");
        cJSON_AddItemToArray(fmts, f);
    }
    return nvr_resp_content(o);
}

static const char *prefer_proto(cJSON *a)
{
    cJSON *arr = cJSON_GetObjectItem(a, "preferProtocol");
    if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
        const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(arr, 0));
        if (s && s[0]) return s;
    }
    if (cJSON_IsString(arr)) return arr->valuestring;
    return "https";
}

char *cmd_startSpeaker(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    if (!c || !c->talk || !c->cm) return nvr_resp_err("not_ready");
    nvr_channel_t ch;
    if (nvr_chan_get(c->cm, chn0, &ch) != 0 || !ch.onvif_ip[0])
        return nvr_resp_err("channel_offline");
    const char *codec = "g711u";
    cJSON *fmt = cJSON_GetObjectItem(a, "audioFormat");
    if (cJSON_IsObject(fmt)) {
        const char *cs = nvr_jstr(fmt, "codec", "g711u");
        if (cs && cs[0]) codec = cs;
    }
    (void)prefer_proto(a);
    char url[128];
    if (nvr_talk_start(c->talk, chn0, ch.backend, ch.onvif_ip, ch.onvif_port,
                       ch.user, ch.pass, ch.video_source_token, codec,
                       url, (int)sizeof(url)) != 0)
        return nvr_resp_err("speaker_failed");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", url);
    return nvr_resp_content(o);
}

char *cmd_stopSpeaker(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 0);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    if (c && c->talk) nvr_talk_stop(c->talk, ch1 - 1);
    return nvr_resp_ok();
}

char *cmd_buildTunnel(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)c;
    const char *proto = nvr_jstr(a, "protocol", "iotc-tunnel");
    if (strcmp(proto, "iotc-tunnel") != 0) return nvr_resp_err("unsupported_protocol");
    /* ODC agent(P2PTunnelServer)自管隧道会话与端口鉴权;返回约定字段供 APP
     * P2PTunnelAgent_Attach_Connect 使用。 */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "username", "admin");
    cJSON_AddStringToObject(o, "password", "12345678");
    cJSON_AddNumberToObject(o, "iotc-channel", 1);
    return nvr_resp_content(o);
}
