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
#include "word_analyse.h"
#include "rtsp_parse.h"
#include "http_cln.h"

/**************************************************************************/
const RREQMTV rtsp_req_mtvs[] =
{
    {RTSP_MT_DESCRIBE,      "DESCRIBE",     8},
    {RTSP_MT_ANNOUNCE,      "ANNOUNCE",     8},
    {RTSP_MT_OPTIONS,       "OPTIONS",      7},
    {RTSP_MT_PAUSE,         "PAUSE",        5},
    {RTSP_MT_PLAY,          "PLAY",         4},
    {RTSP_MT_RECORD,        "RECORD",       6},
    {RTSP_MT_REDIRECT,      "REDIRECT",     8},
    {RTSP_MT_SETUP,         "SETUP",        5},
    {RTSP_MT_SET_PARAMETER, "SET_PARAMETER",13},
    {RTSP_MT_GET_PARAMETER, "GET_PARAMETER",13},
    {RTSP_MT_TEARDOWN,      "TEARDOWN",     8}
};

/**************************************************************************/
BOOL rtsp_is_rtsp_msg(char * msg_buf)
{
    uint32 i;
    for (i=0; i < sizeof(rtsp_req_mtvs)/sizeof(RREQMTV); i++)
    {
        if (memcmp(msg_buf, rtsp_req_mtvs[i].msg_str, rtsp_req_mtvs[i].msg_len) == 0)
        {
            return TRUE;
        }
    }

    if (memcmp(msg_buf, "RTSP/1.0", strlen("RTSP/1.0")) == 0)
    {
        return TRUE;
    }
    
    return FALSE;
}

void rtsp_headl_parse(char * pline, int llen, HRTSP_MSG * p_msg)
{
    char word_buf[256];
    int  word_len;
    int  next_word_offset;
    BOOL have_next_word;

    have_next_word = get_line_word(pline, 0, llen, word_buf, sizeof(word_buf), &next_word_offset, WORD_TYPE_STRING);
    word_len = (int)strlen(word_buf);
    if (word_len > 0 && word_len < 31)
    {
        memcpy(p_msg->first_line.header, pline, word_len);
        p_msg->first_line.header[word_len] = '\0';

        while (pline[next_word_offset] == ' ')
        {
            next_word_offset++;
        }
        
        p_msg->first_line.value_string = pline+next_word_offset;
        p_msg->first_line.value_string[llen-next_word_offset] = '\0';

        if (strcasecmp(word_buf, "RTSP/1.0") == 0)
        {
            if (have_next_word)
            {
                word_len = sizeof(word_buf);
                have_next_word = get_line_word(pline, next_word_offset, llen, word_buf, sizeof(word_buf), &next_word_offset, WORD_TYPE_NUM);
                word_len = (int)strlen(word_buf);
                if (word_len > 0)
                {
                    p_msg->msg_type = 1;
                    p_msg->msg_sub_type = atoi(word_buf);
                }
            }
        }
        else
        {
            uint32 i;
            
            p_msg->msg_type = 0;
            
            for (i=0; i < sizeof(rtsp_req_mtvs)/sizeof(RREQMTV); i++)
            {
                if (strcasecmp(word_buf, (char *)(rtsp_req_mtvs[i].msg_str)) == 0)
                {
                    p_msg->msg_sub_type = rtsp_req_mtvs[i].msg_type;
                    break;
                }
            }
        }
    }
}

int rtsp_line_parse(char * p_buf, int max_len, char sep_char, PPSN_CTX * p_ctx)
{
    char word_buf[256];
    BOOL have_next_line = TRUE;
    int line_len = 0;
    int parse_len = 0;

    char * ptr = p_buf;

    do {
        if (get_sip_line(ptr, max_len, &line_len, &have_next_line) == FALSE)
        {
            return -1;
        }

        if (line_len <= 2)
        {
            return(parse_len + line_len);
        }

        int next_word_offset = 0;
        get_line_word(ptr, 0, line_len-2, word_buf, sizeof(word_buf), &next_word_offset, WORD_TYPE_STRING);
        
        char nchar = *(ptr + next_word_offset);
        if (nchar != sep_char) // SIP is ':',SDP is '='
        {
            return -1;
        }

        next_word_offset++;
        while (ptr[next_word_offset] == ' ')
        {
            next_word_offset++;
        }
        
        HDRV * pHdrV = hdrv_buf_get_idle();
        if (pHdrV == NULL)
        {
            log_print(HT_LOG_ERR, "%s, hdrv_buf_get_idle return NULL!!!\r\n", __FUNCTION__);
            return -1;
        }

        strncpy(pHdrV->header, word_buf, 31);
        pHdrV->value_string = ptr+next_word_offset;
        pps_ctx_ul_add(p_ctx, pHdrV);

        ptr += line_len;
        max_len -= line_len;
        parse_len += line_len;

        while (*ptr == '\r' || *ptr == '\n')
        {
            ptr++;
            parse_len++;
        }
    } while (have_next_line);

    return parse_len;
}

int rtsp_ctx_parse(HRTSP_MSG * p_msg)
{
    RTSPCTXT w_ctx_type;

    HDRV * pHdrV = (HDRV *)pps_lookup_start(&(p_msg->rtsp_ctx));
    while (pHdrV != NULL)
    {
        if (strcasecmp(pHdrV->header, "Content-Length") == 0)
        {
            p_msg->ctx_len = atol(pHdrV->value_string);
        }
        else if (strcasecmp(pHdrV->header, "Content-Type") == 0)
        {
            char type_word[64];
            int  next_tmp;
            get_line_word(pHdrV->value_string, 0, (int)strlen(pHdrV->value_string), type_word, sizeof(type_word), &next_tmp, WORD_TYPE_STRING);

            if (strcasecmp(type_word, "application/sdp") == 0)
            {
                w_ctx_type = RTSP_CTX_SDP;
            }    
            else
            {
                w_ctx_type = RTSP_CTX_NULL;
            }
            
            p_msg->ctx_type = w_ctx_type;
        }
        
        pHdrV = (HDRV *)pps_lookup_next(&(p_msg->rtsp_ctx), pHdrV);
    }
    pps_lookup_end(&(p_msg->rtsp_ctx));

    if (p_msg->ctx_type && p_msg->ctx_len)
    {
        return 1;
    }
    
    return 0;
}

