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

#ifndef AUDIO_PLAY_WIN_H
#define AUDIO_PLAY_WIN_H

#include "media_format.h"
#include "audio_play.h"
#include <dsound.h>


class CWAudioPlay : public CAudioPlay
{
public:
    CWAudioPlay();
    ~CWAudioPlay();

public:
    BOOL        startPlay(int samplerate, int channels);
    void        stopPlay();
    BOOL        setVolume(int volume);
    int         getVolume();
    void        playAudio(uint8 * pData, int len);

private:
    void        playAudio1(uint8 * data, int size);
    void        waitDevice();
    
private:
    LPDIRECTSOUND8      m_pDSound8;
    LPDIRECTSOUNDBUFFER m_pDSoundBuffer;

    void              * m_pMutex;
    uint8             * m_pAudioBuff;
    uint32              m_nAudioBuffLen;
    int                 m_nLastChunk;
    int                 m_nBufferNums;
    int                 m_nSampleNums;
    int                 m_nSpecSize;
};

#endif



