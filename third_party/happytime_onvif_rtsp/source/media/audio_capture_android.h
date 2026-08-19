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

#ifndef AUDIO_CAPTURE_ANDROID_H
#define AUDIO_CAPTURE_ANDROID_H

#include "sys_inc.h"
#include "audio_capture.h"
#include "gles_input.h"


class CAAudioCapture : public CAudioCapture
{
public:

    // get audio capture devcie nubmers     
    static int  getDeviceNums();
    // list avaivalbe video capture device
    static void listDevice();
    // get device index by device name
    static int  getDeviceIndex(const char * name);
    // get device name by device index
    static BOOL getDeviceName(int index, char * name, int namesize);
    
    // get single instance
    static CAudioCapture * getInstance(int devid);

    BOOL    initCapture(int codec, int sampleRate, int channels, int bitrate);
    BOOL    startCapture();

    void    dataCallback(uint8 * pdata, int len);

private:
    CAAudioCapture();
    CAAudioCapture(CAAudioCapture &obj);
    ~CAAudioCapture();
        
    void    stopCapture(); 

private:
    GlesInput     * m_pInput;
};

#endif

