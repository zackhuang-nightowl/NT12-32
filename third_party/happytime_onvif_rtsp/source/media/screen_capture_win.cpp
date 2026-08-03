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
#include "screen_capture_win.h"
#include "media_format.h"
#include "media_codec.h"
#include "lock.h"
#include "win/win_os.h"
#include <string>
#include <vector>

/**************************************************************************************/

typedef struct
{
    HMONITOR        hmonitor;
    std::wstring    name;
} Monitor;

/**************************************************************************************/


void * screenCaptureThread(void * argv)
{
    CWScreenCapture *capture = (CWScreenCapture *)argv;

    capture->captureThread();

    return NULL;
}

BOOL WINAPI enumMonitorProc(HMONITOR hmonitor, HDC hdc, LPRECT lprc, LPARAM data) 
{
    MONITORINFOEX info_ex;  
    info_ex.cbSize = sizeof(MONITORINFOEX);
    
    GetMonitorInfo(hmonitor, &info_ex);
    
    if (info_ex.dwFlags == DISPLAY_DEVICE_MIRRORING_DRIVER)
    {
        return true;
    }
    
    std::vector<Monitor> * monitors = ((std::vector<Monitor> *)data);
    Monitor monitor;

    monitor.hmonitor = hmonitor;
    monitor.name = info_ex.szDevice;  

    monitors->push_back(monitor);  

    return true;
}

void wgc_screen_cb(int fmt, int w, int h, int stride, uint8 * data, void * user)
{
    CWScreenCapture * pthis = (CWScreenCapture *) user;

    pthis->wgcCallback(fmt, w, h, stride, data);
}

CWScreenCapture::CWScreenCapture()  : CScreenCapture()
, m_dcDesktop(NULL)
, m_dcMemory(NULL)
, m_hBitmap(NULL)
, m_hDib(NULL)
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
            m_wgcModule.init_monitor = (wgc_init_monitor) GetProcAddress(m_wgcModule.module, "wgc_init_monitor");
            m_wgcModule.uninit = (wgc_uninit) GetProcAddress(m_wgcModule.module, "wgc_uninit");
            m_wgcModule.start = (wgc_start) GetProcAddress(m_wgcModule.module, "wgc_start");
            m_wgcModule.set_callback = (wgc_set_callback) GetProcAddress(m_wgcModule.module, "wgc_set_callback");
        }
    }
}

CWScreenCapture::~CWScreenCapture()
{
    stopCapture();
}

CScreenCapture * CWScreenCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_MONITOR_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CScreenCapture *) new CWScreenCapture;
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

int CWScreenCapture::getDeviceNums()
{
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(NULL, NULL, enumMonitorProc, (LPARAM)&monitors);
    
    return monitors.size() > MAX_MONITOR_NUMS ? MAX_MONITOR_NUMS : monitors.size();
}

BOOL CWScreenCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    int updown = 0;
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(NULL, NULL, enumMonitorProc, (LPARAM)&monitors);

    if ((int) monitors.size() <= m_nDevIndex || m_nDevIndex < 0)
    {
        return FALSE;
    }

    DEVMODE mode;
    memset(&mode, 0, sizeof(mode));

    mode.dmSize = sizeof(mode);
    
    if (!EnumDisplaySettings(monitors[m_nDevIndex].name.c_str(), ENUM_CURRENT_SETTINGS, &mode))
    {
        log_print(HT_LOG_ERR, "%s, EnumDisplaySettings failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_nWidth = mode.dmPelsWidth;
    m_nHeight = mode.dmPelsHeight;
    m_nPixfmt = to_avpixelformat(VIDEO_FMT_BGRA);

    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    if (m_wgcModule.init_monitor)
    {
        m_wgc = m_wgcModule.init_monitor(monitors[m_nDevIndex].hmonitor);

        if (m_wgc && m_wgcModule.set_callback)
        {
            m_wgcModule.set_callback(m_wgc, wgc_screen_cb, this);
        }
    }
    else
    {
        m_dcDesktop = CreateDC(NULL, monitors[m_nDevIndex].name.c_str(), NULL, NULL);
        if (NULL == m_dcDesktop)
        {
            return FALSE;
        }
        
        m_dcMemory = CreateCompatibleDC(m_dcDesktop);
        if (NULL == m_dcMemory)
        {
            return FALSE;
        }

        m_hBitmap = CreateCompatibleBitmap(m_dcDesktop, m_nWidth, m_nHeight);
        if (NULL == m_hBitmap)
        {
            return FALSE;
        }

        DWORD dwBmpSize = ((m_nWidth * 32 + 31) / 32) * 4 * m_nHeight;
        
        m_hDib = GlobalAlloc(GHND, dwBmpSize); 
        if (NULL == m_hDib)
        {
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
        return FALSE;
    }

    m_bInited = TRUE;
    
    return TRUE;
}

BOOL CWScreenCapture::startCapture()
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
        m_hCapture = sys_os_create_thread((void *)screenCaptureThread, this);

        ret = (NULL != m_hCapture);
    }
    
    return ret;
}

void CWScreenCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    sys_os_wait_thread(&m_hCapture);

    if (m_dcDesktop)
    {
        DeleteDC(m_dcDesktop);
        m_dcDesktop = NULL;
    }

    if (m_dcMemory)
    {
        DeleteDC(m_dcMemory);
        m_dcMemory = NULL;
    }

    if (m_hBitmap)
    {
        DeleteObject(m_hBitmap);
        m_hBitmap = NULL;
    }

    if (m_hDib)
    {
        GlobalFree(m_hDib);
        m_hDib = NULL;
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

void CWScreenCapture::captureCursor()
{
    CURSORINFO ci = {0};
    
    ci.cbSize = sizeof(ci);

    if (GetCursorInfo(&ci))
    {
        HCURSOR icon = CopyCursor(ci.hCursor);
        ICONINFO info;
        POINT pos;
        int vertres = GetDeviceCaps(m_dcDesktop, VERTRES);
        int desktopvertres = GetDeviceCaps(m_dcDesktop, DESKTOPVERTRES);
        info.hbmMask = NULL;
        info.hbmColor = NULL;

        if (ci.flags != CURSOR_SHOWING)
            return;

        if (!icon) 
        {
            /* Use the standard arrow cursor as a fallback.
             * You'll probably only hit this in Wine, which can't fetch
             * the current system cursor. */
            icon = CopyCursor(LoadCursor(NULL, IDC_ARROW));
        }

        if (!GetIconInfo(icon, &info)) 
        {
            log_print(HT_LOG_ERR, "%s, could not get icon info\r\n", __FUNCTION__);
            goto icon_error;
        }

        GetCursorPos(&pos);
        
        pos.x = pos.x - info.xHotspot;
        pos.y = pos.y - info.yHotspot;

        // that would keep the correct location of mouse with hidpi screens
        pos.x = pos.x * desktopvertres / vertres;
        pos.y = pos.y * desktopvertres / vertres;

        if (!DrawIconEx(m_dcMemory, pos.x, pos.y, icon, 0, 0, 0, NULL, DI_NORMAL|DI_COMPAT|DI_DEFAULTSIZE))
        {
            log_print(HT_LOG_ERR, "%s, couldn't draw icon\r\n", __FUNCTION__);
        }

icon_error:

        if (info.hbmMask)
        {
            DeleteObject(info.hbmMask);
        }
        
        if (info.hbmColor)
        {
            DeleteObject(info.hbmColor);
        }
        
        if (icon)
        {
            DestroyCursor(icon);
        }    
    } 
    else 
    {
        log_print(HT_LOG_ERR, "%s, couldn't get cursor info, %d\r\n", 
            __FUNCTION__, GetLastError());
    }
}

BOOL CWScreenCapture::capture()
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

    HGDIOBJ old_bitmap = SelectObject(m_dcMemory, m_hBitmap);

    BitBlt(m_dcMemory, 0, 0, m_nWidth, m_nHeight, m_dcDesktop, 0, 0, SRCCOPY | CAPTUREBLT); 

    captureCursor();
    
    if (old_bitmap != NULL) 
    {
        SelectObject(m_dcMemory, old_bitmap);
    }
      
    BITMAP bmpScreen;    
    GetObject(m_hBitmap, sizeof(BITMAP), &bmpScreen);

    if (bmpScreen.bmBitsPixel != 32)
    {
        log_print(HT_LOG_ERR, "%s, bmBitsPixel != 32\r\n", __FUNCTION__);
        return FALSE;
    }
    
    BITMAPINFOHEADER bi;
     
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpScreen.bmWidth;    
    bi.biHeight = bmpScreen.bmHeight;  
    bi.biPlanes = 1;    
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;      
    bi.biSizeImage = 0;  
    bi.biXPelsPerMeter = 0;    
    bi.biYPelsPerMeter = 0;    
    bi.biClrUsed = 0;     
    bi.biClrImportant = 0;

    BOOL ret = FALSE;
    uint8 *lpbitmap = (uint8 *)GlobalLock(m_hDib); 
    
    GetDIBits(m_dcDesktop, m_hBitmap, 0, (UINT)bmpScreen.bmHeight, lpbitmap, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    AVFrame frame;
    memset(&frame, 0, sizeof(frame));

    frame.data[0] = lpbitmap;
    frame.linesize[0] = bmpScreen.bmWidthBytes;
    frame.extended_data = frame.data;
    frame.width = bmpScreen.bmWidth;
    frame.height = bmpScreen.bmHeight;
    frame.format = m_nPixfmt;
    
    ret = m_encoder.encode(&frame);

    GlobalUnlock(m_hDib);

    return ret;
}

void CWScreenCapture::captureThread()
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

void CWScreenCapture::wgcCallback(int fmt, int w, int h, int stride, uint8 * data)
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



