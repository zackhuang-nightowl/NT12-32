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
#include "file_demux.h"
#include "audio_encoder.h"
#include "video_encoder.h"
#include "media_format.h"
#include "base64.h"
#include "media_util.h"
#include "media_codec.h"
#include "h264.h"
#include "h265.h"
#include "avcodec_mutex.h"


/*********************************************************************************/

/**
 * audio decode callback
 */
void AudioDecodeCallback(AVFrame * frame, void *pUserdata)
{
    CFileDemux * pDemux = (CFileDemux *)pUserdata;

    pDemux->audioEncode(frame);
}

/**
 * video decode callback
 */
void VideoDecodeCallback(AVFrame * frame, void *pUserdata)
{
    CFileDemux * pDemux = (CFileDemux *)pUserdata;

    pDemux->videoEncode(frame);
}

/**
 * audio encode callback
 */
void AudioEncodeCallback(uint8 *data, int size, int nbsamples, void *pUserdata)
{
    CFileDemux * pDemux = (CFileDemux *)pUserdata;

    pDemux->dataCallback(data, size, DATA_TYPE_AUDIO, nbsamples, 1);
}

/**
 * video encode callback
 */
void VideoEncodeCallback(uint8 *data, int size, int waitnext, void *pUserdata)
{
    CFileDemux * pDemux = (CFileDemux *)pUserdata;

    pDemux->dataCallback(data, size, DATA_TYPE_VIDEO, 0, waitnext);
}


CFileDemux::CFileDemux(const char * filename, int loopnums)
{
    m_nAudioIndex = -1;
    m_nVideoIndex = -1;
    m_nDuration = 0;
    m_nCurPos = 0;

    m_nNalLength = 0;

    m_pFormatContext = NULL;

    m_pVideoDecoder = NULL;
    m_pAudioDecoder = NULL;
    m_pVideoEncoder = NULL;
    m_pAudioEncoder = NULL;

    m_nPreNalu = -1;
    m_bVideoFirst = TRUE;
    m_bAudioFirst = TRUE;
    m_pCallback = NULL;
    m_pUserdata = NULL;

    m_nLoopNums = 0;
    m_nMaxLoopNums = loopnums;

    memset(m_pFilename, 0, sizeof(m_pFilename));
    strncpy(m_pFilename, filename, sizeof(m_pFilename)-1);
    
    init(filename);
}

CFileDemux::~CFileDemux()
{
    uninit();
}

/**
 * Init file demux
 * 
 * @param filename the file name
 * @return TRUE on success, FALSE on error
 */
