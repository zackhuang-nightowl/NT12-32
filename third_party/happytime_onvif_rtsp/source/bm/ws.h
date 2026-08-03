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

#ifndef WS_H
#define WS_H

#include "sys_inc.h"

/**************************************************************************************/

#define WS_VERSION          13                  // websocket version
#define WS_MAXMESSAGE       1048576             // Max size message = 1MB

#define WS_OPCODE_TEXT      0x1
#define WS_OPCODE_BINARY    0x2
#define WS_OPCODE_CLOSE     0x8
#define WS_OPCODE_PING      0x9
#define WS_OPCODE_PONG      0xA

/**************************************************************************************/

typedef struct 
{
    uint32 finbit   : 1;
    uint32 opcode   : 4;
    uint32 reserved : 27;
    
    char   mask[4];
    int    skip;
    uint32 buff_len;
    uint32 rcv_len;
    uint32 msg_len;
    char * buff;
} WSMSG;

#ifdef __cplusplus
extern "C" {
#endif

HT_API int  ws_decode_data(WSMSG * p_msg);
HT_API int  ws_encode_data(uint8 * p_data, int len, uint8 opcode, uint8 have_mask);

#ifdef __cplusplus
}
#endif

#endif



