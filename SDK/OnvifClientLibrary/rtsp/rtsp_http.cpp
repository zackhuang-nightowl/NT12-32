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
#include "http.h"
#include "http_parse.h"
#include "http_srv.h"
#include "rtsp_http.h"
#include "rtsp_srv.h"
#include "rtsp_cfg.h"
#include "rtsp_rsua.h"
#include "rtp.h"
#include "base64.h"
#include "net_util.h"
#ifdef RTSP_BACKCHANNEL
#include "rtsp_srv_backchannel.h"
#endif
#ifdef RTSP_OVER_WEBSOCKET
#include "rtsp_srv_ws.h"
#endif

#ifdef RTSP_OVER_HTTP

/***********************************************************************/

extern RTSP_CLASS   g_rtsp;
extern RTSP_CFG     g_rtsp_cfg;

/***********************************************************************/

BOOL rtsp_http_msg_cb(HTTPCLN * p_cln, HTTPMSG * p_msg, void * userdata)
{
    BOOL ret = TRUE;
    
    if (p_msg)
    {
        if (http_get_headline(p_msg, "x-sessioncookie"))
        {
            ret = rtsp_http_msg_process(p_cln, p_msg);
        }
        
#ifdef RTSP_OVER_WEBSOCKET
        else if (http_get_headline(p_msg, "Sec-WebSocket-Key"))
        {
            ret = rtsp_ws_msg_process(p_cln, p_msg);
        }
#endif

        http_free_msg(p_msg);
    }
    else if (p_cln)
    {
        sys_os_mutex_enter(p_cln->userdata_mutex);
        if (p_cln->userdata)
        {
            RSSUA * p_rua = (RSSUA *) p_cln->userdata;
            rtsp_stop_rua(p_rua);
        }
        sys_os_mutex_leave(p_cln->userdata_mutex);
        
        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
    }
    
    return ret;
}

void rtsp_http_data_cb(HTTPCLN * p_cln, char * buff, int buflen, void * userdata)
{
    BOOL ret = FALSE;

    sys_os_mutex_enter(p_cln->userdata_mutex);
    
    if (PROTO_RTSP_OVER_HTTP == p_cln->protocol)
    {
        ret = rtsp_http_data_process(p_cln, buff, buflen);
    }
#ifdef RTSP_OVER_WEBSOCKET    
    else if (PROTO_RTSP_OVER_WEBSOCKET == p_cln->protocol)
    {
        ret = rtsp_ws_data_process(p_cln, buff, buflen);
    }
#endif

    sys_os_mutex_leave(p_cln->userdata_mutex);
    
    if (!ret)
    {
        sys_os_mutex_enter(p_cln->userdata_mutex);
        if (p_cln->userdata)
        {
            RSSUA * p_rua = (RSSUA *) p_cln->userdata;
            rtsp_stop_rua(p_rua);
        }
        sys_os_mutex_leave(p_cln->userdata_mutex);
        
        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
    }
}

BOOL rtsp_http_init()
{
#ifdef NO_ONVIF_SERVER
    if (g_rtsp_cfg.http_port <= 0 || g_rtsp_cfg.http_port > 65535)
    {
        g_rtsp_cfg.http_port = 80;
    }

    if (http_srv_init(&g_rtsp.http_srv, NULL, g_rtsp_cfg.http_port, MAX_NUM_RUA*2, 0, NULL, NULL, g_rtsp_cfg.ipv6_enable))
    {
        http_set_msg_cb(&g_rtsp.http_srv, rtsp_http_msg_cb, NULL);
        http_set_data_cb(&g_rtsp.http_srv, rtsp_http_data_cb, NULL);
    }
#endif

    return TRUE;
}

void rtsp_http_deinit()
{
#ifdef NO_ONVIF_SERVER
    http_srv_deinit(&g_rtsp.http_srv);
#endif
}

#ifdef HTTPS

BOOL rtsp_https_init()
{
#ifdef NO_ONVIF_SERVER
    if (g_rtsp_cfg.https_port <= 0 || g_rtsp_cfg.https_port > 65535)
    {
        g_rtsp_cfg.https_port = 443;
    }

    if (http_srv_init(&g_rtsp.https_srv, NULL, g_rtsp_cfg.https_port, MAX_NUM_RUA*2, 1, 
        g_rtsp_cfg.https_cert, g_rtsp_cfg.https_key, g_rtsp_cfg.ipv6_enable))
    {
        http_set_msg_cb(&g_rtsp.https_srv, rtsp_http_msg_cb, NULL);
        http_set_data_cb(&g_rtsp.https_srv, rtsp_http_data_cb, NULL);
    }
#endif

    return TRUE;
}

void rtsp_https_deinit()
{
#ifdef NO_ONVIF_SERVER
    http_srv_deinit(&g_rtsp.https_srv);
#endif
}

#endif

