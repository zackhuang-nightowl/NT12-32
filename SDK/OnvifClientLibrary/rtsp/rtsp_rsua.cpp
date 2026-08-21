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
#include "sys_buf.h"
#include "word_analyse.h"
#include "rtsp_parse.h"
#include "rtsp_rsua.h"
#include "rtsp_srv.h"
#include "rtsp_stream.h"
#include "rtsp_cfg.h"
#include "rtsp_util.h"
#include "net_util.h"
#include "base64.h"

#ifdef RTSP_BACKCHANNEL
#include "rtsp_srv_backchannel.h"
#endif
#ifdef RTSP_OVER_HTTP
#include "http.h"
#include "http_srv.h"
#endif
#ifdef RTSP_OVER_WEBSOCKET
#include "ws.h"
#include "http_srv.h"
#endif

/***********************************************************************/

extern RTSP_CLASS    g_rtsp;


/***********************************************************************/

BOOL rsua_get_transport_info(RSSUA * p_rua, char * transport_buf, int av_t)
{
    char * p_s = transport_buf;
    char * p_e = p_s;

    while (*p_e != ';' && *p_e != '\0')
    {
        p_e++;
    }
    
    if (*p_e == '\0')
    {
        return FALSE;
    }
    
    *p_e = '\0';
    
    if (strcasecmp(p_s, "RTP/AVP") == 0 || strcasecmp(p_s, "RTP/AVP/UDP") == 0)
    {
        p_rua->rtp_tcp = 0;
    }    
    else if (strcasecmp(p_s, "RTP/AVP/TCP") == 0)
    {
        p_rua->rtp_tcp = 1;
    }
    
    p_e++; 
    p_s = p_e;
    
    while (*p_e != ';' && *p_e != '\0')
    {
        p_e++;
    }
    
    if (*p_e == '\0')
    {
        return FALSE;
    }
    
    *p_e = '\0';
    
    if (strcasecmp(p_s, "unicast") == 0)
    {
        p_rua->rtp_unicast = 1;
    }
    else if (strcasecmp(p_s, "multicast") == 0)
    {
        p_rua->rtp_unicast = 0;
    }
    else
    {
        return FALSE;
    }
    
    p_e++; 
    p_s = p_e;
    
    while (*p_e != ';' && *p_e != '\0')
    {
        p_e++;
    }
    
    if (p_rua->rtp_tcp == 1)
    {
        *p_e = '\0';

        int il_len = strlen("interleaved=");
        if (memcmp(p_s, "interleaved=", il_len) != 0)
        {
            return FALSE;
        }
        
        p_s += il_len; 
        p_e = p_s;
        
        while (*p_e != '-' && *p_e != '\0')
        {
            p_e++;
        }
        
        if (*p_e == '\0')
        {
            return FALSE;
        }
        
        *p_e = '\0';

        if (!is_integer(p_s))
        {
            return FALSE;
        }
        
        int vv = atoi(p_s);
        if (vv > 255 || vv < 0)
        {
            return FALSE;
        }

        p_rua->channels[av_t].interleaved = vv;
    }
    else
    {
        char client_port[32];

        if (p_rua->rtp_unicast == 1)
        {
            if (get_name_value_pair(p_s, strlen(p_s), "client_port", client_port, sizeof(client_port)-1) == FALSE)
            {
                return FALSE;
            }    
        }
        else
        {
            if (get_name_value_pair(p_s, strlen(p_s), "port", client_port, sizeof(client_port)-1) == FALSE)
            {
                // Transport: RTP/AVP;multicast, there is not port
                return TRUE;
            }
        }
        
        int i = 0;
        
        while (client_port[i] != '-' && i < 31 && client_port[i] != '\0')
        {
            i++;
        }
        
        if (i >= 31)
        {
            return FALSE;
        }
        
        client_port[i] = '\0';
        
        int rport = atoi(client_port);
        if (rport >= 0xFFFF || rport < 0)
        {
            return FALSE;
        }

        p_rua->channels[av_t].r_port = rport;
    }

    return TRUE;
}

