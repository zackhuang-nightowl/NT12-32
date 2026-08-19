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
#include "media_proxy.h"
#include "media_format.h"
#include "h264.h"
#include "h265.h"
#include "h264_util.h"
#include "h265_util.h"
#include "media_util.h"
#include "media_parse.h"
#include "mjpeg.h"
#include "base64.h"
#include "http_test.h"
#include <math.h>

#ifdef MEDIA_PROXY

/***************************************************************************************/

MEDIA_PRY * g_proxy = NULL;

/***************************************************************************************/

int event_notify_cb(int evt, void * puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onNotify(evt);
    return 0;
}

int rtsp_audio_cb(uint8 * pdata, int len, uint32 ts, uint16 seq, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onAudio(pdata, len, ts, seq);
    return 0;
}

int rtsp_video_cb(uint8 * pdata, int len, uint32 ts, uint16 seq, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onVideo(pdata, len, ts, seq);
    return 0;
}

int rtsp_metadata_cb(uint8 * pdata, int len, uint32 ts, uint16 seq, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onMetadata(pdata, len, ts, seq);
    return 0;
}

int jpeg_video_cb(uint8 * pdata, int len, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onVideo(pdata, len, 0, 0);
    return 0;
}

#ifdef RTMP_PROXY

int rtmp_video_data_cb(uint8 * pdata, int len, uint32 ts, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onVideo(pdata, len, ts, 0);
    return 0;
}

int rtmp_audio_data_cb(uint8 * pdata, int len, uint32 ts, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onAudio(pdata, len, ts, 0);
    return 0;
}

#endif // RTMP_PROXY

#ifdef SRT_PROXY

int srt_video_data_cb(uint8 * pdata, int len, uint32 ts, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onVideo(pdata, len, ts, 0);
    return 0;
}

int srt_audio_data_cb(uint8 * pdata, int len, uint32 ts, void *puser)
{
    CMediaProxy * p_proxy = (CMediaProxy *) puser;

    p_proxy->onAudio(pdata, len, ts, 0);
    return 0;
}

#endif // SRT_PROXY

void * notifyThread(void * argv)
{
    CMediaProxy * p_proxy = (CMediaProxy *) argv;

    p_proxy->notifyHandler();

    return NULL;
}

/*************************************************************/

MEDIA_PRY * media_add_proxy()
{
    MEDIA_PRY * p_tmp;
    MEDIA_PRY * p_new_proxy = (MEDIA_PRY *) malloc(sizeof(MEDIA_PRY));
    if (NULL == p_new_proxy)
    {
        return NULL;
    }

    memset(p_new_proxy, 0, sizeof(MEDIA_PRY));

    p_tmp = g_proxy;
    if (NULL == p_tmp)
    {
        g_proxy = p_new_proxy;
    }
    else
    {
        while (p_tmp && p_tmp->next)
        {
            p_tmp = p_tmp->next;
        }
        
        p_tmp->next = p_new_proxy;
    }

    return p_new_proxy;
}

