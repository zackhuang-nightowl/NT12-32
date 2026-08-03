/***************************************************************************************
 *  stream_puller.cpp — 每通道一个 CRtspClient 拉流封装（C++）
 *
 *  职责：new CRtspClient → 配置(TCP/超时) → rtsp_start(DESCRIBE) → 定 codec →
 *        开 router(vdec+writer) → 挂 video/audio/notify 回调 → rtsp_play。
 *  回调里把裸帧转交 stream_router.c（纯 C）。
 *
 *  ⚠️ 需要 happytime 头路径（sys_buf.h/media_format.h/rtsp_cln.h…），见 CMakeLists。
 ***************************************************************************************/
#include "stream_internal.h"

#include "sys_inc.h"
#include "media_format.h"      /* VIDEO_CODEC_H264 / VIDEO_CODEC_H265 */
#include "rtsp_cln.h"          /* CRtspClient */

/* Happytime codec(1=H264,4=H265) → rsdk codec(0=H264,1=H265) */
static int to_rsdk_codec(int vc) { return (vc == VIDEO_CODEC_H265) ? RSDK_CODEC_H265 : RSDK_CODEC_H264; }

/* ---------- CRtspClient 回调（C 函数指针；userdata = stream_chan_t*） ---------- */
static int on_video(uint8 *data, int len, uint32 ts, uint16 seq, void *ud)
{
    stream_chan_t *c = (stream_chan_t *)ud;
    if (c) stream_route_video(c, (const uint8_t *)data, len, ts, seq);
    return 0;
}

static int on_audio(uint8 *data, int len, uint32 ts, uint16 seq, void *ud)
{
    (void)seq;
    stream_chan_t *c = (stream_chan_t *)ud;
    if (c) stream_route_audio(c, (const uint8_t *)data, len, ts);
    return 0;
}

static int on_notify(int event, void *ud)
{
    stream_chan_t *c = (stream_chan_t *)ud;
    if (!c) return 0;
    switch (event) {
        case RTSP_EVE_CONNECTING: c->state = NVR_CH_CONNECTING; break;
        case RTSP_EVE_CONNSUCC:   c->state = NVR_CH_PLAYING;    break;
        case RTSP_EVE_CONNFAIL:
        case RTSP_EVE_AUTHFAILED: c->state = NVR_CH_FAIL;       break;
        case RTSP_EVE_NOSIGNAL:
        case RTSP_EVE_NODATA:     c->state = NVR_CH_NOSIGNAL;   break;   /* 供 mgr 重连 */
        case RTSP_EVE_RESUME:     c->state = NVR_CH_PLAYING;    break;
        case RTSP_EVE_STOPPED:    c->state = NVR_CH_IDLE;       break;
        default: break;
    }
    return 0;
}

/* ---------- 对 mgr(C++) 暴露 ---------- */
extern "C" int puller_start(stream_chan_t *c, int conn_to, int rx_to, rsdk_group_t *grp)
{
    if (!c) return -1;
    CRtspClient *cli = new CRtspClient();
    c->puller = cli;
    c->state  = NVR_CH_CONNECTING;

    cli->set_rtp_over_udp(c->cfg.over_tcp ? 0 : 1);   /* NVR 场景默认 TCP 更稳 */
    cli->set_conn_timeout(conn_to > 0 ? conn_to : 5);
    cli->set_rx_timeout  (rx_to   > 0 ? rx_to   : 10);
    cli->set_notify_cb(on_notify, c);                 /* userdata=c → 各回调可拿到通道 */

    /* 连接 + DESCRIBE（拿到 SDP/codec） */
    if (!cli->rtsp_start(c->cfg.url, c->cfg.user, c->cfg.pass)) {
        c->state = NVR_CH_FAIL;
        delete cli; c->puller = NULL;
        return -1;
    }

    /* 定 codec：cfg 指定优先，否则用 SDP 探测结果 */
    c->codec = (c->cfg.codec == NVR_CODEC_AUTO) ? to_rsdk_codec(cli->video_codec())
                                                : c->cfg.codec;

    /* 开 router：解码器(bind 分屏) + 录像写入器 */
    stream_router_open(c, grp);

    /* 挂数据回调后开播 */
    cli->set_video_cb(on_video);
    cli->set_audio_cb(on_audio);
    if (!cli->rtsp_play(0)) {
        c->state = NVR_CH_FAIL;
        return -1;
    }
    return 0;
}

extern "C" void puller_stop(stream_chan_t *c)
{
    if (!c || !c->puller) return;
    CRtspClient *cli = (CRtspClient *)c->puller;
    cli->rtsp_stop();
    cli->rtsp_close();
    delete cli;
    c->puller = NULL;
    stream_router_close(c);
    c->state = NVR_CH_IDLE;
}
