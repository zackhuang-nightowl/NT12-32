/***************************************************************************************
 *  nvr_rtsp_srv.cpp — C++ shim over nop::NopRtspServer.
 *
 *  See nvr_rtsp_srv.h. Owns ONE static nop::NopRtspServer (the SDK engine is a
 *  process-wide singleton: one g_rtsp_cfg / one rtsp_start). All entry points are
 *  extern "C" so the C translation unit nvr_rtsp_live.c can call them.
 *
 *  Slot release (nvr_srv_free_stream): addStream() does getInstance()+initCapture(),
 *  leaving the CLiveVideo/CLiveAudio instance at refcnt 1. To drop exactly that one
 *  reference we must call freeInstance() ONCE. The header's literal
 *  "getInstance(slot)->freeInstance(slot)" would be off by one (getInstance bumps the
 *  count back up), so instead we reach the already-created instance through the public
 *  static table CLiveVideo::m_pInstance[slot] and call freeInstance() on it directly.
 *  freeInstance() decrements to 0, sets m_bInited=FALSE and deletes the instance, so a
 *  later addStream() on the same slot re-inits at the new resolution/codec.
 ***************************************************************************************/

#include "nvr_rtsp_srv.h"

#include "nop_rtsp.h"       /* nop::NopRtspServer + descriptor structs (${OCL}) */
#include "sys_inc.h"        /* uint8/BOOL and the platform headers live_*.h need */
#include "live_video.h"     /* CLiveVideo::m_pInstance[] for ref release */
#include "live_audio.h"     /* CLiveAudio::m_pInstance[] for ref release */

using namespace nop;

/* Process-wide singleton engine (SDK semantics: only one started at a time). */
static NopRtspServer g_srv;
static int           g_running = 0;
static int           g_port    = 0;

extern "C" int nvr_srv_start(int port)
{
    if (g_running) return g_port;

    /* Establish RTSP/RTP buffer pools. Idempotent: the puller already calls this at
     * boot; calling again is safe (both underlying inits return early if present). */
    rtspLibInit();

    NopRtspServerCfg cfg;
    cfg.port      = (port > 0) ? port : 554;
    cfg.need_auth = false;

    if (!g_srv.start(cfg)) {
        g_running = 0;
        g_port    = 0;
        return 0;
    }
    g_running = 1;
    g_port    = cfg.port;
    return g_port;
}

extern "C" void nvr_srv_stop(void)
{
    if (!g_running) return;
    g_srv.stop();
    g_running = 0;
    g_port    = 0;
}

extern "C" int nvr_srv_is_running(void)
{
    return g_running;
}

extern "C" int nvr_srv_add_stream(int slot, int vcodec, int w, int h, double fps, int bitrate,
                                  int want_audio, int acodec, int asr, int ach)
{
    if (!g_running) return -1;

    NopVideoInfo v;
    v.codec     = (vcodec == 1) ? NOP_VIDEO_CODEC_H265 : NOP_VIDEO_CODEC_H264;
    v.width     = w;
    v.height    = h;
    v.framerate = fps;
    v.bitrate   = bitrate;

    NopAudioInfo a;
    if (want_audio) {
        a.codec        = acodec ? acodec : NOP_AUDIO_CODEC_AAC;
        a.samplerate   = asr > 0 ? asr : 16000;
        a.channels     = ach > 0 ? ach : 1;
        a.bitrate      = 0;
        a.bitpersample = 16;
    }

    int rc = g_srv.addStream(slot, v, want_audio ? &a : (const NopAudioInfo *)0);
    return (rc == slot) ? 0 : -1;
}

extern "C" int nvr_srv_push_video(int slot, const uint8_t *data, int len)
{
    if (!g_running || !data || len <= 0) return -1;
    return g_srv.pushVideo(slot, data, len) ? 0 : -1;
}

extern "C" int nvr_srv_push_video_ts(int slot, const uint8_t *data, int len, uint32_t ts)
{
    if (!g_running || !data || len <= 0) return -1;
    /* NopRtspServer::pushVideo has no ts arg → call the ts-carrying media layer directly
     * (same target as pushVideo, plus the 90kHz timestamp for the playback timeline). */
    return media_live_put_video_ts(slot, (uint8 *)data, len, (uint32)ts) ? 0 : -1;
}

extern "C" int nvr_srv_push_audio(int slot, const uint8_t *data, int len, int nbsamples)
{
    if (!g_running || !data || len <= 0) return -1;
    return g_srv.pushAudio(slot, data, len, nbsamples) ? 0 : -1;
}

extern "C" void nvr_srv_free_stream(int slot)
{
    if (slot < 0 || slot >= NOP_RTSP_MAX_STREAMS) return;

    /* Release the single ref addStream() took on each instance (see file header). */
    if (slot < MAX_LIVE_VIDEO_NUMS && CLiveVideo::m_pInstance[slot])
        CLiveVideo::m_pInstance[slot]->freeInstance(slot);

    if (slot < MAX_LIVE_AUDIO_NUMS && CLiveAudio::m_pInstance[slot])
        CLiveAudio::m_pInstance[slot]->freeInstance(slot);
}