BOOL rsua_get_play_range_info(RSSUA * p_rua, char * range_buf)
{
    int timeType = 0; // Relative Time
    double rangeStart = 0, rangeEnd = 0;
    double start, end;
    int numCharsMatched1 = 0, numCharsMatched2 = 0, numCharsMatched3 = 0;
    
    if (sscanf(range_buf, "npt = %lf - %lf", &start, &end) == 2) 
    {
        rangeStart = start;
        rangeEnd = end;
    } 
    else if (sscanf(range_buf, "npt = %n%lf -", &numCharsMatched1, &start) == 1) 
    {
        if (range_buf[numCharsMatched1] == '-') 
        {
            // special case for "npt = -<endtime>", which matches here:
            rangeStart = 0.0;
            rangeEnd = -start;
        } 
        else 
        {
            rangeStart = start;
            rangeEnd = 0.0;
        }
    } 
    else if (sscanf(range_buf, "npt = now - %lf", &end) == 1) 
    {
        rangeStart = 0.0;
        rangeEnd = end;
    } 
    else if (sscanf(range_buf, "npt = now -%n", &numCharsMatched2) == 0 && numCharsMatched2 > 0) 
    {
        rangeStart = 0.0;
        rangeEnd = 0.0;
    }
    else if (sscanf(range_buf, "clock = %n", &numCharsMatched3) == 0 && numCharsMatched3 > 0) 
    {
        timeType = 1; // Absolute time
        rangeStart = rangeEnd = 0.0;

        char const* utcTimes = &range_buf[numCharsMatched3];
        size_t len = strlen(utcTimes) + 1;
        char* as = new char[len];
        char* ae = new char[len];
        int sscanfResult = sscanf(utcTimes, "%[^-]-%[^\r\n]", as, ae);
        if (sscanfResult == 2) 
        {
            time_t t;
        
            if (rtsp_parse_xsd_datetime(as, &t))
            {
                rangeStart = t;
            }

            if (rtsp_parse_xsd_datetime(ae, &t))
            {
                rangeEnd = t;
            }
        } 
        else if (sscanfResult == 1) 
        {
            time_t t;
        
            if (rtsp_parse_xsd_datetime(as, &t))
            {
                rangeStart = t;
            }
        } 
        else 
        {
            delete[] as; delete[] ae;
            return FALSE;
        }

        delete[] as; delete[] ae;
    } 
    else 
    {
        return FALSE; // The header is malformed
    }

    p_rua->play_range_type = timeType;
    p_rua->play_range_begin = (int64)rangeStart;
    p_rua->play_range_end = (int64)rangeEnd;

    return TRUE;    
}

#ifdef MEDIA_PUSHER

BOOL rsua_get_sdp_video_desc(RSSUA * p_rua, const char * key, int * pt, char * p_sdp, int max_len)
{
    int payload_type = 0, i;
    int rtpmap_len = (int)strlen("a=rtpmap:");

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[AV_VIDEO_CH].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", rtpmap_len) == 0)
        {
            char pt_buf[16];
            char code_buf[64];
            int next_offset = 0;
            ptr += rtpmap_len;

            if (get_line_word(ptr, 0, (int)strlen(ptr), pt_buf, sizeof(pt_buf), &next_offset, WORD_TYPE_NUM) == FALSE)
            {
                return FALSE;
            }
            
            get_line_word(ptr, next_offset, (int)strlen(ptr)-next_offset, code_buf, sizeof(code_buf),  &next_offset, WORD_TYPE_STRING);
            
            if (memcmp(code_buf, key, strlen(key)) == 0)
            {
                payload_type = atoi(pt_buf);
                if (payload_type <= 0)
                {
                    return FALSE;
                }
                
                break;
            }
        }
    }

    if (payload_type == 0)
    {
        return FALSE;
    }
    
    if (pt)
    {
        *pt = payload_type;
    }

    if (p_sdp == NULL)    // not need sps, pps parameter
    {
        return TRUE;
    }
    
    p_sdp[0] = '\0';

    char fmtp_buf[32] = {'\0'};
    int fmtp_len = snprintf(fmtp_buf, sizeof(fmtp_buf), "a=fmtp:%d", payload_type);

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[AV_VIDEO_CH].cap_desc[i];
        if (memcmp(ptr, fmtp_buf, fmtp_len) == 0)
        {
            ptr += rtpmap_len+1;
            strncpy(p_sdp, ptr, max_len);
            break;
        }
    }

    return TRUE;
}

BOOL rsua_get_sdp_audio_desc(RSSUA * p_rua, const char * key, int * pt, char * p_sdp, int max_len)
{
    int payload_type = 0, i;
    int rtpmap_len = (int)strlen("a=rtpmap:");

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[AV_AUDIO_CH].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", rtpmap_len) == 0)
        {
            char pt_buf[16];
            char code_buf[64];
            int next_offset = 0;
            ptr += rtpmap_len;

            if (get_line_word(ptr, 0, (int)strlen(ptr), pt_buf, sizeof(pt_buf), &next_offset, WORD_TYPE_NUM) == FALSE)
            {
                return FALSE;
            }
            
            get_line_word(ptr, next_offset, (int)strlen(ptr)-next_offset, code_buf, sizeof(code_buf),  &next_offset, WORD_TYPE_STRING);
            
            if (memcmp(code_buf, key, strlen(key)) == 0)
            {
                payload_type = atoi(pt_buf);                
                if (payload_type <= 0)
                {
                    return FALSE;
                }
                
                break;
            }
        }
    }

    if (payload_type == 0)
    {
        return FALSE;
    }
    
    if (pt)
    {
        *pt = payload_type;
    }

    if (p_sdp == NULL)    // not need sps, pps parameter
    {
        return TRUE;
    }
    
    p_sdp[0] = '\0';

    char fmtp_buf[32] = {'\0'};
    int fmtp_len = snprintf(fmtp_buf, sizeof(fmtp_buf), "a=fmtp:%d", payload_type);

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[AV_AUDIO_CH].cap_desc[i];
        if (memcmp(ptr, fmtp_buf, fmtp_len) == 0)
        {
            ptr += rtpmap_len+1;
            strncpy(p_sdp, ptr, max_len);
            break;
        }
    }

    return TRUE;
}

