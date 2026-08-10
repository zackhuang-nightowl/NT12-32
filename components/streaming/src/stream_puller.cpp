/***************************************************************************************
 *  stream_puller.cpp — 每通道**主+子两路** nop::NopRtspClient 拉流封装（C++）
 *
 *  每路(stream_pull_t)一个 NopRtspClient;回调 ctx = stream_pull_t*,把裸帧转交
 *  stream_route_video(p,...)（纯 C）。主+子都录;显示由 decode_stream 那路喂解码器。
 *  统一 RTSP SDK：NT12-SDK/OnvifClientLibrary(libonvifclient.a)，仅需 nop_rtsp.h。
 ***************************************************************************************/
#include "stream_internal.h"
#include "nvr_log.h"

#include "nop_rtsp.h"

using namespace nop;

static int to_rsdk_codec(int vc) { return (vc == NOP_VIDEO_CODEC_H265) ? RSDK_CODEC_H265 : RSDK_CODEC_H264; }

static void puller_lib_init_once(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    nop::rtspLibInit(256);
    NVR_LOGI("puller", "RTSP 缓冲池初始化: nop::rtspLibInit(256)");
}

static const char *eve_name(int e)
{
    switch (e) {
        case NOP_RTSP_EVE_STOPPED:    return "STOPPED";
        case NOP_RTSP_EVE_CONNECTING: return "CONNECTING";
        case NOP_RTSP_EVE_CONNFAIL:   return "CONNFAIL";
        case NOP_RTSP_EVE_CONNSUCC:   return "CONNSUCC";
        case NOP_RTSP_EVE_NOSIGNAL:   return "NOSIGNAL";
        case NOP_RTSP_EVE_RESUME:     return "RESUME";
        case NOP_RTSP_EVE_AUTHFAILED: return "AUTHFAILED";
        case NOP_RTSP_EVE_NODATA:     return "NODATA";
        default:                      return "?";
    }
}

static const char *sname(int s) { return s == NVR_STREAM_SUB ? "子" : "主"; }

/* 某路连上:定 codec、开 writer(通道级,只开一次)、若是 decode_stream 且可见则开解码器。 */
static void pull_on_connected(stream_pull_t *p)
{
    stream_chan_t *c = p->owner;
    NopRtspClient *cli = (NopRtspClient *)p->puller;
    if (!cli || p->connected) return;
    int vc = cli->videoCodec();
    p->codec = (c->cfg.codec == NVR_CODEC_AUTO) ? to_rsdk_codec(vc) : c->cfg.codec;
    p->connected = 1;
    NVR_LOGI("puller", "ch%d[%s] CONNSUCC (SDP codec=%d 1=H264 4=H265)", c->cfg.chn, sname(p->stream), vc);

    if (!c->router_open) { c->router_open = 1; stream_router_open(c, c->grp); }  /* 首个连上的路开 writer */
    /* 该路是当前解码源且落在可见格 → 开解码器 */
    if (p->stream == c->decode_stream && c->show_win >= 0 && !c->vdec)
        stream_decode_open(c, c->show_win);
}

/* ---------- 回调（ctx = stream_pull_t*） ---------- */
static void on_video(const uint8_t *data, int len, uint32_t ts, uint16_t seq, void *ud)
{
    stream_pull_t *p = (stream_pull_t *)ud;
    if (!p) return;
    if (!p->connected) pull_on_connected(p);   /* 兜底:帧先于 CONNSUCC 到 */
    if (p->vframes == 0)
        NVR_LOGI("puller", "ch%d[%s] 首帧! len=%d codec=%d", p->owner->cfg.chn, sname(p->stream), len, p->codec);
    p->vframes++; p->vbytes += (unsigned long)(len > 0 ? len : 0);
    if ((p->vframes % 200) == 0)
        NVR_LOGI("puller", "ch%d[%s] 已收 %u 帧", p->owner->cfg.chn, sname(p->stream), p->vframes);
    stream_route_video(p, data, len, ts, seq);
}

