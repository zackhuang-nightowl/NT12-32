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
#include "bs.h"
#include "util.h"
#include "h265.h"
#include "h265_util.h"


/**************************************************************************************/

int h265_extract_rbsp(const uint8 *src, int length, uint8 *dst)
{
    int i, si, di;

    for (i = 0; i + 1 < length; i += 2) 
    {
        if (src[i])
        {
            continue;
        } 
        
        if (i > 0 && src[i - 1] == 0)
        {
            i--;
        }
        
        if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) 
        {
            if (src[i + 2] != 3 && src[i + 2] != 0) 
            {
                /* startcode, so we must be past the end */ 
                length = i;
            }

            break;
        }
    }

    if (i >= length - 1) 
    {
        // no escaped 0
        memcpy(dst, src, length);
        return length;
    }

    memcpy(dst, src, i);
    si = di = i;

    while (si + 2 < length) 
    {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) 
        {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } 
        else if (src[si] == 0 && src[si + 1] == 0 && src[si + 2] != 0) 
        {
            if (src[si + 2] == 3) 
            { 
                // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si += 3;
                
                continue;
            } 
            else // next start code
            {
                return si;
            }    
        }

        dst[di++] = src[si++];
    }

    while (si < length)
    {
        dst[di++] = src[si++];
    }
    
    return si;
}

void h265_parser_init(h265_t * h)
{
    memset(h, 0, sizeof(h265_t));
}

int h265_parser_parse(h265_t * h, uint8 * p_data, int len)
{
    uint32 i;
    bs_t s;
    uint8 bufs[512];
    
    if (len > (int)sizeof(bufs))
    {
        return -1;
    }

    len = h265_extract_rbsp(p_data, len, bufs);

    bs_init(&s, bufs, len);

    bs_read(&s, 4);    // sps_video_parameter_set_id u(4)
    h->sps_max_sub_layers_minus1 = bs_read(&s, 3);    // sps_max_sub_layers_minus1 u(3)
    if (h->sps_max_sub_layers_minus1 > 6)
    {
        log_print(HT_LOG_ERR, "%s, sps_max_sub_layers_minus1[%d]>6!!!\r\n", 
            __FUNCTION__, h->sps_max_sub_layers_minus1);
        return -1;
    }
    
    bs_read(&s, 1);    // sps_temporal_id_nesting_flag u(1)

    // profile_tier_level( maxNumSubLayersMinus1 )
    {
        bs_read(&s, 2);        // general_profile_space u(2)
        bs_read(&s, 1);        // general_tier_flag u(1)
        h->general_profile_idc = bs_read(&s, 5);    // general_profile_idc u(5)
        bs_read(&s, 32);    // general_profile_compatibility_flag[ j ] u(5)
        bs_read(&s, 1);        // general_progressive_source_flag u(1)
        bs_read(&s, 1);        // general_interlaced_source_flag u(1)
        bs_read(&s, 1);        // general_non_packed_constraint_flag u(1)
        bs_read(&s, 1);        // general_frame_only_constraint_flag u(1)
        bs_skip(&s, 44);            // general_reserved_zero_44bits u(44)
        h->general_level_idc = bs_read(&s, 8);    // general_level_idc u(8)

        uint8 sub_layer_profile_present_flag[6] = {0};
        uint8 sub_layer_level_present_flag[6]   = {0};
        
        for (i = 0; i < h->sps_max_sub_layers_minus1; i++) 
        {
            sub_layer_profile_present_flag[i]= bs_read(&s, 1);
            sub_layer_level_present_flag[i]= bs_read(&s, 1);
        }
        
        if (h->sps_max_sub_layers_minus1 > 0) 
        {
            for (i = h->sps_max_sub_layers_minus1; i < 8; i++) 
            {
                bs_read(&s, 2);
            }
        }

        for (i = 0; i < h->sps_max_sub_layers_minus1; i++) 
        {
            if (sub_layer_profile_present_flag[i]) 
            {
                bs_read(&s, 2);        // sub_layer_profile_space[i]
                bs_read(&s, 1);        // sub_layer_tier_flag[i]
                bs_read(&s, 5);        // sub_layer_profile_idc[i]
                bs_read(&s, 32);    // sub_layer_profile_compatibility_flag[i][32]
                bs_read(&s, 1);        // sub_layer_progressive_source_flag[i]
                bs_read(&s, 1);        // sub_layer_interlaced_source_flag[i]
                bs_read(&s, 1);        // sub_layer_non_packed_constraint_flag[i]
                bs_read(&s, 1);        // sub_layer_frame_only_constraint_flag[i]
                bs_read(&s, 44);    // sub_layer_reserved_zero_44bits[i]
            }
            
            if (sub_layer_level_present_flag[i])
            {
                bs_read(&s, 8);    // sub_layer_level_idc[i]
            }
        }
    }

    h->sps_seq_parameter_set_id = bs_read_ue(&s);    // sps_seq_parameter_set_id ue(v)
    h->chroma_format_idc = bs_read_ue(&s);            // chroma_format_idc ue(v)
    if (h->chroma_format_idc > 3)
    {
        log_print(HT_LOG_ERR, "%s, chroma_format_idc[%d]!!!\r\n", 
            __FUNCTION__, h->chroma_format_idc);
        return -1;
    }

    if (h->chroma_format_idc == 3)
    {
        h->separate_colour_plane_flag = bs_read(&s, 1); // separate_colour_plane_flag
    }

    h->pic_width_in_luma_samples = bs_read_ue(&s);        // pic_width_in_luma_samples ue(v)
    h->pic_height_in_luma_samples = bs_read_ue(&s);        // pic_height_in_luma_samples ue(v)

    h->conformance_window_flag = bs_read(&s, 1 );        // conformance_window_flag u(1)
    if (h->conformance_window_flag)
    {
        h->conf_win_left_offset = bs_read_ue(&s);        // conf_win_left_offset ue(v)
        h->conf_win_right_offset = bs_read_ue(&s);        // conf_win_right_offset ue(v)
        h->conf_win_top_offset = bs_read_ue(&s);        // conf_win_top_offset ue(v)
        h->conf_win_bottom_offset = bs_read_ue(&s);        // conf_win_bottom_offset ue(v)
    }

    h->bit_depth_luma_minus8 = bs_read_ue(&s);            // bit_depth_luma_minus8 ue(v)
    h->bit_depth_chroma_minus8 = bs_read_ue(&s);        // bit_depth_chroma_minus8 ue(v)

    return 0;
}

