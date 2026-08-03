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
#include "screen_capture_mac.h"
#include "media_format.h"
#include "lock.h"


/**************************************************************************************/

static void avfScreenCallback(avf_screen_data * data, void * userdata)
{
    CMScreenCapture *capture = (CMScreenCapture *)userdata;

    capture->screenCallBack(data);
}

void * screenCaptureThread(void * argv)
{
    CMScreenCapture *capture = (CMScreenCapture *)argv;

    capture->captureThread();

    return NULL;
}

/**************************************************************************************/

CMScreenCapture::CMScreenCapture()  : CScreenCapture()
{
    m_pCapture = NULL;
}

CMScreenCapture::~CMScreenCapture()
{
    stopCapture();
}

CScreenCapture * CMScreenCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_MONITOR_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CScreenCapture *) new CMScreenCapture;
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

int CMScreenCapture::getDeviceNums()
{
    int count = avf_screen_device_nums();
    
    return count > MAX_MONITOR_NUMS ? MAX_MONITOR_NUMS : count;
}

BOOL CMScreenCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_pCapture = avf_screen_init(m_nDevIndex, width, height, framerate);
    if (NULL == m_pCapture)
    {
        log_print(HT_LOG_ERR, "%s, avf_screen_init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nWidth = avf_screen_get_width(m_pCapture);
    m_nHeight = avf_screen_get_height(m_pCapture);
    m_nPixfmt = avf_screen_get_pixfmt(m_pCapture);

    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    avf_screen_set_callback(m_pCapture, avfScreenCallback, this);
    
    VideoEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcWidth = m_nWidth;
    params.SrcHeight = m_nHeight;
    params.SrcPixFmt = (AVPixelFormat) m_nPixfmt;
    params.DstCodec = codec;
    params.DstWidth = width ? width : m_nWidth;
    params.DstHeight = height ? height : m_nHeight;
    params.DstFramerate = framerate;
    params.DstBitrate = bitrate;
    
    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }
    
    m_bInited = TRUE;
       
    return TRUE;
}

BOOL CMScreenCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)screenCaptureThread, this);

    return m_hCapture ? TRUE : FALSE;
}

void CMScreenCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    if (m_pCapture)
    {
        avf_screen_uninit(m_pCapture);
        m_pCapture = NULL;
    }

    m_bInited = FALSE;
}

void CMScreenCapture::screenCallBack(avf_screen_data * data)
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

BOOL CMScreenCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }
    
    BOOL ret = avf_screen_read(m_pCapture);
    
    return ret;
}

void CMScreenCapture::captureThread()
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




