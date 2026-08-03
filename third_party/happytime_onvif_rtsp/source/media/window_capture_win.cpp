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
#include "window_capture_win.h"
#include "media_format.h"
#include "media_codec.h"
#include "lock.h"
#include <dwmapi.h>
#include "win/win_os.h"

/**************************************************************************************/

#pragma comment(lib, "dwmapi.lib")

/**************************************************************************************/

void * windowCaptureThread(void * argv)
{
    CWWindowCapture *capture = (CWWindowCapture *)argv;

    capture->captureThread();

    return NULL;
}

void wgc_window_cb(int fmt, int w, int h, int stride, uint8 * data, void * user)
{
    CWWindowCapture * pthis = (CWWindowCapture *) user;

    pthis->wgcCallback(fmt, w, h, stride, data);
}

CWWindowCapture::CWWindowCapture() : CWindowCapture()
, m_hWnd(NULL)
, m_dcWindow(NULL)
, m_dcMemory(NULL)
, m_wgcSupported(FALSE)
, m_wgc(NULL)
{
    win_version ver;
    win_version win1903 = {10, 0, 18362, 0};
    
    win_get_version(&ver);

    memset(&m_wgcModule, 0, sizeof(WGCMODULE));
    
    m_wgcSupported = win_version_compare(&ver, &win1903) >= 0;
    if (m_wgcSupported)
    {
        m_wgcModule.module = LoadLibrary(L"wgccapture.dll");
        if (m_wgcModule.module)
        {
            m_wgcModule.init_window = (wgc_init_window) GetProcAddress(m_wgcModule.module, "wgc_init_window");
            m_wgcModule.uninit = (wgc_uninit) GetProcAddress(m_wgcModule.module, "wgc_uninit");
            m_wgcModule.start = (wgc_start) GetProcAddress(m_wgcModule.module, "wgc_start");
            m_wgcModule.set_callback = (wgc_set_callback) GetProcAddress(m_wgcModule.module, "wgc_set_callback");
        }
    }
}

CWWindowCapture::~CWWindowCapture()
{
    stopCapture();
}

CWindowCapture * CWWindowCapture::getInstance(char * title)
{
    CWindowCapture * pCapture = NULL;
    InstanceMap::iterator it;
    
    sys_os_mutex_enter(m_pInstMutex);

    it = m_InstanceMap.find(title);
    
    if (it == m_InstanceMap.end())
    {
        pCapture = (CWindowCapture *) new CWWindowCapture;
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

void CWWindowCapture::listWindow()
{
    HWND desktop = GetDesktopWindow();
    HWND window = GetWindow(desktop, GW_CHILD);
    char title[256];
    RECT rect;

    printf("\r\nAvailable window name : \r\n\r\n");
    
    while (window)
    {
        DWORD styles = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);
        if (styles & WS_EX_TOOLWINDOW) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
        if (styles & WS_CHILD) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        if (!IsWindowVisible(window))
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        // Exclude minimized windows
        if (IsIconic(window))
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));

        if (rect.bottom - rect.top == 0 || rect.right - rect.left == 0) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        // Exclude windows that are invisible to the user
        
        DWORD flag = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &flag, sizeof(flag));
        if (flag)
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        GetWindowTextA(window, title, sizeof(title));

        if (title[0] == '\0')
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        printf("%s\r\n", title);
        
        window = GetNextWindow(window, GW_HWNDNEXT);
    }
}

BOOL CWWindowCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    HWND desktop = GetDesktopWindow();
    HWND window = GetWindow(desktop, GW_CHILD);
    char title[256];
    int  updown = 0;
    BOOL found = FALSE;
    RECT rect;
    
    while (window)
    {
        DWORD styles = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);
        if (styles & WS_EX_TOOLWINDOW) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
        if (styles & WS_CHILD) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        if (!IsWindowVisible(window))
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        // Exclude minimized windows
        if (IsIconic(window))
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));

        if (rect.bottom - rect.top == 0 || rect.right - rect.left == 0) 
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        // Exclude windows that are invisible to the user
        
        DWORD flag = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &flag, sizeof(flag));
        if (flag)
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        GetWindowTextA(window, title, sizeof(title));

        if (title[0] == '\0')
        {
            window = GetNextWindow(window, GW_HWNDNEXT);
            continue;
        }

        if (strncasecmp(title, m_pTitle, strlen(m_pTitle)) == 0)
        {
            found = TRUE;
            break;
        }
        
        window = GetNextWindow(window, GW_HWNDNEXT);
    }

    if (!found)
    {
        log_print(HT_LOG_ERR, "%s, not found window, %s\r\n", __FUNCTION__, m_pTitle);
        return FALSE;
    }

    m_hWnd = window;
    m_nWidth = rect.right - rect.left;
    m_nHeight = rect.bottom - rect.top;
    m_nPixfmt = to_avpixelformat(VIDEO_FMT_BGRA);
    
    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    if (m_wgcModule.init_window)
    {
        m_wgc = m_wgcModule.init_window(m_hWnd);

        if (m_wgc && m_wgcModule.set_callback)
        {
            m_wgcModule.set_callback(m_wgc, wgc_window_cb, this);
        }
    }
    else
    {
        m_dcWindow = GetDC(window);
        if (NULL == m_dcWindow)
        {
            log_print(HT_LOG_ERR, "%s, GetDC failed\r\n", __FUNCTION__);
            return FALSE;
        }
        
        m_dcMemory = CreateCompatibleDC(m_dcWindow);
        if (NULL == m_dcMemory)
        {
            log_print(HT_LOG_ERR, "%s, CreateCompatibleDC failed\r\n", __FUNCTION__);
            return FALSE;
        }

        updown = 1;
    }

    VideoEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcWidth = m_nWidth;
    params.SrcHeight = m_nHeight;
    params.SrcPixFmt = (AVPixelFormat) m_nPixfmt;
    params.Updown = updown;
    params.DstCodec = codec;
    params.DstWidth = width ? width : m_nWidth;;
    params.DstHeight = height ? height : m_nHeight;
    params.DstFramerate = framerate;
    params.DstBitrate = bitrate;
    
    if (m_encoder.init(&params) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init encoder failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_bInited = TRUE;
    
    return TRUE;
}

