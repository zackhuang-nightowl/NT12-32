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
#include "media_pusher.h"
#include "rtsp_cfg.h"
#include "media_format.h"
#include "rtp.h"
#include "h264.h"
#include "h265.h"
#include "h264_util.h"
#include "h265_util.h"
#include "media_util.h"
#include "media_parse.h"
#include "mjpeg.h"
#include "base64.h"
#include <math.h>

#ifdef MEDIA_PUSHER

/***************************************************************************************/

MEDIA_PUSH * g_pusher = NULL;
void * g_pusher_mutex = sys_os_create_mutex();

/***************************************************************************************/

MEDIA_PUSH * media_add_pusher(BOOL fixed)
{
    MEDIA_PUSH * p_tmp;
    MEDIA_PUSH * p_new_push = (MEDIA_PUSH *) malloc(sizeof(MEDIA_PUSH));
    if (NULL == p_new_push)
    {
        return NULL;
    }

    memset(p_new_push, 0, sizeof(MEDIA_PUSH));

    p_new_push->is_fixed = fixed;

    sys_os_mutex_enter(g_pusher_mutex);
    
    p_tmp = g_pusher;
    if (NULL == p_tmp)
    {
        g_pusher = p_new_push;
    }
    else
    {
        while (p_tmp && p_tmp->next)
        {
            p_tmp = p_tmp->next;
        }
        
        p_tmp->next = p_new_push;
    }

    sys_os_mutex_leave(g_pusher_mutex);
    
    return p_new_push;
}

