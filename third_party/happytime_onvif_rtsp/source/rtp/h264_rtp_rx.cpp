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
#include "h264_rtp_rx.h"


/****************************************************************************/

void h264_save_parameters(H264RXI * p_rxi, uint8 * p_data, uint32 len)
{
    if (p_rxi->param_sets.sps_len > 0 && p_rxi->param_sets.pps_len > 0)
    {
        return;
    }
    
    uint8 nal_type = (p_data[0] & 0x1F);
    
    if (nal_type == H264_NAL_SPS && p_rxi->param_sets.sps_len == 0)
    {
        if (len <= sizeof(p_rxi->param_sets.sps) - 4)
        {
            int offset = 0;
            
            p_rxi->param_sets.sps[0] = 0;
            p_rxi->param_sets.sps[1] = 0;
            p_rxi->param_sets.sps[2] = 0;
            p_rxi->param_sets.sps[3] = 1;

            offset = 4;
            
            memcpy(p_rxi->param_sets.sps+offset, p_data, len);
            p_rxi->param_sets.sps_len = len+offset;
        }
    }
    else if (nal_type == H264_NAL_PPS && p_rxi->param_sets.pps_len == 0)
    {
        if (len <= sizeof(p_rxi->param_sets.pps) - 4)
        {
            int offset = 0;
            
            p_rxi->param_sets.pps[0] = 0;
            p_rxi->param_sets.pps[1] = 0;
            p_rxi->param_sets.pps[2] = 0;
            p_rxi->param_sets.pps[3] = 1;

            offset = 4;
            
            memcpy(p_rxi->param_sets.pps+offset, p_data, len);
            p_rxi->param_sets.pps_len = len+offset;
        }
    }
}

BOOL h264_handle_aggregated_packet(H264RXI * p_rxi, uint8 * p_data, int len)
{
    uint8 *src  = p_data;
    int src_len = len;

    while (src_len > 2) 
    {
        uint16 nal_size = ((src[0] << 8) | src[1]);

        // consume the length of the aggregate
        src     += 2;
        src_len -= 2;

        if (nal_size <= src_len) 
        {
            h264_save_parameters(p_rxi, src, nal_size);

            if (p_rxi->offset + nal_size + 4 >= p_rxi->buf_len)
            {
                if (p_rxi->rtprxi.rxf_marker)
                {
                    p_rxi->offset = 0;
                }
                
                log_print(HT_LOG_ERR, "%s, packet too big %d!!!", __FUNCTION__, p_rxi->offset + nal_size + 4);
                return FALSE;
            }
            else
            {
                p_rxi->buf[p_rxi->offset+0] = 0;
                p_rxi->buf[p_rxi->offset+1] = 0;
                p_rxi->buf[p_rxi->offset+2] = 0;
                p_rxi->buf[p_rxi->offset+3] = 1;
                p_rxi->offset += 4;
                
                // copying
                memcpy(p_rxi->buf + p_rxi->offset, src, nal_size);

                p_rxi->offset += nal_size;
            }
        } 
        else 
        {
            log_print(HT_LOG_ERR, "%s, nal size exceeds length: %d %d\n", __FUNCTION__, nal_size, src_len);
            return FALSE;
        }

        // eat what we handled
        src     += nal_size;
        src_len -= nal_size;
    }

    if (p_rxi->pkt_func)
    {
        p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
    }

    p_rxi->offset = 0;
        
    return TRUE;
}

