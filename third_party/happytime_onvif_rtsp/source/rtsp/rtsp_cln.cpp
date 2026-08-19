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

#include "sys_inc.h"
#include "rtp.h"
#include "rtsp_cln.h"
#include "mpeg4.h"
#include "mpeg4_rtp_rx.h"
#include "base64.h"
#include "rtsp_util.h"
#include "net_util.h"
#if defined(HTTPS) || defined(RTSPS)
#include "openssl/ssl.h"
#endif

#ifdef BACKCHANNEL
#include "rtsp_backchannel.h"
#if __WINDOWS_OS__
#include "audio_capture_win.h"
#elif defined(ANDROID)
#include "audio_capture_android.h"
#elif defined(IOS)
#include "audio_capture_mac.h"
#elif __LINUX_OS__
#include "audio_capture_linux.h"
#endif
#endif

#if defined(OVER_HTTP) || defined(OVER_WEBSOCKET)
#include "http_parse.h"
#include "http_cln.h"
#endif

/***************************************************************************************/
void * rtsp_tcp_rx_thread(void * argv)
{
    CRtspClient * pRtsp = (CRtspClient *)argv;

    pRtsp->tcp_rx_thread();

    return NULL;
}

void * rtsp_udp_rx_thread(void * argv)
{
    CRtspClient * pRtsp = (CRtspClient *)argv;

    pRtsp->udp_rx_thread();

    return NULL;
}

int video_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CRtspClient * pthis = (CRtspClient *)p_userdata;

    pthis->rtsp_video_data_cb(p_data, len, ts, seq);

    return 0;
}

int audio_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CRtspClient * pthis = (CRtspClient *)p_userdata;

    pthis->rtsp_audio_data_cb(p_data, len, ts, seq);

    return 0;
}

int metadata_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CRtspClient * pthis = (CRtspClient *)p_userdata;

    pthis->rtsp_meta_data_cb(p_data, len, ts, seq);

    return 0;
}

/***************************************************************************************/
CRtspClient::CRtspClient(void)
{
    m_pNotify = NULL;
    m_pUserdata = NULL;
    m_pVideoCB = NULL;
    m_pAudioCB = NULL;
    m_pRtcpCB = NULL;
#ifdef METADATA    
    m_pMetadataCB = NULL;
#endif    
    m_pMutex = sys_os_create_mutex();
    
    m_bRunning = TRUE;
    m_tcpRxTid = 0;
    m_udpRxTid = 0;

    m_nRxTimeout = 10;  // Data receiving timeout, default 10s timeout
    m_nConnTimeout = 5; // Connect timeout, default 5s timeout
    
#ifdef EPOLL
    m_ep_event_num = AV_MAX_CHS * 2 + 2;
    
    m_ep_fd = epoll_create(m_ep_event_num);
    if (m_ep_fd < 0)
    {
        log_print(HT_LOG_ERR, "%s, epoll_create failed\r\n", __FUNCTION__);
    }

    m_ep_events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * m_ep_event_num);
    if (m_ep_events == NULL)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
    }
#endif

    memset(&h265rxi, 0, sizeof(H265RXI));
    memset(&aacrxi, 0, sizeof(AACRXI));
    memset(&metadatarxi, 0, sizeof(PCMRXI));

    set_default();
}

CRtspClient::~CRtspClient(void)
{
    rtsp_close();

#ifdef EPOLL
    if (m_ep_fd)
    {
        close(m_ep_fd);
        m_ep_fd = 0;        
    }

    if (m_ep_events)
    {
        free(m_ep_events);
        m_ep_events = NULL;
    }
#endif

    if (m_pMutex)
    {
        sys_os_destroy_mutex(m_pMutex);
        m_pMutex = NULL;
    }
}

void CRtspClient::set_default()
{
    memset(&m_rua, 0, sizeof(RCUA));
    memset(&m_url, 0, sizeof(m_url));
    memset(&m_ip, 0, sizeof(m_ip));
    memset(&m_suffix, 0, sizeof(m_suffix));

    m_nWidth = m_nHeight = 0;
    m_nport = 554;
    m_rua.rtp_tcp = 1;  // default RTP over RTSP
    m_rua.session_timeout = 60;
    strcpy(m_rua.user_agent, "happytimesoft rtsp client");
    m_rua.video_init_ts = -1;
    m_rua.audio_init_ts = -1;
    
    m_rua.audio_spec = NULL;
    m_rua.audio_spec_len = 0;
        
    m_rua.video_codec = VIDEO_CODEC_NONE;
    m_rua.audio_info.codec = AUDIO_CODEC_NONE;
    m_rua.audio_info.samplerate = 8000;
    m_rua.audio_info.channels = 1;
    m_rua.audio_info.bitpersample = 0;
    
#ifdef BACKCHANNEL
    m_rua.bc_audio_info.codec = AUDIO_CODEC_NONE;
    m_rua.bc_audio_info.samplerate = 0;
    m_rua.bc_audio_info.channels = 0;
    m_rua.bc_audio_info.bitpersample = 0;
#endif
}

BOOL CRtspClient::rtsp_client_start()
{
    int len;
    int timeout = m_nConnTimeout*1000;
    RCUA * p_rua = &m_rua;

    if (!get_address_by_name(m_ip, HT_PROTOCOL_TCP, &p_rua->sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid ip: %s\r\n", __FUNCTION__, m_ip);
        return FALSE;
    }

    if (p_rua->sockaddr.ipv4_flag)
    {
        p_rua->sockaddr.ipv4_addr.sin_port = htons(m_nport);
        p_rua->fd = tcp_connect_timeout((struct sockaddr *) &p_rua->sockaddr.ipv4_addr, timeout);
    }
    else if (p_rua->sockaddr.ipv6_flag)
    {
        p_rua->sockaddr.ipv6_addr.sin6_port = htons(m_nport);
        p_rua->fd = tcp_connect_timeout((struct sockaddr *) &p_rua->sockaddr.ipv6_addr, timeout);
    }
    
    if (p_rua->fd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, %s:%d connect fail!!!\r\n", __FUNCTION__, m_ip, m_nport);
        return FALSE;
    }

#ifdef RTSPS
    if (p_rua->rtsps)
    {
        if (!rtsp_ssl_conn(p_rua, timeout))
        {
            return FALSE;
        }
    }
#endif

    len = 2 * 1024 * 1024;
        
    if (setsockopt(p_rua->fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\n", __FUNCTION__);
    }

    if (setsockopt(p_rua->fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\n", __FUNCTION__);
    }

    m_rua.cseq = 1;
    m_rua.state = RCS_OPTIONS;

    HRTSP_MSG * tx_msg = rcua_build_options(&m_rua);
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);
    }

    return TRUE;
}

void CRtspClient::rtsp_send_h264_params(RCUA * p_rua)
{
    int pt;
    char sps[1000], pps[1000] = {'\0'};
    
    if (!rcua_get_sdp_h264_params(p_rua, &pt, sps, sizeof(sps)))
    {
        return;
    }

    char * ptr = strchr(sps, ',');
    if (ptr && ptr[1] != '\0')
    {
        *ptr = '\0';
        strcpy(pps, ptr+1);
    }

    uint8 sps_pps[1000];
    sps_pps[0] = 0x0;
    sps_pps[1] = 0x0;
    sps_pps[2] = 0x0;
    sps_pps[3] = 0x1;
    
    int len = base64_decode(sps, strlen(sps), sps_pps+4, sizeof(sps_pps)-4);
    if (len <= 0)
    {
        return;
    }

    sys_os_mutex_enter(m_pMutex);
    
    if (m_pVideoCB)
    {
        m_pVideoCB(sps_pps, len+4, 0, 0, m_pUserdata);
    }
    
    if (pps[0] != '\0')
    {        
        len = base64_decode(pps, strlen(pps), sps_pps+4, sizeof(sps_pps)-4);
        if (len > 0)
        {
            if (m_pVideoCB)
            {
                m_pVideoCB(sps_pps, len+4, 0, 0, m_pUserdata);
            }
        }
    }

    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::rtsp_send_h265_params(RCUA * p_rua)
{
    int pt;
    char vps[512] = {'\0'}, sps[512] = {'\0'}, pps[512] = {'\0'};
            
    if (!rcua_get_sdp_h265_params(p_rua, &pt, &h265rxi.rxf_don, vps, sizeof(vps)-1, sps, sizeof(sps)-1, pps, sizeof(pps)-1))
    {
        return;
    }

    uint8 buff[1024];
    buff[0] = 0x0;
    buff[1] = 0x0;
    buff[2] = 0x0;
    buff[3] = 0x1;

    sys_os_mutex_enter(m_pMutex);
    
    if (vps[0] != '\0')
    {
        int len = base64_decode(vps, strlen(vps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            sys_os_mutex_leave(m_pMutex);
            return;
        }

        if (m_pVideoCB)
        {
            m_pVideoCB(buff, len+4, 0, 0, m_pUserdata);
        }
    }

    if (sps[0] != '\0')
    {
        int len = base64_decode(sps, strlen(sps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            sys_os_mutex_leave(m_pMutex);
            return;
        }

        if (m_pVideoCB)
        {
            m_pVideoCB(buff, len+4, 0, 0, m_pUserdata);
        }
    }

    if (pps[0] != '\0')
    {
        int len = base64_decode(pps, strlen(pps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            sys_os_mutex_leave(m_pMutex);
            return;
        }

        if (m_pVideoCB)
        {
            m_pVideoCB(buff, len+4, 0, 0, m_pUserdata);
        }
    }

    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::rtsp_get_mpeg4_config(RCUA * p_rua)
{
    int pt;
    char config[1000];
    
    if (!rcua_get_sdp_mp4_params(p_rua, &pt, config, sizeof(config)-1))
    {
        return;
    }

    uint32  configLen;
    uint8 * configData = mpeg4_parse_config(config, configLen);
    if (configData)
    {                
        mpeg4rxi.hdr_len = configLen;
        memcpy(mpeg4rxi.buf, configData, configLen);

        free(configData);   
    }
}

void CRtspClient::rtsp_get_aac_config(RCUA * p_rua)
{
    int pt = 0;
    int sizelength = 13;
    int indexlength = 3;
    int indexdeltalength = 3;
    char config[128];
    
    if (rcua_get_sdp_aac_params(p_rua, &pt, &sizelength, &indexlength, &indexdeltalength, config, sizeof(config)))
    {
        m_rua.audio_spec = mpeg4_parse_config(config, m_rua.audio_spec_len);
    }
    
    aacrxi.size_length = sizelength;
    aacrxi.index_length = indexlength;
    aacrxi.index_delta_length = indexdeltalength;
}


/***********************************************************************
*
* Close RCUA
*
************************************************************************/
void CRtspClient::rtsp_client_stop(RCUA * p_rua)
{
    if (p_rua->fd > 0)
    {
        HRTSP_MSG * tx_msg = rcua_build_teardown(p_rua);
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(p_rua,tx_msg);
        }    
    }
}

BOOL CRtspClient::rtsp_setup_channel(RCUA * p_rua, int av_t)
{
    HRTSP_MSG * tx_msg = NULL;
    
    p_rua->state = (RCSTATE)(RCS_INIT_V + av_t);

    if (p_rua->rtp_tcp)
    {
        p_rua->channels[av_t].interleaved = av_t * 2;
    }
    else if (p_rua->rtp_mcast)
    {
        if (p_rua->channels[av_t].r_port)
        {
            p_rua->channels[av_t].l_port = p_rua->channels[av_t].r_port;
        }
        else
        {
            p_rua->channels[av_t].l_port = p_rua->channels[av_t].r_port = rtsp_get_udp_port();
        }
    }
    else
    {
        p_rua->channels[av_t].l_port = rtsp_get_udp_port();

        int family = AF_INET;

        if (p_rua->sockaddr.ipv6_flag)
        {
            family = AF_INET6;
        }

        p_rua->channels[av_t].udp_fd = rcua_init_udp_connection(p_rua->channels[av_t].l_port, family);
        if (p_rua->channels[av_t].udp_fd <= 0)
        {
            return FALSE;
        }

        p_rua->channels[av_t].rtcp_fd = rcua_init_udp_connection(p_rua->channels[av_t].l_port+1, family);
        if (p_rua->channels[av_t].rtcp_fd <= 0)
        {
            log_print(HT_LOG_WARN, "%s, init rtcp udp connection failed\r\n", __FUNCTION__);
        }

#ifdef EPOLL
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.u64 = m_rua.channels[av_t].udp_fd;
        epoll_ctl(m_ep_fd, EPOLL_CTL_ADD, m_rua.channels[av_t].udp_fd, &event);

        if (m_rua.channels[av_t].rtcp_fd > 0)
        {
            event.events = EPOLLIN;
            event.data.u64 = m_rua.channels[av_t].rtcp_fd;
            epoll_ctl(m_ep_fd, EPOLL_CTL_ADD, m_rua.channels[av_t].rtcp_fd, &event);
        }
#endif
    }
    
    tx_msg = rcua_build_setup(p_rua, av_t);
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(p_rua, tx_msg);
    }
    
    return TRUE;        
}

BOOL CRtspClient::rtsp_get_transport_info(RCUA * p_rua, HRTSP_MSG * rx_msg, int av_t)
{
    BOOL ret = FALSE;
    
    if (p_rua->rtp_tcp)
    {
        ret = rtsp_get_tcp_transport_info(rx_msg, &p_rua->channels[av_t].interleaved);    
    }
    else if (p_rua->rtp_mcast)
    {
        ret = rtsp_get_mc_transport_info(rx_msg, p_rua->channels[av_t].destination, &p_rua->channels[av_t].r_port);
    }
    else
    {
        ret = rtsp_get_udp_transport_info(rx_msg, &p_rua->channels[av_t].l_port, &p_rua->channels[av_t].r_port);
    }

    return ret;
}

BOOL CRtspClient::rtsp_unauth_res(RCUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = NULL;
    
    if (!p_rua->need_auth || p_rua->auth_retry < 3)
    {
        p_rua->need_auth = TRUE;
        
        if (rtsp_get_digest_info(rx_msg, &p_rua->auth_info))
        {
            p_rua->auth_mode = 1;
            snprintf(p_rua->auth_info.auth_uri, sizeof(p_rua->auth_info.auth_uri), "%s", p_rua->uri);                
        }
        else
        {
            p_rua->auth_mode = 0;
        }

        p_rua->cseq++;
        p_rua->auth_retry++;

        switch (p_rua->state)
        {
        case RCS_OPTIONS:
            tx_msg = rcua_build_options(p_rua);
            break;
            
        case RCS_DESCRIBE:
            tx_msg = rcua_build_describe(p_rua);
            break;

        case RCS_INIT_V:
            tx_msg = rcua_build_setup(p_rua, AV_TYPE_VIDEO);
            break;

        case RCS_INIT_A:
            tx_msg = rcua_build_setup(p_rua, AV_TYPE_AUDIO);
            break;

        case RCS_INIT_M:
            tx_msg = rcua_build_setup(p_rua, AV_TYPE_METADATA);
            break;

        case RCS_INIT_BC:
            tx_msg = rcua_build_setup(p_rua, AV_TYPE_BACKCHANNEL);
            break;

        case RCS_READY:
            tx_msg = rcua_build_play(p_rua);
            break;

        case RCS_PLAYING:
        case RCS_RECORDING:
            if (m_rua.gp_cmd) // the rtsp server supports GET_PARAMETER command
            {
                tx_msg = rcua_build_get_parameter(p_rua);
            }
            else
            {
                tx_msg = rcua_build_options(p_rua);
            }
            break;
            
        default:
            break;
        }
        
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(p_rua, tx_msg);
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        send_notify(RTSP_EVE_AUTHFAILED);
        return FALSE;
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_options_res(RCUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = NULL;
    
    if (rx_msg->msg_sub_type == 200)
    {
        // get supported command list
        p_rua->gp_cmd = rtsp_is_line_exist(rx_msg, "Public", "GET_PARAMETER");
        
        p_rua->cseq++;
        p_rua->state = RCS_DESCRIBE;
        
        tx_msg = rcua_build_describe(p_rua);
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(p_rua, tx_msg);
        }
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_describe_res(RCUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (rx_msg->msg_sub_type == 200)
    {
        char cseq_buf[32];
        char cbase[256];
        
        // Session
        rtsp_get_session_info(rx_msg, p_rua->sid, sizeof(p_rua->sid)-1, &p_rua->session_timeout);

        rtsp_get_msg_cseq(rx_msg, cseq_buf, sizeof(cseq_buf)-1);

        // Content-Base            
        if (rtsp_get_cbase_info(rx_msg, cbase, sizeof(cbase)-1))
        {
            strncpy(p_rua->uri, cbase, sizeof(p_rua->uri)-1);
        }

        rtsp_get_sdp_range(rx_msg, &p_rua->play_start, &p_rua->play_end);

        rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_VIDEO_CH].ctl, "video", sizeof(p_rua->channels[AV_VIDEO_CH].ctl)-1);

#ifdef BACKCHANNEL
        if (p_rua->backchannel)
        {
            p_rua->backchannel = rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_BACK_CH].ctl, "audio", sizeof(p_rua->channels[AV_BACK_CH].ctl)-1, "sendonly");
            if (p_rua->backchannel)
            {
                rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_AUDIO_CH].ctl, "audio", sizeof(p_rua->channels[AV_AUDIO_CH].ctl)-1, "recvonly");
            }
            else
            {
                rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_AUDIO_CH].ctl, "audio", sizeof(p_rua->channels[AV_AUDIO_CH].ctl)-1);
            }
        }
        else
#endif
        rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_AUDIO_CH].ctl, "audio", sizeof(p_rua->channels[AV_AUDIO_CH].ctl)-1);

