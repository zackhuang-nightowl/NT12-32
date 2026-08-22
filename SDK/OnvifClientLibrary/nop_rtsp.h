/***************************************************************************************
 *
 *  nop_rtsp.h
 *
 *  External facade for the RTSP / RTP / backchannel media component built into
 *  libonvifclient.so. This is a THIN C++ wrapper over the Happytime RTSP engine
 *  (rtsp/, rtp/, media/ live-injection slice). It contains no streaming logic of
 *  its own; it merely organizes the existing Happytime calls into one obvious
 *  integration point for the NVR firmware.
 *
 *  The header is intentionally dependency-light: consumers include only this file
 *  and link libonvifclient.so. All Happytime internals (CRtspClient, RTSP_CFG,
 *  CLiveVideo/CLiveAudio, media_live_put_*, rtsp_start/stop, rtsp_bc_cb) are hidden
 *  behind this facade in nop_rtsp.cpp.
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *  (Facade layer added for the OnvifClientLibrary deliverable.)
 *
 ***************************************************************************************/

#ifndef NOP_RTSP_H
#define NOP_RTSP_H

#include <cstdint>

/* Opaque forward declaration of the Happytime RTSP client. Consumers never need
 * the full definition; the facade owns the instance. */
class CRtspClient;

namespace nop {

/***************************************************************************************
 * Codec identifiers.
 *
 * These mirror media/media_format.h exactly, re-exported here so that consumers do
 * not have to include the Happytime media headers just to name a codec.
 ***************************************************************************************/

// Video codecs (== VIDEO_CODEC_* in media_format.h)
enum NopVideoCodec {
    NOP_VIDEO_CODEC_NONE = 0,
    NOP_VIDEO_CODEC_H264 = 1,
    NOP_VIDEO_CODEC_MP4  = 2,
    NOP_VIDEO_CODEC_JPEG = 3,
    NOP_VIDEO_CODEC_H265 = 4
};

// Audio codecs (== AUDIO_CODEC_* in media_format.h)
enum NopAudioCodec {
    NOP_AUDIO_CODEC_NONE  = 0,
    NOP_AUDIO_CODEC_G711A = 1,   // PCMA
    NOP_AUDIO_CODEC_G711U = 2,   // PCMU
    NOP_AUDIO_CODEC_G726  = 3,
    NOP_AUDIO_CODEC_AAC   = 4,
    NOP_AUDIO_CODEC_G722  = 5,
    NOP_AUDIO_CODEC_OPUS  = 6
};

// RTSP client connection events (== RTSP_EVE_* in rtsp_cln.h). Surfaced via the
// NopEventCb registered with NopRtspClient::setEventCb(). The NVR uses these to drive
// its per-channel state machine (open decoder at CONNSUCC; reconnect on NODATA/NOSIGNAL).
enum NopRtspEvent {
    NOP_RTSP_EVE_STOPPED    = 0,
    NOP_RTSP_EVE_CONNECTING = 1,
    NOP_RTSP_EVE_CONNFAIL   = 2,
    NOP_RTSP_EVE_CONNSUCC   = 3,   // SETUP/PLAY done — video_codec() now valid
    NOP_RTSP_EVE_NOSIGNAL   = 4,
    NOP_RTSP_EVE_RESUME     = 5,
    NOP_RTSP_EVE_AUTHFAILED = 6,
    NOP_RTSP_EVE_NODATA     = 7    // no data within rx timeout — caller should reconnect
};

/* Maximum number of live stream slots.
 * MUST stay in sync with MAX_LIVE_VIDEO_NUMS / MAX_LIVE_AUDIO_NUMS in
 * media/live_video.h and media/live_audio.h (currently 4). addStream() validates
 * the requested idx against the real CLiveVideo/CLiveAudio stream count. */
static const int NOP_RTSP_MAX_STREAMS = 32;

/***************************************************************************************
 * Library lifecycle (buffer pools).
 *
 * The RTSP engine (both NopRtspClient and NopRtspServer) allocates RTSP messages and
 * network buffers from two process-wide free-list pools:
 *   - sys_buf_init()        -> net_buf + hdrv buffers
 *   - rtsp_parse_buf_init() -> HRTSP_MSG structure pool
 * Historically these were only established by the SERVER start path (rtsp_start1).
 * But on an NVR the CLIENT (pulling from cameras) runs from boot and must not depend
 * on the server (which only exists while a mobile APP is connected). Call rtspLibInit()
 * ONCE at process startup, before any NopRtspClient::open(). Without it, the client's
 * rcua_build_options()/rtsp_get_msg_buf() return NULL and no RTSP request is ever sent
 * (connect succeeds, then NODATA/timeout, reconnect forever).
 *
 * Both underlying inits are idempotent (return early if the pool already exists), so
 * rtspLibInit() and NopRtspServer::start() may run in any order / repeatedly, safely.
 ***************************************************************************************/

/** Establish the RTSP/RTP buffer pools. Call once at process startup. bufs sizes the
 *  free lists (>= 2 * expected concurrent RTSP sessions; 256 is a generous NVR default). */
void rtspLibInit(int bufs = 256);

/** Release the RTSP/RTP buffer pools (call at shutdown; optional). */
void rtspLibDeinit();

/***************************************************************************************
 * Descriptor structs (POD, standard types only).
 ***************************************************************************************/

/** Video stream description for a served live stream. Maps to VIDEO_INFO. */
struct NopVideoInfo {
    int    codec;       // NopVideoCodec
    int    width;       // pixels
    int    height;      // pixels
    double framerate;   // frames per second
    int    bitrate;     // kb/s (informational; used for SDP/limits)
};

/** Audio stream description for a served live stream. Maps to AUDIO_INFO. */
struct NopAudioInfo {
    int codec;          // NopAudioCodec
    int samplerate;     // Hz
    int channels;       // channel count
    int bitrate;        // kb/s (informational)
    int bitpersample;   // bits per sample (needed for G.726)
};

/** RTSP server configuration. Built into an RTSP_CFG by start(). */
struct NopRtspServerCfg {
    // ---- listener ----
    int         port;           // RTSP listen port (0 => default 554)