BOOL h264_data_rx(H264RXI * p_rxi, uint8 * p_data, int len)
{
    uint8 * headerStart = p_data;
    uint32  packetSize = len;
    uint32  numBytesToSkip;
    uint8   naluType;    
    BOOL    beginPacket = FALSE;
    BOOL    endPacket = FALSE;

    if (packetSize < 1)
    {
        return FALSE;
    }

    if (p_rxi->rtprxi.rxf_loss)
    {
        // Packet loss, discard the previously cached packets
        p_rxi->offset = 0;
    }
    
    naluType = (headerStart[0] & 0x1F);
        
    switch (naluType) 
    {
    case 24:                    // STAP-A
        numBytesToSkip = 1;     // discard the type byte
        return h264_handle_aggregated_packet(p_rxi, p_data+numBytesToSkip, len-numBytesToSkip);
    
    case 25: case 26: case 27:  // STAP-B, MTAP16, or MTAP24
        numBytesToSkip = 3;     // discard the type byte, and the initial DON
        break;
        
    case 28: case 29:           // FU-A or FU-B
    { 
        // For these NALUs, the first two bytes are the FU indicator and the FU header.
        // If the start bit is set, we reconstruct the original NAL header into byte 1
        
        if (packetSize < 2) 
        {
            return FALSE;
        }
        
        uint8 startBit = headerStart[1] & 0x80;
        uint8 endBit = headerStart[1] & 0x40;
        if (startBit) 
        {
            beginPacket = TRUE;

            headerStart[1] = (headerStart[0] & 0xE0) | (headerStart[1] & 0x1F);
            numBytesToSkip = 1;
        } 
        else 
        {
            // The start bit is not set, so we skip both the FU indicator and header:
            beginPacket = FALSE;
            numBytesToSkip = 2;
        }
        
        endPacket = (endBit != 0);
        break;
    }
    
    default:
        // This packet contains one complete NAL unit:
        beginPacket = endPacket = TRUE;
        numBytesToSkip = 0;
        break;
    }

    if (p_rxi->rtprxi.rxf_loss)
    {
        // Packet loss, until the next start packet
        
        if (!beginPacket)
        {
            return FALSE;
        }
        else
        {
            p_rxi->rtprxi.rxf_loss = 0;
        }
    }
    
    // save the H264 parameter sets

    h264_save_parameters(p_rxi, headerStart + numBytesToSkip, packetSize - numBytesToSkip);
    
    if ((p_rxi->offset + 4 + packetSize - numBytesToSkip) >= p_rxi->buf_len)
    {
        if (p_rxi->rtprxi.rxf_marker)
        {
            p_rxi->offset = 0;
        }
        
        log_print(HT_LOG_ERR, "%s, fragment packet too big %d!!!", 
            __FUNCTION__, p_rxi->offset + 4 + packetSize - numBytesToSkip);
        return FALSE;
    }

    if (beginPacket)
    {
        p_rxi->buf[p_rxi->offset+0] = 0;
        p_rxi->buf[p_rxi->offset+1] = 0;
        p_rxi->buf[p_rxi->offset+2] = 0;
        p_rxi->buf[p_rxi->offset+3] = 1;
        p_rxi->offset += 4;
    }
    
    memcpy(p_rxi->buf + p_rxi->offset, headerStart + numBytesToSkip, packetSize - numBytesToSkip);
    p_rxi->offset += packetSize - numBytesToSkip;

    if (endPacket && p_rxi->rtprxi.rxf_marker)
    {
        if (p_rxi->pkt_func)
        {
            p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
        }

        p_rxi->offset = 0;
    }

    return TRUE;
}

BOOL h264_rtp_rx(H264RXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi == NULL)
    {
        return FALSE;
    }
    
    if (!rtp_data_rx(&p_rxi->rtprxi, p_data, len))
    {
        return FALSE;
    }

    return h264_data_rx(p_rxi, p_rxi->rtprxi.p_data, p_rxi->rtprxi.len);
}

BOOL h264_rxi_init(H264RXI * p_rxi, VRTPRXCBF cbf, void * p_userdata)
{
    memset(p_rxi, 0, sizeof(H264RXI));
    
    p_rxi->buf_org = (uint8 *)malloc(RTP_MAX_VIDEO_BUFF);
    if (p_rxi->buf_org == NULL)
    {
        return FALSE;
    }

    p_rxi->buf = p_rxi->buf_org + 32;
    p_rxi->buf_len = RTP_MAX_VIDEO_BUFF - 32;
    p_rxi->pkt_func = cbf;
    p_rxi->user_data = p_userdata;

    return TRUE;
}

void h264_rxi_deinit(H264RXI * p_rxi)
{
    if (p_rxi->buf_org)
    {
        free(p_rxi->buf_org);
    }

    memset(p_rxi, 0, sizeof(H264RXI));
}