void media_free_proxies()
{
    MEDIA_PRY * p_next;
    MEDIA_PRY * p_tmp = g_proxy;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        if (p_tmp->proxy)
        {
            p_tmp->proxy->freeInstance(p_tmp->cfg.key);
            p_tmp->proxy = NULL;
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    g_proxy = NULL;
}

BOOL media_init_proxy(MEDIA_PRY * p_proxy)
{
    BOOL ret = FALSE;
    
    p_proxy->proxy = CMediaProxy::getInstance(p_proxy->cfg.key, &p_proxy->cfg);
    if (p_proxy->proxy)
    {
        ret = TRUE;
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, new rtsp proxy failed\r\n", __FUNCTION__);
    }
    
    return ret;
}

void media_init_proxies()
{
    MEDIA_PRY * p_proxy = g_proxy;
    while (p_proxy)
    {
        if (!p_proxy->cfg.on_demand)
        {
            media_init_proxy(p_proxy);
        }
        
        p_proxy = p_proxy->next;
    }
}

int media_get_proxy_nums()
{
    int nums = 0;
    
    MEDIA_PRY * p_proxy = g_proxy;
    while (p_proxy)
    {
        nums++;
        
        p_proxy = p_proxy->next;
    }

    return nums;
}

MEDIA_PRY * media_proxy_match(const char * key)
{
    MEDIA_PRY * p_proxy = g_proxy;
    while (p_proxy)
    {
        if (strcmp(key, p_proxy->cfg.key) == 0)
        {
            return p_proxy;
        }
        
        p_proxy = p_proxy->next;
    }
    
    return NULL;
}

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

void media_proxy_vdecoder_cb(AVFrame * frame, void *pUserdata)
{
    CMediaProxy * pProxy = (CMediaProxy *)pUserdata;

    pProxy->videoDataEncode(frame);
}

void media_proxy_vencoder_cb(uint8 *data, int size, int waitnext, void *pUserdata)
{
    CMediaProxy * pProxy = (CMediaProxy *)pUserdata;

    pProxy->dataCallback(data, size, DATA_TYPE_VIDEO);
}

void media_proxy_adecoder_cb(AVFrame * frame, void *pUserdata)
{
    CMediaProxy * pProxy = (CMediaProxy *)pUserdata;

    pProxy->audioDataEncode(frame);
}

void meida_proxy_aencoder_cb(uint8 *data, int size, int nbsamples, void *pUserdata)
{
    CMediaProxy * pProxy = (CMediaProxy *)pUserdata;

    pProxy->dataCallback(data, size, DATA_TYPE_AUDIO);
}

#endif

/***************************************************************************************/
void * CMediaProxy::m_pInstMutex = sys_os_create_mutex();
std::map<std::string,CMediaProxy*> CMediaProxy::m_insts;

/***************************************************************************************/

CMediaProxy * CMediaProxy::getInstance(char * key, PROXY_CFG * pConfig)
{
    CMediaProxy * p_proxy = NULL;
    std::map<std::string,CMediaProxy*>::iterator it;
    
    sys_os_mutex_enter(m_pInstMutex);

    it = m_insts.find(key);
    
    if (it == m_insts.end())
    {
        p_proxy = new CMediaProxy(pConfig);

        p_proxy->m_nRefCnt++;
        p_proxy->startConn(pConfig->url, pConfig->user, pConfig->pass);
        
        m_insts[key] = p_proxy;
    }
    else
    {
        p_proxy = it->second;
        p_proxy->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return p_proxy;
}

void CMediaProxy::freeInstance(char * key)
{
    CMediaProxy * p_proxy = NULL;
    
    sys_os_mutex_enter(m_pInstMutex);

    std::map<std::string,CMediaProxy*>::iterator it = m_insts.find(key);
    
    if (it != m_insts.end())
    {
        p_proxy = it->second;
        p_proxy->m_nRefCnt--;

        if (p_proxy->m_nRefCnt <= 0)
        {
            delete p_proxy;
            
            m_insts.erase(it);
        }
    }

    sys_os_mutex_leave(m_pInstMutex);
}

/*************************************************************/
CMediaProxy::CMediaProxy(PROXY_CFG * pConfig)
: m_nRefCnt(0)
, m_bInited(0)
, m_bExiting(0)
, m_pConfig(NULL)
, m_reconnto(1)
, m_rtsp(NULL)
, m_mjpeg(NULL)
#ifdef RTMP_PROXY
, m_rtmp(NULL)
#endif
#ifdef SRT_PROXY
, m_srt(NULL)
#endif
{
    memset(&m_info, 0, sizeof(MEDIA_INFO));
    memset(m_url, 0, sizeof(m_url));
    memset(m_user, 0, sizeof(m_user));
    memset(m_pass, 0, sizeof(m_pass));

    if (pConfig)
    {
        m_pConfig = (PROXY_CFG *) malloc(sizeof(PROXY_CFG));
        memcpy(m_pConfig, pConfig, sizeof(PROXY_CFG));
    }

    m_pMutex = sys_os_create_mutex();
    m_notifyQueue = hqueue_create(10, sizeof(int), HQ_GET_WAIT | HQ_PUT_WAIT);
    m_hNotify = sys_os_create_thread((void *) notifyThread, this);

    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);

    m_nVideoRecodec = 0;
    m_nAudioRecodec = 0;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    m_pVideoDecoder = NULL;
    m_pAudioDecoder = NULL;
    m_pVideoEncoder = NULL;
    m_pAudioEncoder = NULL;
#endif
}

CMediaProxy::~CMediaProxy(void)
{
    int evt = -1;
    
    m_bExiting = 1;

    sys_os_mutex_enter(m_pMutex);
    freeConn();
    sys_os_mutex_leave(m_pMutex);

    hqueue_put(m_notifyQueue, (char *)&evt);

    sys_os_wait_thread(&m_hNotify);

    hqueue_delete(m_notifyQueue);
    m_notifyQueue = NULL;

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
        free(m_pConfig);
        m_pConfig = NULL;
    }
    
    hlist_free_container(m_pCallbackList);

    sys_os_destroy_mutex(m_pMutex);
    sys_os_destroy_mutex(m_pCallbackMutex);
}