void media_free_pusher(const char * key)
{
    BOOL found = FALSE;
    MEDIA_PUSH * p_prev = NULL;
    MEDIA_PUSH * p_tmp;    

    sys_os_mutex_leave(g_pusher_mutex);

    p_tmp = g_pusher;
    
    while (p_tmp)
    {
        if (strcasecmp(p_tmp->cfg.key, key) == 0)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found && !p_tmp->is_fixed)
    {
        if (NULL == p_prev)
        {
            g_pusher = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        if (p_tmp->pusher)
        {
            p_tmp->pusher->freeInstance(p_tmp->cfg.key);
            p_tmp->pusher = NULL;
        }

        free(p_tmp);
    }

    sys_os_mutex_leave(g_pusher_mutex);
}

void media_free_pushers()
{
    MEDIA_PUSH * p_next;
    MEDIA_PUSH * p_tmp;

    sys_os_mutex_enter(g_pusher_mutex);

    p_tmp = g_pusher;
    
    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        if (p_tmp->pusher)
        {
            p_tmp->pusher->freeInstance(p_tmp->cfg.key);
            p_tmp->pusher = NULL;
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    g_pusher = NULL;

    sys_os_mutex_leave(g_pusher_mutex);
}

BOOL media_init_pusher(MEDIA_PUSH * p_pusher)
{
    BOOL ret = FALSE;

    p_pusher->pusher = CMediaPusher::getInstance(p_pusher->cfg.key, &p_pusher->cfg);
    if (NULL == p_pusher->pusher)
    {
        log_print(HT_LOG_ERR, "%s, new rtsp pusher failed\r\n", __FUNCTION__);
    }

    return ret;
}

void media_init_pushers()
{
    sys_os_mutex_enter(g_pusher_mutex);
    
    MEDIA_PUSH * p_pusher = g_pusher;
    while (p_pusher)
    {
        media_init_pusher(p_pusher);
        
        p_pusher = p_pusher->next;
    }

    sys_os_mutex_leave(g_pusher_mutex);
}

int media_get_pusher_nums()
{
    int nums = 0;
    
    sys_os_mutex_enter(g_pusher_mutex);
    
    MEDIA_PUSH * p_pusher = g_pusher;
    while (p_pusher)
    {
        nums++;
        
        p_pusher = p_pusher->next;
    }

    sys_os_mutex_leave(g_pusher_mutex);
    
    return nums;
}

MEDIA_PUSH * media_pusher_match(const char * key)
{
    sys_os_mutex_enter(g_pusher_mutex);
    
    MEDIA_PUSH * p_pusher = g_pusher;
    while (p_pusher)
    {
        if (strcmp(key, p_pusher->cfg.key) == 0)
        {
            break;
        }
        
        p_pusher = p_pusher->next;
    }

    sys_os_mutex_leave(g_pusher_mutex);
    
    return p_pusher;
}

void * media_pusher_tcp_rx(void * argv)
{
    CMediaPusher * p_pusher = (CMediaPusher *)argv;

    p_pusher->tcpRecvThread();

    return NULL;
}

void * media_pusher_udp_rx(void * argv)
{
    CMediaPusher * p_pusher = (CMediaPusher *)argv;

    p_pusher->udpRecvThread();

    return NULL;
}

int video_data_rx(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CMediaPusher * p_pusher = (CMediaPusher *)p_userdata;

    p_pusher->onVideo(p_data, len, ts, seq);

    return 0;
}

int audio_data_rx(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CMediaPusher * p_pusher = (CMediaPusher *)p_userdata;

    p_pusher->onAudio(p_data, len, ts, seq);

    return 0;
}

#ifdef RTSP_METADATA

int meta_data_rx(uint8 * p_data, int len, uint32 ts, uint32 seq, void * p_userdata)
{
    CMediaPusher * p_pusher = (CMediaPusher *)p_userdata;

    p_pusher->onMetadata(p_data, len, ts, seq);

    return 0;
}

#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

void media_pusher_vdecoder_cb(AVFrame * frame, void *pUserdata)
{
    CMediaPusher * pPusher = (CMediaPusher *)pUserdata;

    pPusher->videoDataEncode(frame);
}

void media_pusher_vencoder_cb(uint8 *data, int size, int waitnext, void *pUserdata)
{
    CMediaPusher * pPusher = (CMediaPusher *)pUserdata;

    pPusher->dataCallback(data, size, DATA_TYPE_VIDEO);
}

void media_pusher_adecoder_cb(AVFrame * frame, void *pUserdata)
{
    CMediaPusher * pPusher = (CMediaPusher *)pUserdata;

    pPusher->audioDataEncode(frame);
}

void media_pusher_aencoder_cb(uint8 *data, int size, int nbsamples, void *pUserdata)
{
    CMediaPusher * pPusher = (CMediaPusher *)pUserdata;

    pPusher->dataCallback(data, size, DATA_TYPE_AUDIO);
}

#endif

/***************************************************************************************/
void * CMediaPusher::m_pInstMutex = sys_os_create_mutex();
std::map<std::string,CMediaPusher*> CMediaPusher::m_insts;

/***************************************************************************************/

CMediaPusher * CMediaPusher::getInstance(char * key, PUSHER_CFG * pConfig)
{
    CMediaPusher * p_pusher = NULL;
    std::map<std::string,CMediaPusher*>::iterator it;
    
    sys_os_mutex_enter(m_pInstMutex);

    it = m_insts.find(key);
    
    if (it == m_insts.end())
    {
        p_pusher = new CMediaPusher(pConfig);

        p_pusher->m_nRefCnt++;
        
        m_insts[key] = p_pusher;
    }
    else
    {
        p_pusher = it->second;
        p_pusher->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return p_pusher;
}

void CMediaPusher::freeInstance(char * key)
{
    CMediaPusher * p_pusher = NULL;
    
    sys_os_mutex_enter(m_pInstMutex);

    std::map<std::string,CMediaPusher*>::iterator it = m_insts.find(key);
    
    if (it != m_insts.end())
    {
        p_pusher = it->second;
        p_pusher->m_nRefCnt--;

        if (p_pusher->m_nRefCnt <= 0)
        {
            delete p_pusher;
            
            m_insts.erase(it);
        }
    }

    sys_os_mutex_leave(m_pInstMutex);
}

/***************************************************************************************/

CMediaPusher::CMediaPusher(PUSHER_CFG * pConfig)
: m_nRefCnt(0)
, m_bInited(FALSE)
, m_rua(NULL)
, m_bRecving(FALSE)
, m_tidRecv(0)
, m_pConfig(new PUSHER_CFG(*pConfig))
, m_pVideoExtra(NULL)
, m_nVideoExtraSize(0)
, m_pAudioExtra(NULL)
, m_nAudioExtraSize(0)
{
    memset(&m_vua, 0, sizeof(m_vua));
    memset(&m_aua, 0, sizeof(m_aua));
    
    h265_rxi_init(&m_vua.h265rxi, video_data_rx, this);
    aac_rxi_init(&m_aua.aacrxi, audio_data_rx, this);

    m_aua.aacrxi.size_length = 13;
    m_aua.aacrxi.index_length = 3;
    m_aua.aacrxi.index_delta_length = 3;

    m_pRuaMutex = sys_os_create_mutex();
    
    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);

    m_nVideoRecodec = 0;
    m_nAudioRecodec = 0;
    
#ifdef RTSP_METADATA
    memset(&m_mrxi, 0, sizeof(PCMRXI));
    pcm_rxi_init(&m_mrxi, meta_data_rx, this);
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    m_pVideoDecoder = NULL;
    m_pAudioDecoder = NULL;
    m_pVideoEncoder = NULL;
    m_pAudioEncoder = NULL;
#endif

    initPusher();
}

CMediaPusher::~CMediaPusher(void)
{
    m_bRecving = FALSE;

    sys_os_wait_thread(&m_tidRecv);
    
    if (m_vua.sfd > 0)
    {
        closesocket(m_vua.sfd);
        m_vua.sfd = 0;
    }

    if (m_aua.sfd > 0)
    {
        closesocket(m_aua.sfd);
        m_aua.sfd = 0;
    }

    closeMedia(&m_vua);
    closeMedia(&m_aua);

#ifdef RTSP_METADATA
    pcm_rxi_deinit(&m_mrxi);
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (m_pVideoDecoder)
    {
        delete m_pVideoDecoder;
        m_pVideoDecoder = NULL;
    }

    if (m_pAudioDecoder)
    {
        delete m_pAudioDecoder;
        m_pAudioDecoder = NULL;
    }

    if (m_pVideoEncoder)
    {
        delete m_pVideoEncoder;
        m_pVideoEncoder = NULL;
    }

    if (m_pAudioEncoder)
    {
        delete m_pAudioEncoder;
        m_pAudioEncoder = NULL;
    }
#endif

    if (m_pConfig)
    {
        delete m_pConfig;
        m_pConfig = NULL;
    }

    if (m_pVideoExtra)
    {
        free(m_pVideoExtra);
        m_pVideoExtra = NULL;
    }

    if (m_pAudioExtra)
    {
        free(m_pAudioExtra);
        m_pAudioExtra = NULL;
    }
    
    m_nVideoExtraSize = 0;
    m_nAudioExtraSize = 0;
    
    hlist_free_container(m_pCallbackList);
    
    sys_os_destroy_mutex(m_pCallbackMutex);
    sys_os_destroy_mutex(m_pRuaMutex);
}

void CMediaPusher::initPusher()
{
    if (m_pConfig->input.has_video)
    {
        if (m_pConfig->transfer.mode != TRANSFER_MODE_RTSP)
        {
            if (m_pConfig->transfer.vport <= 0 || m_pConfig->transfer.vport > 65535)
            {
                return;
            }
            
            m_vua.sfd = initSocket(m_pConfig->transfer.ip, m_pConfig->transfer.vport, m_pConfig->transfer.mode, &m_vua.family);
            if (m_vua.sfd <= 0)
            {
                return;
            }
        }
    }
    
    if (m_pConfig->input.has_audio)
    {
        if (m_pConfig->transfer.mode != TRANSFER_MODE_RTSP)
        {
            if (m_pConfig->transfer.aport <= 0 || m_pConfig->transfer.aport > 65535)
            {
                return;
            }
            
            m_aua.sfd = initSocket(m_pConfig->transfer.ip, m_pConfig->transfer.aport, m_pConfig->transfer.mode, &m_aua.family);
            if (m_aua.sfd <= 0)
            {
                return;
            }
        }
    }

    m_bInited = startPusher();
}

int CMediaPusher::initSocket(const char * ip, int port, int mode, int *family)
{
    HT_SOCKADDR sockaddr;
    struct sockaddr * addr;
    socklen_t addrlen;
    SOCKET fd = -1;

    if (ip[0] != '\0')
    {
        HT_PROTOCOL proto;
        
        if (mode == TRANSFER_MODE_TCP)
        {
            proto = HT_PROTOCOL_TCP;
        }
        else
        {
            proto = HT_PROTOCOL_UDP;
        }
        
        if (!get_address_by_name(ip, proto, &sockaddr))
        {
            log_print(HT_LOG_ERR, "%s, invalid ip: %s\r\n", __FUNCTION__, ip);
            return -1;
        }

        if (sockaddr.ipv6_flag)
        {
            addr = (struct sockaddr *) &sockaddr.ipv6_addr;
            addrlen = sizeof(struct sockaddr_in6);
        }
        else
        {
            addr = (struct sockaddr *) &sockaddr.ipv4_addr;
            addrlen = sizeof(struct sockaddr_in);
        }
    }
    else
    {
        sockaddr.ipv4_addr.sin_family = AF_INET;
        
        if (!get_default_if_ip((struct sockaddr *) &sockaddr.ipv4_addr))
        {
            return -1;
        }

        addr = (struct sockaddr *) &sockaddr.ipv4_addr;
        addrlen = sizeof(struct sockaddr_in);
    }

    if (mode == TRANSFER_MODE_TCP)
    {
        fd = socket(addr->sa_family, SOCK_STREAM, 0);
    }
    else if (mode == TRANSFER_MODE_UDP)
    {
        fd = socket(addr->sa_family, SOCK_DGRAM, 0);
    }
    
    if (fd <= 0)
    {
        return -1;
    }

    int len = 2 * 1024 * 1024;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!!!\r\n", __FUNCTION__);
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!!!\r\n", __FUNCTION__);
    }

    sockaddr.ipv4_addr.sin_port = htons(port);
    sockaddr.ipv6_addr.sin6_port = htons(port);

    if (bind(fd, addr, addrlen) < 0)
    {
        log_print(HT_LOG_ERR, "%s, bind errno=%d!!!\r\n", __FUNCTION__, errno);
        closesocket(fd);
        return -1;
    }

    if (mode == TRANSFER_MODE_TCP)
    {
        if (listen(fd, 10) < 0)
        {
            log_print(HT_LOG_ERR, "%s, listen errno=%d!!!\r\n", __FUNCTION__, errno);
            closesocket(fd);
            return -1;
        }
    }

    if (family)
    {
        *family = addr->sa_family;
    }

    return fd;
}

void CMediaPusher::reinitMedia()
{
    closeMedia(&m_vua);
    closeMedia(&m_aua);

    h265_rxi_init(&m_vua.h265rxi, video_data_rx, this);
    aac_rxi_init(&m_aua.aacrxi, audio_data_rx, this);

    m_aua.aacrxi.size_length = 13;
    m_aua.aacrxi.index_length = 3;
    m_aua.aacrxi.index_delta_length = 3;

#ifdef RTSP_METADATA
    pcm_rxi_deinit(&m_mrxi);
    pcm_rxi_init(&m_mrxi, meta_data_rx, this);
#endif    
}

BOOL CMediaPusher::startPusher()
{
    m_bRecving = TRUE;

    if (m_pConfig->transfer.mode == TRANSFER_MODE_TCP)
    {
        m_tidRecv = sys_os_create_thread((void *)media_pusher_tcp_rx, this);
    }
    else if (m_pConfig->transfer.mode == TRANSFER_MODE_UDP)
    {
        m_tidRecv = sys_os_create_thread((void *)media_pusher_udp_rx, this);
    }
    
    return TRUE;
}

void CMediaPusher::tcpRecvThread()
{   
    fd_set fdr;
    int max_fd = 0;

    log_print(HT_LOG_DBG, "%s, start\r\n", __FUNCTION__);

    while (m_bRecving)
    {
        FD_ZERO(&fdr);

        if (m_vua.sfd > 0)
        {
            FD_SET(m_vua.sfd, &fdr);            
            max_fd = (m_vua.sfd > max_fd ? m_vua.sfd : max_fd);        
        }        

        if (m_aua.sfd > 0)
        {
            FD_SET(m_aua.sfd, &fdr);
            max_fd = (m_aua.sfd > max_fd ? m_aua.sfd : max_fd);            
        }        

        if (m_vua.mfd > 0)
        {
            FD_SET(m_vua.mfd, &fdr);
            max_fd = (m_vua.mfd > max_fd ? m_vua.mfd : max_fd);        
        }        

        if (m_aua.mfd > 0)
        {
            FD_SET(m_aua.mfd, &fdr);            
            max_fd = (m_aua.mfd > max_fd ? m_aua.mfd : max_fd);            
        }

        if (0 == max_fd)
        {
            // No sockets needs to receive data 
            break;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int sret = select(max_fd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            log_print(HT_LOG_ERR, "%s, select err[%s], max fd[%d], sret[%d]!!!\r\n", 
                __FUNCTION__, sys_os_get_socket_error(), max_fd, sret);

            usleep(10*1000);                
            continue;
        }

        if (FD_ISSET(m_vua.sfd, &fdr))
        {
            tcpListenRx(&m_vua);
        }

        if (FD_ISSET(m_aua.sfd, &fdr))
        {
            tcpListenRx(&m_aua);
        }
        
        if (FD_ISSET(m_vua.mfd, &fdr))
        {
            if (!tcpDataRx(&m_vua, MEDIA_TYPE_VIDEO))
            {
                closeMedia(&m_vua);
                
                h265_rxi_init(&m_vua.h265rxi, video_data_rx, this);
            }
        }

        if (FD_ISSET(m_aua.mfd, &fdr))
        {
            if (!tcpDataRx(&m_aua, MEDIA_TYPE_AUDIO))
            {
                closeMedia(&m_aua);
                
                aac_rxi_init(&m_aua.aacrxi, audio_data_rx, this);

                m_aua.aacrxi.size_length = 13;
                m_aua.aacrxi.index_length = 3;
                m_aua.aacrxi.index_delta_length = 3;
            }
        }
    }
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void CMediaPusher::tcpListenRx(PUA * p_ua)
{
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;

    if (p_ua->family == AF_INET6)
    {
        addr = (struct sockaddr *) &addr6;
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        addr = (struct sockaddr *) &addr4;
        addrlen = sizeof(struct sockaddr_in);
    }
    
    int cfd = accept(p_ua->sfd, addr, &addrlen);
    if (cfd < 0)
    {
        log_print(HT_LOG_ERR, "%s, accept ret error[%d]!!!\r\n", __FUNCTION__, errno);
        return;
    }

    int on = 1;
    int len = 2 * 1024 * 1024;
    
    if (setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt TCP_NODELAY error[%d]\r\n", __FUNCTION__, errno);
    }

    if (setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error[%d]\r\n", __FUNCTION__, errno);
    }

    if (setsockopt(cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error[%d]\r\n", __FUNCTION__, errno);
    }

    if (p_ua->mfd == 0)
    {
        p_ua->mfd = cfd;
    }
    else
    {
        closesocket(cfd);
    }
}

BOOL CMediaPusher::tcpDataRx(PUA * p_ua, int type)
{
    RILF * pkt = NULL;
    int len = tcpPacketRx(p_ua, &pkt);
    if (len < 0)
    {
        return FALSE;
    }
    
    if (len > 0 && pkt)
    {
        uint8 * p_data = (uint8 *)pkt + sizeof(RILF);
        int rlen = len - sizeof(RILF);

        if (type == MEDIA_TYPE_VIDEO)
        {
            videoRtpRx(p_ua, p_data, rlen);
        }
        else
        {
            audioRtpRx(p_ua, p_data, rlen);
        }
        
        free(pkt);
    }

    return TRUE;
}

BOOL CMediaPusher::videoRtpRx(PUA * p_ua, uint8 * p_rtp, int rlen)
{
    int ret = -1;

    if (m_pConfig->input.video.codec == VIDEO_CODEC_H264)
    {
        ret = h264_rtp_rx(&p_ua->h264rxi, p_rtp, rlen);
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_H265)
    {
        ret = h265_rtp_rx(&p_ua->h265rxi, p_rtp, rlen);
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_MP4)
    {
        ret = mpeg4_rtp_rx(&p_ua->mpeg4rxi, p_rtp, rlen);
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_JPEG)
    {
        ret = mjpeg_rtp_rx(&p_ua->mjpegrxi, p_rtp, rlen);
    }
    else
    {
        return FALSE;
    }

    return ret;
}

BOOL CMediaPusher::audioRtpRx(PUA * p_ua, uint8 * p_rtp, int rlen)
{
    int ret = -1;

    if (m_pConfig->input.audio.codec == AUDIO_CODEC_G711A ||
        m_pConfig->input.audio.codec == AUDIO_CODEC_G711U ||
        m_pConfig->input.audio.codec == AUDIO_CODEC_G726 ||
        m_pConfig->input.audio.codec == AUDIO_CODEC_G722 ||
        m_pConfig->input.audio.codec == AUDIO_CODEC_OPUS)
    {
        ret = pcm_rtp_rx(&p_ua->pcmrxi, p_rtp, rlen);
    }
    else if (m_pConfig->input.audio.codec == AUDIO_CODEC_AAC)
    {
        ret = aac_rtp_rx(&p_ua->aacrxi, p_rtp, rlen);
    }
    else
    {
        return FALSE;
    }

    return ret;
}

int CMediaPusher::tcpPacketRx(PUA * p_ua, RILF ** p_pkt)
{
    BOOL finished = FALSE;
    uint32 offset = p_ua->recv_offset;
    
    if (offset < sizeof(RILF))
    {
        // The header information has not been received
        int len = recv(p_ua->mfd, p_ua->recv_buf + offset, sizeof(RILF) - offset, 0);
        if (len <= 0)
        {
            log_print(HT_LOG_ERR, "%s, recv ret=%d, offset=%d\r\n", 
                __FUNCTION__, len, offset);
            return -1;
        }

        p_ua->recv_offset += len;

        if (p_ua->recv_offset == sizeof(RILF))
        {
            RILF * hdr = (RILF *)p_ua->recv_buf;
            int tlen = ntohs(hdr->rtp_len) + sizeof(RILF);
            if (tlen > MAX_RTP_LEN)
            {
                log_print(HT_LOG_ERR, "%s, len[%d] > %d !!!\r\n", 
                    __FUNCTION__, tlen, MAX_RTP_LEN);
                return -1;
            }

            // Allocate dynamic receive buffer 
            p_ua->dyn_recv_buf = (char *)malloc(tlen);
            if (p_ua->dyn_recv_buf == NULL)
            {
                log_print(HT_LOG_ERR, "%s, dyn_recv_buf len = %d memory failed!!!\r\n", 
                    __FUNCTION__, tlen);
                return -1;
            }

            // Copy the header information to the dynamic buffer, 
            //  and the offset remains unchanged.
            memcpy(p_ua->dyn_recv_buf, p_ua->recv_buf, sizeof(RILF));
            
            if (hdr->rtp_len == 0)
            {
                finished = TRUE;
            }
        }
        else if (p_ua->recv_offset > sizeof(RILF))
        {
            log_print(HT_LOG_ERR, "%s, recv_offset[%u],offset[%u],len[%u]!!!\r\n", 
                __FUNCTION__, p_ua->recv_offset, offset, len);
        }
    }
    else
    {
        RILF * hdr = (RILF *)p_ua->dyn_recv_buf;
        if (hdr == NULL)
        {
            log_print(HT_LOG_ERR, "%s, recv_offset[%u],dyn_recv_buf is null!!!\r\n", 
                __FUNCTION__, p_ua->recv_offset);
            return -1;
        }

        uint32 tlen = ntohs(hdr->rtp_len) + sizeof(RILF);
        if (tlen > MAX_RTP_LEN)
        {
            log_print(HT_LOG_ERR, "%s, len[%d] > %d !!!\r\n", 
                __FUNCTION__, tlen, MAX_RTP_LEN);
            return -1;
        }

        if (tlen > p_ua->recv_offset)
        {
            int len = recv(p_ua->mfd, p_ua->dyn_recv_buf + p_ua->recv_offset, tlen - p_ua->recv_offset, 0);
            if (len <= 0)
            {
                log_print(HT_LOG_ERR, "%s, recv ret %d, offset=%d\r\n", 
                    __FUNCTION__, len, p_ua->recv_offset);
                return -1;
            }

            p_ua->recv_offset += len;
        }

        if (p_ua->recv_offset >= tlen)
        {
            finished = TRUE;
        }    
    }

    if (finished == TRUE)
    {
        RILF * hdr = (RILF *)p_ua->dyn_recv_buf;
        int tlen = ntohs(hdr->rtp_len) + sizeof(RILF);

        // A message has been received

        p_ua->recv_offset = 0;
        p_ua->dyn_recv_buf = NULL;
        
        *p_pkt = hdr;

        return tlen;
    }

    return 0;
}

void CMediaPusher::udpRecvThread()
{
    fd_set fdr;
    int max_fd = 0;

    log_print(HT_LOG_DBG, "%s, start\r\n", __FUNCTION__);

    while (m_bRecving)
    {
        FD_ZERO(&fdr);

        if (m_vua.sfd > 0)
        {
            FD_SET(m_vua.sfd, &fdr);
            max_fd = (m_vua.sfd > max_fd ? m_vua.sfd : max_fd);    
        }        

        if (m_aua.sfd > 0)
        {
            FD_SET(m_aua.sfd, &fdr);
            max_fd = (m_aua.sfd > max_fd ? m_aua.sfd : max_fd);            
        }

        if (0 == max_fd)
        {
            // No sockets needs to receive data 
            break;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int sret = select(max_fd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            log_print(HT_LOG_ERR, "%s, select err[%s], max fd[%d], sret[%d]!!!\r\n", 
                __FUNCTION__, sys_os_get_socket_error(), max_fd, sret);

            usleep(10*1000);                
            continue;
        }

        if (FD_ISSET(m_vua.sfd, &fdr))
        {
            if (!udpDataRx(&m_vua, MEDIA_TYPE_VIDEO))
            {
                closeMedia(&m_vua);

                h265_rxi_init(&m_vua.h265rxi, video_data_rx, this);    
            }
            else 
            {
                sys_os_mutex_enter(m_pRuaMutex);
                if (m_rua)
                {
                    RSSUA * p_rua = (RSSUA *) m_rua;
                    p_rua->last_rx_time = sys_os_get_uptime();
                }
                sys_os_mutex_leave(m_pRuaMutex);
            }
        }

        if (FD_ISSET(m_aua.sfd, &fdr))
        {
            if (!udpDataRx(&m_aua, MEDIA_TYPE_AUDIO))
            {
                closeMedia(&m_aua);

                aac_rxi_init(&m_aua.aacrxi, audio_data_rx, this);

                m_aua.aacrxi.size_length = 13;
                m_aua.aacrxi.index_length = 3;
                m_aua.aacrxi.index_delta_length = 3;                
            }
            else
            {
                sys_os_mutex_enter(m_pRuaMutex);
                if (m_rua)
                {
                    RSSUA * p_rua = (RSSUA *) m_rua;
                    p_rua->last_rx_time = sys_os_get_uptime();
                }
                sys_os_mutex_leave(m_pRuaMutex);
            }
        }
    }
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

BOOL CMediaPusher::udpDataRx(PUA * p_ua, int type)
{
    int buf_len = 0;
    char buf_tmp[2048];
    char * p_buf = NULL;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;

    if (p_ua->sfd <= 0)
    {
        return FALSE;
    }

    p_buf = buf_tmp + 32;
    buf_len = sizeof(buf_tmp) - 32;
    
    if (p_ua->family == AF_INET6)
    {
        addr = (struct sockaddr *) &addr6;
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        addr = (struct sockaddr *) &addr4;
        addrlen = sizeof(struct sockaddr_in);
    }
    
    int rlen = recvfrom(p_ua->sfd, p_buf, buf_len, 0, addr, &addrlen);
    if (rlen <= 0)
    {
        log_print(HT_LOG_ERR, "%s, ret=%d, err=[%s]!!!\r\n", 
            __FUNCTION__, rlen, sys_os_get_socket_error());
        return FALSE;
    }

    if (type == MEDIA_TYPE_VIDEO)
    {
        videoRtpRx(p_ua, (uint8*)p_buf, rlen);
    }    
    else if(type == MEDIA_TYPE_AUDIO)
    {
        audioRtpRx(p_ua, (uint8*)p_buf, rlen);
    }
    
    return TRUE;
}

void CMediaPusher::closeMedia(PUA * p_ua)
{
    if (p_ua->mfd)
    {
        closesocket(p_ua->mfd);
        p_ua->mfd = 0;
    }

    if (p_ua->dyn_recv_buf)
    {
        free(p_ua->dyn_recv_buf);
        p_ua->dyn_recv_buf = NULL;
    }

    p_ua->recv_offset = 0;

    h265_rxi_deinit(&p_ua->h265rxi);
}

void CMediaPusher::onVideo(uint8 * pdata, int len, uint32 ts, uint16 seq)
{
    if (m_pConfig->input.video.width == 0 || m_pConfig->input.video.height == 0)
    {
        avc_parse_video_size(m_pConfig->input.video.codec, pdata, len, 
            &m_pConfig->input.video.width, &m_pConfig->input.video.height);
    }

    int recodec = needVideoRecodec(pdata, len);
    
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (1 == recodec)       // need recodec
    {
        videoDataDecode(pdata, len);
    }
    else
#endif
    if (-1 == recodec)      // not need recodec
    {
        dataCallback(pdata, len, DATA_TYPE_VIDEO);
    }
    else if (0 == recodec)  // still not sure
    {
    }
}

void CMediaPusher::onAudio(uint8 * pdata, int len, uint32 ts, uint16 seq)
{
    int recodec = needAudioRecodec();
    
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (1 == recodec)       // need recodec
    {
        audioDataDecode(pdata, len);
    }
    else
#endif
    if (-1 == recodec)      // not need recodec
    {
        dataCallback(pdata, len, DATA_TYPE_AUDIO);
    }
    else if (0 == recodec)  // still not sure
    {
    }
}

void CMediaPusher::onMetadata(uint8 * pdata, int len, uint32 ts, uint16 seq)
{
    dataCallback(pdata, len, DATA_TYPE_METADATA);
}

void CMediaPusher::setVideoInfo(VIDEO_INFO * p_info)
{
    if (p_info)
    {
        m_pConfig->input.has_video = 1;
        memcpy(&m_pConfig->input.video, p_info, sizeof(VIDEO_INFO));
    }
    else
    {
        m_pConfig->input.has_video = 0;
    }
}

void CMediaPusher::setAudioInfo(AUDIO_INFO * p_info)
{
    if (p_info)
    {
        m_pConfig->input.has_audio = 1;
        memcpy(&m_pConfig->input.audio, p_info, sizeof(AUDIO_INFO));
    }
    else
    {
        m_pConfig->input.has_audio = 0;
    }
}

void CMediaPusher::setVideoExtra(uint8 * extra, int extra_size)
{
    if (m_pVideoExtra)
    {
        free(m_pVideoExtra);
        m_pVideoExtra = NULL;
    }

    if (extra && extra_size > 0)
    {
        m_pVideoExtra = (uint8 *)malloc(extra_size);
        if (m_pVideoExtra)
        {
            memcpy(m_pVideoExtra, extra, extra_size);
        }
    }
    
    m_nVideoExtraSize = extra_size;
}

void CMediaPusher::setAudioExtra(uint8 * extra, int extra_size)
{
    if (m_pAudioExtra)
    {
        free(m_pAudioExtra);
        m_pAudioExtra = NULL;
    }

    if (extra && extra_size > 0)
    {
        m_pAudioExtra = (uint8 *)malloc(extra_size);
        if (m_pAudioExtra)
        {
            memcpy(m_pAudioExtra, extra, extra_size);
        }
    }
    
    m_nAudioExtraSize = extra_size;
}

BOOL CMediaPusher::isCallbackExist(PusherDataCB pCallback, void * pUserdata)
{
    BOOL exist = FALSE;
    PusherCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (PusherCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {
            exist = TRUE;
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);
    
    sys_os_mutex_leave(m_pCallbackMutex);

    return exist;
}

void CMediaPusher::addCallback(PusherDataCB pCallback, void * pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    PusherCB * p_cb = (PusherCB *) malloc(sizeof(PusherCB));
    if (NULL == p_cb)
    {
        return;
    }

    memset(p_cb, 0, sizeof(PusherCB));
    
    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bVFirst = TRUE;
    p_cb->bAFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CMediaPusher::delCallback(PusherDataCB pCallback, void * pUserdata)
{
    PusherCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (PusherCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {        
            free(p_cb);
            
            hlist_remove(m_pCallbackList, p_node);
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

void CMediaPusher::dataCallback(uint8 * data, int size, int type)
{
    PusherCB * p_cb = NULL;
    LKNODE * p_node = NULL;
        
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (PusherCB *) p_node->data;
        if (p_cb->pCallback != NULL)
        {
            if (type == DATA_TYPE_VIDEO)
            {
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)            
                if (NULL == m_pVideoEncoder)
#endif
                {
                    sendExtraData(p_cb, data, size, type);
                }
            }

            p_cb->pCallback(data, size, type, p_cb->pUserdata);
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

BOOL CMediaPusher::getExtraData(uint8 * extra, int * extra_len)
{
    BOOL ret = FALSE;
    
    if (NULL == extra_len)
    {
        return FALSE;
    }

    if (m_pVideoExtra && m_nVideoExtraSize > 0)
    {
        if (*extra_len >= m_nVideoExtraSize)
        {
            if (extra)
            {
                memcpy(extra, m_pVideoExtra, m_nVideoExtraSize);
            }
        }

        *extra_len = m_nVideoExtraSize;
        ret = TRUE;
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_H264)
    {
        if (m_vua.h264rxi.param_sets.sps_len > 0 && m_vua.h264rxi.param_sets.pps_len > 0)
        {
            int len = 0;
            
            if (*extra_len >= len + m_vua.h264rxi.param_sets.sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, m_vua.h264rxi.param_sets.sps, m_vua.h264rxi.param_sets.sps_len);
                }
                len += m_vua.h264rxi.param_sets.sps_len;
            }

            if (*extra_len >= len + m_vua.h264rxi.param_sets.pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, m_vua.h264rxi.param_sets.pps, m_vua.h264rxi.param_sets.pps_len);
                }
                len += m_vua.h264rxi.param_sets.pps_len;
            }
            
            *extra_len = len;
            ret = TRUE;
        }
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_H265)
    {
        if (m_vua.h265rxi.param_sets.vps_len > 0 && 
            m_vua.h265rxi.param_sets.sps_len > 0 && 
            m_vua.h265rxi.param_sets.pps_len > 0)
        {
            int len = 0;

            if (*extra_len >= len + m_vua.h265rxi.param_sets.vps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, m_vua.h265rxi.param_sets.vps, m_vua.h265rxi.param_sets.vps_len);
                }
                len += m_vua.h265rxi.param_sets.vps_len;
            }
            
            if (*extra_len >= len + m_vua.h265rxi.param_sets.sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, m_vua.h265rxi.param_sets.sps, m_vua.h265rxi.param_sets.sps_len);
                }
                len += m_vua.h265rxi.param_sets.sps_len;
            }

            if (*extra_len >= len + m_vua.h265rxi.param_sets.pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, m_vua.h265rxi.param_sets.pps, m_vua.h265rxi.param_sets.pps_len);
                }
                len += m_vua.h265rxi.param_sets.pps_len;
            }
            
            *extra_len = len;
            ret = TRUE;
        }
    }

    if (!ret)
    {
        *extra_len = 0;
    }
    
    return ret;
}

void CMediaPusher::sendExtraData(PusherCB * p_cb, uint8 * data, int size, int type)
{
    if (m_pConfig->input.video.codec == VIDEO_CODEC_H264)
    {
        uint8 naltype = avc_h264_nalu_type(data, size);

        if (H264_NAL_SPS == naltype || H264_NAL_PPS == naltype)
        {
        }
        else if (H264_NAL_IDR != p_cb->nPreNalu && (H264_NAL_IDR == naltype || p_cb->bVFirst))
        {
            uint8 extra[1024];
            int extra_len = sizeof(extra);

            if (getExtraData(extra, &extra_len) && extra_len > 0)
            {
                p_cb->pCallback(extra, extra_len, type, p_cb->pUserdata);
            }
        }

        p_cb->nPreNalu = naltype;
        p_cb->bVFirst = FALSE;
    }
    else if (m_pConfig->input.video.codec == VIDEO_CODEC_H265)
    {
        uint8 naltype = avc_h265_nalu_type(data, size);

        if (HEVC_NAL_VPS == naltype || HEVC_NAL_SPS == naltype || HEVC_NAL_PPS == naltype)
        {
        }
        else if (!IS_IRAP_NAL(p_cb->nPreNalu) && (IS_IRAP_NAL(naltype) || p_cb->bVFirst))
        {
            uint8 extra[1024];
            int extra_len = sizeof(extra);

            if (getExtraData(extra, &extra_len) && extra_len > 0)
            {
                p_cb->pCallback(extra, extra_len, type, p_cb->pUserdata);
            }
        }

        p_cb->nPreNalu = naltype;
        p_cb->bVFirst = FALSE;
    }
}

void CMediaPusher::getH264AuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_rua)
    {
        char sdp[1024] = {'\0'};
        
        if (rsua_get_sdp_h264_desc((RSSUA *)m_rua, NULL, sdp, sizeof(sdp)))
        {
            snprintf(buff, size, "a=fmtp:%d %s", rtp_pt, sdp);
        }
    }
    else
    {
        uint32 sps_size = m_vua.h264rxi.param_sets.sps_len;
        uint32 pps_size = m_vua.h264rxi.param_sets.pps_len;
        
        if (sps_size < 4 || pps_size < 4)
        {
            return;
        }

        uint8 * sps = m_vua.h264rxi.param_sets.sps;
        uint8 * pps = m_vua.h264rxi.param_sets.pps;

        if (sps[0] == 0 && sps[1] == 0 && sps[2] == 0 && sps[3] == 1)
        {
            sps += 4;
            sps_size -= 4;
        }

        if (pps[0] == 0 && pps[1] == 0 && pps[2] == 0 && pps[3] == 1)
        {
            pps += 4;
            pps_size -= 4;
        }

        ht_gen_h264_sdp_line(buff, size, rtp_pt, sps, sps_size, pps, pps_size);
    }
}

