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

#ifndef RTMP_CLN_H
#define RTMP_CLN_H

#include "rtmp.h"
#include "h264.h"


#define RTMP_EVE_STOPPED    40
#define RTMP_EVE_CONNECTING 41
#define RTMP_EVE_CONNFAIL   42
#define RTMP_EVE_NOSIGNAL   43
#define RTMP_EVE_RESUME     44
#define RTMP_EVE_AUTHFAILED 45
#define RTMP_EVE_NODATA     46
#define RTMP_EVE_VIDEOREADY 47
#define RTMP_EVE_AUDIOREADY 48


#define DEF_BUFTIME (10 * 60 * 60 * 1000)   /* 10 hours default */

typedef int (*rtmp_notify_cb)(int, void *);
typedef int (*rtmp_video_cb)(uint8 *, int, uint32, void *);
typedef int (*rtmp_audio_cb)(uint8 *, int, uint32, void *);


class CRtmpClient
{
public:
    CRtmpClient(void);
    ~CRtmpClient(void);

public:
    BOOL    rtmp_start(const char * url, const char * user, const char * pass);
    BOOL    rtmp_play();
    BOOL    rtmp_stop();
    BOOL    rtmp_pause();
    BOOL    rtmp_close();

    char *  get_url() {return m_url;}
    char *  get_user() {return m_user;}
    char *  get_pass() {return m_pass;}
    int     audio_codec() {return m_nAudioCodec;}
    int     video_codec() {return m_nVideoCodec;}
    void    get_h264_params();
    BOOL    get_h264_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len);
    void    get_h265_params();
    BOOL    get_h265_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len, uint8 * p_vps, int * vps_len);
    int     get_video_width() {return m_nWidth;}
    int     get_video_height() {return m_nHeight;}
    double  get_video_framerate() {return m_nFrameRate;}
    int     get_audio_samplerate() {return m_nSamplerate;}
    int     get_audio_channels() {return m_nChannels;}
    uint8 * get_audio_config() {return m_pAudioConfig;}
    int     get_audio_config_len() {return m_nAudioConfigLen;}
    uint32  get_video_init_ts() {return m_nVideoInitTS;}
    uint32  get_audio_init_ts() {return m_nAudioInitTS;}
    
    void    set_notify_cb(rtmp_notify_cb notify, void * userdata);
    void    set_video_cb(rtmp_video_cb cb);
    void    set_audio_cb(rtmp_audio_cb cb);

    void    rx_thread();
    void    rtmp_video_data_cb(uint8 * p_data, int len, uint32 ts);
    void    rtmp_audio_data_cb(uint8 * p_data, int len, uint32 ts);
        
private:
    BOOL    rtmp_connect();
    int     rtmp_h264_rx(RTMPPacket * packet);
    int     rtmp_h265_rx(RTMPPacket * packet);
    int     rtmp_video_rx(RTMPPacket * packet);
    int     rtmp_g711_rx(RTMPPacket * packet);
    int     rtmp_aac_rx(RTMPPacket * packet);
    int     rtmp_audio_rx(RTMPPacket * packet);
    int     rtmp_metadata_rx(RTMPPacket * packet);
    void    rtmp_send_notify(int event);
    
private:
    char            m_url[512];
    char            m_user[64];
    char            m_pass[64];
    
    RTMP          * m_pRtmp;
    int             m_bRunning;
    BOOL            m_bPause;
    BOOL            m_bVideoReady;
    BOOL            m_bAudioReady;
    pthread_t       m_tidRx;
    
    uint8           m_pVps[256];
    int             m_nVpsLen;
    uint8           m_pSps[256];
    int             m_nSpsLen;
    uint8           m_pPps[256];
    int             m_nPpsLen;
    
    uint8           m_pAudioConfig[32];
    uint32          m_nAudioConfigLen;

    int             m_nVideoCodec;
    int             m_nWidth;
    int             m_nHeight;
    double          m_nFrameRate;
    int             m_nAudioCodec;
    int             m_nSamplerate;
    int             m_nChannels;

    uint32          m_nVideoInitTS;
    uint32          m_nAudioInitTS;
    
    rtmp_notify_cb  m_pNotify;
    void          * m_pUserdata;
    rtmp_video_cb   m_pVideoCB;
    rtmp_audio_cb   m_pAudioCB;
    void          * m_pMutex;
};

#endif




