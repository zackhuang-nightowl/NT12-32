/***************************************************************************************
 *
 *  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
 *
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install, 
 *  copy or use the software.
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *
 *  Redistribution and use in binary forms, with or without modification, are permitted.
 *
 *  Unless required by applicable law or agreed to in writing, software distributed 
 *  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 *  language governing permissions and limitations under the License.
 *
****************************************************************************************/

#ifndef RTSP_RCUA_H
#define RTSP_RCUA_H

#include "rtsp_parse.h"
#include "http.h"
#include "media_info.h"
#ifdef BACKCHANNEL
#ifdef MEDIA_DEVICE
#include "audio_capture.h"
#else
class CAudioCapture;    // byte-push backchannel: no mic-capture/ffmpeg engine
#endif
#endif
#ifdef OVER_WEBSOCKET
#include "ws.h"
#endif
#ifdef SRTP
#include "srtp2/srtp.h"
#endif

#define AV_TYPE_VIDEO       0
#define AV_TYPE_AUDIO       1
#define AV_TYPE_METADATA    2
#define AV_TYPE_BACKCHANNEL 3

#define AV_MAX_CHS          4

#define AV_VIDEO_CH         AV_TYPE_VIDEO
#define AV_AUDIO_CH         AV_TYPE_AUDIO
#define AV_METADATA_CH      AV_TYPE_METADATA
#define AV_BACK_CH          AV_TYPE_BACKCHANNEL

/**
 * RTSP over websocket needs to reserve some space
 * in the header of the send buffer
 */
#define RTSP_SND_RESV_SIZE  32

typedef enum rtsp_client_states
{
    RCS_NULL = 0,
    RCS_OPTIONS,
    RCS_DESCRIBE,
    RCS_INIT_V,
    RCS_INIT_A,
    RCS_INIT_M,
    RCS_INIT_BC,
    RCS_READY,
    RCS_PLAYING,
    RCS_RECORDING,
} RCSTATE;

typedef struct rtsp_client_media_channel
{
    uint32          disabled    : 1;    // not setup this channel
    uint32          srtp_enable : 1;    // srtp enable flag
    uint32          reserved    : 30;
    
    SOCKET          udp_fd;             // udp socket
    SOCKET          rtcp_fd;            // rtcp udp socket
    char            ctl[128];           // control string
    uint16          setup;              // whether the media channel already be setuped
    uint16          r_port;             // remote udp port
    uint16          l_port;             // local udp port
    uint16          interleaved;        // rtp channel values
    char            destination[32];    // multicast address
    int             cap_count;          // Local number of capabilities
    uint8           cap[MAX_AVN];       // Local capability
    char            cap_desc[MAX_AVN][MAX_AVDESCLEN];

#ifdef SRTP
    char            crypto_suite[40];   // crypto suite
    uint8           crypto_key[100];    // crypto key
    srtp_policy_t   srtp_policy;        // srtp policy
    srtp_t          srtp;               // srtp
#endif
} RCMCH;

