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

#ifndef _FILE_DEMUX_H
#define _FILE_DEMUX_H

#include "audio_encoder.h"
#include "video_encoder.h"
#include "audio_decoder.h"
#include "video_decoder.h"

extern "C" 
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/avstring.h>
#include <libavutil/base64.h>
#include <libavcodec/bsf.h>
}

/**
 * callback function prototype
 *
 * @param data the encoded data
 * @param size the data size
 * @param type DATA_TYPE_AUDIO or DATA_TYPE_VIDEO
 * @param nbsamples audio sample numbers
 * @param pUserdata user data
 */
typedef void (*DemuxCallback)(uint8 * data, int size, int type, int nbsamples, BOOL waitnext, void * pUserdata);

class CFileDemux
{
public:
    CFileDemux(const char * filename, int loopnums = 1);
    ~CFileDemux();

    /**
     * Is there an audio stream?
     *
     * @return TURE has audio stream, FALSE hasn't audio stream
     */
    BOOL    hasAudio() {return m_nAudioIndex != -1;}
    /**
     * Is there an video stream?
     *
     * @return TURE has video stream, FALSE hasn't video stream
     */
    BOOL    hasVideo() {return m_nVideoIndex != -1;}

    /**
     * get video codec
     *
     * @return the video codec
     */
    enum AVCodecID getVideoCodec();
    
    /**
     * get video width
     *
     * @return if has video stream, the video width, or 0
     */
    int     getWidth();
    /**
     * get video height
     *
     * @return if has video stream, the video height, or 0
     */
    int     getHeight();
    /**
     * get video frame rate
     *
     * @return if has video stream, the video frame rate, or 0
     */
    double  getFramerate();

    /**
     * get audio codec
     *
     * @return the audio codec
     */
    enum AVCodecID getAudioCodec();
    
    /**
     * get audio sample rate
     *
     * @return if has audio stream, the audio sample rate, or 0
     */
    int     getSamplerate();
    /**
     * get audio channels numbers
     *
     * @return if has audio stream, the audio channels numbers, or 0
     */
    int     getChannels();

    /**
     * get stream duration
     *
     * @return the stream duration, unit is millisecond
     */
    int64   getDuration() {return m_nDuration;}

    /**
     * get stream current position
     *
     * @return the stream current position, unit is millisecond
     */
    int64   getCurPos() {return m_nCurPos;}
    
    /**
     * set audio output format
     *
     * @param codec audio output codec, see media_format.h
     * @param samplerate audio output samplerate
     * @param channels audio output channels
     * @return TRUE on success, FALSE on error
     */
    BOOL    setAudioFormat(int codec, int samplerate, int channels, int bitrate);
    /**
     * set video output format
     *
     * @param codec video output codec, see media_format.h
     * @param width video output width
     * @param height video output height
     * @param framerate video output framerate
     * @return TRUE on success, FALSE on error
     */
    BOOL    setVideoFormat(int codec, int width, int height, double framerate, int bitrate);
    /**
     * set data callback function
     *
     * @param pCallback callback functon pointer
     * @param pUserdata user data
     */
    void    setCallback(DemuxCallback pCallback, void * pUserdata);

    /**
     * read a frame, data will be transmitted to the user by callback
     *
     * @return TRUE on success, FALSE on error
     */
    BOOL    readFrame();
    /**
     * get video sdp line
     *
     * @param rtp_pt rtp payload type
     */
    void    getVideoAuxSDPLine(char * buff, int size, int rtp_pt);
    /**
     * get audio sdp line
     *
     * @param rtp_pt rtp payload type
     */
    void    getAudioAuxSDPLine(char * buff, int size, int rtp_pt);

    /**
     * seek stream
     *
     * @param pos seek position, unit is second
     * @return TRUE on success, FALSE on error
     */
    BOOL    seekStream(double pos);

    void    videoEncode(AVFrame * frame);
    void    audioEncode(AVFrame * frame);
    
    void    dataCallback(uint8 * data, int size, int type, int nbsamples, BOOL waitnext);
    
private:
    BOOL    init(const char * filename);
    void    uninit();
    
    void    audioDecode(AVPacket * pkt);
    void    videoDecode(AVPacket * pkt);
    void    videoData(uint8 * data, int size, uint64_t pts, int waitnext);
    
    void    getH264AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getMP4AuxSDPLine(char * buff, int size, int rtp_pt);
    void    getAACAuxSDPLine(char * buff, int size, int rtp_pt);
    void    getH265AuxSDPLine(char * buff, int size, int rtp_pt);
    
private:
    char                m_pFilename[1024];  // file name
    int                 m_nAudioIndex;      // audio stream index
    int                 m_nVideoIndex;      // video stream index
    int64               m_nDuration;        // the stream duration, unit is millisecond
    int64               m_nCurPos;          // the current play position, unit is millisecond
    
    int                 m_nNalLength;
    
    AVFormatContext   * m_pFormatContext;   // format context

    CVideoDecoder     * m_pVideoDecoder;    // video decoder
    CAudioDecoder     * m_pAudioDecoder;    // audio decoder
    CVideoEncoder     * m_pVideoEncoder;    // video encoder  
    CAudioEncoder     * m_pAudioEncoder;    // audio encoder

    uint8               m_nPreNalu;         // previous nalu type
    BOOL                m_bVideoFirst;      // first video flag 
    BOOL                m_bAudioFirst;      // first audio flag
    DemuxCallback       m_pCallback;        // callback function pointer
    void              * m_pUserdata;        // user data

    uint32              m_nLoopNums;        // current loop numbers
    uint32              m_nMaxLoopNums;     // max loop numbers
};

#endif // _FILE_DEMUX_H