void CMediaPusher::getH265AuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_rua)
    {
        char sdp[1024] = {'\0'};
        
        if (rsua_get_sdp_h265_desc((RSSUA *)m_rua, NULL, sdp, sizeof(sdp)))
        {
            snprintf(buff, size, "a=fmtp:%d %s", rtp_pt, sdp);
        }
    }
    else
    {
        uint32 sps_size = m_vua.h265rxi.param_sets.sps_len;
        uint32 pps_size = m_vua.h265rxi.param_sets.pps_len;
        uint32 vps_size = m_vua.h265rxi.param_sets.vps_len;

        if (vps_size < 4 || sps_size < 4 || pps_size < 4)
        {
            return;
        }

        uint8 * sps = m_vua.h265rxi.param_sets.sps;
        uint8 * pps = m_vua.h265rxi.param_sets.pps;
        uint8 * vps = m_vua.h265rxi.param_sets.vps;

        if (sps[0] == 0 && sps[1] == 0 && sps[2] == 0 && sps[3] == 1)
        {
            sps += 4;
            sps_size -= 4;
        }

        if (pps[0] == 0 && pps[1] == 0 && pps[2] == 0 && pps[3] == 1)
        {
            pps += 4;
            pps_size -= 4;
        }

        if (vps[0] == 0 && vps[1] == 0 && vps[2] == 0 && vps[3] == 1)
        {
            vps += 4;
            vps_size -= 4;
        }

        ht_gen_h265_sdp_line(buff, size, rtp_pt, sps, sps_size, pps, pps_size, vps, vps_size);
    }
}

