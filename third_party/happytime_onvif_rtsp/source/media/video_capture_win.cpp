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
#include "video_capture_win.h"
#include "media_format.h"
#include "media_codec.h"
#include "lock.h"
#include <mfidl.h>
#include <mfapi.h>
#include <locale.h>


/***************************************************************************************/

struct mf_fmt_map 
{
    int  ht_fmt;
    GUID mf_fmt;
};

const struct mf_fmt_map mf_fmt_table[] = 
{
    { VIDEO_FMT_YUV420P, MFVideoFormat_IYUV },
    { VIDEO_FMT_YUYV422, MFVideoFormat_YUY2 },
    { VIDEO_FMT_YVYU422, MFVideoFormat_YVYU },
    { VIDEO_FMT_UYVY422, MFVideoFormat_UYVY },
    { VIDEO_FMT_YUV420P, MFVideoFormat_YV12 },
    { VIDEO_FMT_NV12,    MFVideoFormat_NV12 },
    { VIDEO_FMT_RGB24,   MFVideoFormat_RGB24 },
    { VIDEO_FMT_RGB32,   MFVideoFormat_RGB32 },
    { VIDEO_FMT_ARGB,    MFVideoFormat_ARGB32 },
    { VIDEO_FMT_NONE,    MFVideoFormat_Base },
};

BOOL mf_fmt_is_support(GUID guid)
{
    int i;

    for (i = 0; mf_fmt_table[i].ht_fmt != VIDEO_FMT_NONE; i++) 
    {
        if (mf_fmt_table[i].mf_fmt == guid)
        {
            return TRUE;
        }
    }

    return FALSE;
}

int mf_fmt_to_ht_fmt(GUID guid)
{
    int i;

    for (i = 0; mf_fmt_table[i].ht_fmt != VIDEO_FMT_NONE; i++) 
    {
        if (mf_fmt_table[i].mf_fmt == guid)
        {
            return mf_fmt_table[i].ht_fmt;
        }
    }

    return VIDEO_FMT_NONE;
}

/***************************************************************************************/

static void * videoCaptureThread(void * argv)
{
    CWVideoCapture *capture = (CWVideoCapture *)argv;

    capture->captureThread();

    return NULL;
}

/***************************************************************************************/

CWVideoCapture::CWVideoCapture(void) : CVideoCapture()
{
    m_pSource = NULL;
    m_pReader = NULL;
}

CWVideoCapture::~CWVideoCapture(void)
{
    stopCapture();
}

CVideoCapture * CWVideoCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_VIDEO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CVideoCapture *)new CWVideoCapture;
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

int CWVideoCapture::getDeviceNums()
{
    int i, count = 0;
    IMFAttributes * pAttributes = NULL;
    IMFActivate ** ppDevices = NULL;
    
    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, (UINT*)&count);
    if (FAILED(hr))
    {
        goto done;
    }

done:

    if (pAttributes)
    {
        pAttributes->Release();
    }

    for (i = 0; i < count; i++)
    {
        ppDevices[i]->Release();
    }
    
    CoTaskMemFree(ppDevices);
    
    return count > MAX_VIDEO_DEV_NUMS ? MAX_VIDEO_DEV_NUMS : count;
}

void CWVideoCapture::listDevice()
{
    int i, count = 0;
    IMFAttributes * pAttributes = NULL;
    IMFActivate ** ppDevices = NULL;
    
    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, (UINT*)&count);
    if (FAILED(hr))
    {
        goto done;
    }

done:

    if (pAttributes)
    {
        pAttributes->Release();
    }

    printf("\r\nAvailable video capture device : \r\n\r\n");
    
    for (i = 0; i < count && i < MAX_VIDEO_DEV_NUMS; i++)
    {
        wchar_t name[256];
        
        hr = ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 256, NULL);
        if (SUCCEEDED(hr))
        {
            char * locale = setlocale(LC_ALL, ".UTF8");
            _tprintf(_T("index : %d, name : %ws\r\n"), i, name);
            setlocale(LC_ALL, locale);
        }
        
        ppDevices[i]->Release();
    }
    
    CoTaskMemFree(ppDevices);
}

