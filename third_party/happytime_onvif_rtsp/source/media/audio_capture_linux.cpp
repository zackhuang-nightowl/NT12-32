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
#include "audio_capture_linux.h"
#include "lock.h"


/***************************************************************************************/
static void * audioCaptureThread(void * argv)
{
    CLAudioCapture *capture = (CLAudioCapture *)argv;

    capture->captureThread();

    return NULL;
}

/***************************************************************************************/

CLAudioCapture::CLAudioCapture() : CAudioCapture()
{
    memset(&m_alsa, 0, sizeof(m_alsa));
    
    m_alsa.period_size = 64;
    m_alsa.framesize = 16 / 8 * 2;
    
    m_pData = NULL;
}

CLAudioCapture::~CLAudioCapture()
{
    stopCapture();
}

CAudioCapture * CLAudioCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_AUDIO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = new CLAudioCapture();
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

int CLAudioCapture::getDeviceNums()
{
    int card = -1, count = 0, ret;
    snd_ctl_card_info_t * info;

    snd_ctl_card_info_alloca(&info);
    
    ret = snd_card_next(&card);
    
    while (ret == 0 && card != -1) 
    {
        char name[16];
        snd_ctl_t * ctl;
        
        snprintf(name, sizeof(name), "hw:%d", card);
        
        if (snd_ctl_open(&ctl, name, SND_CTL_NONBLOCK) < 0)
        {
            ret = snd_card_next(&card);
            continue;
        }

        if (snd_ctl_card_info(ctl, info) < 0)
        {
            snd_ctl_close(ctl);
            ret = snd_card_next(&card);
            continue;
        }

        snd_ctl_close(ctl);
        
        if (++count >= MAX_AUDIO_DEV_NUMS)
        {
            break;
        }
                
        ret = snd_card_next(&card);
    }
    
    return count > MAX_AUDIO_DEV_NUMS ? MAX_AUDIO_DEV_NUMS : count;
}

void CLAudioCapture::listDevice()
{
    int card = -1, count = 0, ret;
    snd_ctl_card_info_t * info;

    printf("\r\nAvailable audio capture device : \r\n\r\n");

    snd_ctl_card_info_alloca(&info);
    
    ret = snd_card_next(&card);
    
    while (ret == 0 && card != -1) 
    {
        char name[16];
        snd_ctl_t * ctl;
        
        snprintf(name, sizeof(name), "hw:%d", card);
        
        if (snd_ctl_open(&ctl, name, SND_CTL_NONBLOCK) < 0)
        {
            ret = snd_card_next(&card);
            continue;
        }

        if (snd_ctl_card_info(ctl, info) < 0)
        {
            snd_ctl_close(ctl);
            ret = snd_card_next(&card);
            continue;
        }
        
        printf("index : %d, name : %s\r\n", count, snd_ctl_card_info_get_name(info));

        snd_ctl_close(ctl);
        
        if (++count >= MAX_AUDIO_DEV_NUMS)
        {
            break;
        }
                
        ret = snd_card_next(&card);
    }
}

int CLAudioCapture::getDeviceIndex(const char * name)
{
    int card = -1, count = 0, index = 0, ret;
    snd_ctl_card_info_t * info;

    snd_ctl_card_info_alloca(&info);
    
    ret = snd_card_next(&card);

    while (ret == 0 && card != -1) 
    {
        char cname[16];
        snd_ctl_t * ctl;
        
        snprintf(cname, sizeof(cname), "hw:%d", card);
        
        if (snd_ctl_open(&ctl, cname, SND_CTL_NONBLOCK) < 0)
        {
            ret = snd_card_next(&card);
            continue;
        }

        if (snd_ctl_card_info(ctl, info) < 0)
        {
            snd_ctl_close(ctl);
            ret = snd_card_next(&card);
            continue;
        }
        
        if (strcasecmp(snd_ctl_card_info_get_name(info), name) == 0)
        {
            index = count;
            snd_ctl_close(ctl);
            break;
        }

        snd_ctl_close(ctl);
        
        if (++count >= MAX_AUDIO_DEV_NUMS)
        {
            break;
        }
                
        ret = snd_card_next(&card);
    }

    return index;
}

