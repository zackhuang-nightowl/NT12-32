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
#include "window_capture_linux.h"
#include "media_format.h"
#include "lock.h"
#include "xcb_util.h"


/**************************************************************************************/


/**************************************************************************************/

void * windowCaptureThread(void * argv)
{
    CLWindowCapture *capture = (CLWindowCapture *)argv;

    capture->captureThread();

    return NULL;
}

/**************************************************************************************/

CLWindowCapture::CLWindowCapture()  : CWindowCapture()
{
    m_pConn = NULL;
    m_nWindow = 0;
}

CLWindowCapture::~CLWindowCapture()
{
    stopCapture();
}

CWindowCapture * CLWindowCapture::getInstance(char * title)
{
    CWindowCapture * pCapture = NULL;
    InstanceMap::iterator it;
    
    sys_os_mutex_enter(m_pInstMutex);

    it = m_InstanceMap.find(title);
    
    if (it == m_InstanceMap.end())
    {
        pCapture = (CWindowCapture *) new CLWindowCapture;
        if (pCapture)
        {
            pCapture->m_nRefCnt++;
            strcpy(pCapture->m_pTitle, title);

            m_InstanceMap[title] = pCapture;
        }
    }
    else
    {
        pCapture = (CWindowCapture *) it->second;
        pCapture->m_nRefCnt++;
    }
    
    sys_os_mutex_leave(m_pInstMutex);

    return pCapture;
}

void CLWindowCapture::listWindow()
{
    int screen_count;
    xcb_connection_t *conn;
    const xcb_setup_t *setup;

    conn = xcb_connect(NULL, NULL);

    if (xcb_connection_has_error(conn))
    {
        log_print(HT_LOG_ERR, "%s, Cannot open display %s\r\n", __FUNCTION__, "default");
        return;
    }

    printf("\r\nAvailable window name : \r\n\r\n");
    
    setup = xcb_get_setup(conn);
    screen_count = xcb_setup_roots_length(setup);
    
    for (int i = 0; i < screen_count; ++i) 
    {
        xcb_screen_t * screen = xcb_get_screen(setup, i);
        if (NULL == screen)
        {
            continue;
        }

        xcb_atom_t net_client_list = xcb_get_atom(conn, "_NET_CLIENT_LIST");
        xcb_get_property_cookie_t cookie;
        xcb_get_property_reply_t *reply;
        xcb_generic_error_t *e;

        cookie = xcb_get_property(conn, 0, screen->root, net_client_list, XCB_GET_PROPERTY_TYPE_ANY, 0, 100);
        reply = xcb_get_property_reply(conn, cookie, &e);
        if (e) 
        {
            log_print(HT_LOG_WARN, "%s, error: %d", __FUNCTION__, e->error_code);
            free(e);
            continue;
        }

        if (reply) 
        {
            int value_len = xcb_get_property_value_length(reply);
            if (value_len) 
            {
                xcb_window_t * win = (xcb_window_t *)xcb_get_property_value(reply);

                for (int j=0; j<value_len/4; j++)
                {
                    xcb_atom_t net_wm_name = xcb_get_atom(conn, "_NET_WM_NAME");
                    xcb_atom_t utf8_string = xcb_get_atom(conn, "UTF8_STRING");
                    
                    xcb_get_property_cookie_t cookie1 = xcb_get_property(conn, false, win[j], net_wm_name, utf8_string, 0, 1024);
                    xcb_get_property_reply_t *reply1 = xcb_get_property_reply(conn, cookie1, &e);
                    if (e) 
                    {
                        log_print(HT_LOG_WARN, "%s, error: %d", __FUNCTION__, e->error_code);
                        free(e);
                        continue;
                    }

                    if (reply1)
                    {
                        char * title = (char*)xcb_get_property_value(reply1);

                        printf("%s\r\n", title);

                        free(reply1);
                    }
                }
            }
            
            free(reply);
        }
    }

    xcb_disconnect(conn);
}

int CLWindowCapture::getPixfmt(int depth)
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
                    pix_fmt = AV_PIX_FMT_RGB32;
                break;
            case 24:
                if (fmt->bits_per_pixel == 32)
                    pix_fmt = AV_PIX_FMT_RGB32;
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

