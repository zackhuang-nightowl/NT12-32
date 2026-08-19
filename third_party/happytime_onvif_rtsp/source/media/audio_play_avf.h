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

#ifndef AUDIO_PLAY_AVF_H
#define AUDIO_PLAY_AVF_H

typedef void (*avf_audio_play_callback)(void * buff, uint32 size, void * userdata);

#ifdef __cplusplus
extern "C" {
#endif

void  * avf_audio_play_init(int samplerate, int channels);
void    avf_audio_play_uninit(void * ctx);
void    avf_audio_play_set_callback(void * ctx, avf_audio_play_callback cb, void * userdata);
BOOL    avf_audio_play_set_volume(void * ctx, double volume);
double  avf_audio_play_get_volume(void * ctx);

#ifdef __cplusplus
}
#endif

#endif


