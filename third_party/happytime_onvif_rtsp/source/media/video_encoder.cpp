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
#include "video_encoder.h"
#include "media_format.h"
#include "base64.h"
#include "media_util.h"
#include "media_codec.h"
#include "h264.h"
#include "h265.h"
#include "avcodec_mutex.h"

CVideoEncoder::CVideoEncoder()
{
    m_nCodecId = AV_CODEC_ID_NONE;
    m_nDstPixFmt = AV_PIX_FMT_YUV420P;

    memset(&m_EncoderParams, 0, sizeof(VideoEncoderParam));
    
    m_pCodecCtx = NULL;
    m_pFrameSrc = NULL;
    m_pFrameYuv = NULL;
    m_pts = 0;

    m_nFrameIndex = 0;
    m_bInited = FALSE;

    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);
}

CVideoEncoder::~CVideoEncoder()
{
    uninit();

    hlist_free_container(m_pCallbackList);
    
    sys_os_destroy_mutex(m_pCallbackMutex);
}

int CVideoEncoder::computeBitrate(AVCodecID codec, int width, int height, double framerate, int quality)
{    
    double fLQ = 0;
    double fMQ = 0;
    double fHQ = 0;
    
    int factor = 2;
    int pixels = width * height; 

    if (quality>= 0 && quality < 33)
    {
        factor = 0;
    }
    else if (quality >= 33 && quality < 67)
    {
        factor = 1;
    }
    else if (quality >= 67 && quality <= 100)
    {
        factor = 2;
    }
    
    switch (codec) 
    {         
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
        if (pixels < 640*480)
        { 
            fLQ = 12/400.0;   
            fMQ = 12/300.0;   
            fHQ = 12/200.0;  
        }
        else
        {
            fLQ = 12/420.0;   
            fMQ = 12/320.0;   
            fHQ = 12/220.0;  
        }
        break;
        
    default:           
        if (pixels < 640*480)
        { 
            fLQ = 12/360.0;   
            fMQ = 12/260.0;   
            fHQ = 12/160.0;
        }
        else
        {
            fLQ = 12/380.0;   
            fMQ = 12/280.0;   
            fHQ = 12/180.0;  
        }
        break;
    }

    int nBitrate = 0;
     
    if (0 == factor)
    {
        nBitrate = (int) (pixels * framerate * fLQ);
    }
    else if (1 == factor)
    {
        nBitrate =  (int) (pixels * framerate * fMQ);
    }
    else
    {
        nBitrate = (int) (pixels * framerate * fHQ);
    }

    if (quality < 40)
    {
        quality = 40;
    }
    else if (quality > 80)
    {
        quality = 80;
    }
    
    nBitrate = nBitrate * (quality / 100.0);
    
    return nBitrate;
} 

