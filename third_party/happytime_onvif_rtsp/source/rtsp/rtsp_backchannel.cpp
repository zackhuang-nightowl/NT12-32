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
#include "rtsp_backchannel.h"
#include "rtsp_rcua.h"
#include "rtp.h"
#include "rtsp_util.h"
#include "net_util.h"
#include "base64.h"
#include "http_cln.h"
#include "media_format.h"
#ifdef OVER_WEBSOCKET
#include "ws.h"
#endif

#ifdef BACKCHANNEL

/***************************************************************************************/

/**
 * Send rtp data by TCP socket
 *
 * @param p_rua rtsp user agent
 * @param data rtp data
 * @param size rtp data len
 * @return -1 on error, or the data length has been sent
 */
int rtp_tcp_tx(RCUA * p_rua, uint8 * data, int size)
{
    int offset = 0;
    SOCKET fd;
    int buflen;
    char * p_buff;
    
#ifdef OVER_HTTP
    if (p_rua->over_http)
    {
        fd = p_rua->rtsp_send.cfd;
    }
    else
#endif
#ifdef OVER_WEBSOCKET
    if (p_rua->over_ws)
    {
        fd = p_rua->ws_http.cfd;
    }
    else
#endif
    {
        fd = p_rua->fd;
    }
    
    if (fd <= 0)
    {
        return -1;
    }

#ifdef OVER_HTTP
    if (p_rua->over_http)
    {
        char * tx_buf_base64 = (char *)malloc(2 * size);
        if (NULL == tx_buf_base64)
        {
            return -1;
        }
        
        if (0 == base64_encode(data, size, tx_buf_base64, 2 * size))
        {
            free(tx_buf_base64);
            return -1;
        }

        p_buff = tx_buf_base64;
        buflen = strlen(tx_buf_base64);
    }
    else
#endif
#ifdef OVER_WEBSOCKET
    if (p_rua->over_ws)
    {
        int extra = ws_encode_data(data, size, WS_OPCODE_BINARY, 1);

        p_buff = (char *)(data - extra);
        buflen = size + extra;
    }
    else
#endif
#ifdef RTSPS
    if (p_rua->rtsps)
    {
        return rcua_ssl_tx(p_rua, (char *)data, size);
    }
    else
#endif
    {
        p_buff = (char *)data;
        buflen = size;
    }

    while (offset < buflen)
    {
        int tlen;
        
#ifdef OVER_HTTP
        if (p_rua->over_http)
        {
            tlen = http_cln_tx(&p_rua->rtsp_send, p_buff+offset, buflen-offset);
        }
        else 
#endif
#ifdef OVER_WEBSOCKET
        if (p_rua->over_ws)
        {
            tlen = http_cln_tx(&p_rua->ws_http, p_buff+offset, buflen-offset);
        }
        else 
#endif
        {
#if __WINDOWS_OS__
            tlen = send(fd, (const char *)p_buff+offset, buflen-offset, 0);
#else
            tlen = send(fd, (const char *)p_buff+offset, buflen-offset, MSG_DONTWAIT);
#endif
        }
        
        if (tlen > 0)
        {
            offset += tlen;
        }
        else
        {
            int sockerr = sys_os_get_socket_error_num();
            if (sockerr == EINTR || sockerr == EAGAIN)
            {
                usleep(1000);
                continue;
            }
            
            log_print(HT_LOG_ERR, "%s, send failed, fd[%d], tlen[%d,%d], err[%d][%s]!!!\r\n",
                __FUNCTION__, fd, tlen, buflen-offset, sockerr, sys_os_get_socket_error());            
            break;
        }
    }

#ifdef OVER_HTTP
    if (p_rua->over_http)
    {
        free(p_buff);
    }
#endif

    return (offset == buflen) ? size : -1;
}

