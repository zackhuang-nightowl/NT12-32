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

#ifndef LIVE_VIDEO_H
#define LIVE_VIDEO_H

#include "linked_list.h"

/***************************************************************************************/

#define MAX_LIVE_VIDEO_NUMS  4

typedef void (*LiveVideoDataCB)(uint8 *data, int size, void *pUserdata);

typedef struct
{
    LiveVideoDataCB pCallback;      // callback function pointer
    void          * pUserdata;      // user data
    BOOL            bFirst;         // first flag
} LiveVideoCB;

/***************************************************************************************/


class CLiveVideo
{
public:
    virtual ~CLiveVideo();
    
    // get the support stream numbers
    static int      getStreamNums();
    
    static CLiveVideo * getInstance(int idx);
    
    virtual void    freeInstance(int idx);

    virtual BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    virtual BOOL    startCapture();

    virtual void    addCallback(LiveVideoDataCB pCallback, void * pUserdata);
    virtual void    delCallback(LiveVideoDataCB pCallback, void * pUserdata);
    
    virtual void    getAuxSDPLine(char * buff, int size, int rtp_pt);
    virtual BOOL    captureThread();

    void            procData(uint8 * data, int size);
    
protected:
    CLiveVideo();
    CLiveVideo(CLiveVideo &obj);

    BOOL            isCallbackExist(LiveVideoDataCB pCallback, void *pUserdata);
    
    virtual void    stopCapture();

public:
    int                     m_nStreamIndex;
    int                     m_nRefCnt;

    static void           * m_pInstMutex;
    static CLiveVideo     * m_pInstance[MAX_LIVE_VIDEO_NUMS];
    
protected:
    int                     m_nCodecId;
    int                     m_nWidth;
    int                     m_nHeight;
    double                  m_nFramerate;
    int                     m_nBitrate;
    
    void                  * m_pMutex;
    BOOL                    m_bInited;
    BOOL                    m_bCapture;
    pthread_t               m_hCapture;

    void                  * m_pCallbackMutex;
    LKLIST                * m_pCallbackList;
};

/*
 * After getting the encoded data from the hardware encoder, 
 * you can call this interface to send the data to the video sending queue 
 *
 * If you use this interface to send video data, you do not need to 
 * implement the CLiveVideo::captureThread function 
 */
BOOL media_live_put_video(int idx, uint8 * data, int size);

#endif



