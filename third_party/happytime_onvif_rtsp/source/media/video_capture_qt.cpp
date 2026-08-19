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
#include "sys_log.h"
#include "media_format.h"
#include "lock.h"
#include "video_capture_qt.h"
#include <QHash>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QJniObject>
#include <QJniEnvironment>


/***************************************************************************************/

static const char HtCameraListenerClassName[] = "org/happytimesoft/util/HtCameraListener";

typedef QHash<int, CQVideoCapture *> CameraMap;

void *    g_cameras_mutex = sys_os_create_mutex();
CameraMap g_cameras;

/***************************************************************************************/

static void notifyAutoFocusComplete(JNIEnv* , jobject, int id, jboolean success)
{
    
}

static void notifyPictureExposed(JNIEnv* , jobject, int id)
{
   
}

static void notifyPictureCaptured(JNIEnv *env, jobject, int id, jbyteArray data)
{
    
}

static void notifyNewPreviewFrame(JNIEnv *env, jobject, int id, jbyteArray data, int width, int height, int format, int bpl)
{
    CLock lock(g_cameras_mutex);
    
    const auto it = g_cameras.constFind(id);
    if (Q_UNLIKELY(it == g_cameras.cend()))
    {
        return;
    }
    
    const int arrayLength = env->GetArrayLength(data);
    if (arrayLength == 0)
    {
        return;
    }
    
    QByteArray bytes(arrayLength, Qt::Uninitialized);
    env->GetByteArrayRegion(data, 0, arrayLength, (jbyte*)bytes.data());

    (*it)->processFrame((uint8*)bytes.data(), arrayLength, width, height, format, bpl);
}

static void notifyFrameAvailable(JNIEnv *, jobject, int id)
{
    log_print(HT_LOG_DBG, "%s, enter...\r\n", __FUNCTION__);
}

static void notifyError(JNIEnv *, jobject, int id, int error)
{
    CLock lock(g_cameras_mutex);
    
    const auto it = g_cameras.constFind(id);
    if (Q_UNLIKELY(it == g_cameras.cend()))
    {
        return;
    }

    (*it)->processError(error);
}

void * cameraThread(void * argv)
{
    CQVideoCapture * pthis = (CQVideoCapture *)argv;

    pthis->cameraThread();
    
    return NULL;
}

CQVideoCapture::CQVideoCapture(QObject * parent) : QObject(parent), CVideoCapture()
{
    m_nCodec = VIDEO_CODEC_NONE;
    m_nDstWidth = 0;
    m_nDstHeight = 0;
    m_nFrameCount = 0;
    m_bError = FALSE;
    m_bStartCamera = FALSE;
    m_bStartCameraResult = FALSE;
    m_bStopCamera = FALSE;
    m_bStartPreview = FALSE;
    m_bStartPreviewResult = FALSE;

    m_bRunning = TRUE;
    m_pCameraThread = sys_os_create_thread((void *) ::cameraThread, this);
}

CQVideoCapture::~CQVideoCapture(void)
{
    stopCapture();

    m_bRunning = FALSE;

    sys_os_wait_thread(&m_pCameraThread);
}

CVideoCapture * CQVideoCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_VIDEO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CVideoCapture *)new CQVideoCapture;
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

int CQVideoCapture::getDeviceNums()
{
    QList<QCameraDevice> info = QMediaDevices::videoInputs();
    
    log_print(HT_LOG_INFO, "%s, count=%d\r\n", __FUNCTION__, info.count());
    
    return info.count() > MAX_VIDEO_DEV_NUMS ? MAX_VIDEO_DEV_NUMS : info.count();
}

void CQVideoCapture::listDevice()
{
}

int CQVideoCapture::getDeviceIndex(const char * name)
{
    return 0;
}

