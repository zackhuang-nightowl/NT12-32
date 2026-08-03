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
#include "audio_capture_mac.h"
#include "lock.h"


/***************************************************************************************/

static void avfAudioCallback(avf_audio_data * data, void * userdata)
{
    CMAudioCapture *capture = (CMAudioCapture *)userdata;

    capture->audioCallBack(data);
}

/***************************************************************************************/

CMAudioCapture::CMAudioCapture() : CAudioCapture()
{
    m_pCapture = NULL;
}

CMAudioCapture::~CMAudioCapture()
{
    stopCapture();
}

CAudioCapture * CMAudioCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_AUDIO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = new CMAudioCapture;
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

int CMAudioCapture::getDeviceNums()
{
    int count = avf_audio_device_nums();
    
    return count > MAX_AUDIO_DEV_NUMS ? MAX_AUDIO_DEV_NUMS : count;
}

void CMAudioCapture::listDevice()
{
    avf_audio_device_list();
}

int CMAudioCapture::getDeviceIndex(const char * name)
{
    int index = avf_audio_device_get_index(name);

    return index > MAX_AUDIO_DEV_NUMS ? 0 : index;
}

BOOL CMAudioCapture::getDeviceName(int index, char * name, int namesize)
{
    return avf_audio_device_get_name(index, name, namesize);
}

BOOL CMAudioCapture::initCapture(int codec, int samplerate, int channels, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_pCapture = avf_audio_init(m_nDevIndex, samplerate, 2);
    if (NULL == m_pCapture)
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    avf_audio_set_callback(m_pCapture, avfAudioCallback, this);
    
    AudioEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcChannels = avf_audio_get_channels(m_pCapture);
    params.SrcSamplefmt = (AVSampleFormat)avf_audio_get_samplefmt(m_pCapture);
    params.SrcSamplerate = avf_audio_get_samplerate(m_pCapture);
    params.DstChannels = channels;
    params.DstSamplefmt = AV_SAMPLE_FMT_S16;
    params.DstSamplerate = samplerate;
    params.DstBitrate = bitrate;
    params.DstCodec = codec;

    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }

    m_nChannels = params.SrcChannels;
    m_nSampleRate = params.SrcSamplerate;
    m_nBitrate = bitrate;
    m_bInited = TRUE;
    
    return TRUE;
}

void CMAudioCapture::audioCallBack(avf_audio_data * data)
{
    int i;
    AVFrame frame;

    CLock lock(m_pMutex);

    if (!m_bCapture)
    {
        return;
    }
    
    memset(&frame, 0, sizeof(frame));

    for (i = 0; i < 4; i++)
    {
        frame.data[i] = data->data[i];
        frame.linesize[i] = data->linesize[i];
    }

    frame.extended_data = frame.data;
    frame.sample_rate = data->samplerate;
    frame.channels = data->channels;
    frame.format = data->format;
    frame.nb_samples = data->samples;
    frame.channel_layout = av_get_default_channel_layout(data->channels);
    frame.key_frame = 1;
    
    m_encoder.encode(&frame);
}

BOOL CMAudioCapture::startCapture()
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
    
    return TRUE;
}

void CMAudioCapture::stopCapture(void)
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    if (m_pCapture)
    {
        avf_audio_uninit(m_pCapture);
        m_pCapture = NULL;
    }

    m_bInited = FALSE;
}



