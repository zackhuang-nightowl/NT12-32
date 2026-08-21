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

#ifndef RTP_TX_H
#define RTP_TX_H

#include "rtsp_rsua.h"

#define RTSP_RESV_HDR_SIZE  64 // Reserved header size

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 *  Build h264 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h264_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build h265 video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_h265_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build video rtp packet and send (not fragment)
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build jpeg video rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return the send data length, -1 on error
 *
 **/
int rtp_jpeg_video_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build audio rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_audio_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build AAC audio rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_aac_audio_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

/**
 * @brief
 *  Build metadata data rtp packet and send
 *
 * @param p_rua rtsp user agent
 * @param data payload data
 * @param size payload data length
 * @param ts the packet timestamp
 *
 * @return positive integer on success, negative integer on error
 *
 **/
int rtp_metadata_tx(RSSUA * p_rua, uint8 * data, int size, uint32 ts);

#ifdef __cplusplus
}
#endif

#endif


