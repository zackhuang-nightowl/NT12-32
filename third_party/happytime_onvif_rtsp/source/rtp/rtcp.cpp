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
#include "rtcp.h"
#include "rtp.h"
#include "rtp_tx.h"
#include "util.h"
#include "rtsp_util.h"
#include "net_util.h"

#ifdef RTSP_RTCP

/**************************************************************************************/

/* RTCP packets use 0.5% of the bandwidth */
#define RTCP_TX_RATIO_NUM   5
#define RTCP_TX_RATIO_DEN   1000

#define RTCP_SR_SIZE        28

extern int rtp_tcp_tx(RSSUA * p_rua, uint8 * data, int size);

/**************************************************************************************/

int rtcp_udp_tx(RSSUA * p_rua, int av_t, uint8 * data, int size)
{
    int slen;
    SOCKET fd;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;

    if (0 == p_rua->rtp_unicast)
    {
        HT_SOCKADDR sockaddr;

        if (!get_address_by_name(p_rua->channels[av_t].destination, HT_PROTOCOL_UDP, &sockaddr))
        {
            return -1;
        }

        if (sockaddr.ipv6_flag)
        {
            memcpy(&addr6, &sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
            addr6.sin6_port = htons(p_rua->channels[av_t].r_port+1);
            addr = (struct sockaddr *) &addr6;
            addrlen = sizeof(struct sockaddr_in6);
        }
        else
        {
            memcpy(&addr4, &sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
            addr4.sin_port = htons(p_rua->channels[av_t].r_port+1);
            addr = (struct sockaddr *) &addr4;
            addrlen = sizeof(struct sockaddr_in);
        }
    }
    else if (p_rua->sockaddr.ipv4_flag)
    {
        memcpy(&addr4, &p_rua->sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
        addr4.sin_port = htons(p_rua->channels[av_t].r_port+1);
        addr = (struct sockaddr *) &addr4;
        addrlen = sizeof(struct sockaddr_in);
    }
    else if (p_rua->sockaddr.ipv6_flag)
    {
        memcpy(&addr6, &p_rua->sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
        addr6.sin6_port = htons(p_rua->channels[av_t].r_port+1);
        addr = (struct sockaddr *) &addr6;
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        return -1;
    }

    fd = p_rua->channels[av_t].rtcp_fd;
    if (fd <= 0)
    {
        return -1;
    }

    slen = sendto(fd, (char *)data, size, 0, addr, addrlen);
    if (slen != size)
    {
        char ip[64] = {'\0'};
        log_print(HT_LOG_ERR, "%s, slen = %d, size = %d, ip=%s\r\n", 
            __FUNCTION__, slen, size, get_sockaddr_ip(addr, ip, sizeof(ip)));
    }

    return slen;
}

int rtcp_build(RSSUA * p_rua, int av_t, uint8 * data, int size)
{
    int slen = 0;
    int offset = 0;

    if (p_rua->rtp_tcp)
    {
        data -= 4;
        
        *(data+offset) = 0x24; // magic
        offset++;

        *(data+offset) = p_rua->channels[av_t].interleaved + 1; // interleaved
        offset++;

        offset += net_write_uint16(data+offset, size); // rtcp payload length
    }

    int len = offset + size;

#ifdef RTSP_SRTP
    if (p_rua->srtp_enable)
    {
        if (!p_rua->channels[av_t].srtp)
        {
            log_print(HT_LOG_ERR, "%s, srtp is null!\r\n", __FUNCTION__);
            return -1;
        }
        
        if (p_rua->rtp_tcp)
        {
            len -= 4;   // skip rtsp tcp header length
            
            if (srtp_protect_rtcp(p_rua->channels[av_t].srtp, data+4, &len) != srtp_err_status_ok)
            {
                log_print(HT_LOG_ERR, "%s, srtp_protect_rtcp failed\r\n", __FUNCTION__);
                return -1;
            }

            net_write_uint16(data+2, len); // update rtcp payload length
            len += 4;   // add rtsp tcp header length
        }
        else
        {
            if (srtp_protect_rtcp(p_rua->channels[av_t].srtp, data, &len) != srtp_err_status_ok)
            {
                log_print(HT_LOG_ERR, "%s, srtp_protect_rtcp failed\r\n", __FUNCTION__);
                return -1;
            }
        }
    }
#endif

    if (p_rua->rtp_tcp)
    {
        slen = rtp_tcp_tx(p_rua, data, len);
    }
    else
    {
        slen = rtcp_udp_tx(p_rua, av_t, data, len);
    }

    return (slen == len) ? slen : -1;
}

/* send an rtcp sender report packet */
static void rtcp_send_sr(RSSUA * p_rua, int av_t, uint32 ts, uint64 ntp_time, int bye)
{
    int offset = 0;
    uint8 data[2048];
    uint8 * p_data = data + 32;
    
    memset(data, 0, sizeof(data));

    UA_RTP_INFO  * p_rtp;
    UA_RTCP_INFO * p_rtcp;

    p_rtp = &p_rua->channels[av_t].rtp_info;
    p_rtcp = &p_rua->channels[av_t].rtcp_info;
    
    p_rtcp->last_rtcp_ntp_time = ntp_time;

    *(p_data+offset) = (RTP_VERSION << 6);
    offset++;
    
    *(p_data+offset) = RTCP_SR;
    offset++;

    offset += net_write_uint16(p_data+offset, 6);
    offset += net_write_uint32(p_data+offset, p_rtp->rtp_ssrc);
    offset += net_write_uint32(p_data+offset, ntp_time / 1000000);
    offset += net_write_uint32(p_data+offset, ((ntp_time % 1000000) << 32) / 1000000);
    offset += net_write_uint32(p_data+offset, ts);
    offset += net_write_uint32(p_data+offset, p_rtcp->packet_count);
    offset += net_write_uint32(p_data+offset, p_rtcp->octet_count);

    strcpy(p_rtcp->cname, "localhost.localdomain");

    if (p_rtcp->cname[0] != '\0') 
    {
        int len = strlen(p_rtcp->cname);

        *(p_data+offset) = (RTP_VERSION << 6) + 1; // version and count
        offset++;
        
        *(p_data+offset) = RTCP_SDES;
        offset++;

        offset += net_write_uint16(p_data+offset, (7 + len + 3) / 4);

        offset += net_write_uint32(p_data+offset, p_rtp->rtp_ssrc); // ssrc
        
        *(p_data+offset) = 0x01; // CNAME
        offset++;
        
        *(p_data+offset) = len;
        offset++;
        
        strcpy((char*)p_data+offset, p_rtcp->cname);

        offset += len;

        p_data[offset++] = 0; // END
        
        for (len = (7 + len) % 4; len % 4; len++)
        {
            p_data[offset++] = 0;
        }
    }

    rtcp_build(p_rua, av_t, p_data, offset);
}

void rtcp_send_packet(RSSUA * p_rua, int av_t, uint32 ts)
{
    if (p_rua->skip_rtcp)
    {
        return;
    }

    int rtcp_bytes;
    UA_RTCP_INFO * p_rtcp;

    p_rtcp = &p_rua->channels[av_t].rtcp_info;
    
    rtcp_bytes = ((p_rtcp->octet_count - p_rtcp->last_octet_count) * RTCP_TX_RATIO_NUM) / RTCP_TX_RATIO_DEN;
        
    if (!p_rtcp->first_packet || ((rtcp_bytes >= RTCP_SR_SIZE) && 
        (get_ntp_time() - p_rtcp->last_rtcp_ntp_time > 5000000))) 
    {
        rtcp_send_sr(p_rua, av_t, ts, get_ntp_time(), 0);
        
        p_rtcp->last_octet_count = p_rtcp->octet_count;
        p_rtcp->first_packet = 1;
    }
}

void * rtsp_rtcp_rx_thread(void * argv)
{
    int i, maxfd, fd;
    fd_set fdr;
    RSSUA * p_rua = (RSSUA *) argv;
    char buf[2048];
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    
    while (p_rua->rtcp_rx)
    {
        maxfd = 0;
        FD_ZERO(&fdr);

        for (i = 0; i < AV_MAX_CHS; i++)
        {
            fd = p_rua->channels[i].rtcp_fd;
            if (fd > 0)
            {
                FD_SET(fd, &fdr);
                
                if (fd > maxfd)
                {
                    maxfd = fd;
                }
            }
        }

        struct timeval tv = {1, 0};

        int sret = select(maxfd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            usleep(10*1000);
            continue;
        }

        for (i = 0; i < AV_MAX_CHS; i++)
        {
            fd = p_rua->channels[i].rtcp_fd;
            if (fd <= 0 || !FD_ISSET(fd, &fdr))
            {
                continue;
            }

            if (p_rua->sockaddr.ipv6_flag)
            {
                addr = (struct sockaddr *) &addr6;
                addrlen = sizeof(struct sockaddr_in6);
            }
            else
            {
                addr = (struct sockaddr *) &addr4;
                addrlen = sizeof(struct sockaddr_in);
            }

            int rlen = recvfrom(fd, buf, sizeof(buf), 0, addr, &addrlen);
            if (rlen <= 0)
            {
                log_print(HT_LOG_ERR, "%s, recvfrom return %d,err[%s]!!!\r\n", 
                    __FUNCTION__, rlen, sys_os_get_socket_error());
            }
            else
            {
                p_rua->last_rx_time = sys_os_get_uptime();

                // todo : handle the rtcp packet ...
                
            }
        }
    }

    return NULL;
}

#endif // end of RTSP_RTCP