#ifdef METADATA
        rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_METADATA_CH].ctl, "application", sizeof(p_rua->channels[AV_METADATA_CH].ctl)-1);
#endif

        if (rcua_get_media_info(p_rua, rx_msg))
        {
            rtsp_get_video_media_info();
            rtsp_get_audio_media_info(AV_AUDIO_CH, &m_rua.audio_info);

            if (VIDEO_CODEC_JPEG == m_rua.video_codec)
            {
                // Try to obtain the video size from the SDP based on a=x-dimensions
                rtsp_get_video_size(&m_nWidth, &m_nHeight);
            }
            
#ifdef BACKCHANNEL
            if (p_rua->backchannel)
            {
                rtsp_get_audio_media_info(AV_BACK_CH, &m_rua.bc_audio_info);
            }
#endif

#ifdef SRTP
            for (int i = 0; i < AV_MAX_CHS; i++)
            {
                rtsp_get_crypto_info(i);
            }
#endif
        }

        if (p_rua->mcast_flag)
        {
            p_rua->rtp_mcast = rtsp_is_support_mcast(rx_msg);
        }

        p_rua->cseq++;

        for (int i = 0; i < AV_MAX_CHS; i++)
        {
            if (p_rua->channels[i].ctl[0] != '\0' && !p_rua->channels[i].disabled)
            {
                return rtsp_setup_channel(p_rua, i);
            }
        }
        
        return FALSE;
    }    
#ifdef BACKCHANNEL    
    else if (p_rua->backchannel) // the server don't support backchannel
    {
        p_rua->backchannel = 0;

        p_rua->cseq++;
        p_rua->state = RCS_DESCRIBE;
        
        HRTSP_MSG * tx_msg = rcua_build_describe(p_rua);
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(p_rua, tx_msg);
        }
    }
#endif
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL CRtspClient::rtsp_setup_res(RCUA * p_rua, HRTSP_MSG * rx_msg, int av_t)
{
    if (rx_msg->msg_sub_type == 200)
    {
        int i;
        char cbase[256];
        HRTSP_MSG * tx_msg = NULL;
        
        // Content-Base            
        if (rtsp_get_cbase_info(rx_msg, cbase, sizeof(cbase)-1))
        {
            strncpy(p_rua->uri, cbase, sizeof(p_rua->uri)-1);
            snprintf(p_rua->auth_info.auth_uri, sizeof(p_rua->auth_info.auth_uri), "%s", p_rua->uri);
        }
        
        // Session
        if (p_rua->sid[0] == '\0')
        {
            rtsp_get_session_info(rx_msg, p_rua->sid, sizeof(p_rua->sid)-1, &p_rua->session_timeout);
        }

        if (!rtsp_get_transport_info(p_rua, rx_msg, av_t))
        {
            return FALSE;
        }
        
        if (p_rua->rtp_mcast)
        {
            p_rua->channels[av_t].l_port = p_rua->channels[av_t].r_port;
            
            p_rua->channels[av_t].udp_fd = rcua_init_mc_connection(p_rua->channels[av_t].l_port, p_rua->channels[av_t].destination);
            if (p_rua->channels[av_t].udp_fd <= 0)
            {
                return FALSE;
            }

            p_rua->channels[av_t].rtcp_fd = rcua_init_mc_connection(p_rua->channels[av_t].l_port+1, p_rua->channels[av_t].destination);
            if (p_rua->channels[av_t].rtcp_fd <= 0)
            {
                log_print(HT_LOG_WARN, "%s, init rtcp multicast connection failed\r\n", __FUNCTION__);
            }
        }
        
        if (p_rua->sid[0] == '\0')
        {
            snprintf(p_rua->sid, sizeof(p_rua->sid), "%x%x", rand(), rand());
        }

#ifdef SRTP
        if (p_rua->channels[av_t].srtp_enable)
        {
            if (!rtsp_setup_srtp(av_t))
            {
                return FALSE;
            }
        }
#endif

#ifdef BACKCHANNEL
        if (AV_BACK_CH == av_t)
        {
            p_rua->bc_rtp_info.rtp_cnt = rand() & 0xFFFF;
            p_rua->bc_rtp_info.rtp_pt = p_rua->channels[AV_BACK_CH].cap[0];
            p_rua->bc_rtp_info.rtp_ssrc = rand();
        }
#endif

        p_rua->channels[av_t].setup = 1;
        p_rua->cseq++;

        for (i = av_t+1; i < AV_MAX_CHS; i++)
        {
            if (p_rua->channels[i].ctl[0] != '\0' && !p_rua->channels[i].disabled)
            {
                return rtsp_setup_channel(p_rua, i);
            }
        }

        if (rtsp_prepare_play() == FALSE)
        {
            return FALSE;
        }
        
        p_rua->state = RCS_READY;
        
        tx_msg = rcua_build_play(p_rua);
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(p_rua,tx_msg);
        }
    }
    else if (p_rua->rtp_tcp) // maybe the server don't support rtp over tcp, try rtp over udp
    {
        p_rua->rtp_tcp = 0;

        p_rua->cseq++;
        
        if (!rtsp_setup_channel(p_rua, av_t))
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_play_res(RCUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (rx_msg->msg_sub_type == 200)
    {
        // Session
        rtsp_get_session_info(rx_msg, p_rua->sid, sizeof(p_rua->sid)-1, &p_rua->session_timeout);

        rtsp_get_rtp_info(rx_msg, p_rua->channels[AV_VIDEO_CH].ctl, &p_rua->video_init_ts, 
            p_rua->channels[AV_AUDIO_CH].ctl, &p_rua->audio_init_ts);

        if (p_rua->seek_pos > 0)
        {
            p_rua->media_start = p_rua->seek_pos;
            p_rua->seek_pos = 0;
        }
        
        if (p_rua->state == RCS_PLAYING)
        {
            return TRUE;
        }
        
        p_rua->state = RCS_PLAYING;
        p_rua->keepalive_time = sys_os_get_uptime();
        
        if (m_rua.audio_info.codec == AUDIO_CODEC_AAC)
        {
            rtsp_get_aac_config(p_rua);
        }
        
        send_notify(RTSP_EVE_CONNSUCC);

        if (m_rua.video_codec == VIDEO_CODEC_H264)
        {
            rtsp_send_h264_params(p_rua);
        }    
        else if (m_rua.video_codec == VIDEO_CODEC_MP4)
        {
            rtsp_get_mpeg4_config(p_rua);
        }
        else if (m_rua.video_codec == VIDEO_CODEC_H265)
        {
            rtsp_send_h265_params(p_rua);
        }

#ifdef BACKCHANNEL
        if (p_rua->backchannel)
        {
            rtsp_init_backchannel(p_rua);
        }
#endif        
    }
    else
    {
        //error handle
        return FALSE;
    }

    return TRUE;
}

#ifdef METADATA

void CRtspClient::set_metadata_cb(metadata_cb cb)
{
    sys_os_mutex_enter(m_pMutex);
    m_pMetadataCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

#endif // METADATA

#ifdef BACKCHANNEL

int CRtspClient::get_bc_flag()
{
    return m_rua.backchannel;
}

void CRtspClient::set_bc_flag(int flag)
{
    m_rua.backchannel = flag;
}

int CRtspClient::get_bc_data_flag()
{
    if (m_rua.backchannel)
    {
        return m_rua.send_bc_data;
    }

    return -1; // unsupport backchannel
}

void CRtspClient::set_bc_data_flag(int flag)
{
    m_rua.send_bc_data = flag;
}

void CRtspClient::set_audio_device(int index)
{
    m_rua.bc_audio_device = index;
}

BOOL CRtspClient::rtsp_init_backchannel(RCUA * p_rua)
{
    int count;
    
#if __WINDOWS_OS__
    count = CWAudioCapture::getDeviceNums();
    if (m_rua.bc_audio_device >= count)
    {
        return FALSE;
    }
    
    p_rua->bc_audio_captrue = CWAudioCapture::getInstance(m_rua.bc_audio_device);
#elif defined(ANDROID)
    count = CAAudioCapture::getDeviceNums();
    if (m_rua.bc_audio_device >= count)
    {
        return FALSE;
    }
    
    p_rua->bc_audio_captrue = CAAudioCapture::getInstance(m_rua.bc_audio_device);
#elif defined(IOS)
    count = CMAudioCapture::getDeviceNums();
    if (m_rua.bc_audio_device >= count)
    {
        return FALSE;
    }
    
    p_rua->bc_audio_captrue = CMAudioCapture::getInstance(m_rua.bc_audio_device);
#elif __LINUX_OS__
    count = CLAudioCapture::getDeviceNums();
    if (m_rua.bc_audio_device >= count)
    {
        return FALSE;
    }
    
    p_rua->bc_audio_captrue = CLAudioCapture::getInstance(m_rua.bc_audio_device);
#endif

    if (NULL == p_rua->bc_audio_captrue)
    {
        return FALSE;
    }
    
    p_rua->bc_audio_captrue->addCallback(rtsp_bc_cb, p_rua);
    p_rua->bc_audio_captrue->initCapture(m_rua.bc_audio_info.codec, m_rua.bc_audio_info.samplerate, 
                                         m_rua.bc_audio_info.channels, m_rua.bc_audio_info.bitpersample * 8);
    
    return p_rua->bc_audio_captrue->startCapture();
}

#endif // BACKCHANNEL

#ifdef REPLAY

int CRtspClient::get_replay_flag()
{
    return m_rua.replay;
}

void CRtspClient::set_replay_flag(int flag)
{
    m_rua.replay = flag;
}

void CRtspClient::set_scale(double scale)
{
    if (scale != 0)
    {
        m_rua.scale_flag = 1;
        m_rua.scale = (int)(scale * 100);
    }
    else
    {
        m_rua.scale_flag = 0;
        m_rua.scale = 0;
    }
}

void CRtspClient::set_rate_control_flag(int flag)
{
    m_rua.rate_control_flag = 1;
    m_rua.rate_control = flag;
}

void CRtspClient::set_immediate_flag(int flag)
{
    m_rua.immediate_flag = 1;
    m_rua.immediate = flag;
}

void CRtspClient::set_frames_flag(int flag, int interval)
{
    if (flag == 1 || flag == 2)
    {
        m_rua.frame_flag = 1;
        m_rua.frame = flag;
    }
    else
    {
        m_rua.frame_flag = 0;
        m_rua.frame = 0;
    }

    if (interval > 0)
    {
        m_rua.frame_interval_flag = 1;
        m_rua.frame_interval = interval;
    }
    else
    {
        m_rua.frame_interval_flag = 0;
        m_rua.frame_interval = 0;
    }
}

void CRtspClient::set_replay_range(time_t start, time_t end)
{
    m_rua.range_flag = 1;
    m_rua.replay_start = start;
    m_rua.replay_end = end;
}

#endif // REPLAY

#ifdef OVER_HTTP

void CRtspClient::set_rtsp_over_http(int flag, int port)
{
    if (flag)
    {
        m_rua.mcast_flag = 0;
        m_rua.rtp_tcp = 1;
    }
    
    m_rua.over_http = flag;

    if (port > 0 && port <= 65535)
    {
        m_rua.http_port = port;
    }
}

int CRtspClient::rtsp_build_http_get_req(void * p_user, char * bufs, int buflen, char * cookie)
{
    int offset = 0;
    HTTPREQ * p_req = (HTTPREQ *) p_user;
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "User-Agent: Happytimesoft rtsp client\r\n"
                "x-sessioncookie: %s\r\n"
                "Accept: application/x-rtsp-tunnelled\r\n"
                "Pragma: no-cache\r\n"
                "Cache-Control: no-cache\r\n", 
                p_req->url,
                p_req->host, p_req->port,
                cookie);

    if (p_req->need_auth)
    {
        offset += http_build_auth_msg(p_req, "GET", bufs+offset, buflen-offset);        
    }
    
    offset += snprintf(bufs+offset, buflen-offset, "\r\n");

    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", bufs);

    return offset;
}

int CRtspClient::rtsp_build_http_post_req(void * p_user, char * bufs, int buflen, char * cookie)
{
    int offset = 0;
    HTTPREQ * p_req = (HTTPREQ *) p_user;
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "POST %s HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "User-Agent: Happytimesoft rtsp client\r\n"
                "x-sessioncookie: %s\r\n"
                "Content-Type: application/x-rtsp-tunnelled\r\n"
                "Pragma: no-cache\r\n"
                "Cache-Control: no-cache\r\n"
                "Content-Length: 32767\r\n", 
                p_req->url,
                p_req->host, p_req->port,
                cookie);

    if (p_req->need_auth)
    {
        offset += http_build_auth_msg(p_req, "POST", bufs+offset, buflen-offset);        
    }
    
    offset += snprintf(bufs+offset, buflen-offset, "\r\n");

    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", bufs);

    return offset;
}