    // ---- authentication ----
    bool        need_auth;      // require Digest/Basic auth
    const char* username;       // single user credential (used when need_auth)
    const char* password;

    // ---- multicast ----
    bool        multicast;      // enable RTP multicast delivery

    // ---- RTSPS (TLS) ----  (honored only if the engine was built with RTSPS)
    bool        rtsps_enable;   // enable TLS listener
    int         rtsps_port;     // RTSPS listen port (0 => default 322)
    const char* rtsps_cert;     // PEM certificate file path
    const char* rtsps_key;      // PEM private key file path

    // ---- backchannel receive ---- (honored only if built with RTSP_BACKCHANNEL)
    int         bc_codec;       // NopAudioCodec for received talk audio; 0 disables
    int         bc_samplerate;  // Hz (e.g. 8000 for G.711)
    int         bc_channels;    // channel count (typically 1)

    NopRtspServerCfg()
        : port(554), need_auth(false), username(0), password(0),
          multicast(false),
          rtsps_enable(false), rtsps_port(322), rtsps_cert(0), rtsps_key(0),
          bc_codec(0), bc_samplerate(8000), bc_channels(1) {}
};

/***************************************************************************************
 * Callback typedefs.
 ***************************************************************************************/

/**
 * Encoded frame delivered from an RTSP client pull (video or audio).
 * data/len is one complete, decodable access unit; ts/seq come from the RTP header.
 * ctx is the pointer supplied to setVideoCb()/setAudioCb().
 */
typedef void (*NopFrameCb)(const uint8_t* data, int len,
                           uint32_t ts, uint16_t seq, void* ctx);

/**
 * RTSP client connection event (NopRtspEvent). ctx is the pointer supplied to
 * setEventCb(). Invoked on the client's rx thread.
 */
typedef void (*NopEventCb)(int event, void* ctx);

/**
 * Received backchannel (talk) audio surfaced from a connected client on the server
 * side. data/size is the decoded/depacketized payload; nbsamples is the sample
 * count when known (0 if not reported by the transport). ctx is the pointer
 * supplied to setBackchannelSink().
 */
typedef void (*NopBcRecvCb)(const uint8_t* data, int size, int nbsamples, void* ctx);

/***************************************************************************************
 * NopRtspServer
 *
 * Re-serves live streams (pushed by the NVR encoder, or relayed from a camera) to
 * RTSP consumers (mobile app / VLC / ODM). Also receives two-way talk audio.
 *
 * Lifecycle:
 *   NopRtspServer srv;
 *   srv.start(cfg);
 *   srv.addStream(0, mainVideo, &mainAudio);   // main
 *   srv.addStream(1, subVideo,  0);            // sub, video-only
 *   // per encoded frame from the hardware encoder:
 *   srv.pushVideo(0, buf, len);
 *   srv.pushAudio(0, buf, len, nbsamples);
 *   ...
 *   srv.stop();
 *
 * The server is a process-wide singleton in the underlying engine (one g_rtsp_cfg /
 * one rtsp_start). Only one NopRtspServer should be started at a time.
 ***************************************************************************************/
class NopRtspServer {
public:
    NopRtspServer();
    ~NopRtspServer();

