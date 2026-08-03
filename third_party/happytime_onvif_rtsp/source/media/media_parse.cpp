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

#include "media_parse.h"
#include "media_format.h"
#include "media_util.h"
#include "h264.h"
#include "h265.h"
#include "mjpeg.h"
#include "h264_util.h"
#include "h265_util.h"
#include "bs.h"
#include <math.h>


int avc_parse_video_size(int codec, uint8 *data, uint32 size, int *width, int *height)
{
    uint8 nalu_t;

    if (data == NULL || size < 5)
    {
        return -1;
    }
    
    // Need to parse width X height

    if (VIDEO_CODEC_H264 == codec)
    {
        int s_len = 0, n_len = 0, parse_len = size;
        uint8 *p_cur = data;
        uint8 *p_end = data + size;
        
        while (p_cur && p_cur < p_end && parse_len > 0)
        {
            uint8 * p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
            
            nalu_t = (p_cur[s_len] & 0x1F);
            
            int b_start;
            nal_t nal;
            
            nal.i_payload = n_len-s_len-1;
            nal.p_payload = p_cur+s_len+1;
            nal.i_type = nalu_t;

            if (nalu_t == H264_NAL_SPS)    
            {
                h264_t parse;
                h264_parser_init(&parse);

                h264_parser_parse(&parse, &nal, &b_start);
                log_print(HT_LOG_INFO, "%s, H264 width[%d],height[%d]\r\n", __FUNCTION__, parse.i_width, parse.i_height);
                *width = parse.i_width;
                *height = parse.i_height;
                return 0;
            }
            
            parse_len = p_next ? p_end - p_next : 0;
            p_cur = p_next;
        }
    }
    else if (VIDEO_CODEC_H265 == codec)
    {
        int s_len, n_len = 0, parse_len = size;
        uint8 *p_cur = data;
        uint8 *p_end = data + size;

        while (p_cur && p_cur < p_end && parse_len > 0)
        {
            uint8 * p_next = avc_split_nalu(p_cur, parse_len, &s_len, &n_len);
            
            nalu_t = (p_cur[s_len] >> 1) & 0x3F;
            
            if (nalu_t == HEVC_NAL_SPS)    
            {
                h265_t parse;
                h265_parser_init(&parse);

                if (h265_parser_parse(&parse, p_cur+s_len, n_len-s_len) == 0)
                {
                    log_print(HT_LOG_INFO, "%s, H265 width[%d],height[%d]\r\n", __FUNCTION__, parse.pic_width_in_luma_samples, parse.pic_height_in_luma_samples);
                    *width = parse.pic_width_in_luma_samples;
                    *height = parse.pic_height_in_luma_samples;
                    return 0;
                }
            }
            
            parse_len = p_next ? p_end - p_next : 0;
            p_cur = p_next;
        }
    }
    else if (VIDEO_CODEC_JPEG == codec)
    {
        uint32 offset = 0;
        int size_chunk = 0;
        
        while (offset < size - 8 && data[offset] == 0xFF)
        {
            if (data[offset+1] == MARKER_SOF0)
            {
                int h = ((data[offset+5] << 8) | data[offset+6]);
                int w = ((data[offset+7] << 8) | data[offset+8]);
                log_print(HT_LOG_INFO, "%s, MJPEG width[%d],height[%d]\r\n", __FUNCTION__, w, h);
                *width = w;
                *height = h;
                return 0;
            }
            else if (data[offset+1] == MARKER_SOI)
            {
                offset += 2;
            }
            else
            {
                size_chunk = ((data[offset+2] << 8) | data[offset+3]);
                offset += 2 + size_chunk;
            }
        }
    }
    else if (VIDEO_CODEC_MP4 == codec)
    {
        uint32 pos = 0;
        int vol_f = 0;
        int vol_pos = 0;
        int vol_len = 0;

        while (pos < size - 4)
        {
            if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1)
            {
                if (data[pos+3] >= 0x20 && data[pos+3] <= 0x2F)
                {
                    vol_f = 1;
                    vol_pos = pos+4;
                }
                else if (vol_f)
                {
                    vol_len = pos - vol_pos;
                    break;
                }
            }

            pos++;
        }

        if (!vol_f)
        {
            return 0;
        }
        else if (vol_len <= 0)
        {
            vol_len = size - vol_pos;
        }

        int vo_ver_id = 0;

        bs_t bs;
        bs_init(&bs, &data[vol_pos], vol_len * 8);

        bs_skip(&bs, 1);     /* random access */
        bs_skip(&bs, 8);     /* vo_type */

        if (bs_read1(&bs))   /* is_ol_id */
        {
            vo_ver_id = bs_read(&bs, 4); /* vo_ver_id */
            bs_skip(&bs, 3); /* vo_priority */
        }

        if (bs_read(&bs, 4) == 15) // aspect_ratio_info
        {
            bs_skip(&bs, 8);     // par_width
            bs_skip(&bs, 8);     // par_height
        }

        if (bs_read1(&bs)) /* vol control parameter */
        {
            bs_read(&bs, 2);

            bs_skip(&bs, 1);     /* low_delay */

            if (bs_read1(&bs)) 
            {    
                /* vbv parameters */
                bs_read(&bs, 15);   /* first_half_bitrate */
                bs_skip(&bs, 1);
                bs_read(&bs, 15);   /* latter_half_bitrate */
                bs_skip(&bs, 1);
                bs_read(&bs, 15);   /* first_half_vbv_buffer_size */
                bs_skip(&bs, 1);
                bs_read(&bs, 3);    /* latter_half_vbv_buffer_size */
                bs_read(&bs, 11);   /* first_half_vbv_occupancy */
                bs_skip(&bs, 1);
                bs_read(&bs, 15);   /* latter_half_vbv_occupancy */
                bs_skip(&bs, 1);
            }                        
        }

        int shape = bs_read(&bs, 2); /* vol shape */
        
        if (shape == 3 && vo_ver_id != 1) 
        {
            bs_skip(&bs, 4);  /* video_object_layer_shape_extension */
        }

        bs_skip(&bs, 1); 
        
        int framerate = bs_read(&bs, 16);

        int time_increment_bits = (int) (log(framerate - 1.0) * 1.44269504088896340736 + 1); // log2(framerate - 1) + 1
        if (time_increment_bits < 1)
        {
            time_increment_bits = 1;
        }
        
        bs_skip(&bs, 1);
        
        if (bs_read1(&bs) != 0)     /* fixed_vop_rate  */
        {
            bs_skip(&bs, time_increment_bits);    
        }
        
        if (shape == 0) 
        {
            bs_skip(&bs, 1);
            int w = bs_read(&bs, 13);
            bs_skip(&bs, 1);
            int h = bs_read(&bs, 13);

            log_print(HT_LOG_INFO, "%s, MPEG4 width[%d],height[%d]\r\n", __FUNCTION__, w, h);
            *width = w;
            *height = h;
            return 0;
        }
    }
    
    return -1;
}




