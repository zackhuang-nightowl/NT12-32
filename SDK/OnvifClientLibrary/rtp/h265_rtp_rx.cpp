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
#include "h265_rtp_rx.h"


/****************************************************************************/

void h265_save_parameters(H265RXI * p_rxi, uint8 * p_data, uint32 len)
{
    if (p_rxi->param_sets.vps_len > 0 && p_rxi->param_sets.sps_len > 0 && p_rxi->param_sets.pps_len > 0)
    {
        return;
    }
    
    uint8 nal_type = ((p_data[0] >> 1) & 0x3f);
                    
    if (nal_type == HEVC_NAL_VPS && p_rxi->param_sets.vps_len == 0)
    {
        if (len <= sizeof(p_rxi->param_sets.vps) - 4)
        {
            int offset = 0;
            
            p_rxi->param_sets.vps[0] = 0;
            p_rxi->param_sets.vps[1] = 0;
            p_rxi->param_sets.vps[2] = 0;
            p_rxi->param_sets.vps[3] = 1;

            offset = 4;
            
            memcpy(p_rxi->param_sets.vps+offset, p_data, len);
            p_rxi->param_sets.vps_len = len+offset;
        }
    }
    else if (nal_type == HEVC_NAL_SPS && p_rxi->param_sets.sps_len == 0)
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
    else if (nal_type == HEVC_NAL_PPS && p_rxi->param_sets.pps_len == 0)
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

BOOL h265_handle_aggregated_packet(H265RXI * p_rxi, uint8 * p_data, int len)
{
    uint8 *src  = p_data;
    int src_len = len;
    int skip_between = p_rxi->rxf_don ? 1 : 0;

    while (src_len > 2) 
    {
        uint16 nal_size = ((src[0] << 8) | src[1]);

        // consume the length of the aggregate
        src     += 2;
        src_len -= 2;

        if (nal_size <= src_len) 
        {
            h265_save_parameters(p_rxi, src, nal_size);

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
        src     += nal_size + skip_between;
        src_len -= nal_size + skip_between;
    }

    if (p_rxi->pkt_func)
    {
        p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
    }

    p_rxi->offset = 0;
        
    return TRUE;
}

BOOL h265_data_rx(H265RXI * p_rxi, uint8 * p_data, int len)
{
    uint8 * headerStart = p_data;
    uint32  packetSize = len;
    uint32  numBytesToSkip;
    uint8   naluType;    
    BOOL    beginPacket = FALSE;
    BOOL    endPacket = FALSE;

    if (packetSize < 2)
    {
        return FALSE;
    }

    if (p_rxi->rtprxi.rxf_loss)
    {
        // Packet loss, discard the previously cached packets
        p_rxi->offset = 0;
    }
    
    naluType = (headerStart[0] & 0x7E) >> 1;
        
    switch (naluType) 
    {
    case 48: 
    { 
        // Aggregation Packet (AP)
        // We skip over the 2-byte Payload Header, and the DONL header (if any).
        if (p_rxi->rxf_don) 
        {
            if (packetSize < 4)
            {
                return FALSE;
            }
            
            numBytesToSkip = 4;
        } 
        else
        {
            numBytesToSkip = 2;
        }

        return h265_handle_aggregated_packet(p_rxi, p_data+numBytesToSkip, len-numBytesToSkip);
    }
        
    case 49: 
    { 
        // Fragmentation Unit (FU)
        // This NALU begins with the 2-byte Payload Header, the 1-byte FU header, and (optionally)
        // the 2-byte DONL header.
        // If the start bit is set, we reconstruct the original NAL header at the end of these
        // 3 (or 5) bytes, and skip over the first 1 (or 3) bytes.

        if (packetSize < 3) 
        {
            return FALSE;
        }
        
        uint8 startBit = headerStart[2] & 0x80; // from the FU header
        uint8 endBit = headerStart[2] & 0x40;   // from the FU header
        if (startBit)
        {
            beginPacket = TRUE;

            uint8 nal_unit_type = headerStart[2] & 0x3F; // the last 6 bits of the FU header
            uint8 newNALHeader[2];
            
            newNALHeader[0] = (headerStart[0] & 0x81) | (nal_unit_type << 1);
            newNALHeader[1] = headerStart[1];

            if (p_rxi->rxf_don) 
            {
                if (packetSize < 5)
                {
                    return FALSE;
                }
                
                headerStart[3] = newNALHeader[0];
                headerStart[4] = newNALHeader[1];
                numBytesToSkip = 3;
            }
            else 
            {
                headerStart[1] = newNALHeader[0];
                headerStart[2] = newNALHeader[1];
                numBytesToSkip = 1;
            }
        } 
        else 
        {
            // The start bit is not set, so we skip over all headers:
            beginPacket = FALSE;
            
            if (p_rxi->rxf_don) 
            {
                if (packetSize < 5)
                {
                    return FALSE;
                }
                
                numBytesToSkip = 5;
            }
            else 
            {
                numBytesToSkip = 3;
            }
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
    
    // save the H265 parameter sets

    h265_save_parameters(p_rxi, headerStart + numBytesToSkip, packetSize - numBytesToSkip);
    
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

BOOL h265_rtp_rx(H265RXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi == NULL)
    {
        return FALSE;
    }
    
    if (!rtp_data_rx(&p_rxi->rtprxi, p_data, len))
    {
        return FALSE;
    }

    return h265_data_rx(p_rxi, p_rxi->rtprxi.p_data, p_rxi->rtprxi.len);
}

BOOL h265_rxi_init(H265RXI * p_rxi, VRTPRXCBF cbf, void * p_userdata)
{
    memset(p_rxi, 0, sizeof(H265RXI));
    
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

void h265_rxi_deinit(H265RXI * p_rxi)
{
    if (p_rxi->buf_org)
    {
        free(p_rxi->buf_org);
    }

    memset(p_rxi, 0, sizeof(H265RXI));
}



