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

#include "media_util.h"
#include "media_format.h"
#include "h264.h"
#include "h265.h"
#include "base64.h"

uint32 remove_emulation_bytes(uint8 *to, uint32 to_max_size, uint8 *from, uint32 from_size) 
{
    uint32 to_size = 0;
    uint32 i = 0;
    
    while (i < from_size && to_size+1 < to_max_size) 
    {
        if (i+2 < from_size && from[i] == 0 && from[i+1] == 0 && from[i+2] == 3) 
        {
            to[to_size] = to[to_size+1] = 0;
            to_size += 2;
            i += 3;
        }
        else 
        {
            to[to_size] = from[i];
            to_size += 1;
            i += 1;
        }
    }

    return to_size;
}

static uint8 * avc_find_startcode_internal(uint8 *p, uint8 *end)
{
    uint8 *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) 
    {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
        {
            return p;
        }
    }

    for (end -= 3; p < end; p += 4) 
    {
        uint32 x = *(const uint32*)p;
        
        if ((x - 0x01010101) & (~x) & 0x80808080) 
        { // generic
            if (p[1] == 0) 
            {
                if (p[0] == 0 && p[2] == 1)
                {
                    return p;
                }
                
                if (p[2] == 0 && p[3] == 1)
                {
                    return p+1;
                }    
            }
            
            if (p[3] == 0) 
            {
                if (p[2] == 0 && p[4] == 1)
                {
                    return p+2;
                }
                
                if (p[4] == 0 && p[5] == 1)
                {
                    return p+3;
                }    
            }
        }
    }

    for (end += 3; p < end; p++) 
    {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
        {
            return p;
        }
    }

    return end + 3;
}

uint8 * avc_find_startcode(uint8 *p, uint8 *end)
{
    uint8 *out = avc_find_startcode_internal(p, end);
    if (p < out && out < end && !out[-1])
    {
        out--;
    }
    
    return out;
}

uint8 * avc_split_nalu(uint8 *e_buf, int e_len, int *s_len, int * d_len)
{
    uint8 *r, *r1 = NULL, *end = e_buf + e_len;

    *s_len = 0;
    *d_len = 0;

    r = avc_find_startcode(e_buf, end);
    if (r < end)
    {
        if (r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 1)
        {
            *s_len = 4;
        }
        else if (r[0] == 0 && r[1] == 0 && r[2] == 1)
        {
            *s_len = 3;
        }

        while (!*(r++));

        r1 = avc_find_startcode(r, end);

        *d_len = r1 - r + *s_len;
    }
    else
    {
        *d_len = e_len;
    }

    return r1;
}

uint8 avc_h264_nalu_type(uint8 *e_buf, int len)
{
    if (len > 4 && e_buf[0] == 0 && e_buf[1] == 0 && e_buf[2] == 0 && e_buf[3] == 1)
    {
        return (e_buf[4] & 0x1f);
    }
    else if (len > 3 && e_buf[0] == 0 && e_buf[1] == 0 && e_buf[2] == 1)
    {
        return (e_buf[3] & 0x1f);
    }
    else if (len > 0)
    {
        return (e_buf[0] & 0x1f);
    }

    return 0;
}

uint8 avc_h265_nalu_type(uint8 *e_buf, int len)
{
    if (len > 4 && e_buf[0] == 0 && e_buf[1] == 0 && e_buf[2] == 0 && e_buf[3] == 1)
    {
        return ((e_buf[4] >> 1) & 0x3f);
    }
    else if (len > 3 && e_buf[0] == 0 && e_buf[1] == 0 && e_buf[2] == 1)
    {
        return ((e_buf[3] >> 1) & 0x3f);
    }
    else if (len > 0)
    {
        return ((e_buf[0] >> 1) & 0x3f);
    }

    return 0;
}

