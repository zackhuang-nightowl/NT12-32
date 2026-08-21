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

#ifndef __H_RTP_H__
#define __H_RTP_H__

#include "sys_inc.h"

/**************************************************************************************/

#define H26X_RTP_MAX_LEN    (1450 - 4 - 12 - 2 - 16)    // TCP RTP FU REPLAYHDR
#define JPEG_RTP_MAX_LEN    (1450 - 4 - 12 - 16)
#define RTP_MAX_LEN         (1450 - 4 - 12 - 16)

/*
 * Current protocol version.
 */
#define RTP_VERSION         2

#define RTP_SEQ_MOD         (1<<16)
#define RTP_MAX_SDES        255      /* maximum text length for SDES */

/**************************************************************************************/

typedef enum {
    RTCP_FIR        = 192,
    RTCP_NACK,      // 193
    RTCP_SMPTETC,   // 194
    RTCP_IJ,        // 195
    RTCP_SR         = 200,
    RTCP_RR,        // 201
    RTCP_SDES,      // 202
    RTCP_BYE,       // 203
    RTCP_APP,       // 204
    RTCP_RTPFB,     // 205
    RTCP_PSFB,      // 206
    RTCP_XR,        // 207
    RTCP_AVB,       // 208
    RTCP_RSI,       // 209
    RTCP_TOKEN,     // 210
} rtcp_type_t;

#define RTP_PT_IS_RTCP(x) (((x) >= RTCP_FIR && (x) <= RTCP_IJ) || \
                           ((x) >= RTCP_SR  && (x) <= RTCP_TOKEN))

typedef struct
{
    uint32  magic   : 8;
    uint32  channel : 8;
    uint32  rtp_len : 16;
} RILF;

#endif