BOOL CFileDemux::init(const char * filename)
{
    if (avformat_open_input(&m_pFormatContext, filename, NULL, NULL) != 0)
    {
        log_print(HT_LOG_ERR, "avformat_open_input failed, %s\r\n", filename);
        return FALSE;
    }

    avformat_find_stream_info(m_pFormatContext, NULL);

    if (m_pFormatContext->duration != AV_NOPTS_VALUE)
    {
        m_nDuration = m_pFormatContext->duration; 
    }
    
    // find audio & video stream index
    for (uint32 i=0; i < m_pFormatContext->nb_streams; i++)
    {
        if (m_pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_nVideoIndex = i;

            if (m_nDuration < m_pFormatContext->streams[i]->duration) 
            {
                m_nDuration = m_pFormatContext->streams[i]->duration;
            }
        }
        else if (m_pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_nAudioIndex = i;

            if (m_nDuration < m_pFormatContext->streams[i]->duration) 
            {
                m_nDuration = m_pFormatContext->streams[i]->duration;
            }
        }
    }

    m_nDuration /= 1000; // to millisecond
    
    // has video stream
    if (m_nVideoIndex != -1)
    {
        AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
        
        if (codecpar->codec_id == AV_CODEC_ID_H264)
        {
            if (codecpar->extradata && codecpar->extradata_size > 8)
            {
                if (codecpar->extradata[0] == 1)
                {
                    // Store right nal length size that will be used to parse all other nals
                    m_nNalLength = (codecpar->extradata[4] & 0x03) + 1;
                }

                const AVBitStreamFilter * bsfc = av_bsf_get_by_name("h264_mp4toannexb");
                if (bsfc) 
                {
                    int ret;
                    
                    AVBSFContext *bsf;
                    av_bsf_alloc(bsfc, &bsf);

                    ret = avcodec_parameters_copy(bsf->par_in, codecpar);
                    if (ret < 0)
                    {
                       return FALSE;
                    }
                    
                    ret = av_bsf_init(bsf);
                    if (ret < 0)
                    {
                        return FALSE;
                    }
                    
                    ret = avcodec_parameters_copy(codecpar, bsf->par_out);
                    if (ret < 0)
                    {
                        return FALSE;
                    }
                    
                    av_bsf_free(&bsf);              
                }
            }
        }
        else if (codecpar->codec_id == AV_CODEC_ID_HEVC)
        {
            if (codecpar->extradata && codecpar->extradata_size > 8)
            {
                if (codecpar->extradata[0] || codecpar->extradata[1] || codecpar->extradata[2] > 1)
                {
                    m_nNalLength = 4;
                }

                const AVBitStreamFilter * bsfc = av_bsf_get_by_name("hevc_mp4toannexb");
                if (bsfc) 
                {
                    int ret;

                    AVBSFContext *bsf;
                    av_bsf_alloc(bsfc, &bsf);

                    ret = avcodec_parameters_copy(bsf->par_in, codecpar);
                    if (ret < 0)
                    {
                       return FALSE;
                    }
                    
                    ret = av_bsf_init(bsf);
                    if (ret < 0)
                    {
                        return FALSE;
                    }
                    
                    ret = avcodec_parameters_copy(codecpar, bsf->par_out);
                    if (ret < 0)
                    {
                        return FALSE;
                    }
                    
                    av_bsf_free(&bsf);              
                }
            }
        }
    }

    return TRUE;
}

void CFileDemux::uninit()
{
    if (m_pVideoDecoder)
    {
        delete m_pVideoDecoder;
        m_pVideoDecoder = NULL;
    }

    if (m_pAudioDecoder)
    {
        delete m_pAudioDecoder;
        m_pAudioDecoder = NULL;
    }

    if (m_pVideoEncoder)
    {
        delete m_pVideoEncoder;
        m_pVideoEncoder = NULL;
    }
    
    if (m_pAudioEncoder)
    {
        delete m_pAudioEncoder;
        m_pAudioEncoder = NULL;
    }

    if (m_pFormatContext)    
    {
        avformat_close_input(&m_pFormatContext);
    }
}

enum AVCodecID CFileDemux::getVideoCodec()
{
    if (m_nVideoIndex != -1)
    {
        return m_pFormatContext->streams[m_nVideoIndex]->codecpar->codec_id;
    }

    return AV_CODEC_ID_NONE;
}

int CFileDemux::getWidth()
{
    if (m_nVideoIndex != -1)
    {
        return m_pFormatContext->streams[m_nVideoIndex]->codecpar->width;
    }

    return 0;
}

int CFileDemux::getHeight()
{
    if (m_nVideoIndex != -1)
    {
        return m_pFormatContext->streams[m_nVideoIndex]->codecpar->height;
    }

    return 0;
}

double CFileDemux::getFramerate() 
{
    double framerate = 25;
    
    if (m_nVideoIndex != -1)
    {
        if (m_pFormatContext->streams[m_nVideoIndex]->avg_frame_rate.den > 0)
        {
            framerate = m_pFormatContext->streams[m_nVideoIndex]->avg_frame_rate.num / 
                        (double)m_pFormatContext->streams[m_nVideoIndex]->avg_frame_rate.den;
        }

        if ((framerate < 1 || framerate > 60) && 
            m_pFormatContext->streams[m_nVideoIndex]->r_frame_rate.den > 0)
        {
            framerate = m_pFormatContext->streams[m_nVideoIndex]->r_frame_rate.num / 
                        (double)m_pFormatContext->streams[m_nVideoIndex]->r_frame_rate.den;
        }

        if (framerate < 1 || framerate > 60)
        {
            framerate = 25;
        }
    }

    return framerate;
}