void CMediaPusher::getMP4AuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_rua)
    {
        char sdp[1024] = {'\0'};
        
        if (rsua_get_sdp_mp4_desc((RSSUA *)m_rua, NULL, sdp, sizeof(sdp)))
        {
            snprintf(buff, size, "a=fmtp:%d %s", rtp_pt, sdp);
        }
    }
}

void CMediaPusher::getAACAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_rua)
    {
        char sdp[1024] = {'\0'};
        
        if (rsua_get_sdp_aac_desc((RSSUA *)m_rua, NULL, sdp, sizeof(sdp)))
        {
            snprintf(buff, size, "a=fmtp:%d %s", rtp_pt, sdp);
        }
    }
    else
    {
        snprintf(buff, size, 
            "a=fmtp:%d streamtype=5;profile-level-id=1;mode=AAC-hbr;"
            "sizelength=13;indexlength=3;indexdeltalength=3", 
            rtp_pt);
    }
}

void CMediaPusher::getVideoAuxSDPLine(char * buff, int size, int rtp_pt)
{
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->getAuxSDPLine(buff, size, rtp_pt);
    }
    else
#endif    
    {
        int codec = m_pConfig->input.video.codec;

        sys_os_mutex_enter(m_pRuaMutex);
        
        if (m_rua)
        {
            RSSUA * p_rua = (RSSUA *)m_rua;
            codec = p_rua->media_info.v_info.codec;
        }
        
        if (codec == VIDEO_CODEC_H264)
        {
            getH264AuxSDPLine(buff, size, rtp_pt);        
        }
        else if (codec == VIDEO_CODEC_H265)
        {
            getH265AuxSDPLine(buff, size, rtp_pt);
        }
        else if (codec == VIDEO_CODEC_MP4)
        {
            getMP4AuxSDPLine(buff, size, rtp_pt);
        }

        sys_os_mutex_leave(m_pRuaMutex);
    }
}

