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
#include "audio_capture_win.h"
#include "lock.h"
#include <Functiondiscoverykeys_devpkey.h>
#include <mmreg.h>

/**************************************************************************************/

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
const IID IID_IMMEndpoint = __uuidof(IMMEndpoint);

/***************************************************************************************/
static void * audioCaptureThread(void * argv)
{
    CWAudioCapture *capture = (CWAudioCapture *)argv;

    capture->captureThread();

    return NULL;
}


/***************************************************************************************/
CWAudioCapture::CWAudioCapture() 
: CAudioCapture()
, m_pAudioClient(NULL)
, m_pCaptureClient(NULL)
{
    
}

CWAudioCapture::~CWAudioCapture()
{
    stopCapture();
}

CAudioCapture * CWAudioCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_AUDIO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = new CWAudioCapture;
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

int CWAudioCapture::getDeviceNums()
{
    UINT count = 0;
    HRESULT hr;
    IMMDeviceCollection * pEndpoints = NULL;
    
    hr = getDeviceCollection(eAll, &pEndpoints);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, getDeviceCollection failed. hr=%ld", __FUNCTION__, hr);
        return 0;
    }

    hr = pEndpoints->GetCount(&count);

    pEndpoints->Release();
    
    return count > MAX_AUDIO_DEV_NUMS ? MAX_AUDIO_DEV_NUMS : count;
}

void CWAudioCapture::listDevice()
{
    UINT i, count = 0;
    HRESULT hr;
    IMMDeviceCollection * pEndpoints = NULL;

    printf("\r\nAvailable audio capture device : \r\n\r\n");
    
    hr = getDeviceCollection(eAll, &pEndpoints);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, getDeviceCollection failed. hr=%ld", __FUNCTION__, hr);
        return;
    }

    hr = pEndpoints->GetCount(&count);

    for (i = 0; i < count; i++)
    {
        IMMDevice * pDevice = NULL;
        
        hr = pEndpoints->Item(i, &pDevice);
        if (FAILED(hr))
        {
            log_print(HT_LOG_WARN, "%s, pEndpoints->Item failed. hr=%ld", __FUNCTION__, hr);
            continue;
        }

        PROPVARIANT varName;
        // Initialize container for property value.
        PropVariantInit(&varName);
        
        hr = getDeviceName(pDevice, &varName);
        if (FAILED(hr))
        {
            pDevice->Release();
            log_print(HT_LOG_WARN, "%s, getDeviceName failed. hr=%ld", __FUNCTION__, hr);
            continue;
        }

        printf("index : %d, name : %ws\r\n", i, varName.pwszVal);

        PropVariantClear(&varName);
        pDevice->Release();
    }

    pEndpoints->Release();
}

int CWAudioCapture::getDeviceIndex(const char * name)
{
    UINT i, count = 0, index = 0, size;
    HRESULT hr;
    IMMDeviceCollection * pEndpoints = NULL;
    wchar_t * wszname = NULL;
    
    hr = getDeviceCollection(eAll, &pEndpoints);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, getDeviceCollection failed. hr=%ld", __FUNCTION__, hr);
        return 0;
    }

    size = MultiByteToWideChar(CP_ACP, 0, name, -1, NULL, 0);
    wszname = (wchar_t *)malloc(size * sizeof(wchar_t));
    if (wszname)
    {
        MultiByteToWideChar(CP_ACP, 0, name, -1, wszname, size);
    }

    hr = pEndpoints->GetCount(&count);

    for (i = 0; i < count && i < MAX_AUDIO_DEV_NUMS; i++)
    {
        IMMDevice * pDevice = NULL;
        
        hr = pEndpoints->Item(i, &pDevice);
        if (FAILED(hr))
        {
            log_print(HT_LOG_WARN, "%s, pEndpoints->Item failed. hr=%ld", __FUNCTION__, hr);
            continue;
        }

        PROPVARIANT varName;
        // Initialize container for property value.
        PropVariantInit(&varName);
        
        hr = getDeviceName(pDevice, &varName);
        if (FAILED(hr))
        {
            pDevice->Release();
            log_print(HT_LOG_WARN, "%s, getDeviceName failed. hr=%ld", __FUNCTION__, hr);
            continue;
        }

        if (wszname != NULL && wcsicmp(varName.pwszVal, wszname) == 0)
        {
            index = i;

            PropVariantClear(&varName);
            pDevice->Release();
            break;
        }

        PropVariantClear(&varName);
        pDevice->Release();
    }

    pEndpoints->Release();

    if (wszname)
    {
        free(wszname);
    }

    return index;
}

