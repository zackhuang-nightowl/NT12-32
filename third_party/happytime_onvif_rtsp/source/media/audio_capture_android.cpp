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
#include "lock.h"
#include "audio_capture_android.h"


void dataCallback(uint8 * pdata, int len, void * puser)
{
    CAAudioCapture * pthis = (CAAudioCapture *) puser;
    
    pthis->dataCallback(pdata, len);
}

CAAudioCapture::CAAudioCapture()
: CAudioCapture()
, m_pInput(0)
{
}

CAAudioCapture::~CAAudioCapture()
{
    stopCapture();
}

CAudioCapture * CAAudioCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_AUDIO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = new CAAudioCapture;
        if (m_pInstance[devid])
        {
            m_pInstance[devid]->m_nRefCnt++;
            m_pInstance[devid]->m_nDevIndex = devid;
        }
    }
    else
    {
        m_pInstance[devid]->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return m_pInstance[devid];
}

int CAAudioCapture::getDeviceNums()
{
    return 1;
}

void CAAudioCapture::listDevice()
{
}

int CAAudioCapture::getDeviceIndex(const char * name)
{
    return 0;
}

BOOL CAAudioCapture::getDeviceName(int index, char * name, int namesize)
{
    strncpy(name, "Default", namesize);
    
    return TRUE;
}

BOOL CAAudioCapture::initCapture(int codec, int samplerate, int channels, int bitrate)
{
    CLock lock(m_pMutex);

    if (m_bInited)
    {
        return TRUE;
    }
    
    m_nChannels = channels;
    m_nSampleRate = samplerate;
    m_nBitrate = bitrate;

    AudioEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcChannels = 2;
    params.SrcSamplefmt = AV_SAMPLE_FMT_S16;
    params.SrcSamplerate = samplerate;
    params.DstChannels = channels;
    params.DstSamplefmt = AV_SAMPLE_FMT_S16;
    params.DstSamplerate = samplerate;
    params.DstBitrate = bitrate;
    params.DstCodec = codec;

    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }

    m_bInited = TRUE;

    return TRUE;
}

BOOL CAAudioCapture::startCapture()
{    
    CLock lock(m_pMutex);

    if (!m_bInited)
    {
        return FALSE;
    }

    if (m_pInput)
    {
        return TRUE;
    }
    
    m_pInput = new GlesInput();
    if (NULL == m_pInput)
    {
        return FALSE;
    }

    m_pInput->setCallback(::dataCallback, this);
    if (!m_pInput->start(m_nSampleRate))
    {
        log_print(HT_LOG_ERR, "%s, start audio recoding failed\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

void CAAudioCapture::stopCapture(void)
{
    CLock lock(m_pMutex);
    
    if (m_pInput)
    {
        delete m_pInput;
        m_pInput = NULL;
    }

    m_bInited = FALSE;
}

void CAAudioCapture::dataCallback(uint8 * pdata, int len)
{
    m_encoder.encode(pdata, len);
}



