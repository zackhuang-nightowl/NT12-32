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

#ifndef _VIDIO_CAPTURE_WIN_H
#define _VIDIO_CAPTURE_WIN_H

#include <mfidl.h>
#include <mfreadwrite.h>
#include "video_capture.h"

/***************************************************************************************/

class CWVideoCapture : public CVideoCapture
{
public:
    // get video capture devices numbers
    static int  getDeviceNums();
    // list avaivalbe video capture device
    static void listDevice();
    // get device index by device name
    static int  getDeviceIndex(const char * name);

    // get the single instance
    static CVideoCapture * getInstance(int devid);

    BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    BOOL    startCapture();

    void    captureThread();
    
private:
    CWVideoCapture();
    CWVideoCapture(CWVideoCapture &obj);
    ~CWVideoCapture();

    BOOL    capture();
    void    stopCapture();
    BOOL    checkMediaType();
    BOOL    setMediaFormat1(int codec, int width, int height);
    BOOL    setMediaFormat2(int codec, int width, int height);
    BOOL    setMediaFormat3(int codec, int width, int height);
    BOOL    setFramerate(double framerate);
    
private:
    IMFMediaSource  * m_pSource;
    IMFSourceReader * m_pReader;
};

#endif // _VIDIO_CAPTURE_WIN_H