BOOL CQVideoCapture::startCamera()
{
    QJniEnvironment env;
    
    m_camera = QJniObject::callStaticObjectMethod("android/hardware/Camera",
                                                  "open",
                                                  "(I)Landroid/hardware/Camera;",
                                                  m_nDevIndex);
    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, get camera objectd failed 1\r\n", __FUNCTION__);
        env->ExceptionClear();
        return FALSE;
    }

    if (!m_camera.isValid())
    {
        log_print(HT_LOG_ERR, "%s, get camera objectd failed 2\r\n", __FUNCTION__);
        return FALSE;
    }

    m_cameraListener = QJniObject(HtCameraListenerClassName, "(I)V", m_nDevIndex);

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    QJniObject m_parameters = m_camera.callObjectMethod("getParameters",
                                                        "()Landroid/hardware/Camera$Parameters;");
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
        
    if (m_parameters.isValid()) 
    {
        int i;
        int count;
        
        QJniObject formatList = m_parameters.callObjectMethod("getSupportedPreviewFormats",
                                                              "()Ljava/util/List;");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        
        count = formatList.callMethod<jint>("size");
        
        for (i = 0; i < count; ++i) 
        {
            QJniObject format = formatList.callObjectMethod("get",
                                                            "(I)Ljava/lang/Object;",
                                                            i);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
            }
        
            int fmt = format.callMethod<jint>("intValue");
            if (fmt == 17 || fmt == 842094169 || fmt == 35) // NV21 or YV12 or I420
            {
                m_parameters.callMethod<void>("setPreviewFormat", "(I)V", jint(fmt));
                if (env->ExceptionCheck())
                {
                    env->ExceptionClear();
                }
                break;
            }
        }

        QJniObject sizeList = m_parameters.callObjectMethod("getSupportedPreviewSizes",
                                                            "()Ljava/util/List;");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
                
        count = sizeList.callMethod<jint>("size");
        
        for (i = 0; i < count; ++i) 
        {
            QJniObject size = sizeList.callObjectMethod("get",
                                                        "(I)Ljava/lang/Object;",
                                                        i);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
            }
        
            int w = size.getField<jint>("width");
            int h = size.getField<jint>("height");

            if (w == m_nDstWidth && h == m_nDstHeight)
            {
                m_parameters.callMethod<void>("setPreviewSize", "(II)V", w, h);
                m_parameters.callMethod<void>("setPictureSize", "(II)V", w, h);
                break;
            }
            else if ((m_nDstWidth == 0 || m_nDstHeight == 0) && w == 1280 && h == 720)
            {
                m_parameters.callMethod<void>("setPreviewSize", "(II)V", w, h);
                m_parameters.callMethod<void>("setPictureSize", "(II)V", w, h);
                break;
            }

            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
            }
        }

        QJniObject focusMode = QJniObject::getStaticObjectField("android/hardware/Camera$Parameters", 
                                    "FOCUS_MODE_CONTINUOUS_VIDEO", "Ljava/lang/String;");

        log_print(HT_LOG_INFO, "%s, focusMode = %s\r\n", __FUNCTION__, focusMode.toString().toStdString().c_str());
        
        m_parameters.callMethod<void>("setFocusMode", "(Ljava/lang/String)V", focusMode.object<jstring>());

        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
    }

    // m_parameters.callMethod<void>("setRotation", "(I)V", 180);
    
    m_camera.callMethod<void>("setParameters",
                              "(Landroid/hardware/Camera$Parameters;)V",
                              m_parameters.object());

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }

    // m_camera.callMethod<void>("setDisplayOrientation", "(I)V", 270);

    m_parameters = m_camera.callObjectMethod("getParameters",
                                             "()Landroid/hardware/Camera$Parameters;");

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
            
    QJniObject size = m_parameters.callObjectMethod("getPreviewSize",
                                                    "()Landroid/hardware/Camera$Size;");

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    m_nWidth = size.getField<jint>("width");
    m_nHeight = size.getField<jint>("height");

    int format = m_parameters.callMethod<jint>("getPreviewFormat");

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    if (format == 17)
    {
        m_nVideoFormat = VIDEO_FMT_NV21;
    }
    else if (format == 842094169)
    {
        m_nVideoFormat = VIDEO_FMT_YV12;
    }
    else if (format == 35)
    {
        m_nVideoFormat = VIDEO_FMT_YUV420P;
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, unsupport pixel format!!! %d\r\n", __FUNCTION__, format);
        return FALSE;
    }

    m_surfaceTexture = QJniObject("android/graphics/SurfaceTexture", "(I)V", jint(m_nDevIndex));

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    if (!m_surfaceTexture.isValid())
    {
        log_print(HT_LOG_ERR, "m_surfaceTexture is null\r\n");
        return FALSE;
    }
    
    m_camera.callMethod<void>("setPreviewTexture",
                              "(Landroid/graphics/SurfaceTexture;)V",
                              m_surfaceTexture.object());
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return FALSE;
    }
    
    m_cameraListener.callMethod<void>("setupPreviewCallback", "(Landroid/hardware/Camera;)V", m_camera.object());
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    m_cameraListener.callMethod<void>("notifyNewFrames", "(Z)V", true);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    
    log_print(HT_LOG_INFO, "%s, w=%d, h=%d, fmt=%d\r\n", __FUNCTION__, m_nWidth, m_nHeight, m_nVideoFormat);

    VideoEncoderParam params;
    memset(&params, 0, sizeof(params));
    
    params.SrcWidth = m_nWidth;
    params.SrcHeight = m_nHeight;
    params.SrcPixFmt = (AVPixelFormat)m_nVideoFormat;
    params.DstCodec = m_nCodec;
    params.DstWidth = m_nWidth;
    params.DstHeight = m_nHeight;
    params.DstFramerate = m_nFramerate;
    params.DstBitrate = m_nBitrate;
        
    if (m_encoder.init(&params) == FALSE)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL CQVideoCapture::startPreivew()
{
    log_print(HT_LOG_DBG, "%s, enter...\r\n", __FUNCTION__);
    
    m_camera.callMethod<void>("startPreview");

    QJniEnvironment env;
    
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return FALSE;
    }    
    else
    {
        return TRUE;
    }
}

