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

#ifndef MEDIA_CODEC_H
#define MEDIA_CODEC_H

extern "C" {
#include <libavcodec/avcodec.h>
}

#ifdef __cplusplus
extern "C" {
#endif

AVCodecID       to_audio_avcodecid(int codec);
AVCodecID       to_video_avcodecid(int codec);
int             to_audio_codec(AVCodecID codecid);
int             to_video_codec(AVCodecID codecid);
AVPixelFormat   to_avpixelformat(int fmt);

#ifdef __cplusplus
}
#endif

#endif


