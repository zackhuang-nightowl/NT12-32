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
#include "audio_play_win.h"

/**************************************************************************************/

#pragma comment(lib, "dsound.lib")


/**************************************************************************************/

CWAudioPlay::CWAudioPlay()
: CAudioPlay()
, m_pDSound8(NULL)
, m_pDSoundBuffer(NULL)
, m_pMutex(NULL)
, m_pAudioBuff(NULL)
, m_nAudioBuffLen(0)
, m_nLastChunk(0)
, m_nBufferNums(8)
, m_nSampleNums(1024)
, m_nSpecSize(0)
{
    m_pMutex = sys_os_create_mutex();
}

CWAudioPlay::~CWAudioPlay(void)
{
    stopPlay();

    sys_os_destroy_mutex(m_pMutex);
    
    m_pMutex = NULL;
}

BOOL CWAudioPlay::startPlay(int samplerate, int channels)
{
    HRESULT ret = DirectSoundCreate8(NULL, &m_pDSound8, NULL);
    if (FAILED(ret))
    {
        log_print(HT_LOG_ERR, "%s, DirectSoundCreate8 failed\r\n", __FUNCTION__);
        return FALSE;
    }

    ret = m_pDSound8->SetCooperativeLevel(GetDesktopWindow(), DSSCL_NORMAL);
    if (FAILED(ret))
    {
        stopPlay();
        log_print(HT_LOG_ERR, "%s, SetCooperativeLevel failed\r\n", __FUNCTION__);
        return FALSE;
    }

    WAVEFORMATEX format;
    memset(&format, 0, sizeof(WAVEFORMATEX));

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.wBitsPerSample = 16;
    format.nSamplesPerSec = samplerate;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nBlockAlign * format.nSamplesPerSec;   
    format.cbSize = 0;
    
    DSBUFFERDESC buf_desc;
    memset(&buf_desc, 0, sizeof(DSBUFFERDESC));

    m_nSpecSize = m_nSampleNums * channels * 2;
    
    buf_desc.dwSize = sizeof(buf_desc);
    buf_desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;
    buf_desc.dwBufferBytes = m_nBufferNums * m_nSpecSize;
    buf_desc.dwReserved = 0;
    buf_desc.lpwfxFormat = &format;
    
    ret = m_pDSound8->CreateSoundBuffer(&buf_desc, &m_pDSoundBuffer, NULL);
    if (FAILED(ret))
    {
        stopPlay();
        log_print(HT_LOG_ERR, "%s, CreateSoundBuffer failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_pDSoundBuffer->SetFormat(&format);

    void * buf1 = NULL;
    DWORD  len1 = 0;
    void * buf2 = NULL;
    DWORD  len2 = 0;
    
    /* Silence the initial audio buffer */
    ret = m_pDSoundBuffer->Lock(0, buf_desc.dwBufferBytes, &buf1, &len1, &buf2, &len2, DSBLOCK_ENTIREBUFFER);
    if (ret == DS_OK) 
    {
        memset(buf1, 0, len1);

        m_pDSoundBuffer->Unlock(buf1, len1, buf2, len2);
    }

    m_pAudioBuff = (uint8 *) malloc(m_nSpecSize);
    if (NULL == m_pAudioBuff)
    {
        stopPlay();
        log_print(HT_LOG_ERR, "%s, memory malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    m_nSamplerate = samplerate;
    m_nChannels = channels;
    m_bInited = TRUE;
    
    return TRUE;
}

void CWAudioPlay::stopPlay()
{
    sys_os_mutex_enter(m_pMutex);
    
    if (m_pDSoundBuffer)
    {
        m_pDSoundBuffer->Stop();
        m_pDSoundBuffer->Release();
        m_pDSoundBuffer = NULL;
    }

    if (m_pDSound8)
    {
        m_pDSound8->Release();
        m_pDSound8 = NULL;
    }

    if (m_pAudioBuff)
    {
        free(m_pAudioBuff);
        m_pAudioBuff = NULL;
    }

    m_nAudioBuffLen = 0;
    m_bInited = FALSE;

    sys_os_mutex_leave(m_pMutex);
}

BOOL CWAudioPlay::setVolume(int volume)
{
    if (m_pDSoundBuffer)
    {
        double db = (double) volume / (HTVOLUME_MAX - HTVOLUME_MIN);
        if (db < 0)
        {
            db = -db;
        }
            
        int nv = (DSBVOLUME_MAX - DSBVOLUME_MIN) * db + DSBVOLUME_MIN;
            
        HRESULT hr = m_pDSoundBuffer->SetVolume(nv);
        if (DS_OK == hr)
        {
            return TRUE;
        }
    }

    return FALSE;
}

int CWAudioPlay::getVolume()
{
    if (m_pDSoundBuffer)
    {
        long volume = 0;
        
        HRESULT ret = m_pDSoundBuffer->GetVolume(&volume);
        if (SUCCEEDED(ret))
        {
            double db = (double) volume / (DSBVOLUME_MAX - DSBVOLUME_MIN);
            if (db < 0)
            {
                db = -db;
            }
            
            int nv = (HTVOLUME_MAX - HTVOLUME_MIN) * db + HTVOLUME_MIN;
            
            return nv;
        }
    }

    return HTVOLUME_MIN;
}

void CWAudioPlay::playAudio1(uint8 * data, int size) 
{
    DWORD cursor = 0;
    DWORD junk = 0;
    HRESULT result = DS_OK;
    DWORD rawlen = 0;
    void * bufptr = NULL;

    /* Figure out which blocks to fill next */
    result = m_pDSoundBuffer->GetCurrentPosition(&junk, &cursor);
    if (result == DSERR_BUFFERLOST) 
    {
        m_pDSoundBuffer->Restore();
        result = m_pDSoundBuffer->GetCurrentPosition(&junk, &cursor);
    }
    
    if (result != DS_OK) 
    {
        return;
    }
    
    cursor /= m_nSpecSize;

    m_nLastChunk = cursor;
    cursor = (cursor + 1) % m_nBufferNums;
    cursor *= m_nSpecSize;
    
    /* Lock the audio buffer */
    result = m_pDSoundBuffer->Lock(cursor, m_nSpecSize, &bufptr, &rawlen, NULL, &junk, 0);
    if (result == DSERR_BUFFERLOST) 
    {
        m_pDSoundBuffer->Restore();
        result = m_pDSoundBuffer->Lock(cursor, m_nSpecSize, &bufptr, &rawlen, NULL, &junk, 0);
    }
    
    if (result != DS_OK) 
    {
        return;
    }

    if (bufptr && rawlen > 0)
    {
        memcpy(bufptr, data, rawlen);
    }

    m_pDSoundBuffer->Unlock(bufptr, rawlen, NULL, 0);

    waitDevice();
} 

void CWAudioPlay::playAudio(uint8 * data, int size) 
{
    sys_os_mutex_enter(m_pMutex);

    if (!m_bInited || NULL == m_pAudioBuff)
    {
        sys_os_mutex_leave(m_pMutex);
        return;
    }
    
    while (m_nAudioBuffLen + size >= (uint32)m_nSpecSize)
    {
        memcpy(m_pAudioBuff + m_nAudioBuffLen, data, m_nSpecSize - m_nAudioBuffLen);

        playAudio1(m_pAudioBuff, m_nSpecSize);

        size -= m_nSpecSize - m_nAudioBuffLen;
        data += m_nSpecSize - m_nAudioBuffLen;
        
        m_nAudioBuffLen = 0;
    }

    if (size > 0)
    {
        memcpy(m_pAudioBuff + m_nAudioBuffLen, data, size);
        m_nAudioBuffLen += size;
    }

    sys_os_mutex_leave(m_pMutex);
} 

void CWAudioPlay::waitDevice()
{
    DWORD status = 0;
    DWORD cursor = 0;
    DWORD junk = 0;
    HRESULT result = DS_OK;

    result = m_pDSoundBuffer->GetCurrentPosition(&junk, &cursor);
    if (result != DS_OK) 
    {
        if (result == DSERR_BUFFERLOST) 
        {
            m_pDSoundBuffer->Restore();
        }
        return;
    }
    
    while ((cursor / m_nSpecSize) == m_nLastChunk) 
    {
        usleep(1000);

        /* Try to restore a lost sound buffer */
        m_pDSoundBuffer->GetStatus(&status);
        
        if (status & DSBSTATUS_BUFFERLOST)
        {
            m_pDSoundBuffer->Restore();
            m_pDSoundBuffer->GetStatus(&status);

            if ((status & DSBSTATUS_BUFFERLOST))
            {
                break;
            }
        }
        
        if (!(status & DSBSTATUS_PLAYING)) 
        {
            result = m_pDSoundBuffer->Play(0, 0, 0);
            if (result == DS_OK) 
            {
                continue;
            }
            
            return;
        }

        /* Find out where we are playing */
        result = m_pDSoundBuffer->GetCurrentPosition(&junk, &cursor);
        if (result != DS_OK) 
        {
            return;
        }
    }
}


