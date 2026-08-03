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

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include "sys_inc.h"
#include "media_format.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include <libavutil/opt.h>
}

#define HW_DECODING_AUTO            0       // automatic select video acceleration hardware
#define HW_DECODING_D3D11           1       // D3D11 video acceleration
#define HW_DECODING_DXVA            2       // DXVA  video acceleration
#define HW_DECODING_VAAPI           3       // VAAPI video acceleration
#define HW_DECODING_OPENCL          4       // OPENCL video acceleration
#define HW_DECODING_VIDEOTOOLBOX    5       // VideoToolBox video acceleration
#define HW_DECODING_MEDIACODEC      6       // MediaCodec video acceleration
#define HW_DECODING_DISABLE         -1      // disable  video acceleration

typedef void (*VDCB)(AVFrame * frame, void *pUserdata);

class CVideoDecoder
{
public:
    CVideoDecoder();
    ~CVideoDecoder();

public:
    BOOL    init(int codec, uint8 * extradata = NULL, int extradata_size = 0, int hwMode = HW_DECODING_AUTO);
    BOOL    init(enum AVCodecID codec, uint8 * extradata = NULL, int extradata_size = 0, int hwMode = HW_DECODING_AUTO);
    void    uninit();

    int     getWidth();
    int     getHeight();
    double  getFrameRate();
    
    BOOL    decode(uint8 * data, int len, int64_t pts = AV_NOPTS_VALUE);
    BOOL    decode(AVPacket *pkt);
    void    setCallback(VDCB pCallback, void * pUserdata) {m_pCallback = pCallback; m_pUserdata = pUserdata;}
    BOOL    getHWFormat(AVCodecContext *ctx, const AVPixelFormat *pix_fmts, AVPixelFormat *dst);
    
private:
    BOOL    readFrame();
    int     render(AVFrame * frame);
    void    flush();
    int     hwDecoderInit(AVCodecContext *ctx, int hwMode);
    
private:
    BOOL            m_bInited;
    
    const AVCodec * m_pCodec;
    AVCodecContext* m_pContext;
    AVFrame       * m_pFrame;
    AVFrame       * m_pSoftFrame;

    VDCB            m_pCallback;
    void          * m_pUserdata;

    AVPixelFormat   m_hwPixFmt;
    AVBufferRef   * m_pHWDeviceCtx;
};

#endif


