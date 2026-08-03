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

#ifndef MEDIA_UTIL_H
#define MEDIA_UTIL_H

#include "sys_inc.h"

typedef struct
{
    uint8   vps[512];
    int     vps_size;
    uint8   sps[512];
    int     sps_size;
    uint8   pps[512];
    int     pps_size;
} H26XParamSets;

#ifdef __cplusplus
extern "C" {
#endif

uint32  remove_emulation_bytes(uint8 *to, uint32 to_max_size, uint8 *from, uint32 from_size);
uint8 * avc_find_startcode(uint8 *p, uint8 *end);
uint8 * avc_split_nalu(uint8 *e_buf, int e_len, int *s_len, int *d_len);
uint8   avc_h264_nalu_type(uint8 *e_buf, int len);
uint8   avc_h265_nalu_type(uint8 *e_buf, int len);
BOOL    avc_get_h26x_paramsets(uint8 *data, int len, int codec, H26XParamSets *ps);
BOOL    avc_is_decodable_frame(uint8 *data, int len, int codec);
void    ht_gen_h264_sdp_line(char *buff, int size, int rtp_pt, uint8 *sps, int sps_size, uint8 *pps, int pps_size);
void    ht_gen_h265_sdp_line(char *buff, int size, int rtp_pt, uint8 *sps, int sps_size, uint8 *pps, int pps_size, uint8 *vps, int vps_size);
int     ht_get_aac_config(int sample_rate, int channels, uint8 *config, int size);
void    ht_gen_aac_sdp_line(char *buff, int size, int rtp_pt, uint8 *config, int config_size);

#ifdef __cplusplus
}
#endif

#endif



