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
#include "srt_cln.h"
#include "h264.h"
#include "h265.h"
#include "media_format.h"
#include "bs.h"
#include "media_util.h"


void * srt_rx_thread(void * argv)
{
    CSrtClient * pSrtClient = (CSrtClient *) argv;

    pSrtClient->rx_thread();

    return NULL;
}

void ts_payload_cb(uint8 * data, uint32 size, uint32 type, uint64 pts, void * userdata)
{
    CSrtClient * pSrtClient = (CSrtClient *) userdata;

    pSrtClient->srt_payload_cb(data, size, type, pts);
}

CSrtClient::CSrtClient()
: m_nport(0)
, m_fd(0)
, m_eid(0)
, m_bRunning(FALSE)
, m_bVideoReady(FALSE)
, m_bAudioReady(FALSE)
, m_tidRx(0)
, m_nVpsLen(0)
, m_nSpsLen(0)
, m_nPpsLen(0)
, m_nAudioConfigLen(0)
, m_nVideoCodec(VIDEO_CODEC_NONE)
, m_nWidth(0)
, m_nHeight(0)
, m_nFrameRate(0)
, m_nAudioCodec(AUDIO_CODEC_NONE)
, m_nSamplerate(44100)
, m_nChannels(2)
, m_nVideoInitTS(0)
, m_nAudioInitTS(0)
, m_pNotify(NULL)
, m_pUserdata(NULL)
, m_pVideoCB(NULL)
, m_pAudioCB(NULL)
{
    memset(m_url, 0, sizeof(m_url));
    memset(m_user, 0, sizeof(m_user));
    memset(m_pass, 0, sizeof(m_pass));
    memset(m_ip, 0, sizeof(m_ip));
    memset(m_streamid, 0, sizeof(m_streamid));

    memset(&m_pVps, 0, sizeof(m_pVps));
    memset(&m_pSps, 0, sizeof(m_pVps));
    memset(&m_pPps, 0, sizeof(m_pVps));
    memset(&m_pAudioConfig, 0, sizeof(m_pAudioConfig));
    
    m_pMutex = sys_os_create_mutex();

    m_nRxTimeout = 10;  // default 10s timeout

    ts_parser_init(&m_tsParser);

    ts_parser_set_cb(&m_tsParser, ts_payload_cb, this);
}

CSrtClient::~CSrtClient()
{
    srt_close();

    if (m_pMutex)
    {
        sys_os_destroy_mutex(m_pMutex);
        m_pMutex = NULL;
    }

    ts_parser_free(&m_tsParser);
}

BOOL CSrtClient::srt_start(const char * url, const char * user, const char * pass)
{
    int port = 0;
    char proto[32], username[64], password[64], host[100], path[200];

    url_split(url, proto, sizeof(proto), username, sizeof(username), 
        password, sizeof(password), host, sizeof(host), &port, path, sizeof(path));

    if (strcasecmp(proto, "srt") != 0)
    {
        return FALSE;
    }
    
    if (port <= 0)
    {
        return FALSE;
    }

    if (host[0] == '\0')
    {
        return FALSE;
    }

    m_nport = port;

    strncpy(m_ip, host, sizeof(m_ip) - 1);

    char * p = strstr(path, "streamid");
    if (p)
    {
        p += strlen("streamid=");
        strncpy(m_streamid, p, sizeof(m_streamid)-1);
    }
    
    if (url && url[0] != '\0')
    {
        strncpy(m_url, url, sizeof(m_url)-1);
    }

    if (user && user[0] != '\0')
    {
        strncpy(m_user, user, sizeof(m_user)-1);
    }

    if (pass && pass[0] != '\0')
    {
        strncpy(m_pass, pass, sizeof(m_pass)-1);
    }

    log_print(HT_LOG_INFO, "%s, url=%s, ip=%s, port=%d, streamid=%s\r\n",
        __FUNCTION__, m_url, m_ip, m_nport, m_streamid);
    
    m_bRunning = TRUE;
    m_tidRx = sys_os_create_thread((void *)srt_rx_thread, this);
    
    return TRUE;
}