int rtsp_msg_parse(char * msg_buf, int msg_buf_len, HRTSP_MSG * msg)
{
    int line_len = 0;
    BOOL have_next_line;    
    char * p_buf = msg_buf;

    msg->msg_type = (uint32) -1;    

    if (get_sip_line(p_buf, msg_buf_len, &line_len, &have_next_line) == FALSE)
    {
        return -1;
    }
    
    if (line_len > 0)
    {
        rtsp_headl_parse(p_buf, line_len-2, msg);
    }
    
    if (msg->msg_type == (uint32) -1)    
    {
        return -1;
    }
    
    p_buf += line_len;
    msg->rtsp_len = rtsp_line_parse(p_buf, msg_buf_len-line_len, ':', &(msg->rtsp_ctx));
    if (msg->rtsp_len <= 0)
    {
        return -1;
    }
    
    p_buf += msg->rtsp_len;
    
    if (rtsp_ctx_parse(msg) == 1 && msg->ctx_len > 0)
    {
        msg->sdp_len = rtsp_line_parse(p_buf, msg->ctx_len, '=', &(msg->sdp_ctx));
        if (msg->sdp_len < 0)
        {
            return -1;
        }    
    }

    return (line_len + msg->rtsp_len + msg->sdp_len);
}

int rtsp_msg_parse_part1(char * p_buf, int buf_len, HRTSP_MSG * msg)
{
    int line_len = 0;
    BOOL have_next_line;    

    msg->msg_type = (uint32) -1;    

    if (get_sip_line(p_buf, buf_len, &line_len, &have_next_line) == FALSE)
    {
        return -1;
    }
    
    if (line_len > 0)
    {
        rtsp_headl_parse(p_buf, line_len-2, msg);
    }
    
    if (msg->msg_type == (uint32) -1)
    {
        return -1;
    }
    
    p_buf += line_len;
    msg->rtsp_len = rtsp_line_parse(p_buf, buf_len-line_len, ':', &(msg->rtsp_ctx));
    if (msg->rtsp_len <= 0)
    {
        return -1;
    }
    
    rtsp_ctx_parse(msg);

    return (line_len + msg->rtsp_len);
}

int rtsp_msg_parse_part2(char * p_buf, int buf_len, HRTSP_MSG * msg)
{
    msg->sdp_len = rtsp_line_parse(p_buf, buf_len, '=', &(msg->sdp_ctx));
    if (msg->sdp_len < 0)
    {
        msg->sdp_len = rtsp_line_parse(p_buf, buf_len, ':', &(msg->sdp_ctx));
        if (msg->sdp_len < 0)
        {
            return -1;
        }
    }
    
    return msg->sdp_len;
}

HDRV * rtsp_find_headline(HRTSP_MSG * msg, const char * head)
{
    if (msg == NULL || head == NULL)
    {
        return NULL;
    }
    
    HDRV * line = (HDRV *)pps_lookup_start(&(msg->rtsp_ctx));
    while (line != NULL)
    {
        if (strcasecmp(line->header, head) == 0)
        {
            return line;
        }
        
        line = (HDRV *)pps_lookup_next(&(msg->rtsp_ctx), line);
    }
    pps_lookup_end(&(msg->rtsp_ctx));

    return NULL;
}

HDRV * rtsp_find_headline_next(HRTSP_MSG * msg, const char * head, HDRV * hrv)
{
    HDRV * line;
    
    if (msg == NULL || head == NULL)
    {
        return NULL;
    }
    
    line = (HDRV *)pps_lookup_start(&(msg->rtsp_ctx));
    while (line != NULL)
    {
        if (line == hrv)
        {
            line = (HDRV *)pps_lookup_next(&(msg->rtsp_ctx), line);
            break;
        }

        line = (HDRV *)pps_lookup_next(&(msg->rtsp_ctx), line);
    }
    
    while (line != NULL)
    {        
        if (strcasecmp(line->header, head) == 0)
        {
            pps_lookup_end(&(msg->rtsp_ctx));
            return line;
        }

        line = (HDRV *)pps_lookup_next(&(msg->rtsp_ctx), line);
    }
    pps_lookup_end(&(msg->rtsp_ctx));

    return NULL;
}

BOOL rtsp_get_headline_string(HRTSP_MSG * rx_msg, const char * head, char * p_value, int size)
{
    HDRV * rx_head = rtsp_find_headline(rx_msg, head);
    if (rx_head == NULL || p_value == NULL)
    {
        return FALSE;
    }
    
    if (rx_head->value_string == NULL)
    {
        return FALSE;
    }
    
    p_value[0] = '\0';

    int len = (int)strlen(rx_head->value_string);
    if (len >= size)
    {
        log_print(HT_LOG_ERR, "%s, %s, value_string(%s) len(%d) > size(%d)\r\n", 
            __FUNCTION__, head, rx_head->value_string, len, size);
        return FALSE;
    }

    strcpy(p_value, rx_head->value_string);
    
    return TRUE;
}

BOOL rtsp_get_headline_uri(HRTSP_MSG * rx_msg, char * p_uri, int size)
{
    char * p_ptr = rx_msg->first_line.value_string;
    if (p_ptr == NULL)
    {
        return FALSE;
    }
    
    char * p_end = p_ptr;
    while (*p_end != ' ')
    {
        p_end++;
    }
    
    int len = (int)(p_end - p_ptr);
    if (len >= size)
    {
        return FALSE;
    }
    
    memcpy(p_uri, p_ptr, len);
    p_uri[len] = '\0';
    
    return TRUE;
}

HDRV * rtsp_find_sdp_headline(HRTSP_MSG * msg, const char * head)
{
    if (msg == NULL || head == NULL)
    {
        return NULL;
    }
    
    HDRV * line = (HDRV *)pps_lookup_start(&(msg->sdp_ctx));
    while (line != NULL)
    {
        if (strcasecmp(line->header, head) == 0)
        {
            return line;
        }
        
        line = (HDRV *)pps_lookup_next(&(msg->sdp_ctx), line);
    }
    pps_lookup_end(&(msg->sdp_ctx));

    return NULL;
}

