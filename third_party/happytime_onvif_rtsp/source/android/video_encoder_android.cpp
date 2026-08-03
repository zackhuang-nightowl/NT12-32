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
#include "video_encoder_android.h"
#include "media_format.h"
#include "media_util.h"
#include "h264.h"
#include "h265.h"
#include "base64.h"
#include <QJniObject>
#include <QJniEnvironment>


static void nv21tonv12(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i, j;
    uint8  *src, *dst;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    src = data[1];

    // copy UV planar
    
    if (linesize[1] == w)
    {
        for (i = 0; i < w*h/4; i++)
        {
            dst[i * 2] = src[i * 2 + 1];
            dst[i * 2 + 1] = src[i * 2];
        }
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            for (j = 0; j < w/2; j++)
            {
                dst[i * w + j * 2] = src[i * linesize[1] + j * 2 + 1];
                dst[i * w + j * 2 + 1] = src[i * linesize[1] + j * 2];
            }
        }
    }
}

static void nv21toi420(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i, j;
    int u_offset, v_offset;
    uint8  *src, *dst;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    src = data[1];
    u_offset = 0;
    v_offset = u_offset + w * h / 4;

    if (linesize[1] == w)
    {
        for (i = 0; i < w*h/4; i++)
        {
            dst[u_offset++] = src[i * 2 + 1];
            dst[v_offset++] = src[i * 2];
        }
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            for (j = 0; j < w/2; j++)
            {
                dst[u_offset++] = src[i * linesize[1] + j * 2 + 1];
                dst[v_offset++] = src[i * linesize[1] + j * 2];
            }
        }
    }
}

static void yv12tonv12(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i, j;
    uint8  *src, *dst, *u, *v;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    u = data[2];
    v = data[1];

    // copy UV planar
    
    if (linesize[1] == w/2 && linesize[2] == w/2)
    {
        for (i = 0; i < w*h/4; i++)
        {
            dst[i*2] = u[i];
            dst[i*2+1] = v[i];
        }
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            for (j = 0; j < w/2; j++)
            {
                dst[i * w + j * 2] = u[i * linesize[2] + j];
                dst[i * w + j * 2 + 1] = v[i * linesize[1] + j];
            }
        }
    }
}

static void yv12toi420(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i;
    uint8  *src, *dst;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    src = data[2];

    // copy U planar
    
    if (linesize[1] == w/2)
    {
        memcpy(dst, src, w*h/4);
        dst += w*h/4;
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            memcpy(dst, src, w/2);
            src += linesize[2];
            dst += w/2;
        }
    }

    src = data[1];

    // copy V planar

    if (linesize[2] == w/2)
    {
        memcpy(dst, src, w*h/4);
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            memcpy(dst, src, w/2);
            src += linesize[1];
            dst += w/2;
        }
    }
}

static void i420tonv12(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i, j;
    uint8  *src, *dst, *u, *v;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    u = data[1];
    v = data[2];

    // copy UV planar
    
    if (linesize[1] == w/2 && linesize[2] == w/2)
    {
        for (i = 0; i < w*h/4; i++)
        {
            dst[i*2] = u[i];
            dst[i*2+1] = v[i];
        }
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            for (j = 0; j < w/2; j++)
            {
                dst[i * w + j * 2] = u[i * linesize[1] + j];
                dst[i * w + j * 2 + 1] = v[i * linesize[2] + j];
            }
        }
    }
}

