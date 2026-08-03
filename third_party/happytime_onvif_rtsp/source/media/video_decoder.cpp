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

#include "video_decoder.h"
#include "avcodec_mutex.h"
#include "lock.h"
#include "media_codec.h"
#include "media_parse.h"


/***************************************************************************************/

uint32  g_hw_decoder_nums = 0;
uint32  g_hw_decoder_max = 2;   // Hardware decoding resources are limited, Limit up to 2 hardware decoding sessions 
void  * g_hw_decoder_mutex = sys_os_create_mutex();

/***************************************************************************************/

enum AVPixelFormat getHWFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    CVideoDecoder * pthis = (CVideoDecoder *) ctx->opaque;

    AVPixelFormat dst_pix_fmt = AV_PIX_FMT_NONE;
    
    pthis->getHWFormat(ctx, pix_fmts, &dst_pix_fmt);

    return dst_pix_fmt;
}

CVideoDecoder::CVideoDecoder()
{
    m_bInited = FALSE;
    
    m_pCodec = NULL;
    m_pContext = NULL;
    m_pFrame = NULL;
    m_pSoftFrame = NULL;

    m_pCallback = NULL;
    m_pUserdata = NULL;

    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_pHWDeviceCtx = NULL;
}

CVideoDecoder::~CVideoDecoder()
{
    uninit();
}

void CVideoDecoder::uninit()
{
    flush();
    
    if (m_pContext)
    {
        avcodec_thread_close(m_pContext);
        avcodec_free_context(&m_pContext);
    }

    if (m_pFrame)
    {
        av_frame_free(&m_pFrame);
    }

    if (m_pSoftFrame)
    {
        av_frame_free(&m_pSoftFrame);
    }

    if (m_pHWDeviceCtx)
    {
        av_buffer_unref(&m_pHWDeviceCtx);

        CLock lock(g_hw_decoder_mutex);
        g_hw_decoder_nums--;
    }

    m_bInited = FALSE;
}