BOOL rtsp_msg_with_sdp(HRTSP_MSG * msg)
{
    if (msg == NULL)
    {
        return FALSE;
    }
    
    if (msg->sdp_ctx.node_num == 0)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL rtsp_get_msg_session(HRTSP_MSG * rx_msg, char *session_buf, int len)
{
    session_buf[0] = '\0';

    HDRV * rx_id = rtsp_find_headline(rx_msg, "Session");
    if (rx_id == NULL || len <= 0)
    {
        return FALSE;
    }
    
    int next_word_offset;

    get_line_word(rx_id->value_string, 0, (int)strlen(rx_id->value_string), session_buf, len, &next_word_offset, WORD_TYPE_STRING);

    return TRUE;
}

BOOL rtsp_match_msg_session(HRTSP_MSG * rx_msg, HRTSP_MSG * tx_msg)
{
    HDRV * rx_id = rtsp_find_headline(rx_msg, "Session");
    HDRV * tx_id = rtsp_find_headline(tx_msg, "Session");

    if (rx_id == NULL || tx_id == NULL)
    {
        return FALSE;
    }
    
    char rx_word_buf[256];
    char tx_word_buf[256];
    int  next_word_offset;

    get_line_word(rx_id->value_string, 0, (int)strlen(rx_id->value_string),
        rx_word_buf, sizeof(rx_word_buf), &next_word_offset, WORD_TYPE_STRING);
    get_line_word(tx_id->value_string, 0, (int)strlen(tx_id->value_string),
        tx_word_buf, sizeof(tx_word_buf), &next_word_offset, WORD_TYPE_STRING);

    if (strcmp(rx_word_buf, tx_word_buf) != 0)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL rtsp_match_msg_cseq(HRTSP_MSG * rx_msg, HRTSP_MSG * tx_msg)
{
    HDRV * rx_cseq = rtsp_find_headline(rx_msg, "CSeq");
    HDRV * tx_cseq = rtsp_find_headline(tx_msg, "CSeq");

    if (rx_cseq == NULL || tx_cseq == NULL)
    {
        return FALSE;
    }
    
    char rx_word_buf[256];
    char tx_word_buf[256];
    int  next_offset;

    get_line_word(rx_cseq->value_string, 0, (int)strlen(rx_cseq->value_string),
        rx_word_buf, sizeof(rx_word_buf), &next_offset, WORD_TYPE_NUM);
    get_line_word(tx_cseq->value_string, 0, (int)strlen(tx_cseq->value_string),
        tx_word_buf, sizeof(tx_word_buf), &next_offset, WORD_TYPE_NUM);

    if (strcmp(rx_word_buf, tx_word_buf) != 0)
    {
        return FALSE;
    }
    
    get_line_word(rx_cseq->value_string, next_offset, (int)strlen(rx_cseq->value_string),
        rx_word_buf, sizeof(rx_word_buf), &next_offset, WORD_TYPE_STRING);
    get_line_word(tx_cseq->value_string, next_offset, (int)strlen(tx_cseq->value_string),
        tx_word_buf, sizeof(tx_word_buf), &next_offset, WORD_TYPE_STRING);

    if (strcasecmp(rx_word_buf, tx_word_buf) != 0)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL rtsp_get_msg_cseq(HRTSP_MSG * rx_msg, char *cseq_buf, int len)
{
    HDRV * rx_cseq = rtsp_find_headline(rx_msg, "CSeq");
    if ((rx_cseq == NULL) || len <= 0)
    {
        return FALSE;
    }
    
    int    next_offset;

    get_line_word(rx_cseq->value_string, 0, (int)strlen(rx_cseq->value_string), cseq_buf, len, &next_offset, WORD_TYPE_NUM);

    return TRUE;
}

BOOL rtsp_get_user_agent_info(HRTSP_MSG * rx_msg, char * agent_buf, int max_len)
{
    if (agent_buf == NULL || max_len <= 0)
    {
        return FALSE;
    }
    
    agent_buf[0] = '\0';

    HDRV * rx_line = rtsp_find_headline(rx_msg, "User-Agent");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    strncpy(agent_buf, rx_line->value_string, max_len);
    
    return TRUE;
}

BOOL rtsp_get_session_info(HRTSP_MSG * rx_msg, char * session_buf, int max_len, int * timeout)
{
    if (session_buf == NULL || max_len <= 0)
    {
        return FALSE;
    }

    HDRV * rx_line = rtsp_find_headline(rx_msg, "Session");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    session_buf[0] = '\0';
    
    if (rx_line->value_string)
    {        
        char * p = strchr(rx_line->value_string, ';');
        if (!p)
        {
            strncpy(session_buf, rx_line->value_string, max_len);
        }
        else
        {
            *p = '\0';
            strncpy(session_buf, rx_line->value_string, max_len);
            *p = ';';
        }

        if (timeout)
        {
            char value_string[256] = {'\0'};
            strncpy(value_string, rx_line->value_string, sizeof(value_string)-1);
            lowercase(value_string);
            
            p = strstr(value_string, "timeout=");
            if (p)
            {
                p += strlen("timeout=");
                *timeout = atoi(p);

                if (*timeout <= 0)
                {
                    *timeout = 60;
                }
                else if (*timeout < 10)
                {
                    *timeout = 10;
                }
            }
        }
    }    
    
    return TRUE;
}

void rtsp_get_rtptime(char * str, char * ctl, uint32 * p_ts)
{
    int ctl_len;
    char * p = strchr(ctl, '?');
    if (p)
    {
        size_t len = p - ctl;
        char ctrl[128];

        if (len >= sizeof(ctrl))
        {
            len = sizeof(ctrl) - 1;
        }
        
        strncpy(ctrl, ctl, len);
        ctrl[len] = '\0';

        p = strstr(str, ctrl);
        ctl_len = strlen(ctrl);
    }
    else
    {
        p = strstr(str, ctl);
        ctl_len = strlen(ctl);
    }
    
    if (p)
    {
        p += ctl_len;
        
        size_t i = 0, j = 0;
        char * p_buff = NULL;
        char * p_semicolon = strchr(p, ',');
        char * p_rtptime = strstr(p, "rtptime=");
        char rtptime[32] = {'\0'};
        
        if (p_rtptime)
        {
            if (p_semicolon)
            {
                if (p_rtptime < p_semicolon)
                {
                    p_buff = p_rtptime + strlen("rtptime=");
                }
            }
            else
            {
                p_buff = p_rtptime + strlen("rtptime=");
            }

            while (p_buff && p_buff[i] != '\0')
            {
                if (p_buff[i] == ',' || p_buff[i] == ';')
                {
                    break;
                }
                else if (j < sizeof(rtptime)-1)
                {
                    rtptime[j++] = p_buff[i];
                }

                i++;
            }
            
            *p_ts = (uint32)strtoul(rtptime, NULL, 10);
        }
    }
}

BOOL rtsp_get_rtp_info(HRTSP_MSG * rx_msg, char * v_ctl, uint32 * p_v_ts, char * a_ctl, uint32 * p_a_ts)
{
    HDRV * rx_line = rtsp_find_headline(rx_msg, "RTP-Info");
    if (rx_line == NULL)
    {
        return FALSE;
    }

    if (NULL == rx_line->value_string)
    {
        return FALSE;
    }

    // set default value -1
    *p_v_ts = -1;
    *p_a_ts = -1;
    
    if (v_ctl && v_ctl[0] != '\0')
    {
        rtsp_get_rtptime(rx_line->value_string, v_ctl, p_v_ts);
    }

    if (a_ctl && a_ctl[0] != '\0')
    {
        rtsp_get_rtptime(rx_line->value_string, a_ctl, p_a_ts);
    }
    
    return TRUE;
}

BOOL rtsp_get_tcp_transport_info(HRTSP_MSG * rx_msg, uint16 * interleaved)
{
    BOOL ret = FALSE;
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Transport");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    if (rx_line->value_string)
    {    
        char buff[32] = {'\0'};
        if (get_name_value_pair(rx_line->value_string, (int)strlen(rx_line->value_string), "interleaved", buff, sizeof(buff)-1))
        {
            if (interleaved)
            {
                *interleaved = atoi(buff);                
            }

            ret = TRUE;
        }
    }
    
    return ret;
}

BOOL rtsp_get_udp_transport_info(HRTSP_MSG * rx_msg, uint16 * client_port, uint16 * server_port)
{
    BOOL ret = FALSE;
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Transport");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    if (rx_line->value_string)
    {    
        char buff[32] = {'\0'};
        if (get_name_value_pair(rx_line->value_string, (int)strlen(rx_line->value_string), "client_port", buff, sizeof(buff)-1))
        {
            if (client_port)
            {
                *client_port = atoi(buff);                
            }

            ret = TRUE;
        }

        if (get_name_value_pair(rx_line->value_string, (int)strlen(rx_line->value_string), "server_port", buff, sizeof(buff)-1))
        {
            if (server_port)
            {
                *server_port = atoi(buff);                
            }

            ret = TRUE;
        }
    }
    
    return ret;
}

BOOL rtsp_get_mc_transport_info(HRTSP_MSG * rx_msg, char * destination, uint16 * port)
{
    BOOL ret = FALSE;
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Transport");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    if (rx_line->value_string)
    {    
        char buff[32] = {'\0'};
        if (get_name_value_pair(rx_line->value_string, (int)strlen(rx_line->value_string), "destination", buff, sizeof(buff)-1))
        {
            if (destination)
            {
                strcpy(destination, buff);
            }

            ret = TRUE;
        }

        if (get_name_value_pair(rx_line->value_string, (int)strlen(rx_line->value_string), "port", buff, sizeof(buff)-1))
        {
            if (port)
            {
                *port = atoi(buff);                
            }

            ret = TRUE;
        }
    }
    
    return ret;
}

BOOL rtsp_is_line_exist(HRTSP_MSG * rx_msg, const char * head, const char * value)
{
    char buf[256];
    char lovalue[256] = {'\0'};
    
    if (!rtsp_get_headline_string(rx_msg, head, buf, sizeof(buf)))
    {
        return FALSE;
    }

    strncpy(lovalue, value, sizeof(lovalue)-1);

    lowercase(buf);
    lowercase(lovalue);
    
    if (!strstr(buf, lovalue))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_get_cbase_info(HRTSP_MSG * rx_msg, char * cbase_buf, int max_len)
{
    if (cbase_buf == NULL || max_len <= 0)
    {
        return FALSE;
    }
    
    cbase_buf[0] = '\0';

    HDRV * rx_line = rtsp_find_headline(rx_msg, "Content-Base");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    strncpy(cbase_buf, rx_line->value_string, max_len);
    
    return TRUE;
}

void rtsp_add_tx_msg_line(HRTSP_MSG * tx_msg, const char * msg_hdr, const char * msg_fmt, ...)
{
    int slen;
    va_list argptr;    

    if (tx_msg == NULL || tx_msg->msg_buf == NULL)
    {
        return;
    }
    
    HDRV *pHdrV = hdrv_buf_get_idle();
    if (pHdrV == NULL)
    {
        log_print(HT_LOG_ERR, "%s, hdrv_buf_get_idle return NULL!!!\r\n", __FUNCTION__);
        return;
    }

    pHdrV->value_string = tx_msg->msg_buf + tx_msg->buf_offset;

    strncpy(pHdrV->header, msg_hdr, 31);

    va_start(argptr, msg_fmt);
    slen = vsnprintf(pHdrV->value_string, 1600-tx_msg->buf_offset, msg_fmt, argptr);
    va_end(argptr);

    if (slen < 0)
    {
        log_print(HT_LOG_ERR, "%s, vsnprintf return %d !!!\r\n", __FUNCTION__, slen);
        hdrv_buf_free(pHdrV);
        return;
    }

    pHdrV->value_string[slen] = '\0';
    tx_msg->buf_offset += slen + 1;

    pps_ctx_ul_add(&(tx_msg->rtsp_ctx), pHdrV);
}

void rtsp_add_tx_msg_sdp_line(HRTSP_MSG * tx_msg, const char * msg_hdr, const char * msg_fmt, ...)
{
    int slen;
    va_list argptr;    

    if (tx_msg == NULL || tx_msg->msg_buf == NULL)
    {
        return;
    }
    
    HDRV *pHdrV = hdrv_buf_get_idle();
    if (pHdrV == NULL)
    {
        log_print(HT_LOG_ERR, "%s, hdrv_buf_get_idle return NULL!!!\r\n", __FUNCTION__);
        return;
    }

    pHdrV->value_string = tx_msg->msg_buf + tx_msg->buf_offset;

    strncpy(pHdrV->header, msg_hdr, 31);

    va_start(argptr, msg_fmt);
    slen = vsnprintf(pHdrV->value_string, 1600-tx_msg->buf_offset, msg_fmt, argptr);
    va_end(argptr);

    if (slen < 0)
    {
        log_print(HT_LOG_ERR, "%s, vsnprintf return %d !!!\r\n", __FUNCTION__, slen);
        hdrv_buf_free(pHdrV);
        return;
    }

    pHdrV->value_string[slen] = '\0';
    tx_msg->buf_offset += slen + 1;

    pps_ctx_ul_add(&(tx_msg->sdp_ctx), pHdrV);
}

void rtsp_add_tx_msg_fline(HRTSP_MSG * tx_msg, const char * msg_hdr, const char * msg_fmt, ...)
{
    int slen;
    va_list argptr;    

    if (tx_msg == NULL || tx_msg->msg_buf == NULL)
    {
        return;
    }
    
    strncpy(tx_msg->first_line.header, msg_hdr, sizeof(tx_msg->first_line.header)-1);
    tx_msg->first_line.value_string = tx_msg->msg_buf + tx_msg->buf_offset;

    va_start(argptr, msg_fmt);
    slen = vsnprintf(tx_msg->first_line.value_string, 1600-tx_msg->buf_offset, msg_fmt, argptr);
    va_end(argptr);

    if (slen < 0)
    {
        log_print(HT_LOG_ERR, "%s, vsnprintf return %d !!!\r\n", __FUNCTION__, slen);
        return;
    }

    tx_msg->first_line.value_string[slen] = '\0';
    
    tx_msg->buf_offset += slen + 1;
}

void rtsp_copy_msg_line(HRTSP_MSG * src_msg, HRTSP_MSG * dst_msg, char * msg_hdr)
{
    HDRV * src_line = rtsp_find_headline(src_msg, msg_hdr);
    if (src_line == NULL)
    {
        return;
    }
    
    HDRV * dst_line = hdrv_buf_get_idle();
    if (dst_line == NULL)
    {
        return;
    }
    
    strcpy(dst_line->header, src_line->header);

    dst_line->value_string = dst_msg->msg_buf + dst_msg->buf_offset;
    if (dst_line->value_string == NULL)
    {
        hdrv_buf_free(dst_line);
        return;
    }

    strcpy(dst_line->value_string, src_line->value_string);
    dst_msg->buf_offset += (int)strlen(src_line->value_string) + 1;

    pps_ctx_ul_add(&(dst_msg->rtsp_ctx), dst_line);
}

void rtsp_free_msg(HRTSP_MSG * msg)
{
    if (msg == NULL)
    {
        return;
    }
    
    rtsp_free_msg_content(msg);
    rtsp_free_msg_buf(msg);
}

void rtsp_free_msg_content(HRTSP_MSG * msg)
{
    if (msg == NULL)
    {
        return;
    }
    
    hdrv_ctx_free(&(msg->rtsp_ctx));
    hdrv_ctx_free(&(msg->sdp_ctx));

    net_buf_free(msg->msg_buf);
}

BOOL rtsp_get_remote_media_ip(HRTSP_MSG * rx_msg, uint32 * media_ip)
{
    HDRV * pHdr = rtsp_find_sdp_headline(rx_msg, "c");
    if ((pHdr != NULL) && (pHdr->value_string != NULL) && (strlen(pHdr->value_string) > 0))
    {
        char tmp_buf[128];
        int next_offset;
        
        get_line_word(pHdr->value_string, 0, (int)strlen(pHdr->value_string),
                    tmp_buf, sizeof(tmp_buf), &next_offset, WORD_TYPE_STRING);
        if (strcasecmp(tmp_buf, "IN") != 0)
        {
            return FALSE;
        }
        
        get_line_word(pHdr->value_string, next_offset, (int)strlen(pHdr->value_string),
                    tmp_buf, sizeof(tmp_buf), &next_offset, WORD_TYPE_STRING);
        if (strcasecmp(tmp_buf, "IP4") != 0)
        {
            return FALSE;
        }
        
        get_line_word(pHdr->value_string, next_offset, (int)strlen(pHdr->value_string),
                    tmp_buf, sizeof(tmp_buf), &next_offset, WORD_TYPE_STRING);

        log_print(HT_LOG_DBG, "%s, media_ip=%s\r\n", __FUNCTION__, tmp_buf);

        if (is_ip_address(tmp_buf))
        {
            *media_ip = inet_addr(tmp_buf);
            return TRUE;
        }
    }

    return FALSE;
}

HDRV * rtsp_find_mdesc_point(HRTSP_MSG * rx_msg, HDRV * pStartHdr, const char * cap_name, int * next_offset, const char * attr)
{
    char media_type[16];
    HDRV * pHdr = pStartHdr;
    HDRV * pHdr1 = NULL;

    for (; pHdr != NULL; pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx),pHdr))
    {
        if (strcasecmp(pHdr->header, "m") != 0)
        {
            continue;
        }
        
        get_line_word(pHdr->value_string, 0, (int)strlen(pHdr->value_string), media_type, sizeof(media_type), next_offset, WORD_TYPE_STRING);
        
        if (strcasecmp(media_type, cap_name) != 0)
        {
            continue;
        }
        
        if (NULL == attr)
        {
            return pHdr;
        }
        
        pHdr1 = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
        
        for (; pHdr1 != NULL; pHdr1 = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr1))
        {
            if (strcasecmp(pHdr1->header, "m") == 0)
            {
                break;
            }
            
            if (pHdr1->value_string && (pHdr1->header[0] == 'a') && (memcmp(pHdr1->value_string, attr, strlen(attr)) == 0))
            {
                return pHdr;
            }                
        }
    }

    return NULL;
}

BOOL rtsp_get_remote_cap(HRTSP_MSG * rx_msg, const char * cap_name, int * cap_count, uint8 * cap_array, uint16 * rtp_port, const char * attr)
{
    int next_offset = 0;
    char media_port[16] = {'\0'}, tmp_buf[64] = {'\0'};

    *cap_count = 0;

    HDRV * pHdr = (HDRV *)pps_lookup_start(&(rx_msg->sdp_ctx));

    pHdr = rtsp_find_mdesc_point(rx_msg, pHdr, cap_name, &next_offset, attr);
    if (pHdr == NULL)
    {
        pps_lookup_end(&(rx_msg->sdp_ctx));
        return FALSE;
    }

    get_line_word(pHdr->value_string, next_offset, (int)strlen(pHdr->value_string),
                media_port, sizeof(media_port), &next_offset, WORD_TYPE_NUM);
    
    get_line_word(pHdr->value_string, next_offset, (int)strlen(pHdr->value_string),
                tmp_buf, sizeof(tmp_buf), &next_offset, WORD_TYPE_STRING);

    if (strncasecmp(tmp_buf, "RTP/", 4) != 0)
    {
        log_print(HT_LOG_ERR, "%s, unsupport type %s\r\n", __FUNCTION__, tmp_buf);
        pps_lookup_end(&(rx_msg->sdp_ctx));
        return FALSE;
    }

    for (uint32 i = 4; i < strlen(tmp_buf); i++)
    {
        if (tmp_buf[i] == 'A' || tmp_buf[i] == 'V' || tmp_buf[i] == 'P' || tmp_buf[i] == 'F'
#if defined(SRTP) || defined(RTSP_SRTP)
            || tmp_buf[i] == 'S'
#endif
            )
        {
            continue;
        }
        else
        {
            log_print(HT_LOG_ERR, "%s, unsupport type %s\r\n", __FUNCTION__, tmp_buf);
            pps_lookup_end(&(rx_msg->sdp_ctx));
            return FALSE;
        }
    }
    
    int count = 0;
    BOOL cap_next_flag = TRUE;
    
    do {
        cap_next_flag = get_line_word(pHdr->value_string, next_offset, (int)strlen(pHdr->value_string),
                    tmp_buf, sizeof(tmp_buf), &next_offset, WORD_TYPE_NUM);
        if (tmp_buf[0] != '\0')
        {
            if (count >= MAX_AVN)
            {
                pps_lookup_end(&(rx_msg->sdp_ctx));
                return FALSE;
            }

            cap_array[count++] = (uint8)atol(tmp_buf);
            *cap_count = count;
        }
    } while (cap_next_flag);

    if (count > 0)
    {
        if (rtp_port)
        {
            *rtp_port = (uint16)atol(media_port);
        }
        
        pps_lookup_end(&(rx_msg->sdp_ctx));
        return TRUE;
    }

    pps_lookup_end(&(rx_msg->sdp_ctx));

    return FALSE;
}

BOOL rtsp_get_digest_info(HRTSP_MSG * rx_msg, HD_AUTH_INFO * p_auth)
{
    int len;
    int next_offset;
    char word_buf[128];
    HDRV * chap_id = NULL;
    char * p;

    p_auth->auth_response[0] = '\0';

RETRY:

    if (chap_id)
    {
        chap_id = rtsp_find_headline_next(rx_msg, "WWW-Authenticate", chap_id);
    }
    else
    {
        chap_id = rtsp_find_headline(rx_msg, "WWW-Authenticate");
    }
    
    if (chap_id == NULL)
    {
        return FALSE;
    }

    get_line_word(chap_id->value_string, 0, (int)strlen(chap_id->value_string),
        word_buf, sizeof(word_buf), &next_offset, WORD_TYPE_STRING);
    if (strcasecmp(word_buf, "digest") != 0)
    {
        goto RETRY;
    }

    p = chap_id->value_string + next_offset;
    len = (int)strlen(chap_id->value_string) - next_offset;

    if (!http_get_digest_params(p, len, p_auth))
    {
        goto RETRY;
    }

    if (p_auth->auth_algorithm[0] != '\0' && 
        strncasecmp(p_auth->auth_algorithm, "MD5", 3) != 0 &&  
        strncasecmp(p_auth->auth_algorithm, "SHA-256", 7) != 0)
    {
        goto RETRY;
    }
    
    return TRUE;
}

BOOL rtsp_get_auth_digest_info(HRTSP_MSG * rx_msg, HD_AUTH_INFO * p_auth)
{
    int len;
    int next_offset;
    char word_buf[128];
    char * p;
    
    HDRV * res_line = rtsp_find_headline(rx_msg, "Authorization");
    if (res_line == NULL)
    {
        return FALSE;
    }
    
    get_line_word(res_line->value_string, 0, (int)strlen(res_line->value_string),    
        word_buf, sizeof(word_buf), &next_offset, WORD_TYPE_STRING);
        
    if (strcasecmp(word_buf, "digest") != 0)
    {
        return FALSE;
    }

    p = res_line->value_string + next_offset;
    len = (int)strlen(res_line->value_string) - next_offset;
    
    if (!get_name_value_pair(p, len, "username", p_auth->auth_name, sizeof(p_auth->auth_name)))
    {    
        return FALSE;
    }
    
    if (!get_name_value_pair(p, len, "realm", p_auth->auth_realm, sizeof(p_auth->auth_realm)))
    {    
        return FALSE;
    }
    
    if (!get_name_value_pair(p, len, "nonce", p_auth->auth_nonce, sizeof(p_auth->auth_nonce)))
    {    
        return FALSE;
    }
    
    if (!get_name_value_pair(p, len, "uri", p_auth->auth_uri, sizeof(p_auth->auth_uri)))
    {    
        return FALSE;
    }
    
    if (!get_name_value_pair(p, len, "response", p_auth->auth_response, sizeof(p_auth->auth_response)))
    {    
        return FALSE;
    }

    if (!get_name_value_pair(p, len, "algorithm", p_auth->auth_algorithm, sizeof(p_auth->auth_algorithm)))
    {
        p_auth->auth_algorithm[0] = '\0';
    }
    
    if (get_name_value_pair(p, len, "qop", p_auth->auth_qop, sizeof(p_auth->auth_qop)))
    {
        char * stop_string;
        
        if (!get_name_value_pair(p, len, "cnonce", p_auth->auth_cnonce, sizeof(p_auth->auth_cnonce)))
        {    
            p_auth->auth_cnonce[0] = '\0';
        }
        
        if (!get_name_value_pair(p, len, "nc", p_auth->auth_ncstr, sizeof(p_auth->auth_ncstr)))
        {    
            p_auth->auth_ncstr[0] = '\0';
        }
        
        p_auth->auth_nc = strtol(p_auth->auth_ncstr, &stop_string, 16);
        
        if (strlen(stop_string) > 0)
        {
            return FALSE;
        }
    }
    else
    {
        p_auth->auth_qop[0] = '\0';
        p_auth->auth_cnonce[0] = '\0';
        p_auth->auth_ncstr[0] = '\0';
        p_auth->auth_nc = 0;
    }

    return TRUE;
}

BOOL rtsp_get_remote_cap_desc(HRTSP_MSG * rx_msg, const char * cap_name, char cap_desc[][MAX_AVDESCLEN], const char * attr)
{
    int next_offset = 0;
    int index = 0;

    HDRV * pHdr = (HDRV *)pps_lookup_start(&(rx_msg->sdp_ctx));

    pHdr = rtsp_find_mdesc_point(rx_msg, pHdr, cap_name, &next_offset, attr);
    if (pHdr == NULL)
    {
        pps_lookup_end(&(rx_msg->sdp_ctx));
        return FALSE;
    }

    for (index=0; index<MAX_AVN; index++)
    {
        cap_desc[index][0] = '\0';
    }
    
    index = 0;
    pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    while (pHdr != NULL)
    {
        if (strcasecmp(pHdr->header, "m") == 0)
        {
            break;
        }
        
        if (index >= MAX_AVN)
        {
            break;
        }
        
        snprintf(cap_desc[index], MAX_AVDESCLEN, "%s=%s", pHdr->header, pHdr->value_string);

        index++;

        pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    }

    pps_lookup_end(&(rx_msg->sdp_ctx));

    return (index != 0);
}

BOOL rtsp_find_sdp_control(HRTSP_MSG * rx_msg, char * ctl_buf, const char * tname, int max_len, const char *attr)
{
    if (rx_msg == NULL || ctl_buf == NULL)
    {
        return FALSE;
    }
    
    int next_offset = 0;
    int mlen = (int)strlen("control:");
    
    ctl_buf[0] = '\0';

    HDRV * pHdr = (HDRV *)pps_lookup_start(&(rx_msg->sdp_ctx));

    pHdr = rtsp_find_mdesc_point(rx_msg, pHdr, tname, &next_offset, attr);
    if (pHdr == NULL)
    {
        pps_lookup_end(&(rx_msg->sdp_ctx));
        return FALSE;
    }

    pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    while (pHdr != NULL)
    {
        if (strcasecmp(pHdr->header, "m") == 0)
        {
            break;
        }
        
        if (pHdr->value_string && pHdr->header[0] == 'a' && memcmp(pHdr->value_string, "control:", mlen) == 0)
        {
            int rlen = (int)strlen(pHdr->value_string) - mlen;
            if (rlen > max_len)
            {
                rlen = max_len;
            }
            
            int offset = 0;
            
            while (pHdr->value_string[offset+mlen] == ' ')
            {
                offset++;
            }
            
            strcpy(ctl_buf, pHdr->value_string+offset+mlen);

            pps_lookup_end(&(rx_msg->sdp_ctx));
            
            return TRUE;
        }

        pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    }

    pps_lookup_end(&(rx_msg->sdp_ctx));

    return FALSE;
}

// a=range:npt=0-20.735
BOOL rtsp_get_sdp_range(HRTSP_MSG * rx_msg, int * p_start, int * p_end)
{
    if (rx_msg == NULL)
    {
        return FALSE;
    }

    *p_start = 0;
    *p_end = 0;

    int mlen = (int)strlen("range:");
    
    HDRV * pHdr = (HDRV *)pps_lookup_start(&(rx_msg->sdp_ctx));
    pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    while (pHdr != NULL)
    {
        if (strcasecmp(pHdr->header, "m") == 0)
        {
            break;
        }

        if (pHdr->value_string && pHdr->header[0] == 'a' && memcmp(pHdr->value_string, "range:", mlen) == 0)
        {
            int i = 0;
            int offset = 0;
            int rlen = (int)strlen(pHdr->value_string) - mlen;
            char start_buf[32] = {'\0'}, end_buf[32] = {'\0'};
            char * p_buff = start_buf;

            // skip space
            while (pHdr->value_string[offset+mlen] == ' ')
            {
                offset++;
            }

            if (rlen < offset + 4)
            {
                pps_lookup_end(&(rx_msg->sdp_ctx));
                return FALSE;
            }

            if (pHdr->value_string[offset+mlen] != 'n' && 
                pHdr->value_string[offset+mlen+1] == 'p' && 
                pHdr->value_string[offset+mlen+2] == 't')
            {
                pps_lookup_end(&(rx_msg->sdp_ctx));
                return FALSE;
            }

            offset += 3; // skip "npt"

            // skip space
            while (pHdr->value_string[offset+mlen] == ' ')
            {
                offset++;
            }

            if (pHdr->value_string[offset+mlen] != '=')
            {
                pps_lookup_end(&(rx_msg->sdp_ctx));
                return FALSE;
            }

            offset++; // skip '='
            
            while (pHdr->value_string[offset+mlen] != '\0')
            {
                if (pHdr->value_string[offset+mlen] == '-')
                {
                    i = 0;
                    p_buff = end_buf;
                }
                else if (pHdr->value_string[offset+mlen] != ' ')
                {
                    p_buff[i++] = pHdr->value_string[offset+mlen];

                    if (i >= 32)
                    {
                        pps_lookup_end(&(rx_msg->sdp_ctx));
                        return FALSE;
                    }
                }

                offset++;
            }

            *p_start = (int)(atof(start_buf) * 1000); // convert to millisecond 
            *p_end = (int)(atof(end_buf) * 1000);

            break;
        }

        pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    }
    pps_lookup_end(&(rx_msg->sdp_ctx));

    return TRUE;
}

BOOL rtsp_is_support_mcast(HRTSP_MSG * rx_msg)
{
    BOOL ret = FALSE;

    HDRV * pHdr = (HDRV *)pps_lookup_start(&(rx_msg->sdp_ctx));
    while (pHdr != NULL)
    {
        if (strcasecmp(pHdr->header, "m") == 0)
        {
            break;
        }
        
        if (strcasecmp(pHdr->header, "a") == 0)
        {
            if (strstr(pHdr->value_string, "type:broadcast"))
            {
                ret = TRUE;
                break;
            }
        }
        
        pHdr = (HDRV *)pps_lookup_next(&(rx_msg->sdp_ctx), pHdr);
    }
    pps_lookup_end(&(rx_msg->sdp_ctx));
    
    return ret;
}

BOOL rtsp_get_scale_info(HRTSP_MSG * rx_msg, int * p_scale)
{
    if (rx_msg == NULL || p_scale == NULL)
    {
        return FALSE;
    }
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Scale");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    char * ptr = rx_line->value_string;
    
    while (*ptr == ' ' || *ptr == '\t')
    {
        ptr++;
    }
    
    double scale = strtod(ptr, NULL);
    if (scale == 0)
    {
        return FALSE;
    }
    
    *p_scale = (int)(scale * 100);

    return TRUE;
}

BOOL rtsp_get_rate_control(HRTSP_MSG * rx_msg, int * p_ratectrl)
{
    if (rx_msg == NULL || p_ratectrl == NULL)
    {
        return FALSE;
    }
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Rate-Control");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    char * ptr = rx_line->value_string;
    
    while (*ptr == ' ' || *ptr == '\t')
    {
        ptr++;
    }

    int ratectrl = 1;
    
    if (strcasecmp(ptr, "no") == 0)
    {
        ratectrl = 0;
    }
    
    *p_ratectrl = ratectrl;

    return TRUE;
}

BOOL rtsp_get_immediate(HRTSP_MSG * rx_msg, int * p_imme)
{
    if (rx_msg == NULL || p_imme == NULL)
    {
        return FALSE;
    }
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Immediate");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    char * ptr = rx_line->value_string;
    
    while (*ptr == ' ' || *ptr == '\t')
    {
        ptr++;
    }

    int imme = 0;
    
    if (strcasecmp(ptr, "yes") == 0)
    {
        imme = 1;
    }
    
    *p_imme = imme;

    return TRUE;
}

BOOL rtsp_get_frame_info(HRTSP_MSG * rx_msg, int * p_frame, int * p_interval)
{
    if (rx_msg == NULL || p_frame == NULL)
    {
        return FALSE;
    }
    
    HDRV * rx_line = rtsp_find_headline(rx_msg, "Frames");
    if (rx_line == NULL)
    {
        return FALSE;
    }
    
    char * ptr = rx_line->value_string;
    
    while (*ptr == ' ' || *ptr == '\t')
    {
        ptr++;
    }

    int frame = 0; // all frames
    
    if (strncasecmp(ptr, "intra", 5) == 0)
    {
        frame = 2; // I-frame

        ptr += 5;

        while (*ptr == ' ' || *ptr == '\t')
        {
            ptr++;
        }

        if (*ptr == '/')
        {
            ptr++;

            if (p_interval)
            {
                *p_interval = atoi(ptr);
            }
        }        
    }
    else if (strcasecmp(ptr, "predicted") == 0)
    { 
        frame = 1; // I-frame and P-frame
    }
    
    *p_frame = frame;

    return TRUE;
}


/*****************************************************************/
static PPSN_CTX * rtsp_msg_buf_fl = NULL;

/*****************************************************************/
BOOL rtsp_msg_buf_init(int num)
{
    if (rtsp_msg_buf_fl)
    {
        return TRUE;
    }
    
    rtsp_msg_buf_fl = pps_ctx_fl_init(num, sizeof(HRTSP_MSG), TRUE);
    if (rtsp_msg_buf_fl == NULL)
    {
        return FALSE;
    }
    
    return TRUE;
}

void rtsp_msg_buf_deinit()
{
    if (rtsp_msg_buf_fl)
    {
        pps_fl_free(rtsp_msg_buf_fl);
        rtsp_msg_buf_fl = NULL;
    }
}

HRTSP_MSG * rtsp_get_msg_buf()
{
    HRTSP_MSG * tx_msg = (HRTSP_MSG *)pps_fl_pop(rtsp_msg_buf_fl);
    if (tx_msg == NULL)
    {
        return NULL;
    }
    
    memset(tx_msg, 0, sizeof(HRTSP_MSG));
    
    tx_msg->msg_buf = net_buf_get_idle();
    if (tx_msg->msg_buf == NULL)
    {
        rtsp_free_msg_buf(tx_msg);
        return NULL;
    }

    rtsp_msg_ctx_init(tx_msg);

    return tx_msg;
}

void rtsp_msg_ctx_init(HRTSP_MSG * msg)
{
    pps_ctx_ul_init_nm(hdrv_buf_fl, &(msg->rtsp_ctx));
    pps_ctx_ul_init_nm(hdrv_buf_fl, &(msg->sdp_ctx));
}

void rtsp_free_msg_buf(HRTSP_MSG * msg)
{
    pps_fl_push(rtsp_msg_buf_fl, msg);
}

uint32 rtsp_idle_msg_buf_num()
{
    return rtsp_msg_buf_fl->node_num;
}

BOOL rtsp_parse_buf_init(int nums)
{
    BOOL ret = rtsp_msg_buf_init(nums);
    return ret;
}

void rtsp_parse_buf_deinit()
{
    rtsp_msg_buf_deinit();
}