BOOL CSrtClient::srt_play()
{
    return TRUE;
}

BOOL CSrtClient::srt_stop()
{
    return srt_close();
}

BOOL CSrtClient::srt_pause()
{
    return FALSE;
}

BOOL CSrtClient::srt_close()
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = NULL;
    m_pVideoCB = NULL;
    m_pNotify = NULL;
    m_pUserdata = NULL;
    sys_os_mutex_leave(m_pMutex);
    
    m_bRunning = FALSE;

    sys_os_wait_thread(&m_tidRx);
    
    if (m_fd)
    {
        ::srt_close(m_fd);
        m_fd = 0;
    }

    if (m_eid)
    {
        srt_epoll_release(m_eid);
        m_eid = 0;
    }

    m_bVideoReady = FALSE;
    m_bAudioReady = FALSE;
    
    return TRUE;
}

BOOL CSrtClient::srt_connect()
{
    srt_send_notify(SRT_EVE_CONNECTING);
    
    SRTSOCKET fd = srt_create_socket();

    if (srt_setsockopt(fd, 0, SRTO_STREAMID, m_streamid, strlen(m_streamid)) < 0) 
    {
        log_print(HT_LOG_ERR, "%s, srt_setsockopt SRTO_STREAMID failure. err=%s\r\n", 
            __FUNCTION__, srt_getlasterror_str());
        ::srt_close(fd);    
        return FALSE;
    }

    int status;
    HT_SOCKADDR addr;
    
    if (!get_address_by_name(m_ip, HT_PROTOCOL_UDP, &addr))
    {
        ::srt_close(fd);
        log_print(HT_LOG_ERR, "%s, invalid ip %s\r\n", __FUNCTION__, m_ip);
        return FALSE;
    }

    if (addr.ipv4_flag)
    {
        addr.ipv4_addr.sin_port = htons(m_nport);
        status = ::srt_connect(fd, (struct sockaddr *)&addr.ipv4_addr, sizeof(struct sockaddr_in));
    }
    else if (addr.ipv6_flag)
    {
        addr.ipv6_addr.sin6_port = htons(m_nport);
        status = ::srt_connect(fd, (struct sockaddr *)&addr.ipv6_addr, sizeof(struct sockaddr_in6));
    }
    else
    {
        ::srt_close(fd);
        log_print(HT_LOG_ERR, "%s, invalid ip %s\r\n", __FUNCTION__, m_ip);
        return FALSE;
    }
    
    if (status == SRT_ERROR) 
    {
        log_print(HT_LOG_ERR, "%s, srt_connect failure. ip=%s, port=%d, %s\r\n", 
            __FUNCTION__, m_ip, m_nport, srt_getlasterror_str());
        ::srt_close(fd);    
        return FALSE;
    }

    int eid = srt_epoll_create();
    if (eid < 0)
    {
        log_print(HT_LOG_ERR, "%s, srt_epoll_create failure. ip=%s, port=%d, %s\r\n", 
            __FUNCTION__, m_ip, m_nport, srt_getlasterror_str());
        ::srt_close(fd);    
        return FALSE;
    }

    int modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    
    int ret = srt_epoll_add_usock(eid, fd, &modes);
    if (ret < 0) 
    {
        log_print(HT_LOG_ERR, "%s, srt_epoll_add_usock failed, epoll=%d, fd=%d, modes=%d, %s\r\n",
            __FUNCTION__, m_eid, fd, modes, srt_getlasterror_str());
        ::srt_close(fd);
        srt_epoll_release(eid);
        return FALSE;
    }

    m_fd = fd;
    m_eid = eid;

    return TRUE;
}