BOOL CLWindowCapture::initWindow()
{
    BOOL found = FALSE;
    int screen_count;
    xcb_window_t window;
    const xcb_setup_t *setup;

    setup = xcb_get_setup(m_pConn);
    screen_count = xcb_setup_roots_length(setup);
    
    for (int i = 0; i < screen_count; ++i) 
    {
        xcb_screen_t * screen = xcb_get_screen(setup, i);
        if (NULL == screen)
        {
            continue;
        }

        xcb_atom_t net_client_list = xcb_get_atom(m_pConn, "_NET_CLIENT_LIST");
        xcb_get_property_cookie_t cookie;
        xcb_get_property_reply_t *reply;
        xcb_generic_error_t *e;

        cookie = xcb_get_property(m_pConn, 0, screen->root, net_client_list, XCB_GET_PROPERTY_TYPE_ANY, 0, 100);
        reply = xcb_get_property_reply(m_pConn, cookie, &e);
        if (e) 
        {
            log_print(HT_LOG_WARN, "%s, error: %d", __FUNCTION__, e->error_code);
            free(e);
            continue;
        }

        if (reply) 
        {
            int value_len = xcb_get_property_value_length(reply);
            if (value_len) 
            {
                xcb_window_t * win = (xcb_window_t *)xcb_get_property_value(reply);

                for (int j=0; j<value_len/4; j++)
                {
                    xcb_atom_t net_wm_name = xcb_get_atom(m_pConn, "_NET_WM_NAME");
                    xcb_atom_t utf8_string = xcb_get_atom(m_pConn, "UTF8_STRING");
                    
                    xcb_get_property_cookie_t cookie1 = xcb_get_property(m_pConn, false, win[j], net_wm_name, utf8_string, 0, 1024);
                    xcb_get_property_reply_t *reply1 = xcb_get_property_reply(m_pConn, cookie1, &e);
                    if (e) 
                    {
                        log_print(HT_LOG_WARN, "%s, error: %d", __FUNCTION__, e->error_code);
                        free(e);
                        continue;
                    }

                    if (reply1)
                    {
                        char * title = (char*)xcb_get_property_value(reply1);

                        if (strncasecmp(title, m_pTitle, strlen(m_pTitle)) == 0)
                        {
                            free(reply1);
                            window = win[j];
                            found = TRUE;
                            break;
                        }

                        free(reply1);
                    }
                }
            }
            
            free(reply);
        }

        if (found)
        {
            break;
        }
    }

    if (!found)
    {
        log_print(HT_LOG_ERR, "%s, not found window, %s\r\n", __FUNCTION__, m_pTitle);
        return FALSE;
    }

    m_nWindow = window;

    return TRUE;
}

BOOL CLWindowCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_pConn = xcb_connect(NULL, NULL);

    if (xcb_connection_has_error(m_pConn))
    {
        log_print(HT_LOG_ERR, "%s, Cannot open display %s\r\n", __FUNCTION__, "default");
        return FALSE;
    }

    if (!initWindow())
    {
        return FALSE;
    }
    
    xcb_get_geometry_cookie_t gc;
    xcb_get_geometry_reply_t *geo;

    gc  = xcb_get_geometry(m_pConn, m_nWindow);
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

BOOL CLWindowCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)windowCaptureThread, this);

    return m_hCapture ? TRUE : FALSE;
}

void CLWindowCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    if (m_pConn)
    {
        xcb_disconnect(m_pConn);
        m_pConn = NULL;
    }

    m_nWindow = 0;

    m_bInited = FALSE;
}

BOOL CLWindowCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }
    
    BOOL ret = FALSE;
    xcb_get_image_cookie_t iq;
    xcb_get_image_reply_t *img;
    xcb_drawable_t drawable = m_nWindow;
    xcb_generic_error_t *e = NULL;

    xcb_get_geometry_cookie_t gc;
    xcb_get_geometry_reply_t *geo;

    gc  = xcb_get_geometry(m_pConn, m_nWindow);
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

void CLWindowCapture::captureThread()
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