static void i420copy(uint8* data[4], int32 linesize[4], uint8 * output, int w, int h)
{
    int i;
    uint8  *src, *dst;

    src = data[0];
    dst = output;

    // copy Y planar
    
    if (linesize[0] == w)
    {
        memcpy(dst, src, w*h);
        dst += w*h;
    }
    else
    {
        for (i = 0; i < h; i++)
        {
            memcpy(dst, src, w);
            src += linesize[0];
            dst += w;
        }
    }

    src = data[1];

    // copy U planar

    if (linesize[1] == w/2)
    {
        memcpy(dst, src, w*h/4);
        dst += w*h/4;
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            memcpy(dst, src, w/2);
            src += linesize[1];
            dst += w/2;
        }
    }

    src = data[2];

    // copy V planar

    if (linesize[2] == w/2)
    {
        memcpy(dst, src, w*h/4);
    }
    else
    {
        for (i = 0; i < h/2; i++)
        {
            memcpy(dst, src, w/2);
            src += linesize[2];
            dst += w/2;
        }
    }
}

static BOOL IsSupportedColorFormat(int color_format)
{
    static const int supported_colorformats[] = 
    {
        19, // COLOR_FormatYUV420Planar, I420
        21, // COLOR_FormatYUV420SemiPlanar, NV12
        -1
    };

    for (const int *ptr = supported_colorformats; *ptr != -1; ptr++)
    {
        if (color_format == *ptr)
        {
            return TRUE;
        }    
    }

    return FALSE;
}

