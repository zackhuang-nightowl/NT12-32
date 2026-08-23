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

#ifndef _RTSP_MEDIA_H
#define _RTSP_MEDIA_H

#include "hqueue.h"
#include "media_info.h"

#ifdef MEDIA_PROXY
#include "media_proxy.h"
#endif

#ifdef MEDIA_PUSHER
#include "media_pusher.h"
#endif

#ifdef MEDIA_FILE
#include "file_demux.h"
#endif

#ifdef MEDIA_DEVICE
#if __WINDOWS_OS__
#include "audio_capture_win.h"
#include "video_capture_win.h"
#include "screen_capture_win.h"
#include "window_capture_win.h"
#elif defined(ANDROID)
#include "audio_capture_android.h"
#include "video_capture_qt.h"
#include "screen_capture.h"
#include "window_capture.h"
#elif defined(IOS)
#include "audio_capture_mac.h"
#include "video_capture_mac.h"
#include "screen_capture_mac.h"
#include "window_capture_mac.h"
#elif __LINUX_OS__
#include "audio_capture_linux.h"
#include "video_capture_linux.h"
#include "screen_capture_linux.h"
#include "window_capture_linux.h"
#endif
#endif

#ifdef MEDIA_LIVE
#include "live_video.h"
#include "live_audio.h"
#endif

typedef struct
{
    uint32          has_audio   : 1;    // has audio ?
    uint32          has_video   : 1;    // has video ?
    uint32          has_metadata: 1;    // has metadata ?
    uint32          is_file     : 1;    // is file
    uint32          is_device   : 1;    // is device
    uint32          is_screen   : 1;    // is screen live
    uint32          is_window   : 1;    // is window
    uint32          is_proxy    : 1;    // is proxy
    uint32          is_pusher   : 1;    // is pusher
    uint32          is_live     : 1;    // is live stream
    uint32          is_replay   : 1;    // is replay
    uint32          is_publish  : 1;    // is rtsp pusher session
    uint32          seek_flag   : 1;    // seek flag
    uint32          video_tx    : 1;    // video tx flag
    uint32          audio_tx    : 1;    // audio tx flag
    uint32          metadata_tx : 1;    // metadata tx flag
    uint32          v_resync    : 1;    // live: chasing next keyframe (drop until key, then flush+resume)
    uint32          reserved    : 15;

    char            filename[256];      // file name
    int64           duration;           // media duration, unit is millisecond
    int64           curpos;             // media current position, unit is millisecond

    uint8           v_index;            // video stream index
    uint8           a_index;            // audio stream index
    
    VIDEO_INFO      v_info;             // video information
    AUDIO_INFO      a_info;             // audio information

    HQUEUE        * v_queue;            // video queue
    HQUEUE        * a_queue;            // audio queue

    pthread_t       v_thread;           // video thread
    pthread_t       a_thread;           // audio thread

#ifdef RTSP_BACKCHANNEL
    AUDIO_INFO      bc_info;            // audio back-channel information
#endif

#ifdef RTSP_METADATA
    HQUEUE        * m_queue;            // metadata queue
    pthread_t       m_thread;           // metadata thread
#endif

#ifdef MEDIA_PROXY
    CMediaProxy   * proxy;              // proxy object
#endif

#ifdef MEDIA_PUSHER
    CMediaPusher  * pusher;             // pusher object
#endif

#ifdef MEDIA_FILE
    CFileDemux    * file_demuxer;       // file demuxer object
#endif

#ifdef MEDIA_LIVE
    CLiveVideo    * live_video;         // live video object
    CLiveAudio    * live_audio;         // live audio object
#endif

#ifdef MEDIA_DEVICE
    char            window_title[256];  // window title
    CVideoCapture * video_capture;      // video capture
    CAudioCapture * audio_capture;      // audio capture
    CScreenCapture* screen_capture;     // screen capture
    CWindowCapture* window_capture;     // window capture
#endif

#ifdef RTSP_REPLAY
    time_t          earliest;
    time_t          latest;
#endif
} UA_MEDIA_INFO;

/* drain + free all queued packets (C++ linkage; used by the seek flush path). */
void    rtsp_media_clear_queue(HQUEUE * queue);

#ifdef __cplusplus
extern "C" {
#endif

BOOL    rtsp_media_init(void * p_rua);
void    rtsp_media_send_thread(void * rua);

void    rtsp_media_get_video_sdp_line(void * rua, uint8 pt, char * buff, int size);
void    rtsp_media_get_audio_sdp_line(void * rua, uint8 pt, char * buff, int size);

#ifdef __cplusplus
}
#endif

#endif


