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

#ifndef WINDOW_CAPTURE_LINUX_H
#define WINDOW_CAPTURE_LINUX_H

#include "window_capture.h"
#include <xcb/xcb.h>


class CLWindowCapture : public CWindowCapture
{
public:
    static void listWindow();
    
    static CWindowCapture * getInstance(char * title);
    
    BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    BOOL    startCapture();
    
    void    captureThread();
    
private:
    CLWindowCapture();
    CLWindowCapture(CLWindowCapture &obj);
    ~CLWindowCapture();

    BOOL    initWindow();
    BOOL    capture();
    void    stopCapture();
    int     getPixfmt(int depth);
    
private:    
    xcb_connection_t * m_pConn;
    xcb_window_t       m_nWindow;
};

#endif