static BOOL SupportColorFormat(std::vector<int> color_formats, int format)
{
    for (size_t k = 0; k < color_formats.size(); ++k)
    {
        if (color_formats[k] == format)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL IsBlacklisted(const std::string &name)
{
    static const char *blacklisted_encoders[] = 
    {
        // No software encoders
        "OMX.google",
        NULL
    };

    for (const char **ptr = blacklisted_encoders; *ptr; ptr++)
    {
        if (!strncasecmp(*ptr, name.c_str(), strlen(*ptr)))
        {
            return TRUE;
        }    
    }
    
    return FALSE;
}

CAndroidVideoEncoder::CAndroidVideoEncoder()
{
    m_dstFmt = 0;

    m_bInited = FALSE;
    m_yuv = NULL;
    m_orgyuv = NULL;
    
    m_mime = "video/avc";

    m_pExtra = NULL;
    m_nExtraLen = 0;
    m_nInputTimeout = 0;
    m_nOutputTimeout = 0;

    m_pCallbackMutex = sys_os_create_mutex();
    m_pCallbackList = hlist_create(FALSE);
}

CAndroidVideoEncoder::~CAndroidVideoEncoder()
{
    uninit();
    
    hlist_free_container(m_pCallbackList);
    
    sys_os_destroy_mutex(m_pCallbackMutex);
}

BOOL CAndroidVideoEncoder::init(VideoEncoderParam * params)
{
    QJniEnvironment env;
    
    if (QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT") < 17)
    {
        log_print(HT_LOG_ERR, "%s, sdk version too old!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    if (VIDEO_CODEC_H264 == params->DstCodec)
    {
        m_mime = "video/avc";
    }
    else if (VIDEO_CODEC_H265 == params->DstCodec)
    {
        m_mime = "video/hevc";
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, unsupport codec %d\r\n", __FUNCTION__, params->DstCodec);
        return FALSE;
    }

    memcpy(&m_EncoderParams, params, sizeof(VideoEncoderParam));
    
    if (m_EncoderParams.DstHeight < 0)
    {
        m_EncoderParams.DstHeight = -m_EncoderParams.DstHeight;
    }

    m_EncoderParams.DstWidth = m_EncoderParams.DstWidth / 2 * 2;    // align to 2
    m_EncoderParams.DstHeight = m_EncoderParams.DstHeight / 2 * 2;  // align to 2
    
    // mediacodec crashes with null size. Trap this...
    if (!m_EncoderParams.DstWidth || !m_EncoderParams.DstHeight)
    {
        log_print(HT_LOG_ERR, "%s, null size, cannot handle\r\n", __FUNCTION__);
        return FALSE;
    }

    if (m_EncoderParams.SrcPixFmt != VIDEO_FMT_NV21 && 
        m_EncoderParams.SrcPixFmt != VIDEO_FMT_YV12 && 
        m_EncoderParams.SrcPixFmt != VIDEO_FMT_YUV420P)
    {
        log_print(HT_LOG_ERR, "%s, srcfmt = %d, unsupport src format\r\n", __FUNCTION__, m_EncoderParams.SrcPixFmt);
        return FALSE;
    }

    int num_codecs = QJniObject::callStaticMethod<jint>("android/media/MediaCodecList", "getCodecCount", "()I");

    log_print(HT_LOG_INFO, "%s, num_codecs = %d\r\n", __FUNCTION__, num_codecs);

    for (int i = 0; i < num_codecs; i++)
    {
        QJniObject codecInfo = QJniObject::callStaticObjectMethod("android/media/MediaCodecList", 
            "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;", i);

        if (!codecInfo.callMethod<jboolean>("isEncoder", "()Z"))
        {
            continue;
        }

        QJniObject codecInfoName = codecInfo.callObjectMethod("getName", "()Ljava/lang/String;");

        m_codecname = codecInfoName.toString().toStdString();

        log_print(HT_LOG_INFO, "%s, codecname = %s\r\n", __FUNCTION__, m_codecname.c_str());

        if (IsBlacklisted(m_codecname))
        {
            continue;
        }

        QString mime = QString::fromStdString(m_mime);
        QJniObject mimeJava = QJniObject::fromString(mime);
        QJniObject codecCaps = codecInfo.callObjectMethod("getCapabilitiesForType", 
            "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;",
            mimeJava.object<jstring>());
        if (env->ExceptionCheck())
        {
            // Unsupported type?
            env->ExceptionClear();
            continue;
        }

        if (!codecCaps.isValid())
        {
            continue;
        }

        QJniObject colorFormats = codecCaps.getObjectField("colorFormats", "[I");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            continue;
        }

        jsize size = env->GetArrayLength(colorFormats.object<jintArray>());
        std::vector<int> color_formats;
        color_formats.resize(size);
        env->GetIntArrayRegion(colorFormats.object<jintArray>(), 0, size, (jint*)color_formats.data());

        for (size_t k = 0; k < color_formats.size(); ++k)
        {
            log_print(HT_LOG_DBG, "%s, colorFormat = %d\r\n", __FUNCTION__, color_formats[k]);
        }

        QJniObject supportedTypes = codecInfo.callObjectMethod("getSupportedTypes", "()[Ljava/lang/String;");
        size = env->GetArrayLength(supportedTypes.object<jobjectArray>());

        for (int j = 0; j < size; ++j)
        {
            jobject typeJava = env->GetObjectArrayElement(supportedTypes.object<jobjectArray>(), j);
            QJniObject type = QJniObject::fromLocalRef(typeJava);
            QString typeStr = type.toString();
            
            log_print(HT_LOG_INFO, "%s, type = %s\r\n", __FUNCTION__, typeStr.toStdString().c_str());

            if (typeStr.toStdString() == m_mime)
            {
                QString codecName = QString::fromStdString(m_codecname);
                QJniObject codecNameJava = QJniObject::fromString(codecName);
        
                m_codec = QJniObject::callStaticObjectMethod("android/media/MediaCodec", 
                    "createByCodecName", "(Ljava/lang/String;)Landroid/media/MediaCodec;",
                    codecNameJava.object<jstring>());

                if (env->ExceptionCheck())
                {
                    log_print(HT_LOG_ERR, "%s, create mediacodec failed\r\n", __FUNCTION__);

                    env->ExceptionClear();
                    continue;
                }

                if (VIDEO_FMT_NV21 == m_EncoderParams.SrcPixFmt)
                {
                    int format = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecCapabilities", 
                        "COLOR_FormatYUV420SemiPlanar");
                    if (SupportColorFormat(color_formats, format))
                    {
                        m_dstFmt = format;
                    }
                }
                else if (VIDEO_FMT_YV12 == m_EncoderParams.SrcPixFmt)                
                {
                    int format = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecCapabilities", 
                        "COLOR_FormatYUV420Planar");
                    if (SupportColorFormat(color_formats, format))
                    {                       
                        m_dstFmt = format;                    
                    }
                }
                else if (VIDEO_FMT_YUV420P == m_EncoderParams.SrcPixFmt) 
                {
                    int format = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecCapabilities", 
                        "COLOR_FormatYUV420Planar");
                    if (SupportColorFormat(color_formats, format))
                    {
                        m_dstFmt = format;
                    }
                }

                if (m_dstFmt == 0)
                {
                    for (size_t k = 0; k < color_formats.size(); ++k)
                    {
                        log_print(HT_LOG_DBG, "%s, colorFormat = %d\r\n", __FUNCTION__, color_formats[k]);

                        if (IsSupportedColorFormat(color_formats[k]))
                        {
                            m_dstFmt = color_formats[k]; // Save color format for initial output configuration
                            break;
                        }
                    }
                }

                break;
            }
        }

        if (m_codec.isValid())
        {
            break;
        }
    }

    if (!m_codec.isValid())
    {
        log_print(HT_LOG_ERR, "%s, Failed to create Android MediaCodec\r\n", __FUNCTION__);
        return FALSE;
    }

    log_print(HT_LOG_INFO, "%s, dstFmt=%d, codecname=%s\r\n", __FUNCTION__, m_dstFmt, m_codecname.c_str());

    uint32 bitrate;
    
    if (m_EncoderParams.DstBitrate > 0)
    {
        bitrate = m_EncoderParams.DstBitrate * 1000;
    }
    else
    {
        bitrate = m_EncoderParams.DstWidth * m_EncoderParams.DstHeight * m_EncoderParams.DstFramerate * 0.16;
    }

    QString mime = QString::fromStdString(m_mime);
    QJniObject mimeJava = QJniObject::fromString(mime);
    QJniObject mediaFormat = QJniObject::callStaticObjectMethod("android/media/MediaFormat", 
        "createVideoFormat", 
        "(Ljava/lang/String;II)Landroid/media/MediaFormat;",
        mimeJava.object<jstring>(), m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);

    QJniObject KEY_BIT_RATE = QJniObject::getStaticObjectField("android/media/MediaFormat", 
        "KEY_BIT_RATE", "Ljava/lang/String;");
    mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", KEY_BIT_RATE.object<jstring>(), bitrate);

    QJniObject KEY_FRAME_RATE = QJniObject::getStaticObjectField("android/media/MediaFormat", 
        "KEY_FRAME_RATE", "Ljava/lang/String;");
    mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", KEY_FRAME_RATE.object<jstring>(), (int)m_EncoderParams.DstFramerate);

    QJniObject KEY_COLOR_FORMAT = QJniObject::getStaticObjectField("android/media/MediaFormat", 
        "KEY_COLOR_FORMAT", "Ljava/lang/String;");
    mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", KEY_COLOR_FORMAT.object<jstring>(), m_dstFmt);

    QJniObject KEY_I_FRAME_INTERVAL = QJniObject::getStaticObjectField("android/media/MediaFormat", 
        "KEY_I_FRAME_INTERVAL", "Ljava/lang/String;");
    mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", KEY_I_FRAME_INTERVAL.object<jstring>(), 1);

    if (VIDEO_CODEC_H264 == m_EncoderParams.DstCodec)
    {
        int profile = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecProfileLevel", 
            "AVCProfileBaseline");
        mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", 
            QJniObject::fromString("profile").object<jstring>(), profile);

        int level = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecProfileLevel", 
            "AVCLevel31");
        mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", 
            QJniObject::fromString("level").object<jstring>(), level);
    }
    else if (VIDEO_CODEC_H265 == m_EncoderParams.DstCodec)
    {
        int profile = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecProfileLevel", 
            "HEVCProfileMain");
        mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", 
            QJniObject::fromString("profile").object<jstring>(), profile);

        int level = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecProfileLevel", 
            "HEVCHighTierLevel4");
        mediaFormat.callMethod<void>("setInteger", "(Ljava/lang/String;I)V", 
            QJniObject::fromString("level").object<jstring>(), level);
    }
    
    m_codec.callMethod<void>("configure", "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V", 
        mediaFormat.object<jobject>(), NULL, NULL,
        QJniObject::getStaticField<jint>("android/media/MediaCodec", "CONFIGURE_FLAG_ENCODE"));

    // always, check/clear jni exceptions.
    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, MediaCodec configure ExceptionCheck\r\n", __FUNCTION__);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return FALSE;
    }
    
    m_codec.callMethod<void>("start", "()V");

    // always, check/clear jni exceptions.
    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, MediaCodec start ExceptionCheck\r\n", __FUNCTION__);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return FALSE;
    }
    
    m_orgyuv = new uint8[m_EncoderParams.DstWidth * m_EncoderParams.DstHeight * 3 / 2 + 2048];
    m_yuv = m_orgyuv + (32 - ((long)m_orgyuv % 32));

    m_bInited = TRUE;
    
    return TRUE;
}