BOOL CVideoDecoder::init(enum AVCodecID codec, uint8 * extradata, int extradata_size, int hwMode)
{
    int width = 0;
    int height = 0;
        
    if (extradata && extradata_size > 0)
    {
        int vcodec = VIDEO_CODEC_NONE;
    
        if (AV_CODEC_ID_H264 == codec)
        {
            vcodec = VIDEO_CODEC_H264;
        }
        else if (AV_CODEC_ID_HEVC == codec)
        {
            vcodec = VIDEO_CODEC_H265;
        }
        else if (AV_CODEC_ID_MJPEG == codec)
        {
            vcodec = VIDEO_CODEC_JPEG;
        }
        else if (AV_CODEC_ID_MPEG4 == codec)
        {
            vcodec = VIDEO_CODEC_MP4;
        }

        avc_parse_video_size(vcodec, extradata, extradata_size, &width, &height);
    }
    
#ifdef ANDROID
    if (HW_DECODING_DISABLE != hwMode && width*height >= 320*240)
    {
        if (AV_CODEC_ID_H264 == codec)
        {
            m_pCodec = avcodec_find_decoder_by_name("h264_mediacodec");
        }
        else if (AV_CODEC_ID_HEVC == codec)
        {
            m_pCodec = avcodec_find_decoder_by_name("hevc_mediacodec");
        }
        else if (AV_CODEC_ID_MPEG4 == codec)
        {
            m_pCodec = avcodec_find_decoder_by_name("mpeg4_mediacodec");
        }
    }

    if (NULL == m_pCodec)
    {
        m_pCodec = avcodec_find_decoder(codec);
    }
#else
    m_pCodec = avcodec_find_decoder(codec);
#endif
    
    if (NULL == m_pCodec)
    {
        log_print(HT_LOG_ERR, "%s, m_pCodec is NULL\r\n", __FUNCTION__);
        return FALSE;
    }
    
    m_pContext = avcodec_alloc_context3(m_pCodec);
    if (NULL == m_pContext)
    {
        log_print(HT_LOG_ERR, "%s, avcodec_alloc_context3 failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_pContext->width = width;
    m_pContext->height = height;
    
    m_pContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_pContext->flags2 |= AV_CODEC_FLAG2_FAST;

    /* ***** Output always the frames ***** */
    m_pContext->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    
    av_opt_set_int(m_pContext, "refcounted_frames", 1, 0);

    if (HW_DECODING_DISABLE != hwMode && hwDecoderInit(m_pContext, hwMode) < 0)
    {
        log_print(HT_LOG_WARN, "%s, hwDecoderInit failed\r\n", __FUNCTION__);
    }

    if (extradata && extradata_size > 0)
    {
        int size = extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;
        
        m_pContext->extradata = (uint8 *)av_mallocz(size);
        if (m_pContext->extradata)
        {
            m_pContext->extradata_size = extradata_size;
            memcpy(m_pContext->extradata, extradata, extradata_size);
        }
    }

    if (avcodec_thread_open(m_pContext, m_pCodec, NULL) < 0)
    {
        log_print(HT_LOG_ERR, "%s, avcodec_thread_open failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_pFrame = av_frame_alloc();
    if (NULL == m_pFrame)
    {    
        log_print(HT_LOG_ERR, "%s, av_frame_alloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_pSoftFrame = av_frame_alloc();
    if (NULL == m_pSoftFrame)
    {    
        log_print(HT_LOG_ERR, "%s, av_frame_alloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_bInited = TRUE;
    
    return TRUE;
}

BOOL CVideoDecoder::init(int codec, uint8 * extradata, int extradata_size, int hwMode)
{
    return init(to_video_avcodecid(codec), extradata, extradata_size, hwMode);
}

int CVideoDecoder::hwDecoderInit(AVCodecContext *ctx, int hwMode)
{
    int i, err = 0;
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    
    if (HW_DECODING_DISABLE == hwMode)
    {
        return 0;
    }

    CLock lock(g_hw_decoder_mutex);

    if (g_hw_decoder_max > 0)
    {
        if (g_hw_decoder_nums >= g_hw_decoder_max)
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }

    char hwtype[32] = {'\0'};
    
#if __WINDOWS_OS__

    if (HW_DECODING_D3D11 == hwMode)
    {
        strcpy(hwtype, "d3d11va");
    }
    else if (HW_DECODING_DXVA == hwMode)
    {
        strcpy(hwtype, "dxva2");
    }
    else if (HW_DECODING_AUTO == hwMode)
    {
        strcpy(hwtype, "d3d11va");
        
        type = av_hwdevice_find_type_by_name(hwtype);
        if (AV_HWDEVICE_TYPE_NONE == type)
        {
            strcpy(hwtype, "dxva2");
        }
    }

#elif defined(IOS)
    
    if (HW_DECODING_VIDEOTOOLBOX == hwMode)
    {
        strcpy(hwtype, "videotoolbox");
    }
    else if (HW_DECODING_OPENCL == hwMode)
    {
        strcpy(hwtype, "opencl");
    }
    else if (HW_DECODING_AUTO == hwMode)
    {
        strcpy(hwtype, "videotoolbox");
        
        type = av_hwdevice_find_type_by_name(hwtype);
        if (AV_HWDEVICE_TYPE_NONE == type)
        {
            strcpy(hwtype, "opencl");
        }
    }
    
#elif defined(ANDROID)

    if (HW_DECODING_MEDIACODEC == hwMode)
    {
        strcpy(hwtype, "mediacodec");
    }
    else if (HW_DECODING_AUTO == hwMode)
    {
        strcpy(hwtype, "mediacodec");
    }
    
#elif __LINUX_OS__

    if (HW_DECODING_VAAPI == hwMode)
    {
        strcpy(hwtype, "vaapi");
    }
    else if (HW_DECODING_OPENCL == hwMode)
    {
        strcpy(hwtype, "opencl");
    }
    else if (HW_DECODING_AUTO == hwMode)
    {
        strcpy(hwtype, "vaapi");
        
        type = av_hwdevice_find_type_by_name(hwtype);
        if (AV_HWDEVICE_TYPE_NONE == type)
        {
            strcpy(hwtype, "opencl");
        }
    }
    
#endif

    type = av_hwdevice_find_type_by_name(hwtype);
    if (AV_HWDEVICE_TYPE_NONE == type)
    {
        log_print(HT_LOG_WARN, "%s, hwdevice type %s not support\r\n", __FUNCTION__, hwtype);

        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
        {
            log_print(HT_LOG_INFO, "%s, %s\r\n", __FUNCTION__, av_hwdevice_get_type_name(type));
        }
        
        return -1;
    }
    
    for (i = 0;; i++) 
    {
        const AVCodecHWConfig * config = avcodec_get_hw_config(m_pCodec, i);
        if (!config) 
        {
            log_print(HT_LOG_WARN, "%s, decoder %s does not support device type %s\r\n", __FUNCTION__,
                   m_pCodec->long_name, av_hwdevice_get_type_name(type));
            return -1;
        }
        
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && 
            config->device_type == type) 
        {
            m_hwPixFmt = config->pix_fmt;
            break;
        }
    }
    
    err = av_hwdevice_ctx_create(&m_pHWDeviceCtx, type, NULL, NULL, 0);
    if (err < 0) 
    {
        log_print(HT_LOG_ERR, "%s, Failed to create specified HW device, type=%s\r\n", 
            __FUNCTION__, av_hwdevice_get_type_name(type));
        return err;
    }

    ctx->opaque = this;
    ctx->get_format = ::getHWFormat;
    ctx->hw_device_ctx = av_buffer_ref(m_pHWDeviceCtx);

    g_hw_decoder_nums++;
    
    return err;
}

BOOL CVideoDecoder::getHWFormat(AVCodecContext *ctx, const AVPixelFormat *pix_fmts, AVPixelFormat *dst)
{
    const AVPixelFormat *p;

    *dst = AV_PIX_FMT_NONE;

    for (p = pix_fmts; *p != -1; p++) 
    {
        if (*p == m_hwPixFmt)
        {
            *dst = *p;
            return TRUE;
        }    
    }

    for (p = pix_fmts; *p != -1; p++) 
    {
        if (*p == AV_PIX_FMT_YUV420P)
        {
            *dst = *p;
            return TRUE;
        }
    }

    for (p = pix_fmts; *p != -1; p++) 
    {
        if (*p == AV_PIX_FMT_YUVJ420P || *p == AV_PIX_FMT_YUVJ422P)
        {
            *dst = *p;
            return TRUE;
        }
    }

    if (*pix_fmts != -1)
    {
        *dst = *pix_fmts;
        return TRUE;
    }
    
    log_print(HT_LOG_ERR, "%s, Failed to get HW surface format\r\n", __FUNCTION__);
    
    return FALSE;
}

int CVideoDecoder::getWidth()
{
    if (m_pContext)
    {
        return m_pContext->width;
    }

    return 0;
}

int CVideoDecoder::getHeight()
{
    if (m_pContext)
    {
        return m_pContext->height;
    }

    return 0;
}

double CVideoDecoder::getFrameRate()
{
    if (m_pContext)
    {
        if (m_pContext->framerate.den > 0)
        {
            return (double)((double)(m_pContext->framerate.num) / m_pContext->framerate.den);
        }
    }

    return 0;
}

BOOL CVideoDecoder::decode(AVPacket * pkt)
{
    if (!m_bInited)
    {
        return FALSE;
    }

    int cnt = 0;
    int ret;
    
RETRY:    
    ret = avcodec_send_packet(m_pContext, pkt);
    if (ret == AVERROR(EAGAIN))
    {
        readFrame();

        if (cnt++ < 3)
        {
            goto RETRY;
        }
    }
    else if (ret < 0) 
    {       
        return FALSE;   
    }  

    if (ret >= 0)
    {
        return readFrame();
    }
    else
    {
        return FALSE;
    }    
}

BOOL CVideoDecoder::decode(uint8 * data, int len, int64_t pts)
{
    BOOL ret;
    AVPacket * packet;

    packet = av_packet_alloc();
    if (!packet)
    {
        return FALSE;
    }
    
    packet->data = data;
    packet->size = len;
    packet->pts = packet->dts = pts;

    ret = decode(packet);

    av_packet_free(&packet);

    return ret;
}

BOOL CVideoDecoder::readFrame()
{
    int ret = 0;
    AVFrame * tmp_frame = NULL;
    
    while (ret >= 0)
    {        
        ret = avcodec_receive_frame(m_pContext, m_pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return TRUE; 
        }        
        else if (ret < 0)
        {     
            log_print(HT_LOG_ERR, "%s, avcodec_receive_frame, ret=%d\r\n", __FUNCTION__, ret);
            return FALSE;     
        }

        if (m_pFrame->format == m_hwPixFmt) 
        {
            /* retrieve data from GPU to CPU */
            ret = av_hwframe_transfer_data(m_pSoftFrame, m_pFrame, 0);
            if (ret < 0) 
            {
                log_print(HT_LOG_ERR, "%s, error transferring the data to system memory, ret=%d\r\n", __FUNCTION__, ret);
                return FALSE; 
            }

            m_pSoftFrame->pts = m_pFrame->pts;
            m_pSoftFrame->pkt_dts = m_pFrame->pkt_dts;
            
            tmp_frame = m_pSoftFrame;
        } 
        else
        {
            tmp_frame = m_pFrame;
        }

        render(tmp_frame);

        av_frame_unref(tmp_frame);
    }

    return TRUE;
}

int CVideoDecoder::render(AVFrame * frame)
{
    if (m_pCallback)
    {
        m_pCallback(frame, m_pUserdata);
    }

    return 1;
}

void CVideoDecoder::flush()
{
    if (NULL == m_pContext || 
        NULL == m_pContext->codec || 
        !(m_pContext->codec->capabilities | AV_CODEC_CAP_DELAY))
    {
        return;
    }
    
    decode(NULL);
}




