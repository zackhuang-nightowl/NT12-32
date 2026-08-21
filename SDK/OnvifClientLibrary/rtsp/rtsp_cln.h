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

#ifndef RTSP_CLN_H
#define RTSP_CLN_H

#include "sys_buf.h"
#include "rtsp_rcua.h"
#include "media_format.h"
#include "h264_rtp_rx.h"
#include "mjpeg_rtp_rx.h"
#include "h265_rtp_rx.h"
#include "mpeg4_rtp_rx.h"
#include "aac_rtp_rx.h"
#include "pcm_rtp_rx.h"


/*************************************************************************************/

/**
 * @brief
 *  RTSP event notification callback  
 *
 * @param event
 *  Event type, reference RTSP_EVE_XXX
 * @param userdata
 *  User data
 **/
typedef int (*notify_cb)(int event, void *userdata);

/**
 * @brief
 *  RTSP video data callback 
 *
 * @param data
 *  Video data buffer, data is a complete and decodable packet of data
 * @param len
 *  Video data buffer length
 * @param ts
 *  timestamp extracted from RTP header
 * @param seq
 *  Sequence number extracted from RTP header, the Sequence number may not be 
 *  continuous because of the assembly of fragmented packages.
 * @param userdata
 *  User data
 **/
typedef int (*video_cb)(uint8 * data, int len, uint32 ts, uint16 seq, void * userdata);

/**
 * @brief
 *  RTSP audio data callback 
 *
 * @param data
 *  Audio data buffer, data is a complete and decodable packet of data
 * @param len
 *  Audio data buffer length
 * @param ts
 *  timestamp extracted from RTP header
 * @param seq
 *  Sequence number extracted from RTP header, the Sequence number may not be 
 *  continuous because of the assembly of fragmented packages.
 * @param userdata
 *  User data
 **/
typedef int (*audio_cb)(uint8 * data, int len, uint32 ts, uint16 seq, void * userdata);

/**
 * @brief
 *  Rtcp data callback 
 *
 * @param data
 *  Rtcp data buffer, the data contains RTP headers
 * @param len
 *  Rtcp data buffer length
 * @param type
 *  AV_VIDEO_CH - video rtcp data
 *  AV_AUDIO_CH - audio rtcp data
 *  AV_METADATA_CH - metadata rtcp data
 * @param userdata
 *  User data
 **/
typedef int (*rtcp_cb)(uint8 * data, int len, int type, void * userdata);

/**
 * @brief
 *  RTSP metadata data callback 
 *
 * @param data
 *  Metadata data buffer, data is a complete and decodable packet of data
 * @param len
 *  metadata data buffer length
 * @param ts
 *  timestamp extracted from RTP header
 * @param seq
 *  Sequence number extracted from RTP header, the Sequence number may not be 
 *  continuous because of the assembly of fragmented packages.
 * @param userdata
 *  User data
 **/
typedef int (*metadata_cb)(uint8 * data, int len, uint32 ts, uint16 seq, void * userdata);


#define RTSP_EVE_STOPPED    0           // stopped
#define RTSP_EVE_CONNECTING 1           // connecting
#define RTSP_EVE_CONNFAIL   2           // connect failed
#define RTSP_EVE_CONNSUCC   3           // connect success
#define RTSP_EVE_NOSIGNAL   4           // no signal
#define RTSP_EVE_RESUME     5           // resume 
#define RTSP_EVE_AUTHFAILED 6           // authentication failed
#define RTSP_EVE_NODATA     7           // No data received within the timeout period

#define RTSP_RX_FAIL        -1          // rtsp data receive failed
#define RTSP_RX_TIMEOUT     1           // rtsp data receive timeout
#define RTSP_RX_SUCC        2           // rtsp data receive success

#define RTSP_PARSE_FAIL     -1          // rtsp message parse failed 
#define RTSP_PARSE_MOREDATA 0           // need more data to parse rtsp message
#define RTSP_PARSE_SUCC     1           // rtsp message parse success



class CRtspClient
{
public:
    CRtspClient(void);
    ~CRtspClient(void);

public:

    /**
     * @brief
     *  Start rtsp connection  
     *
     * @param suffix
     *  RTSP stream address suffix, such as rtsp://ip:port/xxx, then the 
     *  stream suffix is xxx.
     * @param ip
     *  rtsp server ip
     * @param port
     *  rtsp server port
     * @param user
     *  RTSP authentication username
     * @param pass
     *  RTSP authentication password
     **/
    BOOL    rtsp_start(const char * suffix, const char * ip, int port, const char * user, const char * pass);