    /**
     * Build an RTSP_CFG from cfg and start the RTSP server engine.
     * @return true on success.
     */
    bool start(const NopRtspServerCfg& cfg);

    /** Stop the RTSP server engine and release its resources. */
    void stop();

    /**
     * Register / initialize a live stream slot so that pushVideo/pushAudio(idx,...)
     * can inject encoded frames. Does NOT start any capture thread (we push, we do
     * not capture).
     *
     * idx is a GENERAL slot in [0, NOP_RTSP_MAX_STREAMS). The primary source uses
     * main = 0, sub = 1; additional local encoder channels or relayed cameras map
     * to further idx values.
     *
     * @param idx    stream slot index
     * @param video  video description (required)
     * @param audio  optional audio description (0 => video-only stream)
     * @return idx on success, -1 on error (bad idx or init failure).
     */
    int addStream(int idx, const NopVideoInfo& video, const NopAudioInfo* audio);

    /**
     * Inject one encoded video access unit into stream idx. Fans out to every
     * PLAYing session as RTP. @return true on success.
     */
    bool pushVideo(int idx, const uint8_t* data, int size);

    /**
     * Inject one encoded audio frame into stream idx. nbsamples is the audio sample
     * count in the frame. @return true on success.
     */
    bool pushAudio(int idx, const uint8_t* data, int size, int nbsamples);

    /**
     * Route received talk audio (from a connected app) to cb. Pass cb=0 to detach.
     * The sink is process-wide (one server engine).
     */
    void setBackchannelSink(NopBcRecvCb cb, void* ctx);

private:
    NopRtspServer(const NopRtspServer&);
    NopRtspServer& operator=(const NopRtspServer&);

    bool m_started;
};

/***************************************************************************************
 * NopRtspClient
 *
 * Pulls encoded video/audio from an IP camera (record / relay) and can push
 * two-way talk audio upstream via the backchannel (byte-push mode: the facade
 * feeds already-encoded G.711/AAC frames, no local mic capture).
 *
 * Lifecycle:
 *   NopRtspClient cln;
 *   cln.setVideoCb(onVideo, ctx);
 *   cln.setAudioCb(onAudio, ctx);
 *   cln.open("rtsp://camera/stream", "admin", "pass");
 *   cln.play();
 *   // talk to camera, per encoded frame:
 *   cln.sendBackchannel(buf, len);
 *   ...
 *   cln.stop();
 ***************************************************************************************/
class NopRtspClient {
public:
    NopRtspClient();
    ~NopRtspClient();

    /**
     * Create the underlying client, register callbacks and start the RTSP connection
     * (OPTIONS/DESCRIBE/SETUP). Call setVideoCb/setAudioCb before open() so frames
     * route from the first packet. Backchannel is NOT negotiated unless
     * setBackchannel(true) was called first (plain pulls stay one-way).
     * @return true on success.
     */
    bool open(const char* url, const char* user, const char* pass);

    /** Opt in to two-way audio (talk-down). When enabled BEFORE open(), the client
     *  arms the byte-push backchannel so DESCRIBE/SETUP negotiate it and play() enables
     *  send. Default: false — a live/record pull must not trigger a backchannel SETUP. */
    void setBackchannel(bool enable);

    /** Register the encoded-video sink. */
    void setVideoCb(NopFrameCb cb, void* ctx);

    /** Register the encoded-audio sink. */
    void setAudioCb(NopFrameCb cb, void* ctx);