BOOL CRtspClient::rtsp_over_http_start()
{
    int len;
    int offset = 0;
    int timeout = m_nConnTimeout*1000;
    char buff[2048];
    char cookie[100];
    HTTPREQ * p_http;
    static int cnt = 1;
    HRTSP_MSG * tx_msg;

    srand((uint32)time(NULL)+cnt++);
    snprintf(cookie, sizeof(cookie), "%x%x%x", rand(), rand(), sys_os_get_ms());

RETRY:

    p_http = &m_rua.rtsp_recv;

    if (m_nport != 554)
    {
        p_http->port = m_nport; // Port from the stream address
    }
    else
    {
        p_http->port = m_rua.http_port; // Configured RTSP OVER HTTP port
    }
    
    strcpy(p_http->host, m_ip);
    strcpy(p_http->url, m_suffix);

    if (memcmp(m_url, "https://", 8) == 0)
    {
        p_http->https = 1;
    }

    // First try the stream address port, 
    // If the connection fails, try to connect to the configured port

    HT_SOCKADDR sockaddr;
    
    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid host: %s\r\n", __FUNCTION__, p_http->host);
        return FALSE;
    }

    if (sockaddr.ipv4_flag)
    {
        sockaddr.ipv4_addr.sin_port = htons(p_http->port);

        p_http->family = AF_INET;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        sockaddr.ipv6_addr.sin6_port = htons(p_http->port);

        p_http->family = AF_INET6;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
    
    if (p_http->cfd <= 0)
    {
        if (p_http->port == m_rua.http_port)
        {
            log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
                __FUNCTION__, p_http->host, p_http->port);
            goto FAILED;
        }

        if (sockaddr.ipv4_flag)
        {
            sockaddr.ipv4_addr.sin_port = htons(m_rua.http_port);
            p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
        }
        else if (sockaddr.ipv6_flag)
        {
            sockaddr.ipv6_addr.sin6_port = htons(m_rua.http_port);
            p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
        }
    
        if (p_http->cfd <= 0)
        {
            log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
                __FUNCTION__, p_http->host, p_http->port);
            goto FAILED;
        }
    }

    len = 2 * 1024 * 1024;
        
    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\n", __FUNCTION__);
    }

    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\n", __FUNCTION__);
    }

    if (p_http->https)
    {
#ifdef HTTPS    
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            goto FAILED;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif
    }

    offset = rtsp_build_http_get_req(p_http, buff, sizeof(buff), cookie);
    
    if (http_cln_tx(p_http, buff, offset) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, http_cln_tx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (!http_cln_rx_timeout(p_http, timeout))
    {
        log_print(HT_LOG_ERR, "%s, http_cln_rx_timeout failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (p_http->rx_msg->msg_sub_type == 401)
    {
        if (p_http->need_auth == FALSE)
        {
            http_cln_auth_set(p_http, m_rua.auth_info.auth_name, m_rua.auth_info.auth_pwd);

            http_cln_free_req(p_http);

            goto RETRY;
        }

        send_notify(RTSP_EVE_AUTHFAILED);
        goto FAILED;
    }
    else if (p_http->rx_msg->msg_sub_type != 200 || p_http->rx_msg->ctt_type != CTT_RTSP_TUNNELLED)
    {
        log_print(HT_LOG_ERR, "%s, msg_sub_type=%d, ctt_type=%d\r\n", 
            __FUNCTION__, p_http->rx_msg->msg_sub_type, p_http->rx_msg->ctt_type);
        goto FAILED;
    }

    p_http = &m_rua.rtsp_send;

    p_http->https = m_rua.rtsp_recv.https;
    p_http->port =  m_rua.rtsp_recv.port;
    strcpy(p_http->host, m_ip);
    strcpy(p_http->url, m_suffix);

    if (sockaddr.ipv4_flag)
    {
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
        
    if (p_http->cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
            __FUNCTION__, p_http->host, p_http->port);
        goto FAILED;
    }

    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\n", __FUNCTION__);
    }

    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\n", __FUNCTION__);
    }
    
#ifdef HTTPS
    if (p_http->https)
    {
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            goto FAILED;
        }
    }
#endif

    offset = rtsp_build_http_post_req(p_http, buff, sizeof(buff), cookie);
    
    if (http_cln_tx(p_http, buff, offset) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, http_cln_tx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    m_rua.cseq = 1;
    m_rua.rtp_tcp = 1;
    m_rua.state = RCS_OPTIONS;
    
    tx_msg = rcua_build_options(&m_rua);
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);
    }
    
    return TRUE;

FAILED:

    http_cln_free_req(&m_rua.rtsp_recv);
    http_cln_free_req(&m_rua.rtsp_send);

    return FALSE;
}

int CRtspClient::rtsp_over_http_rx(RCUA * p_rua)
{
    int rlen;
    BOOL selflag = TRUE;
    SOCKET fd = -1;

    fd = p_rua->rtsp_recv.cfd;
    if (fd <= 0)
    {
        return RTSP_RX_FAIL;
    }

#ifdef HTTPS
    if (p_rua->rtsp_recv.https)
    {
        sys_os_mutex_enter(p_rua->rtsp_recv.ssl_mutex);
        if (p_rua->rtsp_recv.ssl)
        {
            if (SSL_pending((SSL *)p_rua->rtsp_recv.ssl) > 0)
            {
                selflag = FALSE; // There is data to read 
            }
        }
        sys_os_mutex_leave(p_rua->rtsp_recv.ssl_mutex);
    }
#endif

    if (selflag)
    {
#ifndef EPOLL    
        fd_set fdread;
        struct timeval tv = {1, 0};

        FD_ZERO(&fdread);
        FD_SET(fd, &fdread);
    
        int sret = select((int)(fd+1), &fdread, NULL, NULL, &tv); 
        if (sret == 0) // Time expired 
        { 
            return RTSP_RX_TIMEOUT; 
        }
        else if (!FD_ISSET(fd, &fdread))
        {
            return RTSP_RX_TIMEOUT;
        }
#endif        
    }

    if (p_rua->rtp_rcv_buf == NULL || p_rua->rtp_t_len == 0)
    {
#ifdef HTTPS
        if (p_rua->rtsp_recv.https)
        {
            sys_os_mutex_enter(p_rua->rtsp_recv.ssl_mutex);
            if (p_rua->rtsp_recv.ssl)
            {
                rlen = SSL_read((SSL *)p_rua->rtsp_recv.ssl, p_rua->rcv_buf+p_rua->rcv_dlen, 
                            sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->rtsp_recv.ssl_mutex);
        }
        else
#endif
        {
            rlen = recv(fd, p_rua->rcv_buf+p_rua->rcv_dlen, sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen, 0);
        }
        
        if (rlen <= 0)
        {
            // recv error, connection maybe disconn
            
            log_print(HT_LOG_WARN, "%s, ret = %d, err = %s\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());    
            return RTSP_RX_FAIL;
        }

        p_rua->rcv_dlen += rlen;

        if (p_rua->rcv_dlen < 4)
        {
            return RTSP_RX_SUCC;
        }

        p_rua->rcv_buf[p_rua->rcv_dlen] = '\0';
    }
    else
    {
#ifdef HTTPS
        if (p_rua->rtsp_recv.https)
        {
            sys_os_mutex_enter(p_rua->rtsp_recv.ssl_mutex);
            if (p_rua->rtsp_recv.ssl)
            {
                rlen = SSL_read((SSL*)p_rua->rtsp_recv.ssl, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, 
                            p_rua->rtp_t_len-p_rua->rtp_rcv_len);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->rtsp_recv.ssl_mutex);
        }
        else
#endif
        {
            rlen = recv(fd, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, p_rua->rtp_t_len-p_rua->rtp_rcv_len, 0);
        }
        
        if (rlen <= 0)
        {
            log_print(HT_LOG_WARN, "%s, ret = %d, err = %s\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());    // recv error, connection maybe disconn?
            return RTSP_RX_FAIL;
        }

        p_rua->rtp_rcv_len += rlen;
        
        if (p_rua->rtp_rcv_len == p_rua->rtp_t_len)
        {
            tcp_data_rx((uint8*)p_rua->rtp_rcv_buf, p_rua->rtp_rcv_len);
            
            free(p_rua->rtp_rcv_buf);
            p_rua->rtp_rcv_buf = NULL;
            p_rua->rtp_rcv_len = 0;
            p_rua->rtp_t_len = 0;
        }
        
        return RTSP_RX_SUCC;
    }

    return rtsp_tcp_data_rx(p_rua);
}

#endif // OVER_HTTP

#ifdef OVER_WEBSOCKET

void CRtspClient::set_rtsp_over_ws(int flag, int port)
{
    if (flag)
    {
        m_rua.mcast_flag = 0;
        m_rua.rtp_tcp = 1;
    }
    
    m_rua.over_ws = flag;

    if (port > 0 && port <= 65535)
    {
        m_rua.ws_port = port;
    }
}

int CRtspClient::rtsp_build_ws_req(void * p_user, char * bufs, int buflen)
{
    int offset = 0;
    uint8 key[20];
    char  key_base64[32] = {'\0'};
    HTTPREQ * p_req = (HTTPREQ *) p_user;

    snprintf((char *)key, sizeof(key), "%08d%08x", rand(), sys_os_get_ms());

    base64_encode(key, 16, key_base64, sizeof(key_base64)-1);
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: %s\r\n"
                "Sec-WebSocket-Protocol: rtsp.onvif.org\r\n"
                "Sec-WebSocket-Version: %d\r\n", 
                p_req->url,
                p_req->host, p_req->port,
                key_base64,
                WS_VERSION);

    if (p_req->need_auth)
    {
        offset += http_build_auth_msg(p_req, "GET", bufs+offset, buflen-offset);        
    }
    
    offset += snprintf(bufs+offset, buflen-offset, "\r\n");

    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", bufs);

    return offset;
}

BOOL CRtspClient::rtsp_over_ws_start()
{
    int len;
    int offset = 0;
    int timeout = m_nConnTimeout*1000;
    char buff[2048];
    HTTPREQ * p_http;
    HRTSP_MSG * tx_msg;

RETRY:

    p_http = &m_rua.ws_http;

    if (m_nport != 554)
    {
        p_http->port = m_nport; // Port from the stream address
    }
    else
    {
        p_http->port = m_rua.ws_port; // Configured RTSP OVER websocket port
    }
    
    strcpy(p_http->host, m_ip);
    strcpy(p_http->url, m_suffix);

    if (memcmp(m_url, "wss://", 6) == 0)
    {
        p_http->https = 1;
    }

    HT_SOCKADDR sockaddr;
    
    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid host: %s\r\n", __FUNCTION__, p_http->host);
        return FALSE;
    }

    if (sockaddr.ipv4_flag)
    {
        sockaddr.ipv4_addr.sin_port = htons(p_http->port);

        p_http->family = AF_INET;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        sockaddr.ipv6_addr.sin6_port = htons(p_http->port);

        p_http->family = AF_INET6;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
    
    if (p_http->cfd <= 0)
    {
        if (p_http->port == m_rua.ws_port)
        {
            log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
                __FUNCTION__, p_http->host, p_http->port);
            goto FAILED;
        }

        if (sockaddr.ipv4_flag)
        {
            sockaddr.ipv4_addr.sin_port = htons(m_rua.ws_port);
            p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
        }
        else if (sockaddr.ipv6_flag)
        {
            sockaddr.ipv6_addr.sin6_port = htons(m_rua.ws_port);
            p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
        }
        
        if (p_http->cfd <= 0)
        {
            log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
                __FUNCTION__, p_http->host, p_http->port);
            goto FAILED;
        }
    }

    len = 2 * 1024 * 1024;
        
    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\n", __FUNCTION__);
    }

    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\n", __FUNCTION__);
    }

    if (p_http->https)
    {
#ifdef HTTPS    
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            goto FAILED;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif
    }
    
    offset = rtsp_build_ws_req(p_http, buff, sizeof(buff));
    
    if (http_cln_tx(p_http, buff, offset) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, http_cln_tx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (!http_cln_rx_timeout(p_http, timeout))
    {
        log_print(HT_LOG_ERR, "%s, http_cln_rx_timeout failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (p_http->rx_msg->msg_sub_type == 401)
    {
        if (p_http->need_auth == FALSE)
        {
            http_cln_auth_set(p_http, m_rua.auth_info.auth_name, m_rua.auth_info.auth_pwd);

            http_cln_free_req(p_http);

            goto RETRY;
        }

        send_notify(RTSP_EVE_AUTHFAILED);
        goto FAILED;
    }
    else if (p_http->rx_msg->msg_sub_type != 101)
    {
        log_print(HT_LOG_ERR, "%s, msg_sub_type=%d, ctt_type=%d\r\n", 
            __FUNCTION__, p_http->rx_msg->msg_sub_type, p_http->rx_msg->ctt_type);
        goto FAILED;
    }

    m_rua.cseq = 1;
    m_rua.rtp_tcp = 1;
    m_rua.state = RCS_OPTIONS;

    m_rua.ws_msg.buff = (char *)malloc(WS_MAXMESSAGE);
    if (NULL == m_rua.ws_msg.buff)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed!\r\n", __FUNCTION__);
        goto FAILED;
    }
    
    m_rua.ws_msg.buff_len = WS_MAXMESSAGE;
    
    tx_msg = rcua_build_options(&m_rua);
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);
    }
    
    return TRUE;