enum AVCodecID CFileDemux::getAudioCodec()
{
    if (m_nAudioIndex != -1)
    {
        return m_pFormatContext->streams[m_nAudioIndex]->codecpar->codec_id;
    }

    return AV_CODEC_ID_NONE;
}

int CFileDemux::getSamplerate()
{
    if (m_nAudioIndex != -1)
    {
        return m_pFormatContext->streams[m_nAudioIndex]->codecpar->sample_rate;
    }

    return 8000;
}

int CFileDemux::getChannels()
{
    if (m_nAudioIndex != -1)
    {
        return m_pFormatContext->streams[m_nAudioIndex]->codecpar->channels;
    }

    return 2;
}

/**
 * set audio output parameters
 *
 * @param codec audio output codec
 * @param samplerate audio output sample rate
 * @param channels audio output channels
 * @return TRUE on success, FALSE on error
 */
BOOL CFileDemux::setAudioFormat(int codec, int samplerate, int channels, int bitrate)
{
    if (m_nAudioIndex == -1)
    {
        return FALSE;
    }

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nAudioIndex]->codecpar;
    
#if 0                
    if (codecpar->codec_id != to_audio_avcodecid(codec) ||
        codecpar->channels != channels ||
        codecpar->sample_rate != samplerate ||
        codecpar->format != AV_SAMPLE_FMT_S16)
#else
    if (1)
#endif        
    {
        m_pAudioDecoder = new CAudioDecoder;
        if (NULL == m_pAudioDecoder)
        {
            return FALSE;
        }

        if (FALSE == m_pAudioDecoder->init(codecpar->codec_id, codecpar->sample_rate, codecpar->channels, codecpar->bits_per_coded_sample))
        {
            return FALSE;
        }

        m_pAudioDecoder->setCallback(AudioDecodeCallback, this);

        m_pAudioEncoder = new CAudioEncoder;
        if (NULL == m_pAudioEncoder)
        {
            return FALSE;
        }

        AudioEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcChannels = codecpar->channels;
        params.SrcSamplerate = codecpar->sample_rate;
        params.SrcSamplefmt = AV_SAMPLE_FMT_S16;
        params.DstChannels = channels;
        params.DstSamplefmt = AV_SAMPLE_FMT_S16;
        params.DstSamplerate = samplerate;
        params.DstBitrate = bitrate;
        params.DstCodec = codec;
        
        if (FALSE == m_pAudioEncoder->init(&params))
        {
            return FALSE;
        }

        m_pAudioEncoder->addCallback(AudioEncodeCallback, this);
    }

    return TRUE;
}

BOOL CFileDemux::setVideoFormat(int codec, int width, int height, double framerate, int bitrate)
{
    if (m_nVideoIndex == -1)
    {
        return FALSE;
    }

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;

    if (codecpar->codec_id != to_video_avcodecid(codec) || 
        codecpar->width != width ||
        codecpar->height != height)
    {
        m_pVideoDecoder = new CVideoDecoder;
        if (NULL == m_pVideoDecoder)
        {
            return FALSE;
        }

        if (FALSE == m_pVideoDecoder->init(codecpar->codec_id, codecpar->extradata, codecpar->extradata_size))
        {
            return FALSE;
        }

        m_pVideoDecoder->setCallback(VideoDecodeCallback, this);
        
        m_pVideoEncoder = new CVideoEncoder;        
        if (NULL == m_pVideoEncoder)
        {
            return FALSE;
        }

        VideoEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcWidth = codecpar->width;
        params.SrcHeight = codecpar->height;
        params.SrcPixFmt = (AVPixelFormat)codecpar->format;
        params.DstCodec = codec;
        params.DstWidth = width;
        params.DstHeight = height;
        params.DstFramerate = framerate;
        params.DstBitrate = bitrate;
        
        if (FALSE == m_pVideoEncoder->init(&params))
        {
            return FALSE;
        }

        m_pVideoEncoder->addCallback(VideoEncodeCallback, this);
    }

    if (m_pVideoDecoder && codecpar->extradata_size > 0)
    {
        m_pVideoDecoder->decode(codecpar->extradata, codecpar->extradata_size, 0);
    }
        
    return TRUE;
}