BOOL rsua_get_sdp_h264_desc(RSSUA * p_rua, int * pt, char * p_sdp, int max_len)
{
    return rsua_get_sdp_video_desc(p_rua, "H264/90000", pt, p_sdp, max_len);
}

BOOL rsua_get_sdp_h265_desc(RSSUA * p_rua, int * pt, char * p_sdp, int max_len)
{
    return rsua_get_sdp_video_desc(p_rua, "H265/90000", pt, p_sdp, max_len);
}

BOOL rsua_get_sdp_mp4_desc(RSSUA * p_rua, int * pt, char * p_sdp, int max_len)
{
    return rsua_get_sdp_video_desc(p_rua, "MP4V-ES/90000", pt, p_sdp, max_len);
}

BOOL rsua_get_sdp_mp4_params(RSSUA * p_rua, int * pt, char * p_cfg, int max_len)
{
    BOOL ret = FALSE;
    char sdp[1024] = {'\0'};
    
    if (rsua_get_sdp_mp4_desc(p_rua, pt, sdp, sizeof(sdp)) == FALSE)
    {
        return FALSE;
    }

    char * p_substr = strstr(sdp, "config=");
    if (p_substr != NULL)
    {
        p_substr += strlen("config=");
        char * p_tmp = p_substr;

        while(*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        int cfg_len = (int)(p_tmp - p_substr);
        if(cfg_len < max_len)
        {
            memcpy(p_cfg, p_substr, cfg_len);
            p_cfg[cfg_len] = '\0';
            ret = TRUE;
        }
        else
        {
            ret = FALSE;
        }
    }

    return ret;
}

BOOL rsua_get_sdp_aac_desc(RSSUA * p_rua, int * pt, char * p_sdp, int max_len)
{
    return rsua_get_sdp_audio_desc(p_rua, "MPEG4-GENERIC", pt, p_sdp, max_len);
}

BOOL rsua_get_sdp_aac_params(RSSUA * p_rua, int *pt, int *sizelength, int *indexlength, int *indexdeltalength, char * p_cfg, int max_len)
{
    char sdp[1024] = {'\0'};
    
    if (rsua_get_sdp_aac_desc(p_rua, pt, sdp, sizeof(sdp)) == FALSE)
    {
        return FALSE;
    }

    char * p_substr = strstr(sdp, "config=");
    if (p_substr != NULL)
    {
        p_substr += strlen("config=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        int len = (int)(p_tmp - p_substr);
        if (len < max_len)
        {
            memcpy(p_cfg, p_substr, len);
            p_cfg[len] = '\0';
        }
        else
        {
            return FALSE;
        }
    }

    p_substr = strstr(sdp, "sizelength=");
    if (p_substr != NULL)
    {
        p_substr += strlen("sizelength=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        int len = (int)(p_tmp - p_substr);
        if (len > 0)
        {
            *sizelength = atoi(p_substr);
        }
    }

    p_substr = strstr(sdp, "indexlength=");
    if (p_substr != NULL)
    {
        p_substr += strlen("indexlength=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        int len = (int)(p_tmp - p_substr);
        if (len > 0)
        {
            *indexlength = atoi(p_substr);
        }
    }

    p_substr = strstr(sdp, "indexdeltalength=");
    if (p_substr != NULL)
    {
        p_substr += strlen("indexdeltalength=");
        char * p_tmp = p_substr;

        while (*p_tmp != ' ' && *p_tmp != ';' && *p_tmp != '\0')
        {
            p_tmp++;
        }
        
        int len = (int)(p_tmp - p_substr);
        if (len > 0)
        {
            *indexdeltalength = atoi(p_substr);
        }
    }

    return TRUE;
}

#endif

/***********************************************************************/

HRTSP_MSG * rsua_build_security_response(RSSUA * p_rua)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 401;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "401 Unauthorized");
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());    

    snprintf(p_rua->auth_info.auth_nonce, sizeof(p_rua->auth_info.auth_nonce), "%08X%08X", rand(), rand());
    strcpy(p_rua->auth_info.auth_realm, "happytimesoft");
    
    rtsp_add_tx_msg_line(tx_msg, "WWW-Authenticate", 
        "Digest realm=\"%s\", nonce=\"%s\"", 
        p_rua->auth_info.auth_realm, p_rua->auth_info.auth_nonce);

    return tx_msg;
}

HRTSP_MSG * rsua_build_options_response(RSSUA * p_rua)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 200;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "200 OK");
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());
#ifdef MEDIA_PUSHER
    rtsp_add_tx_msg_line(tx_msg, "Public", 
        "DESCRIBE, SETUP, PLAY, PAUSE, OPTIONS, TEARDOWN, GET_PARAMETER, SET_PARAMETER, ANNOUNCE, RECORD");
#else
    rtsp_add_tx_msg_line(tx_msg, "Public", 
        "DESCRIBE, SETUP, PLAY, PAUSE, OPTIONS, TEARDOWN, GET_PARAMETER, SET_PARAMETER");
#endif

    return tx_msg;
}

