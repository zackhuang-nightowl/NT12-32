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
#include "window_capture_mac.h"
#include "media_format.h"
#include "lock.h"


/**************************************************************************************/

static void avfWindowCallback(avf_window_data * data, void * userdata)
{
    CMWindowCapture *capture = (CMWindowCapture *)userdata;

    capture->windowCallBack(data);
}

/**************************************************************************************/

CMWindowCapture::CMWindowCapture()  : CWindowCapture()
{
    m_pCapture = NULL;
}

CMWindowCapture::~CMWindowCapture()
{
    stopCapture();
}

CWindowCapture * CMWindowCapture::getInstance(char * title)
{
    CWindowCapture * pCapture = NULL;
    InstanceMap::iterator it;
    
    sys_os_mutex_enter(m_pInstMutex);

    it = m_InstanceMap.find(title);
    
    if (it == m_InstanceMap.end())
    {
        pCapture = (CWindowCapture *) new CMWindowCapture;
        if (pCapture)
        {
            pCapture->m_nRefCnt++;
            strcpy(pCapture->m_pTitle, title);

            m_InstanceMap[title] = pCapture;
        }
    }
    else
    {
        pCapture = (CWindowCapture *) it->second;
        pCapture->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return pCapture;
}

void CMWindowCapture::listWindow()
{
    avf_window_list();
}

BOOL CMWindowCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_pCapture = avf_window_init(m_pTitle, width, height, framerate);
    if (NULL == m_pCapture)
    {
        log_print(HT_LOG_ERR, "%s, avf_window_init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nWidth = avf_window_get_width(m_pCapture);
    m_nHeight = avf_window_get_height(m_pCapture);
    m_nPixfmt = avf_window_get_pixfmt(m_pCapture);

    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    avf_window_set_callback(m_pCapture, avfWindowCallback, this);
    
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

BOOL CMWindowCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;

    return avf_window_start(m_pCapture);
}

void CMWindowCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    if (m_pCapture)
    {
        avf_window_uninit(m_pCapture);
        m_pCapture = NULL;
    }

    m_bInited = FALSE;
}

void CMWindowCapture::windowCallBack(avf_window_data * data)
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
    frame.width = data->width;
    frame.height = data->height;
    frame.format = data->format;

    m_encoder.encode(&frame);
}