int rtsp_http_rly(HTTPCLN * p_user, HTTPMSG * rx_msg, const char * p_xml, int len)
{
    int tlen;
    char * p_bufs;

    p_bufs = (char *)malloc(len + 1024);
    if (NULL == p_bufs)
    {
        return -1;
    }
    
    tlen = snprintf(p_bufs, len + 1024, 
            "HTTP/1.1 200 OK\r\n"
            "Server: happytimesoft\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n\r\n",
            "application/x-rtsp-tunnelled", 
            len);

    if (p_xml && len > 0)
    {
        memcpy(p_bufs+tlen, p_xml, len);
        tlen += len;
    }

    p_bufs[tlen] = '\0';
    
    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", p_bufs);

    tlen = http_srv_cln_tx(p_user, p_bufs, tlen);

    free(p_bufs);
    
    return tlen;
}

BOOL rtsp_http_msg_process(HTTPCLN * p_user, HTTPMSG * rx_msg)
{
    char * p = http_get_headline(rx_msg, "x-sessioncookie");
    if (NULL == p)
    {
        return FALSE;
    }

    if (p_user->userdata)
    {
        log_print(HT_LOG_ERR, "%s, rua already exist\r\n", __FUNCTION__);
        return FALSE;
    }
    
    if (strstr(rx_msg->first_line.header, "GET"))
    {
        RSSUA * p_rua = rsua_get_idle_rua();
        if (NULL == p_rua)
        {
            log_print(HT_LOG_ERR, "%s, rua_get_idle_rua failed\r\n", __FUNCTION__);
            return FALSE;
        }

        p_rua->rtp_tcp = 1;
        p_rua->rtsp_send = p_user;
        p_rua->last_rx_time = sys_os_get_uptime();
        strncpy(p_rua->sessioncookie, p, sizeof(p_rua->sessioncookie)-1);
        
        rsua_set_online_rua(p_rua);

        sys_os_mutex_enter(p_user->use_count_mutex);
        p_user->use_count++;
        sys_os_mutex_leave(p_user->use_count_mutex);
        
        p_user->userdata = p_rua;
        p_user->userdata_mutex = sys_os_create_mutex();
        p_user->pass_through = TRUE;

        rtsp_http_rly(p_user, rx_msg, NULL, 0);
    }
    else if (strstr(rx_msg->first_line.header, "POST"))
    {
        RSSUA * p_rua = rsua_get_by_sessioncookie(p);        
        if (p_rua)
        {
            p_rua->last_rx_time = sys_os_get_uptime();
            p_rua->rtsp_recv = p_user;

            sys_os_mutex_enter(p_user->use_count_mutex);
            p_user->use_count++;
            sys_os_mutex_leave(p_user->use_count_mutex);

            p_user->userdata = p_rua;
            p_user->userdata_mutex = sys_os_create_mutex();
            p_user->pass_through = TRUE;

            p_rua->base64_buff_len = 0;
            p_rua->base64_buff_tlen = 10000;
            p_rua->base64_buff = (char *)malloc(p_rua->base64_buff_tlen);
            if (p_rua->base64_buff)
            {
                memset(p_rua->base64_buff, 0, p_rua->base64_buff_tlen);
            }
            else
            {
                p_rua->base64_buff_tlen = 0;
            }
        }
        else
        {
            return FALSE;
        }
    }

    return TRUE;
}

int rtsp_split_base64(char * buff, int len, char **base64_start, int *start_len, int *have_next)
{
    int i = 0;

    // skip non-base64 char
    
    for (; i < len; i++)
    {
        if (is_base64_char(buff[i]))
        {
            break;
        }
    }

    *start_len = i;
    *base64_start = buff + i;
    
    for (; i < len; i++)
    {
        char c = buff[i];

        if (c == '\r' || c == '\n' || c == '\t')
        {
            continue;
        }
        else if (c == '=')
        {
            break;
        }
        else if (is_base64_char(c)) 
        {
            continue;
        }
        else 
        {
            break;
        }
    }
    
    if (i < len)
    {
        while (i < len && buff[i] == '=')
        {
            i++;
        }

        *have_next = (i < len) ? 1 : 0;
        return i - *start_len;
    }
    else
    {
        *have_next = 0;
        return len - *start_len;
    }
}

