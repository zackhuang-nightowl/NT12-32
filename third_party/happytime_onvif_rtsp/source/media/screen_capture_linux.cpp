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
#include "screen_capture_linux.h"
#include "media_format.h"
#include "lock.h"
#include "xcb_util.h"


/**************************************************************************************/

void * screenCaptureThread(void * argv)
{
    CLScreenCapture *capture = (CLScreenCapture *)argv;

    capture->captureThread();

    return NULL;
}

/**************************************************************************************/

CLScreenCapture::CLScreenCapture()  : CScreenCapture()
{
    m_pConn = NULL;
    m_pScreen = NULL;
}

CLScreenCapture::~CLScreenCapture()
{
    stopCapture();
}

CScreenCapture * CLScreenCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_MONITOR_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CScreenCapture *) new CLScreenCapture;
        if (m_pInstance[devid])
        {
            m_pInstance[devid]->m_nRefCnt++;
            m_pInstance[devid]->m_nDevIndex = devid;
        }
    }
    else
    {
        m_pInstance[devid]->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return m_pInstance[devid];
}

int CLScreenCapture::getDeviceNums()
{
    int count = 0;
    xcb_connection_t * c = xcb_connect(NULL, NULL);
    const xcb_setup_t * setup;

    if (xcb_connection_has_error(c))
    {
        log_print(HT_LOG_ERR, "%s, Cannot open display %s, error %d\r\n", __FUNCTION__, "default");
        return 0;
    }

    setup = xcb_get_setup(c);
    
    count = xcb_setup_roots_length(setup);

    xcb_disconnect(c);
    
    return count > MAX_MONITOR_NUMS ? MAX_MONITOR_NUMS : count;
}

int CLScreenCapture::getPixfmt(int depth)
{
    const xcb_setup_t *setup = xcb_get_setup(m_pConn);
    const xcb_format_t *fmt  = xcb_setup_pixmap_formats(setup);
    int length               = xcb_setup_pixmap_formats_length(setup);

    int pix_fmt = 0;

    while (length--) 
    {
        if (fmt->depth == depth) 
        {
            switch (depth) 
            {
            case 32:
                if (fmt->bits_per_pixel == 32)
                    pix_fmt = AV_PIX_FMT_0RGB;
                break;
            case 24:
                if (fmt->bits_per_pixel == 32)
                    pix_fmt = AV_PIX_FMT_0RGB32;
                else if (fmt->bits_per_pixel == 24)
                    pix_fmt = AV_PIX_FMT_RGB24;
                break;
            case 16:
                if (fmt->bits_per_pixel == 16)
                    pix_fmt = AV_PIX_FMT_RGB565;
                break;
            case 15:
                if (fmt->bits_per_pixel == 16)
                    pix_fmt = AV_PIX_FMT_RGB555;
                break;
            case 8:
                if (fmt->bits_per_pixel == 8)
                    pix_fmt = AV_PIX_FMT_RGB8;
                break;
            }
        }

        if (pix_fmt) 
        {
            return pix_fmt;
        }

        fmt++;
    }

    return AV_PIX_FMT_NONE;
}