    /** Register the connection-event sink (NopRtspEvent). Call before open(). */
    void setEventCb(NopEventCb cb, void* ctx);

    /* ---- transport tuning (apply BEFORE open(); ignored afterwards) ---- */

    /** true => RTP interleaved over the RTSP TCP connection (NVR default, most robust);
     *  false => RTP over UDP. Default: TCP. */
    void setTransportTcp(bool tcp);

    /** TCP connect timeout in seconds (default engine value if <= 0). */
    void setConnTimeout(int sec);

    /** Receive-idle timeout in seconds; on expiry the engine raises NOP_RTSP_EVE_NODATA. */
    void setRxTimeout(int sec);

    /** Negotiated video codec (NopVideoCodec), valid after NOP_RTSP_EVE_CONNSUCC.
     *  Returns NOP_VIDEO_CODEC_NONE if not open / not yet negotiated. */
    int videoCodec() const;

    /**
     * Copy the SDP-advertised video parameter sets (sprop-parameter-sets for H.264;
     * sprop-vps/sps/pps for H.265) into `out` as an Annex-B byte stream — each NAL
     * prefixed with the 4-byte start code 00 00 00 01, concatenated in order
     * (SPS,PPS for H.264; VPS,SPS,PPS for H.265). Real source: the camera's DESCRIBE
     * SDP parsed by the engine (rcua_get_sdp_h264/h265_params) — NOT guessed/synthesized.
     * Valid after NOP_RTSP_EVE_CONNSUCC. Cameras that carry parameter sets only in SDP
     * (never in-band before the IDR) need this so the NVR can prepend a real SPS/PPS to
     * the first IDR for both decode and recording.
     * @param out  destination buffer
     * @param max  size of out in bytes
     * @return number of Annex-B bytes written (0 if none / not open / codec has no SDP params).
     */
    int videoParameterSets(uint8_t* out, int max) const;

    /**
     * Send the PLAY request and enable backchannel data sending
     * (set_bc_data_flag(1)). @return true on success.
     */
    bool play();

    /** TEARDOWN and close the connection, releasing all client resources. */
    void stop();

    /**
     * Push one encoded backchannel (talk) audio frame to the camera. Reuses the
     * existing RTP/SRTP/transport path via CRtspClient::send_backchannel().
     * Only valid after play() when the camera advertised a backchannel.
     * @return true if the frame was queued for transmission.
     */
    bool sendBackchannel(const uint8_t* data, int len);

private:
    NopRtspClient(const NopRtspClient&);
    NopRtspClient& operator=(const NopRtspClient&);

    // Trampolines matching the Happytime C callback signatures. They recover the
    // NopRtspClient* from the engine's single userdata slot and dispatch to the
    // user-registered NopFrameCb.
    static int  videoTrampoline(uint8_t* data, int len, uint32_t ts, uint16_t seq, void* ud);
    static int  audioTrampoline(uint8_t* data, int len, uint32_t ts, uint16_t seq, void* ud);
    static int  notifyTrampoline(int event, void* ud);

    CRtspClient* m_client;
    NopFrameCb   m_videoCb;
    void*        m_videoCtx;
    NopFrameCb   m_audioCb;
    void*        m_audioCtx;
    NopEventCb   m_eventCb;
    void*        m_eventCtx;

    // transport config captured before open(), applied to m_client before rtsp_start
    bool         m_useTcp;
    int          m_connTimeout;   // seconds; <=0 => leave engine default
    int          m_rxTimeout;     // seconds; <=0 => leave engine default
    bool         m_wantBc;        // opt-in backchannel; false => no talk-down SETUP
};

} // namespace nop

/***************************************************************************************
 * Server backchannel-receive integration hook (C linkage).
 *
 * The Happytime server-side backchannel receive path (rtsp_bc_data_cb() in
 * rtsp/rtsp_srv_backchannel.cpp, the non-MEDIA_DEVICE branch) calls this function to
 * hand decoded talk audio to the facade, which forwards it to the sink registered
 * via NopRtspServer::setBackchannelSink(). Declared here so that C source in the
 * rtsp/ module can call it without pulling in the C++ facade class definitions.
 ***************************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

void nop_rtsp_server_on_bc_audio(const uint8_t* data, int size, int nbsamples);

#ifdef __cplusplus
}
#endif

#endif // NOP_RTSP_H
