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

#include "media_codec.h"
#include "media_format.h"


AVCodecID to_audio_avcodecid(int codec)
{
    AVCodecID codecid = AV_CODEC_ID_NONE;
    
    switch (codec)
    {
    case AUDIO_CODEC_G711A:
        codecid = AV_CODEC_ID_PCM_ALAW;
        break;

    case AUDIO_CODEC_G711U:
        codecid = AV_CODEC_ID_PCM_MULAW;
        break;
        
    case AUDIO_CODEC_G726:
        codecid = AV_CODEC_ID_ADPCM_G726;
        break;

    case AUDIO_CODEC_G722:
        codecid = AV_CODEC_ID_ADPCM_G722;
        break;

    case AUDIO_CODEC_OPUS:
        codecid = AV_CODEC_ID_OPUS;
        break;
        
    case AUDIO_CODEC_AAC:
        codecid = AV_CODEC_ID_AAC;
        break;
    }

    return codecid;
}

AVCodecID to_video_avcodecid(int codec)
{
    AVCodecID codecid = AV_CODEC_ID_NONE;
    
    switch (codec)
    {
    case VIDEO_CODEC_H264:
        codecid = AV_CODEC_ID_H264;
        break;

    case VIDEO_CODEC_H265:
        codecid = AV_CODEC_ID_HEVC;
        break;
        
    case VIDEO_CODEC_MP4:
        codecid = AV_CODEC_ID_MPEG4;
        break;

    case VIDEO_CODEC_JPEG:
        codecid = AV_CODEC_ID_MJPEG;
        break;
    }

    return codecid;
}

int to_audio_codec(AVCodecID codecid)
{
    int codec = AUDIO_CODEC_NONE;
    
    switch (codecid)
    {
    case AV_CODEC_ID_PCM_ALAW:
        codec = AUDIO_CODEC_G711A;
        break;

    case AV_CODEC_ID_PCM_MULAW:
        codec = AUDIO_CODEC_G711U;
        break;
        
    case AV_CODEC_ID_ADPCM_G726:
        codec = AUDIO_CODEC_G726;
        break;

    case AV_CODEC_ID_ADPCM_G722:
        codec = AUDIO_CODEC_G722;
        break;

    case AV_CODEC_ID_OPUS:
        codec = AUDIO_CODEC_OPUS;
        break;
        
    case AV_CODEC_ID_AAC:
        codec = AUDIO_CODEC_AAC;
        break;

    default:
        break;    
    }

    return codec;
}

int to_video_codec(AVCodecID codecid)
{
    int codec = VIDEO_CODEC_NONE;
    
    switch (codecid)
    {
    case AV_CODEC_ID_H264:
        codec = VIDEO_CODEC_H264;
        break;

    case AV_CODEC_ID_HEVC:
        codec = VIDEO_CODEC_H265;
        break;
        
    case AV_CODEC_ID_MPEG4:
        codec = VIDEO_CODEC_MP4;
        break;

    case AV_CODEC_ID_MJPEG:
        codec = VIDEO_CODEC_JPEG;
        break;

    default:
        break;
    }

    return codec;
}

AVPixelFormat to_avpixelformat(int fmt)
{
    AVPixelFormat pixfmt = AV_PIX_FMT_NONE;
    
    if (VIDEO_FMT_BGR24 == fmt)
    {
        pixfmt = AV_PIX_FMT_BGR24;
    }
    else if (VIDEO_FMT_YUV420P == fmt)
    {
        pixfmt = AV_PIX_FMT_YUV420P;
    }
    else if (VIDEO_FMT_YUYV422 == fmt)
    {
        pixfmt = AV_PIX_FMT_YUYV422;
    }
    else if (VIDEO_FMT_YVYU422 == fmt)
    {
        pixfmt = AV_PIX_FMT_YVYU422;
    }
    else if (VIDEO_FMT_UYVY422 == fmt)
    {
        pixfmt = AV_PIX_FMT_UYVY422;
    }
    else if (VIDEO_FMT_NV12 == fmt)
    {
        pixfmt = AV_PIX_FMT_NV12;
    }
    else if (VIDEO_FMT_NV21 == fmt)
    {
        pixfmt = AV_PIX_FMT_NV21;
    }
    else if (VIDEO_FMT_RGB24 == fmt)
    {
        pixfmt = AV_PIX_FMT_RGB24;
    }
    else if (VIDEO_FMT_RGB32 == fmt)
    {
        pixfmt = AV_PIX_FMT_RGB32;
    }
    else if (VIDEO_FMT_ARGB == fmt)
    {
        pixfmt = AV_PIX_FMT_ARGB;
    }
    else if (VIDEO_FMT_BGRA == fmt)
    {
        pixfmt = AV_PIX_FMT_BGRA;
    }
    else if (VIDEO_FMT_YV12 == fmt)
    {
        pixfmt = AV_PIX_FMT_YUV420P;
    }
    else if (VIDEO_FMT_BGR32 == fmt)
    {
        pixfmt = AV_PIX_FMT_BGR32;
    }
    else if (VIDEO_FMT_YUV422P == fmt)
    {
        pixfmt = AV_PIX_FMT_YUV422P;
    }

    return pixfmt;
}



