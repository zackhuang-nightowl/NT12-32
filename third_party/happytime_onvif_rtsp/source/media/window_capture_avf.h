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

#ifndef WINDOW_CAPTURE_AVF_H
#define WINDOW_CAPTURE_AVF_H

typedef struct
{
    uint8  *data[4];
    int     linesize[4];
    int     width;
    int     height;
    int     format;
} avf_window_data;

typedef void (*avf_window_callback)(avf_window_data * data, void * userdata);

#ifdef __cplusplus
extern "C" {
#endif

void    avf_window_list();
void  * avf_window_init(char * title, int width, int height, double framerate);
void    avf_window_uninit(void * ctx);
void    avf_window_set_callback(void * ctx, avf_window_callback cb, void * userdata);
int     avf_window_get_width(void * ctx);
int     avf_window_get_height(void * ctx);
int     avf_window_get_pixfmt(void * ctx);
BOOL    avf_window_start(void * ctx);

#ifdef __cplusplus
}
#endif

#endif