BOOL CVideoEncoder::init(VideoEncoderParam * params)
{
    memcpy(&m_EncoderParams, params, sizeof(VideoEncoderParam));
    
    if (m_EncoderParams.DstHeight < 0)
    {
        m_EncoderParams.DstHeight = -m_EncoderParams.DstHeight;
    }

    m_EncoderParams.DstWidth = m_EncoderParams.DstWidth / 2 * 2;    // align to 2
    m_EncoderParams.DstHeight = m_EncoderParams.DstHeight / 2 * 2;  // align to 2

    m_nCodecId = to_video_avcodecid(m_EncoderParams.DstCodec);
    
    const AVCodec * pCodec = avcodec_find_encoder(m_nCodecId);
    if (pCodec == NULL)
    {
        log_print(HT_LOG_ERR, "avcodec_find_encoder failed, %d\r\n", m_nCodecId);
        return FALSE;
    }
    
    m_pCodecCtx = avcodec_alloc_context3(pCodec);
    if (m_pCodecCtx == NULL)
    {
        log_print(HT_LOG_ERR, "avcodec_alloc_context3 failed\r\n");
        return FALSE;
    }

    // set destination pixmel format
    
    if (pCodec->pix_fmts)
    {
        m_nDstPixFmt = pCodec->pix_fmts[0];
    }
    else if (AV_CODEC_ID_MJPEG == m_nCodecId)
    {
        m_nDstPixFmt = AV_PIX_FMT_YUVJ420P;
    }
    else
    {
        m_nDstPixFmt = AV_PIX_FMT_YUV420P;
    }

    int num;
    
    if (m_EncoderParams.DstFramerate - (int)m_EncoderParams.DstFramerate > 0.0)
    {
        num = (int)(1001 * m_EncoderParams.DstFramerate) + 1;
    }    
    else
    {
        num = (int)(1001 * m_EncoderParams.DstFramerate);
    }
    
    // set video encoder parameters
    m_pCodecCtx->qblur = 0.5;
    m_pCodecCtx->qcompress = 0.5;
    m_pCodecCtx->b_quant_offset = 1.25;
    m_pCodecCtx->b_quant_factor = 1.25;
    m_pCodecCtx->i_quant_offset = 0.0;
    m_pCodecCtx->i_quant_factor = (float)-0.8;
        
    m_pCodecCtx->codec_id = m_nCodecId;
    m_pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;                            
    m_pCodecCtx->width = m_EncoderParams.DstWidth;
    m_pCodecCtx->height = m_EncoderParams.DstHeight;
    m_pCodecCtx->gop_size = m_EncoderParams.DstFramerate;
    m_pCodecCtx->pix_fmt = m_nDstPixFmt;
    m_pCodecCtx->time_base.num = 1001;
    m_pCodecCtx->time_base.den = num;
    m_pCodecCtx->framerate.num = num;
    m_pCodecCtx->framerate.den = 1001;
    m_pCodecCtx->qmin = 3;
    m_pCodecCtx->qmax = 31;
    
    if (m_EncoderParams.DstBitrate > 0)
    {
        m_pCodecCtx->bit_rate = m_EncoderParams.DstBitrate * 1000;
    }
    else
    {
        m_pCodecCtx->bit_rate = computeBitrate(m_nCodecId, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight, m_EncoderParams.DstFramerate, 100);
    }
    
    if (m_nCodecId == AV_CODEC_ID_MPEG4)
    {
        m_pCodecCtx->profile = FF_PROFILE_MPEG4_SIMPLE;
        m_pCodecCtx->codec_tag = 0x30355844;
    }
    else if (m_nCodecId == AV_CODEC_ID_H264)
    {
        m_pCodecCtx->thread_count = 1;
        m_pCodecCtx->profile = FF_PROFILE_H264_MAIN;

        av_opt_set(m_pCodecCtx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(m_pCodecCtx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(m_pCodecCtx->priv_data, "x264-params", "log-level=-1:open-gop=0", 0);
    }
    else if (m_nCodecId == AV_CODEC_ID_HEVC)
    {
        m_pCodecCtx->profile = FF_PROFILE_HEVC_MAIN;

        av_opt_set(m_pCodecCtx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(m_pCodecCtx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(m_pCodecCtx->priv_data, "x265-params", "log-level=-1:open-gop=0", 0);
    }
    else if (m_nCodecId == AV_CODEC_ID_MJPEG)
    {
        m_pCodecCtx->color_range = AVCOL_RANGE_JPEG;

        av_opt_set(m_pCodecCtx->priv_data, "huffman", "default", 0);
    }

    if (m_nCodecId != AV_CODEC_ID_MPEG4)
    {
        m_pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    if (avcodec_thread_open(m_pCodecCtx, pCodec, NULL) < 0)
    {
        log_print(HT_LOG_ERR, "avcodec_thread_open failed, video encoder\r\n");
        return FALSE;
    }

    m_bInited = TRUE;

    return TRUE;
}

void CVideoEncoder::uninit()
{
    flush();
    
    if (m_pCodecCtx)
    {
        avcodec_thread_close(m_pCodecCtx);
        avcodec_free_context(&m_pCodecCtx);
    }

    if (m_pFrameSrc)
    {
        av_frame_free(&m_pFrameSrc);
    }

    if (m_pFrameYuv)
    {
        av_frame_free(&m_pFrameYuv);
    }

    m_bInited = FALSE;
}

void CVideoEncoder::flush()
{
    if (NULL == m_pCodecCtx || 
        NULL == m_pCodecCtx->codec || 
        !(m_pCodecCtx->codec->capabilities | AV_CODEC_CAP_DELAY))
    {
        return;
    }
    
    encode(NULL);
}

BOOL CVideoEncoder::encode(uint8 * data, int size)
{
    if (!m_bInited)
    {
        return FALSE;
    }

    if (NULL == m_pFrameSrc)
    {
        m_pFrameSrc = av_frame_alloc();

        if (m_pFrameSrc)
        {
            m_pFrameSrc->format = m_EncoderParams.SrcPixFmt;
            m_pFrameSrc->width  = m_EncoderParams.SrcWidth; 
            m_pFrameSrc->height = m_EncoderParams.SrcHeight;
        }
        else
        {
            return FALSE;
        }
    }
        
    av_image_fill_arrays(m_pFrameSrc->data, m_pFrameSrc->linesize, data, m_EncoderParams.SrcPixFmt, m_pFrameSrc->width, m_pFrameSrc->height, 1);
    
    return encode(m_pFrameSrc);
}

BOOL CVideoEncoder::encode(AVFrame * pFrame)
{
    if (!m_bInited)
    {
        return FALSE;
    }

    SwsContext * pSwsCtx = NULL;
    
    if (pFrame && 
        (pFrame->format != m_nDstPixFmt ||
         pFrame->width != m_EncoderParams.DstWidth || 
         pFrame->height != m_EncoderParams.DstHeight))
    {
        if (NULL == m_pFrameYuv)
        {
            m_pFrameYuv = av_frame_alloc();
            if (m_pFrameYuv)
            {
                m_pFrameYuv->format = m_nDstPixFmt;
                m_pFrameYuv->width  = m_EncoderParams.DstWidth; 
                m_pFrameYuv->height = m_EncoderParams.DstHeight;

                if (av_frame_get_buffer(m_pFrameYuv, 0) < 0)
                {
                    log_print(HT_LOG_ERR, "%s, av_frame_get_buffer failed\r\n", __FUNCTION__);
                    return FALSE;
                }

                av_frame_make_writable(m_pFrameYuv); // make sure the frame data is writable
            }
            else
            {
                log_print(HT_LOG_ERR, "%s, av_frame_alloc failed\r\n", __FUNCTION__);
                return FALSE;
            }
        }
        
        pSwsCtx = sws_getContext(pFrame->width, pFrame->height, (AVPixelFormat)pFrame->format, 
                                 m_pFrameYuv->width, m_pFrameYuv->height, m_nDstPixFmt, 
                                 SWS_FAST_BILINEAR, NULL, NULL, NULL);
    }

    AVFrame * pFrameSrc = NULL;
    
    if (NULL == pSwsCtx)
    {
        pFrameSrc = pFrame;        
    }
    else if (pFrame)
    {
        if (m_pFrameYuv->height != sws_scale(pSwsCtx, pFrame->data, pFrame->linesize, 0, pFrame->height, m_pFrameYuv->data, m_pFrameYuv->linesize))
        {
            log_print(HT_LOG_ERR, "%s, sws_scale failed\r\n", __FUNCTION__);
            return FALSE;
        }

        sws_freeContext(pSwsCtx);
        
        pFrameSrc = m_pFrameYuv;
    }

    if (pFrameSrc)
    {
        if ((m_nFrameIndex % (int)m_EncoderParams.DstFramerate) == 0)
        {
            pFrameSrc->pict_type = AV_PICTURE_TYPE_I; 
            pFrameSrc->key_frame = 1; 
        }
        else
        {
            pFrameSrc->pict_type = AV_PICTURE_TYPE_P;
            pFrameSrc->key_frame = 0; 
        }

        m_nFrameIndex++;

        if (m_EncoderParams.Updown)
        {
            pFrameSrc->data[0] += pFrameSrc->linesize[0] * (m_EncoderParams.DstHeight - 1);
            pFrameSrc->linesize[0] *= -1;
            pFrameSrc->data[1] += pFrameSrc->linesize[1] * (m_EncoderParams.DstHeight / 2 - 1);
            pFrameSrc->linesize[1] *= -1;
            pFrameSrc->data[2] += pFrameSrc->linesize[2] * (m_EncoderParams.DstHeight / 2 - 1);
            pFrameSrc->linesize[2] *= -1;
        }
    }

    int ret;
    AVPacket * pkt;
    
    pkt = av_packet_alloc();
    if (!pkt)
    {
        return FALSE;
    }

    pkt->data = 0;
    pkt->size = 0;

    if (m_nCodecId == AV_CODEC_ID_MPEG4 && pFrameSrc)
    {
        // MP4 encoding PTS needs to keep increasing
        pFrameSrc->pts = pFrameSrc->pkt_dts = m_pts;
        m_pts += 2;
    }
    
    ret = avcodec_send_frame(m_pCodecCtx, pFrameSrc);    
    if (ret < 0) 
    {
        av_packet_free(&pkt);
        log_print(HT_LOG_ERR, "%s, error sending a frame for encoding\r\n", __FUNCTION__);        
        return FALSE;    
    }

    while (ret >= 0) 
    {        
        ret = avcodec_receive_packet(m_pCodecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_packet_free(&pkt);
            return TRUE;
        }    
        else if (ret < 0) 
        {
            av_packet_free(&pkt);
            log_print(HT_LOG_ERR, "%s, error during encoding\r\n", __FUNCTION__);
            return FALSE;
        }

        if (pkt->data && pkt->size > 0)
        {
            procData(pkt->data, pkt->size);
        }
        else
        {
            log_print(HT_LOG_WARN, "%s, data is null\r\n", __FUNCTION__);
        }
        
        av_packet_unref(pkt);    
    }

    av_packet_free(&pkt);

    return TRUE;
}

void CVideoEncoder::procData(uint8 * data, int size)
{
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback != NULL)
        {
            if (m_nCodecId == AV_CODEC_ID_H264 && m_pCodecCtx && m_pCodecCtx->extradata_size > 8)
            {
                uint8 naltype = avc_h264_nalu_type(data, size);

                if (H264_NAL_IDR == naltype || p_cb->bFirst)
                {
                    p_cb->bFirst = FALSE;
                    p_cb->pCallback(m_pCodecCtx->extradata, m_pCodecCtx->extradata_size, 0, p_cb->pUserdata);
                }
            }
            else if (m_nCodecId == AV_CODEC_ID_HEVC && m_pCodecCtx && m_pCodecCtx->extradata_size > 8)
            {
                uint8 naltype = avc_h265_nalu_type(data, size);

                if (IS_IRAP_NAL(naltype) || p_cb->bFirst)
                {
                    p_cb->bFirst = FALSE;
                    p_cb->pCallback(m_pCodecCtx->extradata, m_pCodecCtx->extradata_size, 0, p_cb->pUserdata);
                }
            }
            
            p_cb->pCallback(data, size, 1, p_cb->pUserdata);
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

BOOL CVideoEncoder::isCallbackExist(VideoDataCallback pCallback, void *pUserdata)
{
    BOOL exist = FALSE;
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {
            exist = TRUE;
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);
    
    sys_os_mutex_leave(m_pCallbackMutex);

    return exist;
}

void CVideoEncoder::addCallback(VideoDataCallback pCallback, void *pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    VideoEncoderCB * p_cb = (VideoEncoderCB *) malloc(sizeof(VideoEncoderCB));
    if (NULL == p_cb)
    {
        return;
    }
    
    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CVideoEncoder::delCallback(VideoDataCallback pCallback, void *pUserdata)
{
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {        
            free(p_cb);
            
            hlist_remove(m_pCallbackList, p_node);
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

void CVideoEncoder::getH264AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 * sps = NULL; uint32 sps_size = 0;
    uint8 * pps = NULL; uint32 pps_size = 0;

    if (NULL == m_pCodecCtx || m_pCodecCtx->extradata_size <= 8)
    {
        return;
    }

    uint8 *r, *end = m_pCodecCtx->extradata + m_pCodecCtx->extradata_size;
    r = avc_find_startcode(m_pCodecCtx->extradata, end);

    while (r < end) 
    {
        uint8 *r1;
        
        while (!*(r++));
        r1 = avc_find_startcode(r, end);

        int nal_type = (r[0] & 0x1F);
        
        if (H264_NAL_PPS == nal_type)
        {
            pps = r;
            pps_size = r1 - r;
        }
        else if (H264_NAL_SPS == nal_type)
        {
            sps = r;
            sps_size = r1 - r;
        }
        
        r = r1;
    }

    if (NULL == sps || sps_size == 0 ||
        NULL == pps || pps_size == 0)
    {
        return;
    }

    ht_gen_h264_sdp_line(buff, size, rtp_pt, sps, sps_size, pps, pps_size);
}

void CVideoEncoder::getH265AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8* vps = NULL; uint32 vps_size = 0;
    uint8* sps = NULL; uint32 sps_size = 0;
    uint8* pps = NULL; uint32 pps_size = 0;

    if (NULL == m_pCodecCtx || m_pCodecCtx->extradata_size < 12)
    {
        return;
    }

    uint8 *r, *end = m_pCodecCtx->extradata + m_pCodecCtx->extradata_size;
    r = avc_find_startcode(m_pCodecCtx->extradata, end);

    while (r < end) 
    {
        uint8 *r1;
        
        while (!*(r++));
        r1 = avc_find_startcode(r, end);

        int nal_type = (r[0] >> 1) & 0x3F;
        
        if (HEVC_NAL_VPS == nal_type)
        {
            vps = r;
            vps_size = r1 - r;
        }
        else if (HEVC_NAL_PPS == nal_type)
        {
            pps = r;
            pps_size = r1 - r;
        }
        else if (HEVC_NAL_SPS == nal_type)
        {
            sps = r;
            sps_size = r1 - r;
        }
        
        r = r1;
    }
    
    if (NULL == vps || vps_size == 0 ||
        NULL == sps || sps_size == 0 ||
        NULL == pps || pps_size == 0)
    {
        return;
    }

    ht_gen_h265_sdp_line(buff, size, rtp_pt, sps, sps_size, pps, pps_size, vps, vps_size);
}

void CVideoEncoder::getMP4AuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (NULL == m_pCodecCtx || m_pCodecCtx->extradata_size == 0)
    {
        return;
    }

    int i, offset = 0;

    offset += snprintf(buff, size, "a=fmtp:%d profile-level-id=%d;config=", rtp_pt, 1);

    for (i = 0; i < m_pCodecCtx->extradata_size; ++i) 
    {
        offset += snprintf(buff+offset, size-offset, "%02X", m_pCodecCtx->extradata[i]);
    }
}

void CVideoEncoder::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_nCodecId == AV_CODEC_ID_H264)
    {
        getH264AuxSDPLine(buff, size, rtp_pt);
    }
    else if (m_nCodecId == AV_CODEC_ID_MPEG4)
    {
        getMP4AuxSDPLine(buff, size, rtp_pt);
    }
    else if (m_nCodecId == AV_CODEC_ID_HEVC)
    {
        getH265AuxSDPLine(buff, size, rtp_pt);
    }
}

BOOL CVideoEncoder::getExtraData(uint8 ** extradata, int * extralen)
{
    if (m_pCodecCtx && m_pCodecCtx->extradata_size > 0)
    {
        *extradata = m_pCodecCtx->extradata;
        *extralen = m_pCodecCtx->extradata_size;

        return TRUE;
    }

    return FALSE;
}