void CQVideoCapture::stopCamera()
{
    log_print(HT_LOG_DBG, "%s, enter...\r\n", __FUNCTION__);

    QJniEnvironment env;

    m_parameters = QJniObject();
    
    if (m_camera.isValid()) 
    {
        m_camera.callMethod<void>("release");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
    }

    if (m_surfaceTexture.isValid())
    {
        m_surfaceTexture.callMethod<void>("release");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
    }

    m_nFrameCount = 0;

    m_encoder.uninit();
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

BOOL CQVideoCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    log_print(HT_LOG_INFO, "%s, enter...\r\n", __FUNCTION__);

    sys_os_mutex_enter(m_pMutex);
    
    if (m_bInited)
    {
        sys_os_mutex_leave(m_pMutex);
        return TRUE;
    }

    m_nCodec = codec;
    m_nDstWidth = width;
    m_nDstHeight = height;
    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    m_bStartCamera = TRUE;

    while (m_bStartCamera)
    {
        usleep(100*1000);
    }

    m_bInited = m_bStartCameraResult;

    sys_os_mutex_leave(m_pMutex);
    
    if (m_bInited)
    {
        CLock lock(g_cameras_mutex);
    
        g_cameras.insert(m_nDevIndex, this);
    }
    
    return m_bInited;
}

BOOL CQVideoCapture::startCapture()
{
    log_print(HT_LOG_DBG, "%s\r\n", __FUNCTION__);

    CLock lock(m_pMutex);

    if (!m_bInited)
    {
        return FALSE;
    }
    
    if (m_bCapture)
    {
        return TRUE;
    }

    m_bStartPreview = TRUE;

    while (m_bStartPreview)
    {
        usleep(100*1000);
    }

    m_bCapture = m_bStartPreviewResult;

    return m_bCapture;
}

