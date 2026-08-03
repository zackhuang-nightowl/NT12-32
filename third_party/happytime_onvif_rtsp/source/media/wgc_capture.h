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

#ifndef WGC_CAPTURE_H
#define WGC_CAPTURE_H

typedef void    (*wgc_callback) (int fmt, int w, int h, int stride, unsigned char * data, void * userdata);
typedef void *  (*wgc_init_window)(HWND);
typedef void *  (*wgc_init_monitor)(HMONITOR);
typedef void    (*wgc_uninit)(void *);
typedef bool    (*wgc_start)(void *);
typedef void    (*wgc_set_callback)(void *, wgc_callback, void *);

typedef struct
{
    HMODULE             module;
    wgc_init_window     init_window;
    wgc_init_monitor    init_monitor;
    wgc_uninit          uninit;
    wgc_start           start;
    wgc_set_callback    set_callback;
} WGCMODULE;

#endif


