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

#ifndef MEDIA_PUSHER_H
#define MEDIA_PUSHER_H

#include "sys_inc.h"
#include "media_info.h"
#include "linked_list.h"
#include "hqueue.h"
#include "h264_rtp_rx.h"
#include "h265_rtp_rx.h"
#include "mjpeg_rtp_rx.h"
#include "mpeg4_rtp_rx.h"
#include "pcm_rtp_rx.h"
#include "aac_rtp_rx.h"
#include "rtp.h"
#include <map>
#include <string>

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
#include "video_decoder.h"
#include "audio_decoder.h"
#include "video_encoder.h"
#include "audio_encoder.h"
#endif


#define MAX_RTP_LEN         (64*1024)

#define TRANSFER_MODE_TCP   0
#define TRANSFER_MODE_UDP   1
#define TRANSFER_MODE_RTSP  2
#define TRANSFER_MODE_RTMP  3

#define MEDIA_TYPE_VIDEO    0
#define MEDIA_TYPE_AUDIO    1

class CMediaPusher;

typedef struct
{
    int             mode;
    char            ip[64];
    int             vport;
    int             aport;
} MEDIA_TRANSFER;

typedef struct
{
    uint32          has_output: 1;
    uint32          reserved  : 31;
    
    char            key[256];

    MEDIA_TRANSFER  transfer;       // transfer information

    MEDIA_INFO      input;
    MEDIA_INFO      output;
} PUSHER_CFG;

typedef struct _MEDIA_PUSH
{
    struct _MEDIA_PUSH * next;

    BOOL            is_fixed;
    PUSHER_CFG      cfg;
    
    CMediaPusher   *pusher;
} MEDIA_PUSH;

typedef void (*PusherDataCB)(uint8 * data, int size, int type, void * pUserdata);

typedef struct
{
    PusherDataCB    pCallback;
    void           *pUserdata;
    BOOL            bVFirst;
    BOOL            bAFirst;
    uint8           nPreNalu;
} PusherCB;

typedef struct
{
    int             sfd;                    // listen socket 
    int             mfd;                    // data send and receive socket, only for TCP mode
    int             family;                 // address family, AF_INET or AF_INET6
    int             rtp_pt;                 // rtp payload type
    
    char            recv_buf[16];           // receive buffer 
    char           *dyn_recv_buf;           // dynamic receive buffer
    uint32          recv_offset;            // receive buffer offset 

    union {
        H264RXI     h264rxi;
        H265RXI     h265rxi;
        MJPEGRXI    mjpegrxi;
        MPEG4RXI    mpeg4rxi;

        AACRXI      aacrxi;
        PCMRXI      pcmrxi;        
    };
} PUA;


#ifdef __cplusplus
extern "C" {
#endif

extern MEDIA_PUSH * g_pusher;

MEDIA_PUSH* media_add_pusher(BOOL fixed);
void        media_free_pusher(const char * key);
void        media_free_pushers();
BOOL        media_init_pusher(MEDIA_PUSH * p_pusher);
void        media_init_pushers();
int         media_get_pusher_nums();
MEDIA_PUSH* media_pusher_match(const char * key);

#ifdef __cplusplus
}
#endif

class CMediaPusher
{
public:
    CMediaPusher(PUSHER_CFG * pConfig);
    ~CMediaPusher(void);

    static  CMediaPusher * getInstance(char * key, PUSHER_CFG * pConfig);
    void    freeInstance(char * key);
    
    void    onVideo(uint8 * pdata, int len, uint32 ts, uint16 seq);
    void    onAudio(uint8 * pdata, int len, uint32 ts, uint16 seq);
    void    onMetadata(uint8 * pdata, int len, uint32 ts, uint16 seq);
    
    void    setVideoInfo(VIDEO_INFO * p_info);
    void    setAudioInfo(AUDIO_INFO * p_info);
    void    setVideoExtra(uint8 * extra, int extra_size);
    void    setAudioExtra(uint8 * extra, int extra_size);
    
