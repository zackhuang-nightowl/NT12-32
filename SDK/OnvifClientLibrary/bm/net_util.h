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

#ifndef NET_UTIL_H
#define NET_UTIL_H

#include "sys_inc.h"

#define NTP_OFFSET          2208988800ULL
#define NTP_OFFSET_US       (NTP_OFFSET * 1000000ULL)

#ifdef __cplusplus
extern "C" {
#endif

HT_API uint8    net_read_uint8(uint8 * input);
HT_API uint16   net_read_uint16(uint8 * input);
HT_API uint32   net_read_uint32(uint8 * input);
HT_API uint64   net_read_uint64(uint8 * input);
HT_API int      net_write_uint8(uint8 * output, uint8 val);
HT_API int      net_write_uint16(uint8 * output, uint16 val);
HT_API int      net_write_uint32(uint8 * output, uint32 val);
HT_API int      net_write_uint64(uint8 * output, uint64 val);
HT_API uint32   get_rtp_timestamp(int frequency);
HT_API uint64   get_ntp_time();

#ifdef __cplusplus
}
#endif

#endif



