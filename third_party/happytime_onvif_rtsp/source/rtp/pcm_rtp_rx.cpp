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
#include "pcm_rtp_rx.h"


BOOL pcm_data_rx(PCMRXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi->rtprxi.rxf_loss)
    {
        // Submit cached data
        
        if (p_rxi->pkt_func && p_rxi->offset > 0)
        {
            p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
        }
          
        p_rxi->offset = 0;
        p_rxi->rtprxi.rxf_loss = 0;
    }
    
    if ((p_rxi->offset + len) >= p_rxi->buf_len)
    {
        // Submit cached data
        
        if (p_rxi->pkt_func)
        {
            p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
        }
          
        p_rxi->offset = 0;
    }

    if ((p_rxi->offset + len) > p_rxi->buf_len)
    {
        log_print(HT_LOG_ERR, "%s, buffer overflow! offset=%d, len=%d, buf_len=%d\r\n", 
            __FUNCTION__, p_rxi->offset, len, p_rxi->buf_len);
        return FALSE;
    }
    
    memcpy(p_rxi->buf + p_rxi->offset, p_data, len);
    p_rxi->offset += len;
    
    if (p_rxi->rtprxi.rxf_marker)
    {
        if (p_rxi->pkt_func)
        {
            p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
        }

        p_rxi->offset = 0;
    }

    return TRUE;
}

BOOL pcm_rtp_rx(PCMRXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi == NULL)
    {
        return FALSE;
    }
    
    if (!rtp_data_rx(&p_rxi->rtprxi, p_data, len))
    {
        return FALSE;
    }

    return pcm_data_rx(p_rxi, p_rxi->rtprxi.p_data, p_rxi->rtprxi.len);
}

BOOL pcm_rxi_init(PCMRXI * p_rxi, VRTPRXCBF cbf, void * p_userdata)
{
    memset(p_rxi, 0, sizeof(PCMRXI));
    
    p_rxi->buf_org = (uint8 *)malloc(RTP_MAX_AUDIO_BUFF);
    if (p_rxi->buf_org == NULL)
    {
        return FALSE;
    }
    
    p_rxi->buf = p_rxi->buf_org + 32;
    p_rxi->buf_len = RTP_MAX_AUDIO_BUFF - 32;
    p_rxi->pkt_func = cbf;
    p_rxi->user_data = p_userdata;

    return TRUE;
}

void pcm_rxi_deinit(PCMRXI * p_rxi)
{
    if (p_rxi->buf_org)
    {
        free(p_rxi->buf_org);
    }
    
    memset(p_rxi, 0, sizeof(PCMRXI));
}