    void    addCallback(PusherDataCB pCallback, void * pUserdata);
    void    delCallback(PusherDataCB pCallback, void * pUserdata);
    void    dataCallback(uint8 * data, int size, int type);
    
    PUSHER_CFG * getConfig() {return m_pConfig;}
    
    BOOL    isInited() {return m_bInited;} 
    void    getVideoAuxSDPLine(char * buff, int size, int rtp_pt);
    void    getAudioAuxSDPLine(char * buff, int size, int rtp_pt);
    void    tcpRecvThread();
    void    udpRecvThread();

    /************************************************************/
    BOOL    startUdpRx(int vfd, int afd);
    void    stopUdpRx();
    void    setRua(void * p_rua);
    void  * getRua() {return m_rua;}
    void    setAACConfig(int size_length, int index_length, int index_delta_length);
    void    setMpeg4Config(uint8 * data, int len);
    void    setH264Params(char * p_sdp);
    void    setH265Params(char * p_sdp);
    BOOL    videoRtpRx(uint8 * p_rtp, int rlen);
    BOOL    audioRtpRx(uint8 * p_rtp, int rlen);
    int     needVideoRecodec(uint8 * p_data, int len);
    int     needAudioRecodec();
    
#ifdef RTSP_METADATA
    BOOL    metadataRtpRx(uint8 * p_rtp, int rlen);
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)    
    int     getVideoRecodec() {return m_nVideoRecodec;}
    int     getAudioRecodec() {return m_nAudioRecodec;}
    void    videoDataDecode(uint8 * p_data, int len);
    void    videoDataEncode(AVFrame * frame);
    void    audioDataDecode(uint8 * p_data, int len);
    void    audioDataEncode(AVFrame * frame);
#endif

private:
    void    initPusher();
    int     initSocket(const char * ip, int port, int mode, int *family);
    void    reinitMedia();
    BOOL    startPusher();

    void    tcpListenRx(PUA * p_ua);
    BOOL    tcpDataRx(PUA * p_ua, int type);
    int     tcpPacketRx(PUA * p_ua, RILF ** p_pkt);
    BOOL    videoRtpRx(PUA * p_ua, uint8 * p_rtp, int rlen);
    BOOL    audioRtpRx(PUA * p_ua, uint8 * p_rtp, int rlen);
    BOOL    udpDataRx(PUA * p_ua, int type);
    void    closeMedia(PUA * p_ua);
    
    BOOL    isCallbackExist(PusherDataCB pCallback, void * pUserdata);
    BOOL    getExtraData(uint8 * extra, int * extra_len);
    void    sendExtraData(PusherCB * p_cb, uint8 * data, int size, int type);

    void    getH264AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getH265AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getMP4AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getAACAuxSDPLine(char * buff, int size, int rtp_pt);

public:
    int             m_nRefCnt;
    
    static void   * m_pInstMutex;
    static std::map<std::string,CMediaPusher*> m_insts;

private:
    BOOL            m_bInited;
    PUA             m_vua;
    PUA             m_aua;
    void          * m_pRuaMutex;
    void          * m_rua;
    BOOL            m_bRecving;
    pthread_t       m_tidRecv;

    PUSHER_CFG    * m_pConfig;
    uint8         * m_pVideoExtra;
    int             m_nVideoExtraSize;
    uint8         * m_pAudioExtra;
    int             m_nAudioExtraSize;

    void          * m_pCallbackMutex;
    LKLIST        * m_pCallbackList;

    int             m_nVideoRecodec;    // 1  - need; 0  - Still not sure; -1 - not need
    int             m_nAudioRecodec;    // 1  - need; 0  - Still not sure; -1 - not need
    
#ifdef RTSP_METADATA
    PCMRXI          m_mrxi;             // metadata receiver
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    CVideoDecoder * m_pVideoDecoder;    // video decoder
    CAudioDecoder * m_pAudioDecoder;    // audio decoder
    CVideoEncoder * m_pVideoEncoder;    // video encoder  
    CAudioEncoder * m_pAudioEncoder;    // audio encoder
#endif
};

#endif



