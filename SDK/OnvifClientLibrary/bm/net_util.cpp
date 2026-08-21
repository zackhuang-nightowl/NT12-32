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
#include "net_util.h"


HT_API uint8 net_read_uint8(uint8 * input)
{
    return input[0];
}

HT_API uint16 net_read_uint16(uint8 * input)
{
    return (uint16)((input[0] << 8) | input[1]);
}

HT_API uint32 net_read_uint32(uint8 * input)
{
    return (uint32)((input[0] << 24) | (input[1] << 16) | (input[2] << 8) | input[3]);
}

HT_API uint64 net_read_uint64(uint8 * input)
{
    uint32 low = net_read_uint32(input);
    uint32 high = net_read_uint32(input+4);
    return (uint64)(((uint64)low) << 32 | high);
}

HT_API int net_write_uint8(uint8 * output, uint8 val)
{
    output[0] = val;

    return 1;
}

HT_API int net_write_uint16(uint8 * output, uint16 val)
{
    output[1] = val & 0xff;
    output[0] = val >> 8;

    return 2;
}

HT_API int net_write_uint32(uint8 * output, uint32 val)
{
    output[3] = val & 0xff;
    output[2] = val >> 8;
    output[1] = val >> 16;
    output[0] = val >> 24;

    return 4;
}

HT_API int net_write_uint64(uint8 * output, uint64 val)
{
    uint32 low = (uint32)(val & 0xFFFF);
    uint32 high = (uint32)(val > 32);
    
    net_write_uint32(output, low);
    net_write_uint32(output+4, high);

    return 8;
}

HT_API uint32 get_rtp_timestamp(int frequency) 
{
    struct timeval tv;

    gettimeofday(&tv, 0);

    // Begin by converting from "struct timeval" units to RTP timestamp units:
    uint32 increment = (frequency*tv.tv_sec);
    increment += (uint32)(frequency*(tv.tv_usec/1000000.0) + 0.5); // note: rounding  

    return increment;
}

HT_API uint64 get_ntp_time()
{
    uint64 tm;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tm = (int64)tv.tv_sec * 1000000 + tv.tv_usec;

    return (tm / 1000) * 1000 + NTP_OFFSET_US;
}



