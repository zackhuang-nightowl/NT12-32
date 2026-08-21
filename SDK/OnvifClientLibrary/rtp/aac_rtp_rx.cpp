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
#include "aac_rtp_rx.h"
#include "bs.h"


BOOL aac_data_rx(AACRXI * p_rxi, uint8 * p_data, int len)
{
    struct AUHeader 
    {
        uint32 size;
        uint32 index; // indexDelta for the 2nd & subsequent headers
    };

    uint8* headerStart = p_data;
    uint32 packetSize = len;
    uint32 resultSpecialHeaderSize;

    uint32 fNumAUHeaders; // in the most recently read packet
    struct AUHeader* fAUHeaders;

    // default values:
    resultSpecialHeaderSize = 0;
    fNumAUHeaders = 0;
    fAUHeaders = NULL;

    if (p_rxi->size_length == 0) 
    {
        return FALSE;
    }
    
    // The packet begins with a "AU Header Section".  Parse it, to
    // determine the "AU-header"s for each frame present in this packet
    resultSpecialHeaderSize += 2;
    if (packetSize < resultSpecialHeaderSize)
    {
        return FALSE;
    }

    uint32 AU_headers_length = (headerStart[0] << 8) | headerStart[1];
    uint32 AU_headers_length_bytes = (AU_headers_length + 7) / 8;
    if (packetSize < resultSpecialHeaderSize + AU_headers_length_bytes)
    {
        return FALSE;
    }
    resultSpecialHeaderSize += AU_headers_length_bytes;

    // Figure out how many AU-headers are present in the packet
    int bitsAvail = AU_headers_length - (p_rxi->size_length + p_rxi->index_length);
    if (bitsAvail >= 0 && (p_rxi->size_length + p_rxi->index_delta_length) > 0)
    {
        fNumAUHeaders = 1 + bitsAvail / (p_rxi->size_length + p_rxi->index_delta_length);
    }
    
    if (fNumAUHeaders > 0) 
    {
        fAUHeaders = (struct AUHeader*) malloc(sizeof(AUHeader) * fNumAUHeaders);
        if (NULL == fAUHeaders)
        {
            return FALSE;
        }
        
        // Fill in each header:
        bs_t bs;
        bs_init(&bs, &headerStart[2], AU_headers_length);

        fAUHeaders[0].size = bs_read(&bs, p_rxi->size_length);
        fAUHeaders[0].index = bs_read(&bs, p_rxi->index_length);

        for (uint32 i = 1; i < fNumAUHeaders; ++i) 
        {
            fAUHeaders[i].size = bs_read(&bs, p_rxi->size_length);
            fAUHeaders[i].index = bs_read(&bs, p_rxi->index_delta_length);
        }
    }

    p_data += resultSpecialHeaderSize;
    len -= resultSpecialHeaderSize;
    
    if (fNumAUHeaders == 1 && (uint32) len < fAUHeaders[0].size) 
    {
        if (fAUHeaders[0].size > p_rxi->buf_len) 
        {
            if (fAUHeaders)
            {
                free(fAUHeaders);
            }
                
            return FALSE;
        }

        if (p_rxi->offset + len > p_rxi->buf_len)
        {
            if (fAUHeaders)
            {
                free(fAUHeaders);
            }
            
            log_print(HT_LOG_ERR, "%s, buffer overflow! offset=%d, len=%d, buf_len=%d\r\n", 
                __FUNCTION__, p_rxi->offset, len, p_rxi->buf_len);
            return FALSE;
        }

        memcpy(p_rxi->buf + p_rxi->offset, p_data, len);
        p_rxi->offset += len;

        if (p_rxi->rtprxi.rxf_marker)
        {
            if (p_rxi->offset != fAUHeaders[0].size) 
            {
                if (fAUHeaders)
                {
                    free(fAUHeaders);
                }

                p_rxi->offset = 0;
                return FALSE;
            }

            if (p_rxi->pkt_func)
            {
                p_rxi->pkt_func(p_rxi->buf, p_rxi->offset, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
            }
        }
    }
    else
    {
        for (uint32 i = 0; i < fNumAUHeaders; i++)
        {
            if (fAUHeaders[i].size > (uint32) len) 
            {
                if (fAUHeaders)
                {
                    free(fAUHeaders);
                }
    
                return FALSE;
            }

            if (fAUHeaders[i].size > p_rxi->buf_len)
            {
                log_print(HT_LOG_ERR, "%s, buffer overflow! len=%d, buf_len=%d\r\n", 
                    __FUNCTION__, fAUHeaders[i].size, p_rxi->buf_len);

                if (fAUHeaders)
                {
                    free(fAUHeaders);
                }
                
                return FALSE;
            }

            memcpy(p_rxi->buf, p_data, fAUHeaders[i].size);
            p_data += fAUHeaders[i].size;
            len -= fAUHeaders[i].size;

            if (p_rxi->pkt_func)
            {
                p_rxi->pkt_func(p_rxi->buf, fAUHeaders[i].size, p_rxi->rtprxi.ts, p_rxi->rtprxi.seq, p_rxi->user_data);
            }
        }
    }

    if (fAUHeaders)
    {
        free(fAUHeaders);
    }

    return TRUE;
}

BOOL aac_rtp_rx(AACRXI * p_rxi, uint8 * p_data, int len)
{
    if (p_rxi == NULL)
    {
        return FALSE;
    }
    
    if (!rtp_data_rx(&p_rxi->rtprxi, p_data, len))
    {
        return FALSE;
    }

    return aac_data_rx(p_rxi, p_rxi->rtprxi.p_data, p_rxi->rtprxi.len);
}

BOOL aac_rxi_init(AACRXI * p_rxi, VRTPRXCBF cbf, void * p_userdata)
{
    memset(p_rxi, 0, sizeof(AACRXI));
    
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

void aac_rxi_deinit(AACRXI * p_rxi)
{
    if (p_rxi->buf_org)
    {
        free(p_rxi->buf_org);
    }
    
    memset(p_rxi, 0, sizeof(AACRXI));
}



