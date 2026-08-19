/***************************************************************************************
 *  nvr_cmd_p2p.c — NOP APP(经 TUTK agent/tunnel)远程媒体命令:
 *    getLiveCapabilities / startLiveStream / stopLiveStream /
 *    startPlayback / stopPlayback / getSpeakerCapabilities /
 *    startSpeaker / stopSpeaker / buildTunnel。
 *
 *  直播/回放 URL host 必须是 iotc-tunnel;端口取本机实际 RTSP 监听口。
 *    live:      rtsp://iotc-tunnel:<port>/ch{N}_{stream}.264  (N=0-based)
 *               仅音频: .../ch{N}_audio
 *    playback:  rtsp://iotc-tunnel:<port>/playback/<startTime>
 ***************************************************************************************/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_channel.h"
#include "nvr_streaming.h"
#include "nvr_rtsp_live.h"
#include "nvr_talk.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
    int stream;
    int want_video;
    int want_audio;
    int ok;
} stream_sel_t;

static stream_sel_t parse_stream_type(const char *st)
{
    stream_sel_t r = { 0, 1, 1, 1 };
    if (!st || !st[0] || strcmp(st, "audioAndVideo") == 0) return r;
    if (strcmp(st, "video") == 0) { r.want_audio = 0; return r; }
    if (strcmp(st, "subVideo") == 0) { r.stream = 1; r.want_audio = 0; return r; }
    if (strcmp(st, "audioAndSubVideo") == 0 || strcmp(st, "subAudioAndVideo") == 0) {
        r.stream = 1; return r;
    }
    if (strcmp(st, "audio") == 0) { r.want_video = 0; return r; }
    if (strcmp(st, "subAudio") == 0) { r.stream = 1; r.want_video = 0; return r; }
    r.ok = 0;
    return r;
}

static int proto_live_ok(const char *p)
{
    return p && strcmp(p, "rtsp-iotc-tunnel") == 0 && nvr_rtsp_live_port() > 0;
}

static int proto_pb_ok(const char *p)
{
    /* 回放协议注明 hls-lan 不支持;本机无 IOTC-AV 推流。 */
    return p && strcmp(p, "rtsp-iotc-tunnel") == 0 && nvr_rtsp_live_port() > 0;
}

static const char *pick_prefer(cJSON *a, int (*ok)(const char *))
{
    cJSON *arr = a ? cJSON_GetObjectItem(a, "preferProtocol") : NULL;
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        if (n > 0) {
            cJSON *it;
            cJSON_ArrayForEach(it, arr) {
                const char *s = cJSON_GetStringValue(it);
                if (s && ok(s)) return s;
            }
            return NULL;
        }
    } else if (cJSON_IsString(arr) && arr->valuestring && arr->valuestring[0]) {
        return ok(arr->valuestring) ? arr->valuestring : NULL;
    }
    return ok("rtsp-iotc-tunnel") ? "rtsp-iotc-tunnel" : NULL;
}

static int parse_utc_parts(int y, int mo, int d, int h, int mi, int s, uint32_t *out)
{
    struct tm t;
    time_t ts;
    if (y < 1970 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60)
        return -1;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = 0;
    ts = timegm(&t);
    if (ts < 0) return -1;
    *out = (uint32_t)ts;
    return 0;
}

/* startTime 优先;否则 fileName: YYYYMMDDHH / YYYYMMDDHHMMSS / unix 数字串。 */
static int playback_start_utc(cJSON *a, uint32_t *out)
{
    if (nvr_jhas(a, "startTime")) {
        const cJSON *v = cJSON_GetObjectItem(a, "startTime");
        if (cJSON_IsNumber(v)) {
            *out = (uint32_t)v->valuedouble;
            return 0;
        }
        if (cJSON_IsString(v) && v->valuestring) {
            *out = (uint32_t)strtoul(v->valuestring, NULL, 10);
            return 0;
        }
        return -1;
    }
    const char *fn = nvr_jstr(a, "fileName", NULL);
    if (!fn || !fn[0]) return -1;
    int n = (int)strlen(fn), i;
    for (i = 0; i < n; i++) {
        if (!isdigit((unsigned char)fn[i])) return -1;
    }
    if (n == 14) {
        int y, mo, d, h, mi, s;
        if (sscanf(fn, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6)
            return -1;
        return parse_utc_parts(y, mo, d, h, mi, s, out);
    }
    if (n == 10) {
        int y = 0, mo = 0, d = 0, h = 0;
        if (sscanf(fn, "%4d%2d%2d%2d", &y, &mo, &d, &h) == 4 &&
            parse_utc_parts(y, mo, d, h, 0, 0, out) == 0)
            return 0;
        *out = (uint32_t)strtoul(fn, NULL, 10);
        return 0;
    }
    if (n == 8) {
        int y, mo, d;
        if (sscanf(fn, "%4d%2d%2d", &y, &mo, &d) != 3) return -1;
        return parse_utc_parts(y, mo, d, 0, 0, 0, out);
    }
    *out = (uint32_t)strtoul(fn, NULL, 10);
    return 0;
}

static const char *chan_signal_status(const nvr_cmd_ctx_t *c, int chn0, int code)
{
    nvr_ch_state_t st = (c && c->sm) ? nvr_stream_state(c->sm, chn0) : NVR_CH_IDLE;
    if (st == NVR_CH_PLAYING || code == 1) return "good";
    if (st == NVR_CH_NOSIGNAL) return "noSignal";
    if (code == 3) return "noSignal";
    return "bad";
}

static void p2p_fill_rand(unsigned char *buf, int n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    int ok = 0;
    if (fd >= 0) {
        if (read(fd, buf, (size_t)n) == n) ok = 1;
        close(fd);
    }
    if (!ok) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (int i = 0; i < n; i++) buf[i] = (unsigned char)(rand() & 0xFF);
    }
}