void CAndroidVideoEncoder::uninit()
{
    QJniEnvironment env;
    
    if (m_codec.isValid())
    {
        m_codec.callMethod<void>("stop", "()V");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        
        m_codec.callMethod<void>("release", "()V");
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }    
    }

    if (m_orgyuv)
    {
        delete[] m_orgyuv;
        m_orgyuv = NULL;
    }

    if (m_pExtra)
    {
        free(m_pExtra);
        m_pExtra = NULL;
    }

    m_nExtraLen = 0;
    m_bInited = FALSE;
}

BOOL CAndroidVideoEncoder::encode(uint8* data[4], int32 linesize[4])
{
    int nv12, i420;
    BOOL ret = FALSE;
    int64 timeout_us = 5000;
    int inputBufferIndex = 0;
    int buffsize =0;
    int dstsize = 0;
    int sizeTMP = 0;
    QJniEnvironment env;

    if (!m_bInited)
    {
        return FALSE;
    }

    if (NULL == data[0] || 0 == linesize[0])
    {
        goto FLUSH_END;
    }
    
    nv12 = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecCapabilities", 
        "COLOR_FormatYUV420SemiPlanar");

    i420 = QJniObject::getStaticField<jint>("android/media/MediaCodecInfo$CodecCapabilities", 
        "COLOR_FormatYUV420Planar");

    if (VIDEO_FMT_NV21 == m_EncoderParams.SrcPixFmt)
    {
        if (nv12 == m_dstFmt)
        {
            nv21tonv12(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
        else if (i420 == m_dstFmt)
        {
            nv21toi420(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
    }
    else if (VIDEO_FMT_YV12 == m_EncoderParams.SrcPixFmt)
    {
        if (nv12 == m_dstFmt)
        {
            yv12tonv12(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
        else if (i420 == m_dstFmt)
        {
            yv12toi420(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
    }
    else if (VIDEO_FMT_YUV420P == m_EncoderParams.SrcPixFmt)
    {
        if (nv12 == m_dstFmt)
        {
            i420tonv12(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
        else if (i420 == m_dstFmt)
        {
            i420copy(data, linesize, m_yuv, m_EncoderParams.DstWidth, m_EncoderParams.DstHeight);
        }
    }
    else
    {
        log_print(HT_LOG_ERR, "unsupport src format (%d)\r\n", m_EncoderParams.SrcPixFmt);
        return FALSE;
    }

    inputBufferIndex = m_codec.callMethod<jint>("dequeueInputBuffer", "(J)I", timeout_us);

    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, MediaCodec dequeueInputBuffer ExceptionCheck\r\n", __FUNCTION__);
        env->ExceptionClear();
        goto FLUSH_END;
    }
    
    if (inputBufferIndex >= 0)
    {
        QJniObject inputBuffer = m_codec.callObjectMethod("getInputBuffer", "(I)Ljava/nio/ByteBuffer;", inputBufferIndex);
        
        buffsize = m_EncoderParams.DstWidth * m_EncoderParams.DstHeight * 3 / 2;

        dstsize = inputBuffer.callMethod<jint>("capacity", "()I");

        sizeTMP = buffsize > dstsize ? dstsize : buffsize;

        // fetch a pointer to the ByteBuffer backing store
        uint8 *dst_ptr = (uint8_t*)env->GetDirectBufferAddress(inputBuffer.object<jobject>());
        if (dst_ptr)
        {
            memcpy(dst_ptr, m_yuv, sizeTMP);
        }
        else
        {
            log_print(HT_LOG_DBG, "%s, input dst_ptr is null\r\n", __FUNCTION__);
        }

        m_codec.callMethod<void>("queueInputBuffer", "(IIIJI)V", inputBufferIndex, 0, sizeTMP, sys_os_get_ms()*1000, 0);
        
        if (env->ExceptionCheck())
        {
            log_print(HT_LOG_ERR, "%s, MediaCodec queueInputBuffer ExceptionCheck\r\n", __FUNCTION__);
            env->ExceptionClear();
        }
        else
        {
            ret = TRUE;
        }

        m_nInputTimeout = 0;
    }
    else if (inputBufferIndex == QJniObject::getStaticField<jint>("android/media/MediaCodec", "INFO_TRY_AGAIN_LATER"))
    {
        m_nInputTimeout++;

        log_print(HT_LOG_INFO, "%s, INPUT INFO_TRY_AGAIN_LATER, %d\r\n", __FUNCTION__, m_nInputTimeout);
    }
    else
    {
        log_print(HT_LOG_DBG, "%s, inputBufferIndex=%d\r\n", __FUNCTION__, inputBufferIndex);
    }
    
FLUSH_END:

    QJniObject bufferInfo("android/media/MediaCodec$BufferInfo");

    int outputBufferIndex = m_codec.callMethod<jint>("dequeueOutputBuffer", 
        "(Landroid/media/MediaCodec$BufferInfo;J)I",
        bufferInfo.object<jobject>(), timeout_us);

    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, MediaCodec dequeueOutputBuffer ExceptionCheck\r\n", __FUNCTION__);
        env->ExceptionClear();
        return FALSE;
    }
    
    if (outputBufferIndex >= 0)
    {
        QJniObject outputBuffer = m_codec.callObjectMethod("getOutputBuffer", "(I)Ljava/nio/ByteBuffer;", outputBufferIndex);
        
        uint8 *dst_ptr = (uint8_t*)env->GetDirectBufferAddress(outputBuffer.object<jobject>());
        if (dst_ptr)
        {
            dst_ptr += bufferInfo.getField<jint>("offset");

            int buff_size = bufferInfo.getField<jint>("size");
            int key_frame = 0;
            uint8 nalu_t;
            int extra = 0;

            if (VIDEO_CODEC_H264 == m_EncoderParams.DstCodec)
            {
                nalu_t = avc_h264_nalu_type(dst_ptr, buff_size);
                key_frame = (nalu_t == H264_NAL_IDR ? 1 : 0);

                if (nalu_t == H264_NAL_SPS || nalu_t == H264_NAL_PPS)
                {
                    extra = 1;
                }
            }
            else if (VIDEO_CODEC_H265 == m_EncoderParams.DstCodec)
            {
                nalu_t = avc_h265_nalu_type(dst_ptr, buff_size);
                key_frame = IS_IRAP_NAL(nalu_t);

                if (nalu_t == HEVC_NAL_VPS || nalu_t == HEVC_NAL_SPS || nalu_t == HEVC_NAL_PPS)
                {
                    extra = 1;
                }
            }
            
            if (extra && !m_pExtra)
            {
                if (buff_size > 0)
                {
                    m_pExtra = (uint8 *)malloc(buff_size);
                    if (m_pExtra)
                    {
                        m_nExtraLen = buff_size;
                        memcpy(m_pExtra, dst_ptr, buff_size);
                    }
                }
            }
            else
            {
                if (key_frame && m_nExtraLen > 0 && m_pExtra)
                {
                    procData(m_pExtra, m_nExtraLen);
                }
                
                procData(dst_ptr, buff_size);
            }
        }
        else
        {
            log_print(HT_LOG_DBG, "%s, outout dst_ptr is null\r\n", __FUNCTION__);
        }

        m_codec.callMethod<void>("releaseOutputBuffer", "(IZ)V", outputBufferIndex, false);
        
        if (env->ExceptionCheck())
        {
            log_print(HT_LOG_ERR, "%s, ReleaseOutputBuffer ExceptionCheck\r\n", __FUNCTION__);
            env->ExceptionClear();
        }

        m_nOutputTimeout = 0;
    }
    else if (outputBufferIndex == QJniObject::getStaticField<jint>("android/media/MediaCodec", "INFO_OUTPUT_BUFFERS_CHANGED"))
    {
        log_print(HT_LOG_INFO, "%s, INFO_OUTPUT_BUFFERS_CHANGED\r\n", __FUNCTION__);
    }
    else if (outputBufferIndex == QJniObject::getStaticField<jint>("android/media/MediaCodec", "INFO_OUTPUT_FORMAT_CHANGED"))
    {
        log_print(HT_LOG_INFO, "%s, INFO_OUTPUT_FORMAT_CHANGED\r\n", __FUNCTION__);
    }
    else if (outputBufferIndex == QJniObject::getStaticField<jint>("android/media/MediaCodec", "INFO_TRY_AGAIN_LATER"))
    {
        // log_print(HT_LOG_INFO, "%s, OUTPUT INFO_TRY_AGAIN_LATER, %d\r\n", __FUNCTION__, m_nOutputTimeout);

        m_nOutputTimeout++;
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, unknow index, %d\r\n", __FUNCTION__, outputBufferIndex);
    }

    // always, check/clear jni exceptions.
    if (env->ExceptionCheck())
    {
        log_print(HT_LOG_ERR, "%s, ExceptionCheck\r\n", __FUNCTION__);
        env->ExceptionClear();
    }

    if (m_nInputTimeout > 30 || m_nOutputTimeout > 30)
    {
        m_nInputTimeout = 0;
        m_nOutputTimeout = 0;

        log_print(HT_LOG_INFO, "%s, reset encoder start\r\n", __FUNCTION__);
        
        uninit();
        init(&m_EncoderParams);

        log_print(HT_LOG_INFO, "%s, reset encoder end\r\n", __FUNCTION__);
    }

    return ret;
}

BOOL CAndroidVideoEncoder::encode(uint8 * buff, int size)
{
    uint8* data[4];
    int32 linesize[4];
    
    if (!m_bInited)
    {
        return FALSE;
    }

    data[0] = data[1] = data[2] = data[3] = NULL;
    linesize[0] = linesize[1] = linesize[2] = linesize[3] = 0;
        
    if (NULL != buff && 0 < size)
    {
        if (VIDEO_FMT_NV21 == m_EncoderParams.SrcPixFmt)
        {
            data[0] = buff;
            linesize[0] = m_EncoderParams.DstWidth;
            data[1] = data[0] + m_EncoderParams.DstWidth * m_EncoderParams.DstHeight;
            linesize[1] = m_EncoderParams.DstWidth;
        }
        else if (VIDEO_FMT_YV12 == m_EncoderParams.SrcPixFmt || 
            VIDEO_FMT_YUV420P == m_EncoderParams.SrcPixFmt)
        {
            data[0] = buff;
            linesize[0] = m_EncoderParams.DstWidth;
            data[1] = data[0] + m_EncoderParams.DstWidth * m_EncoderParams.DstHeight;
            linesize[1] = m_EncoderParams.DstWidth / 2;
            data[2] = data[1] + m_EncoderParams.DstWidth * m_EncoderParams.DstHeight / 4;
            linesize[2] = m_EncoderParams.DstWidth / 2;
        }
    }

    return encode(data, linesize);
}

void CAndroidVideoEncoder::procData(uint8 * data, int size)
{
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback != NULL)
        {
            p_cb->pCallback(data, size, 0, p_cb->pUserdata);
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

BOOL CAndroidVideoEncoder::isCallbackExist(VideoDataCallback pCallback, void *pUserdata)
{
    BOOL exist = FALSE;
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {
            exist = TRUE;
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);
    
    sys_os_mutex_leave(m_pCallbackMutex);

    return exist;
}

void CAndroidVideoEncoder::addCallback(VideoDataCallback pCallback, void *pUserdata)
{
    if (isCallbackExist(pCallback, pUserdata))
    {
        return;
    }
    
    VideoEncoderCB * p_cb = (VideoEncoderCB *) malloc(sizeof(VideoEncoderCB));
    if (NULL == p_cb)
    {
        return;
    }
    
    p_cb->pCallback = pCallback;
    p_cb->pUserdata = pUserdata;
    p_cb->bFirst = TRUE;

    sys_os_mutex_enter(m_pCallbackMutex);
    hlist_add_at_back(m_pCallbackList, p_cb);    
    sys_os_mutex_leave(m_pCallbackMutex);
}

void CAndroidVideoEncoder::delCallback(VideoDataCallback pCallback, void *pUserdata)
{
    VideoEncoderCB * p_cb = NULL;
    LKNODE * p_node = NULL;
    
    sys_os_mutex_enter(m_pCallbackMutex);

    p_node = hlist_lookup_start(m_pCallbackList);
    while (p_node)
    {
        p_cb = (VideoEncoderCB *) p_node->data;
        if (p_cb->pCallback == pCallback && p_cb->pUserdata == pUserdata)
        {        
            free(p_cb);
            
            hlist_remove(m_pCallbackList, p_node);
            break;
        }
        
        p_node = hlist_lookup_next(m_pCallbackList, p_node);
    }
    hlist_lookup_end(m_pCallbackList);

    sys_os_mutex_leave(m_pCallbackMutex);
}

void CAndroidVideoEncoder::getH264AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8 * sps = NULL; uint32 sps_size = 0;
    uint8 * pps = NULL; uint32 pps_size = 0;

    if (NULL == m_pExtra || m_nExtraLen <= 8)
    {
        return;
    }

    uint8 *r, *end = m_pExtra + m_nExtraLen;
    r = avc_find_startcode(m_pExtra, end);

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

void CAndroidVideoEncoder::getH265AuxSDPLine(char * buff, int size, int rtp_pt)
{
    uint8* vps = NULL; uint32 vps_size = 0;
    uint8* sps = NULL; uint32 sps_size = 0;
    uint8* pps = NULL; uint32 pps_size = 0;

    if (NULL == m_pExtra || m_nExtraLen < 12)
    {
        return;
    }

    uint8 *r, *end = m_pExtra + m_nExtraLen;
    r = avc_find_startcode(m_pExtra, end);

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

void CAndroidVideoEncoder::getAuxSDPLine(char * buff, int size, int rtp_pt)
{
    if (VIDEO_CODEC_H264 == m_EncoderParams.DstCodec)
    {
        getH264AuxSDPLine(buff, size, rtp_pt);
    }
    else if (VIDEO_CODEC_H265 == m_EncoderParams.DstCodec)
    {
        getH265AuxSDPLine(buff, size, rtp_pt);
    }
}

BOOL CAndroidVideoEncoder::getExtraData(uint8 ** extradata, int * extralen)
{
    if (m_pExtra && m_nExtraLen > 0)
    {
        *extradata = m_pExtra;
        *extralen = m_nExtraLen;

        return TRUE;
    }

    return FALSE;
}



