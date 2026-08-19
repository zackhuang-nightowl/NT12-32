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
#include "ws.h"
#include "net_util.h"


HT_API int ws_decode_data(WSMSG * p_msg)
{
    int length, has_mask;
    char * p_buff = p_msg->buff;
    
    /**
     * Extracting information from frame
     */
    has_mask = p_buff[1] & 0x80 ? 1 : 0;
    length = p_buff[1] & 0x7f;

    p_msg->opcode = (p_buff[0] & 0xF);
    p_msg->finbit = ((p_buff[0] & 0x80) ? 1 : 0);

    /**
     * We need to handle the received frame differently according to which
     * length that the frame has set.
     *
     * length <= 125: We know that length is the actual length of the message,
     *                   and that the maskin data must be placed 2 bytes further 
     *                   ahead.
     * length == 126: We know that the length is an unsigned 16 bit integer,
     *                   which is placed at the 2 next bytes, and that the masking
     *                   data must be further 2 bytes away.
     * length == 127: We know that the length is an unsigned 64 bit integer,
     *                   which is placed at the 8 next bytes, and that the masking
     *                   data must be further 2 bytes away.
     */
    if (length <= 125) 
    {
        if (p_msg->rcv_len < 6)
        {
            return 0;
        }
        
        p_msg->msg_len = length;    
        p_msg->skip = 2;

        if (has_mask)
        {
            p_msg->skip += 4;
            memcpy(&p_msg->mask, p_buff + 2, sizeof(p_msg->mask));
        }
    }
    else if (length == 126) 
    {
        if (p_msg->rcv_len < 8)
        {
            return 0;
        }
        
        p_msg->msg_len = net_read_uint16((uint8 *)p_buff + 2);
        p_msg->skip = 4;

        if (has_mask)
        {
            p_msg->skip += 4;
            memcpy(&p_msg->mask, p_buff + 4, sizeof(p_msg->mask));
        }
    } 
    else if (length == 127) 
    {
        if (p_msg->rcv_len < 14)
        {
            return 0;
        }

        p_msg->msg_len = (uint32)net_read_uint64((uint8 *)p_buff + 2);
        p_msg->skip = 10;

        if (has_mask)
        {
            p_msg->skip += 4;
            memcpy(&p_msg->mask, p_buff + 10, sizeof(p_msg->mask));
        }
    } 
    else 
    {
        log_print(HT_LOG_ERR, "%s, Obscure length received from client: %d\r\n", __FUNCTION__, length);
        return -1;    
    }

    /**
     * If the message length is greater that our WS_MAXMESSAGE constant, we
     * skip the message and close the connection.
     */
    if (p_msg->msg_len >= WS_MAXMESSAGE) 
    {
        log_print(HT_LOG_ERR, "%s, Message received was bigger than WS_MAXMESSAGE\r\n", __FUNCTION__);
        return -1;
    }

    uint32 buf_len = (p_msg->rcv_len - p_msg->skip);

    /**
     * The message read from recv is larger than the message we are supposed
     * to receive. This means that we have received the first part of the next
     * message as well.
     */
    if (buf_len >= p_msg->msg_len) 
    {
        if (has_mask)
        {
            uint32 i;
            char * buff = p_msg->buff + p_msg->skip;
            
            /**
             * If everything went well, we have to remove the masking from the data.
             */
            for (i = 0; i < p_msg->msg_len; i++)
            {
                buff[i] = buff[i] ^ p_msg->mask[i % 4];
            }
        }
    
        return 1;
    }
    
    return 0;
}

/***
 * websocket encod data, add websocket header before p_data
 * The p_data pointer must reserve a certain amount of space
 */
HT_API int ws_encode_data(uint8 * p_data, int len, uint8 opcode, uint8 have_mask)
{
    int offset = 0;
    uint8 mask[4];

    if (have_mask)
    {
        int i;
        
        mask[0] = rand();
        mask[1] = rand();
        mask[2] = rand();
        mask[3] = rand();

        for (i = 0; i < len; i++)
        {
            p_data[i] = p_data[i] ^ mask[i % 4];
        }
    }
        
    /**
     * RFC6455 message encoding
     */
    if (len <= 125) 
    {
        offset = have_mask ? 6 : 2;
        p_data -= offset;
        p_data[0] = 0x80 | opcode;
        p_data[1] = have_mask ? (len | 0x80) : len;
        if (have_mask)
        {
            memcpy(p_data+2, mask, 4);
        }
    } 
    else if (len <= 65535) 
    {
        offset = have_mask ? 8 : 4;
        p_data -= offset;
        p_data[0] = 0x80 | opcode;
        p_data[1] = have_mask ? (126 | 0x80) : 126;
        net_write_uint16(p_data + 2, len);
        if (have_mask)
        {
            memcpy(p_data+4, mask, 4);
        }
    }
    else 
    {
        uint64 len64 = len;
        offset = have_mask ? 14 : 10;
        p_data -= offset;
        p_data[0] = 0x80 | opcode;
        p_data[1] = have_mask ? (127 | 0x80) : 127;
        net_write_uint32(p_data + 2, len64 & 0xFFFF);
        net_write_uint32(p_data + 6, (len64 >> 32) & 0xFFFF);
        if (have_mask)
        {
            memcpy(p_data+10, mask, 4);
        }
    }

    return offset;
}