int rtp_udp_tx(RCUA * p_rua, int av_t, uint8 * data, int size)
{
    int slen;
    SOCKET fd = 0;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    
    if (AV_TYPE_BACKCHANNEL != av_t)
    {
        return -1;
    }
    
    if (p_rua->rtp_mcast)
    {
        HT_SOCKADDR sockaddr;

        if (!get_address_by_name(p_rua->channels[av_t].destination, HT_PROTOCOL_UDP, &sockaddr))
        {
            return -1;
        }

        if (sockaddr.ipv6_flag)
        {
            memcpy(&addr6, &sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
            addr6.sin6_port = htons(p_rua->channels[av_t].r_port);
            addr = (struct sockaddr *) &addr6;
            addrlen = sizeof(struct sockaddr_in6);
        }
        else
        {
            memcpy(&addr4, &sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
            addr4.sin_port = htons(p_rua->channels[av_t].r_port);
            addr = (struct sockaddr *) &addr4;
            addrlen = sizeof(struct sockaddr_in);
        }
    }
    else if (p_rua->sockaddr.ipv4_flag)
    {
        memcpy(&addr4, &p_rua->sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
        addr4.sin_port = htons(p_rua->channels[av_t].r_port);
        addr = (struct sockaddr *) &addr4;
        addrlen = sizeof(struct sockaddr_in);
    }
    else if (p_rua->sockaddr.ipv6_flag)
    {
        memcpy(&addr6, &p_rua->sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
        addr6.sin6_port = htons(p_rua->channels[av_t].r_port);
        addr = (struct sockaddr *) &addr6;
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        return -1;
    }

    fd = p_rua->channels[av_t].udp_fd;
    if (fd <= 0)
    {
        return -1;
    }

    slen = sendto(fd, (char *)data, size, 0, addr, addrlen);
    if (slen != size)
    {
        char ip[64] = {'\0'};
        log_print(HT_LOG_ERR, "%s, send failed. slen = %d, size = %d, [%s:%d]\r\n", __FUNCTION__, 
            slen, size, get_sockaddr_ip(addr, ip, sizeof(ip)), get_sockaddr_port(addr));
    }

    return slen;
}

/**
 * Build audio rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @ts the packet timestamp
 * @return the rtp packet length, -1 on error
 */
int rtp_audio_build(RCUA * p_rua, uint8 * data, int size, uint32 ts, int mbit)
{
    int offset = 0;
    int slen = 0;    
    uint8 * p_rtp_ptr = data - 12; // shift forward RTP head

    if (p_rua->rtp_tcp)
    {
        p_rtp_ptr -= 4;
        
        *(p_rtp_ptr+offset) = 0x24; // magic
        offset++;
        
        *(p_rtp_ptr+offset) = p_rua->channels[AV_BACK_CH].interleaved;  // interleaved
        offset++;

        offset += net_write_uint16(p_rtp_ptr+offset, 12 + size); // rtp payload length
    }

    *(p_rtp_ptr+offset) = (RTP_VERSION << 6);
    offset++;
    
    *(p_rtp_ptr+offset) = (p_rua->bc_rtp_info.rtp_pt) | ((mbit & 0x01) << 7);
    offset++;

    offset += net_write_uint16(p_rtp_ptr+offset, p_rua->bc_rtp_info.rtp_cnt);

    offset += net_write_uint32(p_rtp_ptr+offset, p_rua->bc_rtp_info.rtp_ts);

    offset += net_write_uint32(p_rtp_ptr+offset, p_rua->bc_rtp_info.rtp_ssrc);

    int len = offset + size;
    uint8 * ptr = p_rtp_ptr;
    
#ifdef SRTP
    uint8 sbuff[32+RTP_MAX_LEN+SRTP_MAX_SRTCP_TRAILER_LEN+64];
    
    if (p_rua->channels[AV_BACK_CH].srtp_enable)
    {
        if (!p_rua->channels[AV_BACK_CH].srtp)
        {
            log_print(HT_LOG_ERR, "%s, srtp is null!\r\n", __FUNCTION__);
            return -1;
        }

        ptr = sbuff + 32;
        memcpy(ptr, p_rtp_ptr, len);

        if (p_rua->rtp_tcp)
        {
            len -= 4;   // skip rtsp tcp header length

            if (srtp_protect(p_rua->channels[AV_BACK_CH].srtp, ptr+4, &len) != srtp_err_status_ok)
            {
                log_print(HT_LOG_ERR, "%s, srtp_protect failed\r\n", __FUNCTION__);
                return -1;
            }

            net_write_uint16(ptr+2, len); // update rtp payload length
            len += 4;   // add rtsp tcp header length
        }
        else
        {
            if (srtp_protect(p_rua->channels[AV_BACK_CH].srtp, ptr, &len) != srtp_err_status_ok)
            {
                log_print(HT_LOG_ERR, "%s, srtp_protect failed\r\n", __FUNCTION__);
                return -1;
            }
        }
    }
#endif

    if (p_rua->rtp_tcp)
    {
        slen = rtp_tcp_tx(p_rua, ptr, len);
    }
    else
    {
        slen = rtp_udp_tx(p_rua, AV_TYPE_BACKCHANNEL, ptr, len);
    }

    p_rua->bc_rtp_info.rtp_cnt = (p_rua->bc_rtp_info.rtp_cnt + 1) & 0xFFFF;
    
    return (slen == len) ? slen : -1;
}

/**
 * Build audio rtp packet and send (not fragment)
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @ts the packet timestamp
 * @return the rtp packet length, -1 on error
 */
int rtp_audio_tx(RCUA * p_rua, uint8 * data, int size, uint32 ts)
{
    int ret = 0;
    int len, max_size;

    max_size = RTP_MAX_LEN;

    while (size > 0) 
    {
        len = max_size;
        if (len > size)
        {
            len = size;
        }

        ret = rtp_audio_build(p_rua, data, len, ts, (len == size));
        if (ret < 0)
        {
            break;
        }

        data += len;
        size -= len;
    }

    return ret;
}

int rtp_aac_audio_tx(RCUA * p_rua, uint8 * data, int size, uint32 ts)
{
    int ret = 0;
    int len, max_size, au_size;
    uint8 *p;

    max_size = RTP_MAX_LEN;

    if (data[0] == 0xFF && (data[1] & 0xF0) == 0xF0)
    {
        // skip ADTS header
        data += 7;
        size -= 7;
    }

    p = data - 4;
    au_size = size;
        
    while (size > 0) 
    {
        len = MIN(size, max_size);

        net_write_uint16(p, 2 * 8);
        net_write_uint16(&p[2], au_size * 8);
        
        ret = rtp_audio_build(p_rua, p, len + 4, ts, len == size);
        if (ret < 0)
        {
            break;
        }
        
        size -= len;
        p += len;
    }

    return ret;
}

void rtsp_bc_cb(uint8 * data, int size, int nbsamples, void *pUserdata)
{
    RCUA * p_rua = (RCUA *)pUserdata;

    if (p_rua->send_bc_data)
    {
        uint8 * p_buf = NULL;
    
        p_buf = (uint8 *)malloc(size + 64);
        if (p_buf == NULL)
        {
            log_print(HT_LOG_ERR, "%s, malloc failed!!!\r\n", __FUNCTION__);
            return;
        }

        uint8 * p_tbuf = p_buf + 64; // shift forword header

        memcpy(p_tbuf, data, size);
    
        if (AUDIO_CODEC_AAC == p_rua->bc_audio_info.codec)
        {
            rtp_aac_audio_tx(p_rua, p_tbuf, size, get_rtp_timestamp(p_rua->bc_audio_info.samplerate));
        }
        else
        {
            rtp_audio_tx(p_rua, p_tbuf, size, get_rtp_timestamp(p_rua->bc_audio_info.samplerate));
        }

        free(p_buf);
    }
}

#else

void rtsp_bc_cb(uint8 * data, int size, int nbsamples, void *pUserdata)
{
}

#endif // BACKCHANNEL



