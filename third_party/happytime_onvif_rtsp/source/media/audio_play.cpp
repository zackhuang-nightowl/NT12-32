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
#include "audio_play.h"


CAudioPlay::CAudioPlay()
{
    m_bInited = FALSE;
    m_nSamplerate = 8000;
    m_nChannels = 1;
}

CAudioPlay::~CAudioPlay(void)
{
}

BOOL CAudioPlay::startPlay(int samplerate, int channels)
{
    m_nSamplerate = samplerate;
    m_nChannels = channels;
    
    return FALSE;
}

void CAudioPlay::stopPlay(void)
{
    m_bInited = FALSE;
}

BOOL CAudioPlay::setVolume(int volume)
{
    return FALSE;
}

int CAudioPlay::getVolume()
{    
    return 0;
}

void CAudioPlay::playAudio(uint8 * data, int size) 
{ 
    if (!m_bInited)
    {
        return;
    }
}