void CFileDemux::videoDecode(AVPacket * pkt)
{
    if (m_pVideoDecoder)
    {
        m_pVideoDecoder->decode(pkt);
    }
}

void CFileDemux::audioDecode(AVPacket * pkt)
{
    if (m_pAudioDecoder)
    {
        m_pAudioDecoder->decode(pkt);
    }
}

void CFileDemux::videoEncode(AVFrame * frame)
{
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->encode(frame);
    }
}

void CFileDemux::audioEncode(AVFrame * frame)
{
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->encode(frame);
    }
}

void CFileDemux::videoData(uint8 * data, int size, uint64_t pts, int waitnext)
{
    if (NULL == m_pVideoDecoder)
    {
        dataCallback(data, size, DATA_TYPE_VIDEO, 0, waitnext);
    }
    else
    {
        m_pVideoDecoder->decode(data, size, pts);
    }
}

BOOL CFileDemux::readFrame()
{
    int rret = 0;
    AVPacket * pkt;
    
    pkt = av_packet_alloc();
    if (!pkt)
    {
        return FALSE;
    }

    pkt->data = 0;
    pkt->size = 0;

    rret = av_read_frame(m_pFormatContext, pkt);
    if (AVERROR_EOF == rret)
    {
        if (++m_nLoopNums >= m_nMaxLoopNums)
        {
            av_packet_free(&pkt);
            return FALSE;
        }
        
        if (m_pFormatContext)    
        {
            avformat_close_input(&m_pFormatContext);
        }

        av_packet_free(&pkt);

        return init(m_pFilename);
    }    
    else if (0 != rret)
    {
        av_packet_free(&pkt);
        return FALSE;
    }

    int64 pts = AV_NOPTS_VALUE;

    if (pkt->dts != AV_NOPTS_VALUE)
    {
        pts = pkt->dts;
        
        if (m_pFormatContext->start_time != AV_NOPTS_VALUE)
        {
            pts -= m_pFormatContext->start_time;
        }
    }
    else if (pkt->pts != AV_NOPTS_VALUE)
    {
        pts = pkt->pts;

        if (m_pFormatContext->start_time != AV_NOPTS_VALUE)
        {
            pts -= m_pFormatContext->start_time;
        }
    }
    
    if (pkt->stream_index == m_nVideoIndex)
    {
        AVRational q = {1, AV_TIME_BASE};
        
        if (pts != AV_NOPTS_VALUE)
        {
            m_nCurPos = av_rescale_q (pts, m_pFormatContext->streams[m_nVideoIndex]->time_base, q);
            m_nCurPos /= 1000;
        }
            
        AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
        
        if (codecpar->codec_id == AV_CODEC_ID_H264 || codecpar->codec_id == AV_CODEC_ID_HEVC)
        {
            if (m_nNalLength)
            {
                uint8 * data = pkt->data;
                int size = pkt->size;
                int err = 0;
                
                while (pkt->size >= m_nNalLength)
                {
                    int len = 0;
                    int nal_length = m_nNalLength;
                    uint8 * pdata = pkt->data;
                    
                    while (nal_length--)
                    {
                        len = (len << 8) | *pdata++;
                    }
                    
                    if (len > pkt->size - m_nNalLength || len <= 0)
                    {
                        err = 1;
                        log_print(HT_LOG_DBG, "len=%d, pkt->size=%d\r\n", len, pkt->size);
                        break;
                    }

                    nal_length = m_nNalLength;
                    pkt->data[nal_length-1] = 1;
                    nal_length--;
                    while (nal_length--)
                    {
                        pkt->data[nal_length] = 0;
                    }
                    
                    pkt->data += len + m_nNalLength;
                    pkt->size -= len + m_nNalLength;
                }    

                if (!err)
                {
                    videoData(data, size, pts, 1);
                }
            }                
            else if (pkt->data[0] == 0 && pkt->data[1] == 0 && pkt->data[2] == 0 && pkt->data[3] == 1)
            {
                videoData(pkt->data, pkt->size, pts, 1);
            }
            else if (pkt->data[0] == 0 && pkt->data[1] == 0 && pkt->data[2] == 1)
            {
                videoData(pkt->data, pkt->size, pts, 1);
            }
            else 
            {
                log_print(HT_LOG_ERR, "%s, unknown format\r\n", __FUNCTION__);
            }
        }
        else
        {
            videoData(pkt->data, pkt->size, pts, 1);
        }
    }
    else if (pkt->stream_index == m_nAudioIndex)
    {
        AVRational q = {1, AV_TIME_BASE};
        
        if (pts != AV_NOPTS_VALUE)
        {
            m_nCurPos = av_rescale_q (pts, m_pFormatContext->streams[m_nAudioIndex]->time_base, q);
            m_nCurPos /= 1000;
        }
        
        if (NULL == m_pAudioEncoder)
        {
            dataCallback(pkt->data, pkt->size, DATA_TYPE_AUDIO, 1024, 1);
        }
        else
        {
            audioDecode(pkt);
        }
    }

    av_packet_free(&pkt);

    return TRUE;
}

