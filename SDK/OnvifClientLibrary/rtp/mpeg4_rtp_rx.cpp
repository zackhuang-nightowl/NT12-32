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
#include "mpeg4_rtp_rx.h"


BOOL mpeg4_data_rx(MPEG4RXI * p_rxi, uint8 * p_data, int len)
{
    BOOL begin_packet = FALSE;
    
    // The packet begins a frame iff its data begins with a system code 
    // (i.e., 0x000001??)  
    begin_packet = (len >= 4 && p_data[0] == 0 && p_data[1] == 0 && p_data[2] == 1);

    if (begin_packet)
    {
        p_rxi->offset = 0;
    }

    if (p_rxi->rtprxi.rxf_loss)
    {
        // Packet loss, discard the previously cached packets
        p_rxi->offset = 0;

        // Packet loss, until the next start packet
        
        if (!begin_packet)
        {
            return FALSE;
        }
        else
        {
            p_rxi->rtprxi.rxf_loss = 0;
        }
    }
    
    if ((p_rxi->offset + len + p_rxi->hdr_len) >= p_rxi->buf_len)
    {
        log_print(HT_LOG_ERR, "%s, fragment packet too big %d!!!", 
            __FUNCTION__, p_rxi->offset + len + p_rxi->hdr_len);
        return FALSE;
    }

    memcpy(p_rxi->buf + p_rxi->offset + p_rxi->hdr_len, p_data, len);
    p_rxi->offset += len;
    
    if (p_rxi->rtprxi.rxf_marker)
    {
        if (p_rxi->pkt_func)
        {
            p_rxi->pkt_func(p_rxi->buf, p_rxi->offset + p_rxi->hdr_len, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
        }

        p_rxi->offset = 0;
    }

    return TRUE;
}

BOOL mpeg4_rtp_rx(MPEG4RXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi == NULL)
    {
        return FALSE;
    }
    
    if (!rtp_data_rx(&p_rxi->rtprxi, p_data, len))
    {
        return FALSE;
    }

    return mpeg4_data_rx(p_rxi, p_rxi->rtprxi.p_data, p_rxi->rtprxi.len);
}

BOOL mpeg4_rxi_init(MPEG4RXI * p_rxi, VRTPRXCBF cbf, void * p_userdata)
{
    memset(p_rxi, 0, sizeof(MPEG4RXI));
    
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

void mpeg4_rxi_deinit(MPEG4RXI * p_rxi)
{
    if (p_rxi->buf_org)
    {
        free(p_rxi->buf_org);
    }
    
    memset(p_rxi, 0, sizeof(MPEG4RXI));
}





