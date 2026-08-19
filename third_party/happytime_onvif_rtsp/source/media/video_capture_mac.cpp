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
#include "video_capture_mac.h"
#include "media_format.h"
#include "media_codec.h"
#include "lock.h"


/***************************************************************************************/

static void avfVideoCallback(avf_video_data * data, void * userdata)
{
    CMVideoCapture *capture = (CMVideoCapture *)userdata;

    capture->videoCallBack(data);
}

static void * videoCaptureThread(void * argv)
{
    CMVideoCapture *capture = (CMVideoCapture *)argv;

    capture->captureThread();

    return NULL;
}

CMVideoCapture::CMVideoCapture(void) : CVideoCapture()
{
    m_pCapture = NULL;
}

CMVideoCapture::~CMVideoCapture(void)
{
    stopCapture();
}

CVideoCapture * CMVideoCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_VIDEO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CVideoCapture *)new CMVideoCapture;
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

int CMVideoCapture::getDeviceNums()
{
    int count = avf_video_device_nums();
    
    return count > MAX_VIDEO_DEV_NUMS ? MAX_VIDEO_DEV_NUMS : count;
}

void CMVideoCapture::listDevice()
{
    avf_video_device_list();
}

int CMVideoCapture::getDeviceIndex(const char * name)
{
    int index = avf_video_device_get_index(name);

    return index > MAX_VIDEO_DEV_NUMS ? 0 : index;
}

BOOL CMVideoCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }
    
    m_pCapture = avf_video_init(m_nDevIndex, width, height, framerate);
    if (NULL == m_pCapture)
    {
        log_print(HT_LOG_ERR, "%s, avf_video_init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nFramerate = framerate;
    m_nBitrate = bitrate;
    
    m_nWidth = avf_video_get_width(m_pCapture);
    m_nHeight = avf_video_get_height(m_pCapture);

    avf_video_set_callback(m_pCapture, avfVideoCallback, this);

    VideoEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcWidth = m_nWidth;
    params.SrcHeight = m_nHeight;
    params.SrcPixFmt = (AVPixelFormat)avf_video_get_pixfmt(m_pCapture);
    params.DstCodec = codec;
    params.DstWidth = width ? width : m_nWidth;
    params.DstHeight = height ? height : m_nHeight;
    params.DstFramerate = m_nFramerate;
    params.DstBitrate = bitrate;
        
    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }
    
    m_bInited = TRUE;

    return TRUE;
}

void CMVideoCapture::videoCallBack(avf_video_data * data)
{
    int i;
    AVFrame frame;
    
    memset(&frame, 0, sizeof(frame));

    for (i = 0; i < 4; i++)
    {
        frame.data[i] = data->data[i];
        frame.linesize[i] = data->linesize[i];
    }

    frame.extended_data = frame.data;
    frame.width = data->width;
    frame.height = data->height;
    frame.format = data->format;

    m_encoder.encode(&frame);
}

BOOL CMVideoCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }

    BOOL ret = avf_video_read(m_pCapture);

    return ret;    
}

BOOL CMVideoCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)videoCaptureThread, this);

    return (NULL != m_hCapture);
}

void CMVideoCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    // wait for capture thread exit
    sys_os_wait_thread(&m_hCapture);

    if (m_pCapture)
    {
        avf_video_uninit(m_pCapture);
        m_pCapture = NULL;
    }
    
    m_bInited = FALSE;
}

void CMVideoCapture::captureThread()
{    
    int64 cur_delay = 0;
    int64 pre_delay = 0;
    int timeout = 1000000.0 / m_nFramerate;
    uint32 cur_time = 0;
    uint32 pre_time = 0;
        
    while (m_bCapture)
    {
        if (capture())
        {
            cur_time = sys_os_get_ms();
            cur_delay = timeout;

            if (pre_time > 0)
            {
                cur_delay += pre_delay - (cur_time - pre_time) * 1000;
                if (cur_delay < 1000)
                {
                    cur_delay = 0;
                }
            }

            pre_time = cur_time;
            pre_delay = cur_delay;
            
            if (cur_delay > 0)
            {
                usleep(cur_delay);
            }
        }
        else
        {
            usleep(10*1000);
        }
    }
}