int rtsp_http_data_process2(RSSUA * p_rua, uint8 * buff, int buff_len)
{
    int remain_len = buff_len;
    
    if (NULL == p_rua->rtp_rcv_buf || 0 == p_rua->rtp_t_len)
    {
        if (buff_len <= (int)sizeof(p_rua->rcv_buf) - 4 - p_rua->rcv_dlen)
        {
            memcpy(p_rua->rcv_buf + p_rua->rcv_dlen, buff, remain_len);
            p_rua->rcv_dlen += buff_len;

            remain_len = 0;
        }
        else
        {
            int diff = sizeof(p_rua->rcv_buf) - 4 - p_rua->rcv_dlen;
            
            memcpy(p_rua->rcv_buf + p_rua->rcv_dlen, buff, diff);
            p_rua->rcv_dlen += diff;

            remain_len -= diff;
        }

        if (p_rua->rcv_dlen < 16)
        {
            return buff_len - remain_len;
        }
    }
    else
    {
        if (buff_len > p_rua->rtp_t_len - p_rua->rtp_rcv_len)
        {
            int diff = p_rua->rtp_t_len - p_rua->rtp_rcv_len;
            
            memcpy(p_rua->rtp_rcv_buf + p_rua->rtp_rcv_len, buff, diff);
            p_rua->rtp_rcv_len = p_rua->rtp_t_len;

            remain_len -= diff;
        }
        else
        {
            memcpy(p_rua->rtp_rcv_buf + p_rua->rtp_rcv_len, buff, buff_len);
            p_rua->rtp_rcv_len += buff_len;

            remain_len = 0;
        }

        if (p_rua->rtp_rcv_len == p_rua->rtp_t_len)
        {
#ifdef RTSP_BACKCHANNEL
            if (p_rua->backchannel)
            {
                rtsp_bc_tcp_data_rx(p_rua, (uint8*)p_rua->rtp_rcv_buf, p_rua->rtp_rcv_len);
            }
#endif
            
            free(p_rua->rtp_rcv_buf);
            p_rua->rtp_rcv_buf = NULL;
            p_rua->rtp_rcv_len = 0;
            p_rua->rtp_t_len = 0;
        }
        
        return buff_len - remain_len;
    }

    if (!rtsp_data_parser(p_rua))
    {
        remain_len = 0;
    }

    return buff_len - remain_len;
}

BOOL rtsp_http_data_process1(RSSUA * p_rua, char * base64, int base64_len)
{
    int len, bufflen;
    uint8 *buff, *buffptr;

    bufflen = 2 * base64_len;
    buff = (uint8 *)malloc(bufflen);
    if (NULL == buff)
    {
        return FALSE;
    }

    buffptr = buff;
    
    bufflen = base64_decode(base64, base64_len, buffptr, bufflen);
    if (-1 == bufflen)
    {
        free(buff);
        log_print(HT_LOG_ERR, "%s, base64_decode failed\r\n", __FUNCTION__);
        return FALSE;
    }

    while (bufflen > 0)
    {
        len = rtsp_http_data_process2(p_rua, buffptr, bufflen);
        if (len == 0)
        {
            break;
        }

        bufflen -= len;
        buffptr += len;
    }

    free(buff);

    return TRUE;
}

BOOL rtsp_http_data_process(HTTPCLN * p_user, char * p_buff, int len)
{
    if (NULL == p_user->userdata)
    {
        log_print(HT_LOG_ERR, "%s, rua is null\r\n", __FUNCTION__);
        return FALSE;
    }

    RSSUA * p_rua = (RSSUA *) p_user->userdata;
    
    p_rua->last_rx_time = sys_os_get_uptime();

    if (len + p_rua->base64_buff_len <= p_rua->base64_buff_tlen)
    {
        memcpy(p_rua->base64_buff + p_rua->base64_buff_len, p_buff, len);
        p_rua->base64_buff_len += len;
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, len=%d, base64 buff len=%d, tlen=%d\r\n", 
            __FUNCTION__, len, p_rua->base64_buff_len, p_rua->base64_buff_tlen);
        return FALSE;
    }

    char * base64_buff = p_rua->base64_buff;
    int base64_buff_len = p_rua->base64_buff_len;

    while (base64_buff_len > 0)
    {
        char * base64_start = NULL;
        int base64_len = 0;
        int have_next = 0;
        int start_len = 0;

        if (base64_buff[0] == 0x24) // rtp magic
        {
            uint16 rtp_len = net_read_uint16((uint8*)base64_buff+2);
            
            if (base64_buff_len >= rtp_len + 4)
            {
                // skip rtp packet
                base64_buff += rtp_len + 4;
                base64_buff_len -= rtp_len + 4;
                continue;
            }
            else
            {
                // need more data
                break;
            }
        }
    
        base64_len = rtsp_split_base64(base64_buff, base64_buff_len, &base64_start, &start_len, &have_next);

        if (base64_len == 0)
        {
            break;
        }
        else if (is_base64_string(base64_start, base64_len))
        {
            rtsp_http_data_process1(p_rua, base64_start, base64_len);
        }
        else if (!have_next)
        {
            // maybe need more data
            break;
        }

        base64_buff += base64_len + start_len;
        base64_buff_len -= base64_len + start_len;
    }

    if (base64_buff_len > 0)
    {
        if (base64_buff != p_rua->base64_buff)
        {
            memmove(p_rua->base64_buff, base64_buff, base64_buff_len);
        }
        
        p_rua->base64_buff_len = base64_buff_len;
    }
    else
    {
        p_rua->base64_buff_len = 0;
    }

    return TRUE;
}

#endif // end of RTSP_OVER_HTTP