    /**
     * @brief
     *  Start rtsp connection  
     *
     * @param url
     *  RTSP stream address
     * @param user
     *  RTSP authentication username
     * @param pass
     *  RTSP authentication password
     **/
    BOOL    rtsp_start(const char * url, const char * user, const char * pass);

    /**
     * @brief
     *  Send Play Request 
     *
     * @param npt_start
     *  Specify the starting playback position in milliseconds.
     **/
    BOOL    rtsp_play(int npt_start = 0);

    /**
     * @brief
     *  Send Teardown Request 
     **/
    BOOL    rtsp_stop();

    /**
     * @brief
     *  Send Pause Request 
     **/
    BOOL    rtsp_pause();

    /**
     * @brief
     *  Close rtsp connection, it will release all resources. 
     **/
    BOOL    rtsp_close();

    /**
     * @brief
     *  Get RCUA object
     *
     * @return RCUA object pointer
     **/
    RCUA *  get_rua() {return &m_rua;}

    /**
     * @brief
     *  Get rtsp stream address
     *
     * @return rtsp stream address
     **/
    char *  get_url() {return m_url;}

    /**
     * @brief
     *  Get rtsp server ip
     *
     * @return rtsp server ip
     **/
    char *  get_ip() {return  m_ip;}

    /**
     * @brief
     *  Get rtsp server port
     *
     * @return rtsp server port
     **/
    int     get_port() {return m_nport;}

    /**
     * @brief
     *  Get rtsp authentication username
     *
     * @return rtsp authentication username
     **/
    char *  get_user() {return m_rua.auth_info.auth_name;}

    /**
     * @brief
     *  Get rtsp authentication password
     *
     * @return rtsp authentication password
     **/
    char *  get_pass() {return m_rua.auth_info.auth_pwd;}

    /**
     * @brief
     *  Set rtsp event notify callback function
     *
     * @param notify
     *  notify callback function pointer
     * @param userdata
     *  user data
     **/
    void    set_notify_cb(notify_cb notify, void * userdata);

    /**
     * @brief
     *  Set video data callback function
     *
     * @param cb
     *  video data callback function pointer
     **/
    void    set_video_cb(video_cb cb);

    /**
     * @brief
     *  Set audio data callback function
     *
     * @param cb
     *  audio data callback function pointer
     **/
    void    set_audio_cb(audio_cb cb);

    /**
     * @brief
     *  Set metadata data callback function
     *
     * @param cb
     *  metadata data callback function pointer
     **/
    void    set_metadata_cb(metadata_cb cb);

    /**
     * @brief
     *  Set rtcp data callback function
     *
     * @param cb
     *  rtcp data callback function pointer
     **/
    void    set_rtcp_cb(rtcp_cb cb);

    /**
     * @brief
     *  Set channel flags.
     *  All channels are setup by default.
     *
     * @param channel
     *  AV_VIDEO_CH : video channel
     *  AV_AUDIO_CH : audio channel
     *  AV_METADATA_CH : metadata channel
     *  AV_BACK_CH : audio back channel
     *
     * @param flag
     *  1 - If the SDP description has the channel information, then SETUP this channel, 
     *  0 - Do not setup this channel 
     **/
    void    set_channel(int channel, int flag);

    /**
     * @brief
     *  Set rtp multicast flag
     *
     * @param flag
     *  1 - enable rtp multcast
     *  0 - disable rtp multcast
     **/
    void    set_rtp_multicast(int flag);

    /**
     * @brief
     *  Set rtp over udp flag
     *
     * @param flag
     *  1 - enable rtp over udp
     *  0 - disable rtp over udp
     **/
    void    set_rtp_over_udp(int flag);

    /**
     * @brief
     *  Set rtsp over http flag
     *
     * @param flag
     *  1 - enable rtsp over http
     *  0 - disable rtsp over http
     * @param port
     *  if flag = 1, specify the http port
     **/
    void    set_rtsp_over_http(int flag, int port);

    /**
     * @brief
     *  Set rtsp over websocket flag
     *
     * @param flag
     *  1 - enable rtsp over websocket
     *  0 - disable rtsp over websocket
     * @param port
     *  if flag = 1, specify the websocket port
     **/
    void    set_rtsp_over_ws(int flag, int port);

