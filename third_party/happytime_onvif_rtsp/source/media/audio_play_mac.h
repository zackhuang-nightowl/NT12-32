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

#ifndef AUDIO_PLAY_MAC_H
#define AUDIO_PLAY_MAC_H

#include "sys_inc.h"
#include "linked_list.h"
#include "media_format.h"
#include "audio_play.h"
#include "audio_play_avf.h"


class CMAudioPlay : public CAudioPlay
{
public:
    CMAudioPlay();
    ~CMAudioPlay();

    BOOL        startPlay(int samplerate, int channels);
    void        stopPlay();
    BOOL        setVolume(int volume);
    int         getVolume();
    void        playAudio(uint8 * pData, int len);
    void        playCallback(void * buff, uint32 size);
    
private:
    void      * m_pMutex;       // mutex
    void      * m_pPlayer;
    uint8     * m_pBuffer;
    uint32      m_nBufferSize;
    uint32      m_nOffset;
};

#endif