BOOL CWWindowCapture::startCapture()
{
    BOOL ret;
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;

    if (m_wgc && m_wgcModule.start)
    {
        ret = m_wgcModule.start(m_wgc);
    }
    else
    {
        m_hCapture = sys_os_create_thread((void *)windowCaptureThread, this);

        ret = (NULL != m_hCapture);
    }
    
    return ret;
}

void CWWindowCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    if (m_dcWindow)
    {
        ReleaseDC(m_hWnd, m_dcWindow);
        m_dcWindow = NULL;
    }

    if (m_dcMemory)
    {
        DeleteDC(m_dcMemory);
        m_dcMemory = NULL;
    }

    if (m_wgcModule.uninit && m_wgc)
    {
        m_wgcModule.uninit(m_wgc);
        m_wgc = NULL;
    }

    if (m_wgcModule.module)
    {
        FreeLibrary(m_wgcModule.module);
        
        memset(&m_wgcModule, 0, sizeof(WGCMODULE));
    }

    m_bInited = FALSE;
}

BOOL CWWindowCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }

    // Request that the system not power-down the system, or the display hardware.
    if (!SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)) 
    {
        log_print(HT_LOG_WARN, "Failed to make system & display power assertion\r\n");
    }

    RECT rect;
    memset(&rect, 0, sizeof(rect));
    
    DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    
    if (w == 0 || h == 0) 
    {
        return FALSE;
    }

    HBITMAP hBitmap = NULL;    
    HANDLE  hDib = NULL;
    
    hBitmap = CreateCompatibleBitmap(m_dcWindow, w, h);
    if (NULL == hBitmap)
    {
        log_print(HT_LOG_ERR, "%s, CreateCompatibleBitmap failed\r\n", __FUNCTION__);
        return FALSE;
    }

    DWORD dwBmpSize = ((w * 32 + 31) / 32) * 4 * h;
    
    hDib = GlobalAlloc(GHND, dwBmpSize); 
    if (NULL == hDib)
    {
        DeleteObject(hBitmap);
        log_print(HT_LOG_ERR, "%s, GlobalAlloc failed\r\n", __FUNCTION__);
        return FALSE;
    }        

    HGDIOBJ old_bitmap = SelectObject(m_dcMemory, hBitmap);

    BitBlt(m_dcMemory, 0, 0, w, h, m_dcWindow, 0, 0, SRCCOPY | CAPTUREBLT); 
    
    if (old_bitmap != NULL) 
    {
        SelectObject(m_dcMemory, old_bitmap);
    }
      
    BITMAP bmpWindow;    
    GetObject(hBitmap, sizeof(BITMAP), &bmpWindow);

    if (bmpWindow.bmBitsPixel != 32)
    {
        DeleteObject(hBitmap);
        GlobalFree(hDib);
        log_print(HT_LOG_ERR, "%s, bmBitsPixel != 32\r\n", __FUNCTION__);
        return FALSE;
    }
    
    BITMAPINFOHEADER bi;
     
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpWindow.bmWidth;    
    bi.biHeight = bmpWindow.bmHeight;  
    bi.biPlanes = 1;    
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;      
    bi.biSizeImage = 0;  
    bi.biXPelsPerMeter = 0;    
    bi.biYPelsPerMeter = 0;    
    bi.biClrUsed = 0;     
    bi.biClrImportant = 0;

    BOOL ret = FALSE;
    uint8 *lpbitmap = (uint8 *)GlobalLock(hDib); 
    
    GetDIBits(m_dcWindow, hBitmap, 0, (UINT)bmpWindow.bmHeight, lpbitmap, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    AVFrame frame;
    memset(&frame, 0, sizeof(frame));

    frame.data[0] = lpbitmap;
    frame.linesize[0] = bmpWindow.bmWidthBytes;
    frame.extended_data = frame.data;
    frame.width = bmpWindow.bmWidth;
    frame.height = bmpWindow.bmHeight;
    frame.format = m_nPixfmt;
    
    ret = m_encoder.encode(&frame);

    GlobalUnlock(hDib);

    DeleteObject(hBitmap);
    GlobalFree(hDib);
    
    return ret;
}

void CWWindowCapture::captureThread()
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

void CWWindowCapture::wgcCallback(int fmt, int w, int h, int stride, uint8 * data)
{
    AVFrame frame;
    memset(&frame, 0, sizeof(frame));

    frame.data[0] = data;
    frame.linesize[0] = stride;
    frame.extended_data = frame.data;
    frame.width = w;
    frame.height = h;
    frame.format = m_nPixfmt;
    
    m_encoder.encode(&frame);
}



