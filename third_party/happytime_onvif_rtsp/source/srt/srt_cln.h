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

#ifndef SRT_CLN_H
#define SRT_CLN_H

#include "srt/srt.h"
#include "h264.h"
#include "ts_parser.h"


#define SRT_EVE_STOPPED     60
#define SRT_EVE_CONNECTING  61
#define SRT_EVE_CONNFAIL    62
#define SRT_EVE_NOSIGNAL    63
#define SRT_EVE_RESUME      64
#define SRT_EVE_AUTHFAILED  65
#define SRT_EVE_NODATA      66
#define SRT_EVE_VIDEOREADY  67
#define SRT_EVE_AUDIOREADY  68


#define DEF_BUFTIME    (10 * 60 * 60 * 1000)    /* 10 hours default */

typedef int (*srt_notify_cb)(int, void *);
typedef int (*srt_video_cb)(uint8 *, int, uint32, void *);
typedef int (*srt_audio_cb)(uint8 *, int, uint32, void *);


class CSrtClient
{
public:
    CSrtClient(void);
    ~CSrtClient(void);

public:
    BOOL    srt_start(const char * url, const char * user, const char * pass);
    BOOL    srt_play();
    BOOL    srt_stop();
    BOOL    srt_pause();
    BOOL    srt_close();

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
    
    void    set_notify_cb(srt_notify_cb notify, void * userdata);
    void    set_video_cb(srt_video_cb cb);
    void    set_audio_cb(srt_audio_cb cb);
    void    set_rx_timeout(int timeout);

    void    rx_thread();
    void    srt_payload_cb(uint8 * p_data, uint32 len, uint32 type, uint64 pts);
    void    video_data_cb(uint8 * p_data, int len, uint32 ts);
    void    audio_data_cb(uint8 * p_data, int len, uint32 ts);
        
private:
    BOOL    srt_connect();
    void    srt_send_notify(int event);
    void    srt_h264_rx(uint8 * p_data, uint32 len, uint64 pts);
    void    srt_h265_rx(uint8 * p_data, uint32 len, uint64 pts);
    BOOL    srt_parse_adts(uint8 * p_data, uint32 len);
    
private:
    char            m_url[512];
    char            m_user[64];
    char            m_pass[64];
    char            m_ip[128];
    char            m_streamid[256];
    int             m_nport;

    int             m_fd;
    int             m_eid;
    
    BOOL            m_bRunning;
    BOOL            m_bVideoReady;
    BOOL            m_bAudioReady;
    pthread_t       m_tidRx;
    
    uint8           m_pVps[256];
    int             m_nVpsLen;
    uint8           m_pSps[256];
    int             m_nSpsLen;
    uint8           m_pPps[256];
    int             m_nPpsLen;

    TSParser        m_tsParser;
    
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
    
    srt_notify_cb   m_pNotify;
    void          * m_pUserdata;
    srt_video_cb    m_pVideoCB;
    srt_audio_cb    m_pAudioCB;
    void          * m_pMutex;
    int             m_nRxTimeout;
};

#endif