void CFileDemux::getH264AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 * sps = NULL; uint32 sps_size = 0;
    uint8 * pps = NULL; uint32 pps_size = 0;

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
    
    if (codecpar->extradata_size <= 8)
    {
        return;
    }

    uint8 *r, *end = codecpar->extradata + codecpar->extradata_size;
    r = avc_find_startcode(codecpar->extradata, end);

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

void CFileDemux::getH265AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8* vps = NULL; uint32 vps_size = 0;
    uint8* sps = NULL; uint32 sps_size = 0;
    uint8* pps = NULL; uint32 pps_size = 0;

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
    
    if (codecpar->extradata_size < 23)
    {
        return;
    }

    uint8 *r, *end = codecpar->extradata + codecpar->extradata_size;
    r = avc_find_startcode(codecpar->extradata, end);

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

void CFileDemux::getMP4AuxSDPLine(char * buff, int size, int rtp_pt)
{
    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
    
    if (codecpar->extradata_size == 0)
    {
        return;
    }

    int i, offset = 0;

    offset += snprintf(buff, size, "a=fmtp:%d profile-level-id=%d;config=", rtp_pt, 1);

    for (i = 0; i < codecpar->extradata_size; ++i) 
    {
        offset += snprintf(buff+offset, size-offset, "%02X", codecpar->extradata[i]);
    }
}

void CFileDemux::getVideoAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_pVideoEncoder)
    {
        m_pVideoEncoder->getAuxSDPLine(buff, size, rtp_pt);
        return;
    }

    if (m_nVideoIndex < 0)
    {
        return;
    }

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
    
    if (codecpar->codec_id == AV_CODEC_ID_H264)
    {
        getH264AuxSDPLine(buff, size, rtp_pt);        
    }
    else if (codecpar->codec_id == AV_CODEC_ID_MPEG4)
    {
        getMP4AuxSDPLine(buff, size, rtp_pt);
    }
    else if (codecpar->codec_id == AV_CODEC_ID_HEVC)
    {
        getH265AuxSDPLine(buff, size, rtp_pt);
    }   
}

void CFileDemux::getAACAuxSDPLine(char * buff, int size, int rtp_pt)
{
    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nAudioIndex]->codecpar;
    
    if (codecpar->extradata_size == 0)
    {
        return;
    }

    ht_gen_aac_sdp_line(buff, size, rtp_pt, codecpar->extradata, codecpar->extradata_size);
}

void CFileDemux::getAudioAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (m_pAudioEncoder)
    {
        m_pAudioEncoder->getAuxSDPLine(buff, size, rtp_pt);
        return;
    }

    if (m_nAudioIndex < 0)
    {
        return;
    }

    AVCodecParameters * codecpar = m_pFormatContext->streams[m_nAudioIndex]->codecpar;
    
    if (codecpar->codec_id == AV_CODEC_ID_AAC)
    {
        getAACAuxSDPLine(buff, size, rtp_pt);        
    }
}