void CMediaProxy::freeConn()
{
    if (m_rtsp)
    {
        // stop rtsp connection
        m_rtsp->rtsp_stop();
        m_rtsp->rtsp_close();
    
        delete m_rtsp;
        m_rtsp = NULL;
    }

    if (m_mjpeg)
    {
        m_mjpeg->mjpeg_stop();
        m_mjpeg->mjpeg_close();
        
        delete m_mjpeg;
        m_mjpeg = NULL;
    }

#ifdef RTMP_PROXY
    if (m_rtmp)
    {
        // stop rtmp connection
        m_rtmp->rtmp_stop();
        m_rtmp->rtmp_close();
    
        delete m_rtmp;
        m_rtmp = NULL;
    }
#endif

#ifdef SRT_PROXY
    if (m_srt)
    {
        // stop srt connection
        m_srt->srt_stop();
        m_srt->srt_close();

        delete m_srt;
        m_srt = NULL;
    }
#endif
}

BOOL CMediaProxy::startConn()
{
    BOOL isRtsp = FALSE;
    BOOL isMjpeg = FALSE;
#ifdef RTMP_PROXY    
    BOOL isRtmp = FALSE;
#endif
#ifdef SRT_PROXY
    BOOL isSrt = FALSE;
#endif
    
    if (memcmp(m_url, "rtsp://", 7) == 0)
    {
        isRtsp = TRUE;
    }
#ifdef RTSPS
    else if (memcmp(m_url, "rtsps://", 8) == 0)
    {
        isRtsp = TRUE;
    }
#endif
    else if (memcmp(m_url, "http://", 7) == 0 || memcmp(m_url, "https://", 8) == 0)
    {
#ifdef OVER_HTTP   
        HTTPCTT ctt = CTT_NULL;
        
        if (http_test(m_url, m_user, m_pass, &ctt))
        {
            if (ctt == CTT_RTSP_TUNNELLED)
            {
                // rtsp over http or rtsp over https
                isRtsp = TRUE;
            }
            else
            {
                isMjpeg = TRUE;
            }
        }
        else
#endif
        {
            isMjpeg = TRUE;
        }
    }
#ifdef OVER_WEBSOCKET
    else if (memcmp(m_url, "ws://", 5) == 0 || memcmp(m_url, "wss://", 6) == 0)
    {
        isRtsp = TRUE;
    }
#endif
#ifdef RTMP_PROXY    
    else if (memcmp(m_url, "rtmp://", 7) == 0 || 
        memcmp(m_url, "rtmpt://", 8) == 0 ||
        memcmp(m_url, "rtmps://", 8) == 0 || 
        memcmp(m_url, "rtmpe://", 8) == 0 ||
        memcmp(m_url, "rtmpfp://", 9) == 0 || 
        memcmp(m_url, "rtmpte://", 9) == 0 ||
        memcmp(m_url, "rtmpts://", 9) == 0)    
    {
        isRtmp = TRUE;
    }
#endif
#ifdef SRT_PROXY
    else if (memcmp(m_url, "srt://", 6) == 0)
    {
        isSrt = TRUE;
    }
#endif
        
    if (isRtsp)
    {
        m_rtsp = new CRtspClient;
        if (NULL == m_rtsp)
        {
            log_print(HT_LOG_ERR, "%s, new rtsp failed\r\n", __FUNCTION__);
            return FALSE;
        }

        m_rtsp->set_notify_cb(event_notify_cb, this);
        m_rtsp->set_video_cb(rtsp_video_cb);
        m_rtsp->set_audio_cb(rtsp_audio_cb);
#ifdef METADATA        
        m_rtsp->set_metadata_cb(rtsp_metadata_cb);
#endif

        if (m_pConfig)
        {
            if (m_pConfig->transfer == RC_TRANSFER_UDP)
            {
                m_rtsp->set_rtp_over_udp(1);
            }
            else if (m_pConfig->transfer == RC_TRANSFER_MULTICAST)
            {
                m_rtsp->set_rtp_multicast(1);
            }
        }
        
        return m_rtsp->rtsp_start(m_url, m_user, m_pass);
    }
    else if (isMjpeg)
    {        
        m_mjpeg = new CHttpMjpeg;
        if (NULL == m_mjpeg)
        {
            log_print(HT_LOG_ERR, "%s, new httpmjpeg failed\r\n", __FUNCTION__);
            return FALSE;
        }

        m_mjpeg->set_notify_cb(event_notify_cb, this);
        m_mjpeg->set_video_cb(jpeg_video_cb);
        
        return m_mjpeg->mjpeg_start(m_url, m_user, m_pass);
    }
#ifdef RTMP_PROXY    
    else if (isRtmp)
    {
        m_rtmp = new CRtmpClient;
        if (NULL == m_rtmp)
        {
            log_print(HT_LOG_ERR, "%s, new rtmp client failed\r\n", __FUNCTION__);
            return FALSE;
        }

        m_rtmp->set_notify_cb(event_notify_cb, this);
        m_rtmp->set_video_cb(rtmp_video_data_cb);
        m_rtmp->set_audio_cb(rtmp_audio_data_cb);
        
        return m_rtmp->rtmp_start(m_url, m_user, m_pass);
    }
#endif
#ifdef SRT_PROXY
    else if (isSrt)
    {
        m_srt = new CSrtClient;
        if (NULL == m_srt)
        {
            log_print(HT_LOG_ERR, "%s, new srt failed\r\n", __FUNCTION__);
            return FALSE;
        }

        m_srt->set_notify_cb(event_notify_cb, this);
        m_srt->set_video_cb(srt_video_data_cb);
        m_srt->set_audio_cb(srt_audio_data_cb);
        
        return m_srt->srt_start(m_url, m_user, m_pass);
    }
#endif

    return FALSE;
}

