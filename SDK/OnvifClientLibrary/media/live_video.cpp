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
#include "live_video.h"
#include "media_format.h"
#include "lock.h"

#ifdef MEDIA_LIVE

/**************************************************************************************/

CLiveVideo * CLiveVideo::m_pInstance[] = {NULL, NULL, NULL, NULL};
void * CLiveVideo::m_pInstMutex = sys_os_create_mutex();


/**************************************************************************************/

void * liveVideoThread(void * argv)
{
    CLiveVideo *capture = (CLiveVideo *)argv;

    capture->captureThread();

    return NULL;
}

/**************************************************************************************/

CLiveVideo::CLiveVideo()
{
    m_nStreamIndex = 0;    
    m_nCodecId = VIDEO_CODEC_NONE;
    m_nWidth = 0;
    m_nHeight = 0;
    m_nFramerate = 25;
    m_nBitrate = 0;

    m_pMutex = sys_os_create_mutex();
    
    m_bInited = FALSE;
    m_bCapture = FALSE;
    m_hCapture = 0;
    
    m_nRefCnt = 0;

    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);

    memset(&m_paramSets, 0, sizeof(m_paramSets));
    m_bParamsGot = FALSE;

    m_curTs = 0;
    m_curTsValid = 0;
}

CLiveVideo::~CLiveVideo()
{
    stopCapture();
    
    sys_os_destroy_mutex(m_pMutex);

    hlist_free_container(m_pCallbackList);
    
    sys_os_destroy_mutex(m_pCallbackMutex);
}

CLiveVideo * CLiveVideo::getInstance(int idx)
{
    if (idx < 0 || idx >= MAX_LIVE_VIDEO_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[idx])
    {
        m_pInstance[idx] = (CLiveVideo *) new CLiveVideo;
        if (m_pInstance[idx])
        {
            m_pInstance[idx]->m_nRefCnt++;
            m_pInstance[idx]->m_nStreamIndex = idx;
        }
    }
    else
    {
        m_pInstance[idx]->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return m_pInstance[idx];
}

void CLiveVideo::freeInstance(int idx)
{
    if (idx < 0 || idx >= MAX_LIVE_VIDEO_NUMS)
    {
        return;
    }

    if (m_pInstance[idx])
    {
        sys_os_mutex_enter(m_pInstMutex);
        
        if (m_pInstance[idx])
        {
            m_pInstance[idx]->m_nRefCnt--;

            if (m_pInstance[idx]->m_nRefCnt <= 0)
            {
                m_pInstance[idx]->m_bInited = FALSE;

                delete m_pInstance[idx];
                m_pInstance[idx] = NULL;
            }
        }

        sys_os_mutex_leave(m_pInstMutex);
    }
}

int CLiveVideo::getStreamNums()
{
    // NVR serves up to one live slot per device channel (32). addStream() validates
    // the requested idx against this; keep it == MAX_LIVE_VIDEO_NUMS.
    return MAX_LIVE_VIDEO_NUMS;
}

BOOL CLiveVideo::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_nCodecId = codec;
    m_nWidth = width;
    m_nHeight= height;
    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    // todo : here add your init code ... 
    

    m_bInited = TRUE;

    return TRUE;
}

void CLiveVideo::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (buff && size > 0)
    {
        buff[0] = '\0';
    }

    if (!m_bParamsGot || NULL == buff || size <= 0)
    {
        return;
    }

    if (VIDEO_CODEC_H264 == m_nCodecId)
    {
        ht_gen_h264_sdp_line(buff, size, rtp_pt,
                             m_paramSets.sps, m_paramSets.sps_size,
                             m_paramSets.pps, m_paramSets.pps_size);
    }
    else if (VIDEO_CODEC_H265 == m_nCodecId)
    {
        ht_gen_h265_sdp_line(buff, size, rtp_pt,
                             m_paramSets.sps, m_paramSets.sps_size,
                             m_paramSets.pps, m_paramSets.pps_size,
                             m_paramSets.vps, m_paramSets.vps_size);
    }
}

BOOL CLiveVideo::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)liveVideoThread, this);

    return m_hCapture ? TRUE : FALSE;
}

void CLiveVideo::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    // todo : here add your uninit code ...

    

    m_bInited = FALSE;
}

BOOL CLiveVideo::captureThread()
{
    // todo : when get the encoded data, call procData 

    while (m_bCapture)
    {
        // todo : here add the capture handler ... 


        usleep(10*1000);
    }
    
    return TRUE;
}

BOOL CLiveVideo::isCallbackExist(LiveVideoDataCB pCallback, void *pUserdata)
{
    BOOL exist = FALSE;
    LiveVideoCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveVideoCB *) p_node->data;
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

void CLiveVideo::addCallback(LiveVideoDataCB pCallback, void * pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    LiveVideoCB * p_cb = (LiveVideoCB *) malloc(sizeof(LiveVideoCB));

    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CLiveVideo::delCallback(LiveVideoDataCB pCallback, void * pUserdata)
{
    LiveVideoCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveVideoCB *) p_node->data;
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

void CLiveVideo::procData(uint8 * data, int size, uint32 ts, int ts_valid)
{
    LiveVideoCB * p_cb = NULL;
    LKNODE * p_node = NULL;

    /* Record this frame's caller ts for the RTSP callback (synchronous fan-out below
     * reads it via getCurTs/getCurTsValid). Playback sets a real 90kHz ts; live leaves 0. */
    m_curTs = ts;
    m_curTsValid = ts_valid;

    /* Sniff VPS/SPS/PPS once from the pushed Annex-B so getAuxSDPLine can emit
     * sprop-* in the served SDP (needed by H265/DXVA clients that init the
     * depacketizer from SDP rather than in-band). Cheap after first capture. */
    if (!m_bParamsGot && data && size > 0)
    {
        if (avc_get_h26x_paramsets(data, size, m_nCodecId, &m_paramSets))
        {
            m_bParamsGot = TRUE;
        }
    }

    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveVideoCB *) p_node->data;
        if (p_cb->pCallback != NULL)
        {            
            p_cb->pCallback(data, size, p_cb->pUserdata);
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

int CLiveVideo::getCodec() const
{
    return m_nCodecId;
}

int CLiveVideo::getWidth() const
{
    return m_nWidth;
}

int CLiveVideo::getHeight() const
{
    return m_nHeight;
}

double CLiveVideo::getFramerate() const
{
    return m_nFramerate;
}

int CLiveVideo::getBitrate() const
{
    return m_nBitrate;
}

BOOL CLiveVideo::isInited() const
{
    return m_bInited;
}

uint32 CLiveVideo::getCurTs() const
{
    return m_curTs;
}

int CLiveVideo::getCurTsValid() const
{
    return m_curTsValid;
}

BOOL media_live_put_video(int idx, uint8 * data, int size)
{
    CLiveVideo * live = CLiveVideo::getInstance(idx);
    if (NULL == live)
    {
        return FALSE;
    }

    live->procData(data, size);

    live->freeInstance(idx);

    return TRUE;
}

BOOL media_live_put_video_ts(int idx, uint8 * data, int size, uint32 ts)
{
    CLiveVideo * live = CLiveVideo::getInstance(idx);
    if (NULL == live)
    {
        return FALSE;
    }

    live->procData(data, size, ts, 1);

    live->freeInstance(idx);

    return TRUE;
}

#endif // MEDIA_LIVE


