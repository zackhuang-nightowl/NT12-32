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
#include "audio_decoder.h"
#include "avcodec_mutex.h"
#include "media_codec.h"

CAudioDecoder::CAudioDecoder()
{
    m_bInited = FALSE;
    m_nSamplerate = 0;
    m_nChannels = 0;
    
    m_pCodec = NULL;
    m_pContext = NULL;
    m_pFrame = NULL;
    m_pResampleFrame = NULL;
    m_pSwrCtx = NULL;
    m_nDstFormat = AV_SAMPLE_FMT_S16;

    m_pCallback = NULL;
    m_pUserdata = NULL;
}

CAudioDecoder::~CAudioDecoder()
{
    uninit();
}

BOOL CAudioDecoder::init(enum AVCodecID codec, int samplerate, int channels, int bitpersample)
{
    m_pCodec = avcodec_find_decoder(codec);
    if (m_pCodec == NULL)
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

    m_pContext->sample_rate = samplerate;
    m_pContext->channels = channels;
    m_pContext->channel_layout = av_get_default_channel_layout(channels);

    if (AV_CODEC_ID_ADPCM_G726 == codec)
    {
        if (0 == bitpersample)
        {
            bitpersample = 2;
        }
        
        m_pContext->bits_per_coded_sample = bitpersample;
        m_pContext->bit_rate = bitpersample * 8000;
    }

    if (avcodec_thread_open(m_pContext, m_pCodec, NULL) < 0)
    {
        log_print(HT_LOG_ERR, "%s, avcodec_thread_open failed\r\n", __FUNCTION__);
        return FALSE;
    }

    if (m_pContext->sample_fmt != m_nDstFormat)
    {
        m_pSwrCtx = swr_alloc_set_opts(NULL, 
            av_get_default_channel_layout(channels), m_nDstFormat, samplerate, 
            av_get_default_channel_layout(channels), m_pContext->sample_fmt, samplerate, 0, NULL);

        swr_init(m_pSwrCtx);
    }
    
    m_pFrame = av_frame_alloc();
    if (NULL == m_pFrame)
    {
        log_print(HT_LOG_ERR, "%s, av_frame_alloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nSamplerate = samplerate;
    m_nChannels = channels;
    
    m_bInited = TRUE;
    
    return TRUE;
}

BOOL CAudioDecoder::init(int codec, int samplerate, int channels, int bitpersample)
{
    return init(to_audio_avcodecid(codec), samplerate, channels, bitpersample);
}

BOOL CAudioDecoder::decode(uint8 * data, int len, int64_t pts)
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

BOOL CAudioDecoder::decode(AVPacket *pkt)
{
    int ret;    

    if (!m_bInited)
    {
        return FALSE;
    }
    
    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(m_pContext, pkt);    
    if (ret < 0)
    { 
        log_print(HT_LOG_ERR, "%s, error submitting the packet to the decoder\r\n", __FUNCTION__); 
        return FALSE;    
    }

    /* read all the output frames (in general there may be any number of them */    
    while (ret >= 0) 
    {        
        ret = avcodec_receive_frame(m_pContext, m_pFrame);        
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return TRUE; 
        }
        else if (ret < 0) 
        {  
            log_print(HT_LOG_ERR, "%s, error during decoding\r\n", __FUNCTION__);
            return FALSE;
        }

        render(m_pFrame);

        av_frame_unref(m_pFrame);
    }

    return TRUE;
}

void CAudioDecoder::flush()
{
    if (NULL == m_pContext || 
        NULL == m_pContext->codec || 
        !(m_pContext->codec->capabilities | AV_CODEC_CAP_DELAY))
    {
        return;
    }
    
    decode(NULL);
}

void CAudioDecoder::uninit()
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

    if (m_pResampleFrame)
    {
        av_frame_free(&m_pResampleFrame);
    }

    if (m_pSwrCtx)
    {
        swr_free(&m_pSwrCtx);
    }

    m_bInited = FALSE;
}

void CAudioDecoder::render(AVFrame * frame)
{
    if (NULL == frame)
    {
        return;
    }

    if (m_pSwrCtx)
    {
        if (NULL == m_pResampleFrame)
        {
            m_pResampleFrame = av_frame_alloc();
            if (NULL == m_pResampleFrame)
            {
                return;
            }
        }
        
        m_pResampleFrame->sample_rate = m_nSamplerate;
        m_pResampleFrame->format = m_nDstFormat;
        m_pResampleFrame->channels = m_nChannels;
        m_pResampleFrame->channel_layout = av_get_default_channel_layout(m_nChannels);
            
        int swrret = swr_convert_frame(m_pSwrCtx, m_pResampleFrame, frame);
        if (swrret == 0)
        {
            if (m_pCallback)
            {
                m_pResampleFrame->pts = frame->pts;
                m_pResampleFrame->pkt_dts = frame->pkt_dts;
                
                m_pCallback(m_pResampleFrame, m_pUserdata);
            }
        }

        av_frame_unref(m_pResampleFrame);
    }
    else if (m_pCallback)
    {
        m_pCallback(frame, m_pUserdata);
    }    
}