BOOL CMediaProxy::startConn(const char * url, char * user, char * pass)
{
    strncpy(m_url, url, sizeof(m_url)-1);
    strncpy(m_user, user, sizeof(m_user)-1);
    strncpy(m_pass, pass, sizeof(m_pass)-1);

    return startConn();
}

BOOL CMediaProxy::restartConn()
{
    BOOL ret = FALSE;
    
    sys_os_mutex_enter(m_pMutex);
    if (!m_bExiting)
    {
        freeConn();

        ret = startConn();
    }
    sys_os_mutex_leave(m_pMutex);

    return ret;
}

void CMediaProxy::onNotify(int evt)
{
    if (evt == RTSP_EVE_CONNSUCC)
    {
        m_info.video.codec = m_rtsp->video_codec();
        m_info.audio.codec = m_rtsp->audio_codec();

        if (m_info.video.codec != VIDEO_CODEC_NONE)
        {
            m_info.has_video = 1;
        }

        if (m_info.audio.codec != AUDIO_CODEC_NONE)
        {
            m_info.has_audio = 1;
            m_info.audio.samplerate = m_rtsp->get_audio_samplerate();
            m_info.audio.channels = m_rtsp->get_audio_channels();
            m_info.audio.bitpersample = m_rtsp->get_audio_bitpersample();
        }
        
        m_bInited = 1;
    }
    else if (evt == MJPEG_EVE_CONNSUCC)
    {
        m_info.video.codec = VIDEO_CODEC_JPEG;
        m_info.has_video = 1;

        m_bInited = 1;
    }
#ifdef RTMP_PROXY
    else if (evt == RTMP_EVE_VIDEOREADY)
    {
        m_info.video.codec = m_rtmp->video_codec();
        if (m_info.video.codec != VIDEO_CODEC_NONE)
        {
            m_info.has_video = 1;
        }
        
        m_bInited = 1;
    }
    else if (evt == RTMP_EVE_AUDIOREADY)
    {
        m_info.audio.codec = m_rtmp->audio_codec();
        if (m_info.audio.codec != AUDIO_CODEC_NONE)
        {
            m_info.has_audio = 1;
            m_info.audio.samplerate = m_rtmp->get_audio_samplerate();
            m_info.audio.channels = m_rtmp->get_audio_channels();
        }
        
        m_bInited = 1;
    }
#endif
#ifdef SRT_PROXY
    else if (evt == SRT_EVE_VIDEOREADY)
    {
        m_info.video.codec = m_srt->video_codec();
        if (m_info.video.codec != VIDEO_CODEC_NONE)
        {
            m_info.has_video = 1;
        }
        
        m_bInited = 1;
    }
    else if (evt == SRT_EVE_AUDIOREADY)
    {
        m_info.audio.codec = m_srt->audio_codec();
        if (m_info.audio.codec != AUDIO_CODEC_NONE)
        {
            m_info.has_audio = 1;
            m_info.audio.samplerate = m_srt->get_audio_samplerate();
            m_info.audio.channels = m_srt->get_audio_channels();
        }
        
        m_bInited = 1;
    }
#endif
    
    hqueue_put(m_notifyQueue, (char *)&evt);
}

