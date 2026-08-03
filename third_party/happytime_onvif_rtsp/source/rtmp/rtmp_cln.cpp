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

#include "sys_inc.h"
#include "rtmp_cln.h"
#include "h264.h"
#include "h265.h"
#include "media_format.h"


void * rtmp_rx_thread(void * argv)
{
    CRtmpClient * pRtmpClient = (CRtmpClient *) argv;

    pRtmpClient->rx_thread();

    return NULL;
}

CRtmpClient::CRtmpClient()
: m_pRtmp(NULL)
, m_bRunning(FALSE)
, m_bPause(FALSE)
, m_bVideoReady(FALSE)
, m_bAudioReady(FALSE)
, m_tidRx(0)
, m_nVpsLen(0)
, m_nSpsLen(0)
, m_nPpsLen(0)
, m_nAudioConfigLen(0)
, m_nVideoCodec(VIDEO_CODEC_NONE)
, m_nWidth(0)
, m_nHeight(0)
, m_nFrameRate(0)
, m_nAudioCodec(AUDIO_CODEC_NONE)
, m_nSamplerate(44100)
, m_nChannels(2)
, m_nVideoInitTS(0)
, m_nAudioInitTS(0)
, m_pNotify(NULL)
, m_pUserdata(NULL)
, m_pVideoCB(NULL)
, m_pAudioCB(NULL)
{
    memset(m_url, 0, sizeof(m_url));
    memset(m_user, 0, sizeof(m_user));
    memset(m_pass, 0, sizeof(m_pass));

    memset(&m_pVps, 0, sizeof(m_pVps));
    memset(&m_pSps, 0, sizeof(m_pVps));
    memset(&m_pPps, 0, sizeof(m_pVps));
    memset(&m_pAudioConfig, 0, sizeof(m_pAudioConfig));
    
    m_pMutex = sys_os_create_mutex();
}

CRtmpClient::~CRtmpClient()
{
    rtmp_close();

    if (m_pMutex)
    {
        sys_os_destroy_mutex(m_pMutex);
        m_pMutex = NULL;
    }
}

BOOL CRtmpClient::rtmp_start(const char * url, const char * user, const char * pass)
{
    if (url && url[0] != '\0')
    {
        strncpy(m_url, url, sizeof(m_url)-1);
    }

    if (user && user[0] != '\0')
    {
        strncpy(m_user, user, sizeof(m_user)-1);
    }

    if (pass && pass[0] != '\0')
    {
        strncpy(m_pass, pass, sizeof(m_pass)-1);
    }
    
    m_bRunning = TRUE;
    m_tidRx = sys_os_create_thread((void *)rtmp_rx_thread, this);
    
    return (m_tidRx != 0) ? TRUE : FALSE;
}

BOOL CRtmpClient::rtmp_play()
{
    if (m_pRtmp && m_bPause)
    {
        RTMP_Pause(m_pRtmp, 0);
        m_bPause = 0;
    }

    return TRUE;
}

BOOL CRtmpClient::rtmp_stop()
{
    return rtmp_close();
}

BOOL CRtmpClient::rtmp_pause()
{
    if (m_pRtmp && !m_bPause)
    {
        RTMP_Pause(m_pRtmp, 1);
        m_bPause = 1;
    }
    
    return TRUE;
}

BOOL CRtmpClient::rtmp_close()
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = NULL;
    m_pVideoCB = NULL;
    m_pNotify = NULL;
    m_pUserdata = NULL;
    sys_os_mutex_leave(m_pMutex);

    m_bRunning = FALSE;

    sys_os_wait_thread(&m_tidRx);

    if (m_pRtmp)
    {
        RTMP_Close(m_pRtmp);
        RTMP_Free(m_pRtmp);

        m_pRtmp = NULL;
    }

    m_bVideoReady = FALSE;
    m_bAudioReady = FALSE;
    
    return TRUE;
}

