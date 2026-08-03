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
#include "rtp.h"
#include "rtp_tx.h"
#include "mjpeg_tables.h"
#include "mjpeg.h"
#include "util.h"
#include "h264.h"
#include "h265.h"
#include "media_util.h"
#include "rtsp_util.h"
#include "net_util.h"
#ifdef RTSP_RTCP
#include "rtcp.h"
#endif
#ifdef RTSP_OVER_HTTP
#include "http.h"
#include "http_srv.h"
#endif
#ifdef RTSP_OVER_WEBSOCKET
#include "ws.h"
#include "http_srv.h"
#endif
#ifdef RTSPS
#include "rtsp_srv.h"
#endif

/**************************************************************************************/

/**
 * @brief
 *  Send rtp data by TCP socket
 *
 * @param p_rua rtsp user agent
 * @param data rtp data
 * @param size rtp data length
 *
 * @return -1 on error, or the data length has been sent
 *
 **/
int rtp_tcp_tx(RSSUA * p_rua, uint8 * data, int size)
{
    int tlen;
    int offset = 0;
    SOCKET fd;

#ifdef RTSP_OVER_HTTP
    if (p_rua->rtsp_send)
    {
        return http_srv_cln_tx((HTTPCLN *) p_rua->rtsp_send, (char *)data, size);
    }
#endif

#ifdef RTSP_OVER_WEBSOCKET
    if (p_rua->http_cln)
    {
        int extra = ws_encode_data(data, size, WS_OPCODE_BINARY, 0);
        
        size += extra;
        data -= extra;

        tlen = http_srv_cln_tx((HTTPCLN *) p_rua->http_cln, (char *)data, size);
        return tlen - extra;
    }
#endif

#ifdef RTSPS
    if (p_rua->rtsps)
    {
        return rtsp_srv_ssl_tx(p_rua, (char *)data, size);
    }
#endif

    fd = p_rua->fd;
    if (fd <= 0)
    {
        return -1;
    }

    while (p_rua->rtp_tx && offset < size)
    {
#if __WINDOWS_OS__
        tlen = send(fd, (const char *)data+offset, size-offset, 0);
#else
        tlen = send(fd, (const char *)data+offset, size-offset, MSG_DONTWAIT);
#endif
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
                __FUNCTION__, fd, tlen, size-offset, sockerr, sys_os_get_socket_error());
            return -1;
        }
    }

    return offset;
}

/**
 * @brief
 *  Send rtp data by UDP socket
 *
 * @param p_rua rtsp user agent
 * @param av_t data type
 *  AV_TYPE_VIDEO
 *  AV_TYPE_AUDIO
 *  AV_TYPE_METADATA
 *  AV_TYPE_BACKCHANNEL
 * @param data rtp data
 * @param size rtp data length
 *
 * @return -1 on error, or the data length has been sent
 *
 **/
int rtp_udp_tx(RSSUA * p_rua, int av_t, uint8 * data, int size)
{
    int offset = 0;
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

    while (p_rua->rtp_tx && offset < size)
    {
        int tlen = sendto(fd, (char *)data+offset, size-offset, 0, addr, addrlen);
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
                __FUNCTION__, fd, tlen, size-offset, sockerr, sys_os_get_socket_error());
            return -1;
        }
    }

    return offset;
}

/**
 * @brief
 *  Build rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param av_t data type
 *  AV_TYPE_VIDEO
 *  AV_TYPE_AUDIO
 *  AV_TYPE_METADATA
 *  AV_TYPE_BACKCHANNEL
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 * @param mbit rtp marker flag
 *
 * @return the rtp packet length, -1 on error
 *
 **/