void CMediaProxy::onVideo(uint8 * pdata, int len, uint32 ts, uint16 seq)
{
    if (m_info.video.width == 0 || m_info.video.height == 0)
    {
        avc_parse_video_size(m_info.video.codec, pdata, len, &m_info.video.width, &m_info.video.height);
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

void CMediaProxy::onAudio(uint8 * pdata, int len, uint32 ts, uint16 seq)
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

void CMediaProxy::onMetadata(uint8 * pdata, int len, uint32 ts, uint16 seq)
{
    dataCallback(pdata, len, DATA_TYPE_METADATA);
}

void CMediaProxy::clearNotify()
{
    int evt;
    
    while (!hqueue_is_empty(m_notifyQueue))
    {
        hqueue_get(m_notifyQueue, (char *)&evt);
    }
}

void CMediaProxy::notifyHandler()
{
    int evt;
    
    while (!m_bExiting && hqueue_get(m_notifyQueue, (char *)&evt))
    {
        log_print(HT_LOG_DBG, "%s, evt = %d\r\n", __FUNCTION__, evt);
        
        if (evt < 0)
        {
            break;
        }

        if (   evt == RTSP_EVE_CONNFAIL || evt == RTSP_EVE_NOSIGNAL
            || evt == RTSP_EVE_NODATA || evt == RTSP_EVE_STOPPED 
            || evt == MJPEG_EVE_CONNFAIL || evt == MJPEG_EVE_NOSIGNAL
            || evt == MJPEG_EVE_NODATA || evt == MJPEG_EVE_STOPPED
#ifdef RTMP_PROXY            
            || evt == RTMP_EVE_CONNFAIL || evt == RTMP_EVE_NOSIGNAL
            || evt == RTMP_EVE_NODATA || evt == RTMP_EVE_STOPPED
#endif
#ifdef SRT_PROXY
            || evt == SRT_EVE_CONNFAIL || evt == SRT_EVE_NOSIGNAL
            || evt == SRT_EVE_NODATA || evt == SRT_EVE_STOPPED
#endif
            )
        {
            if (m_reconnto < 10)
            {
                m_reconnto *= 2;
            }
            
            sleep(m_reconnto);

            clearNotify();
            
            restartConn();
        }
        else if (evt == RTSP_EVE_CONNSUCC)
        {
            if (VIDEO_CODEC_H264 == m_info.video.codec)
            {
                m_rtsp->get_h264_params();
            }
            else if (VIDEO_CODEC_H265 == m_info.video.codec)
            {
                m_rtsp->get_h265_params();
            }
        }
#ifdef RTMP_PROXY
        else if (evt == RTMP_EVE_VIDEOREADY)
        {
            if (VIDEO_CODEC_H264 == m_info.video.codec)
            {
                m_rtmp->get_h264_params();
            }
            else if (VIDEO_CODEC_H265 == m_info.video.codec)
            {
                m_rtmp->get_h265_params();
            }
        }
#endif
#ifdef SRT_PROXY
        else if (evt == SRT_EVE_VIDEOREADY)
        {
            if (VIDEO_CODEC_H264 == m_info.video.codec)
            {
                m_srt->get_h264_params();
            }
            else if (VIDEO_CODEC_H265 == m_info.video.codec)
            {
                m_srt->get_h265_params();
            }
        }
#endif
    }
}

BOOL CMediaProxy::isCallbackExist(ProxyDataCB pCallback, void * pUserdata)
{
    BOOL exist = FALSE;
    ProxyCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (ProxyCB *) p_node->data;
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

void CMediaProxy::addCallback(ProxyDataCB pCallback, void * pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    ProxyCB * p_cb = (ProxyCB *) malloc(sizeof(ProxyCB));
    if (NULL == p_cb)
    {
        return;
    }

    memset(p_cb, 0, sizeof(ProxyCB));
    
    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bVFirst = TRUE;
    p_cb->bAFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CMediaProxy::delCallback(ProxyDataCB pCallback, void * pUserdata)
{
    ProxyCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (ProxyCB *) p_node->data;
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

void CMediaProxy::dataCallback(uint8 * data, int size, int type)
{
    ProxyCB * p_cb = NULL;
    LKNODE * p_node = NULL;
        
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (ProxyCB *) p_node->data;
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

BOOL CMediaProxy::getExtraData(uint8 * extra, int * extra_len)
{
    BOOL ret = FALSE;
    
    if (NULL == extra_len)
    {
        return FALSE;
    }
    
    if (m_info.video.codec == VIDEO_CODEC_H264)
    {
        uint8 sps[512], pps[512];
        int sps_len = 0, pps_len = 0, len = 0;
    
        if (m_rtsp && m_rtsp->get_h264_params(sps, &sps_len, pps, &pps_len))
        {
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#ifdef RTMP_PROXY                        
        else if (m_rtmp && m_rtmp->get_h264_params(sps, &sps_len, pps, &pps_len))
        {
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#endif
#ifdef SRT_PROXY
        else if (m_srt && m_srt->get_h264_params(sps, &sps_len, pps, &pps_len))
        {
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#endif

        *extra_len = len;
        ret = TRUE;
    }
    else if (m_info.video.codec == VIDEO_CODEC_H265)
    {
        uint8 vps[512], sps[512], pps[512];
        int vps_len, sps_len = 0, pps_len = 0, len = 0;
    
        if (m_rtsp && m_rtsp->get_h265_params(sps, &sps_len, pps, &pps_len, vps, &vps_len))
        {
            if (*extra_len >= len + vps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, vps, vps_len);
                }
                len += vps_len;
            }
            
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#ifdef RTMP_PROXY                        
        else if (m_rtmp && m_rtmp->get_h265_params(sps, &sps_len, pps, &pps_len, vps, &vps_len))
        {
            if (*extra_len >= len + vps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, vps, vps_len);
                }
                len += vps_len;
            }
            
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#endif
#ifdef SRT_PROXY
        else if (m_srt && m_srt->get_h265_params(sps, &sps_len, pps, &pps_len, vps, &vps_len))
        {
            if (*extra_len >= len + vps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, vps, vps_len);
                }
                len += vps_len;
            }
            
            if (*extra_len >= len + sps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, sps, sps_len);
                }
                len += sps_len;
            }

            if (*extra_len >= len + pps_len)
            {
                if (extra)
                {
                    memcpy(extra+len, pps, pps_len);
                }
                len += pps_len;
            }
        }
#endif

        *extra_len = len;
        ret = TRUE;
    }

    if (!ret)
    {
        *extra_len = 0;
    }
    
    return ret;
}

void CMediaProxy::sendExtraData(ProxyCB * p_cb, uint8 * data, int size, int type)
{
    if (m_info.video.codec == VIDEO_CODEC_H264)
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
    else if (m_info.video.codec == VIDEO_CODEC_H265)
    {
        uint8 naltype = avc_h265_nalu_type(data, size);

        if (HEVC_NAL_VPS == naltype || HEVC_NAL_SPS == naltype || HEVC_NAL_PPS == naltype)
        {
        }
        else if (!IS_IRAP_NAL(p_cb->nPreNalu) && (IS_IRAP_NAL(naltype) || p_cb->bVFirst))
        {
            uint8 extra[2048];
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

void CMediaProxy::getH264AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 sps_buf[512]; int sps_size = 0;
    uint8 pps_buf[512]; int pps_size = 0;

    if (m_rtsp && !m_rtsp->get_h264_params(sps_buf, &sps_size, pps_buf, &pps_size))
    {
        return;
    }
#ifdef RTMP_PROXY    
    else if (m_rtmp && !m_rtmp->get_h264_params(sps_buf, &sps_size, pps_buf, &pps_size))
    {
        return;
    }
#endif
#ifdef SRT_PROXY
    else if (m_srt && !m_srt->get_h264_params(sps_buf, &sps_size, pps_buf, &pps_size))
    {
        return;
    }
#endif

    if (sps_size < 4 || pps_size < 4)
    {
        return;
    }

    uint8 * sps = sps_buf;
    uint8 * pps = pps_buf;

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

void CMediaProxy::getH265AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 vps_buf[512]; int vps_size = 0;
    uint8 sps_buf[512]; int sps_size = 0;
    uint8 pps_buf[512]; int pps_size = 0;

    if (m_rtsp && !m_rtsp->get_h265_params(sps_buf, &sps_size, pps_buf, &pps_size, vps_buf, &vps_size))
    {
        return;
    }
#ifdef RTMP_PROXY    
    else if (m_rtmp && !m_rtmp->get_h265_params(sps_buf, &sps_size, pps_buf, &pps_size, vps_buf, &vps_size))
    {
        return;
    }
#endif
#ifdef SRT_PROXY
    else if (m_srt && !m_srt->get_h265_params(sps_buf, &sps_size, pps_buf, &pps_size, vps_buf, &vps_size))
    {
        return;
    }
#endif

    if (vps_size < 4 || sps_size < 4 || pps_size < 4)
    {
        return;
    }

    uint8 * sps = sps_buf;
    uint8 * pps = pps_buf;
    uint8 * vps = vps_buf;

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

void CMediaProxy::getMP4AuxSDPLine(char * buff, int size, int rtp_pt)
{
    char sdp[1024] = {'\0'};

    if (NULL == m_rtsp)
    {
        return;
    }

    if (m_rtsp->get_mp4_sdp_desc(sdp, sizeof(sdp)) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, get_mp4_sdp_desc failed\r\n", __FUNCTION__);
        return;
    }

    snprintf(buff, size, "a=fmtp:%d %s", rtp_pt, sdp);
}

void CMediaProxy::getAACAuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 * ac = NULL;
    int aclen = 0;
    uint8 config[4];

    if (m_rtsp)
    {
        ac = m_rtsp->get_audio_config();
        aclen = m_rtsp->get_audio_config_len();
    }
#ifdef RTMP_PROXY    
    else if (m_rtmp)
    {
        ac = m_rtmp->get_audio_config();
        aclen = m_rtmp->get_audio_config_len();
    }
#endif
#ifdef SRT_PROXY
    else if (m_srt)
    {
        ac = m_srt->get_audio_config();
        aclen = m_srt->get_audio_config_len();
    }
#endif

    if (NULL == ac || 0 == aclen)
    {
        int sample_rate = 8000;
        int channels = 1;

        if (m_rtsp)
        {
            sample_rate = m_rtsp->get_audio_samplerate();
            channels = m_rtsp->get_audio_channels();
        }
#ifdef RTMP_PROXY        
        else if (m_rtmp)
        {
            sample_rate = m_rtmp->get_audio_samplerate();
            channels = m_rtmp->get_audio_channels();
        }
#endif
#ifdef SRT_PROXY
        else if (m_srt)
        {
            sample_rate = m_srt->get_audio_samplerate();
            channels = m_srt->get_audio_channels();
        }
#endif

        aclen = ht_get_aac_config(sample_rate, channels, config, sizeof(config));
        ac = config;
    }

    ht_gen_aac_sdp_line(buff, size, rtp_pt, ac, aclen);
}

void CMediaProxy::getVideoAuxSDPLine(char * buff, int size, int rtp_pt)
{
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->getAuxSDPLine(buff, size, rtp_pt);
    }
    else
#endif

    if (m_info.video.codec == VIDEO_CODEC_H264)
    {
        getH264AuxSDPLine(buff, size, rtp_pt);
    }
    else if (m_info.video.codec == VIDEO_CODEC_H265)
    {
        getH265AuxSDPLine(buff, size, rtp_pt);
    }
    else if (m_info.video.codec == VIDEO_CODEC_MP4)
    {
        getMP4AuxSDPLine(buff, size, rtp_pt);
    }
}

void CMediaProxy::getAudioAuxSDPLine(char * buff, int size, int rtp_pt)
{
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->getAuxSDPLine(buff, size, rtp_pt);
    }
    else
#endif

    if (m_info.audio.codec == AUDIO_CODEC_AAC)
    {
        getAACAuxSDPLine(buff, size, rtp_pt);
    }
}

/**
 * 1  - need
 * 0  - Still not sure
 * -1 - not need
 */
int CMediaProxy::needVideoRecodec(uint8 * p_data, int len)
{
    if (0 != m_nVideoRecodec)
    {
        return m_nVideoRecodec;    
    }
    
    if (!m_pConfig->has_output || !m_pConfig->output.has_video)
    {
        m_nVideoRecodec = -1;
        return -1;
    }

    if (m_info.video.width == 0 || m_info.video.height == 0)
    {
        return 0;
    }

    if (VIDEO_CODEC_NONE == m_pConfig->output.video.codec)
    {
        m_pConfig->output.video.codec = m_info.video.codec;
    }

    if (0 == m_pConfig->output.video.width)
    {
        m_pConfig->output.video.width = m_info.video.width;
    }

    if (0 == m_pConfig->output.video.height)
    {
        m_pConfig->output.video.height = m_info.video.height;
    }

    if (0 == m_pConfig->output.video.framerate)
    {
        m_pConfig->output.video.framerate = 25;
    }
            
    if (m_pConfig->output.video.codec != m_info.video.codec ||
        m_pConfig->output.video.width != m_info.video.width ||
        m_pConfig->output.video.height != m_info.video.height)
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
            
        if (!m_pVideoDecoder->init(m_info.video.codec, extra, extra_len))
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, video decoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pVideoDecoder->setCallback(media_proxy_vdecoder_cb, this);

        m_pVideoEncoder = new CVideoEncoder;        
        if (NULL == m_pVideoEncoder)
        {
            m_nVideoRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new video encoder failed\r\n", __FUNCTION__);
            return -1;
        }

        VideoEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcWidth = m_info.video.width;
        params.SrcHeight = m_info.video.height;
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

        m_pVideoEncoder->addCallback(media_proxy_vencoder_cb, this);

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
int CMediaProxy::needAudioRecodec()
{
    if (0 != m_nAudioRecodec)
    {
        return m_nAudioRecodec;    
    }
    
    if (!m_pConfig->has_output || !m_pConfig->output.has_audio)
    {
        m_nAudioRecodec = -1;
        return -1;
    }

    if (AUDIO_CODEC_NONE == m_pConfig->output.audio.codec)
    {
        m_pConfig->output.audio.codec = m_info.audio.codec;
    }

    if (0 == m_pConfig->output.audio.samplerate)
    {
        m_pConfig->output.audio.samplerate = m_info.audio.samplerate;
    }

    if (0 == m_pConfig->output.audio.channels)
    {
        m_pConfig->output.audio.channels = m_info.audio.channels;
    }
            
    if (m_pConfig->output.audio.codec != m_info.audio.codec ||
        m_pConfig->output.audio.samplerate != m_info.audio.samplerate ||
        m_pConfig->output.audio.channels != m_info.audio.channels)
    {
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

        m_pAudioDecoder = new CAudioDecoder;
        if (NULL == m_pAudioDecoder)
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new audio decoder failed\r\n", __FUNCTION__);
            return -1;
        }

        if (!m_pAudioDecoder->init(m_info.audio.codec, m_info.audio.samplerate, m_info.audio.channels, m_info.audio.bitpersample))
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, audio decoder init failed\r\n", __FUNCTION__);
            return -1;
        }

        m_pAudioDecoder->setCallback(media_proxy_adecoder_cb, this);

        m_pAudioEncoder = new CAudioEncoder;        
        if (NULL == m_pAudioEncoder)
        {
            m_nAudioRecodec = -1;
            log_print(HT_LOG_ERR, "%s, new audio encoder failed\r\n", __FUNCTION__);
            return -1;
        }

        AudioEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcSamplerate = m_info.audio.samplerate;
        params.SrcChannels = m_info.audio.channels;
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

        m_pAudioEncoder->addCallback(meida_proxy_aencoder_cb, this);
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

void CMediaProxy::videoDataDecode(uint8 * p_data, int len)
{
    if (m_pVideoDecoder)
    {
        m_pVideoDecoder->decode(p_data, len);
    }
}

void CMediaProxy::videoDataEncode(AVFrame * frame)
{
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->encode(frame);
    }
}

void CMediaProxy::audioDataDecode(uint8 * p_data, int len)
{
    if (m_pAudioDecoder)
    {
        m_pAudioDecoder->decode(p_data, len);
    }
}

void CMediaProxy::audioDataEncode(AVFrame * frame)
{
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->encode(frame);
    }
}

#endif // end of defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

#endif // end of MEDIA_PROXY


