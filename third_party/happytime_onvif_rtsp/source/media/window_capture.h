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

#ifndef WINDOW_CAPTURE_H
#define WINDOW_CAPTURE_H

#include "video_encoder.h"
#include <map>
#include <string>

/***************************************************************************************/

typedef std::map<std::string, void*> InstanceMap;

/***************************************************************************************/

class CWindowCapture
{
public:
    virtual ~CWindowCapture();

    static CWindowCapture * getInstance(char * title);
    
    virtual void    freeInstance(char * title);
    
    virtual BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    virtual BOOL    startCapture();

    virtual void    addCallback(VideoDataCallback pCallback, void * pUserdata);
    virtual void    delCallback(VideoDataCallback pCallback, void * pUserdata);

    virtual int     getWidth() {return m_nWidth;}
    virtual int     getHeight() {return m_nHeight;}
    virtual void    getAuxSDPLine(char * buff, int size, int rtp_pt);
    
protected:
    CWindowCapture();
    CWindowCapture(CWindowCapture &obj);

    virtual BOOL    capture();
    virtual void    stopCapture();

public:
    int                     m_nRefCnt;
    char                    m_pTitle[256];

    static void           * m_pInstMutex;
    static InstanceMap      m_InstanceMap;
    
protected:
    int                     m_nWidth;
    int                     m_nHeight;
    int                     m_nPixfmt;
    double                  m_nFramerate;
    int                     m_nBitrate;

    CVideoEncoder           m_encoder;

    void                  * m_pMutex;
    BOOL                    m_bInited;
    BOOL                    m_bCapture;
    pthread_t               m_hCapture;
};

#endif