BOOL avc_get_h26x_paramsets(uint8 *data, int len, int codec, H26XParamSets *ps)
{
    BOOL ret = FALSE;
    int s_len = 0, n_len = 0, parse_len = len;
    uint8 *p_cur = data;
    uint8 *p_end = data + len;
        
    while (p_cur && p_cur < p_end && parse_len > 0)
    {
        uint8 nalu;
        uint8 *p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
        
        if (VIDEO_CODEC_H264 == codec)
        {
            nalu = avc_h264_nalu_type(p_cur, n_len);
            
            if (nalu == H264_NAL_SPS && ps->sps_size == 0 && n_len <= (int)sizeof(ps->sps))
            {
                memcpy(ps->sps, p_cur, n_len);
                ps->sps_size = n_len;
            }
            else if (nalu == H264_NAL_PPS && ps->pps_size == 0 && n_len <= (int)sizeof(ps->pps))
            {
                memcpy(ps->pps, p_cur, n_len);
                ps->pps_size = n_len;
            }

            if (ps->sps_size > 0 && ps->pps_size > 0)
            {
                ret = TRUE;
                break;
            }
        }
        else if (VIDEO_CODEC_H265 == codec)
        {
            nalu = avc_h265_nalu_type(p_cur, n_len);
            
            if (nalu == HEVC_NAL_VPS && ps->vps_size == 0 && n_len <= (int)sizeof(ps->vps))
            {
                memcpy(ps->vps, p_cur, n_len);
                ps->vps_size = n_len;
            }
            else if (nalu == HEVC_NAL_SPS && ps->sps_size == 0 && n_len <= (int)sizeof(ps->sps))
            {
                memcpy(ps->sps, p_cur, n_len);
                ps->sps_size = n_len;
            }
            else if (nalu == HEVC_NAL_PPS && ps->pps_size == 0 && n_len <= (int)sizeof(ps->pps))
            {
                memcpy(ps->pps, p_cur, n_len);
                ps->pps_size = n_len;
            }

            if (ps->vps_size > 0 && ps->sps_size > 0 && ps->pps_size > 0)
            {
                ret = TRUE;
                break;
            }
        }
        
        parse_len = p_next ? p_end - p_next : 0;
        p_cur = p_next;
    }

    return ret;
}

BOOL avc_is_decodable_frame(uint8 *data, int len, int codec)
{
    BOOL ret = FALSE;
    int s_len = 0, n_len = 0, parse_len = len;
    uint8 *p_cur = data;
    uint8 *p_end = data + len;
        
    while (p_cur && p_cur < p_end && parse_len > 0)
    {
        uint8 nalu;
        uint8 * p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
        
        if (VIDEO_CODEC_H264 == codec)
        {
            nalu = avc_h264_nalu_type(p_cur, n_len);
            
            if (nalu == H264_NAL_SLICE || nalu == H264_NAL_IDR)
            {
                ret = TRUE;
                break;
            }
        }
        else if (VIDEO_CODEC_H265 == codec)
        {
            nalu = avc_h265_nalu_type(p_cur, n_len);
            
            if ((nalu >= 0 && nalu <= 21) || (nalu >= 36 && nalu <= 38))
            {
                ret = TRUE;
                break;
            }
        }
        
        parse_len = p_next ? p_end - p_next : 0;
        p_cur = p_next;
    }

    return ret;
}

void ht_gen_h264_sdp_line(char *buff, int size, int rtp_pt, uint8 *sps, int sps_size, uint8 *pps, int pps_size)
{
    // Set up the "a=fmtp:" SDP line

    if (sps_size <= 0)
    {
        return;
    }
    
    uint8 *sps_web = (uint8 *) malloc(sps_size); // "WEB" means "Without Emulation Bytes"
    if (NULL == sps_web)
    {
        return;
    }
    
    uint32 vps_web_size = remove_emulation_bytes(sps_web, sps_size, sps, sps_size);
    if (vps_web_size < 4) 
    {
        // Bad SPS size => assume our source isn't ready
        free(sps_web);
        return;
    }
    
    uint32 profile_level_id = (sps_web[1]<<16) | (sps_web[2]<<8) | sps_web[3];

    free(sps_web);

    char *sps_base64 = (char *) malloc(sps_size*2+1);
    if (NULL == sps_base64)
    {
        return;
    }
    
    char *pps_base64 = (char *) malloc(pps_size*2+1);
    if (NULL == pps_base64)
    {
        free(sps_base64);
        return;
    }

    base64_encode(sps, sps_size, sps_base64, sps_size*2+1);
    base64_encode(pps, pps_size, pps_base64, pps_size*2+1);

    snprintf(buff, size, 
        "a=fmtp:%d packetization-mode=1;profile-level-id=%06X;sprop-parameter-sets=%s,%s", 
        rtp_pt, profile_level_id, sps_base64, pps_base64);

    free(sps_base64);
    free(pps_base64);
}

