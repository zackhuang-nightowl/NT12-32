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

#ifndef _VIDIO_CAPTURE_H
#define _VIDIO_CAPTURE_H

/***************************************************************************************/

#ifdef ANDROID
#include "video_encoder_android.h"
#else
#include "video_decoder.h"
#include "video_encoder.h"
#endif

/***************************************************************************************/

#define MAX_VIDEO_DEV_NUMS  8

/***************************************************************************************/

void VideoDecodeCb(AVFrame * frame, void *pUserdata);

/***************************************************************************************/

class CVideoCapture
{
public:
    virtual ~CVideoCapture();

    // get video capture devices numbers
    static int      getDeviceNums();

    // get the single instance
    static CVideoCapture * getInstance(int devid);
    
    // free the instance
    virtual void    freeInstance(int devid);

    virtual BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    virtual BOOL    startCapture();

    virtual int     getWidth() {return m_nWidth;}
    virtual int     getHeight() {return m_nHeight;}
    virtual void    getAuxSDPLine(char * buff, int size, int rtp_pt);

    virtual void    setCodecFlag(int codec);
    virtual int     getVideoCodec(int video_foramt);
    virtual void    videoDecodeCb(AVFrame * frame);
    
    virtual void    addCallback(VideoDataCallback pCallback, void * pUserdata);
    virtual void    delCallback(VideoDataCallback pCallback, void * pUserdata);
    
protected:
    CVideoCapture();
    CVideoCapture(CVideoCapture &obj);    

    virtual BOOL    capture();
    virtual void    stopCapture();        

public:
    int                     m_nDevIndex;
    int                     m_nRefCnt;

    static void           * m_pInstMutex;
    static CVideoCapture  * m_pInstance[MAX_VIDEO_DEV_NUMS];
    
protected:    
    int                     m_nWidth;
    int                     m_nHeight;
    double                  m_nFramerate;
    int                     m_nBitrate;
    int                     m_nVideoFormat;
    BOOL                    m_bDecode;
    BOOL                    m_bEncode;
    
#ifdef ANDROID
    CAndroidVideoEncoder    m_encoder;
#else
    CVideoDecoder           m_decoder;
    CVideoEncoder           m_encoder;
#endif

    void                  * m_pMutex;
    BOOL                    m_bInited;
    BOOL                    m_bCapture;
    pthread_t               m_hCapture;
};

#endif // _VIDIO_CAPTURE_H


