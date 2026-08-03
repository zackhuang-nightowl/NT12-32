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
#include "window_capture.h"
#include "media_format.h"
#include "lock.h"

/**************************************************************************************/

void * CWindowCapture::m_pInstMutex = sys_os_create_mutex();
InstanceMap CWindowCapture::m_InstanceMap;

/**************************************************************************************/

CWindowCapture::CWindowCapture()
{
    m_nWidth = 0;
    m_nHeight = 0;
    m_nPixfmt = 0;
    m_nFramerate = 15;
    m_nBitrate = 0;

    m_pMutex = sys_os_create_mutex();
    
    m_bInited = FALSE;
    m_bCapture = FALSE;
    m_hCapture = 0;
    
    m_nRefCnt = 0;
    memset(m_pTitle, 0, sizeof(m_pTitle));
}

CWindowCapture::~CWindowCapture()
{
    sys_os_destroy_mutex(m_pMutex);
}

CWindowCapture * CWindowCapture::getInstance(char * title)
{
    return NULL;
}

void CWindowCapture::freeInstance(char * title)
{
    InstanceMap::iterator it = m_InstanceMap.find(title);
    
    if (it != m_InstanceMap.end())
    {
        sys_os_mutex_enter(m_pInstMutex);

        it = m_InstanceMap.find(title);
        
        if (it != m_InstanceMap.end())
        {
            CWindowCapture * pCapture = (CWindowCapture *) it->second;
            
            pCapture->m_nRefCnt--;

            if (pCapture->m_nRefCnt <= 0)
            {
                delete pCapture;
                
                m_InstanceMap.erase(it);
            }
        }

        sys_os_mutex_leave(m_pInstMutex);
    }
}

BOOL CWindowCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    return FALSE;
}

void CWindowCapture::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    m_encoder.getAuxSDPLine(buff, size, rtp_pt);
}

BOOL CWindowCapture::startCapture()
{
    return FALSE;
}

void CWindowCapture::stopCapture()
{
    
}

BOOL CWindowCapture::capture()
{
    return FALSE;
}

void CWindowCapture::addCallback(VideoDataCallback pCallback, void * pUserdata)
{
    m_encoder.addCallback(pCallback, pUserdata);
}

void CWindowCapture::delCallback(VideoDataCallback pCallback, void * pUserdata)
{
    m_encoder.delCallback(pCallback, pUserdata);
}



