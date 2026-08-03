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

#ifndef VIDIO_CAPTURE_AVF_H
#define VIDIO_CAPTURE_AVF_H

typedef struct
{
    uint8  *data[4];
    int     linesize[4];
    int     width;
    int     height;
    int     format;
} avf_video_data;

typedef void (*avf_video_callback)(avf_video_data * data, void * userdata);

#ifdef __cplusplus
extern "C" {
#endif

int     avf_video_device_nums();
void    avf_video_device_list();
int     avf_video_device_get_index(const char * name);
void  * avf_video_init(int device_index, int width, int height, double framerate);
void    avf_video_uninit(void * ctx);
void    avf_video_set_callback(void * ctx, avf_video_callback cb, void * userdata);
int     avf_video_get_width(void * ctx);
int     avf_video_get_height(void * ctx);
int     avf_video_get_pixfmt(void * ctx);
BOOL    avf_video_read(void * ctx);

#ifdef __cplusplus
}
#endif

#endif