BOOL CFileDemux::seekStream(double pos)
{
    if (pos < 0 || pos * 1000 >= m_nDuration)
    {
        return FALSE;
    }

    if (pos * 1000 == m_nCurPos)
    {
        return TRUE;
    }

    int stream = -1;
    int64 seekpos = pos * 1000000;
    
    if (m_nVideoIndex >= 0)
    {
        stream = m_nVideoIndex;
    }    
    else if (m_nAudioIndex >= 0)
    {
        stream = m_nAudioIndex;
    }

    if (m_pFormatContext->start_time != AV_NOPTS_VALUE)
    {
        seekpos += m_pFormatContext->start_time;
    }

    if (stream >= 0)
    {
        AVRational q = {1, AV_TIME_BASE}; 
        
        seekpos = av_rescale_q(seekpos, q, m_pFormatContext->streams[stream]->time_base);
    }

    if (av_seek_frame(m_pFormatContext, stream, seekpos, AVSEEK_FLAG_BACKWARD) < 0)
    { 
        return FALSE;
    }

    // Accurate seek to the specified position
    AVPacket * pkt;
    
    pkt = av_packet_alloc();
    if (!pkt)
    {
        return FALSE;
    }

    pkt->data = 0;
    pkt->size = 0;
    
    while (av_read_frame(m_pFormatContext, pkt) == 0)
    {
        if (pkt->stream_index != stream)
        {
            av_packet_unref(pkt);
            continue;
        }
        
        if (pkt->pts != AV_NOPTS_VALUE)
        {
            if (pkt->pts < seekpos)
            {
                av_packet_unref(pkt);
                continue;
            }
            else
            {
                break;
            }
        }
        else if (pkt->dts != AV_NOPTS_VALUE)
        {
            if (pkt->dts < seekpos)
            {
                av_packet_unref(pkt);
                continue;
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
        
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    
    m_nCurPos = pos * 1000;

    return TRUE;
}

void CFileDemux::dataCallback(uint8 * data, int size, int type, int nbsamples, BOOL waitnext)
{
    if (m_pCallback)
    {
        if (DATA_TYPE_VIDEO == type && NULL == m_pVideoEncoder && m_pFormatContext && m_nVideoIndex >= 0)
        {
            AVCodecParameters * codecpar = m_pFormatContext->streams[m_nVideoIndex]->codecpar;
            
            if (codecpar->codec_id == AV_CODEC_ID_H264 && codecpar->extradata_size > 8)
            {
                uint8 naltype = avc_h264_nalu_type(data, size);

                if (H264_NAL_SPS == naltype || H264_NAL_PPS == naltype)
                {
                }
                else if (H264_NAL_IDR != m_nPreNalu && (H264_NAL_IDR == naltype || m_bVideoFirst))
                {
                    m_pCallback(codecpar->extradata, codecpar->extradata_size, type, 0, 0, m_pUserdata);
                }

                m_nPreNalu = naltype;
                m_bVideoFirst = FALSE;
            }
            else if (codecpar->codec_id == AV_CODEC_ID_HEVC && codecpar->extradata_size > 8)
            {
                uint8 naltype = avc_h265_nalu_type(data, size);

                if (HEVC_NAL_VPS == naltype || HEVC_NAL_SPS == naltype || HEVC_NAL_PPS == naltype)
                {
                }
                else if (!IS_IRAP_NAL(m_nPreNalu) && (IS_IRAP_NAL(naltype) || m_bVideoFirst))
                {
                    m_pCallback(codecpar->extradata, codecpar->extradata_size, type, 0, 0, m_pUserdata);
                }

                m_nPreNalu = naltype;
                m_bVideoFirst = FALSE;
            }
        }
        
        m_pCallback(data, size, type, nbsamples, waitnext, m_pUserdata);
    }
}

void CFileDemux::setCallback(DemuxCallback pCallback, void * pUserdata)
{
    m_pCallback = pCallback;
    m_pUserdata = pUserdata;
}