void CMediaPusher::getAudioAuxSDPLine(char * buff, int size, int rtp_pt)
{
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->getAuxSDPLine(buff, size, rtp_pt);
    }
    else
#endif
    {
        int codec = m_pConfig->input.audio.codec;
        
        sys_os_mutex_enter(m_pRuaMutex);
        
        if (m_rua)
        {
            RSSUA * p_rua = (RSSUA *)m_rua;
            codec = p_rua->media_info.a_info.codec;
        }
        
        if (codec == AUDIO_CODEC_AAC)
        {
            getAACAuxSDPLine(buff, size, rtp_pt);        
        }

        sys_os_mutex_leave(m_pRuaMutex);
    }
}


/*******************************************************************************/

void CMediaPusher::setRua(void * p_rua) 
{
    if (m_pConfig->transfer.mode != TRANSFER_MODE_RTSP)
    {
        return;
    }
    
    sys_os_mutex_enter(m_pRuaMutex);

    m_rua = p_rua;

    if (NULL == m_rua)
    {
        reinitMedia();
    }
    
    sys_os_mutex_leave(m_pRuaMutex);
}

void CMediaPusher::setAACConfig(int size_length, int index_length, int index_delta_length)
{
    m_aua.aacrxi.size_length = size_length;
    m_aua.aacrxi.index_length = index_length;
    m_aua.aacrxi.index_delta_length = index_delta_length;
}