int CWVideoCapture::getDeviceIndex(const char * name)
{
    int i, count = 0, index = 0, size;
    IMFAttributes * pAttributes = NULL;
    IMFActivate ** ppDevices = NULL;
    wchar_t * wszname = NULL;
    
    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, (UINT*)&count);
    if (FAILED(hr))
    {
        goto done;
    }

    size = MultiByteToWideChar(CP_ACP, 0, name, -1, NULL, 0);
    wszname = (wchar_t *)malloc(size * sizeof(wchar_t));
    if (wszname)
    {
        MultiByteToWideChar(CP_ACP, 0, name, -1, wszname, size);
    }

done:

    if (pAttributes)
    {
        pAttributes->Release();
    }
    
    for (i = 0; i < count && i < MAX_VIDEO_DEV_NUMS; i++)
    {
        wchar_t wname[256];
        
        hr = ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, wname, 256, NULL);
        if (SUCCEEDED(hr))
        {
            if (wszname != NULL && wcsicmp(wname, wszname) == 0)
            {
                index = i;
                break;
            }
        }
    }

    for (i = 0; i < count; i++)
    {
        ppDevices[i]->Release();
    }
    
    CoTaskMemFree(ppDevices);

    if (wszname)
    {
        free(wszname);
    }

    return index;
}

BOOL CWVideoCapture::checkMediaType()
{
    BOOL ret = FALSE;
    GUID guid;
    IMFMediaType *pType = NULL;
    
    HRESULT hr = m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pType->GetGUID(MF_MT_SUBTYPE, &guid);
    if (FAILED(hr))
    {
        goto done;
    }

    if (guid == MFVideoFormat_MJPG)
    {
        m_nVideoFormat = VIDEO_FMT_MJPG;
    }
    else if (mf_fmt_is_support(guid))
    {
        m_nVideoFormat = mf_fmt_to_ht_fmt(guid);
    }
    else
    {
        goto done;
    }

    hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, (UINT*)&m_nWidth, (UINT*)&m_nHeight);
    if (FAILED(hr))
    {
        goto done;
    }

    UINT num, den;
    
    hr = MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den);
    if (FAILED(hr))
    {
        goto done;
    }
    
    m_nFramerate = (double)num/den;

    ret = TRUE;

done:

    if (pType)
    {
        pType->Release();
    }

    return ret;
}

BOOL CWVideoCapture::setMediaFormat1(int codec, int width, int height)
{
    BOOL set = FALSE;
    int dw = 0, dh = 0, w = 0, h = 0;
    DWORD index = 0;
    HRESULT hr;
    IMFMediaType *pType = NULL;
    IMFMediaType *pDType = NULL;

    hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    while (SUCCEEDED(hr) && pType)
    {
        GUID guid;
        
        hr = pType->GetGUID(MF_MT_SUBTYPE, &guid);
        if (FAILED(hr))
        {
            goto retry;
        }

        if (MFVideoFormat_MJPG == guid && VIDEO_CODEC_JPEG == codec)
        {
        }
        else
        {
            goto retry;
        }

        hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, (UINT*)&w, (UINT*)&h);
        if (FAILED(hr))
        {
            goto retry;
        }
       
        if (w == width && h == height)
        {
            hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            if (SUCCEEDED(hr))
            {
                set = TRUE;
                pType->Release();
                break;
            }
        }
        else if (dw * dh < w * h)
        {
            dw = w;
            dh = h;

            if (pDType)
            {
                pDType->Release();
            }
            pDType = pType;
            
            index++;
            hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
            continue;
        }

retry:

        pType->Release();
        
        index++;
        hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    }

    if (!set && pDType)
    {
        hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pDType);
        if (SUCCEEDED(hr))
        {
            set = TRUE;
        }
    }

    if (pDType)
    {
        pDType->Release();
    }
        
    return set;
}

