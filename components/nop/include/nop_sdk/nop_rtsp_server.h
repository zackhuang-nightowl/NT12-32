/**
 * @file nop_rtsp_server.h
 * @brief Minimal RTSP media server — turns the nop_media frame source into
 *        pullable RTSP streams, and accepts an ONVIF talk backchannel.
 *
 * This is the streaming half of the media plane (the control half is the ONVIF
 * server). It subscribes to nop_media and serves encoded video (H.264/H.265)
 * to RTSP clients as RTP, so an ONVIF/RTSP client (ODM, VLC, the mobile app)
 * can pull rtsp://<ip>:<port>/main and /sub. Talk uses the ONVIF backchannel:
 * audio the client sends upstream is handed to a callback (route it to
 * hal_audio_if.write_audio_frame for the device speaker).
 *
 * Scope/transport: RTSP over TCP with RTP/AVP/TCP *interleaved* delivery only
 * (Transport: RTP/AVP/TCP;interleaved=0-1) — universally supported by ONVIF
 * clients/VLC and NAT-friendly (no UDP port negotiation). Two video streams by
 * path: "/main" (channel 0, stream 0) and "/sub" (channel 0, stream 1).
 * Backchannel audio is G.711 (PCMU/PCMA) passthrough. No auth in this version.
 *
 * POSIX only (TCP + pthreads).
 *
 *   nop_media_t *media = nop_media_create();
 *   nop_media_bind_hal_video(media);              // 8856 encoder -> media
 *   nop_rtsp_server_config_t cfg = {0};
 *   cfg.port = 554; cfg.media = media;
 *   cfg.backchannel = my_talk_sink;               // -> hal_audio write_audio_frame
 *   nop_rtsp_server_t *rtsp = nop_rtsp_server_start(&cfg);
 *   ...
 *   nop_rtsp_server_stop(rtsp);
 */
#ifndef NOP_SDK_RTSP_SERVER_H
#define NOP_SDK_RTSP_SERVER_H

#include "nop_sdk/nop_media.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque RTSP server handle. */
typedef struct nop_rtsp_server nop_rtsp_server_t;

/**
 * Backchannel audio sink: @p data/@p len is one received audio payload (G.711
 * bytes, the RTP header already stripped) for @p channel. Route it to the
 * device speaker (hal_audio_if.write_audio_frame). Invoked on the client's
 * connection thread; keep it quick or copy.
 */
typedef void (*nop_rtsp_backchannel_fn)(void *ctx, int channel,
                                        const uint8_t *data, size_t len);

/** Server configuration. Zero-initialize; media is required. */
typedef struct nop_rtsp_server_config {
    int                     port;               /**< RTSP port (0 => 554) */
    nop_media_t            *media;              /**< video frame source (required) */
    int                     enable_backchannel; /**< 1: advertise + accept talk audio */
    nop_rtsp_backchannel_fn backchannel;        /**< talk-audio sink (may be NULL) */
    void                   *backchannel_ctx;
} nop_rtsp_server_config_t;

/**
 * Start the RTSP server: bind/listen, subscribe to @p config->media, and serve
 * on a background accept thread. Returns NULL on failure (bad config / bind /
 * thread). @p config->media must outlive the server.
 */
nop_rtsp_server_t *nop_rtsp_server_start(const nop_rtsp_server_config_t *config);

/** Stop the server, drop all client sessions, and unsubscribe. NULL-safe. */
void nop_rtsp_server_stop(nop_rtsp_server_t *server);

/** @return the bound TCP port, or 0 if @p server is NULL. */
int nop_rtsp_server_port(const nop_rtsp_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_RTSP_SERVER_H */
