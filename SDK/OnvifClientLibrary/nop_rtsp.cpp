/***************************************************************************************
 *
 *  nop_rtsp.cpp
 *
 *  Implementation of the nop_rtsp facade. THIN wrapper only: every method maps
 *  directly onto an existing Happytime call. No streaming/packetization logic is
 *  added here.
 *
 *  Call map (facade -> Happytime):
 *    NopRtspServer::start          -> build g_rtsp_cfg + rtsp_start1()
 *    NopRtspServer::stop           -> rtsp_stop()
 *    NopRtspServer::addStream      -> CLiveVideo/CLiveAudio getInstance()+initCapture()
 *    NopRtspServer::pushVideo      -> media_live_put_video()
 *    NopRtspServer::pushAudio      -> media_live_put_audio()
 *    NopRtspServer::setBackchannelSink -> store sink; fed by nop_rtsp_server_on_bc_audio()
 *    NopRtspClient::open           -> new CRtspClient; set_*_cb; set_bc_flag(1);
 *                                     set_audio_device(-1); rtsp_start(url,user,pass)
 *    NopRtspClient::setVideoCb     -> store cb (dispatched by videoTrampoline)
 *    NopRtspClient::setAudioCb     -> store cb (dispatched by audioTrampoline)
 *    NopRtspClient::play           -> rtsp_play(); set_bc_data_flag(1)
 *    NopRtspClient::stop           -> rtsp_stop(); rtsp_close()
 *    NopRtspClient::sendBackchannel-> CRtspClient::send_backchannel(data,len)
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *  (Facade layer added for the OnvifClientLibrary deliverable.)
 *
 ***************************************************************************************/

#include "sys_inc.h"          // base types (uint8, BOOL, ...) and platform glue
#include "media_format.h"
#include "media_info.h"
#include "live_video.h"
#include "live_audio.h"
#include "rtsp_cfg.h"         // RTSP_CFG, g_rtsp_cfg
#include "rtsp_srv.h"         // rtsp_start1(), rtsp_stop()
#include "rtsp_cln.h"         // CRtspClient
#include "rtsp_rcua.h"        // RCUA + rcua_get_sdp_h264/h265_params (SDP parameter sets)
#include "base64.h"           // base64_decode (SDP sprop values are base64)
#include "rtsp_backchannel.h" // rtsp_bc_cb() (used by CRtspClient::send_backchannel)
#include "rtsp_parse.h"       // rtsp_parse_buf_init/deinit (HRTSP_MSG pool)
#include "sys_buf.h"          // sys_buf_init/deinit (net_buf + hdrv pools)

#include "nop_rtsp.h"

#include <string.h>