BOOL CLScreenCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    const xcb_setup_t *setup;
    
    m_pConn = xcb_connect(NULL, NULL);

    if (xcb_connection_has_error(m_pConn))
    {
        log_print(HT_LOG_ERR, "%s, Cannot open display %s\r\n", __FUNCTION__, "default");
        return FALSE;
    }

    setup = xcb_get_setup(m_pConn);

    m_pScreen = xcb_get_screen(setup, m_nDevIndex);
    if (!m_pScreen) 
    {
        xcb_disconnect(m_pConn);
        m_pConn = NULL;
        
        log_print(HT_LOG_ERR, "%s, The screen %d does not exist\r\n", __FUNCTION__, m_nDevIndex);
        return FALSE;
    }

    xcb_get_geometry_cookie_t gc;
    xcb_get_geometry_reply_t *geo;

    gc  = xcb_get_geometry(m_pConn, m_pScreen->root);
    geo = xcb_get_geometry_reply(m_pConn, gc, NULL);
    if (!geo)
    {
        xcb_disconnect(m_pConn);
        m_pConn = NULL;
        
        log_print(HT_LOG_ERR, "%s, xcb_get_geometry_reply failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nWidth = geo->width;
    m_nHeight = geo->height;
    m_nPixfmt = getPixfmt(geo->depth);

    m_nFramerate = framerate;
    m_nBitrate = bitrate;
    
    VideoEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcWidth = m_nWidth;
    params.SrcHeight = m_nHeight;
    params.SrcPixFmt = (AVPixelFormat) m_nPixfmt;
    params.DstCodec = codec;
    params.DstWidth = width ? width : m_nWidth;
    params.DstHeight = height ? height : m_nHeight;
    params.DstFramerate = framerate;
    params.DstBitrate = bitrate;

    free(geo);
    
    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }
    
    m_bInited = TRUE;
       
    return TRUE;
}

BOOL CLScreenCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)screenCaptureThread, this);

    return m_hCapture ? TRUE : FALSE;
}

void CLScreenCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    if (m_pConn)
    {
        xcb_disconnect(m_pConn);
        m_pConn = NULL;
    }

    m_pScreen = NULL;

    m_bInited = FALSE;
}

BOOL CLScreenCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }
    
    BOOL ret = FALSE;
    xcb_get_image_cookie_t iq;
    xcb_get_image_reply_t *img;
    xcb_drawable_t drawable = m_pScreen->root;
    xcb_generic_error_t *e = NULL;

    xcb_get_geometry_cookie_t gc;
    xcb_get_geometry_reply_t *geo;

    gc  = xcb_get_geometry(m_pConn, m_pScreen->root);
    geo = xcb_get_geometry_reply(m_pConn, gc, NULL);
    if (!geo)
    {
        log_print(HT_LOG_ERR, "%s, xcb_get_geometry_reply failed\r\n", __FUNCTION__);
        return FALSE;
    }

    int w = geo->width;
    int h = geo->height;

    iq = xcb_get_image(m_pConn, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable, 0, 0, w, h, ~0);

    img = xcb_get_image_reply(m_pConn, iq, &e);

    if (e) 
    {
        log_print(HT_LOG_ERR, 
           "Cannot get the image data "
           "event_error: response_type:%u error_code:%u "
           "sequence:%u resource_id:%u minor_code:%u major_code:%u\r\n",
           e->response_type, e->error_code,
           e->sequence, e->resource_id, e->minor_code, e->major_code);
        free(e);
        free(geo);
        return FALSE;
    }

    if (!img)
    {
        free(geo);
        return FALSE;
    }

    AVFrame frame;
    memset(&frame, 0, sizeof(frame));

    frame.data[0] = xcb_get_image_data(img);
    frame.linesize[0] = av_image_get_linesize((AVPixelFormat)m_nPixfmt, w, 0);
    frame.extended_data = frame.data;
    frame.width = w;
    frame.height = h;
    frame.format = m_nPixfmt;

    ret = m_encoder.encode(&frame);

    free(img);
    free(geo);
    
    return ret;
}

void CLScreenCapture::captureThread()
{    
    int64 cur_delay = 0;
    int64 pre_delay = 0;
    int timeout = 1000000.0 / m_nFramerate;
    uint32 cur_time = 0;
    uint32 pre_time = 0;
        
    while (m_bCapture)
    {
        if (capture())
        {
            cur_time = sys_os_get_ms();
            cur_delay = timeout;

            if (pre_time > 0)
            {
                cur_delay += pre_delay - (cur_time - pre_time) * 1000;
                if (cur_delay < 1000)
                {
                    cur_delay = 0;
                }
            }

            pre_time = cur_time;
            pre_delay = cur_delay;
            
            if (cur_delay > 0)
            {
                usleep(cur_delay);
            }
        }
        else
        {
            usleep(10*1000);
        }
    }
}