HRTSP_MSG * rsua_build_descibe_response(RSSUA * p_rua)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 200;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "200 OK");
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());

    if (p_rua->sid[0] != '\0')
    {
        rtsp_add_tx_msg_line(tx_msg, "Session", "%s", p_rua->sid);
    }
    
    rtsp_add_tx_msg_line(tx_msg, "Content-Base", "%s", p_rua->cbase);
    rtsp_add_tx_msg_line(tx_msg, "Content-type", "application/sdp");

    if (!rsua_build_sdp_msg(p_rua, tx_msg))
    {
        rtsp_free_msg(tx_msg);
        return NULL;
    }
    
    int sdp_len = rtsp_calc_sdp_length(tx_msg);
    rtsp_add_tx_msg_line(tx_msg, "Content-Length", "%d", sdp_len);

    return tx_msg;
}

HRTSP_MSG * rsua_build_setup_response(RSSUA * p_rua, int av_t)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 200;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "200 OK");
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());
    
    if (p_rua->sid[0] == '\0')
    {
        snprintf(p_rua->sid, sizeof(p_rua->sid), "%u", rand());
    }
    
    rtsp_add_tx_msg_line(tx_msg, "Session", "%s", p_rua->sid);

    if (p_rua->rtp_tcp) // tcp
    {
        rtsp_add_tx_msg_line(tx_msg, "Transport", "RTP/AVP/TCP;unicast;interleaved=%u-%u",
            p_rua->channels[av_t].interleaved, p_rua->channels[av_t].interleaved+1);
    }        
    else if (1 == p_rua->rtp_unicast) // udp unicast
    {
        rtsp_add_tx_msg_line(tx_msg, "Transport", "RTP/AVP/UDP;unicast;client_port=%u-%u;server_port=%u-%u",
            p_rua->channels[av_t].r_port, p_rua->channels[av_t].r_port+1,
            p_rua->channels[av_t].l_port, p_rua->channels[av_t].l_port+1);
    }
    else // udp multicast
    {
        RTSP_SRV * p_srv = &g_rtsp.rtsp;

#ifdef RTSPS
        if (p_rua->rtsps)
        {
            p_srv = &g_rtsp.rtsps;
        }
#endif

        if (p_rua->sockaddr.ipv6_flag)
        {
            rtsp_add_tx_msg_line(tx_msg, "Transport", "RTP/AVP;multicast;destination=%s;source=%s;port=%u-%u;ttl=64",
                p_rua->channels[av_t].destination, p_srv->ipv6_host, 
                p_rua->channels[av_t].r_port, p_rua->channels[av_t].r_port+1);
        }
        else
        {
            rtsp_add_tx_msg_line(tx_msg, "Transport", "RTP/AVP;multicast;destination=%s;source=%s;port=%u-%u;ttl=64",
                p_rua->channels[av_t].destination, p_srv->ipv4_host, 
                p_rua->channels[av_t].r_port, p_rua->channels[av_t].r_port+1);
        }
    }

    return tx_msg;
}

HRTSP_MSG * rsua_build_play_response(RSSUA * p_rua)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 200;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "200 OK");
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());

    if (p_rua->scale_flag)
    {
        rtsp_add_tx_msg_line(tx_msg, "Scale", "%f", p_rua->scale / 100.0);
    }

#ifdef RTSP_REPLAY
    if (p_rua->replay)
    {
        if (p_rua->play_range && p_rua->play_range_begin != 0)
        {
            char range[256] = {'\0'};
            char start[80] = {'\0'}, end[80] = {'\0'};
            time_t st = p_rua->play_range_begin;
            time_t et = p_rua->play_range_end;
            struct tm *t1;    

            t1 = gmtime(&st);

            snprintf(start, sizeof(start)-1, "%04d%02d%02dT%02d%02d%02dZ",
                t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
                t1->tm_hour, t1->tm_min, t1->tm_sec);

            if (et > 0)
            {
                t1 = gmtime(&et);

                snprintf(end, sizeof(end)-1, "%04d%02d%02dT%02d%02d%02dZ",
                    t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
                    t1->tm_hour, t1->tm_min, t1->tm_sec);

                snprintf(range, sizeof(range)-1, "clock=%s-%s", start, end);    
            }
            else
            {
                snprintf(range, sizeof(range)-1, "clock=%s-", start);
            }

            rtsp_add_tx_msg_line(tx_msg, "Range", range);
        }
    }
    else
#endif

    if (p_rua->play_range)
    {
        if (p_rua->play_range_end > 0)
        {
            rtsp_add_tx_msg_line(tx_msg, "Range", "npt=%.3f-%.3f", (double)p_rua->play_range_begin, (double)p_rua->play_range_end);
        }
        else
        {
            rtsp_add_tx_msg_line(tx_msg, "Range", "npt=%.3f-", (double)p_rua->play_range_begin);
        }
    }
    else
    {
        rtsp_add_tx_msg_line(tx_msg, "Range", "npt=%.3f-", p_rua->media_info.curpos/1000.0);
    }

    rtsp_add_tx_msg_line(tx_msg, "Session", "%s;timeout=%d", p_rua->sid, g_rtsp.session_timeout);

    char rtpinfo[4096] = {'\0'};
    char v_rtpinfo[1024] = {'\0'};
    char a_rtpinfo[1024] = {'\0'};
#ifdef RTSP_METADATA
    char m_rtpinfo[1024] = {'\0'};