BOOL CWAudioCapture::getDeviceName(int index, char * name, int namesize)
{
    UINT count = 0;
    HRESULT hr;
    IMMDevice * pDevice = NULL;
    IMMDeviceCollection * pEndpoints = NULL;
    PROPVARIANT varName;
    
    hr = getDeviceCollection(eAll, &pEndpoints);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, getDeviceCollection failed. hr=%ld", __FUNCTION__, hr);
        return FALSE;
    }

    hr = pEndpoints->GetCount(&count);
    if (FAILED(hr))
    {
        pEndpoints->Release();
        log_print(HT_LOG_ERR, "%s, GetCount failed. hr=%ld", __FUNCTION__, hr);
        return FALSE;
    }

    if (index < 0 || (UINT)index >= count || index >= MAX_AUDIO_DEV_NUMS)
    {
        pEndpoints->Release();
        log_print(HT_LOG_ERR, "%s, invalid index %d, count=%d\r\n", __FUNCTION__, index, count);
        return FALSE;
    }
    
    hr = pEndpoints->Item(index, &pDevice);
    if (FAILED(hr))
    {
        pEndpoints->Release();
        log_print(HT_LOG_ERR, "%s, pEndpoints->Item failed. hr=%ld", __FUNCTION__, hr);
        return FALSE;
    }

    pEndpoints->Release();
    
    // Initialize container for property value.
    PropVariantInit(&varName);
    
    hr = getDeviceName(pDevice, &varName);
    if (FAILED(hr))
    {
        pDevice->Release();
        log_print(HT_LOG_WARN, "%s, getDeviceName failed. hr=%ld", __FUNCTION__, hr);
        return FALSE;
    }

    WideCharToMultiByte(CP_ACP, 0, varName.pwszVal, -1, name, namesize, NULL, NULL);

    PropVariantClear(&varName);
    
    pDevice->Release();

    return TRUE;
}

BOOL CWAudioCapture::initCapture(int codec, int samplerate, int channels, int bitrate)
{
    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    int samplefmt;
    DWORD flags = 0;
    HRESULT hr;
    IMMDevice * pDevice = NULL;
    WAVEFORMATEX * pwfx = NULL;
    
    hr = getDeviceByIndex(m_nDevIndex, &pDevice);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, getDeviceByIndex failed. index=%d, hr=%ld", 
            __FUNCTION__, m_nDevIndex, hr);
        return FALSE;
    }
    
    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, pDevice->Activate failed. hr=%ld", __FUNCTION__, hr);
        goto done;
    }
    
    hr = m_pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, m_pAudioClient->GetMixFormat failed. hr=%ld", __FUNCTION__, hr);
        goto done;
    }

    samplefmt = getSampleFmt(pwfx);
    if (samplefmt == AV_SAMPLE_FMT_NONE)
    {
        log_print(HT_LOG_ERR, "%s, getSampleFmt failed", __FUNCTION__);
        goto done;
    }

    if (getDataFlow(pDevice) == eRender)
    {
        flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = m_pAudioClient->Initialize(
             AUDCLNT_SHAREMODE_SHARED,
             flags,
             1000000, // 100ms
             0,
             pwfx,
             NULL);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, m_pAudioClient->Initialize failed. hr=%ld", __FUNCTION__, hr);
        goto done;
    }

    hr = m_pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&m_pCaptureClient);
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, m_pAudioClient->GetService failed. hr=%ld", __FUNCTION__, hr);
        goto done;
    }

    AudioEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcChannels = pwfx->nChannels;
    params.SrcSamplefmt = (AVSampleFormat) samplefmt;
    params.SrcSamplerate = pwfx->nSamplesPerSec;
    params.DstChannels = channels;
    params.DstSamplefmt = AV_SAMPLE_FMT_S16;
    params.DstSamplerate = samplerate;
    params.DstBitrate = bitrate;
    params.DstCodec = codec;

    if (m_encoder.init(&params) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init encoder failed", __FUNCTION__);
        goto done;
    }

    m_nChannels = channels;
    m_nSampleRate = samplerate;
    m_nBitrate = bitrate;
    m_nSampleSize = pwfx->wBitsPerSample * pwfx->nChannels;
    m_bInited = TRUE;

done:

    if (pDevice)
    {
        pDevice->Release();
    }

    if (pwfx)
    {
        CoTaskMemFree(pwfx);
    }

    if (!m_bInited)
    {
        if (m_pAudioClient)
        {
            m_pAudioClient->Release();
            m_pAudioClient = NULL;
        }

        if (m_pCaptureClient)
        {
            m_pCaptureClient->Release();
            m_pCaptureClient = NULL;
        }
    }

    return m_bInited;
}

BOOL CWAudioCapture::startCapture()
{
    CLock lock(m_pMutex);

    if (!m_bInited)
    {
        return FALSE;
    }
    
    if (m_bCapture)
    {
        return TRUE;
    }

    HRESULT hr = m_pAudioClient->Start();
    if (FAILED(hr))
    {
        log_print(HT_LOG_ERR, "%s, m_pAudioClient->Start failed. hr=%ld", __FUNCTION__, hr);
        return FALSE;
    }

    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)audioCaptureThread, this);

    return (m_hCapture ? TRUE : FALSE);
}