void ht_gen_h265_sdp_line(char *buff, int size, int rtp_pt, uint8 *sps, int sps_size, uint8 *pps, int pps_size, uint8 *vps, int vps_size)
{
    // Set up the "a=fmtp:" SDP line

    if (vps_size < 7)
    {
        return;
    }
    
    uint8 *vps_web = (uint8 *) malloc(vps_size); // "WEB" means "Without Emulation Bytes"
    if (NULL == vps_web)
    {
        return;
    }
    
    uint32 vps_web_size = remove_emulation_bytes(vps_web, vps_size, vps, vps_size);
    if (vps_web_size < 6 + 12) // profile_tier_level's offset + num 'profile_tier_level' bytes
    {
        // Bad VPS size => assume our source isn't ready
        free(vps_web);
        return;
    }
    
    uint8 const *header = &vps_web[6];          // profile tier level header bytes
    uint32 profile_space = header[0] >> 6;      // general_profile_space
    uint32 profile_id = header[0] & 0x1F;       // general_profile_idc
    uint32 tier_flag = (header[0] >>5 ) & 0x1;  // general_tier_flag
    uint32 level_id = header[11];               // general_level_idc
    uint8 const* constraints = &header[5];      // interop constraints

    char constraints_str[100];
    snprintf(constraints_str, sizeof(constraints_str),
        "%02X%02X%02X%02X%02X%02X", 
        constraints[0], constraints[1], constraints[2], constraints[3], constraints[4], constraints[5]);

    free(vps_web);

    char *sprop_vps = (char *) malloc(vps_size*2+1);
    if (NULL == sprop_vps)
    {
        return;
    }
    
    char *sprop_sps = (char *) malloc(sps_size*2+1);
    if (NULL == sprop_sps)
    {
        free(sprop_vps);
        return;
    }
    
    char *sprop_pps = (char *) malloc(pps_size*2+1);
    if (NULL == sprop_pps)
    {
        free(sprop_vps);
        free(sprop_sps);
        return;
    }

    base64_encode(vps, vps_size, sprop_vps, vps_size*2+1);
    base64_encode(sps, sps_size, sprop_sps, sps_size*2+1);
    base64_encode(pps, pps_size, sprop_pps, pps_size*2+1);

    snprintf(buff, size,
        "a=fmtp:%d profile-space=%u"
        ";profile-id=%u"
        ";tier-flag=%u"
        ";level-id=%u"
        ";interop-constraints=%s"
        ";sprop-vps=%s"
        ";sprop-sps=%s"
        ";sprop-pps=%s",
        rtp_pt, profile_space,
        profile_id,
        tier_flag,
        level_id,
        constraints_str,
        sprop_vps,
        sprop_sps,
        sprop_pps);

    free(sprop_vps);
    free(sprop_sps);
    free(sprop_pps);
}

int ht_get_aac_config(int sample_rate, int channels, uint8 *config, int config_size)
{
    int rate_idx = 0;
    uint8 obj_type = 2; // AAC-LC
    
    if (NULL == config || config_size < 2)
    {
        return 0;
    }
    
    switch (sample_rate)
    {
    case 96000:
        rate_idx = 0;
        break;

    case 88200:
        rate_idx = 1;
        break;
        
    case 64000:
        rate_idx = 2;
        break;
        
    case 48000:
        rate_idx = 3;
        break;
        
    case 44100:
        rate_idx = 4;
        break;
        
    case 32000:
        rate_idx = 5;
        break;
        
    case 24000:
        rate_idx = 6;
        break;

    case 22050:
        rate_idx = 7;
        break;

    case 16000:
        rate_idx = 8;
        break;     

    case 12000:
        rate_idx = 9;
        break;

    case 11025:
        rate_idx = 10;
        break;

    case 8000:
        rate_idx = 11;
        break;

    case 7350:
        rate_idx = 12;
        break;
        
    default:
        rate_idx = 4;
        break;
    }
    
    config[0] = (obj_type << 3) | (rate_idx >> 1);
    config[1] = (rate_idx << 7) | (channels << 3);

    return 2;
}

void ht_gen_aac_sdp_line(char *buff, int size, int rtp_pt, uint8 *config, int config_size)
{
    int i, offset = 0;

    offset += snprintf(buff, size, 
        "a=fmtp:%d streamtype=5;profile-level-id=1;mode=AAC-hbr;"
        "sizelength=13;indexlength=3;indexdeltalength=3;config=", 
        rtp_pt);

    for (i = 0; i < config_size; ++i)
    {
        offset += snprintf(buff+offset, size-offset, "%02X", config[i]);
    }
}