BOOL CRtmpClient::rtmp_connect()
{
    RTMP * rtmp;
    
    rtmp_send_notify(RTMP_EVE_CONNECTING);
    
    rtmp = RTMP_Alloc();
    if (NULL == rtmp)
    {
        log_print(HT_LOG_ERR, "%s, RTMP_Alloc failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    RTMP_Init(rtmp);

    // set connection timeout,default 30s
    rtmp->Link.timeout = 30;

    if (RTMP_SetupURL(rtmp, m_url) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, RTMP_SetupURL failed. url=%s\r\n", __FUNCTION__, m_url);
        goto FAILED;
    }

    rtmp->Link.lFlags |= RTMP_LF_LIVE;
    rtmp->m_sb.sb_observe = &m_bRunning;
    
    RTMP_SetBufferMS(rtmp, DEF_BUFTIME);

    if (!RTMP_Connect(rtmp, NULL))
    {
        log_print(HT_LOG_ERR, "%s, RTMP_Connect failed. url=%s\r\n", __FUNCTION__, m_url);
        goto FAILED;
    }

    if (!RTMP_ConnectStream(rtmp, 0))
    {
        log_print(HT_LOG_ERR, "%s, RTMP_ConnectStream failed. url=%s\r\n", __FUNCTION__, m_url);
        goto FAILED;
    }
    
    m_pRtmp = rtmp;
    
    return TRUE;

FAILED: 

    if (rtmp)
    {
        RTMP_Close(rtmp);
        RTMP_Free(rtmp);
    }

    return FALSE;
}

int CRtmpClient::rtmp_h264_rx(RTMPPacket * packet)
{
    uint8 * data = (uint8 *) packet->m_body;
    uint32 dataLen = packet->m_nBodySize;

    if (data[1] == 0x00)   // AVC sequence header
    {
        if (dataLen < 15)
        {
            log_print(HT_LOG_WARN, "%s, dataLen=%d\r\n", __FUNCTION__, dataLen);
            return 0;
        }
    
        uint32 offset = 10;
        int spsnum = (data[offset++] & 0x1f);
        int i = 0;
        
        while (i < spsnum)
        {
            uint32 spslen = (data[offset] & 0x000000FF) << 8 | (data[offset + 1] & 0x000000FF);
            
            offset += 2;

            if (offset + spslen > dataLen)
            {
                return -1;
            }

            if (0 == m_nSpsLen && spslen <= sizeof(m_pSps) + 4)
            {
                m_pSps[0] = 0;
                m_pSps[1] = 0;
                m_pSps[2] = 0;
                m_pSps[3] = 1;
                m_nSpsLen = spslen + 4;
                memcpy(m_pSps + 4, data + offset, spslen);
            }
            
            offset += spslen;
            i++;
        }
        
        int ppsnum = (data[offset++] & 0x1f);
        i = 0;
        
        while (i < ppsnum)
        {
            uint32 ppslen = (data[offset] & 0x000000FF) << 8 | (data[offset + 1] & 0x000000FF);
            
            offset += 2;

            if (offset + ppslen > dataLen)
            {
                return -1;
            }
            
            if (0 == m_nPpsLen && ppslen <= sizeof(m_pPps) + 4)
            {
                m_pPps[0] = 0;
                m_pPps[1] = 0;
                m_pPps[2] = 0;
                m_pPps[3] = 1;
                m_nPpsLen = ppslen + 4;
                memcpy(m_pPps + 4, data + offset, ppslen);
            }
            
            offset += ppslen;
            i++;
        }

        if (!m_bVideoReady)
        {
            m_bVideoReady = TRUE;
            rtmp_send_notify(RTMP_EVE_VIDEOREADY);
        }

        if (m_nSpsLen > 0 && m_nPpsLen > 0)
        {
            rtmp_video_data_cb(m_pSps, m_nSpsLen, 0);
            rtmp_video_data_cb(m_pPps, m_nPpsLen, 0);
        }
    }
    else if (data[1] == 0x01)    // AVC NALU
    {
        if (dataLen < 10)
        {
            log_print(HT_LOG_WARN, "%s, dataLen=%d\r\n", __FUNCTION__, dataLen);
            return 0;
        }
        
        int len = 0;
        int num = 5;

        while (num < (int)packet->m_nBodySize)
        {
            len = (data[num] & 0x000000FF) << 24 | (data[num + 1] & 0x000000FF) << 16 | 
                  (data[num + 2] & 0x000000FF) << 8 | (data[num + 3] & 0x000000FF);

            num = num + 4;

            if (data[num] != 0 || data[num+1] != 0 || data[num+2] != 0 || data[num+3] != 1)
            {
                data[num-4]= 0;
                data[num-3]= 0;
                data[num-2]= 0;
                data[num-1]= 1;

                rtmp_video_data_cb(data + num - 4, len + 4, packet->m_nTimeStamp);
            }
            else
            {
                rtmp_video_data_cb(data + num, len, packet->m_nTimeStamp);
            }
            
            num = num + len;
        }
    }
    else
    {
        log_print(HT_LOG_WARN, "%s, tag type [%02x]!!!\r\n", __FUNCTION__, data[1]);
    }

    return 0;
}

int CRtmpClient::rtmp_h265_rx(RTMPPacket * packet)
{
    uint8 * data = (uint8 *) packet->m_body;
    uint32 dataLen = packet->m_nBodySize;
    
    if (data[1] == 0x00)   // AVC sequence header
    {
        if (dataLen < 32)
        {
            log_print(HT_LOG_WARN, "%s, dataLen=%d\r\n", __FUNCTION__, dataLen);
            return 0;
        }
        
        uint32 offset = 5 + 22;
        int numOfArrays = data[offset++];

        for (int i=0; i<numOfArrays; i++)
        {
            int naluType = data[offset++];
            int numNalus = (data[offset] & 0x000000FF) << 8 | (data[offset + 1] & 0x000000FF);

            offset += 2;

            for (int j = 0; j < numNalus; j++)
            {
                uint32 naluLen = (data[offset] & 0x000000FF) << 8 | (data[offset + 1] & 0x000000FF);

                offset += 2;

                if (offset + naluLen > dataLen)
                {
                    return -1;
                }
            
                if (naluType == HEVC_NAL_VPS && 0 == m_nVpsLen && naluLen <= sizeof(m_pVps) + 4)
                {
                    m_pVps[0] = 0;
                    m_pVps[1] = 0;
                    m_pVps[2] = 0;
                    m_pVps[3] = 1;
                    memcpy(m_pVps+4, data+offset, naluLen);
                    m_nVpsLen = naluLen + 4;
                }
                else if (naluType == HEVC_NAL_SPS && 0 == m_nSpsLen && naluLen <= sizeof(m_pSps) + 4)
                {
                    m_pSps[0] = 0;
                    m_pSps[1] = 0;
                    m_pSps[2] = 0;
                    m_pSps[3] = 1;
                    memcpy(m_pSps+4, data+offset, naluLen);
                    m_nSpsLen = naluLen + 4;
                }
                else if (naluType == HEVC_NAL_PPS && 0 == m_nPpsLen && naluLen <= sizeof(m_pPps) + 4)
                {
                    m_pPps[0] = 0;
                    m_pPps[1] = 0;
                    m_pPps[2] = 0;
                    m_pPps[3] = 1;
                    memcpy(m_pPps+4, data+offset, naluLen);
                    m_nPpsLen = naluLen + 4;
                }

                offset += naluLen;
            }
        }

        if (!m_bVideoReady)
        {
            m_bVideoReady = TRUE;
            rtmp_send_notify(RTMP_EVE_VIDEOREADY);
        }

        if (m_nVpsLen > 0 && m_nSpsLen > 0 && m_nPpsLen > 0)
        {
            rtmp_video_data_cb(m_pVps, m_nVpsLen, 0);
            rtmp_video_data_cb(m_pSps, m_nSpsLen, 0);
            rtmp_video_data_cb(m_pPps, m_nPpsLen, 0);
        }
    }
    else if (data[1] == 0x01)    // AVC NALU
    {
        if (dataLen < 10)
        {
            log_print(HT_LOG_WARN, "%s, dataLen=%d\r\n", __FUNCTION__, dataLen);
            return 0;
        }
    
        int len = 0;
        int num = 5;

        while (num < (int)packet->m_nBodySize)
        {
            len = (data[num] & 0x000000FF) << 24 | (data[num + 1] & 0x000000FF) << 16 | 
                  (data[num + 2] & 0x000000FF) << 8 | (data[num + 3] & 0x000000FF);

            num = num + 4;

            if (data[num] != 0 || data[num+1] != 0 || data[num+2] != 0 || data[num+3] != 1)
            {
                data[num-4]= 0;
                data[num-3]= 0;
                data[num-2]= 0;
                data[num-1]= 1;

                rtmp_video_data_cb(data + num - 4, len + 4, packet->m_nTimeStamp);
            }
            else
            {
                rtmp_video_data_cb(data + num, len, packet->m_nTimeStamp);
            }
            
            num = num + len;
        }
    }
    else
    {
        log_print(HT_LOG_WARN, "%s, tag type [%02x]!!!\r\n", __FUNCTION__, data[1]);
    }

    return 0;
}

int CRtmpClient::rtmp_video_rx(RTMPPacket * packet)
{
    int ret = 0;
    uint8 v_type = (uint8) packet->m_body[0];
    v_type = (v_type & 0x0f);

    if (v_type == 2)        // Sorenson H.263
    {
    }
    else if (v_type == 3)   // Screen video
    {
    }
    else if (v_type == 4)   // On2 VP6
    {
    }
    else if (v_type == 5)   // On2 VP6 with alpha channel
    {
    }
    else if (v_type == 6)   // Screen video version 2
    {
    }
    else if (v_type == 7)   // AVC
    {
        m_nVideoCodec = VIDEO_CODEC_H264;

        ret = rtmp_h264_rx(packet);
    }
    else if (v_type == 12)  // H265
    {
        m_nVideoCodec = VIDEO_CODEC_H265;

        ret = rtmp_h265_rx(packet);
    }

    return ret;
}

int CRtmpClient::rtmp_g711_rx(RTMPPacket * packet)
{
    uint8 * data = (uint8 *)packet->m_body;

    if (!m_bAudioReady)
    {
        m_nChannels = ((data[0] & 0x01) ? 2 : 1);
        m_nSamplerate = 8000;
        m_bAudioReady = TRUE;

        rtmp_send_notify(RTMP_EVE_AUDIOREADY);
    }

    rtmp_audio_data_cb(data + 1, packet->m_nBodySize - 1, packet->m_nTimeStamp);

    return 0;
}

int CRtmpClient::rtmp_aac_rx(RTMPPacket * packet)
{
    uint8 * data = (uint8 *)packet->m_body;

    if (packet->m_nBodySize < 4)
    {
        return -1;
    }
    
    // AAC Packet Type: 0x00 - AAC Sequence Header; 0x01 - AAC Raw
    if (data[1] == 0x00)
    {
        int nAudioSampleType;

        memcpy(m_pAudioConfig, data+2, 2);
        m_nAudioConfigLen = 2;
        m_nChannels = ((data[0] & 0x01) ? 2 : 1);   // Whether stereo (0: Mono, 1: Stereo)
        nAudioSampleType = (data[0] & 0x0c) >> 2;   // Audio sample rate (0: 5.5kHz, 1:11KHz, 2:22 kHz, 3:44 kHz)
        
        switch (nAudioSampleType)
        {
        case 0:
            m_nSamplerate = 5500;
            break;

        case 1:
            m_nSamplerate = 11025;
            break;

        case 2:
            m_nSamplerate = 22050;
            break; 

        case 3:
            m_nSamplerate = 44100;
            break;

        default:
            m_nSamplerate = 44100;
            break;
        }

        uint16 audioSpecificConfig = 0;    
        
        audioSpecificConfig = (data[2] & 0xff) << 8;
        audioSpecificConfig += 0x00ff & data[3];
        
        uint8 nSampleFrequencyIndex = (audioSpecificConfig & 0x0780) >> 7;
        uint8 nChannels = (audioSpecificConfig & 0x78) >> 3;

        m_nChannels = nChannels;

        switch (nSampleFrequencyIndex)
        {
        case 0:
            m_nSamplerate = 96000;
            break;

        case 1:
            m_nSamplerate = 88200;
            break;
            
        case 2:
            m_nSamplerate = 64000;
            break;
            
        case 3:
            m_nSamplerate = 48000;
            break;
            
        case 4:
            m_nSamplerate = 44100;
            break;
            
        case 5:
            m_nSamplerate = 32000;
            break;
            
        case 6:
            m_nSamplerate = 24000;
            break;

        case 7:
            m_nSamplerate = 22050;
            break;

        case 8:
            m_nSamplerate = 16000;
            break;     

        case 9:
            m_nSamplerate = 12000;
            break;

        case 10:
            m_nSamplerate = 11025;
            break;

        case 11:
            m_nSamplerate = 8000;
            break;

        case 12:
            m_nSamplerate = 7350;
            break;
            
        default:
            m_nSamplerate = 44100;
            break;
        }

        if (!m_bAudioReady)
        {
            m_bAudioReady = TRUE;
            rtmp_send_notify(RTMP_EVE_AUDIOREADY);
        }
    }
    else if (data[1] == 0x01)
    {
        rtmp_audio_data_cb(data + 2, packet->m_nBodySize - 2, packet->m_nTimeStamp);
    }

    return 0;
}

int CRtmpClient::rtmp_audio_rx(RTMPPacket * packet)
{
    int ret = 0;
    uint8 a_type = (uint8) packet->m_body[0];
    a_type = a_type >> 4;
    
    if (a_type == 0)        // Linear PCM, platform endian
    {
    }
    else if (a_type == 1)    //  ADPCM
    {
    }
    else if (a_type == 2)   // MP3
    {
    }
    else if (a_type == 3)   // Linear PCM, little endian
    {
    }
    else if (a_type == 7)   // G711A
    {
        m_nAudioCodec = AUDIO_CODEC_G711A;

        ret = rtmp_g711_rx(packet);
    }
    else if (a_type == 8)   // G711U
    {
        m_nAudioCodec = AUDIO_CODEC_G711U;

        ret = rtmp_g711_rx(packet);
    }
    else if (a_type == 10)    // AAC
    {
        m_nAudioCodec = AUDIO_CODEC_AAC;
        
        ret = rtmp_aac_rx(packet);
    }
    else if (a_type == 11)    // Speex
    {
    }
    else if (a_type == 14)    // MP3 8-Khz
    {
    }
    
    return ret;
}

int CRtmpClient::rtmp_metadata_rx(RTMPPacket * packet)
{
    AMFObject obj;    
    AVal val;    
    AMFObjectProperty * property;    
    AMFObject subObject;    
    
    int nRes = AMF_Decode(&obj, packet->m_body, packet->m_nBodySize, FALSE);    
    if (nRes < 0)    
    {
        log_print(HT_LOG_ERR, "%s, error decoding invoke packet\r\n", __FUNCTION__);
        return -1;    
    }

    AMF_Dump(&obj);

    for (int n = 0; n < obj.o_num; n++)
    {
        property = AMF_GetProp(&obj, NULL, n);
        if (NULL == property)
        {
            continue;
        }

        if (property->p_type == AMF_OBJECT || property->p_type == AMF_ECMA_ARRAY)
        {
            AMFProp_GetObject(property, &subObject);
            
            for (int m = 0; m < subObject.o_num; m++)
            {
                property = AMF_GetProp(&subObject, NULL, m);
                if (NULL == property)
                {
                    continue;
                }
                
                if (property->p_type == AMF_OBJECT)
                {
                }
                else if (property->p_type == AMF_BOOLEAN)
                {                            
                    int bVal = AMFProp_GetBoolean(property);
                    if (strncmp("stereo", property->p_name.av_val, property->p_name.av_len) == 0)
                    {
                        m_nChannels = bVal > 0 ? 2 : 1;
                    }
                }
                else if (property->p_type == AMF_NUMBER)
                {
                    double dVal = AMFProp_GetNumber(property);
                    if (strncmp("width", property->p_name.av_val, property->p_name.av_len) == 0)
                    {
                        m_nWidth = (int)dVal;
                    }
                    else if (strcasecmp("height", property->p_name.av_val) == 0)
                    {
                        m_nHeight = (int)dVal;
                    }
                    else if (strcasecmp("framerate", property->p_name.av_val) == 0)
                    {
                        m_nFrameRate = dVal;
                    }
                    else if (strcasecmp("videocodecid", property->p_name.av_val) == 0)
                    {
                        switch ((int)dVal)
                        {
                        case 7:
                            m_nVideoCodec = VIDEO_CODEC_H264;
                            break;
                        case 12:
                            m_nVideoCodec = VIDEO_CODEC_H265;
                            break;
                        }
                    }
                    else if (strcasecmp("audiosamplerate", property->p_name.av_val) == 0)
                    {
                        m_nSamplerate = (int)dVal;
                    }
                    else if (strcasecmp("audiosamplesize", property->p_name.av_val) == 0)
                    {
                    }
                    else if (strcasecmp("audiocodecid", property->p_name.av_val) == 0)
                    {
                        switch ((int)dVal)
                        {
                        case 7:
                            m_nAudioCodec = AUDIO_CODEC_G711A;
                            break;
                        case 8:
                            m_nAudioCodec = AUDIO_CODEC_G711U;
                            break;
                        case 10:
                            m_nAudioCodec = AUDIO_CODEC_AAC;
                            break;    
                        }
                    }
                    else if (strcasecmp("filesize", property->p_name.av_val) == 0)
                    {
                    }
                }
                else if (property->p_type == AMF_STRING)
                {
                    AMFProp_GetString(property, &val);
                }
            }
        }
        else
        {
            AMFProp_GetString(property, &val);
        }
    }    

    AMF_Reset(&obj);

    if (m_nVideoCodec != VIDEO_CODEC_NONE && !m_bVideoReady)
    {
        m_bVideoReady = TRUE;
        rtmp_send_notify(RTMP_EVE_VIDEOREADY);
    }
    
    if (m_nAudioCodec != AUDIO_CODEC_NONE && !m_bAudioReady)
    {
        m_bAudioReady = TRUE;
        rtmp_send_notify(RTMP_EVE_AUDIOREADY);
    }
    
    return 0;
}

void CRtmpClient::rx_thread()
{
    int ret = 0;
    
    if (!rtmp_connect())
    {
        rtmp_send_notify(RTMP_EVE_CONNFAIL);
        return;
    }

    RTMPPacket pc;
    memset(&pc, 0, sizeof(pc));

    while (m_bRunning)
    {
        if (!RTMP_IsConnected(m_pRtmp))
        {
            RTMPPacket_Free(&pc);
            break;
        }
            
        if (!RTMP_ReadPacket(m_pRtmp, &pc))
        {
            RTMPPacket_Free(&pc);
            break;
        }
        
        if (!RTMPPacket_IsReady(&pc) || !pc.m_nBodySize)
        {
            RTMPPacket_Free(&pc);
            continue;
        }

        if (pc.m_packetType == RTMP_PACKET_TYPE_VIDEO && RTMP_ClientPacket(m_pRtmp, &pc))
        {
            ret = rtmp_video_rx(&pc);
        }
        else if (pc.m_packetType == RTMP_PACKET_TYPE_AUDIO && RTMP_ClientPacket(m_pRtmp, &pc))
        {
            ret = rtmp_audio_rx(&pc);
        }
        else if (pc.m_packetType == RTMP_PACKET_TYPE_INFO && RTMP_ClientPacket(m_pRtmp, &pc))
        {
            rtmp_metadata_rx(&pc);
        }

        RTMPPacket_Free(&pc);

        if (ret < 0)
        {
            log_print(HT_LOG_ERR, "%s, ret = %d\r\n", __FUNCTION__, ret);
            break;
        }
    }
    
    rtmp_send_notify(RTMP_EVE_STOPPED);
}

void CRtmpClient::rtmp_send_notify(int event)
{
    sys_os_mutex_enter(m_pMutex);
    
    if (m_pNotify)
    {
        m_pNotify(event, m_pUserdata);
    }

    sys_os_mutex_leave(m_pMutex);
}

void CRtmpClient::rtmp_video_data_cb(uint8 * p_data, int len, uint32 ts)
{
    if (m_nVideoInitTS == 0 && ts != 0)
    {
        m_nVideoInitTS = ts;
    }

    sys_os_mutex_enter(m_pMutex);
    if (m_pVideoCB)
    {
        m_pVideoCB(p_data, len, ts, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CRtmpClient::rtmp_audio_data_cb(uint8 * p_data, int len, uint32 ts)
{
    if (m_nAudioInitTS == 0 && ts != 0)
    {
        m_nAudioInitTS = ts;
    }
    
    sys_os_mutex_enter(m_pMutex);
    if (m_pAudioCB)
    {
        m_pAudioCB(p_data, len, ts, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

void CRtmpClient::get_h264_params()
{
    if (m_nSpsLen > 0)
    {
        rtmp_video_data_cb(m_pSps, m_nSpsLen, 0);
    }

    if (m_nPpsLen > 0)
    {
        rtmp_video_data_cb(m_pPps, m_nPpsLen, 0);
    }
}

BOOL CRtmpClient::get_h264_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len)
{
    if (m_nSpsLen > 0)
    {
        *sps_len = m_nSpsLen;
        memcpy(p_sps, m_pSps, m_nSpsLen);
    }

    if (m_nPpsLen > 0)
    {
        *pps_len = m_nPpsLen;
        memcpy(p_pps, m_pPps, m_nPpsLen);
    }

    return TRUE;
}

void CRtmpClient::get_h265_params()
{
    if (m_nVpsLen > 0)
    {
        rtmp_video_data_cb(m_pVps, m_nVpsLen, 0);
    }
    
    if (m_nSpsLen > 0)
    {
        rtmp_video_data_cb(m_pSps, m_nSpsLen, 0);
    }

    if (m_nPpsLen > 0)
    {
        rtmp_video_data_cb(m_pPps, m_nPpsLen, 0);
    }
}

BOOL CRtmpClient::get_h265_params(uint8 * p_sps, int * sps_len, uint8 * p_pps, int * pps_len, uint8 * p_vps, int * vps_len)
{
    if (m_nVpsLen > 0)
    {
        *vps_len = m_nVpsLen;
        memcpy(p_vps, m_pVps, m_nVpsLen);
    }
    
    if (m_nSpsLen > 0)
    {
        *sps_len = m_nSpsLen;
        memcpy(p_sps, m_pSps, m_nSpsLen);
    }

    if (m_nPpsLen > 0)
    {
        *pps_len = m_nPpsLen;
        memcpy(p_pps, m_pPps, m_nPpsLen);
    }

    return TRUE;
}

void CRtmpClient::set_notify_cb(rtmp_notify_cb notify, void * userdata) 
{
    sys_os_mutex_enter(m_pMutex);    
    m_pNotify = notify;
    m_pUserdata = userdata;
    sys_os_mutex_leave(m_pMutex);
}

void CRtmpClient::set_video_cb(rtmp_video_cb cb) 
{
    sys_os_mutex_enter(m_pMutex);
    m_pVideoCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CRtmpClient::set_audio_cb(rtmp_audio_cb cb)
{
    sys_os_mutex_enter(m_pMutex);
    m_pAudioCB = cb;
    sys_os_mutex_leave(m_pMutex);
}