/* 在线通道(status!=0)→ channels[](1-based);可选 channelsStatus[]。返回个数。 */
static int add_online_channels(const nvr_cmd_ctx_t *c, cJSON *chs, cJSON *status_arr)
{
    if (!c || !c->cm) return 0;
    nvr_channel_t list[32];
    int n = nvr_chan_list(c->cm, list, 32), cnt = 0;
    for (int i = 0; i < n; i++) {
        int code = nvr_chan_status_code_of(c->cm, list[i].chn);
        if (code == 0) continue;
        int ch1 = list[i].chn + 1;
        if (chs) cJSON_AddItemToArray(chs, cJSON_CreateNumber(ch1));
        cnt++;
        if (status_arr) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "channel", ch1);
            cJSON_AddStringToObject(e, "name", list[i].name[0] ? list[i].name : "");
            cJSON_AddStringToObject(e, "signalStatus",
                                    chan_signal_status(c, list[i].chn, code));
            cJSON_AddItemToArray(status_arr, e);
        }
    }
    return cnt;
}

static void add_rtsp_proto_if_up(cJSON *proto)
{
    if (nvr_rtsp_live_port() > 0)
        cJSON_AddItemToArray(proto, cJSON_CreateString("rtsp-iotc-tunnel"));
}

static void add_av_stream_types(cJSON *st)
{
    static const char *types[] = {
        "video", "subVideo", "audio", "audioAndVideo", "audioAndSubVideo"
    };
    for (int i = 0; i < 5; i++)
        cJSON_AddItemToArray(st, cJSON_CreateString(types[i]));
}

char *cmd_getLiveCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    add_rtsp_proto_if_up(proto);
    cJSON *st = cJSON_AddArrayToObject(o, "streamType");
    add_av_stream_types(st);
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    cJSON *cst = cJSON_AddArrayToObject(o, "channelsStatus");
    add_online_channels(c, chs, cst);
    return nvr_resp_content(o);
}

char *cmd_startLiveStream(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    if (c->cm && nvr_chan_status_code_of(c->cm, chn0) == 0)
        return nvr_resp_err("channel_offline");
    if (!pick_prefer(a, proto_live_ok))
        return nvr_resp_err("unsupported_protocol");
    stream_sel_t sel = parse_stream_type(nvr_jstr(a, "streamType", "audioAndVideo"));
    if (!sel.ok) return nvr_resp_not_support();
    int port = nvr_rtsp_live_port();
    if (port <= 0) return nvr_resp_err("not_ready");
    nvr_rtsp_live_select_media(chn0, sel.stream, sel.want_video, sel.want_audio);
    char url[160];
    if (!sel.want_video && sel.want_audio)
        snprintf(url, sizeof(url), "rtsp://iotc-tunnel:%d/ch%d_audio", port, chn0);
    else
        snprintf(url, sizeof(url), "rtsp://iotc-tunnel:%d/ch%d_%d.264",
                 port, chn0, sel.stream);
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

char *cmd_startPlayback(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    if (!pick_prefer(a, proto_pb_ok))
        return nvr_resp_err("unsupported_protocol");
    stream_sel_t sel = parse_stream_type(nvr_jstr(a, "streamType", "audioAndVideo"));
    if (!sel.ok) return nvr_resp_not_support();
    uint32_t start = 0;
    if (playback_start_utc(a, &start) != 0)
        return nvr_resp_err("invalid_param");
    int port = nvr_rtsp_live_port();
    if (port <= 0) return nvr_resp_err("not_ready");
    (void)c;
    nvr_rtsp_pb_prepare(chn0, start, sel.stream, sel.want_audio, sel.want_video, NULL);
    char url[160];
    snprintf(url, sizeof(url), "rtsp://iotc-tunnel:%d/playback/%u", port, start);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", url);
    return nvr_resp_content(o);
}

char *cmd_stopPlayback(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    nvr_rtsp_pb_stop();
    return nvr_resp_ok();
}

char *cmd_getSpeakerCapabilities(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *proto = cJSON_AddArrayToObject(o, "protocol");
    /* 本机是 TCP+HTTP POST（文档 http/https）；无 IOTC-AV 推流，不报 iotc-av。 */
    cJSON_AddItemToArray(proto, cJSON_CreateString("https"));
    cJSON *chs = cJSON_AddArrayToObject(o, "channels");
    add_online_channels(c, chs, NULL);
    cJSON *fmts = cJSON_AddArrayToObject(o, "supportAudioFormats");
    struct { const char *codec; int bits; } codecs[] = {
        { "g711u", 8 }, { "g711a", 8 }, { "pcm", 16 }
    };
    for (int i = 0; i < 3; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "codec", codecs[i].codec);
        cJSON_AddNumberToObject(f, "sampleRate", 8000);
        cJSON_AddNumberToObject(f, "dataBit", codecs[i].bits);
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
    /* agent v3.x:username/password 与 P2PTunnel 联动;cgi 示例生成 32 位随机密 + iotc-channel=2 */
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    unsigned char rnd[32];
    p2p_fill_rand(rnd, (int)sizeof(rnd));
    char password[33];
    for (int i = 0; i < 32; i++)
        password[i] = charset[rnd[i] % (sizeof(charset) - 1)];
    password[32] = 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "username", "Tutk.com");
    cJSON_AddStringToObject(o, "password", password);
    cJSON_AddNumberToObject(o, "iotc-channel", 2);
    return nvr_resp_content(o);
}
