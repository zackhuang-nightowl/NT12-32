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

#ifndef LIVE_AUDIO_H
#define LIVE_AUDIO_H

/***************************************************************************************/

#include "linked_list.h"

/***************************************************************************************/

#define MAX_LIVE_AUDIO_NUMS  4

typedef void (*LiveAudioDataCB)(uint8 *data, int size, int nbsamples, void *pUserdata);

typedef struct
{
    LiveAudioDataCB pCallback;    // callback function pointer
    void          * pUserdata;    // user data
    BOOL            bFirst;       // first flag
} LiveAudioCB;

/***************************************************************************************/

class CLiveAudio
{
public:
    virtual ~CLiveAudio();
    
    // get the support stream numbers     
    static int      getStreamNums();

    // get single instance
    static CLiveAudio * getInstance(int idx);
    
    // free instance
    virtual void    freeInstance(int idx);

    virtual BOOL    initCapture(int codec, int sampleRate, int channels, int bitrate);
    virtual BOOL    startCapture();
    
    virtual void    getAuxSDPLine(char * buff, int size, int rtp_pt);
    
    virtual void    addCallback(LiveAudioDataCB pCallback, void * pUserdata);
    virtual void    delCallback(LiveAudioDataCB pCallback, void * pUserdata);

    virtual BOOL    captureThread();    

    void            procData(uint8 * data, int size, int nbsamples);
    
protected:
    CLiveAudio();
    CLiveAudio(CLiveAudio &obj);    
    
    virtual void    stopCapture();
    
    BOOL            isCallbackExist(LiveAudioDataCB pCallback, void *pUserdata);
    
public:
    int                     m_nStreamIndex;
    int                     m_nRefCnt;      // single instance ref count

    static void           * m_pInstMutex;   // instance mutex
    static CLiveAudio     * m_pInstance[MAX_LIVE_AUDIO_NUMS];
    
protected:    
    BOOL                    m_bCapture;     // capturing flag
    pthread_t               m_hCapture;

    int                     m_nCodecId;     // audio codec
    int                     m_nSampleRate;  // sample rate
    int                     m_nChannel;     // channel
    int                     m_nBitrate;     // bitrate
    
    void                  * m_pMutex;       // mutex
    BOOL                    m_bInited;      // inited flag

    void                  * m_pCallbackMutex;
    LKLIST                * m_pCallbackList;
};

/*
 * After getting the encoded data from the hardware encoder, 
 * you can call this interface to send the data to the audio sending queue 
 *
 * If you use this interface to send audio data, you do not need to 
 * implement the CLiveAudio::captureThread function 
 */
BOOL media_live_put_audio(int idx, uint8 * data, int size, int nbsamples);

#endif

