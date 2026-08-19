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

#ifndef MEDIA_PROXY_H
#define MEDIA_PROXY_H

#include "rtsp_cln.h"
#include "http_mjpeg_cln.h"
#include "linked_list.h"
#include "hqueue.h"
#include "media_info.h"
#include <map>
#include <string>

#ifdef RTMP_PROXY
#include "rtmp_cln.h"
#endif
#ifdef SRT_PROXY
#include "srt_cln.h"
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
#include "video_decoder.h"
#include "audio_decoder.h"
#include "video_encoder.h"
#include "audio_encoder.h"
#endif

class CMediaProxy;

#define RC_TRANSFER_TCP         0
#define RC_TRANSFER_UDP         1
#define RC_TRANSFER_MULTICAST   2

typedef struct
{
    uint32  has_output : 1;
    uint32  on_demand  : 1;
    uint32  reserved   : 30;
    
    char    key[256];
    char    url[256];
    char    user[32];
    char    pass[32];
    int     transfer;           // rtsp client transfer protocol, TCP,UDP,MULTICAST

    MEDIA_INFO output;
} PROXY_CFG;

typedef struct _MEDIA_PRY
{
    struct _MEDIA_PRY * next;
    
    PROXY_CFG    cfg;
    
    CMediaProxy *proxy;
} MEDIA_PRY;

typedef void (*ProxyDataCB)(uint8 * data, int size, int type, void * pUserdata);

typedef struct
{
    ProxyDataCB  pCallback;
    void        *pUserdata;
    BOOL         bVFirst;
    BOOL         bAFirst;
    uint8        nPreNalu;
} ProxyCB;


#ifdef __cplusplus
extern "C" {
#endif

extern MEDIA_PRY * g_proxy;

MEDIA_PRY  * media_add_proxy();
void         media_free_proxies();
BOOL         media_init_proxy(MEDIA_PRY * p_proxy);
void         media_init_proxies();
int          media_get_proxy_nums();
MEDIA_PRY  * media_proxy_match(const char * key);

#ifdef __cplusplus
}
#endif

class CMediaProxy 
{
public:
    CMediaProxy(PROXY_CFG * pConfig);
    ~CMediaProxy(void);

    static  CMediaProxy * getInstance(char * key, PROXY_CFG * pConfig);
    void    freeInstance(char * key);

    void    onNotify(int evt);
    void    onVideo(uint8 * pdata, int len, uint32 ts, uint16 seq);
    void    onAudio(uint8 * pdata, int len, uint32 ts, uint16 seq);
    void    onMetadata(uint8 * pdata, int len, uint32 ts, uint16 seq);
    void    notifyHandler();

    BOOL    startConn(const char * url, char * user, char * pass);
    
    void    getVideoAuxSDPLine(char * buff, int size, int rtp_pt);
    void    getAudioAuxSDPLine(char * buff, int size, int rtp_pt);

    void    addCallback(ProxyDataCB pCallback, void * pUserdata);
    void    delCallback(ProxyDataCB pCallback, void * pUserdata);
    void    dataCallback(uint8 * data, int size, int type);
    BOOL    getExtraData(uint8 * extra, int * extra_len);
    void    sendExtraData(ProxyCB * p_cb, uint8 * data, int size, int type);
    int     needVideoRecodec(uint8 * p_data, int len);
    int     needAudioRecodec();
    
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)    
    int     getVideoRecodec() {return m_nVideoRecodec;}
    int     getAudioRecodec() {return m_nAudioRecodec;}
    void    videoDataDecode(uint8 * p_data, int len);
    void    videoDataEncode(AVFrame * frame);
    void    audioDataDecode(uint8 * p_data, int len);
    void    audioDataEncode(AVFrame * frame);
#endif

private:
    void    freeConn();
    BOOL    startConn();
    BOOL    restartConn();
    void    clearNotify();
    BOOL    isCallbackExist(ProxyDataCB pCallback, void * pUserdata);
    void    getH264AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getH265AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getMP4AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getAACAuxSDPLine(char * buff, int size, int rtp_pt);

public:
    int             m_nRefCnt;
    
    static void   * m_pInstMutex;
    static std::map<std::string,CMediaProxy*> m_insts;
    
public:
    uint32          m_bInited   : 1;
    uint32          m_bExiting  : 1;
    uint32          reserved    : 30;

    PROXY_CFG     * m_pConfig;

    MEDIA_INFO      m_info;

    char            m_url[500];
    char            m_user[32];
    char            m_pass[32];
    int             m_reconnto;
    
    CRtspClient   * m_rtsp;
    CHttpMjpeg    * m_mjpeg;
#ifdef RTMP_PROXY
    CRtmpClient   * m_rtmp;
#endif
#ifdef SRT_PROXY
    CSrtClient    * m_srt;
#endif

    void          * m_pMutex;
    HQUEUE        * m_notifyQueue;
    pthread_t       m_hNotify;
    
    void          * m_pCallbackMutex;
    LKLIST        * m_pCallbackList;

    int             m_nVideoRecodec;    // 1  - need; 0  - Still not sure; -1 - not need
    int             m_nAudioRecodec;    // 1  - need; 0  - Still not sure; -1 - not need
    
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    CVideoDecoder * m_pVideoDecoder;    // video decoder
    CAudioDecoder * m_pAudioDecoder;    // audio decoder
    CVideoEncoder * m_pVideoEncoder;    // video encoder  
    CAudioEncoder * m_pAudioEncoder;    // audio encoder
#endif
};

#endif