void CSrtClient::rx_thread()
{
    if (!srt_connect())
    {
        srt_send_notify(SRT_EVE_CONNFAIL);
        return;
    }

#define TS_UDP_LEN 1316 // 7*188

    int  tm_count = 0;
    BOOL nodata_notify = FALSE;
    char buff[TS_UDP_LEN];
    
    while (m_bRunning)
    {
        SRTSOCKET read_socks[1];
        int read_len = 1;
        
        int ret = srt_epoll_wait(m_eid, read_socks, &read_len, NULL, NULL, 1000, 0, 0, 0, 0);
        if (ret < 0) 
        {
            ret = srt_getlasterror(NULL);
            if (ret == SRT_ETIMEOUT)
            {
                tm_count++;
                if (tm_count >= m_nRxTimeout && !nodata_notify)    // in 10s without data
                {
                    nodata_notify = TRUE;
                    srt_send_notify(SRT_EVE_NODATA);
                }

                continue;
            }    
            else
            {
                log_print(HT_LOG_ERR, "%s, srt_epoll_wait failed. err=%d\r\n", __FUNCTION__, ret);
                break;
            }
        }
        
        if (read_len <= 0) 
        {
            continue;
        }

        SRT_SOCKSTATUS status = srt_getsockstate(m_fd);
        if (status == SRTS_BROKEN || 
            status == SRTS_NONEXIST || 
            status == SRTS_CLOSED)
        {
            break;
        }

        ret = srt_recvmsg(m_fd, buff, TS_UDP_LEN);
        if (ret < 0) 
        {
            if (SRT_EASYNCRCV != srt_getlasterror(NULL))
            {
                log_print(HT_LOG_WARN, "%s, srt_recvmsg failed, sock=%d, ret=%d, err=%s\r\n",
                    __FUNCTION__, m_fd, ret, srt_getlasterror_str());
                break;
            }
            continue;
        }

        if (nodata_notify)
        {
            nodata_notify = FALSE;
            srt_send_notify(SRT_EVE_RESUME);
        }
            
        tm_count = 0;

        ts_parser_parse(&m_tsParser, (uint8*)buff, ret);
    }
    
    srt_send_notify(SRT_EVE_STOPPED);
}