void hvcc_init(HEVCDecoderConfigurationRecord * hvcc)
{
    memset(hvcc, 0, sizeof(HEVCDecoderConfigurationRecord));

    hvcc->lengthSizeMinusOne   = 3; // 4 bytes

    /*
     * The following fields have all their valid bits set by default,
     * the ProfileTierLevel parsing code will unset them when needed.
     */
    hvcc->general_profile_compatibility_flags = 0xffffffff;
    hvcc->general_constraint_indicator_flags  = 0xffffffffffff;

    /*
     * Initialize this field with an invalid value which can be used to detect
     * whether we didn't see any VUI (in which case it should be reset to zero).
     */
    hvcc->min_spatial_segmentation_idc = MAX_SPATIAL_SEGMENTATION + 1;
}

int hvcc_parse_vps(HEVCDecoderConfigurationRecord * hvcc, uint8 * p_data, int len)
{
    uint32 vps_max_sub_layers_minus1;
    uint8 bufs[512];
    bs_t s;
    
    if (len > (int)sizeof(bufs))
    {
        return -1;
    }

    len = h265_extract_rbsp(p_data, len, bufs);

    bs_init(&s, bufs, len);
    
    /*
     * vps_video_parameter_set_id u(4)
     * vps_reserved_three_2bits   u(2)
     * vps_max_layers_minus1      u(6)
     */
    bs_skip(&s, 12);

    vps_max_sub_layers_minus1 = bs_read(&s, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    hvcc->numTemporalLayers = MAX(hvcc->numTemporalLayers, vps_max_sub_layers_minus1 + 1);

    /*
     * vps_temporal_id_nesting_flag u(1)
     * vps_reserved_0xffff_16bits   u(16)
     */
    bs_skip(&s, 17);

    uint8  profile_space;
    uint8  tier_flag;
    uint8  profile_idc;
    uint32 profile_compatibility_flags;
    uint64 constraint_indicator_flags;
    uint8  level_idc;

    profile_space               = bs_read(&s, 2);
    tier_flag                   = bs_read(&s, 1);
    profile_idc                 = bs_read(&s, 5);
    profile_compatibility_flags = bs_read(&s, 32);
    constraint_indicator_flags  = (uint64)bs_read(&s, 16) << 32;
    constraint_indicator_flags |= bs_read(&s, 32);
    level_idc                   = bs_read(&s, 8);
    
    hvcc->general_profile_space = profile_space;

    /*
     * The level indication general_level_idc must indicate a level of
     * capability equal to or greater than the highest level indicated for the
     * highest tier in all the parameter sets.
     */
    if (hvcc->general_tier_flag < tier_flag)
        hvcc->general_level_idc = level_idc;
    else
        hvcc->general_level_idc = MAX(hvcc->general_level_idc, level_idc);

    /*
     * The tier indication general_tier_flag must indicate a tier equal to or
     * greater than the highest tier indicated in all the parameter sets.
     */
    hvcc->general_tier_flag = MAX(hvcc->general_tier_flag, tier_flag);

    /*
     * The profile indication general_profile_idc must indicate a profile to
     * which the stream associated with this configuration record conforms.
     *
     * If the sequence parameter sets are marked with different profiles, then
     * the stream may need examination to determine which profile, if any, the
     * entire stream conforms to. If the entire stream is not examined, or the
     * examination reveals that there is no profile to which the entire stream
     * conforms, then the entire stream must be split into two or more
     * sub-streams with separate configuration records in which these rules can
     * be met.
     *
     * Note: set the profile to the highest value for the sake of simplicity.
     */
    hvcc->general_profile_idc = MAX(hvcc->general_profile_idc, profile_idc);

    /*
     * Each bit in general_profile_compatibility_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_profile_compatibility_flags &= profile_compatibility_flags;

    /*
     * Each bit in general_constraint_indicator_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_constraint_indicator_flags &= constraint_indicator_flags;

    /* nothing useful for hvcC past this point */
    return 0;
}

int hvcc_parse_pps(HEVCDecoderConfigurationRecord * hvcc, uint8 * p_data, int len)
{
    uint8 tiles_enabled_flag, entropy_coding_sync_enabled_flag;
    uint8 bufs[512];
    bs_t s;
    
    if (len > (int)sizeof(bufs))
    {
        return -1;
    }

    len = h265_extract_rbsp(p_data, len, bufs);

    bs_init(&s, bufs, len);
    
    bs_read_ue(&s); // pps_pic_parameter_set_id
    bs_read_ue(&s); // pps_seq_parameter_set_id

    /*
     * dependent_slice_segments_enabled_flag u(1)
     * output_flag_present_flag              u(1)
     * num_extra_slice_header_bits           u(3)
     * sign_data_hiding_enabled_flag         u(1)
     * cabac_init_present_flag               u(1)
     */
    bs_skip(&s, 7);

    bs_read_ue(&s); // num_ref_idx_l0_default_active_minus1
    bs_read_ue(&s); // num_ref_idx_l1_default_active_minus1
    bs_read_se(&s); // init_qp_minus26

    /*
     * constrained_intra_pred_flag u(1)
     * transform_skip_enabled_flag u(1)
     */
    bs_skip(&s, 2);

    if (bs_read(&s, 1)) // cu_qp_delta_enabled_flag
        bs_read_ue(&s); // diff_cu_qp_delta_depth

    bs_read_se(&s); // pps_cb_qp_offset
    bs_read_se(&s); // pps_cr_qp_offset

    /*
     * pps_slice_chroma_qp_offsets_present_flag u(1)
     * weighted_pred_flag               u(1)
     * weighted_bipred_flag             u(1)
     * transquant_bypass_enabled_flag   u(1)
     */
    bs_skip(&s, 4);

    tiles_enabled_flag               = bs_read(&s, 1);
    entropy_coding_sync_enabled_flag = bs_read(&s, 1);

    if (entropy_coding_sync_enabled_flag && tiles_enabled_flag)
        hvcc->parallelismType = 0; // mixed-type parallel decoding
    else if (entropy_coding_sync_enabled_flag)
        hvcc->parallelismType = 3; // wavefront-based parallel decoding
    else if (tiles_enabled_flag)
        hvcc->parallelismType = 2; // tile-based parallel decoding
    else
        hvcc->parallelismType = 1; // slice-based parallel decoding

    /* nothing useful for hvcC past this point */
    return 0;
}