BOOL CWVideoCapture::setMediaFormat2(int codec, int width, int height)
{
    BOOL set = FALSE;
    int dw = 0, dh = 0, w = 0, h = 0;
    DWORD index = 0;
    HRESULT hr;
    IMFMediaType *pType = NULL;
    IMFMediaType *pDType = NULL;

    hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    while (SUCCEEDED(hr) && pType)
    {
        GUID guid;
        
        hr = pType->GetGUID(MF_MT_SUBTYPE, &guid);
        if (FAILED(hr))
        {
            goto retry;
        }

        if (!mf_fmt_is_support(guid))
        {
            goto retry;
        }

        hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, (UINT*)&w, (UINT*)&h);
        if (FAILED(hr))
        {
            goto retry;
        }
       
        if (w == width && h == height)
        {
            hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            if (SUCCEEDED(hr))
            {
                set = TRUE;
                pType->Release();
                break;
            }
        }
        else if (dw * dh < w * h)
        {
            dw = w;
            dh = h;

            if (pDType)
            {
                pDType->Release();
            }
            pDType = pType;
            
            index++;
            hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
            continue;
        }

retry:

        pType->Release();
        
        index++;
        hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    }

    if (!set && pDType)
    {
        hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pDType);
        if (SUCCEEDED(hr))
        {
            set = TRUE;
        }
    }

    if (pDType)
    {
        pDType->Release();
    }
        
    return set;
}

BOOL CWVideoCapture::setMediaFormat3(int codec, int width, int height)
{
    BOOL set = FALSE;
    int dw = 0, dh = 0, w = 0, h = 0;
    DWORD index = 0;
    HRESULT hr;
    IMFMediaType *pType = NULL;
    IMFMediaType *pDType = NULL;

    hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    while (SUCCEEDED(hr) && pType)
    {
        GUID guid;
        
        hr = pType->GetGUID(MF_MT_SUBTYPE, &guid);
        if (FAILED(hr))
        {
            goto retry;
        }

        if (MFVideoFormat_MJPG == guid)
        {
        }
        else
        {
            goto retry;
        }

        hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, (UINT*)&w, (UINT*)&h);
        if (FAILED(hr))
        {
            goto retry;
        }
       
        if (w == width && h == height)
        {
            hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            if (SUCCEEDED(hr))
            {
                set = TRUE;
                pType->Release();
                break;
            }
        }
        else if (dw * dh < w * h)
        {
            dw = w;
            dh = h;

            if (pDType)
            {
                pDType->Release();
            }
            pDType = pType;
            
            index++;
            hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
            continue;
        }

retry:

        pType->Release();
        
        index++;
        hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    }

    if (!set && pDType)
    {
        hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pDType);
        if (SUCCEEDED(hr))
        {
            set = TRUE;
        }
    }

    if (pDType)
    {
        pDType->Release();
    }
        
    return set;
}

BOOL CWVideoCapture::setFramerate(double framerate)
{
    if (framerate <= 0)
    {
        return TRUE;
    }

    int  min, max, cur;
    UINT num, den;
    BOOL ret = FALSE;
    IMFMediaType *pType = NULL;
    
    HRESULT hr = m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
    if (FAILED(hr))
    {
        goto done;
    }
    
    hr = MFGetAttributeRatio(pType, MF_MT_FRAME_RATE_RANGE_MIN, &num, &den);
    if (FAILED(hr))
    {
        goto done;
    }

    min = num / den;

    hr = MFGetAttributeRatio(pType, MF_MT_FRAME_RATE_RANGE_MAX, &num, &den);
    if (FAILED(hr))
    {
        goto done;
    }

    max = num / den;

    hr = MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den);
    if (FAILED(hr))
    {
        goto done;
    }

    cur = num /den;

    if (framerate < min)
    {
        framerate = min;
    }
    else if (framerate > max)
    {
        framerate = max;
    }

    if (framerate == cur)
    {
        ret = TRUE;
        goto done;
    }

    if (framerate - (int)framerate > 0.0)
    {
        num = (int)(1001 * framerate) + 1;
    }    
    else
    {
        num = (int)(1001 * framerate);
    }
    
    den = 1001;

    hr = MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, num, den);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
    if (FAILED(hr))
    {
        goto done;
    }

    ret = TRUE;