FAILED:
    
    http_cln_free_req(&m_rua.ws_http);

    return FALSE;
}

BOOL CRtspClient::rtsp_ws_send_msg(RCUA * p_rua, int opcode, uint8 have_mask, char * buff, int len)
{
    int slen;
    int offset = len;
    uint8 * p_buff = (uint8 *)buff;
    
    int extra = ws_encode_data(p_buff, offset, opcode, have_mask);
    
    offset += extra;
    p_buff -= extra;

    slen = http_cln_tx(&p_rua->ws_http, (char *)p_buff, offset);
    if (slen != offset)
    {
        log_print(HT_LOG_ERR, "%s, message send failed. slen=%d\r\n", __FUNCTION__, slen);
        return FALSE;
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_ws_ping_process(RCUA * p_rua, char * buff, int len)
{
    BOOL ret;
    char * p_buff = (char *)malloc(len + 32);
    if (NULL == p_buff)
    {
        return FALSE;
    }

    memcpy(p_buff+32, buff, len);

    ret = rtsp_ws_send_msg(p_rua, WS_OPCODE_PONG, 1, p_buff+32, len);

    free(p_buff);

    return ret;
}

BOOL CRtspClient::rtsp_ws_rtsp_process(RCUA * p_rua, char * buff, int len)
{
    if (NULL == p_rua->rtp_rcv_buf || 0 == p_rua->rtp_t_len)
    {
        if (len > (int) sizeof(p_rua->rcv_buf) - 4 - p_rua->rcv_dlen)
        {
            log_print(HT_LOG_ERR, "%s, bufflen=%d, tlen=%d, rcv_len=%d\r\n", 
                __FUNCTION__, len, sizeof(p_rua->rcv_buf) - 4, p_rua->rcv_dlen);
            return FALSE;
        }
        
        memcpy(p_rua->rcv_buf + p_rua->rcv_dlen, buff, len);
        p_rua->rcv_dlen += len;

        if (p_rua->rcv_dlen < 4)
        {
            return TRUE;
        }

        p_rua->rcv_buf[p_rua->rcv_dlen] = '\0';
    }
    else
    {
        if (len > p_rua->rtp_t_len - p_rua->rtp_rcv_len)
        {
            log_print(HT_LOG_ERR, "%s, bufflen=%d, rtp_t_len=%d, rtp_rcv_len=%d\r\n", 
                __FUNCTION__, len, p_rua->rtp_t_len, p_rua->rtp_rcv_len);
            return FALSE;
        }

        memcpy(p_rua->rtp_rcv_buf + p_rua->rtp_rcv_len, buff, len);
        p_rua->rtp_rcv_len += len;

        if (p_rua->rtp_rcv_len == p_rua->rtp_t_len)
        {
            tcp_data_rx((uint8*)p_rua->rtp_rcv_buf, p_rua->rtp_rcv_len);
            
            free(p_rua->rtp_rcv_buf);
            p_rua->rtp_rcv_buf = NULL;
            p_rua->rtp_rcv_len = 0;
            p_rua->rtp_t_len = 0;
        }
        
        return TRUE;
    }

    return rtsp_tcp_data_rx(p_rua);
}

BOOL CRtspClient::rtsp_ws_data_process(RCUA * p_rua)
{
    WSMSG * p_msg = &p_rua->ws_msg;

PARSE:

    int ret = ws_decode_data(p_msg);
    if (ret > 0)
    {
        if (p_msg->opcode == WS_OPCODE_TEXT || p_msg->opcode == WS_OPCODE_BINARY)
        {
            rtsp_ws_rtsp_process(p_rua, p_msg->buff+p_msg->skip, p_msg->msg_len);
        }
        else if (p_msg->opcode == WS_OPCODE_CLOSE)
        {
            log_print(HT_LOG_INFO, "%s, recv close message\r\n", __FUNCTION__);
            return FALSE;
        }
        else if (p_msg->opcode == WS_OPCODE_PING)
        {
            rtsp_ws_ping_process(p_rua, p_msg->buff+p_msg->skip, p_msg->msg_len);
        }
        else if (p_msg->opcode == WS_OPCODE_PONG)
        {
            log_print(HT_LOG_INFO, "%s, recv pong message\r\n", __FUNCTION__);
        }
        else
        {
            log_print(HT_LOG_INFO, "%s, recv unkonw message %d\r\n", __FUNCTION__, p_msg->opcode);
            return FALSE;
        }
        
        p_msg->rcv_len -= p_msg->msg_len + p_msg->skip;

        if (p_msg->rcv_len > 0)
        {
            memmove(p_msg->buff, p_msg->buff + p_msg->msg_len + p_msg->skip, p_msg->rcv_len);
        }
        
        if (p_msg->rcv_len >= 6)
        {
            goto PARSE;
        }

        return TRUE;
    }
    else if (ret == 0) // need more data to parse
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

int CRtspClient::rtsp_over_ws_rx(RCUA * p_rua)
{
    int rlen;
    BOOL selflag = TRUE;
    WSMSG * p_msg = &p_rua->ws_msg;
    SOCKET fd = -1;

    fd = p_rua->ws_http.cfd;
    if (fd <= 0)
    {
        return RTSP_RX_FAIL;
    }

#ifdef HTTPS
    if (p_rua->ws_http.https)
    {
        sys_os_mutex_enter(p_rua->ws_http.ssl_mutex);
        if (p_rua->ws_http.ssl)
        {
            if (SSL_pending((SSL*)p_rua->ws_http.ssl) > 0)
            {
                selflag = FALSE; // There is data to read 
            }
        }
        sys_os_mutex_leave(p_rua->ws_http.ssl_mutex);
    }
#endif

    if (selflag)
    {
#ifndef EPOLL    
        fd_set fdread;
        struct timeval tv = {1, 0};

        FD_ZERO(&fdread);
        FD_SET(fd, &fdread);
    
        int sret = select((int)(fd+1), &fdread, NULL, NULL, &tv); 
        if (sret == 0) // Time expired 
        { 
            return RTSP_RX_TIMEOUT; 
        }
        else if (!FD_ISSET(fd, &fdread))
        {
            return RTSP_RX_TIMEOUT;
        }
#endif        
    }

#ifdef HTTPS
    if (p_rua->ws_http.https)
    {
        sys_os_mutex_enter(p_rua->ws_http.ssl_mutex);
        if (p_rua->ws_http.ssl)
        {
            rlen = SSL_read((SSL*)p_rua->ws_http.ssl, p_msg->buff+p_msg->rcv_len, p_msg->buff_len-p_msg->rcv_len);
        }
        else
        {
            rlen = -1;
        }
        sys_os_mutex_leave(p_rua->ws_http.ssl_mutex);
    }
    else
#endif
    {
        rlen = recv(fd, p_msg->buff+p_msg->rcv_len, p_msg->buff_len-p_msg->rcv_len, 0);
    }
    
    if (rlen <= 0)
    {
        // recv error, connection maybe disconnct
        
        log_print(HT_LOG_WARN, "%s, ret = %d, err = %s\r\n", 
            __FUNCTION__, rlen, sys_os_get_socket_error());    
        return RTSP_RX_FAIL;
    }

    p_msg->rcv_len += rlen;

    if (p_msg->rcv_len < 4)
    {
        return RTSP_RX_SUCC;
    }

    return rtsp_ws_data_process(p_rua) ? RTSP_RX_SUCC : RTSP_RX_FAIL;
}

#endif // OVER_WEBSOCKET

#ifdef SRTP

BOOL CRtspClient::rtsp_get_crypto_info(int ch)
{
    if (ch < 0 || ch >= AV_MAX_CHS)
    {
        return FALSE;
    }
    
    int i;
    RCMCH * p_channel = &m_rua.channels[ch];

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_channel->cap_desc[i];
        if (memcmp(ptr, "a=crypto:", 9) == 0)
        {
            int next = 0;
            char buff[100];
            
            ptr += 9;
    
            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;
            
            if (!get_line_word(ptr, 0, (int)strlen(ptr), p_channel->crypto_suite, 
                             sizeof(p_channel->crypto_suite), &next, WORD_TYPE_STRING))
            {
                return FALSE;
            }
            
            ptr += next;

            while (*ptr == ' ') ptr++;

            if (memcmp(ptr, "inline:", 7) != 0)
            {
                return FALSE;
            }

            ptr += 7;

            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff),  &next, WORD_TYPE_STRING);

            if (base64_decode(buff, strlen(buff), p_channel->crypto_key, sizeof(p_channel->crypto_key)) <= 0)
            {
                return FALSE;
            }

            p_channel->srtp_enable = 1;

            break;
        }
    }

    return p_channel->srtp_enable ? TRUE : FALSE;
}