BOOL CLAudioCapture::getDeviceName(int index, char * name, int namesize)
{
    BOOL res = FALSE;
    int card = -1, count = 0, ret;
    snd_ctl_card_info_t * info;

    snd_ctl_card_info_alloca(&info);
    
    ret = snd_card_next(&card);

    while (ret == 0 && card != -1) 
    {
        char cname[16];
        snd_ctl_t * ctl;
        
        snprintf(cname, sizeof(cname), "hw:%d", card);
        
        if (snd_ctl_open(&ctl, cname, SND_CTL_NONBLOCK) < 0)
        {
            ret = snd_card_next(&card);
            continue;
        }

        if (snd_ctl_card_info(ctl, info) < 0)
        {
            snd_ctl_close(ctl);
            ret = snd_card_next(&card);
            continue;
        }
        
        if (index == count)
        {
            res = TRUE;
            strncpy(name, snd_ctl_card_info_get_name(info), namesize);
            snd_ctl_close(ctl);
            break;
        }

        snd_ctl_close(ctl);
        
        if (++count >= MAX_AUDIO_DEV_NUMS)
        {
            break;
        }
                
        ret = snd_card_next(&card);
    }

    return res;
}

BOOL CLAudioCapture::initCapture(int codec, int samplerate, int channels, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    if (alsa_get_device_name(m_nDevIndex, m_alsa.devname, sizeof(m_alsa.devname)) == FALSE)
    {
        log_print(HT_LOG_ERR, "get device name failed. %d\r\n", m_nDevIndex);
        return FALSE;
    }
    
    if (alsa_open_device(&m_alsa, SND_PCM_STREAM_CAPTURE, (uint32 *)&samplerate, 2) == FALSE)
    {
        log_print(HT_LOG_ERR, "open device (%s) failed\r\n", m_alsa.devname);
        return FALSE;
    }

    m_pData = (uint8 *)malloc(ALSA_BUFFER_SIZE_MAX);
    if (NULL == m_pData)
    {
        return FALSE;
    }
    memset(m_pData, 0, ALSA_BUFFER_SIZE_MAX);
    
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

    m_nChannels = channels;
    m_nSampleRate = samplerate;
    m_nBitrate = bitrate;
    m_bInited = TRUE;
    
    return TRUE;
}

BOOL CLAudioCapture::capture()
{
    int res = 0;

    if (NULL == m_alsa.handler)
    {
        return FALSE;
    }

    while (m_bCapture)
    {
        res = snd_pcm_wait(m_alsa.handler, 100);
        if (res < 0)
        {
            if (alsa_xrun_recover(m_alsa.handler, res) < 0)
            {
                return FALSE;
            }
        }

        res = snd_pcm_readi(m_alsa.handler, m_pData, m_alsa.period_size);
        if (res == -EAGAIN)
        {
            continue;
        }
        else if (res < 0)
        {
            if (alsa_xrun_recover(m_alsa.handler, res) < 0)
            {
                return FALSE;
            }
        }
        else if (res > 0)
        {
            break;
        }
    }
    
    return m_encoder.encode(m_pData, res * m_alsa.framesize);
}

void CLAudioCapture::captureThread()
{
    while (m_bCapture)
    {
        if (capture())
        {
        }
        else
        {
            usleep(10*1000);
        }
    }

    log_print(HT_LOG_INFO, "%s, exit\r\n", __FUNCTION__);
}

BOOL CLAudioCapture::startCapture()
{
    CLock lock(m_pMutex);

    if (!m_bInited)
    {
        return FALSE;
    }
    
    if (m_bCapture)
    {
        return TRUE;
    }
    
    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)audioCaptureThread, this);

    return TRUE;
}

void CLAudioCapture::stopCapture(void)
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    alsa_close_device(&m_alsa);

    if (m_pData)
    {
        free(m_pData);
        m_pData = NULL;
    }

    m_bInited = FALSE;
}







