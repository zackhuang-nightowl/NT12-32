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

#ifndef VIDEO_ENCODER_ANDROID_H
#define VIDEO_ENCODER_ANDROID_H

#include "sys_inc.h"
#include "linked_list.h"
#include "video_encoder.h"
#include <QJniObject>


class CAndroidVideoEncoder
{
public:
    CAndroidVideoEncoder();
    ~CAndroidVideoEncoder();

    BOOL                init(VideoEncoderParam * params);
    void                uninit();
    BOOL                encode(uint8* data[4], int32 linesize[4]);
    BOOL                encode(uint8* data, int size);
    void                addCallback(VideoDataCallback pCallback, void *pUserdata);
    void                delCallback(VideoDataCallback pCallback, void *pUserdata);
    void                getAuxSDPLine(char * buff, int size, int rtp_pt);
    BOOL                getExtraData(uint8 ** extradata, int * extralen);

private:    
    BOOL                isCallbackExist(VideoDataCallback pCallback, void *pUserdata);
    void                procData(uint8 * data, int size);

    void                getH264AuxSDPLine(char * buff, int size, int rtp_pt);
    void                getH265AuxSDPLine(char * buff, int size, int rtp_pt);

private:
    QJniObject          m_codec;
    
    VideoEncoderParam   m_EncoderParams;
    
    int                 m_dstFmt;

    BOOL                m_bInited;
    uint8             * m_yuv;
    uint8             * m_orgyuv;
    
    std::string         m_mime;
    std::string         m_codecname;

    uint8             * m_pExtra;
    int                 m_nExtraLen;
    int                 m_nInputTimeout;
    int                 m_nOutputTimeout;
    
    void              * m_pCallbackMutex;
    LKLIST            * m_pCallbackList;
};

#endif // VIDEO_ENCODER_ANDROID_H