BOOL CRtspClient::rtsp_setup_srtp(int ch)
{
    if (ch < 0 || ch >= AV_MAX_CHS)
    {
        return FALSE;
    }

    RCMCH * p_channel = &m_rua.channels[ch];

    memset(&p_channel->srtp_policy, 0, sizeof(p_channel->srtp_policy));

    srtp_crypto_policy_set_rtp_default(&p_channel->srtp_policy.rtp); 
    srtp_crypto_policy_set_rtcp_default(&p_channel->srtp_policy.rtcp);
        
    if (strcasecmp(p_channel->crypto_suite, "AES_CM_128_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES128_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_128_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES128_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_256_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES256_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_256_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES256_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_192_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES192_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_192_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES192_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else
    {
        log_print(HT_LOG_ERR, "unsupport crypto suite : %s\r\n", p_channel->crypto_suite);
        return FALSE;
    }
    
    if (ch == AV_BACK_CH)
    {
        p_channel->srtp_policy.ssrc.type = ssrc_any_outbound;
    }
    else
    {
        p_channel->srtp_policy.ssrc.type = ssrc_any_inbound;
    }
    
    p_channel->srtp_policy.key = p_channel->crypto_key;
    p_channel->srtp_policy.next = NULL;

    if (srtp_create(&p_channel->srtp, &p_channel->srtp_policy) != srtp_err_status_ok) 
    {
        log_print(HT_LOG_ERR, "Error creating inbound SRTP session\r\n");
        return FALSE;
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_setup_srtp(RCUA * p_rua)
{
    int i, j;
    
    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].ctl[0] == '\0')
        {
            continue;
        }
        
        memset(&p_rua->channels[i].srtp_policy, 0, sizeof(p_rua->channels[i].srtp_policy));

        srtp_crypto_policy_set_rtp_default(&p_rua->channels[i].srtp_policy.rtp); 
        srtp_crypto_policy_set_rtcp_default(&p_rua->channels[i].srtp_policy.rtcp);

        // SRTP_KEY_LENGTH (16) + SRTP_SALT_LEN (14)
        for (j = 0; j < 30; j++)
        {
            p_rua->channels[i].crypto_key[j] = (uint8) rand();
        }

        p_rua->channels[i].srtp_policy.ssrc.type = ssrc_any_outbound;
        
        p_rua->channels[i].srtp_policy.key = p_rua->channels[i].crypto_key;
        p_rua->channels[i].srtp_policy.next = NULL;

        if (srtp_create(&p_rua->channels[i].srtp, &p_rua->channels[i].srtp_policy) != srtp_err_status_ok) 
        {
            log_print(HT_LOG_ERR, "Error creating SRTP session\r\n");
        }
    }

    return TRUE;
}

BOOL CRtspClient::srtp_tcp_data_rx(int interleaved, uint8 * p_rtp, int * rtp_len)
{
    for (int i = 0; i < AV_MAX_CHS; i++)
    {
        if (!m_rua.channels[i].setup)
        {
            continue;
        }
        
        if (interleaved == m_rua.channels[i].interleaved)
        {
            if (m_rua.channels[i].srtp_enable)
            {
                if (m_rua.channels[i].srtp)
                {
                    if (srtp_unprotect(m_rua.channels[i].srtp, p_rtp, rtp_len) != srtp_err_status_ok)
                    {
                        return FALSE;
                    }
                }
                else
                {
                    return FALSE;
                }
            }

            break;
        }
        else if (interleaved == m_rua.channels[i].interleaved+1)
        {
            if (m_rua.channels[i].srtp_enable)
            {
                if (m_rua.channels[i].srtp)
                {
                    if (srtp_unprotect_rtcp(m_rua.channels[i].srtp, p_rtp, rtp_len) != srtp_err_status_ok)
                    {
                        return FALSE;
                    }
                }
                else
                {
                    return FALSE;
                }
            }

            break;
        }
    }

    return TRUE;
}

BOOL CRtspClient::srtp_udp_data_rx(int channel, int is_rtcp, uint8 * p_rtp, int * rtp_len)
{
    if (m_rua.channels[channel].srtp_enable)
    {
        if (m_rua.channels[channel].srtp)
        {
            if (is_rtcp)
            {
                if (srtp_unprotect_rtcp(m_rua.channels[channel].srtp, p_rtp, rtp_len) != srtp_err_status_ok)
                {
                    return FALSE;
                }
            }
            else
            {
                if (srtp_unprotect(m_rua.channels[channel].srtp, p_rtp, rtp_len) != srtp_err_status_ok)
                {
                    return FALSE;
                }
            }
        }
        else
        {
            return FALSE;
        }
    }

    return TRUE;
}

#endif // SRTP

#ifdef RTSPS

void CRtspClient::set_rtsp_over_ssl(int flag)
{
    if (flag)
    {
        m_rua.mcast_flag = 0;
        m_rua.rtp_tcp = 1;
    }
    
    m_rua.rtsps = flag;
}

BOOL CRtspClient::rtsp_ssl_conn(RCUA * p_rua, int timeout)
{
    SSL_CTX * ctx = NULL;
    const SSL_METHOD * method = NULL;

    SSL_library_init();  
    SSL_load_error_strings();
    
    method = TLS_client_method();  
    ctx = SSL_CTX_new(method);
    if (NULL == ctx)
    {
        log_print(HT_LOG_ERR, "%s, SSL_CTX_new failed!\r\n", __FUNCTION__);
        return FALSE;
    }
    else
    {
        SSL_CTX_set_options(ctx, SSL_OP_ALL);
        SSL_CTX_set_default_verify_paths(ctx);
    }
    
#if __WINDOWS_OS__
    int tv = timeout;
#else
    struct timeval tv = {timeout / 1000, (timeout % 1000) * 1000};
#endif

    setsockopt(p_rua->fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    
    p_rua->ssl = SSL_new(ctx); 
    if (NULL == p_rua->ssl)
    {
        SSL_CTX_free(ctx);
        log_print(HT_LOG_ERR, "%s, SSL_new failed!\r\n", __FUNCTION__);
        return FALSE;
    }
    
    SSL_set_fd((SSL *)p_rua->ssl, p_rua->fd); 

    if (SSL_connect((SSL*)p_rua->ssl) == -1)
    {
        SSL_CTX_free(ctx);
        log_print(HT_LOG_ERR, "%s, SSL_connect failed!\r\n", __FUNCTION__);
        return FALSE;
    }

    SSL_CTX_free(ctx);

    p_rua->ssl_mutex = sys_os_create_mutex();
    
    return TRUE;
}

#endif // RTSPS

BOOL CRtspClient::rtsp_client_state(RCUA * p_rua, HRTSP_MSG * rx_msg)
{
    BOOL ret = TRUE;

    if (rx_msg->msg_type == 0)  // Request message?
    {
        return FALSE;
    }

    if (rx_msg->msg_sub_type == 401)
    {
        return rtsp_unauth_res(p_rua, rx_msg);
    }
    else
    {
        p_rua->auth_retry = 0;
    }
    
    switch (p_rua->state)
    {
    case RCS_NULL:
        break;

    case RCS_OPTIONS:
        ret = rtsp_options_res(p_rua, rx_msg);
        break;
        
    case RCS_DESCRIBE:
        ret = rtsp_describe_res(p_rua, rx_msg);
        break;

    case RCS_INIT_V:
        ret = rtsp_setup_res(p_rua, rx_msg, AV_VIDEO_CH);
        break;

    case RCS_INIT_A:
        ret = rtsp_setup_res(p_rua, rx_msg, AV_AUDIO_CH);
        break;

    case RCS_INIT_M:
        ret = rtsp_setup_res(p_rua, rx_msg, AV_METADATA_CH);
        break;

    case RCS_INIT_BC:
        ret = rtsp_setup_res(p_rua, rx_msg, AV_BACK_CH);
        break;

    case RCS_READY:
        ret = rtsp_play_res(p_rua, rx_msg);
        break;

    case RCS_PLAYING:
        rtsp_play_res(p_rua, rx_msg);
        break;

    case RCS_RECORDING:
        break;

    default:
        break;
    }        

    return ret;
}

BOOL CRtspClient::rtsp_prepare_play()
{    
    if (m_rua.channels[AV_VIDEO_CH].ctl[0] != '\0')
    {
        if (m_rua.video_codec == VIDEO_CODEC_H264)
        {
            h264_rxi_init(&h264rxi, video_data_cb, this);
        }
        else if (m_rua.video_codec == VIDEO_CODEC_H265)
        {
            h265_rxi_init(&h265rxi, video_data_cb, this);
        }
        else if (m_rua.video_codec == VIDEO_CODEC_JPEG)
        {
            mjpeg_rxi_init(&mjpegrxi, video_data_cb, this);

            mjpegrxi.width = m_nWidth;
            mjpegrxi.height = m_nHeight;
        }
        else if (m_rua.video_codec == VIDEO_CODEC_MP4)
        {
            mpeg4_rxi_init(&mpeg4rxi, video_data_cb, this);
        }
    }
    
    if (m_rua.channels[AV_AUDIO_CH].ctl[0] != '\0')
    {
        if (m_rua.audio_info.codec == AUDIO_CODEC_AAC)
        {
            aac_rxi_init(&aacrxi, audio_data_cb, this);
        }
        else if (m_rua.audio_info.codec != AUDIO_CODEC_NONE )
        {
            pcm_rxi_init(&pcmrxi, audio_data_cb, this);
        }
    }

#ifdef METADATA
    if (m_rua.channels[AV_METADATA_CH].ctl[0] != '\0')
    {
        pcm_rxi_init(&metadatarxi, metadata_data_cb, this);
    }
#endif

#ifndef EPOLL
    if (!m_rua.rtp_tcp)
    {
        m_udpRxTid = sys_os_create_thread((void *)rtsp_udp_rx_thread, this);
    }
#endif

    return TRUE;
}

void CRtspClient::tcp_data_rx(uint8 * data, int rlen)
{
    uint8 interleaved;
    uint8 * p_rtp = data + 4;
    int rtp_len = rlen - 4;

    interleaved = net_read_uint8(data+1);

#ifdef SRTP
    if (!srtp_tcp_data_rx(interleaved, p_rtp, &rtp_len))
    {
        return;
    }
#endif

    if (interleaved == m_rua.channels[AV_VIDEO_CH].interleaved)
    {
        if (VIDEO_CODEC_H264 == m_rua.video_codec)
        {
            h264_rtp_rx(&h264rxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_JPEG == m_rua.video_codec)
        {
            mjpeg_rtp_rx(&mjpegrxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_MP4 == m_rua.video_codec)
        {
            mpeg4_rtp_rx(&mpeg4rxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_H265 == m_rua.video_codec)
        {
            h265_rtp_rx(&h265rxi, p_rtp, rtp_len);
        }
    }
    else if (interleaved == m_rua.channels[AV_AUDIO_CH].interleaved)
    {
        if (AUDIO_CODEC_AAC == m_rua.audio_info.codec)
        {
            aac_rtp_rx(&aacrxi, p_rtp, rtp_len);
        }
        else if (AUDIO_CODEC_NONE != m_rua.audio_info.codec)
        {
            pcm_rtp_rx(&pcmrxi, p_rtp, rtp_len);
        }
    }
#ifdef METADATA    
    else if (interleaved == m_rua.channels[AV_METADATA_CH].interleaved)
    {
        pcm_rtp_rx(&metadatarxi, p_rtp, rtp_len);
    }
#endif
    else if (interleaved == m_rua.channels[AV_VIDEO_CH].interleaved+1)
    {
        rtcp_data_rx(p_rtp, rtp_len, AV_VIDEO_CH);
    }
    else if (interleaved == m_rua.channels[AV_AUDIO_CH].interleaved+1)
    {
        rtcp_data_rx(p_rtp, rtp_len, AV_AUDIO_CH);
    }
#ifdef METADATA        
    else if (interleaved == m_rua.channels[AV_METADATA_CH].interleaved+1)
    {
        rtcp_data_rx(p_rtp, rtp_len, AV_METADATA_CH);
    }
#endif
}

void CRtspClient::udp_data_rx(uint8 * data, int rlen, int type)
{
    uint8 * p_rtp = data;
    int rtp_len = rlen;

    if (rtp_len >= 2 && RTP_PT_IS_RTCP(p_rtp[1]))
    {
        rtcp_data_rx(p_rtp, rtp_len, type);
    }
    else if (AV_TYPE_VIDEO == type)
    {
        if (VIDEO_CODEC_H264 == m_rua.video_codec)
        {
            h264_rtp_rx(&h264rxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_JPEG == m_rua.video_codec)
        {
            mjpeg_rtp_rx(&mjpegrxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_MP4 == m_rua.video_codec)
        {
            mpeg4_rtp_rx(&mpeg4rxi, p_rtp, rtp_len);
        }
        else if (VIDEO_CODEC_H265 == m_rua.video_codec)
        {
            h265_rtp_rx(&h265rxi, p_rtp, rtp_len);
        }
    }
    else if (AV_TYPE_AUDIO == type)
    {
        if (AUDIO_CODEC_AAC == m_rua.audio_info.codec)
        {
            aac_rtp_rx(&aacrxi, p_rtp, rtp_len);
        }
        else if (AUDIO_CODEC_NONE != m_rua.audio_info.codec)
        {
            pcm_rtp_rx(&pcmrxi, p_rtp, rtp_len);
        }
    }
#ifdef METADATA    
    else if (AV_TYPE_METADATA == type)
    {
        pcm_rtp_rx(&metadatarxi, p_rtp, rtp_len);
    }
#endif    
}

void CRtspClient::rtcp_data_rx(uint8 * data, int rlen, int type)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pRtcpCB)
    {
        m_pRtcpCB(data, rlen, type, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

int CRtspClient::rtsp_msg_parser(RCUA * p_rua)
{
    int rtsp_pkt_len = rtsp_pkt_find_end(p_rua->rcv_buf, p_rua->rcv_dlen);
    if (rtsp_pkt_len == 0) // wait for next recv
    {
        return RTSP_PARSE_MOREDATA;
    }
    
    HRTSP_MSG * rx_msg = rtsp_get_msg_buf();
    if (rx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return null!!!\r\n", __FUNCTION__);
        return RTSP_PARSE_FAIL;
    }
    
    memcpy(rx_msg->msg_buf, p_rua->rcv_buf, rtsp_pkt_len);
    rx_msg->msg_buf[rtsp_pkt_len] = '\0';

    log_print(HT_LOG_DBG, "RX << %s\r\n", rx_msg->msg_buf);

    int parse_len = rtsp_msg_parse_part1(rx_msg->msg_buf, rtsp_pkt_len, rx_msg);
    if (parse_len != rtsp_pkt_len)    //parse error
    {
        log_print(HT_LOG_ERR, "%s, rtsp_msg_parse_part1=%d, rtsp_pkt_len=%d!!!\r\n", 
            __FUNCTION__, parse_len, rtsp_pkt_len);
        rtsp_free_msg(rx_msg);

        p_rua->rcv_dlen = 0;
        return RTSP_PARSE_FAIL;
    }
    
    if (rx_msg->ctx_len > 0)    
    {
        if (p_rua->rcv_dlen < (parse_len + rx_msg->ctx_len))
        {
            rtsp_free_msg(rx_msg);
            return RTSP_PARSE_MOREDATA;
        }

        memcpy(rx_msg->msg_buf+rtsp_pkt_len, p_rua->rcv_buf+rtsp_pkt_len, rx_msg->ctx_len);
        rx_msg->msg_buf[rtsp_pkt_len+rx_msg->ctx_len] = '\0';

        log_print(HT_LOG_DBG, "%s\r\n", rx_msg->msg_buf+rtsp_pkt_len);
        
        int sdp_parse_len = rtsp_msg_parse_part2(rx_msg->msg_buf+rtsp_pkt_len, rx_msg->ctx_len, rx_msg);
        if (sdp_parse_len != rx_msg->ctx_len)
        {
        }
        parse_len += rx_msg->ctx_len;
    }
    
    if (parse_len < p_rua->rcv_dlen)
    {
        while (p_rua->rcv_buf[parse_len] == ' ' || 
            p_rua->rcv_buf[parse_len] == '\r' || 
            p_rua->rcv_buf[parse_len] == '\n')
        {
            parse_len++;
        }
        
        memmove(p_rua->rcv_buf, p_rua->rcv_buf + parse_len, p_rua->rcv_dlen - parse_len);
        p_rua->rcv_dlen -= parse_len;
    }
    else
    {
        p_rua->rcv_dlen = 0;
    }
    
    int ret = rtsp_client_state(p_rua, rx_msg);
    
    rtsp_free_msg(rx_msg);

    return ret ? RTSP_PARSE_SUCC : RTSP_PARSE_FAIL;
}

#ifdef EPOLL

int CRtspClient::rtsp_epoll_rx()
{
    int i, nfds;
    struct epoll_event * ep_events = (struct epoll_event *) m_ep_events;
    
    nfds = epoll_wait(m_ep_fd, ep_events, m_ep_event_num, 1000);
    if (0 == nfds)
    {
        return RTSP_RX_TIMEOUT;
    }

    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    
    for (i = 0; i < nfds; i++)
    {
        if (ep_events[i].events & EPOLLIN)
        {
            int fd = (int)(ep_events[i].data.u64 & 0xFFFF);

            if ((ep_events[i].data.u64 & ((uint64)1 << 63)) != 0)
            {
                if (rtsp_tcp_rx() == RTSP_RX_FAIL)
                {
                    return RTSP_RX_FAIL;
                }
            }
            else
            {
                int ch;
                char buf[2048];

                if (m_rua.sockaddr.ipv6_flag)
                {
                    addr = (struct sockaddr *) &addr6;
                    addrlen = sizeof(struct sockaddr_in6);
                }
                else
                {
                    addr = (struct sockaddr *) &addr4;
                    addrlen = sizeof(struct sockaddr_in);
                }

                int rlen = recvfrom(fd, buf, sizeof(buf), 0, addr, &addrlen);
                if (rlen <= 12)
                {
                    log_print(HT_LOG_ERR, "%s, recvfrom return %d, err[%s]!!!\r\n", 
                        __FUNCTION__, rlen, sys_os_get_socket_error());
                    continue;
                }

                for (ch = 0; ch < AV_MAX_CHS; ch++)
                {
                    if (m_rua.channels[ch].udp_fd == fd)
                    {
#ifdef SRTP
                        if (!srtp_udp_data_rx(ch, 0, (uint8*)buf, &rlen))
                        {
                            continue;
                        }
#endif
                        udp_data_rx((uint8*)buf, rlen, ch);
                    }
                    else if (m_rua.channels[ch].rtcp_fd == fd)
                    {
#ifdef SRTP
                        if (!srtp_udp_data_rx(ch, 1, (uint8*)buf, &rlen))
                        {
                            continue;
                        }
#endif
                        rtcp_data_rx((uint8*)buf, rlen, ch);
                    }
                }
            }
        }
    }

    return RTSP_RX_SUCC;
}

#endif

int CRtspClient::rtsp_tcp_data_rx(RCUA * p_rua)
{
rx_point:

    if (rtsp_is_rtsp_msg(p_rua->rcv_buf))    //Is RTSP Packet?
    {
        int ret = rtsp_msg_parser(p_rua);
        if (ret == RTSP_PARSE_FAIL)
        {
            return RTSP_RX_FAIL;
        }
        else if (ret == RTSP_PARSE_MOREDATA)
        {
            return RTSP_RX_SUCC;
        }

        if (p_rua->rcv_dlen >= 4)
        {
            goto rx_point;
        }
    }
    else
    {
        uint8 * p_buff = (uint8 *) p_rua->rcv_buf;
        uint8 magic = net_read_uint8(p_buff);
        
        if (magic != 0x24)
        {        
            log_print(HT_LOG_WARN, "%s, magic[0x%02X]!!!\r\n", __FUNCTION__, magic);

            // Try to recover from wrong data
            
            for (int i = 1; i <= p_rua->rcv_dlen - 4; i++)
            {
                if (p_rua->rcv_buf[i] == 0x24 &&
                    (p_rua->rcv_buf[i+1] == p_rua->channels[AV_VIDEO_CH].interleaved ||
                     p_rua->rcv_buf[i+1] == p_rua->channels[AV_AUDIO_CH].interleaved))
                {
                    memmove(p_rua->rcv_buf, p_rua->rcv_buf+i, p_rua->rcv_dlen - i);
                    p_rua->rcv_dlen -= i;
                    goto rx_point;
                }
            }

            p_rua->rcv_dlen = 0;
            
            return RTSP_RX_SUCC;
        }
        
        uint16 rtp_len = net_read_uint16(p_buff+2);
        if (rtp_len > (p_rua->rcv_dlen - 4))
        {
            if (p_rua->rtp_rcv_buf)
            {
                free(p_rua->rtp_rcv_buf);
            }
            
            p_rua->rtp_rcv_buf = (char *)malloc(rtp_len+4);
            if (p_rua->rtp_rcv_buf == NULL) 
            {
                return RTSP_RX_FAIL;
            }
            
            memcpy(p_rua->rtp_rcv_buf, p_rua->rcv_buf, p_rua->rcv_dlen);
            p_rua->rtp_rcv_len = p_rua->rcv_dlen;
            p_rua->rtp_t_len = rtp_len+4;

            p_rua->rcv_dlen = 0;

            return RTSP_RX_SUCC;
        }
            
        tcp_data_rx(p_buff, rtp_len+4);

        p_rua->rcv_dlen -= rtp_len+4;
        if (p_rua->rcv_dlen > 0)
        {
            memmove(p_rua->rcv_buf, p_rua->rcv_buf+rtp_len+4, p_rua->rcv_dlen);
        }

        if (p_rua->rcv_dlen >= 4)
        {
            goto rx_point;
        }
    }

    return RTSP_RX_SUCC;
}

int CRtspClient::rtsp_over_tcp_rx(RCUA * p_rua)
{
    int rlen;
    BOOL selflag = TRUE;
    SOCKET fd;

    fd = p_rua->fd;
    if (fd <= 0)
    {
        return RTSP_RX_FAIL;
    }

#ifdef RTSPS
    if (p_rua->rtsps)
    {
        sys_os_mutex_enter(p_rua->ssl_mutex);
        if (p_rua->ssl)
        {
            if (SSL_pending((SSL*)p_rua->ssl) > 0)
            {
                selflag = FALSE; // There is data to read 
            }
        }
        sys_os_mutex_leave(p_rua->ssl_mutex);
    }
#endif

    if (selflag)
    {
#ifndef EPOLL
        fd_set fdread;
        struct timeval tv = {1, 0};

        FD_ZERO(&fdread);
        FD_SET(fd, &fdread);

        int sret = select((int)(fd+1), &fdread, NULL, NULL, &tv); 
        if (sret == 0) // Time expired 
        { 
            return RTSP_RX_TIMEOUT; 
        }
        else if (!FD_ISSET(fd, &fdread))
        {
            return RTSP_RX_TIMEOUT;
        }
#endif
    }

    if (p_rua->rtp_rcv_buf == NULL || p_rua->rtp_t_len == 0)
    {
#ifdef RTSPS
        if (p_rua->rtsps)
        {
            sys_os_mutex_enter(p_rua->ssl_mutex);
            if (p_rua->ssl)
            {
                rlen = SSL_read((SSL*)p_rua->ssl, p_rua->rcv_buf+p_rua->rcv_dlen, sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->ssl_mutex);
        }
        else 
#endif
        {
            rlen = recv(fd, p_rua->rcv_buf+p_rua->rcv_dlen, sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen, 0);
        }
        
        if (rlen <= 0)
        {
            // recv error, connection maybe disconn
            
            log_print(HT_LOG_WARN, "%s, ret = %d, err = %s\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());    
            return RTSP_RX_FAIL;
        }

        p_rua->rcv_dlen += rlen;

        if (p_rua->rcv_dlen < 4)
        {
            return RTSP_RX_SUCC;
        }

        p_rua->rcv_buf[p_rua->rcv_dlen] = '\0';
    }
    else
    {
#ifdef RTSPS
        if (p_rua->rtsps)
        {
            sys_os_mutex_enter(p_rua->ssl_mutex);
            if (p_rua->ssl)
            {
                rlen = SSL_read((SSL*)p_rua->ssl, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, p_rua->rtp_t_len-p_rua->rtp_rcv_len);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->ssl_mutex);
        }
        else 
#endif
        {
            rlen = recv(fd, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, p_rua->rtp_t_len-p_rua->rtp_rcv_len, 0);
        }
        
        if (rlen <= 0)
        {
            // recv error, connection maybe disconn
            
            log_print(HT_LOG_WARN, "%s, ret = %d, err = %s\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());    
            return RTSP_RX_FAIL;
        }

        p_rua->rtp_rcv_len += rlen;
        
        if (p_rua->rtp_rcv_len == p_rua->rtp_t_len)
        {
            tcp_data_rx((uint8*)p_rua->rtp_rcv_buf, p_rua->rtp_rcv_len);
            
            free(p_rua->rtp_rcv_buf);
            p_rua->rtp_rcv_buf = NULL;
            p_rua->rtp_rcv_len = 0;
            p_rua->rtp_t_len = 0;
        }
        
        return RTSP_RX_SUCC;
    }

    return rtsp_tcp_data_rx(p_rua);
}

int CRtspClient::rtsp_tcp_rx()
{
    RCUA * p_rua = &m_rua;

#ifdef OVER_HTTP    
    if (p_rua->over_http)
    {
        return rtsp_over_http_rx(p_rua);
    }
    else 
#endif
#ifdef OVER_WEBSOCKET
    if (p_rua->over_ws)
    {
        return rtsp_over_ws_rx(p_rua);
    }
    else 
#endif
    {
        return rtsp_over_tcp_rx(p_rua);
    }
}

int CRtspClient::rtsp_udp_rx()
{
    int i, max_fd = 0;
    fd_set fdr;
    
    FD_ZERO(&fdr);

    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (m_rua.channels[i].udp_fd > 0)
        {
            FD_SET(m_rua.channels[i].udp_fd, &fdr);
            max_fd = (max_fd >= (int)m_rua.channels[i].udp_fd)? max_fd : (int)m_rua.channels[i].udp_fd;
        }
        
        if (m_rua.channels[i].rtcp_fd > 0)
        {
            FD_SET(m_rua.channels[i].rtcp_fd, &fdr);
            max_fd = (max_fd >= (int)m_rua.channels[i].rtcp_fd)? max_fd : (int)m_rua.channels[i].rtcp_fd;
        }
    }

    struct timeval tv = {1, 0};

    int sret = select(max_fd+1, &fdr, NULL, NULL, &tv);
    if (sret <= 0)
    {
        return RTSP_RX_TIMEOUT;
    }

    char buf[2048];
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;

    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (m_rua.channels[i].udp_fd > 0 && FD_ISSET(m_rua.channels[i].udp_fd, &fdr))
        {
            if (m_rua.sockaddr.ipv6_flag)
            {
                addr = (struct sockaddr *) &addr6;
                addrlen = sizeof(struct sockaddr_in6);
            }
            else
            {
                addr = (struct sockaddr *) &addr4;
                addrlen = sizeof(struct sockaddr_in);
            }
            
            int rlen = recvfrom(m_rua.channels[i].udp_fd, buf, sizeof(buf), 0, addr, &addrlen);
            if (rlen <= 12)
            {
                log_print(HT_LOG_ERR, "%s, recvfrom return %d, err[%s]!!!\r\n", 
                    __FUNCTION__, rlen, sys_os_get_socket_error());
            }
            else
            {
#ifdef SRTP
                if (srtp_udp_data_rx(i, 0, (uint8*)buf, &rlen))
                {
                    udp_data_rx((uint8*)buf, rlen, i);
                }
#else
                udp_data_rx((uint8*)buf, rlen, i);
#endif
            }
        }

        if (m_rua.channels[i].rtcp_fd > 0 && FD_ISSET(m_rua.channels[i].rtcp_fd, &fdr))
        {
            if (m_rua.sockaddr.ipv6_flag)
            {
                addr = (struct sockaddr *) &addr6;
                addrlen = sizeof(struct sockaddr_in6);
            }
            else
            {
                addr = (struct sockaddr *) &addr4;
                addrlen = sizeof(struct sockaddr_in);
            }
            
            int rlen = recvfrom(m_rua.channels[i].rtcp_fd, buf, sizeof(buf), 0, addr, &addrlen);
            if (rlen <= 12)
            {
                log_print(HT_LOG_ERR, "%s, recvfrom return %d, err[%s]!!!\r\n", 
                    __FUNCTION__, rlen, sys_os_get_socket_error());
            }
            else
            {
#ifdef SRTP
                if (srtp_udp_data_rx(i, 1, (uint8*)buf, &rlen))
                {
                    rtcp_data_rx((uint8*)buf, rlen, i);
                }
#else
                rtcp_data_rx((uint8*)buf, rlen, i);
#endif
            }
        }
    }

    return RTSP_RX_SUCC;
}

BOOL CRtspClient::rtsp_start(const char * url, const char * ip, int port, const char * user, const char * pass)
{
    char proto[16];
    
    if (m_rua.state != RCS_NULL)
    {
        rtsp_play();
        return TRUE;
    }

    if (user && user != m_rua.auth_info.auth_name)
    {
        strcpy(m_rua.auth_info.auth_name, user);
    }

    if (pass && pass != m_rua.auth_info.auth_pwd)
    {
        strcpy(m_rua.auth_info.auth_pwd, pass);
    }
    
    if (url && url != m_url)
    {
        strcpy(m_url, url);
    }
    
    if (ip && ip != m_ip)
    {
        strcpy(m_ip, ip);
    }
    
    m_nport = port;

#ifdef RTSPS
    if (m_rua.rtsps)
    {
        strcpy(proto, "rtsps");
    }
    else
#endif
    {
        strcpy(proto, "rtsp");
    }

    if (m_suffix[0] == '/')
    {
        if (is_ipv6_address(m_ip))
        {
            snprintf(m_rua.uri, sizeof(m_rua.uri)-1, "%s://[%s]:%d%s", proto, m_ip, m_nport, m_suffix);
        }
        else
        {
            snprintf(m_rua.uri, sizeof(m_rua.uri)-1, "%s://%s:%d%s", proto, m_ip, m_nport, m_suffix);
        }
    }
    else
    {
        if (is_ipv6_address(m_ip))
        {
            snprintf(m_rua.uri, sizeof(m_rua.uri)-1, "%s://[%s]:%d/%s", proto, m_ip, m_nport, m_suffix);
        }
        else
        {
            snprintf(m_rua.uri, sizeof(m_rua.uri)-1, "%s://%s:%d/%s", proto, m_ip, m_nport, m_suffix);
        }
    }

    m_bRunning = TRUE;
    m_tcpRxTid = sys_os_create_thread((void *)rtsp_tcp_rx_thread, this);
    if (m_tcpRxTid == 0)
    {
        log_print(HT_LOG_ERR, "%s, sys_os_create_thread failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }
    
    return TRUE;
}

BOOL CRtspClient::rtsp_start(const char * url, const char * user, const char * pass)
{
    int port;
    char proto[32], username[64], password[64], host[128], path[200];

    url_split(url, proto, sizeof(proto), username, sizeof(username), 
        password, sizeof(password), host, sizeof(host), &port, path, sizeof(path));

    if (strcasecmp(proto, "rtsp") == 0)
    {
        port = (port == -1) ? 554 : port;
    }
#ifdef RTSPS
    else if (strcasecmp(proto, "rtsps") == 0)
    {
        set_rtsp_over_ssl(1);

        port = (port == -1) ? 322 : port;
    }
#endif
#ifdef OVER_HTTP    
    else if (strcasecmp(proto, "http") == 0)
    {
        set_rtsp_over_http(1, 0);
        
        port = (port == -1) ? 80 : port;
    }
    else if (strcasecmp(proto, "https") == 0)
    {
        set_rtsp_over_http(1, 0);
        
        port = (port == -1) ? 443 : port;
    }
#endif
#ifdef OVER_WEBSOCKET    
    else if (strcasecmp(proto, "ws") == 0)
    {
        set_rtsp_over_ws(1, 0);

        port = (port == -1) ? 80 : port;
    }
    else if (strcasecmp(proto, "wss") == 0)
    {
        set_rtsp_over_ws(1, 0);

        port = (port == -1) ? 443 : port;
    }
#endif
    else
    {
        return FALSE;
    }

    if (host[0] == '\0')
    {
        return FALSE;
    }

    strncpy(m_ip, host, sizeof(m_ip) - 1);

    if (username[0] != '\0')
    {
        strcpy(m_rua.auth_info.auth_name, username);
    }
    else if (user && user[0] != '\0')
    {
        strcpy(m_rua.auth_info.auth_name, user);
    }

    if (password[0] != '\0')
    {
        strcpy(m_rua.auth_info.auth_pwd, password);
    }
    else if (pass && pass[0] != '\0')
    {
        strcpy(m_rua.auth_info.auth_pwd, pass);
    }

    if (path[0] != '\0')
    {
        strncpy(m_suffix, path, sizeof(m_suffix) - 1);
    }

    if (path[0] == '/')
    {
        snprintf(m_url, sizeof(m_url)-1, "%s://%s:%d%s", proto, host, port, path);
    }
    else
    {
        snprintf(m_url, sizeof(m_url)-1, "%s://%s:%d/%s", proto, host, port, path);
    }
    
    m_nport = port;

    return rtsp_start(m_url, m_ip, m_nport, m_rua.auth_info.auth_name, m_rua.auth_info.auth_pwd);
}

BOOL CRtspClient::rtsp_play(int npt_start)
{
    m_rua.cseq++;
    m_rua.seek_pos = npt_start;
    
    HRTSP_MSG * tx_msg = rcua_build_play(&m_rua);    
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);
    }
    
    return TRUE;
}

BOOL CRtspClient::rtsp_stop()
{
    if (RCS_NULL == m_rua.state)
    {
        return TRUE;
    }
    
    m_rua.cseq++;
    m_rua.state = RCS_NULL;
    
    HRTSP_MSG * tx_msg = rcua_build_teardown(&m_rua);
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);    
    }
    
    return TRUE;
}

BOOL CRtspClient::rtsp_pause()
{
    m_rua.cseq++;
    
    HRTSP_MSG * tx_msg = rcua_build_pause(&m_rua);    
    if (tx_msg)
    {
        rcua_send_free_rtsp_msg(&m_rua, tx_msg);    
    }
    
    return TRUE;
}

BOOL CRtspClient::rtsp_close()
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = NULL;
    m_pVideoCB = NULL;
    m_pRtcpCB = NULL;
#ifdef METADATA    
    m_pMetadataCB = NULL;
#endif
    m_pNotify = NULL;
    m_pUserdata = NULL;
    sys_os_mutex_leave(m_pMutex);

#ifdef OVER_HTTP
    if (m_rua.over_http)
    {
#ifdef HTTPS    
        if (m_rua.rtsp_recv.https && m_rua.rtsp_recv.ssl)
        {
            SSL_shutdown((SSL*)m_rua.rtsp_recv.ssl);
        }
        
        if (m_rua.rtsp_send.https && m_rua.rtsp_send.ssl)
        {
            SSL_shutdown((SSL*)m_rua.rtsp_send.ssl);
        }
#endif

        if (m_rua.rtsp_recv.cfd > 0)
        {
#ifdef EPOLL
            epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, m_rua.rtsp_recv.cfd, NULL);
#endif        
            closesocket(m_rua.rtsp_recv.cfd);
            m_rua.rtsp_recv.cfd = 0;
        }
        
        if (m_rua.rtsp_send.cfd)
        {
            closesocket(m_rua.rtsp_send.cfd);
            m_rua.rtsp_send.cfd = 0;
        }
    }
#endif

#ifdef OVER_WEBSOCKET
    if (m_rua.over_ws)
    {
#ifdef HTTPS    
        if (m_rua.ws_http.https && m_rua.ws_http.ssl)
        {
            SSL_shutdown((SSL*)m_rua.ws_http.ssl);
        }
#endif

        if (m_rua.ws_http.cfd > 0)
        {
#ifdef EPOLL
            epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, m_rua.ws_http.cfd, NULL);
#endif        
            closesocket(m_rua.ws_http.cfd);
            m_rua.ws_http.cfd = 0;
        }
    }
#endif

    m_bRunning = FALSE;

    sys_os_wait_thread(&m_tcpRxTid);

    sys_os_wait_thread(&m_udpRxTid);

    for (int i = 0; i < AV_MAX_CHS; i++)
    {
        if (m_rua.channels[i].udp_fd > 0)
        {
#ifdef EPOLL        
            epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, m_rua.channels[i].udp_fd, NULL);
#endif

            closesocket(m_rua.channels[i].udp_fd);
            m_rua.channels[i].udp_fd = 0;
        }

        if (m_rua.channels[i].rtcp_fd > 0)
        {
#ifdef EPOLL        
            epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, m_rua.channels[i].rtcp_fd, NULL);
#endif

            closesocket(m_rua.channels[i].rtcp_fd);
            m_rua.channels[i].rtcp_fd = 0;
        }

#ifdef SRTP
        if (m_rua.channels[i].srtp)
        {
            srtp_dealloc(m_rua.channels[i].srtp);
            m_rua.channels[i].srtp = NULL;
        }
#endif
    }

#ifdef BACKCHANNEL
    if (m_rua.bc_audio_captrue)
    {
        m_rua.bc_audio_captrue->delCallback(rtsp_bc_cb, &m_rua);
        m_rua.bc_audio_captrue->freeInstance(0);
        m_rua.bc_audio_captrue = NULL;
    }
#endif
    
    if (m_rua.video_codec == VIDEO_CODEC_H264)
    {
        h264_rxi_deinit(&h264rxi);
    }
    else if (m_rua.video_codec == VIDEO_CODEC_H265)
    {
        h265_rxi_deinit(&h265rxi);
    }
    else if (m_rua.video_codec == VIDEO_CODEC_JPEG)
    {
        mjpeg_rxi_deinit(&mjpegrxi);
    }
    else if (m_rua.video_codec == VIDEO_CODEC_MP4)
    {
        mpeg4_rxi_deinit(&mpeg4rxi);
    }

    if (m_rua.audio_info.codec == AUDIO_CODEC_AAC)
    {
        aac_rxi_deinit(&aacrxi);
    }
    else if (m_rua.audio_info.codec != AUDIO_CODEC_NONE)
    {
        pcm_rxi_deinit(&pcmrxi);
    }

#ifdef METADATA
    pcm_rxi_deinit(&metadatarxi);
#endif

    if (m_rua.audio_spec)
    {
        free(m_rua.audio_spec);
        m_rua.audio_spec = NULL;
    }

#ifdef OVER_HTTP
    if (m_rua.over_http)
    {
        http_cln_free_req(&m_rua.rtsp_recv);
        http_cln_free_req(&m_rua.rtsp_send);
    }
#endif

#ifdef OVER_WEBSOCKET
    if (m_rua.over_ws)
    {
        http_cln_free_req(&m_rua.ws_http);
    }

    if (m_rua.ws_msg.buff)
    {
        free(m_rua.ws_msg.buff);
        m_rua.ws_msg.buff = NULL;
    }
#endif

#ifdef RTSPS
    if (m_rua.rtsps)
    {
        sys_os_mutex_enter(m_rua.ssl_mutex);
        if (m_rua.ssl)
        {
            SSL_shutdown((SSL*)m_rua.ssl);
            SSL_free((SSL*)m_rua.ssl);
            m_rua.ssl = NULL;
        }
        sys_os_mutex_leave(m_rua.ssl_mutex);

        if (m_rua.ssl_mutex)
        {
            sys_os_destroy_mutex(m_rua.ssl_mutex);
            m_rua.ssl_mutex = NULL;
        }
    }
#endif

    set_default();
    
    return TRUE;
}

void CRtspClient::rtsp_video_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pVideoCB)
    {
        m_pVideoCB(p_data, len, ts, seq, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::rtsp_audio_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pAudioCB)
    {
        m_pAudioCB(p_data, len, ts, seq, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::rtsp_meta_data_cb(uint8 * p_data, int len, uint32 ts, uint32 seq)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pMetadataCB)
    {
        m_pMetadataCB(p_data, len, ts, seq, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::rtsp_keep_alive()
{
    uint32 cur_time = sys_os_get_uptime();
    
    if (cur_time - m_rua.keepalive_time >= (uint32)(m_rua.session_timeout - 5))
    {
        m_rua.keepalive_time = cur_time;
        m_rua.cseq++;
        
        HRTSP_MSG * tx_msg;

        if (m_rua.gp_cmd) // the rtsp server supports GET_PARAMETER command
        {
            tx_msg = rcua_build_get_parameter(&m_rua);
        }
        else
        {
            tx_msg = rcua_build_options(&m_rua);
        }
        
        if (tx_msg)
        {
            rcua_send_free_rtsp_msg(&m_rua, tx_msg);
        }
    }
}

void CRtspClient::tcp_rx_thread()
{
    int  ret;
    int  tm_count = 0;
    BOOL nodata_notify = FALSE;
#ifdef EPOLL
    int  fd;
    uint64 e_dat;
#endif

    send_notify(RTSP_EVE_CONNECTING);

#ifdef OVER_HTTP
    if (m_rua.over_http)
    {
        ret = rtsp_over_http_start();
    }
    else 
#endif
#ifdef OVER_WEBSOCKET
    if (m_rua.over_ws)
    {
        ret = rtsp_over_ws_start();
    }
    else 
#endif
    {
        ret = rtsp_client_start();
    }
    
    if (!ret)    
    {
        send_notify(RTSP_EVE_CONNFAIL);
        goto rtsp_rx_exit;
    }

#ifdef EPOLL
    
#ifdef OVER_HTTP    
    if (m_rua.over_http)
    {
        fd = m_rua.rtsp_recv.cfd;
    }
    else
#endif
#ifdef OVER_WEBSOCKET
    if (m_rua.over_ws)
    {
        fd = m_rua.ws_http.cfd;
    }
    else
#endif
    {
        fd = m_rua.fd;
    }
    
    e_dat = fd;
    e_dat |= ((uint64)1 << 63);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = e_dat;
    epoll_ctl(m_ep_fd, EPOLL_CTL_ADD, fd, &event);
#endif

    while (m_bRunning)
    {
#ifdef EPOLL
        ret = rtsp_epoll_rx();
#else
        ret = rtsp_tcp_rx();
#endif

        if (ret == RTSP_RX_FAIL)
        {
            break;
        }
        else 
#ifndef EPOLL
        if (m_rua.rtp_tcp)
        {
#endif
            if (ret == RTSP_RX_TIMEOUT)
            {
                tm_count++;
                if (tm_count >= m_nRxTimeout && !nodata_notify)    // in 10s without data
                {
                    nodata_notify = TRUE;
                    send_notify(RTSP_EVE_NODATA);
                }
            }
            else // should be RTSP_RX_SUCC
            {
                if (nodata_notify)
                {
                    nodata_notify = FALSE;
                    send_notify(RTSP_EVE_RESUME);
                }
                
                tm_count = 0;
            }
#ifndef EPOLL
        }
        else
        {
            if (ret == RTSP_RX_TIMEOUT)
            {
                usleep(100*1000);
            }
        }
#endif

        if (m_rua.state == RCS_PLAYING)
        {            
            rtsp_keep_alive();
        }
    }

    if (m_rua.fd > 0)
    {
#ifdef EPOLL        
        epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, m_rua.fd, NULL);
#endif

        closesocket(m_rua.fd);
        m_rua.fd = 0;
    }

    if (m_rua.rtp_rcv_buf)
    {
        free(m_rua.rtp_rcv_buf);
        m_rua.rtp_rcv_buf = NULL;
    }
    
    send_notify(RTSP_EVE_STOPPED);

rtsp_rx_exit:

    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void CRtspClient::udp_rx_thread()
{
    int  ret;
    int  tm_count = 0;
    BOOL nodata_notify = FALSE;
    
    while (m_bRunning)
    {
        ret = rtsp_udp_rx();
        if (ret == RTSP_RX_FAIL)
        {
            break;
        }
        else if (ret == RTSP_RX_TIMEOUT)
        {
            tm_count++;
            if (tm_count >= m_nRxTimeout && !nodata_notify)    // in 10s without data
            {
                nodata_notify = TRUE;
                send_notify(RTSP_EVE_NODATA);
            }
        }
        else // should be RTSP_RX_SUCC
        {
            if (nodata_notify)
            {
                nodata_notify = FALSE;
                send_notify(RTSP_EVE_RESUME);
            }
            
            tm_count = 0;
        }
    }
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void CRtspClient::send_notify(int event)
{
    sys_os_mutex_enter(m_pMutex);
    
    if (m_pNotify)
    {
        m_pNotify(event, m_pUserdata);
    }

    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::get_h264_params()
{
    rtsp_send_h264_params(&m_rua);
}

BOOL CRtspClient::get_h264_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len)
{
    char sps[1000] = {'\0'}, pps[1000] = {'\0'};
    
    if (!rcua_get_sdp_h264_params(&m_rua, NULL, sps, sizeof(sps)))
    {
        if (h264rxi.param_sets.sps_len > 0 && h264rxi.param_sets.pps_len > 0)
        {
            memcpy(p_sps, h264rxi.param_sets.sps, h264rxi.param_sets.sps_len);
            *sps_len = h264rxi.param_sets.sps_len;

            memcpy(p_pps, h264rxi.param_sets.pps, h264rxi.param_sets.pps_len);
            *pps_len = h264rxi.param_sets.pps_len;

            return TRUE;
        }
    
        return FALSE;
    }

    char * ptr = strchr(sps, ',');
    if (ptr && ptr[1] != '\0')
    {
        *ptr = '\0';
        strcpy(pps, ptr+1);
    }

    uint8 sps_pps[1000];
    sps_pps[0] = 0x0;
    sps_pps[1] = 0x0;
    sps_pps[2] = 0x0;
    sps_pps[3] = 0x1;
    
    int len = base64_decode(sps, strlen(sps), sps_pps+4, sizeof(sps_pps)-4);
    if (len <= 0)
    {
        return FALSE;
    }

    if ((sps_pps[4] & 0x1f) == 7)
    {
        memcpy(p_sps, sps_pps, len+4);
        *sps_len = len+4;
    }
    else if ((sps_pps[4] & 0x1f) == 8)
    {
        memcpy(p_pps, sps_pps, len+4);
        *pps_len = len+4;
    }
    
    if (pps[0] != '\0')
    {        
        len = base64_decode(pps, strlen(pps), sps_pps+4, sizeof(sps_pps)-4);
        if (len > 0)
        {
            if ((sps_pps[4] & 0x1f) == 7)
            {
                memcpy(p_sps, sps_pps, len+4);
                *sps_len = len+4;
            }
            else if ((sps_pps[4] & 0x1f) == 8)
            {
                memcpy(p_pps, sps_pps, len+4);
                *pps_len = len+4;
            }
        }
    }

    return TRUE;
}

BOOL CRtspClient::get_h264_sdp_desc(char * p_sdp, int max_len)
{
    return rcua_get_sdp_h264_desc(&m_rua, NULL, p_sdp, max_len);
}

BOOL CRtspClient::get_h265_sdp_desc(char * p_sdp, int max_len)
{
    return rcua_get_sdp_h265_desc(&m_rua, NULL, p_sdp, max_len);
}

BOOL CRtspClient::get_mp4_sdp_desc(char * p_sdp, int max_len)
{    
    return rcua_get_sdp_mp4_desc(&m_rua, NULL, p_sdp, max_len);
}

BOOL CRtspClient::get_aac_sdp_desc(char * p_sdp, int max_len)
{
    return rcua_get_sdp_aac_desc(&m_rua, NULL, p_sdp, max_len);
}

void CRtspClient::get_h265_params()
{
    rtsp_send_h265_params(&m_rua);
}

BOOL CRtspClient::get_h265_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len, uint8 * p_vps, int * vps_len)
{
    int pt;
    BOOL don;
    char vps[1000] = {'\0'}, sps[1000] = {'\0'}, pps[1000] = {'\0'};
    
    if (!rcua_get_sdp_h265_params(&m_rua, &pt, &don, vps, sizeof(vps), sps, sizeof(sps), pps, sizeof(pps)))
    {
        if (h265rxi.param_sets.sps_len > 0 && h265rxi.param_sets.pps_len > 0 && h265rxi.param_sets.vps_len > 0)
        {
            memcpy(p_sps, h265rxi.param_sets.sps, h265rxi.param_sets.sps_len);
            *sps_len = h265rxi.param_sets.sps_len;

            memcpy(p_pps, h265rxi.param_sets.pps, h265rxi.param_sets.pps_len);
            *pps_len = h265rxi.param_sets.pps_len;

            memcpy(p_vps, h265rxi.param_sets.vps, h265rxi.param_sets.vps_len);
            *vps_len = h265rxi.param_sets.vps_len;
            
            return TRUE;
        }
    
        return FALSE;
    }

    uint8 buff[1000];
    buff[0] = 0x0;
    buff[1] = 0x0;
    buff[2] = 0x0;
    buff[3] = 0x1;
    
    if (vps[0] != '\0')
    {
        int len = base64_decode(vps, strlen(vps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            return FALSE;
        }

        memcpy(p_vps, buff, len+4);
        *vps_len = len+4;
    }

    if (sps[0] != '\0')
    {
        int len = base64_decode(sps, strlen(sps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            return FALSE;
        }

        memcpy(p_sps, buff, len+4);
        *sps_len = len+4;
    }

    if (pps[0] != '\0')
    {
        int len = base64_decode(pps, strlen(pps), buff+4, sizeof(buff)-4);
        if (len <= 0)
        {
            return FALSE;
        }

        memcpy(p_pps, buff, len+4);
        *pps_len = len+4;
    }

    return TRUE;
}    

void CRtspClient::rtsp_get_video_size(int * w, int * h)
{
    int i;

    for (i = 0; i < MAX_AVN; i++)
    {
        char * sdpline = m_rua.channels[AV_VIDEO_CH].cap_desc[i];
        
        if (sdpline[0] == '\0')
        {
            break;
        }
        
        if (strncasecmp(sdpline, "a=x-dimensions:", 15) == 0)
        {
            int width, height;
            
            if (sscanf(sdpline, "a=x-dimensions:%d,%d", &width, &height) == 2) 
            {
                if (w) *w = width;
                if (h) *h = height;

                break;
            }
        }
    }
}

BOOL CRtspClient::rtsp_get_video_media_info()
{
    if (m_rua.channels[AV_VIDEO_CH].cap_count == 0)
    {
        return FALSE;
    }

    if (m_rua.channels[AV_VIDEO_CH].cap[0] == 26)
    {
        m_rua.video_codec = VIDEO_CODEC_JPEG;
    }

    for (int i=0; i<MAX_AVN; i++)
    {
        char * ptr = m_rua.channels[AV_VIDEO_CH].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", 9) == 0)
        {
            int next = 0;
            char buff[100];
        
            ptr += 9;

            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;
            
            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_STRING);

            if (strcasecmp(buff, "H264/90000") == 0)
            {
                m_rua.video_codec = VIDEO_CODEC_H264;
            }
            else if (strcasecmp(buff, "JPEG/90000") == 0)
            {
                m_rua.video_codec = VIDEO_CODEC_JPEG;
            }
            else if (strcasecmp(buff, "MP4V-ES/90000") == 0)
            {
                m_rua.video_codec = VIDEO_CODEC_MP4;
            }
            else if (strcasecmp(buff, "H265/90000") == 0)
            {
                m_rua.video_codec = VIDEO_CODEC_H265;
            }

            break;
        }
    }

    return TRUE;
}

BOOL CRtspClient::rtsp_get_audio_media_info(int ch, AUDIO_INFO * p_info)
{
    if (m_rua.channels[ch].cap_count == 0)
    {
        return FALSE;
    }

    if (m_rua.channels[ch].cap[0] == 0)
    {
        p_info->codec = AUDIO_CODEC_G711U;
    }
    else if (m_rua.channels[ch].cap[0] == 8)
    {
        p_info->codec = AUDIO_CODEC_G711A;
    }
    else if (m_rua.channels[ch].cap[0] == 9)
    {
        p_info->codec = AUDIO_CODEC_G722;
    }

    for (int i=0; i<MAX_AVN; i++)
    {
        char * ptr = m_rua.channels[ch].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", 9) == 0)
        {
            int next = 0;
            char buff[64];

            ptr += 9;

            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;
            
            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff),  &next, WORD_TYPE_STRING);

            uppercase(buff);

            if (strstr(buff, "G726"))
            {
                p_info->codec = AUDIO_CODEC_G726;
                p_info->bitpersample = atoi(buff+5) / 8; // G726-16, G726-24,G726-32,G726-48

                if (p_info->bitpersample == 0)
                {
                    p_info->bitpersample = 2;
                }
            }
            else if (strstr(buff, "G722"))
            {
                p_info->codec = AUDIO_CODEC_G722;
            }
            else if (strstr(buff, "PCMU"))
            {
                p_info->codec = AUDIO_CODEC_G711U;
            }
            else if (strstr(buff, "PCMA"))
            {
                p_info->codec = AUDIO_CODEC_G711A;
            }
            else if (strstr(buff, "MPEG4-GENERIC"))
            {
                p_info->codec = AUDIO_CODEC_AAC;
            }
            else if (strstr(buff, "OPUS"))
            {
                p_info->codec = AUDIO_CODEC_OPUS;
            }

            char * p = strchr(buff, '/');
            if (p)
            {
                p++;
                
                char * p1 = strchr(p, '/');
                if (p1)
                {
                    *p1 = '\0';
                    p_info->samplerate = atoi(p);

                    p1++;
                    if (p1 && *p1 != '\0')
                    {
                        p_info->channels = atoi(p1);
                    }
                    else
                    {
                        p_info->channels = 1;
                    }
                }
                else
                {
                    p_info->samplerate = atoi(p);
                    p_info->channels = 1;
                }
            }

            break;
        }
    }

    if (p_info->codec == AUDIO_CODEC_G722)
    {
        p_info->samplerate = 16000;
        p_info->channels = 1;
    }
    
    if (p_info->codec == AUDIO_CODEC_OPUS)
    {
        p_info->samplerate = 48000;
    }
    
    return TRUE;
}

void CRtspClient::set_notify_cb(notify_cb notify, void * userdata) 
{
    sys_os_mutex_enter(m_pMutex);    
    m_pNotify = notify;
    m_pUserdata = userdata;
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::set_video_cb(video_cb cb) 
{
    sys_os_mutex_enter(m_pMutex);
    m_pVideoCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::set_audio_cb(audio_cb cb)
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::set_rtcp_cb(rtcp_cb cb)
{
    sys_os_mutex_enter(m_pMutex);
    m_pRtcpCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CRtspClient::set_channel(int channel, int flag)
{
    if (channel >= 0 && channel < AV_MAX_CHS)
    {
        m_rua.channels[channel].disabled = !flag;
    }
}

void CRtspClient::set_rtp_multicast(int flag)
{
    if (flag)
    {
        m_rua.rtp_tcp = 0;
    }
    
    m_rua.mcast_flag = flag;
}

void CRtspClient::set_rtp_over_udp(int flag)
{
    if (flag)
    {
        m_rua.rtp_tcp = 0;
    }
    else
    {
        m_rua.rtp_tcp = 1;
    }
}

void CRtspClient::set_rx_timeout(int timeout)
{
    m_nRxTimeout = timeout;
}

void CRtspClient::set_conn_timeout(int timeout)
{
    m_nConnTimeout = timeout;
}


