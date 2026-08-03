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
#include "video_capture.h"
#include "media_format.h"
#include "lock.h"

/**************************************************************************************/

CVideoCapture * CVideoCapture::m_pInstance[] = {NULL, NULL, NULL, NULL};
void * CVideoCapture::m_pInstMutex = sys_os_create_mutex();

/**************************************************************************************/

void VideoDecodeCb(AVFrame * frame, void *pUserdata)
{
    CVideoCapture *capture = (CVideoCapture *)pUserdata;

    capture->videoDecodeCb(frame);
}

/**************************************************************************************/

CVideoCapture::CVideoCapture(void)
{
    m_nDevIndex = 0;    
    m_nWidth = 640;
    m_nHeight = 480;
    m_nFramerate = 15;
    m_nBitrate = 0;
    m_nVideoFormat = VIDEO_FMT_NONE;
    m_bDecode = FALSE;
    m_bEncode = FALSE;

    m_pMutex = sys_os_create_mutex();
    
    m_bInited = FALSE;
    m_bCapture = FALSE;
    m_hCapture = 0;
    
    m_nRefCnt = 0;
}

CVideoCapture::~CVideoCapture(void)
{
    sys_os_destroy_mutex(m_pMutex);
}

CVideoCapture * CVideoCapture::getInstance(int devid)
{
    return NULL;
}

void CVideoCapture::freeInstance(int devid)
{
    if (devid < 0 || devid >= MAX_VIDEO_DEV_NUMS)
    {
        return;
    }
    
    if (m_pInstance[devid])
    {
        sys_os_mutex_enter(m_pInstMutex);

        if (m_pInstance[devid])
        {
            m_pInstance[devid]->m_nRefCnt--;

            if (m_pInstance[devid]->m_nRefCnt <= 0)
            {
                delete m_pInstance[devid];
                m_pInstance[devid] = NULL;
            }
        }

        sys_os_mutex_leave(m_pInstMutex);
    }
}

int CVideoCapture::getDeviceNums()
{
    return 0;
}

void CVideoCapture::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    m_encoder.getAuxSDPLine(buff, size, rtp_pt);
}

void CVideoCapture::setCodecFlag(int codec)
{
    if (VIDEO_FMT_MJPG == m_nVideoFormat)
    {
        if (VIDEO_CODEC_JPEG == codec)
        {
            m_bDecode = FALSE;
            m_bEncode = FALSE;
        }
        else
        {
            m_bDecode = TRUE;
            m_bEncode = TRUE;
        }
    }
    else if (VIDEO_FMT_NONE != m_nVideoFormat)
    {
        m_bDecode = FALSE;
        m_bEncode = TRUE;
    }
}

int CVideoCapture::getVideoCodec(int video_foramt)
{
    if (VIDEO_FMT_MJPG == video_foramt)
    {
        return VIDEO_CODEC_JPEG;
    }
    else
    {
        return VIDEO_CODEC_NONE;
    }
}

void CVideoCapture::videoDecodeCb(AVFrame * frame)
{
    if (m_bEncode)
    {
#ifdef ANDROID    
#else
        m_encoder.encode(frame);
#endif
    }
}

BOOL CVideoCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    return FALSE;
}

BOOL CVideoCapture::capture()
{
    return FALSE;
}

BOOL CVideoCapture::startCapture()
{
    return FALSE;
}

void CVideoCapture::stopCapture()
{
}

void CVideoCapture::addCallback(VideoDataCallback pCallback, void * pUserdata)
{
    m_encoder.addCallback(pCallback, pUserdata);
}

void CVideoCapture::delCallback(VideoDataCallback pCallback, void * pUserdata)
{
    m_encoder.delCallback(pCallback, pUserdata);
}