void CWAudioCapture::stopCapture(void)
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;    

    sys_os_wait_thread(&m_hCapture);

    if (m_pAudioClient)
    {
        m_pAudioClient->Stop();
        m_pAudioClient->Release();
        m_pAudioClient = NULL;
    }

    if (m_pCaptureClient)
    {
        m_pCaptureClient->Release();
        m_pCaptureClient = NULL;
    }

    m_bInited = FALSE;
}

BOOL CWAudioCapture::capture()
{
    HRESULT hr;
    UINT32 packetLength = 0;
    
    hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr) || packetLength == 0)
    {
        return FALSE;
    }

    BYTE  * data;
    DWORD   flags;
    UINT32  size;

    hr = m_pCaptureClient->GetBuffer(&data, &size, &flags, NULL, NULL);
    if (FAILED(hr))
    {
        return FALSE;
    }

    m_encoder.encode(data, size * (m_nSampleSize / 8));

    hr = m_pCaptureClient->ReleaseBuffer(size);

    return TRUE;
}

void CWAudioCapture::captureThread()
{
    while (m_bCapture)
    {
        if (capture())
        {
        }
        else
        {
            usleep(10*1000);
        }
    }
}

HRESULT CWAudioCapture::getDeviceCollection(EDataFlow dataFlow, IMMDeviceCollection ** ppEndpoints)
{
    HRESULT hr;
    IMMDeviceEnumerator * pEnumerator = NULL;
    
    hr = CoCreateInstance(
             CLSID_MMDeviceEnumerator, NULL,
             CLSCTX_ALL, IID_IMMDeviceEnumerator,
             (void**)&pEnumerator);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pEnumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, ppEndpoints);

    pEnumerator->Release();

    return hr;
}

HRESULT CWAudioCapture::getDeviceName(IMMDevice * pDevice, PROPVARIANT * pName)
{
    HRESULT hr;
    IPropertyStore * pProps = NULL;
    
    hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (FAILED(hr))
    {
        return hr;
    }

    // Get the endpoint's friendly-name property.
    hr = pProps->GetValue(PKEY_Device_FriendlyName, pName);

    pProps->Release();
    
    return hr;
}

HRESULT CWAudioCapture::getDeviceByIndex(int index, IMMDevice ** ppDevice)
{
    UINT count = 0;
    HRESULT hr;
    IMMDeviceCollection * pEndpoints = NULL;
    
    hr = getDeviceCollection(eAll, &pEndpoints);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pEndpoints->GetCount(&count);
    
    if ((UINT) index >= count)
    {
        pEndpoints->Release();
        
        return E_INVALIDARG;
    }

    hr = pEndpoints->Item(index, ppDevice);

    pEndpoints->Release();

    return hr;
}

int CWAudioCapture::getSampleFmt(WAVEFORMATEX * pwfx)
{
    int fmt = AV_SAMPLE_FMT_NONE;
    
    if ((pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) && (pwfx->wBitsPerSample == 32)) 
    {
        fmt = AV_SAMPLE_FMT_FLT;
    } 
    else if ((pwfx->wFormatTag == WAVE_FORMAT_PCM) && (pwfx->wBitsPerSample == 16)) 
    {
        fmt = AV_SAMPLE_FMT_S16;
    } 
    else if ((pwfx->wFormatTag == WAVE_FORMAT_PCM) && (pwfx->wBitsPerSample == 32)) 
    {
        fmt = AV_SAMPLE_FMT_S32;
    } 
    else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) 
    {
        const WAVEFORMATEXTENSIBLE * ext = (const WAVEFORMATEXTENSIBLE *) pwfx;
        
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && (pwfx->wBitsPerSample == 32)) 
        {
            fmt = AV_SAMPLE_FMT_FLT;
        } 
        else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) && (pwfx->wBitsPerSample == 16)) 
        {
            fmt = AV_SAMPLE_FMT_S16;
        } 
        else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) && (pwfx->wBitsPerSample == 32)) 
        {
            fmt = AV_SAMPLE_FMT_S32;
        }
    }

    return fmt;
}

int CWAudioCapture::getDataFlow(IMMDevice * pDevice)
{
    HRESULT hr;
    EDataFlow dataflow = eCapture;
    IMMEndpoint * pEndpoint;
    
    hr = pDevice->QueryInterface(IID_IMMEndpoint, (void **)&pEndpoint);
    if (FAILED(hr))
    {
        return dataflow;
    }

    hr = pEndpoint->GetDataFlow(&dataflow);

    pEndpoint->Release();
    
    return dataflow;
}