#endif

    if (p_rua->media_info.has_video && p_rua->channels[AV_VIDEO_CH].setup)
    {
        snprintf(v_rtpinfo, sizeof(v_rtpinfo), "url=%s/%s;seq=%d;rtptime=%u", 
            p_rua->cbase, p_rua->channels[AV_VIDEO_CH].ctl, 
            ++p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_cnt, 
            get_rtp_timestamp(90000)); 
    }

    if (p_rua->media_info.has_audio && p_rua->channels[AV_AUDIO_CH].setup)
    {
        p_rua->audio_ts = get_rtp_timestamp(p_rua->media_info.a_info.samplerate);
        
        snprintf(a_rtpinfo, sizeof(a_rtpinfo), "url=%s/%s;seq=%d;rtptime=%u", 
            p_rua->cbase, p_rua->channels[AV_AUDIO_CH].ctl, 
            ++p_rua->channels[AV_AUDIO_CH].rtp_info.rtp_cnt, 
            p_rua->audio_ts);
    }

#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        snprintf(m_rtpinfo, sizeof(m_rtpinfo), "url=%s/%s;seq=%d;rtptime=%u", 
            p_rua->cbase, p_rua->channels[AV_METADATA_CH].ctl, 
            ++p_rua->channels[AV_METADATA_CH].rtp_info.rtp_cnt, 
            get_rtp_timestamp(90000)); 
    }
#endif

    int offset = 0;
    int mlen = sizeof(rtpinfo);

    if (p_rua->media_info.has_video && p_rua->channels[AV_VIDEO_CH].setup)
    {
        offset += snprintf(rtpinfo+offset, mlen-offset, "%s", v_rtpinfo);
    }

    if (p_rua->media_info.has_audio && p_rua->channels[AV_AUDIO_CH].setup)
    {
        if (offset != 0)
        {
            offset += snprintf(rtpinfo+offset, mlen-offset, "%s", ",");
        }
        
        offset += snprintf(rtpinfo+offset, mlen-offset, "%s", a_rtpinfo);
    }

#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        if (offset != 0)
        {
            offset += snprintf(rtpinfo+offset, mlen-offset, "%s", ",");
        }
        
        offset += snprintf(rtpinfo+offset, mlen-offset, "%s", m_rtpinfo);
    }
#endif

    if (rtpinfo[0] != '\0')
    {
        rtsp_add_tx_msg_line(tx_msg, "RTP-Info", "%s", rtpinfo);
    }

    return tx_msg;
}

HRTSP_MSG * rsua_build_response(RSSUA * p_rua, const char * resp_str)
{
    HRTSP_MSG * tx_msg = rtsp_get_msg_buf();
    if (tx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return NULL!!!\r\n", __FUNCTION__);
        return NULL;
    }

    tx_msg->msg_type = 1;
    tx_msg->msg_sub_type = 200;

    rtsp_add_tx_msg_fline(tx_msg, "RTSP/1.0", "%s", resp_str);
    rtsp_add_tx_msg_line(tx_msg, "Server", "%s", g_rtsp.srv_ver);
    rtsp_add_tx_msg_line(tx_msg, "CSeq", "%u", p_rua->cseq);
    rtsp_add_tx_msg_line(tx_msg, "Date", "%s", rtsp_get_utc_time());

    if (p_rua->sid[0] != '\0')
    {
        rtsp_add_tx_msg_line(tx_msg, "Session", "%s", p_rua->sid);
    }

    return tx_msg;
}

BOOL rsua_build_sdp_msg(RSSUA * p_rua, HRTSP_MSG * tx_msg)
{
    int i, j;
    HT_SOCKADDR * sockaddr;
    RTSP_SRV * p_srv;
    
    if (tx_msg == NULL)
    {
        return FALSE;
    }
    
    rtsp_add_tx_msg_sdp_line(tx_msg, "v", "0");

#ifdef RTSP_OVER_HTTP
    if (p_rua->rtsp_send)
    {
        HTTPCLN * p_http = (HTTPCLN *) p_rua->rtsp_send;
        sockaddr = (HT_SOCKADDR *) &p_http->sockaddr;
    }
    else
#endif
#ifdef RTSP_OVER_WEBSOCKET
    if (p_rua->http_cln)
    {
        HTTPCLN * p_http = (HTTPCLN *) p_rua->http_cln;
        sockaddr = (HT_SOCKADDR *) &p_http->sockaddr;
    }
    else
#endif
    {
        sockaddr = &p_rua->sockaddr;
    }

    p_srv = &g_rtsp.rtsp;

#ifdef RTSPS
    if (p_rua->rtsps)
    {
        p_srv = &g_rtsp.rtsps;
    }
#endif

    if (sockaddr->ipv6_flag)
    {
        rtsp_add_tx_msg_sdp_line(tx_msg, "o", "- 0 0 IN IP6 %s", p_srv->ipv6_host);
        rtsp_add_tx_msg_sdp_line(tx_msg, "c", "IN IP6 %s", p_srv->ipv6_host);
    }
    else if (sockaddr->ipv4_flag)
    {
        rtsp_add_tx_msg_sdp_line(tx_msg, "o", "- 0 0 IN IP4 %s", p_srv->ipv4_host);
        rtsp_add_tx_msg_sdp_line(tx_msg, "c", "IN IP4 %s", p_srv->ipv4_host);
    }
    else
    {
        return FALSE;
    }
    
    rtsp_add_tx_msg_sdp_line(tx_msg, "s", "session");
    rtsp_add_tx_msg_sdp_line(tx_msg, "t", "0 0");
    rtsp_add_tx_msg_sdp_line(tx_msg, "a", "control:*");

    if (!p_rua->media_info.is_file || p_rua->media_info.duration == 0)
    {
        rtsp_add_tx_msg_sdp_line(tx_msg, "a", "range:npt=0-");
    }
    else
    {
        rtsp_add_tx_msg_sdp_line(tx_msg, "a", "range:npt=0-%0.3f", p_rua->media_info.duration / 1000.0);
    }

    if (g_rtsp_cfg.multicast && 0 == p_rua->rtp_unicast)
    {
        rtsp_add_tx_msg_sdp_line(tx_msg, "a", "type:broadcast");
    }

    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].cap_count == 0)
        {
            continue;
        }
        
        int  offset = 0;
        char cap_buf[128];
        char media[20];

        for (j = 0; j < p_rua->channels[i].cap_count; j++)
        {
            offset += snprintf(cap_buf+offset, sizeof(cap_buf)-offset, "%u ", p_rua->channels[i].cap[j]);
        }
        
        if (offset > 0)
        {
            cap_buf[offset-1] = '\0';
        }

        if (AV_VIDEO_CH == i)
        {
            strcpy(media, "video");
        }
        else if (AV_AUDIO_CH == i || AV_BACK_CH == i)
        {
            strcpy(media, "audio");
        }
        else if (AV_METADATA_CH == i)
        {
            strcpy(media, "application");
        }

