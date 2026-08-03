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

#ifndef RTSP_CFG_H
#define RTSP_CFG_H

#include "media_info.h"
#ifdef MEDIA_PROXY
#include "media_proxy.h"
#endif
#ifdef MEDIA_PUSHER
#include "media_pusher.h"
#endif
#ifdef HTTP_NOTIFY
#include "http_notify.h"
#endif
#include "rtsp_srv.h"

// the default configuration file name
#define RTSP_DEF_CFG    "rtspserver.cfg"

#define MAX_USERS       10


typedef struct
{
    char    username[32];
    char    password[32];
} RTSP_USER;

typedef struct
{
    int     codec;                  // audio codec, refer media_format.h
    int     samplerate;             // sample rate
    int     channels;               // channels
} RTSP_BC_CFG;

typedef struct _RTSP_CFG
{
    uint32          ipv6_enable     : 1;    // ipv6 enable flag
    uint32          need_auth       : 1;    // auth flag
    uint32          log_enable      : 1;    // log enable flag
    uint32          crypt           : 1;    // data crypt flag
    uint32          multicast       : 1;    // enable multicast
    uint32          metadata        : 1;    // enable metadata stream
    uint32          rtsp_over_http  : 1;    // rtsp over http flag
    uint32          rtsp_over_https : 1;    // rtsp oved https flag
    uint32          rtsps_enable    : 1;    // rtsps enable flag
    uint32          reserved        : 23;
    
    char            serverip[128];  // server ip addr
    int             rtsp_port;      // rtsp port
    uint32          loop_nums;      // file loop-play numbers
    int             log_level;      // log level
    uint16          udp_base_port;  // udp media base port

#ifdef RTSPS
    int             rtsps_port;     // rtsps port
    char            rtsps_cert[128];// rtsps certificate file
    char            rtsps_key[128]; // rtsps private key file
#endif

#ifdef RTSP_OVER_HTTP
    int             http_port;      // http port
#ifdef HTTPS
    int             https_port;     // https port
    char            https_cert[128];// https certificate file 
    char            https_key[128]; // https private key file 
#endif
#endif

    RTSP_USER       users[MAX_USERS];
    
    MEDIA_OUTPUT  * output;         // the audio & video output configuration

#ifdef RTSP_BACKCHANNEL
    RTSP_BC_CFG     backchannel;    // back channel configuration
#endif

#ifdef HTTP_NOTIFY
    HTTP_NOTIFY_CFG http_notify;    // http notify
#endif
} RTSP_CFG;

#ifdef __cplusplus
extern "C" {
#endif

extern RTSP_CFG g_rtsp_cfg;

/**
 * @brief 
 *  read configuration file
 *
 * @param config the configuration file name
 *
 * @return TRUE on success, FALSE on error
 **/
BOOL            rtsp_read_config(const char * config);
const char    * rtsp_get_user_pass(const char * username);
BOOL            rtsp_cfg_get_video_info(const char * url, VIDEO_INFO * info);
BOOL            rtsp_cfg_get_audio_info(const char * url, AUDIO_INFO * info);
BOOL            rtsp_cfg_get_backchannel_info(AUDIO_INFO * info);
MEDIA_OUTPUT  * rtsp_add_output(MEDIA_OUTPUT ** p_output);
void            rtsp_free_outputs(MEDIA_OUTPUT ** p_output);

#ifdef __cplusplus
}
#endif

#endif // RTSP_CFG_H


