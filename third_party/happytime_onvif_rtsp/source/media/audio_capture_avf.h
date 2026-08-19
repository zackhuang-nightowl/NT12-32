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

#ifndef AUDIO_CAPTURE_AVF_H
#define AUDIO_CAPTURE_AVF_H

typedef struct
{
    uint8  *data[4];
    int     linesize[4];
    int     samplerate;
    int     channels;
    int     format;
    int     samples;
} avf_audio_data;

typedef void (*avf_audio_callback)(avf_audio_data * data, void * userdata);

#ifdef __cplusplus
extern "C" {
#endif

int     avf_audio_device_nums();
void    avf_audio_device_list();
int     avf_audio_device_get_index(const char * name);
BOOL    avf_audio_device_get_name(int index, char * name, int namesize);
void  * avf_audio_init(int device_index, int samplerate, int channels);
void    avf_audio_uninit(void * ctx);
void    avf_audio_set_callback(void * ctx, avf_audio_callback cb, void * userdata);
int     avf_audio_get_samplerate(void * ctx);
int     avf_audio_get_channels(void * ctx);
int     avf_audio_get_samplefmt(void * ctx);
BOOL    avf_audio_read(void * ctx, int * samples);

#ifdef __cplusplus
}
#endif

#endif