typedef struct rtsp_client_user_agent
{
    uint32          used_flag   : 1;    // used flag
    uint32          rtp_tcp     : 1;    // rtp over tcp
    uint32          mcast_flag  : 1;    // use rtp multicast, set by user
    uint32          rtp_mcast   : 1;    // use rtp multicast, set by stack
    uint32          rtp_tx      : 1;    // rtp sending flag
    uint32          need_auth   : 1;    // need auth flag
    uint32          auth_mode   : 2;    // 0 - baisc; 1 - digest
    uint32          gp_cmd      : 1;    // is support get_parameter command
    uint32          backchannel : 1;    // audio backchannle flag
    uint32          send_bc_data: 1;    // audio backchannel data sending flag
    uint32          replay      : 1;    // replay flag
    uint32          over_http   : 1;    // rtsp over http flag
    uint32          over_ws     : 1;    // rtsp over websocket flag
    uint32          rtsps       : 1;    // rtsp over ssl flag
    uint32          auth_retry  : 3;    // auth retry numbers
    uint32          reserved    : 14;

    RCSTATE         state;              // state, RCSTATE
    SOCKET          fd;                 // socket handler
    HT_SOCKADDR     sockaddr;           // remote socket address
    uint32          keepalive_time;     // keepalive time

    uint32          cseq;               // seq no                
    char            sid[64];            // Session ID
    char            uri[300];           // rtsp://221.10.50.195:554/cctv.sdp
    char            cbase[256];         // Content-Base: rtsp://221.10.50.195:554/broadcast.sdp/
    char            user_agent[64];     // user agent string 
    int             session_timeout;    // session timeout value
    int             play_start;         // a=range:npt=0-20.735, unit is millisecond
    int             play_end;           // a=range:npt=0-20.735, unit is millisecond
    uint32          video_init_ts;      // video init timestampe
    uint32          audio_init_ts;      // audio init timestampe
    int             seek_pos;           // seek pos
    int             media_start;        // media start time, unit is millisecond

    char            rcv_buf[2052];      // receive buffer
    int             rcv_dlen;           // receive data length    
    int             rtp_t_len;          // rtp payload total length
    int             rtp_rcv_len;        // rtp payload receive length
    char          * rtp_rcv_buf;        // rtp payload receive buffer

    RCMCH           channels[AV_MAX_CHS];   // media channels

    HD_AUTH_INFO    auth_info;          // auth information
    int             video_codec;        // video codec
    AUDIO_INFO      audio_info;         // audio information
    uint8         * audio_spec;         // audio special data, for AAC etc.
    uint32          audio_spec_len;     // audio special data length

#ifdef BACKCHANNEL
    int             bc_audio_device;    // back channel audio device index, start from 0
    AUDIO_INFO      bc_audio_info;      // back channel audio information
    UA_RTP_INFO     bc_rtp_info;        // back channel audio rtp info
    CAudioCapture * bc_audio_captrue;   // back channel audio capture object
#endif

#ifdef REPLAY
    uint32          scale_flag          : 1;
    uint32          rate_control_flag   : 1;
    uint32          immediate_flag      : 1;
    uint32          frame_flag          : 1;
    uint32          frame_interval_flag : 1;
    uint32          range_flag          : 1;
    uint32          replay_reserved     : 26;

    int             scale;              // scale info, when not set the rata control flag, the scale is valid.
                                        //  It shall be either 100.0 or -100.0, to indicate forward or reverse playback respectively. 
                                        //  If it is not present, forward playback is assumed
                                        //  100 means 1.0, Divide by 100 when using
    int             rate_control;       // rate control flag, 
                                        // 1-the stream is delivered in real time using standard RTP timing mechanisms
                                        //  0-the stream is delivered as fast as possible, using only the flow control provided by the transport to limit the delivery rate
    int             immediate;          // 1 - immediately start playing from the new location, cancelling any existing PLAY command.
                                        //    The first packet sent from the new location shall have the D (discontinuity) bit set in its RTP extension header. 
    int             frame;              // 0 - all frames
                                        // 1 - I-frame and P-frame
                                        // 2 - I-frame
    int             frame_interval;     // I-frame interval, unit is milliseconds    
    time_t          replay_start;       // replay start time, calendar time
    time_t          replay_end;         // replay end time, calendar time
#endif

#ifdef OVER_HTTP
    uint16          http_port;          // rtsp over http port
    HTTPREQ         rtsp_send;          // rtsp over http get connection
    HTTPREQ         rtsp_recv;          // rtsp over http post connection
#endif

#ifdef OVER_WEBSOCKET
    uint16          ws_port;            // rtsp over websocket port
    HTTPREQ         ws_http;            // rtsp over websocket connection
    WSMSG           ws_msg;             // rtsp over websocket message handler
#endif

#ifdef RTSPS
    void          * ssl;                // rtsps SSL 
    void          * ssl_mutex;          // rtsps SSL mutex
#endif
} RCUA;


#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************/
HRTSP_MSG * rcua_build_describe(RCUA * p_rua);
HRTSP_MSG * rcua_build_setup(RCUA * p_rua,int type);
HRTSP_MSG * rcua_build_play(RCUA * p_rua);
HRTSP_MSG * rcua_build_pause(RCUA * p_rua);
HRTSP_MSG * rcua_build_teardown(RCUA * p_rua);
HRTSP_MSG * rcua_build_get_parameter(RCUA * p_rua);
HRTSP_MSG * rcua_build_options(RCUA * p_rua);

/*************************************************************************/
int         rcua_ssl_tx(RCUA * p_rua, const char * p_data, int len);
BOOL        rcua_get_media_info(RCUA * p_rua, HRTSP_MSG * rx_msg);

BOOL        rcua_get_sdp_video_desc(RCUA * p_rua, const char * key, int * pt, char * p_sdp, int max_len);
BOOL        rcua_get_sdp_audio_desc(RCUA * p_rua, const char * key, int * pt, char * p_sdp, int max_len);

BOOL        rcua_get_sdp_h264_desc(RCUA * p_rua, int * pt, char * p_sdp, int max_len);
BOOL        rcua_get_sdp_h264_params(RCUA * p_rua, int * pt, char * p_sps_pps, int max_len);

BOOL        rcua_get_sdp_h265_desc(RCUA * p_rua, int * pt, char * p_sdp, int max_len);
BOOL        rcua_get_sdp_h265_params(RCUA * p_rua, int * pt, BOOL * donfield, char * p_vps, int vps_len, char * p_sps, int sps_len, char * p_pps, int pps_len);

BOOL        rcua_get_sdp_mp4_desc(RCUA * p_rua, int * pt, char * p_sdp, int max_len);
BOOL        rcua_get_sdp_mp4_params(RCUA * p_rua, int * pt, char * p_cfg, int max_len);

BOOL        rcua_get_sdp_aac_desc(RCUA * p_rua, int * pt, char * p_sdp, int max_len);
BOOL        rcua_get_sdp_aac_params(RCUA * p_rua, int *pt, int *sizelength, int *indexlength, int *indexdeltalength, char * p_cfg, int max_len);

SOCKET      rcua_init_udp_connection(uint16 port, int family);
SOCKET      rcua_init_mc_connection(uint16 port, char * destination);

/*************************************************************************/
void        rcua_send_rtsp_msg(RCUA * p_rua,HRTSP_MSG * tx_msg);

#define     rcua_send_free_rtsp_msg(p_rua,tx_msg) \
                do { \
                    rcua_send_rtsp_msg(p_rua,tx_msg); \
                    rtsp_free_msg(tx_msg); \
                } while(0)


#ifdef __cplusplus
}
#endif

#endif // RTSP_RCUA_H




