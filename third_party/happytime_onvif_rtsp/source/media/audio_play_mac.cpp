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
#include "audio_play_mac.h"


void audioPlayCb(void * buff, uint32 size, void * userdata)
{
    CMAudioPlay * player = (CMAudioPlay *) userdata;

    player->playCallback(buff, size);
}

CMAudioPlay::CMAudioPlay() : CAudioPlay()
, m_pMutex(NULL)
, m_pPlayer(NULL)
, m_pBuffer(NULL)
, m_nBufferSize(0)
, m_nOffset(0)
{
    m_pMutex = sys_os_create_mutex();
}

CMAudioPlay::~CMAudioPlay()
{
    stopPlay();

    sys_os_destroy_mutex(m_pMutex);
    
    m_pMutex = NULL;
}

BOOL CMAudioPlay::startPlay(int samplerate, int channels)
{
    m_pPlayer = avf_audio_play_init(samplerate, channels);
    if (NULL == m_pPlayer)
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_play_init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nBufferSize = 8192;
    m_pBuffer = (uint8 *) malloc(m_nBufferSize);
    if (NULL == m_pBuffer)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    avf_audio_play_set_callback(m_pPlayer, audioPlayCb, this);
    
    m_nSamplerate = samplerate;
    m_nChannels = channels;

    m_bInited = TRUE;
    
    return TRUE;
}

void CMAudioPlay::stopPlay()
{
    avf_audio_play_uninit(m_pPlayer);

    sys_os_mutex_enter(m_pMutex);
    
    if (m_pBuffer)
    {
        free(m_pBuffer);
        m_pBuffer = NULL;
    }

    m_nOffset = 0;

    m_bInited = FALSE;

    sys_os_mutex_leave(m_pMutex);
}

void CMAudioPlay::playAudio(uint8 * data, int size) 
{
    while (1)
    {
        sys_os_mutex_enter(m_pMutex);

        if (!m_bInited || NULL == m_pBuffer)
        {
            sys_os_mutex_leave(m_pMutex);
            break;
        }

        if (m_nOffset + size <= m_nBufferSize)
        {
            memcpy(m_pBuffer + m_nOffset, data, size);
            m_nOffset += size;

            sys_os_mutex_leave(m_pMutex);
            break;
        }
        else
        {
            sys_os_mutex_leave(m_pMutex);
            usleep(10*1000);
            continue;
        }
    }
}

void CMAudioPlay::playCallback(void * buff, uint32 size)
{
    sys_os_mutex_enter(m_pMutex);

    if (!m_bInited || NULL == m_pBuffer)
    {
        sys_os_mutex_leave(m_pMutex);
        return;
    }

    if (m_nOffset >= size)
    {
        memcpy(buff, m_pBuffer, size);
        m_nOffset -= size;

        if (m_nOffset > 0)
        {
            memmove(m_pBuffer, m_pBuffer+size, m_nOffset);
        }
    }
    else
    {
        memset(buff, 0, size);
    }

    sys_os_mutex_leave(m_pMutex);
}

BOOL CMAudioPlay::setVolume(int volume)
{
    if (m_pPlayer)
    {
        double db = (double) volume / (HTVOLUME_MAX - HTVOLUME_MIN);
        if (db < 0)
        {
            db = 0.0;
        }
        else if (db > 1.0)
        {
            db = 1.0;
        }

        return avf_audio_play_set_volume((void *)m_pPlayer, db);
    }  

    return FALSE;
}

int CMAudioPlay::getVolume()
{
    if (m_pPlayer)
    {
        double volume = avf_audio_play_get_volume((void *)m_pPlayer);
        
        int nv = (HTVOLUME_MAX - HTVOLUME_MIN) * volume + HTVOLUME_MIN;

        return nv;
    }
    
    return HTVOLUME_MIN;
}