#ifdef RTSP_SRTP
        if (p_rua->srtp_enable)
        {
            rtsp_add_tx_msg_sdp_line(tx_msg, "m", "%s %u RTP/SAVP %s", media, p_rua->channels[i].l_port, cap_buf);
        }
        else
#endif
        rtsp_add_tx_msg_sdp_line(tx_msg, "m", "%s %u RTP/AVP %s", media, p_rua->channels[i].l_port, cap_buf);

        if (g_rtsp_cfg.multicast && 0 == p_rua->rtp_unicast)
        {
            if (p_rua->sockaddr.ipv6_flag)
            {
                rtsp_add_tx_msg_sdp_line(tx_msg, "c", "IN IP6 %s", p_rua->channels[i].destination);
            }
            else
            {
                rtsp_add_tx_msg_sdp_line(tx_msg, "c", "IN IP4 %s/255", p_rua->channels[i].destination);
            }
        }

        for (j = 0; j < MAX_AVN; j++)
        {
            char * mstr = p_rua->channels[i].cap_desc[j];
            if (mstr[0] != '\0')
            {
                rtsp_add_tx_msg_sdp_line(tx_msg, "", "%s", mstr);
            }
        }
        
        rtsp_add_tx_msg_sdp_line(tx_msg, "a", "control:%s", p_rua->channels[i].ctl);

#ifdef RTSP_SRTP
        if (p_rua->srtp_enable)
        {
            int len = SRTP_KEY_LENGTH + SRTP_SALT_LENGTH;
            char key[2 * (SRTP_KEY_LENGTH + SRTP_SALT_LENGTH)] = {'\0'};

            base64_encode(p_rua->channels[i].crypto_key, len, key, 2*len);

            rtsp_add_tx_msg_sdp_line(tx_msg, "a", "crypto:1 AES_CM_128_HMAC_SHA1_80 inline:%s", key);
        }
#endif
    }

    return TRUE;
}

int rtsp_calc_sdp_length(HRTSP_MSG * tx_msg)
{
    if (tx_msg == NULL)
    {
        return 0;
    }
    
    int offset = 0;

    HDRV * pHdrV = (HDRV *)pps_lookup_start(&(tx_msg->sdp_ctx));
    while (pHdrV != NULL) 
    {
        if (pHdrV->header[0] != '\0')
        {
            offset += strlen(pHdrV->header) + strlen(pHdrV->value_string) + 3;
        }    
        else
        {
            offset += strlen(pHdrV->value_string) + 2;
        }
        
        pHdrV = (HDRV *)pps_lookup_next(&(tx_msg->sdp_ctx), pHdrV);
    }
    pps_lookup_end(&(tx_msg->sdp_ctx));

    return offset;
}