void CQVideoCapture::stopCapture()
{
    log_print(HT_LOG_DBG, "%s, enter...\r\n", __FUNCTION__);

    sys_os_mutex_enter(m_pMutex);
    
    // wait for capture thread exit
    m_bCapture = FALSE;
    
    while (m_hCapture)
    {
        usleep(10*1000);
    }
    
    m_bStopCamera = TRUE;

    while (m_bStopCamera)
    {
        usleep(100*1000);
    }
    
    m_bInited = FALSE;

    sys_os_mutex_leave(m_pMutex);

    CLock lock(g_cameras_mutex);
    
    g_cameras.remove(m_nDevIndex);

    log_print(HT_LOG_DBG, "%s, exit...\r\n", __FUNCTION__);
}

void CQVideoCapture::processFrame(uint8 * data, int size, int width, int height, int format, int bpl)
{
    CLock lock(m_pMutex);
    
    if (!m_bInited)
    {
        return;
    }

    emit imageReady(data, size, width, height, format, bpl);
    
    m_nFrameCount++;
    
    m_encoder.encode(data, size);
}

void CQVideoCapture::processError(int error)
{
    log_print(HT_LOG_ERR, "%s, error=%d\r\n", __FUNCTION__, error);

    m_bError = TRUE;
}

void CQVideoCapture::cameraThread()
{
    while (m_bRunning)
    {
        if (m_bStartCamera)
        {
            m_bStartCameraResult = startCamera();

            m_bStartCamera = FALSE;
        }

        if (m_bStopCamera)
        {
            stopCamera();

            m_bStopCamera = FALSE;
        }

        if (m_bStartPreview)
        {
            m_bStartPreviewResult = startPreivew();

            m_bStartPreview = FALSE;
        }

        checkError();

        usleep(100*1000);
    }

    log_print(HT_LOG_DBG, "%s, exit...\r\n", __FUNCTION__);
}

void CQVideoCapture::checkError()
{
    int  count = 0;
    BOOL reset = FALSE;
    
    if (m_bError)
    {
        m_bError = FALSE;
        reset = TRUE;
    }
    else if (m_bCapture)
    {
        if (count++ > 10*4) // 4 seconds
        {
            count = 0;
            
            if (m_nFrameCount == 0)
            {
                reset = TRUE;
            }
            else
            {
                m_nFrameCount = 0;
            }
        }
    }

    if (reset)
    {
        log_print(HT_LOG_DBG, "%s, reset camera start\r\n", __FUNCTION__);

        stopCapture();

        initCapture(m_nCodec, m_nDstWidth, m_nDstHeight, m_nFramerate, m_nBitrate);

        startCapture();

        log_print(HT_LOG_DBG, "%s, reset camera end\r\n", __FUNCTION__);
    }
}

void CQVideoCapture::initJNI(JNIEnv *env)
{
    jclass clazz = env->FindClass(HtCameraListenerClassName);

    static const JNINativeMethod methods[] = {
        {"notifyAutoFocusComplete", "(IZ)V", (void *)notifyAutoFocusComplete},
        {"notifyPictureExposed", "(I)V", (void *)notifyPictureExposed},
        {"notifyPictureCaptured", "(I[B)V", (void *)notifyPictureCaptured},
        {"notifyNewPreviewFrame", "(I[BIIII)V", (void *)notifyNewPreviewFrame},
        {"notifyFrameAvailable", "(I)V", (void *)notifyFrameAvailable},
        {"notifyError", "(II)V", (void *)notifyError}
    };

    if (clazz) 
    {
        if (env->RegisterNatives(clazz,
                                  methods,
                                  sizeof(methods) / sizeof(methods[0])) != JNI_OK)
        {
            log_print(HT_LOG_INFO, "RegisterNatives failed!\r\n");
        }
        else
        {
            log_print(HT_LOG_INFO, "RegisterNatives succ!\r\n");
        }
    }
    else
    {
        log_print(HT_LOG_ERR, "clazz is null\r\n");
    }
}