done:

    if (pType)
    {
        pType->Release();
    }

    return ret;
}

BOOL CWVideoCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    BOOL set;
    int i, count = 0;
    IMFAttributes * pAttributes = NULL;
    IMFActivate ** ppDevices = NULL;
    
    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, (UINT*)&count);
    if (FAILED(hr))
    {
        goto done;
    }

    if (count <= m_nDevIndex)
    {
        goto done;
    }
    
    hr = ppDevices[m_nDevIndex]->ActivateObject(__uuidof(IMFMediaSource), (void**)&m_pSource);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);
    if (FAILED(hr))
    {
        goto done;
    }

    set = setMediaFormat1(codec, width, height);
    if (!set)
    {
        set = setMediaFormat2(codec, width, height);
    }

    if (!set)
    {
        set = setMediaFormat3(codec, width, height);
    }

    if (!set)
    {
        goto done;
    }

    setFramerate(framerate);

    if (!checkMediaType())
    {
        goto done;
    }

    setCodecFlag(codec);

    if (m_bDecode)
    {
        if (!m_decoder.init(getVideoCodec(m_nVideoFormat), NULL, 0, HW_DECODING_AUTO))
        {
            goto done;
        }

        m_decoder.setCallback(VideoDecodeCb, this);
    }

    if (m_bEncode)
    {
        VideoEncoderParam params;
        memset(&params, 0, sizeof(params));
        
        params.SrcWidth = m_nWidth;
        params.SrcHeight = m_nHeight;
        params.SrcPixFmt = to_avpixelformat(m_nVideoFormat);
        params.DstCodec = codec;
        params.DstWidth = width ? width : m_nWidth;
        params.DstHeight = height ? height : m_nHeight;
        params.DstFramerate = m_nFramerate;
        params.DstBitrate = bitrate;
            
        if (m_encoder.init(&params) == FALSE)
        {
            goto done;
        }
    }

    m_nBitrate = bitrate;
    m_bInited = TRUE;
       
done:

    if (pAttributes)
    {
        pAttributes->Release();
    }

    for (i = 0; i < count; i++)
    {
        ppDevices[i]->Release();
    }
    
    CoTaskMemFree(ppDevices);
       
    return m_bInited;
}

BOOL CWVideoCapture::capture()
{
    if (!m_bInited)
    {
        return FALSE;
    }

    DWORD dwFlags = 0;
    IMFSample *pSample = NULL;
    
    HRESULT hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &dwFlags, NULL, &pSample);
    if (FAILED(hr) || NULL == pSample)
    {
        return FALSE;
    }

    IMFMediaBuffer *pBuffer = NULL;

    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(hr))
    {
        pSample->Release();
        return FALSE;
    }

    BYTE * buff = NULL;
    DWORD  size = 0;
  
    hr = pBuffer->Lock(&buff, NULL, &size);
    if (FAILED(hr))
    {
        pBuffer->Release();
        pSample->Release();
        return FALSE;
    }

    BOOL ret = TRUE;

    if (m_bDecode)
    {
        ret = m_decoder.decode(buff, size, 0);
    }
    else if (m_bEncode)
    {
        ret = m_encoder.encode(buff, size);
    }
    else
    {
        m_encoder.procData(buff, size);
    }

    pBuffer->Unlock();

    pBuffer->Release();
    pSample->Release();

    return ret;    
}

BOOL CWVideoCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)videoCaptureThread, this);

    return (NULL != m_hCapture);
}

void CWVideoCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    // wait for capture thread exit
    sys_os_wait_thread(&m_hCapture);

    if (m_pReader)
    {
        m_pReader->Release();
        m_pReader = NULL;
    }

    if (m_pSource)
    {
        m_pSource->Release();
        m_pSource = NULL;
    }
    
    m_bInited = FALSE;
}

void CWVideoCapture::captureThread()
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





