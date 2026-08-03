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
#include "audio_play_linux.h"


CLAudioPlay::CLAudioPlay() : CAudioPlay()
{
    memset(&m_alsa, 0, sizeof(m_alsa));
    
    m_alsa.period_size = 64;
    m_alsa.framesize = 16 / 8 * 2;

    m_pMutex = sys_os_create_mutex();

    m_pAudioBuff = NULL;
    m_nAudioBuffLen = 0;
}

CLAudioPlay::~CLAudioPlay(void)
{
    stopPlay(); 

    sys_os_destroy_mutex(m_pMutex);
    
    m_pMutex = NULL;
}

BOOL CLAudioPlay::startPlay(int samplerate, int channels)
{
    m_nSamplerate = samplerate;
    m_nChannels = channels;

    strcpy(m_alsa.devname, "default");
    
    if (alsa_open_device(&m_alsa, SND_PCM_STREAM_PLAYBACK, (uint32 *)&m_nSamplerate, m_nChannels) == FALSE)
    {
        log_print(HT_LOG_ERR, "open device (%s) failed\r\n", m_alsa.devname);
        return FALSE;
    }

    m_pAudioBuff = (uint8 *) malloc(m_alsa.period_size * m_alsa.framesize);
    if (NULL == m_pAudioBuff)
    {
        stopPlay();
        log_print(HT_LOG_ERR, "%s, memory malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_bInited = TRUE;
    
    return TRUE;
}

void CLAudioPlay::stopPlay()
{
    sys_os_mutex_enter(m_pMutex);

    m_bInited = FALSE;
    
    alsa_close_device(&m_alsa);

    if (m_pAudioBuff)
    {
        free(m_pAudioBuff);
        m_pAudioBuff = NULL;
    }

    m_nAudioBuffLen = 0;
    
    sys_os_mutex_leave(m_pMutex);
}

BOOL CLAudioPlay::setVolume(int volume)
{
    return FALSE;
}

int CLAudioPlay::getVolume()
{    
    return 0;
}

void CLAudioPlay::playAudio1(uint8 * data, int size) 
{
    int res;
    int frames = size / m_alsa.framesize;
    uint8 * buff = data;
    
    while (frames > 0) 
    {
        res = snd_pcm_writei(m_alsa.handler, buff, frames);
        if (res < 0)
        {
            if (res == -EAGAIN)
            {
                usleep(1000);
                continue;
            }
            
            if (alsa_xrun_recover(m_alsa.handler, res) < 0)
            {
                break;
            }
        }
        else if (res == 0)
        {
            usleep(1000);
        }
        else
        {
            buff += res * m_alsa.framesize;
            frames -= res;
        }
    }
}

void CLAudioPlay::playAudio(uint8 * data, int size)
{
    sys_os_mutex_enter(m_pMutex);
    
    if (!m_bInited || NULL == m_pAudioBuff)
    {
        sys_os_mutex_leave(m_pMutex);
        return;
    }
    
    uint32 specsize = m_alsa.period_size * m_alsa.framesize;
    
    while (m_nAudioBuffLen + size >= specsize)
    {
        memcpy(m_pAudioBuff + m_nAudioBuffLen, data, specsize - m_nAudioBuffLen);

        playAudio1(m_pAudioBuff, specsize);

        size -= specsize - m_nAudioBuffLen;
        data += specsize - m_nAudioBuffLen;
        
        m_nAudioBuffLen = 0;
    }

    if (size > 0)
    {
        memcpy(m_pAudioBuff + m_nAudioBuffLen, data, size);
        m_nAudioBuffLen += size;
    }

    sys_os_mutex_leave(m_pMutex);
}