static void on_audio(const uint8_t *data, int len, uint32_t ts, uint16_t seq, void *ud)
{
    (void)seq;
    stream_pull_t *p = (stream_pull_t *)ud;
    if (p) stream_route_audio(p, data, len, ts);
}

static void on_event(int event, void *ud)
{
    stream_pull_t *p = (stream_pull_t *)ud;
    if (!p) return;
    stream_chan_t *c = p->owner;
    NVR_LOGI("puller", "ch%d[%s] RTSP事件: %s(%d)", c->cfg.chn, sname(p->stream), eve_name(event), event);
    switch (event) {
        case NOP_RTSP_EVE_CONNECTING: if (c->state == NVR_CH_IDLE) c->state = NVR_CH_CONNECTING; break;
        case NOP_RTSP_EVE_CONNSUCC:   pull_on_connected(p); c->state = NVR_CH_PLAYING; break;
        case NOP_RTSP_EVE_CONNFAIL:
        case NOP_RTSP_EVE_AUTHFAILED: p->connected = 0; if (c->state != NVR_CH_PLAYING) c->state = NVR_CH_FAIL; break;
        case NOP_RTSP_EVE_NOSIGNAL:
        case NOP_RTSP_EVE_NODATA:     p->connected = 0; c->state = NVR_CH_NOSIGNAL; break;  /* 供 mgr 重连 */
        case NOP_RTSP_EVE_RESUME:     c->state = NVR_CH_PLAYING; break;
        case NOP_RTSP_EVE_STOPPED:    p->connected = 0; break;
        default: break;
    }
}

/* ---------- 单路 start/stop（供 mgr 按 URL 到位分别启停） ---------- */
extern "C" int stream_pull_start(stream_pull_t *p, int conn_to, int rx_to)
{
    stream_chan_t *c = p->owner;
    if (!p->url[0] || p->puller) return 0;   /* 无 URL 或已在跑 */
    puller_lib_init_once();
    NopRtspClient *cli = new NopRtspClient();
    p->puller = cli;
    p->connected = 0; p->vframes = 0; p->vbytes = 0;
    p->par_len = 0; p->par_building = 0;

    cli->setEventCb(on_event, p);            /* ctx = 本路 */
    cli->setVideoCb(on_video, p);
    cli->setAudioCb(on_audio, p);
    cli->setTransportTcp(c->cfg.over_tcp ? true : false);
    cli->setConnTimeout(conn_to > 0 ? conn_to : 5);
    cli->setRxTimeout  (rx_to   > 0 ? rx_to   : 10);

    NVR_LOGI("puller", "ch%d[%s] 开始拉流: url=%s", c->cfg.chn, sname(p->stream), p->url);
    if (!cli->open(p->url, c->cfg.user, c->cfg.pass)) {
        NVR_LOGE("puller", "ch%d[%s] open 失败", c->cfg.chn, sname(p->stream));
        delete cli; p->puller = NULL;
        return -1;
    }
    return 0;
}

extern "C" void stream_pull_stop(stream_pull_t *p)
{
    if (!p || !p->puller) return;
    NopRtspClient *cli = (NopRtspClient *)p->puller;
    cli->stop();
    delete cli;
    p->puller = NULL;
    p->connected = 0;
}

/* ---------- 通道级 start/stop（两路一起） ---------- */
extern "C" int puller_start(stream_chan_t *c, int conn_to, int rx_to, rsdk_group_t *grp)
{
    if (!c) return -1;
    c->grp = grp;
    c->state = NVR_CH_CONNECTING;
    int r1 = stream_pull_start(&c->pmain, conn_to, rx_to);   /* 主路(有 URL 才起) */
    int r2 = stream_pull_start(&c->psub,  conn_to, rx_to);   /* 子路 */
    return (r1 == 0 || r2 == 0 || (!c->pmain.url[0] && !c->psub.url[0])) ? 0 : -1;
}

extern "C" void puller_stop(stream_chan_t *c)
{
    if (!c) return;
    stream_pull_stop(&c->pmain);
    stream_pull_stop(&c->psub);
    stream_router_close(c);
    c->router_open = 0;
    c->state = NVR_CH_IDLE;
}
