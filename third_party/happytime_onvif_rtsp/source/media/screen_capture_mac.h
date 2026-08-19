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

#ifndef SCREEN_CAPTURE_MAC_H
#define SCREEN_CAPTURE_MAC_H

#include "screen_capture.h"
#include "screen_capture_avf.h"


class CMScreenCapture : public CScreenCapture
{
public:
    // get monitor numbers
    static int getDeviceNums();
    
    static CScreenCapture * getInstance(int devid);
    
    BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    BOOL    startCapture();
    
    void    captureThread();
    void    screenCallBack(avf_screen_data * data);
    
private:
    CMScreenCapture();
    CMScreenCapture(CMScreenCapture &obj);
    ~CMScreenCapture();

    BOOL    capture();
    void    stopCapture();
    
private:
    void  * m_pCapture;
};

#endif



