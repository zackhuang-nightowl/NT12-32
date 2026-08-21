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
#include "rtsp_srv_ws.h"
#include "rtsp_http.h"
#include "http_parse.h"
#include "base64.h"
#include "sha1.h"
#include "rtsp_rsua.h"
#include "rtsp_srv.h"
#include "rtp.h"
#include "rtp_tx.h"
#include "http_srv.h"
#include "net_util.h"
#ifdef RTSP_BACKCHANNEL
#include "rtsp_srv_backchannel.h"
#endif

#ifdef RTSP_OVER_WEBSOCKET

/**************************************************************************************/

int rtsp_ws_build_res(char * bufs, int buflen, char * key, char * protocol)
{
    int   offset = 0;
    uint8 hash[20];
    char  acceptKey[100];
    sha1_context ctx;
    
    sha1_starts(&ctx);
    sha1_update(&ctx, (uint8 *)key, (uint32)strlen(key));
    sha1_update(&ctx, (uint8 *)"258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    sha1_finish(&ctx, (uint8 *)hash);

    base64_encode(hash, 20, acceptKey, sizeof(acceptKey));

    offset += snprintf(bufs+offset, buflen-offset, 
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n"
                "Sec-WebSocket-Protocol: %s\r\n"
                "Sec-WebSocket-Version: %d\r\n"
                "\r\n",
                acceptKey,
                protocol,
                WS_VERSION);

    bufs[offset] = '\0';
    
    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", bufs);

    return offset;
}

void rtsp_ws_ver_err_res(HTTPCLN * p_user)
{
    int offset = 0;
    char bufs[256] = {'\0'};
    int buflen = sizeof(bufs);
    
    offset += snprintf(bufs, buflen-offset, 
                "HTTP/1.1 426 Upgrade Required\r\n"
                "Sec-WebSocket-Version: %d\r\n"
                "\r\n", 
                WS_VERSION);

    http_srv_cln_tx(p_user, bufs, offset);
}

BOOL rtsp_ws_msg_process(HTTPCLN * p_user, HTTPMSG * rx_msg)
{
    int version;
    char path[512] = {'\0'}, protocol[32] = {'\0'};
    char *value, *ws_protocol;
    
    if (strcasecmp(rx_msg->first_line.header, "GET"))
    {
        log_print(HT_LOG_ERR, "%s, The request method (%s) is invalid.\r\n", 
            __FUNCTION__, rx_msg->first_line.header);
        return FALSE;
    }

    if (sscanf(rx_msg->first_line.value_string, "%[^ ] %[^ ]", path, protocol) != 2) 
    {
        log_print(HT_LOG_ERR, "%s, Can't parse request %s\r\n", 
            __FUNCTION__, rx_msg->first_line.value_string);
        return FALSE;
    }

    if (strcasecmp(protocol, "HTTP/1.1"))
    {
        log_print(HT_LOG_ERR, "%s, The http version (%s) is invalid.\r\n", 
            __FUNCTION__, protocol);
        return FALSE;
    }

    value = http_get_headline(rx_msg, "Upgrade");
    if (!value || strncasecmp("websocket", value, 9))
    {
        log_print(HT_LOG_ERR, "%s, Unknown upgrade value, %s\r\n", 
            __FUNCTION__, value);
        return FALSE;
    }

    value = http_get_headline(rx_msg, "Connection");
    if (!value || strncasecmp("Upgrade", value, 9))
    {
        log_print(HT_LOG_ERR, "%s, Unknown connection value, %s\r\n", 
            __FUNCTION__, value);
        return FALSE;
    }

    value = http_get_headline(rx_msg, "Sec-WebSocket-Protocol");
    if (!value)
    {
        log_print(HT_LOG_ERR, "%s, without protocol\r\n", __FUNCTION__);
        return FALSE;
    }

    ws_protocol = value;
    
    value = http_get_headline(rx_msg, "Sec-WebSocket-Version");
    if (!value)
    {
        log_print(HT_LOG_ERR, "%s, without version\r\n", __FUNCTION__);
        return FALSE;
    }

    version = strtol(value, (char **) NULL, 10);
    if (version != WS_VERSION)
    {
        rtsp_ws_ver_err_res(p_user);
        
        log_print(HT_LOG_ERR, "%s, version %d mismatch\r\n", 
            __FUNCTION__, version);
        return FALSE;
    }

    value = http_get_headline(rx_msg, "Sec-WebSocket-Key");
    if (!value)
    {
        log_print(HT_LOG_ERR, "%s, without key\r\n", __FUNCTION__);
        return FALSE;
    }

    int offset = 0;
    char bufs[1024] = {'\0'};
    
    offset = rtsp_ws_build_res(bufs, sizeof(bufs), value, ws_protocol);

    int slen = http_srv_cln_tx(p_user, bufs, offset);
    if (slen != offset)
    {
        log_print(HT_LOG_ERR, "%s, message send failed. %d\r\n", 
            __FUNCTION__, slen);
        return FALSE;
    }

    RSSUA * p_rua = rsua_get_idle_rua();
    if (NULL == p_rua)
    {
        log_print(HT_LOG_ERR, "%s, rua_get_idle_rua failed\r\n", __FUNCTION__);
        return FALSE;
    }

    p_rua->rtp_tcp = 1;
    p_rua->last_rx_time = sys_os_get_uptime();
    p_rua->ws_msg.buff = (char *)malloc(WS_MAXMESSAGE);
    if (NULL == p_rua->ws_msg.buff)
    {
        rsua_set_idle_rua(p_rua);
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    p_rua->ws_msg.buff_len = WS_MAXMESSAGE;
    p_rua->http_cln = p_user;
    
    rsua_set_online_rua(p_rua);

    sys_os_mutex_enter(p_user->use_count_mutex);
    p_user->use_count++;
    sys_os_mutex_leave(p_user->use_count_mutex);

    p_user->userdata = p_rua;
    p_user->userdata_mutex = sys_os_create_mutex();
    p_user->protocol = PROTO_RTSP_OVER_WEBSOCKET;
    p_user->pass_through = TRUE;
    
    return TRUE;
}

BOOL rtsp_ws_send_msg(RSSUA * p_rua, int opcode, uint8 have_mask, char * buff, int len)
{
    int slen;
    int offset = len;
    uint8 * p_buff = (uint8 *)buff;
    
    int extra = ws_encode_data(p_buff, offset, opcode, have_mask);
    
    offset += extra;
    p_buff -= extra;

    slen = http_srv_cln_tx((HTTPCLN*)p_rua->http_cln, (char *)p_buff, offset);
    if (slen != offset)
    {
        log_print(HT_LOG_ERR, "%s, message send failed. slen=%d\r\n", 
            __FUNCTION__, slen);
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_ws_ping_process(RSSUA * p_rua, char * buff, int len)
{
    BOOL ret;
    char * p_buff = (char *)malloc(len + 32);
    if (NULL == p_buff)
    {
        return FALSE;
    }

    memcpy(p_buff+32, buff, len);
    
    ret = rtsp_ws_send_msg(p_rua, WS_OPCODE_PONG, 0, p_buff+32, len);

    free(p_buff);

    return ret;
}

BOOL rtsp_ws_rtsp_process(RSSUA * p_rua, char * p_buff, int len)
{
    if (NULL == p_rua->rtp_rcv_buf || 0 == p_rua->rtp_t_len)
    {
        memcpy(p_rua->rcv_buf + p_rua->rcv_dlen, p_buff, len);
        p_rua->rcv_dlen += len;

        if (p_rua->rcv_dlen < 16)
        {
            return TRUE;
        }
    }
    else
    {
        if (len > p_rua->rtp_t_len - p_rua->rtp_rcv_len)
        {
            log_print(HT_LOG_ERR, "%s, bufflen=%d, rtp_t_len=%d, rtp_rcv_len=%d\r\n", 
                __FUNCTION__, len, p_rua->rtp_t_len, p_rua->rtp_rcv_len);
            return FALSE;
        }

        memcpy(p_rua->rtp_rcv_buf + p_rua->rtp_rcv_len, p_buff, len);
        p_rua->rtp_rcv_len += len;

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
        
        return TRUE;
    }

    return rtsp_data_parser(p_rua);
}

BOOL rtsp_ws_data_process(HTTPCLN * p_user, char * buff, int len)
{
    if (NULL == p_user->userdata)
    {
        log_print(HT_LOG_ERR, "%s, rua is null\r\n", __FUNCTION__);
        return FALSE;
    }
    
    RSSUA * p_rua = (RSSUA *) p_user->userdata;
    WSMSG * p_msg = &p_rua->ws_msg;

    if (len + p_msg->rcv_len > p_msg->buff_len)
    {
        log_print(HT_LOG_ERR, "%s, recv length(%d) bigger than buff length(%d)\r\n", 
            __FUNCTION__, len + p_msg->rcv_len, p_msg->buff_len);
        return FALSE;
    }

    p_rua->last_rx_time = sys_os_get_uptime();
    
    memcpy(p_msg->buff + p_msg->rcv_len, buff, len);
    p_msg->rcv_len += len;

PARSE:

    int ret = ws_decode_data(p_msg);
    if (ret > 0)
    {
        if (p_msg->opcode == WS_OPCODE_TEXT || p_msg->opcode == WS_OPCODE_BINARY)
        {
            rtsp_ws_rtsp_process(p_rua, p_msg->buff+p_msg->skip, p_msg->msg_len);
        }
        else if (p_msg->opcode == WS_OPCODE_CLOSE)
        {
            log_print(HT_LOG_INFO, "%s, recv close message\r\n", __FUNCTION__);
            return FALSE;
        }
        else if (p_msg->opcode == WS_OPCODE_PING)
        {
            rtsp_ws_ping_process(p_rua, p_msg->buff+p_msg->skip, p_msg->msg_len);
        }
        else if (p_msg->opcode == WS_OPCODE_PONG)
        {
            log_print(HT_LOG_INFO, "%s, recv pong message\r\n", __FUNCTION__);
        }
        else
        {
            log_print(HT_LOG_INFO, "%s, recv unkonw message %d\r\n", __FUNCTION__, p_msg->opcode);
            return FALSE;
        }
        
        p_msg->rcv_len -= p_msg->msg_len + p_msg->skip;

        if (p_msg->rcv_len > 0)
        {
            memmove(p_msg->buff, p_msg->buff + p_msg->msg_len + p_msg->skip, p_msg->rcv_len);
        }
        
        if (p_msg->rcv_len >= 6)
        {
            goto PARSE;
        }

        return TRUE;
    }
    else if (ret == 0) // need more data to parse
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

#endif