void CSrtClient::srt_send_notify(int event)
{
    sys_os_mutex_enter(m_pMutex);
    
    if (m_pNotify)
    {
        m_pNotify(event, m_pUserdata);
    }

    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::srt_h264_rx(uint8 * p_data, uint32 len, uint64 pts)
{
    int s_len = 0, n_len = 0, parse_len = len;
    uint8 nalu_t;
    uint8 * p_cur = p_data;
    uint8 * p_end = p_data + len;
    
    while (p_cur && p_cur < p_end && parse_len > 0)
    {
        uint8 * p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
        
        nalu_t = (p_cur[s_len] & 0x1F);

        if (nalu_t == H264_NAL_SPS && m_nSpsLen == 0)    
        {
            if (n_len <= (int)sizeof(m_pSps))
            {
                memcpy(m_pSps, p_cur, n_len);
                m_nSpsLen = n_len;
            }

            video_data_cb(p_cur, n_len, pts);
        }
        else if (nalu_t == H264_NAL_PPS && m_nPpsLen == 0)    
        {
            if (n_len <= (int)sizeof(m_pPps))
            {
                memcpy(m_pPps, p_cur, n_len);
                m_nPpsLen = n_len;
            }

            video_data_cb(p_cur, n_len, pts);
        }
        else if (nalu_t == H264_NAL_AUD || nalu_t == H264_NAL_SEI)
        {
           // skip  
        }
        else
        {
            video_data_cb(p_cur, n_len, pts);
        }
        
        parse_len = p_next ? p_end - p_next : 0;
        p_cur = p_next;
    }
}

void CSrtClient::srt_h265_rx(uint8 * p_data, uint32 len, uint64 pts)
{
    int s_len = 0, n_len = 0, parse_len = len;
    uint8 nalu_t;
    uint8 * p_cur = p_data;
    uint8 * p_end = p_data + len;
    
    while (p_cur && p_cur < p_end && parse_len > 0)
    {
        uint8 * p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
        
        nalu_t = (p_cur[s_len] >> 1) & 0x3F;

        if (nalu_t == HEVC_NAL_VPS && m_nVpsLen == 0)    
        {
            if (n_len <= (int)sizeof(m_pVps))
            {
                memcpy(m_pVps, p_cur, n_len);
                m_nVpsLen = n_len;
            }

            video_data_cb(p_cur, n_len, pts);
        }
        else if (nalu_t == HEVC_NAL_SPS && m_nSpsLen == 0)    
        {
            if (n_len <= (int)sizeof(m_pSps))
            {
                memcpy(m_pSps, p_cur, n_len);
                m_nSpsLen = n_len;
            }

            video_data_cb(p_cur, n_len, pts);
        }
        else if (nalu_t == HEVC_NAL_PPS && m_nPpsLen == 0)    
        {
            if (n_len <= (int)sizeof(m_pPps))
            {
                memcpy(m_pPps, p_cur, n_len);
                m_nPpsLen = n_len;
            }

            video_data_cb(p_cur, n_len, pts);
        }
        else if (nalu_t == HEVC_NAL_AUD || 
            nalu_t == HEVC_NAL_SEI_PREFIX ||
            nalu_t == HEVC_NAL_SEI_SUFFIX)
        {
            // skip
        }
        else
        {
            video_data_cb(p_cur, n_len, pts);
        }
        
        parse_len = p_next ? p_end - p_next : 0;
        p_cur = p_next;
    }
}

BOOL CSrtClient::srt_parse_adts(uint8 * p_data, uint32 len)
{
    bs_t bs;
    bs_init(&bs, p_data, len);
    
    if (bs_read(&bs, 12) != 0xfff)
    {
        return FALSE;
    }

    const int audio_sample_rates[16] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    int sr, ch, size;
    
    bs_skip(&bs, 1);            /* id */
    bs_skip(&bs, 2);            /* layer */
    bs_skip(&bs, 1);            /* protection_absent */
    bs_skip(&bs, 2);            /* profile_objecttype */
    sr = bs_read(&bs, 4);       /* sample_frequency_index */
    if (!audio_sample_rates[sr])
    {
        return FALSE;
    }
    
    bs_skip(&bs, 1);            /* private_bit */
    ch = bs_read(&bs, 3);       /* channel_configuration */

    bs_skip(&bs, 1);            /* original/copy */
    bs_skip(&bs, 1);            /* home */

    /* adts_variable_header */
    bs_skip(&bs, 1);            /* copyright_identification_bit */
    bs_skip(&bs, 1);            /* copyright_identification_start */
    size = bs_read(&bs, 13);    /* aac_frame_length */
    if (size < 7)
    {
        return FALSE;
    }
    
    bs_skip(&bs, 11);           /* adts_buffer_fullness */
    bs_skip(&bs, 2);            /* number_of_raw_data_blocks_in_frame */

    m_nSamplerate = audio_sample_rates[sr];
    m_nChannels = ch;
    
    return TRUE;
}

void CSrtClient::srt_payload_cb(uint8 * p_data, uint32 len, uint32 type, uint64 pts)
{
    if (type == TS_VIDEO_H264)
    {
        if (!m_bVideoReady)
        {
            m_nVideoCodec = VIDEO_CODEC_H264;
            m_bVideoReady = TRUE;
            srt_send_notify(SRT_EVE_VIDEOREADY);
        }

        srt_h264_rx(p_data, len, pts);
    }
    else if (type == TS_VIDEO_HEVC)
    {
        if (!m_bVideoReady)
        {
            m_nVideoCodec = VIDEO_CODEC_H265;
            m_bVideoReady = TRUE;
            srt_send_notify(SRT_EVE_VIDEOREADY);
        }

        srt_h265_rx(p_data, len, pts);
    }
    else if (type == TS_VIDEO_MPEG4)
    {
        if (!m_bVideoReady)
        {
            m_nVideoCodec = VIDEO_CODEC_MP4;
            m_bVideoReady = TRUE;
            srt_send_notify(SRT_EVE_VIDEOREADY);
        }

        video_data_cb(p_data, len, pts);
    }
    else if (type == TS_AUDIO_AAC)
    {
        if (!m_bAudioReady)
        {
            srt_parse_adts(p_data, len);
            
            m_nAudioCodec = AUDIO_CODEC_AAC;
            m_bAudioReady = TRUE;
            srt_send_notify(SRT_EVE_AUDIOREADY);
        }
        
        audio_data_cb(p_data, len, pts);
    }
}

void CSrtClient::video_data_cb(uint8 * p_data, int len, uint32 ts)
{
    if (m_nVideoInitTS == 0 && ts != 0)
    {
        m_nVideoInitTS = ts;
    }

    sys_os_mutex_enter(m_pMutex);
    if (m_pVideoCB)
    {
        m_pVideoCB(p_data, len, ts, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::audio_data_cb(uint8 * p_data, int len, uint32 ts)
{
    if (m_nAudioInitTS == 0 && ts != 0)
    {
        m_nAudioInitTS = ts;
    }
    
    sys_os_mutex_enter(m_pMutex);
    if (m_pAudioCB)
    {
        m_pAudioCB(p_data, len, ts, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::get_h264_params()
{
    if (m_nSpsLen > 0)
    {
        video_data_cb(m_pSps, m_nSpsLen, 0);
    }

    if (m_nPpsLen > 0)
    {
        video_data_cb(m_pPps, m_nPpsLen, 0);
    }
}

BOOL CSrtClient::get_h264_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len)
{
    if (m_nSpsLen > 0)
    {
        *sps_len = m_nSpsLen;
        memcpy(p_sps, m_pSps, m_nSpsLen);
    }

    if (m_nPpsLen > 0)
    {
        *pps_len = m_nPpsLen;
        memcpy(p_pps, m_pPps, m_nPpsLen);
    }

    return TRUE;
}

void CSrtClient::get_h265_params()
{
    if (m_nVpsLen > 0)
    {
        video_data_cb(m_pVps, m_nVpsLen, 0);
    }
    
    if (m_nSpsLen > 0)
    {
        video_data_cb(m_pSps, m_nSpsLen, 0);
    }

    if (m_nPpsLen > 0)
    {
        video_data_cb(m_pPps, m_nPpsLen, 0);
    }
}

BOOL CSrtClient::get_h265_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len, uint8 * p_vps, int * vps_len)
{
    if (m_nVpsLen > 0)
    {
        *vps_len = m_nVpsLen;
        memcpy(p_vps, m_pVps, m_nVpsLen);
    }
    
    if (m_nSpsLen > 0)
    {
        *sps_len = m_nSpsLen;
        memcpy(p_sps, m_pSps, m_nSpsLen);
    }

    if (m_nPpsLen > 0)
    {
        *pps_len = m_nPpsLen;
        memcpy(p_pps, m_pPps, m_nPpsLen);
    }

    return TRUE;
}

void CSrtClient::set_notify_cb(srt_notify_cb notify, void * userdata) 
{
    sys_os_mutex_enter(m_pMutex);    
    m_pNotify = notify;
    m_pUserdata = userdata;
    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::set_video_cb(srt_video_cb cb) 
{
    sys_os_mutex_enter(m_pMutex);
    m_pVideoCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::set_audio_cb(srt_audio_cb cb)
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CSrtClient::set_rx_timeout(int timeout)
{
    m_nRxTimeout = timeout;
}