void rsua_send_rtsp_msg(RSSUA * p_rua, HRTSP_MSG * tx_msg)
{
    int     slen;
    int     offset = 0;
    int     buflen;
    char  * tx_buf;    
    char    rtsp_tx_buffer[2048+32];

    if (tx_msg == NULL)
    {
        return;
    }
    
    tx_buf = rtsp_tx_buffer + 32;
    buflen = sizeof(rtsp_tx_buffer) - 32;

    offset += snprintf(tx_buf+offset, buflen-offset, "%s %s\r\n", tx_msg->first_line.header, tx_msg->first_line.value_string);
    
    HDRV * pHdrV = (HDRV *)pps_lookup_start(&(tx_msg->rtsp_ctx));
    while (pHdrV != NULL)
    {
        offset += snprintf(tx_buf+offset, buflen-offset, "%s: %s\r\n", pHdrV->header, pHdrV->value_string);
        pHdrV = (HDRV *)pps_lookup_next(&(tx_msg->rtsp_ctx), pHdrV);
    }
    pps_lookup_end(&(tx_msg->rtsp_ctx));

    offset += snprintf(tx_buf+offset, buflen-offset, "\r\n");

    if (tx_msg->sdp_ctx.node_num != 0)
    {
        pHdrV = (HDRV *)pps_lookup_start(&(tx_msg->sdp_ctx));
        while (pHdrV != NULL)
        {
            if ((strcmp(pHdrV->header, "pidf") == 0) || (strcmp(pHdrV->header, "text/plain") == 0))
            {
                offset += snprintf(tx_buf+offset, buflen-offset, "%s\r\n", pHdrV->value_string);
            }    
            else
            {
                if (pHdrV->header[0] != '\0')
                {
                    offset += snprintf(tx_buf+offset, buflen-offset, "%s=%s\r\n", pHdrV->header, pHdrV->value_string);
                }    
                else
                {
                    offset += snprintf(tx_buf+offset, buflen-offset, "%s\r\n", pHdrV->value_string);
                }    
            }

            pHdrV = (HDRV *)pps_lookup_next(&(tx_msg->sdp_ctx), pHdrV);
        }
        pps_lookup_end(&(tx_msg->sdp_ctx));
    }

    log_print(HT_LOG_DBG, "%s\r\n", tx_buf);

#ifdef RTSP_OVER_HTTP
    if (p_rua->rtsp_send)
    {
        slen = http_srv_cln_tx((HTTPCLN *) p_rua->rtsp_send, tx_buf, offset);
    }
    else
#endif  
#ifdef RTSP_OVER_WEBSOCKET
    if (p_rua->http_cln)
    {
        int extra = ws_encode_data((uint8 *)tx_buf, offset, WS_OPCODE_BINARY, 0);

        offset += extra;
        tx_buf -= extra;

        slen = http_srv_cln_tx((HTTPCLN *) p_rua->http_cln, tx_buf, offset);
    }
    else
#endif
#ifdef RTSPS
    if (p_rua->rtsps)
    {
        slen = rtsp_srv_ssl_tx(p_rua, tx_buf, offset);
    }
    else
#endif
    {
        slen = send(p_rua->fd, tx_buf, offset, 0);
    }
    
    if (slen <= 0)
    {
        log_print(HT_LOG_ERR, "%s, send message failed!!!\r\n", __FUNCTION__);
    }
}

/***********************************************************************/
void rsua_proxy_init()
{
    g_rtsp.rua_fl = pps_ctx_fl_init(MAX_NUM_RUA, sizeof(RSSUA), TRUE);
    g_rtsp.rua_ul = pps_ctx_ul_init(g_rtsp.rua_fl, TRUE);
}

void rsua_proxy_deinit()
{
    if (g_rtsp.rua_ul)
    {
        pps_ul_free(g_rtsp.rua_ul);
        g_rtsp.rua_ul = NULL;
    }

    if (g_rtsp.rua_fl)
    {
        pps_fl_free(g_rtsp.rua_fl);
        g_rtsp.rua_fl = NULL;
    }
}

RSSUA * rsua_get_idle_rua()
{
    RSSUA * p_rua = (RSSUA *)pps_fl_pop(g_rtsp.rua_fl);
    if (p_rua)
    {
        memset(p_rua, 0, sizeof(RSSUA));

        p_rua->used_flag = 1;
        
        log_print(HT_LOG_INFO, "%s, p_sua=%p, index[%u]\r\n", 
            __FUNCTION__, p_rua, rsua_get_index(p_rua));
    }    
    
    return p_rua;
}

void rsua_set_online_rua(RSSUA * p_rua)
{
    pps_ctx_ul_add(g_rtsp.rua_ul, p_rua);
}

void rsua_set_idle_rua(RSSUA * p_rua)
{
    log_print(HT_LOG_INFO, "%s, p_rua=%p, index[%u]\r\n", 
        __FUNCTION__, p_rua, rsua_get_index(p_rua));
    
    pps_ctx_ul_del(g_rtsp.rua_ul, p_rua);
    
    memset(p_rua, 0, sizeof(RSSUA));
    
    pps_fl_push_tail(g_rtsp.rua_fl, p_rua);
}

/* Get the current number of rtsp client connections */
int rsua_get_rua_nums()
{
    return pps_node_count(g_rtsp.rua_ul);
}

RSSUA * rsua_lookup_start()
{
    return (RSSUA *)pps_lookup_start(g_rtsp.rua_ul);
}

RSSUA * rsua_lookup_next(RSSUA * p_rua)
{
    return (RSSUA *)pps_lookup_next(g_rtsp.rua_ul, p_rua);
}

void rsua_lookup_stop()
{
    pps_lookup_end(g_rtsp.rua_ul);
}

uint32 rsua_get_index(RSSUA * p_rua)
{
    return pps_get_index(g_rtsp.rua_fl, p_rua);
}