    /**
     * @brief
     *  Set rtsp over ssl flag
     *
     * @param flag
     *  1 - enable rtsp over ssl
     *  0 - disable rtsp over ssl
     **/
    void    set_rtsp_over_ssl(int flag);
    
    /**
     * @brief
     *  Set the data rx timeout, if the data is not received within the 
     *  specified time, send RTSP_EVE_NODATA notification.
     *
     * @param timeout
     *  Timeout value, unit is second
     **/
    void    set_rx_timeout(int timeout);

    /**
     * @brief
     *  Set the connect timeout, if the connection is unsuccessful within
     *  the timeout period, an RTSP_EVE_CONNFAIL event notification will 
     *  be sent.
     *
     * @param timeout
     *  Timeout value, unit is second
     **/
    void    set_conn_timeout(int timeout);

    /**
     * @brief
     *  Get SPS, PPS parameters of H264, and return through the video 
     *  callback function
     **/
    void     get_h264_params();

    /**
     * @brief
     *  Get SPS, PPS parameters of H264.
     *
     * @param p_sps 
     *  Receive H264 SPS data, the buffer size is not less than 256
     * @param sps_len
     *  Receive H264 SPS buffer length
     * @param p_pps
     *  Receive H264 PPS data, the buffer size is not less than 256
     * @param pps_len
     *  Receive H264 PPS buffer length
     **/
    BOOL     get_h264_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len);

    /**
     * @brief
     *  Get VPS, SPS, PPS parameters of H265, and return through the video
     *  callback function
     **/
    void     get_h265_params();

    /**
     * @brief
     *  Get VPS, SPS, PPS parameters of H265
     *
     * @param p_sps
     *  Receive H264 SPS data, the buffer size is not less than 256
     * @param sps_len
     *  Receive H264 SPS buffer length
     * @param p_pps
     *  Receive H264 PPS data, the buffer size is not less than 256
     * @param pps_len
     *  Receive H264 PPS buffer length
     * @param p_vps
     *  Receive H264 VPS data, the buffer size is not less than 256
     * @param vps_len
     *  Receive H264 VPS buffer length
     **/
    BOOL    get_h265_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len, uint8 * p_vps, int * vps_len);

    /**
     * @brief
     *  If the describe request response returned by the server has an H264
     *  SDP description, get the H264 SDP description.
     *
     * @param p_sdp
     *  Receive H264 SDP description
     * @param max_len
     *  Maximum length of buffer p_sdp
     **/
    BOOL    get_h264_sdp_desc(char * p_sdp, int max_len);

    /**
     * @brief
     *  If the describe request response returned by the server has an H265 
     *  SDP description, get the H265 SDP description.
     *
     * @param p_sdp
     *  Receive H265 SDP description
     * @param max_len
     *  Maximum length of buffer p_sdp
     **/
    BOOL    get_h265_sdp_desc(char * p_sdp, int max_len);

    /**
     * @brief
     *  If the describe request response returned by the server has an MP4
     *  SDP description, get the MP4 SDP description.
     *
     * @param p_sdp
     *  Receive MP4 SDP description
     * @parm max_len
     *  Maximum length of buffer p_sdp
     **/
    BOOL    get_mp4_sdp_desc(char * p_sdp, int max_len);

    /**
     * @brief
     *  If the describe request response returned by the server has an AAC 
     *  SDP description, get the AAC SDP description.
     *
     * @param p_sdp
     *  Receive AAC SDP description
     * @param max_len
     *  Maximum length of buffer p_sdp
     **/
    BOOL    get_aac_sdp_desc(char * p_sdp, int max_len);

    /**
     * @brief
     *  Get video codec
     *
     * @retrun
     *  reference the media_format.h, VIDEO_CODEC_H264, ...
     **/
    int     audio_codec() {return m_rua.audio_info.codec;}

    /**
     * @brief 
     *  Get audio codec
     *
     * @retrun : reference the media_format.h, AUDIO_CODEC_AAC, ...
     **/
    int     video_codec() {return m_rua.video_codec;}

    /**
     * @brief
     *  Get audio sample rate.
     *
     * @return audio sample rate
     **/
    int     get_audio_samplerate() {return m_rua.audio_info.samplerate;}

    /**
     * @brief
     *  Get audio channel numbers
     *
     * @return audio channel numbers
     **/
    int     get_audio_channels() {return m_rua.audio_info.channels;}

    /**
     * @brief
     *  Get audio bits per sample, for G726 decoder
     *
     **/
    int     get_audio_bitpersample() {return m_rua.audio_info.bitpersample;}
    
    /**
     * @brief
     *  Get audio config data, for AAC
     *
     * @return audio config data
     **/
    uint8 * get_audio_config() {return m_rua.audio_spec;}

    /**
     * @brief
     *  Get audio config data length, for AAC
     *
     * @return audio config data length
     **/
    int     get_audio_config_len() {return m_rua.audio_spec_len;}

    /**
     * @brief
     *  Get the audio backchannel flag
     *
     * @return audio backchannel flag
     **/
    int     get_bc_flag();

    /**
     * @brief
     *  Set the audio backchannel flag
     *
     * @param flag
     *  1 - enable audio backchannel
     *  0 - disable audio backchannel
     **/
    void    set_bc_flag(int flag);

    /**
     * @brief
     *  Get the audio backchannel data sending flag
     *
     * @return audio backchannel data sending flag
     **/
    int     get_bc_data_flag();

    /**
     * @brief
     *  Set the audio backchannel data sending flag
     *
     * @param flag
     *  1 - enable audio backchannel data sending
     *  0 - disable audio backchannel data sending
     **/
    void    set_bc_data_flag(int flag);

    /**
     * @brief
     *  Set the audio backchannel audio capture device index
     *
     * @param index
     *  audio capture device index
     **/
    void    set_audio_device(int index);

    /**
     * @brief
     *  Send audio data over the backchannel (byte-push mode).
     *  Reuses the existing rtsp_bc_cb() RTP TX path; no mic capture needed.
     *
     * @param data
     *  encoded audio payload (e.g. G.711)
     * @param len
     *  payload length in bytes
     * @param nbsamples
     *  number of samples (optional)
     *
     * @return
     *  TRUE on success, FALSE if backchannel is not active
     **/
    BOOL    send_backchannel(uint8 * data, int len, int nbsamples = 0);

    /**
     * @brief
     *  Get the replay flag
     *
     * @return replay flag
     **/
    int     get_replay_flag();

    /**
     * @brief
     *  Set the replay flag
     *
     * @param flag
     *  1 - enable replay
     *  0 - disable replay
     **/
    void    set_replay_flag(int flag);

    /**
     * @brief
     *  Set the scale info
     *
     * @param scale
     *  when not set the rata control flag, the scale is valid.
     *  It shall be either 1.0 or -1.0, to indicate forward or reverse 
     *  playback respectively. 
     *  If it is not present, forward playback is assumed
     **/
    void    set_scale(double scale);

    /**
     * @brief
     *  Set the rate control flag
     *
     * @param flag
     *  1-the stream is delivered in real time using standard RTP timing 
     *  mechanisms
     *  0-the stream is delivered as fast as possible, using only the flow
     *  control provided by the transport to limit the delivery rate
     **/
    void    set_rate_control_flag(int flag);

    /**
     * @brief
     *  Set the immediate flag
     *
     * @param flag
     *  1 - immediately start playing from the new location, cancelling any 
     *  existing PLAY command.
     *  The first packet sent from the new location shall have the D (discontinuity)
     *  bit set in its RTP extension header. 
     **/
    void    set_immediate_flag(int flag);

    /**
     * @brief
     *  Set the frame flag
     *
     * @param flag
     *  0 - all frames
     *  1 - I-frame and P-frame
     *  2 - I-frame
     * @param interval
     *  when flag = 2, set the I-frame interval
     **/
    void    set_frames_flag(int flag, int interval);

    /**
     * @brief
     *  St replay range
     *
     * @param start
     *  the replay start timestamp, calendar time
     * @param end
     *  the replay end timestamp, calendar time
     **/
    void    set_replay_range(time_t start, time_t end);

    void    tcp_rx_thread();
    void    udp_rx_thread();
    void    rtsp_video_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq);
    void    rtsp_audio_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq);
    void    rtsp_meta_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq);
    