namespace nop {

/***************************************************************************************
 * Library lifecycle — buffer pools (see nop_rtsp.h). Both underlying inits are
 * idempotent, so this is safe to call before/independently of NopRtspServer::start().
 * The NVR client path needs these from boot (it must not wait for the server, which
 * only runs while a mobile app is connected).
 ***************************************************************************************/
void rtspLibInit(int bufs)
{
    if (bufs <= 0) bufs = 256;
    sys_buf_init(bufs);          // net_buf + hdrv (idempotent: returns early if built)
    rtsp_parse_buf_init(bufs);   // HRTSP_MSG pool (idempotent)
}

void rtspLibDeinit()
{
    rtsp_parse_buf_deinit();
    sys_buf_deinit();
}

/***************************************************************************************
 * Server-side backchannel sink (process-wide; the engine is a singleton).
 ***************************************************************************************/
static NopBcRecvCb g_bcSinkCb  = 0;
static void*       g_bcSinkCtx = 0;

/***************************************************************************************
 * NopRtspServer
 ***************************************************************************************/

NopRtspServer::NopRtspServer()
    : m_started(false)
{
}

NopRtspServer::~NopRtspServer()
{
    stop();
}

bool NopRtspServer::start(const NopRtspServerCfg& cfg)
{
    if (m_started)
    {
        return true;
    }

    // Build the RTSP_CFG in code (no config file) and hand it straight to
    // rtsp_start1(). rtsp_start(const char*) would instead read a .cfg file, so we
    // deliberately populate g_rtsp_cfg ourselves and skip that path.
    memset(&g_rtsp_cfg, 0, sizeof(g_rtsp_cfg));

    g_rtsp_cfg.rtsp_port = (cfg.port > 0) ? cfg.port : 554;
    g_rtsp_cfg.need_auth = cfg.need_auth ? 1 : 0;
    g_rtsp_cfg.multicast = cfg.multicast ? 1 : 0;

    if (cfg.need_auth && cfg.username)
    {
        strncpy(g_rtsp_cfg.users[0].username, cfg.username,
                sizeof(g_rtsp_cfg.users[0].username) - 1);
        if (cfg.password)
        {
            strncpy(g_rtsp_cfg.users[0].password, cfg.password,
                    sizeof(g_rtsp_cfg.users[0].password) - 1);
        }
    }

#ifdef RTSPS
    if (cfg.rtsps_enable)
    {
        g_rtsp_cfg.rtsps_enable = 1;
        g_rtsp_cfg.rtsps_port   = (cfg.rtsps_port > 0) ? cfg.rtsps_port : 322;
        if (cfg.rtsps_cert)
        {
            strncpy(g_rtsp_cfg.rtsps_cert, cfg.rtsps_cert, sizeof(g_rtsp_cfg.rtsps_cert) - 1);
        }
        if (cfg.rtsps_key)
        {
            strncpy(g_rtsp_cfg.rtsps_key, cfg.rtsps_key, sizeof(g_rtsp_cfg.rtsps_key) - 1);
        }
    }
#endif

#ifdef RTSP_BACKCHANNEL
    if (cfg.bc_codec != 0)
    {
        g_rtsp_cfg.backchannel.codec      = cfg.bc_codec;
        g_rtsp_cfg.backchannel.samplerate = cfg.bc_samplerate;
        g_rtsp_cfg.backchannel.channels   = cfg.bc_channels;
    }
#endif

    // No static MEDIA_OUTPUT list: live streams are served through the engine's
    // MEDIA_LIVE URL routing, and are provisioned via addStream() below.
    g_rtsp_cfg.output = 0;

    m_started = (rtsp_start1() == TRUE);
    return m_started;
}

void NopRtspServer::stop()
{
    if (m_started)
    {
        rtsp_stop();
        m_started = false;
    }
}

int NopRtspServer::addStream(int idx, const NopVideoInfo& video, const NopAudioInfo* audio)
{
    // Video slot: create/init the live instance so media_live_put_video(idx,...)
    // has a target. We do NOT call startCapture() -- we push, we do not capture.
    if (idx < 0 || idx >= CLiveVideo::getStreamNums())
    {
        return -1;
    }

    CLiveVideo* v = CLiveVideo::getInstance(idx);
    if (0 == v)
    {
        return -1;
    }

    if (v->initCapture(video.codec, video.width, video.height,
                       video.framerate, video.bitrate) == FALSE)
    {
        v->freeInstance(idx);
        return -1;
    }

    if (audio)
    {
        if (idx < 0 || idx >= CLiveAudio::getStreamNums())
        {
            return -1;
        }

        CLiveAudio* a = CLiveAudio::getInstance(idx);
        if (0 == a)
        {
            return -1;
        }

        if (a->initCapture(audio->codec, audio->samplerate,
                           audio->channels, audio->bitrate) == FALSE)
        {
            a->freeInstance(idx);
            return -1;
        }
    }

    return idx;
}

bool NopRtspServer::pushVideo(int idx, const uint8_t* data, int size)
{
    return media_live_put_video(idx, (uint8*)data, size) == TRUE;
}

bool NopRtspServer::pushAudio(int idx, const uint8_t* data, int size, int nbsamples)
{
    return media_live_put_audio(idx, (uint8*)data, size, nbsamples) == TRUE;
}

void NopRtspServer::setBackchannelSink(NopBcRecvCb cb, void* ctx)
{
    g_bcSinkCb  = cb;
    g_bcSinkCtx = ctx;
}

/***************************************************************************************
 * NopRtspClient
 ***************************************************************************************/

NopRtspClient::NopRtspClient()
    : m_client(0), m_videoCb(0), m_videoCtx(0), m_audioCb(0), m_audioCtx(0),
      m_eventCb(0), m_eventCtx(0),
      m_useTcp(true), m_connTimeout(0), m_rxTimeout(0),
      m_wantBc(false)
{
}

NopRtspClient::~NopRtspClient()
{
    stop();
}

// The engine carries a single userdata pointer (set via set_notify_cb). We set it
// to `this`, so the trampolines below recover the NopRtspClient and dispatch to the
// user-registered callback with the user's own ctx.
int NopRtspClient::videoTrampoline(uint8_t* data, int len, uint32_t ts, uint16_t seq, void* ud)
{
    NopRtspClient* self = (NopRtspClient*)ud;
    if (self && self->m_videoCb)
    {
        self->m_videoCb(data, len, ts, seq, self->m_videoCtx);
    }
    return 0;
}

int NopRtspClient::audioTrampoline(uint8_t* data, int len, uint32_t ts, uint16_t seq, void* ud)
{
    NopRtspClient* self = (NopRtspClient*)ud;
    if (self && self->m_audioCb)
    {
        self->m_audioCb(data, len, ts, seq, self->m_audioCtx);
    }
    return 0;
}

int NopRtspClient::notifyTrampoline(int event, void* ud)
{
    NopRtspClient* self = (NopRtspClient*)ud;
    if (self && self->m_eventCb)
    {
        self->m_eventCb(event, self->m_eventCtx);   // event codes == NopRtspEvent
    }
    return 0;
}

bool NopRtspClient::open(const char* url, const char* user, const char* pass)
{
    if (m_client)
    {
        return false; // already open
    }

    m_client = new CRtspClient();
    if (0 == m_client)
    {
        return false;
    }

    // Register trampolines and plant `this` as the userdata for all callbacks.
    m_client->set_notify_cb(&NopRtspClient::notifyTrampoline, this);
    m_client->set_video_cb(&NopRtspClient::videoTrampoline);
    m_client->set_audio_cb(&NopRtspClient::audioTrampoline);

    // Apply transport tuning captured via the setters. MUST precede rtsp_start (it
    // spawns the rx thread that runs OPTIONS/DESCRIBE/SETUP with these settings).
    m_client->set_rtp_over_udp(m_useTcp ? 0 : 1);
    if (m_connTimeout > 0) m_client->set_conn_timeout(m_connTimeout);
    if (m_rxTimeout   > 0) m_client->set_rx_timeout(m_rxTimeout);

    // Backchannel (talk-down) is OPT-IN. A plain live/record pull must NOT negotiate
    // a backchannel — arming it makes DESCRIBE/SETUP advertise sendonly audio and the
    // camera performs a spurious backchannel SETUP on every stream. Only arm it when a
    // caller explicitly asked for two-way audio via setBackchannel(true).
    if (m_wantBc) {
        m_client->set_bc_flag(1);
        m_client->set_audio_device(-1);   // byte-push: no local mic capture
    }

    if (m_client->rtsp_start(url, user, pass) == FALSE)
    {
        m_client->rtsp_close();
        delete m_client;
        m_client = 0;
        return false;
    }

    return true;
}

void NopRtspClient::setVideoCb(NopFrameCb cb, void* ctx)
{
    m_videoCb  = cb;
    m_videoCtx = ctx;
}

void NopRtspClient::setAudioCb(NopFrameCb cb, void* ctx)
{
    m_audioCb  = cb;
    m_audioCtx = ctx;
}

void NopRtspClient::setEventCb(NopEventCb cb, void* ctx)
{
    m_eventCb  = cb;
    m_eventCtx = ctx;
}

void NopRtspClient::setBackchannel(bool enable) { m_wantBc = enable; }
void NopRtspClient::setTransportTcp(bool tcp) { m_useTcp = tcp; }
void NopRtspClient::setConnTimeout(int sec)   { m_connTimeout = sec; }
void NopRtspClient::setRxTimeout(int sec)     { m_rxTimeout = sec; }

int NopRtspClient::videoCodec() const
{
    return m_client ? m_client->video_codec() : (int)NOP_VIDEO_CODEC_NONE;
}

// Decode one base64 parameter-set NAL and append it Annex-B framed
// (00 00 00 01 + payload) at out[off]. Returns bytes appended, 0 on
// decode failure or insufficient space. Bytes are the real SDP value, not synthesized.
static int append_annexb_nal(const char* b64, int b64len, uint8_t* out, int max, int off)
{
    uint8_t nal[1024];
    if (!b64 || b64len <= 0) return 0;
    int n = base64_decode(b64, (uint32)b64len, nal, (uint32)sizeof(nal));
    if (n <= 0 || off + 4 + n > max) return 0;
    out[off + 0] = 0x00; out[off + 1] = 0x00; out[off + 2] = 0x00; out[off + 3] = 0x01;
    memcpy(out + off + 4, nal, (size_t)n);
    return 4 + n;
}

int NopRtspClient::videoParameterSets(uint8_t* out, int max) const
{
    if (!m_client || !out || max <= 0) return 0;

    RCUA* rua = m_client->get_rua();
    if (!rua) return 0;

    int codec = m_client->video_codec();
    int pt = 0, off = 0;

    if (codec == NOP_VIDEO_CODEC_H264)
    {
        // sprop-parameter-sets = base64(SPS),base64(PPS)[,...] — split on ','.
        char b64[1024] = {0};
        if (rcua_get_sdp_h264_params(rua, &pt, b64, (int)sizeof(b64)) == FALSE)
            return 0;
        char* start = b64;
        while (*start)
        {
            char* comma = strchr(start, ',');
            int toklen = comma ? (int)(comma - start) : (int)strlen(start);
            if (toklen > 0)
            {
                int w = append_annexb_nal(start, toklen, out, max, off);
                if (w == 0) break;   // decode failed or out of space
                off += w;
            }
            if (!comma) break;
            start = comma + 1;
        }
    }
    else if (codec == NOP_VIDEO_CODEC_H265)
    {
        // Separate sprop-vps= / sprop-sps= / sprop-pps= base64 values.
        char vps[1024] = {0}, sps[1024] = {0}, pps[1024] = {0};
        BOOL don = FALSE;
        if (rcua_get_sdp_h265_params(rua, &pt, &don, vps, (int)sizeof(vps),
                                     sps, (int)sizeof(sps), pps, (int)sizeof(pps)) == FALSE)
            return 0;
        const char* parts[3] = { vps, sps, pps };   // VPS,SPS,PPS order
        for (int i = 0; i < 3; i++)
        {
            if (parts[i][0] == '\0') continue;
            int w = append_annexb_nal(parts[i], (int)strlen(parts[i]), out, max, off);
            if (w == 0) break;
            off += w;
        }
    }

    return off;
}

bool NopRtspClient::play()
{
    if (0 == m_client)
    {
        return false;
    }

    BOOL ok = m_client->rtsp_play();

    // Enable backchannel data sending only when backchannel was armed at open().
    // send_backchannel() also guards on actual negotiation, but keeping this gated
    // avoids touching the send path for plain pulls.
    if (m_wantBc) {
        m_client->set_bc_data_flag(1);
    }

    return ok == TRUE;
}

void NopRtspClient::stop()
{
    if (m_client)
    {
        m_client->rtsp_stop();
        m_client->rtsp_close();
        delete m_client;
        m_client = 0;
    }
}

bool NopRtspClient::sendBackchannel(const uint8_t* data, int len)
{
    if (0 == m_client)
    {
        return false;
    }
    return m_client->send_backchannel((uint8*)data, len) == TRUE;
}

} // namespace nop

/***************************************************************************************
 * Server backchannel-receive hook (C linkage).
 *
 * Invoked by rtsp/rtsp_srv_backchannel.cpp (rtsp_bc_data_cb, non-MEDIA_DEVICE branch)
 * with decoded talk audio. Forwards to the sink registered via
 * NopRtspServer::setBackchannelSink().
 ***************************************************************************************/
extern "C" void nop_rtsp_server_on_bc_audio(const uint8_t* data, int size, int nbsamples)
{
    if (nop::g_bcSinkCb)
    {
        nop::g_bcSinkCb(data, size, nbsamples, nop::g_bcSinkCtx);
    }
}