RSSUA * rsua_get_by_index(uint32 index)
{
    return (RSSUA *)pps_get_node_by_index(g_rtsp.rua_fl, index);
}

#ifdef RTSP_OVER_HTTP
RSSUA * rsua_get_by_sessioncookie(char * sessioncookie)
{
    RSSUA * p_rua = (RSSUA *)pps_lookup_start(g_rtsp.rua_ul);
    while (p_rua)
    {
        if (strcmp(p_rua->sessioncookie, sessioncookie) == 0)
        {
            break;
        }
        
        p_rua = (RSSUA *)pps_lookup_next(g_rtsp.rua_ul, p_rua);
    }
    pps_lookup_end(g_rtsp.rua_ul);

    return p_rua;
}
#endif // end of RTSP_OVER_HTTP

SOCKET rsua_init_udp_connection(struct sockaddr * addr, uint16 port)
{
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * saddr;
    socklen_t saddrlen;

    if (addr->sa_family == AF_INET)
    {
        memcpy(&addr4, addr, sizeof(struct sockaddr_in));
        addr4.sin_port = htons(port);
        saddr = (struct sockaddr *) &addr4;
        saddrlen = sizeof(struct sockaddr_in);
    }
    else if (addr->sa_family == AF_INET6)
    {
        memcpy(&addr6, addr, sizeof(struct sockaddr_in6));
        addr6.sin6_port = htons(port);
        saddr = (struct sockaddr *) &addr6;
        saddrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        return -1;
    }

    SOCKET fd = socket(saddr->sa_family, SOCK_DGRAM, 0);
    if (fd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, socket SOCK_DGRAM error!\n", __FUNCTION__);
        return -1;
    }

    int len = 2 * 1024 * 1024;
    
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\r\n", __FUNCTION__);
    }
    
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\r\n", __FUNCTION__);
    }

    if (bind(fd, saddr, saddrlen) == -1)
    {
        log_print(HT_LOG_ERR, "%s, Bind udp socket fail, port = %d, error = %s\r\n", 
            __FUNCTION__, port, sys_os_get_socket_error());
        closesocket(fd);
        return -1;
    }

    return fd;
}

SOCKET rsua_init_mc_connection(struct sockaddr * addr, uint16 port, char * destination)
{
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * saddr;
    socklen_t saddrlen;

    if (addr->sa_family == AF_INET6)
    {
        memcpy(&addr6, addr, sizeof(struct sockaddr_in6));
        addr6.sin6_port = htons(port);
        saddr = (struct sockaddr *) &addr6;
        saddrlen = sizeof(struct sockaddr_in6);
    }
    else if (addr->sa_family == AF_INET)
    {
        memcpy(&addr4, addr, sizeof(struct sockaddr_in));
        addr4.sin_port = htons(port);
        saddr = (struct sockaddr *) &addr4;
        saddrlen = sizeof(struct sockaddr_in);
    }
    else
    {
        return -1;
    }

    SOCKET fd = socket(saddr->sa_family, SOCK_DGRAM, 0);
    if (fd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, socket SOCK_DGRAM error!\n", __FUNCTION__);
        return -1;
    }

    /* reuse socket addr */
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt))) 
    {  
        log_print(HT_LOG_WARN, "%s, setsockopt SO_REUSEADDR error!\r\n", __FUNCTION__);
    }

    int len = 2 * 1024 * 1024;
    
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\r\n", __FUNCTION__);
    }
    
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\r\n", __FUNCTION__);
    }

    if (addr->sa_family == AF_INET6)
    {
        int hops = 64;
        uint32 loopback = 1;
        struct ipv6_mreq mcast;

        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&hops, sizeof(hops));
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback));

        memset(&mcast, 0, sizeof(mcast));
        inet_pton(AF_INET6, destination, &mcast.ipv6mr_multiaddr);

        if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&mcast, sizeof(mcast)))
        {
            log_print(HT_LOG_ERR, "%s, setsockopt IPV6_JOIN_GROUP error!%s\r\n", 
                __FUNCTION__, sys_os_get_socket_error());
            closesocket(fd);
            return -1;
        }
        
#ifndef _WIN32
        addr6.sin6_addr = in6addr_any;
#endif
    }
    else
    {
        uint8 ttl = 64;
        uint8 loopback = 1;
        struct ip_mreq mcast;
        
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loopback, sizeof(loopback));

        memset(&mcast, 0, sizeof(mcast));
        mcast.imr_multiaddr.s_addr = inet_addr(destination);
        mcast.imr_interface.s_addr = addr4.sin_addr.s_addr;

        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mcast, sizeof(mcast)))
        {
            log_print(HT_LOG_ERR, "%s, setsockopt IP_ADD_MEMBERSHIP error!%s\r\n", 
                __FUNCTION__, sys_os_get_socket_error());
            closesocket(fd);
            return -1;
        }

#ifndef _WIN32
        addr4.sin_addr.s_addr = INADDR_ANY;
#endif
    }    

    if (bind(fd, saddr, saddrlen) == -1)
    {
        log_print(HT_LOG_ERR, "%s, Bind udp socket fail, port = %d, error = %s\r\n", 
            __FUNCTION__, port, sys_os_get_socket_error());
        closesocket(fd);
        return -1;
    }

    return fd;
}