private:
    void    set_default();
    void    send_notify(int event);
    BOOL    rtsp_client_start();
    void    rtsp_client_stop(RCUA * p_rua);
    BOOL    rtsp_client_state(RCUA * p_rua, HRTSP_MSG * rx_msg);
    int     rtsp_epoll_rx();
    int     rtsp_tcp_data_rx(RCUA * p_rua);
    int     rtsp_over_tcp_rx(RCUA * p_rua);
    int     rtsp_tcp_rx();
    int     rtsp_udp_rx();
    int     rtsp_msg_parser(RCUA * p_rua);
    void    rtsp_keep_alive();
    BOOL    rtsp_setup_channel(RCUA * p_rua, int av_t);
    BOOL    rtsp_setup_srtp(int ch);
    BOOL    rtsp_setup_srtp(RCUA * p_rua);
    BOOL    rtsp_prepare_play();
    BOOL    rtsp_unauth_res(RCUA * p_rua, HRTSP_MSG * rx_msg);
    BOOL    rtsp_options_res(RCUA * p_rua, HRTSP_MSG * rx_msg);
    BOOL    rtsp_describe_res(RCUA * p_rua, HRTSP_MSG * rx_msg);
    BOOL    rtsp_setup_res(RCUA * p_rua, HRTSP_MSG * rx_msg, int av_t);
    BOOL    rtsp_play_res(RCUA * p_rua, HRTSP_MSG * rx_msg);

    BOOL    rtsp_init_backchannel(RCUA * p_rua);
    BOOL    rtsp_get_transport_info(RCUA * p_rua, HRTSP_MSG * rx_msg, int av_type);
    void    rtsp_get_video_size(int * w, int * h);
    BOOL    rtsp_get_crypto_info(int ch);
    BOOL    rtsp_get_video_media_info();
    BOOL    rtsp_get_audio_media_info(int ch, AUDIO_INFO * p_info);
    
    void    tcp_data_rx(uint8 * data, int rlen);
    void    udp_data_rx(uint8 * data, int rlen, int type);
    void    rtcp_data_rx(uint8 * data, int rlen, int type);
    BOOL    srtp_tcp_data_rx(int interleaved, uint8 * p_rtp, int * rtp_len);
    BOOL    srtp_udp_data_rx(int channel, int is_rtcp, uint8 * p_rtp, int * rtp_len);
        
    void    rtsp_send_h264_params(RCUA * p_rua);    
    void    rtsp_send_h265_params(RCUA * p_rua);
    void    rtsp_get_mpeg4_config(RCUA * p_rua);
    void    rtsp_get_aac_config(RCUA * p_rua);

    int     rtsp_build_http_get_req(void * p_user, char * bufs, int buflen, char * cookie);
    int     rtsp_build_http_post_req(void * p_user, char * bufs, int buflen, char * cookie);
    BOOL    rtsp_over_http_start();
    int     rtsp_over_http_rx(RCUA * p_rua);
    
    int     rtsp_build_ws_req(void * p_user, char * bufs, int buflen);
    BOOL    rtsp_over_ws_start();
    BOOL    rtsp_ws_send_msg(RCUA * p_rua, int opcode, uint8 have_mask, char * buff, int len);
    BOOL    rtsp_ws_ping_process(RCUA * p_rua, char * buff, int len);
    BOOL    rtsp_ws_rtsp_process(RCUA * p_rua, char * buff, int len);
    BOOL    rtsp_ws_data_process(RCUA * p_rua);
    int     rtsp_over_ws_rx(RCUA * p_rua);
    BOOL    rtsp_ssl_conn(RCUA * p_rua, int timeout);
    
private:
    RCUA            m_rua;
    char            m_url[360];
    char            m_ip[128];
    char            m_suffix[200];
    int             m_nport;

    int             m_ep_fd;
    void          * m_ep_events;
    int             m_ep_event_num;

    notify_cb       m_pNotify;
    void          * m_pUserdata;
    video_cb        m_pVideoCB;
    audio_cb        m_pAudioCB;
    rtcp_cb         m_pRtcpCB;
    metadata_cb     m_pMetadataCB;
    void          * m_pMutex;
    int             m_nRxTimeout;
    int             m_nConnTimeout;
    int             m_nWidth;
    int             m_nHeight;
    
    union {
        H264RXI     h264rxi;
        H265RXI     h265rxi;
        MJPEGRXI    mjpegrxi;
        MPEG4RXI    mpeg4rxi;
    };

    union {
        AACRXI      aacrxi;
        PCMRXI      pcmrxi;
    };

    PCMRXI          metadatarxi;
    
    BOOL            m_bRunning;
    pthread_t       m_tcpRxTid;
    pthread_t       m_udpRxTid;
};



#endif  // RTSP_CLN_H