int rtp_build(RSSUA * p_rua, int av_t, uint8 * data, int size, uint32 ts, int mbit)
{
    int offset = 0;
    int slen = 0;
    uint8 buff[RTSP_RESV_HDR_SIZE];
    uint8 * p_rtp_ptr;

#ifdef RTSP_RTCP
    rtcp_send_packet(p_rua, av_t, ts);
#endif

    p_rua->channels[av_t].rtp_info.rtp_ts = ts;
    
    if (p_rua->rtp_tcp)
    {
        *(buff+offset) = 0x24; // magic
        offset++;

        *(buff+offset) = p_rua->channels[av_t].interleaved; // interleaved
        offset++;

#ifdef RTSP_REPLAY
        if (p_rua->replay)
        {
            offset += net_write_uint16(buff+offset, 12 + size + 16); // rtp payload length
        }
        else
#endif
        {
            offset += net_write_uint16(buff+offset, 12 + size);     // rtp payload length
        }
    }

#ifdef RTSP_REPLAY
    if (p_rua->replay)
    {
        *(buff+offset) = (RTP_VERSION << 6) | (1 << 4); // Set RTP header extension bit
    }
    else
#endif    
    {
        *(buff+offset) = (RTP_VERSION << 6);
    }
    offset++;

    *(buff+offset) = (p_rua->channels[av_t].rtp_info.rtp_pt) | ((mbit & 0x01) << 7);
    offset++;

    offset += net_write_uint16(buff+offset, p_rua->channels[av_t].rtp_info.rtp_cnt);

    offset += net_write_uint32(buff+offset, p_rua->channels[av_t].rtp_info.rtp_ts);

    offset += net_write_uint32(buff+offset, p_rua->channels[av_t].rtp_info.rtp_ssrc);

#ifdef RTSP_REPLAY
    if (p_rua->replay)
    {
        offset += net_write_uint16(buff+offset, 0xABAC);
        offset += net_write_uint16(buff+offset, 3);

        offset += net_write_uint32(buff+offset, p_rua->channels[av_t].rep_hdr.ntp_sec);
        offset += net_write_uint32(buff+offset, p_rua->channels[av_t].rep_hdr.ntp_frac);

        *(buff+offset) = ((p_rua->channels[av_t].rep_hdr.t & 0x01) << 4) | 
                         ((p_rua->channels[av_t].rep_hdr.d & 0x01) << 5) | 
                         ((p_rua->channels[av_t].rep_hdr.e & 0x01) << 6) | 
                         ((p_rua->channels[av_t].rep_hdr.c & 0x01) << 7) | 
                         p_rua->channels[av_t].rep_hdr.mbz;
        offset++;

        *(buff+offset) = p_rua->channels[av_t].rep_hdr.seq;
        offset++;

        offset += net_write_uint16(buff+offset, p_rua->channels[av_t].rep_hdr.padding);

        p_rua->channels[av_t].rep_hdr.d = 0;
    }
#endif

    p_rtp_ptr = data - offset;
    memcpy(p_rtp_ptr, buff, offset);

    int len = offset + size;
    uint8 * ptr = p_rtp_ptr;

#ifdef RTSP_SRTP
    uint8 sbuff[32+RTP_MAX_LEN+SRTP_MAX_SRTCP_TRAILER_LEN+64];
    
    if (p_rua->srtp_enable)
    {
        if (!p_rua->channels[av_t].srtp)
        {
            log_print(HT_LOG_ERR, "%s, srtp is null!\r\n", __FUNCTION__);
            return -1;
        }

        ptr = sbuff + 32;
        memcpy(ptr, p_rtp_ptr, len);

        if (p_rua->rtp_tcp)
        {
            len -= 4;   // skip rtsp tcp header length

            if (srtp_protect(p_rua->channels[av_t].srtp, ptr+4, &len) != srtp_err_status_ok)
            {
                log_print(HT_LOG_ERR, "%s, srtp_protect failed\r\n", __FUNCTION__);
                return -1;
            }

            net_write_uint16(ptr+2, len); // update rtp payload length
            len += 4;   // add rtsp tcp header length
        }
        else
        {
            if (srtp_protect(p_rua->channels[av_t].srtp, ptr, &len) != srtp_err_status_ok)
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
        slen = rtp_udp_tx(p_rua, av_t, ptr, len);
    }

    p_rua->channels[av_t].rtp_info.rtp_cnt = (p_rua->channels[av_t].rtp_info.rtp_cnt + 1) & 0xFFFF;

#ifdef RTSP_RTCP
    p_rua->channels[av_t].rtcp_info.octet_count += size;
    p_rua->channels[av_t].rtcp_info.packet_count++;
#endif

    return (slen == len) ? slen : -1;
}

/**
 * @brief
 *  Build video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
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

        ret = rtp_build(p_rua, AV_TYPE_VIDEO, data, len, ts, (len == size));
        if (ret < 0)
        {
            break;
        }

        data += len;
        size -= len;
    }

    return ret;
}

/**
 * @brief
 *  Judgment and segmentation h264/h265 rtp data into a single package
 *
 * @param fu_flag fragment flag
 * @param fu_s fragment start flag
 * @param fu_e fragment end flag
 * @param size data length
 *
 * @return the fragmention length
 *
 **/
int rtp_h26x_fu_split(int * fu_flag, int * fu_s, int * fu_e, int size)
{
    if (*fu_flag == 0) // have not yet begun fragment
    {
        if (size <= H26X_RTP_MAX_LEN) // Need not be fragmented
        {
            return size;
        }

        *fu_flag = 1;
        *fu_s = 1;
        *fu_e = 0;

        return H26X_RTP_MAX_LEN;
    }
    else // It has begun to fragment
    {
        *fu_s = 0;

        if (size <= H26X_RTP_MAX_LEN) // End fragmentation
        {
            *fu_e = 1;
            return size;
        }
        else
        {
            return H26X_RTP_MAX_LEN;
        }
    }
}

/**
 * @brief
 *  Send h264 data
 *
 * @param p_rua rtsp user agent
 * @param ts the packet timestamp
 * @param nalu_t the NALU type
 * @param fu_flag fragment flag
 * @param fu_s fragement start flag
 * @param fu_e fragement end flag
 * @param data payload data
 * @param size payload data length
 * @param last is it the last fragment package
 *
 * @return the rtp packet length, -1 on error
 *
 **/
int rtp_h264_tx(RSSUA * p_rua, uint32 ts, uint8 nalu_t, int fu_flag, int fu_s, int fu_e, uint8 * data, int size, int last)
{
    if (fu_flag)
    {
        if (fu_s == 1)
        {
            size--;
            data++;
        }

        size += 2;
        data -= 2;

        data[0] = (nalu_t & 0x60) | 28;
        data[1] = (nalu_t & 0x1F);

        if (fu_s == 1)
        {
            data[1] |= 0x80;
        }

        if (fu_e == 1)
        {
            data[1] |= 0x40;
        }
    }

    return rtp_build(p_rua, AV_TYPE_VIDEO, data, size, ts, last);
}

/**
 * @brief
 *  Build h264 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 * @param last is it the last fragment package
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h264_video_pkt_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts, int last)
{
    int ret = 1;
    int fu_s = 0, fu_e = 0, fu_flag = 0;    // Fragement start, end flag
    uint8 nalu_t = data[0];

#ifdef RTSP_REPLAY
    int keyframe = ((nalu_t & 0x1F) == H264_NAL_IDR);
    p_rua->channels[AV_VIDEO_CH].rep_hdr.c = keyframe;
#endif

    while (size > 0)
    {
        int tlen = rtp_h26x_fu_split(&fu_flag, &fu_s, &fu_e, size);

#ifdef RTSP_REPLAY
        if (p_rua->replay && 2 == p_rua->frame && 0 == keyframe) 
        {
            // only send I-frame
        }
        else
#endif
        {
            ret = rtp_h264_tx(p_rua, ts, nalu_t, fu_flag, fu_s, fu_e, data, tlen, fu_flag ? (fu_e ? last : 0) : last);
            if (ret == -1)
            {
                break;
            }
        }

        data += tlen;
        size -= tlen;
    }

    return ret;
}

/**
 * @brief
 *  Build h264 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h264_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
{
    int ret = 1;

    uint8 *r, *end = data + size;
    r = avc_find_startcode(data, end);

    while (r < end) 
    {
        uint8 *r1;

        while (!*(r++));
        r1 = avc_find_startcode(r, end);

        ret = rtp_h264_video_pkt_tx(p_rua, r, r1 - r, ts, r1 == end);
        if (ret < 0)
        {
            break;
        }

        r = r1;
    }

    return ret;
}

/**
 * @brief
 *  Send h265 rtp packet
 *
 * @param p_rua rtsp user agent
 * @param ts the packet timestamp
 * @param nalu_t the NALU type
 * @param fu_flag fragment flag
 * @param fu_s fragement start flag
 * @param fu_e fragement end flag
 * @param data payload data
 * @param size payload data length
 * @param last is it the last fragment package
 *
 * @return the rtp packet length, -1 on error
 *
 **/
int rtp_h265_tx(RSSUA * p_rua, uint32 ts, uint8 nalu_t, int fu_flag, int fu_s, int fu_e, uint8 * data, int size, int last)
{
    if (fu_flag)
    {
        if (fu_s == 1)
        {
            size -= 2;
            data += 2;
        }
        
        size += 3;
        data -= 3;

        data[0] = 49 << 1;  
        data[1] = 1;
        data[2] = nalu_t;

        if (fu_s == 1)
        {
            data[2] |= 1 << 7;
        }

        if (fu_e == 1)
        {
            data[2] |= 1 << 6;
        }
    }

    return rtp_build(p_rua, AV_TYPE_VIDEO, data, size, ts, last);
}

/**
 * @brief
 *  Build h265 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 * @param last is it the last fragment package
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h265_video_pkt_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts, int last)
{
    int ret = 1;
    int fu_s = 0, fu_e = 0, fu_flag = 0;    // Fragement start, end flag
    uint8 nalu_t = (data[0] >> 1) & 0x3F;

#ifdef RTSP_REPLAY
    int keyframe = IS_IRAP_NAL(nalu_t);
    p_rua->channels[AV_VIDEO_CH].rep_hdr.c = keyframe;
#endif

    while (size > 0)
    {
        int tlen = rtp_h26x_fu_split(&fu_flag, &fu_s, &fu_e, size);

#ifdef RTSP_REPLAY
        if (p_rua->replay && 2 == p_rua->frame && 0 == keyframe) 
        {
            // only send I-frame
        }
        else
#endif
        {
            ret = rtp_h265_tx(p_rua, ts, nalu_t, fu_flag, fu_s, fu_e, data, tlen, fu_flag ? (fu_e ? last : 0) : last);
            if (ret == -1)
            {
                break;
            }
        }

        data += tlen;
        size -= tlen;
    }

    return ret;
}

/**
 * @brief
 *  Build h265 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h265_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
{
    int ret = -1;

    uint8 *r, *end = data + size;
    r = avc_find_startcode(data, end);

    while (r < end) 
    {
        uint8 *r1;

        while (!*(r++));
        r1 = avc_find_startcode(r, end);

        ret = rtp_h265_video_pkt_tx(p_rua, r, r1 - r, ts, r1 == end);
        if (ret < 0)
        {
            break;
        }

        r = r1;
    }

    return ret;
}

/**
 * @brief
 *  Build jpeg video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return the send data length, -1 on error
 *
 **/
int rtp_jpeg_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
{
    int ret = 0;
    const uint8 *qtables[4] = { NULL };
    int nb_qtables = 0;
    uint8 type;
    uint8 w, h;
    uint8 *p;
    int off = 0; /* fragment offset of the current JPEG frame */
    int len;
    int i;
    int default_huffman_tables = 0;

    if (p_rua->media_info.v_info.width <= 2048 && 
        p_rua->media_info.v_info.height <= 2048)
    {
        /* convert video pixel dimensions from pixels to blocks */
        w = p_rua->media_info.v_info.width >> 3;
        h = p_rua->media_info.v_info.height >> 3;
    }
    else
    {
        // rfc2435, when using RTP to transmit JPEG packets, 
        //  the maximum width and height are 2048.
        //  If the width or height exceeds 2048, 
        //  use a=x-dimensions in the SDP to pass the video size
        
        w = h = 0;
    }
    
    type = 1; // YUVJ420P, YUVJ422P is 0

    /* preparse the header for getting some infos */
    for (i = 0; i < size; i++)
    {
        if (data[i] != 0xff)
        {
            continue;
        }

        if (data[i + 1] == MARKER_DQT) 
        {
            int tables, j;
            
            if (data[i + 4] & 0xF0)
            {
                log_print(HT_LOG_WARN, "%s, Only 8-bit precision is supported\r\n", __FUNCTION__);
            }

            /* a quantization table is 64 bytes long */
            tables = net_read_uint16(&data[i + 2]) / 65;
            if (i + 5 + tables * 65 > size)
            {      
                log_print(HT_LOG_ERR, "%s, Too short JPEG header. Aborted!\r\n", __FUNCTION__);
                return -1;
            }
            
            if (nb_qtables + tables > 4) 
            {       
                log_print(HT_LOG_ERR, "%s, Invalid number of quantisation tables\r\n", __FUNCTION__);
                return -1;
            }

            for (j = 0; j < tables; j++)
            {
                qtables[nb_qtables + j] = data + i + 5 + j * 65;
            }    
            nb_qtables += tables;
        } 
        else if (data[i + 1] == MARKER_SOF0) 
        {
            if (data[i + 14] != 17 || data[i + 17] != 17)
            {
                log_print(HT_LOG_ERR, "%s, Only 1x1 chroma blocks are supported. Aborted!\r\n", __FUNCTION__);
                return -1;
            }
        } 
        else if (data[i + 1] == MARKER_DHT) 
        {
            int dht_size = net_read_uint16(&data[i + 2]);
            default_huffman_tables |= 1 << 4;
            i += 3;
            dht_size -= 2;
            if (i + dht_size >= size)
            {
                continue;
            }
            
            while (dht_size > 0)
            {
                switch (data[i + 1]) 
                {
                case 0x00:
                    if (   dht_size >= 29
                        && !memcmp(data + i +  2, lum_dc_codelens, 16)
                        && !memcmp(data + i + 18, lum_dc_symbols, 12))
                    {
                        default_huffman_tables |= 1;
                        i += 29;
                        dht_size -= 29;
                    } 
                    else 
                    {
                        i += dht_size;
                        dht_size = 0;
                    }
                    break;
                    
                case 0x01:
                    if (   dht_size >= 29
                        && !memcmp(data + i +  2, chm_dc_codelens, 16)
                        && !memcmp(data + i + 18, lum_dc_symbols, 12)) 
                    {
                        default_huffman_tables |= 1 << 1;
                        i += 29;
                        dht_size -= 29;
                    }
                    else 
                    {
                        i += dht_size;
                        dht_size = 0;
                    }
                    break;
                    
                case 0x10:
                    if (   dht_size >= 179
                        && !memcmp(data + i +  2, lum_ac_codelens, 16)
                        && !memcmp(data + i + 18, lum_ac_symbols, 162)) 
                    {
                        default_huffman_tables |= 1 << 2;
                        i += 179;
                        dht_size -= 179;
                    }
                    else 
                    {
                        i += dht_size;
                        dht_size = 0;
                    }
                    break;
                    
                case 0x11:
                    if (   dht_size >= 179
                        && !memcmp(data + i +  2, chm_ac_codelens, 16)
                        && !memcmp(data + i + 18, chm_ac_symbols, 162))
                    {
                        default_huffman_tables |= 1 << 3;
                        i += 179;
                        dht_size -= 179;
                    } 
                    else 
                    {
                        i += dht_size;
                        dht_size = 0;
                    }
                    break;
                    
                default:
                    i += dht_size;
                    dht_size = 0;
                    continue;
                }
            }
        } 
        else if (data[i + 1] == MARKER_SOS)
        {
            /* SOS is last marker in the header */
            i += net_read_uint16(&data[i + 2]) + 2;
            if (i > size) 
            {
                log_print(HT_LOG_ERR, "%s, Insufficient data. Aborted!\r\n", __FUNCTION__);
                return -1;
            }
            break;
        }
    }
    
    if (default_huffman_tables && default_huffman_tables != 31) 
    {
        log_print(HT_LOG_ERR, "%s, RFC 2435 requires standard Huffman tables for jpeg\r\n", __FUNCTION__);
        return -1;
    }
    
    if (nb_qtables && nb_qtables != 2)
    {
        // log_print(HT_LOG_INFO, "%s, RFC 2435 suggests two quantization tables, %d provided\r\n", __FUNCTION__, nb_qtables);
    }

    /* skip JPEG header */
    data += i;
    size -= i;

    for (i = size - 2; i >= 0; i--) 
    {
        if (data[i] == 0xff && data[i + 1] == MARKER_EOI) 
        {
            /* Remove the EOI marker */
            size = i;
            break;
        }
    }

    uint8 buff[2048];    
    p = buff + RTSP_RESV_HDR_SIZE;
    
    while (size > 0) 
    {
        int hdr_size = 8;
        
        if (off == 0 && nb_qtables)
        {
            hdr_size += 4 + 64 * nb_qtables;
        }

        /* payload max in one packet */
        len = MIN(size, JPEG_RTP_MAX_LEN - hdr_size);

        /* set main header */
        *p++ = 0;
        *p++ = (off >> 16);
        *p++ = (off >> 8);
        *p++ = off;
        *p++ = type;
        *p++ = 255;
        *p++ = w;
        *p++ = h;

        if (off == 0 && nb_qtables)
        {
            /* set quantization tables header */
            *p++ = 0;
            *p++ = 0;
            *p++ = ((64 * nb_qtables) >> 8);
            *p++ = 64 * nb_qtables;

            for (i = 0; i < nb_qtables; i++)
            {
                memcpy(p, qtables[i], 64);
                p += 64;
            }
        }

        /* copy payload data */
        memcpy(p, data, len);

        /* marker bit is last packet in frame */
        ret = rtp_build(p_rua, AV_TYPE_VIDEO, buff + RTSP_RESV_HDR_SIZE, len + hdr_size, ts, size == len);
        if (ret < 0)
        {
            return -1;
        }
 
        data += len;
        size -= len;
        off += len;
        p = buff + RTSP_RESV_HDR_SIZE;
    }

    return off;
}

/**
 * @brief
 *  Build audio rtp packet and send (not fragment)
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_audio_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
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

        ret = rtp_build(p_rua, AV_TYPE_AUDIO, data, len, ts, (len == size));
        if (ret < 0)
        {
            break;
        }

        data += len;
        size -= len;
    }

    return ret;
}

/**
 * @brief
 *  Build AAC audio rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_aac_audio_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
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
        
        ret = rtp_build(p_rua, AV_TYPE_AUDIO, p, len + 4, ts, len == size);
        if (ret < 0)
        {
            break;
        }
        
        size -= len;
        p += len;
    }

    return ret;
}

#ifdef RTSP_METADATA

/**
 * @brief
 *  Build metadata data rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_metadata_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts)
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

        ret = rtp_build(p_rua, AV_TYPE_METADATA, data, len, ts, (len == size));
        if (ret < 0)
        {
            break;
        }

        data += len;
        size -= len;
    }

    return ret;
}

#endif