void CMediaPusher::setMpeg4Config(uint8 * data, int len)
{
    m_vua.mpeg4rxi.hdr_len = len;
    memcpy(m_vua.mpeg4rxi.buf, data, len);
}

void CMediaPusher::setH264Params(char * p_sdp)
{
    char sps[1000] = {'\0'}, pps[1000] = {'\0'};

    char * p_substr = strstr(p_sdp, "sprop-parameter-sets=");
    if (p_substr != NULL)
    {
        p_substr += strlen("sprop-parameter-sets=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0') 
        {
            p_tmp++;
        }
        
        uint32 sps_base64_len = p_tmp - p_substr;
        if (sps_base64_len < sizeof(sps))
        {
            memcpy(sps, p_substr, sps_base64_len);
            sps[sps_base64_len] = '\0';
        }
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
    if (len > 0)
    {
        if ((sps_pps[4] & 0x1f) == 7)
        {
            if (len+4 < (int) sizeof(m_vua.h264rxi.param_sets.sps))
            {
                memcpy(m_vua.h264rxi.param_sets.sps, sps_pps, len+4);
                m_vua.h264rxi.param_sets.sps_len = len+4;

                onVideo(sps_pps, len+4, 0, 0);
            }
        }
        else if ((sps_pps[4] & 0x1f) == 8)
        {
            if (len+4 < (int) sizeof(m_vua.h264rxi.param_sets.pps))
            {
                memcpy(m_vua.h264rxi.param_sets.pps, sps_pps, len+4);
                m_vua.h264rxi.param_sets.pps_len = len+4;

                onVideo(sps_pps, len+4, 0, 0);
            }
        }
    }    
    
    if (pps[0] != '\0')
    {        
        len = base64_decode(pps, strlen(pps), sps_pps+4, sizeof(sps_pps)-4);
        if (len > 0)
        {
            if ((sps_pps[4] & 0x1f) == 7)
            {
                if (len+4 < (int) sizeof(m_vua.h264rxi.param_sets.sps))
                {
                    memcpy(m_vua.h264rxi.param_sets.sps, sps_pps, len+4);
                    m_vua.h264rxi.param_sets.sps_len = len+4;

                    onVideo(sps_pps, len+4, 0, 0);
                }
            }
            else if ((sps_pps[4] & 0x1f) == 8)
            {
                if (len+4 < (int) sizeof(m_vua.h264rxi.param_sets.pps))
                {
                    memcpy(m_vua.h264rxi.param_sets.pps, sps_pps, len+4);
                    m_vua.h264rxi.param_sets.pps_len = len+4;

                    onVideo(sps_pps, len+4, 0, 0);
                }
            }
        }
    }
}

void CMediaPusher::setH265Params(char * p_sdp)
{
    char vps[1000] = {'\0'}, sps[1000] = {'\0'}, pps[1000] = {'\0'};
    
    char * p_substr = strstr(p_sdp, "sprop-vps=");
    if (p_substr != NULL)
    {
        p_substr += strlen("sprop-vps=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        uint32 len = p_tmp - p_substr;
        if (len < sizeof(vps)-1)
        {
            memcpy(vps, p_substr, len);
            vps[len] = '\0';
        }
    }

    p_substr = strstr(p_sdp, "sprop-sps=");
    if (p_substr != NULL)
    {
        p_substr += strlen("sprop-sps=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        uint32 len = p_tmp - p_substr;
        if (len < sizeof(sps)-1)
        {
            memcpy(sps, p_substr, len);
            sps[len] = '\0';
        }
    }

    p_substr = strstr(p_sdp, "sprop-pps=");
    if (p_substr != NULL)
    {
        p_substr += strlen("sprop-pps=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        uint32 len = p_tmp - p_substr;
        if (len < sizeof(pps)-1)
        {
            memcpy(pps, p_substr, len);
            pps[len] = '\0';
        }
    }

    uint8 buff[1000];
    buff[0] = 0x0;
    buff[1] = 0x0;
    buff[2] = 0x0;
    buff[3] = 0x1;
    
    if (vps[0] != '\0')
    {
        int len = base64_decode(vps, strlen(vps), buff+4, sizeof(buff)-4);
        if (len > 0)
        {
            memcpy(m_vua.h265rxi.param_sets.vps, buff, len+4);
            m_vua.h265rxi.param_sets.vps_len = len+4;

            onVideo(buff, len+4, 0, 0);
        }
    }

    if (sps[0] != '\0')
    {
        int len = base64_decode(sps, strlen(sps), buff+4, sizeof(buff)-4);
        if (len > 0)
        {
            memcpy(m_vua.h265rxi.param_sets.sps, buff, len+4);
            m_vua.h265rxi.param_sets.sps_len = len+4;

            onVideo(buff, len+4, 0, 0);
        }
    }

    if (pps[0] != '\0')
    {
        int len = base64_decode(pps, strlen(pps), buff+4, sizeof(buff)-4);
        if (len > 0)
        {
            memcpy(m_vua.h265rxi.param_sets.pps, buff, len+4);
            m_vua.h265rxi.param_sets.pps_len = len+4;

            onVideo(buff, len+4, 0, 0);
        }
    }
}

BOOL CMediaPusher::startUdpRx(int vfd, int afd)
{
    if (m_pConfig->transfer.mode == TRANSFER_MODE_RTSP)
    {
        m_vua.sfd = vfd;
        m_aua.sfd = afd;
    
        m_bRecving = TRUE;
        m_tidRecv = sys_os_create_thread((void *)media_pusher_udp_rx, this);

        return TRUE;
    }

    return FALSE;
}

void CMediaPusher::stopUdpRx()
{
    if (m_pConfig->transfer.mode != TRANSFER_MODE_RTSP)
    {
        return;
    }
    
    m_bRecving = FALSE;

    sys_os_wait_thread(&m_tidRecv);
    
    reinitMedia();
}

BOOL CMediaPusher::videoRtpRx(uint8 * p_rtp, int rlen)
{
    return videoRtpRx(&m_vua, p_rtp, rlen);
}

BOOL CMediaPusher::audioRtpRx(uint8 * p_rtp, int rlen)
{
    return audioRtpRx(&m_aua, p_rtp, rlen);
}

#ifdef RTSP_METADATA
BOOL CMediaPusher::metadataRtpRx(uint8 * p_rtp, int rlen)
{
    return pcm_rtp_rx(&m_mrxi, p_rtp, rlen);
}
#endif

/***********************************************************************/

/**
 * 1  - need
 * 0  - Still not sure
 * -1 - not need
 */
int CMediaPusher::needVideoRecodec(uint8 * p_data, int len)
{
    if (0 != m_nVideoRecodec)
    {
        return m_nVideoRecodec;    
    }
    
    if (NULL == m_pConfig || !m_pConfig->has_output || !m_pConfig->output.has_video)
    {
        m_nVideoRecodec = -1;
        return -1;
    }

    if (m_pConfig->input.video.width == 0 || m_pConfig->input.video.height == 0)
    {
        if (VIDEO_CODEC_H264 == m_pConfig->input.video.codec)
        {
            if (m_vua.h264rxi.param_sets.sps_len > 0)
            {
                avc_parse_video_size(VIDEO_CODEC_H264, 
                    m_vua.h264rxi.param_sets.sps, m_vua.h264rxi.param_sets.sps_len, 
                    &m_pConfig->input.video.width, &m_pConfig->input.video.height);
            }
        }
        else if (VIDEO_CODEC_H265 == m_pConfig->input.video.codec)
        {
            if (m_vua.h265rxi.param_sets.sps_len > 0)
            {
                avc_parse_video_size(VIDEO_CODEC_H265, 
                    m_vua.h265rxi.param_sets.sps, m_vua.h265rxi.param_sets.sps_len, 
                    &m_pConfig->input.video.width, &m_pConfig->input.video.height);
            }
        }
    }

    if (m_pConfig->input.video.width == 0 || m_pConfig->input.video.height == 0)
    {
        return 0;
    }

    if (VIDEO_CODEC_NONE == m_pConfig->output.video.codec)
    {
        m_pConfig->output.video.codec = m_pConfig->input.video.codec;
    }

    if (0 == m_pConfig->output.video.width)
    {
        m_pConfig->output.video.width = m_pConfig->input.video.width;
    }

    if (0 == m_pConfig->output.video.height)
    {
        m_pConfig->output.video.height = m_pConfig->input.video.height;
    }

    if (0 == m_pConfig->output.video.framerate)
    {
        m_pConfig->output.video.framerate = 25;
    }
            
    if (m_pConfig->output.video.codec != m_pConfig->input.video.codec ||
        m_pConfig->output.video.width != m_pConfig->input.video.width ||
        m_pConfig->output.video.height != m_pConfig->input.video.height)
    {
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

        m_pVideoDecoder = new CVideoDecoder;
        if (NULL == m_pVideoDecoder)
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new video decoder failed\r\n", __FUNCTION__);
            return -1;
        }

        uint8 extra[1024];
        int extra_len = sizeof(extra);

        getExtraData(extra, &extra_len);
            
        if (!m_pVideoDecoder->init(m_pConfig->input.video.codec, extra, extra_len))
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, video decoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pVideoDecoder->setCallback(media_pusher_vdecoder_cb, this);

        if (VIDEO_CODEC_H264 == m_pConfig->input.video.codec)
        {
            if (m_vua.h264rxi.param_sets.sps_len > 0)
            {
                m_pVideoDecoder->decode(m_vua.h264rxi.param_sets.sps, m_vua.h264rxi.param_sets.sps_len);
            }

            if (m_vua.h264rxi.param_sets.pps_len > 0)
            {
                m_pVideoDecoder->decode(m_vua.h264rxi.param_sets.pps, m_vua.h264rxi.param_sets.pps_len);
            }
        }
        else if (VIDEO_CODEC_H265 == m_pConfig->input.video.codec)
        {
            if (m_vua.h265rxi.param_sets.vps_len > 0)
            {
                m_pVideoDecoder->decode(m_vua.h265rxi.param_sets.vps, m_vua.h265rxi.param_sets.vps_len);
            }
            
            if (m_vua.h265rxi.param_sets.sps_len > 0)
            {
                m_pVideoDecoder->decode(m_vua.h265rxi.param_sets.sps, m_vua.h265rxi.param_sets.sps_len);
            }

            if (m_vua.h265rxi.param_sets.pps_len > 0)
            {
                m_pVideoDecoder->decode(m_vua.h265rxi.param_sets.pps, m_vua.h265rxi.param_sets.pps_len);
            }
        }

        m_pVideoEncoder = new CVideoEncoder;        
        if (NULL == m_pVideoEncoder)
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new video encoder failed\r\n", __FUNCTION__);
            return -1;
        }

        VideoEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcWidth = m_pConfig->input.video.width;
        params.SrcHeight = m_pConfig->input.video.height;
        params.SrcPixFmt = AV_PIX_FMT_YUV420P;
        params.DstCodec = m_pConfig->output.video.codec;
        params.DstWidth = m_pConfig->output.video.width;
        params.DstHeight = m_pConfig->output.video.height;
        params.DstFramerate = m_pConfig->output.video.framerate;
        params.DstBitrate = m_pConfig->output.video.bitrate;
        
        if (FALSE == m_pVideoEncoder->init(&params))
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, video encoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pVideoEncoder->addCallback(media_pusher_vencoder_cb, this);
#endif

        m_nVideoRecodec = 1;
    }
    else
    {
        m_nVideoRecodec = -1;
    }

    return m_nVideoRecodec;
}

/**
 * 1  - need
 * 0  - Still not sure
 * -1 - not need
 */
int CMediaPusher::needAudioRecodec()
{
    if (0 != m_nAudioRecodec)
    {
        return m_nAudioRecodec;    
    }
    
    if (NULL == m_pConfig || !m_pConfig->has_output || !m_pConfig->output.has_audio)
    {
        m_nAudioRecodec = -1;
        return -1;
    }

    if (AUDIO_CODEC_NONE == m_pConfig->output.audio.codec)
    {
        m_pConfig->output.audio.codec = m_pConfig->input.audio.codec;
    }

    if (0 == m_pConfig->output.audio.samplerate)
    {
        m_pConfig->output.audio.samplerate = m_pConfig->input.audio.samplerate;
    }

    if (0 == m_pConfig->output.audio.channels)
    {
        m_pConfig->output.audio.channels = m_pConfig->input.audio.channels;
    }
            
    if (m_pConfig->output.audio.codec != m_pConfig->input.audio.codec ||
        m_pConfig->output.audio.samplerate != m_pConfig->input.audio.samplerate ||
        m_pConfig->output.audio.channels != m_pConfig->input.audio.channels)
    {
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

        m_pAudioDecoder = new CAudioDecoder;
        if (NULL == m_pAudioDecoder)
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new audio decoder failed\r\n", __FUNCTION__);
            return -1;
        }

        int bitpersample = m_pConfig->input.audio.bitrate / 8; // For G726 decode
        
        if (!m_pAudioDecoder->init(m_pConfig->input.audio.codec, m_pConfig->input.audio.samplerate, 
                                   m_pConfig->input.audio.channels, bitpersample))
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, audio decoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pAudioDecoder->setCallback(media_pusher_adecoder_cb, this);

        m_pAudioEncoder = new CAudioEncoder;        
        if (NULL == m_pAudioEncoder)
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new audio encoder failed\r\n", __FUNCTION__);
            return -1;
        }

        AudioEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcSamplerate = m_pConfig->input.audio.samplerate;
        params.SrcChannels = m_pConfig->input.audio.channels;
        params.SrcSamplefmt = AV_SAMPLE_FMT_S16;
        params.DstCodec = m_pConfig->output.audio.codec;
        params.DstSamplerate = m_pConfig->output.audio.samplerate;
        params.DstChannels = m_pConfig->output.audio.channels;
        params.DstSamplefmt = AV_SAMPLE_FMT_S16;
        params.DstBitrate = m_pConfig->output.audio.bitrate;
        
        if (FALSE == m_pAudioEncoder->init(&params))
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, audio encoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pAudioEncoder->addCallback(media_pusher_aencoder_cb, this);
#endif

        m_nAudioRecodec = 1;
    }
    else
    {
        m_nAudioRecodec = -1;
    }

    return m_nAudioRecodec;
}

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

void CMediaPusher::videoDataDecode(uint8 * p_data, int len)
{
    if (m_pVideoDecoder)
    {
        m_pVideoDecoder->decode(p_data, len);
    }
}

void CMediaPusher::videoDataEncode(AVFrame * frame)
{
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->encode(frame);
    }
}

void CMediaPusher::audioDataDecode(uint8 * p_data, int len)
{
    if (m_pAudioDecoder)
    {
        m_pAudioDecoder->decode(p_data, len);
    }
}

void CMediaPusher::audioDataEncode(AVFrame * frame)
{
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->encode(frame);
    }
}

#endif // end of defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

#endif // end of MEDIA_PUSHER


