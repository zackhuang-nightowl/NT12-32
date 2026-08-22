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
#include "live_audio.h"
#include "lock.h"
#include "media_format.h"
#include "media_util.h"

#ifdef MEDIA_LIVE

/***************************************************************************************/

CLiveAudio * CLiveAudio::m_pInstance[] = {NULL, NULL, NULL, NULL};
void * CLiveAudio::m_pInstMutex = sys_os_create_mutex();


/***************************************************************************************/

void * liveAudioThread(void * argv)
{
    CLiveAudio *capture = (CLiveAudio *)argv;

    capture->captureThread();

    return NULL;
}

/***************************************************************************************/

CLiveAudio::CLiveAudio()
{ 
    m_nStreamIndex = 0;
    m_bCapture = FALSE;
    m_hCapture = 0;
    m_nCodecId = AUDIO_CODEC_NONE;
    m_nSampleRate = 8000;
    m_nBitrate = 0;
    m_nChannel = 1;
    
    m_pMutex = sys_os_create_mutex();    
    m_bInited = FALSE;    
    m_nRefCnt = 0;  

    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);
}

CLiveAudio::~CLiveAudio()
{ 
    stopCapture();
    
    sys_os_destroy_mutex(m_pMutex);

    hlist_free_container(m_pCallbackList);
    
    sys_os_destroy_mutex(m_pCallbackMutex);
}

CLiveAudio * CLiveAudio::getInstance(int idx)
{
    if (idx < 0 || idx >= MAX_LIVE_AUDIO_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[idx])
    {
        m_pInstance[idx] = new CLiveAudio;
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

void CLiveAudio::freeInstance(int idx)
{
    if (idx < 0 || idx >= MAX_LIVE_AUDIO_NUMS)
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

void CLiveAudio::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    // For AAC, Get AAC-specification parameters from the hardware encoder
    //  Construct "a = fmt:96 xxx" SDP line

    if (AUDIO_CODEC_AAC != m_nCodecId)
    {
        return;
    }

    uint8 config[4];
    int config_size;

    config_size = ht_get_aac_config(m_nSampleRate, m_nChannel, config, sizeof(config));
    ht_gen_aac_sdp_line(buff, size, rtp_pt, config, config_size);
}

int CLiveAudio::getStreamNums()
{
    // NVR serves up to one live slot per device channel (32). Keep == MAX_LIVE_AUDIO_NUMS.
    return MAX_LIVE_AUDIO_NUMS;
}

BOOL CLiveAudio::initCapture(int codec, int samplerate, int channels, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_nCodecId = codec;
    m_nSampleRate = samplerate;
    m_nChannel = channels;
    m_nBitrate = bitrate;

    // todo : here add your init code ... 
    

    m_bInited = TRUE;

    return TRUE;
}

BOOL CLiveAudio::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)liveAudioThread, this);

    return m_hCapture ? TRUE : FALSE;
}

void CLiveAudio::stopCapture(void)
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);
    
    // todo : here add your uninit code ...

    

    m_bInited = FALSE;
}

BOOL CLiveAudio::captureThread()
{
    // todo : when get the encoded data, call procData 

    while (m_bCapture)
    {
        // todo : here add the capture handler ... 


        usleep(10*1000);
    }
    
    return TRUE;
}

BOOL CLiveAudio::isCallbackExist(LiveAudioDataCB pCallback, void *pUserdata)
{
    BOOL exist = FALSE;
    LiveAudioCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveAudioCB *) p_node->data;
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

void CLiveAudio::addCallback(LiveAudioDataCB pCallback, void * pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    LiveAudioCB * p_cb = (LiveAudioCB *) malloc(sizeof(LiveAudioCB));

    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CLiveAudio::delCallback(LiveAudioDataCB pCallback, void * pUserdata)
{
    LiveAudioCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveAudioCB *) p_node->data;
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

void CLiveAudio::procData(uint8 * data, int size, int nbsamples)
{
    LiveAudioCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (LiveAudioCB *) p_node->data;
        if (p_cb->pCallback != NULL)
        { 
            p_cb->pCallback(data, size, nbsamples, p_cb->pUserdata);
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

int CLiveAudio::getCodec() const
{
    return m_nCodecId;
}

int CLiveAudio::getSamplerate() const
{
    return m_nSampleRate;
}

int CLiveAudio::getChannels() const
{
    return m_nChannel;
}

int CLiveAudio::getBitrate() const
{
    return m_nBitrate;
}

BOOL CLiveAudio::isInited() const
{
    return m_bInited;
}

BOOL media_live_put_audio(int idx, uint8 * data, int size, int nbsamples)
{
    CLiveAudio * live = CLiveAudio::getInstance(idx);
    if (NULL == live)
    {
        return FALSE;
    }

    live->procData(data, size, nbsamples);

    live->freeInstance(idx);
    
    return TRUE;
}

#endif // end of MEDIA_LIVE


