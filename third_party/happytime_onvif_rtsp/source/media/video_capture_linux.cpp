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
#include "video_capture_linux.h"
#include "media_format.h"
#include "media_codec.h"
#include "lock.h"
#include "v4l2.h"
#include "v4l2_comm.h"
#include <linux/videodev2.h>


/***************************************************************************************/

static void * videoCaptureThread(void * argv)
{
    CLVideoCapture *capture = (CLVideoCapture *)argv;

    capture->captureThread();

    return NULL;
}

CLVideoCapture::CLVideoCapture(void) : CVideoCapture()
{
    m_fd = 0;
    m_nBuffers = 0;
    m_pBufStart = NULL;
    m_pBufLen = NULL;
}

CLVideoCapture::~CLVideoCapture(void)
{
    stopCapture();
}

CVideoCapture * CLVideoCapture::getInstance(int devid)
{
    if (devid < 0 || devid >= MAX_VIDEO_DEV_NUMS)
    {
        return NULL;
    }
    
    sys_os_mutex_enter(m_pInstMutex);

    if (NULL == m_pInstance[devid])
    {
        m_pInstance[devid] = (CVideoCapture*) new CLVideoCapture;
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

int CLVideoCapture::getDeviceNums()
{
    int fd;
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    char filename[512] = {'\0'};

    dir = opendir("/dev");
    if (!dir) 
    {
        log_print(HT_LOG_ERR, "%s, opendir failed\r\n", __FUNCTION__);
        return 0;
    }

    while ((entry = readdir(dir))) 
    {
        if (strncmp(entry->d_name, "video", 5))
        {
            continue;
        }

        snprintf(filename, sizeof(filename), "/dev/%s", entry->d_name);
        
        if ((fd = v4l2_open_device(filename)) < 0)
        {
            continue;
        }

        close(fd);

        if (++count > MAX_VIDEO_DEV_NUMS)
        {
            break;
        }
    }
    
    closedir(dir);
    
    return count > MAX_VIDEO_DEV_NUMS ? MAX_VIDEO_DEV_NUMS : count;
}

void CLVideoCapture::listDevice()
{
    int fd;
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    char filename[512] = {'\0'};
    struct v4l2_capability cap;

    dir = opendir("/dev");
    if (!dir) 
    {
        log_print(HT_LOG_ERR, "%s, opendir failed\r\n", __FUNCTION__);
        return;
    }

    printf("\r\nAvailable video capture device : \r\n\r\n");
    
    while ((entry = readdir(dir))) 
    {
        if (strncmp(entry->d_name, "video", 5))
        {
            continue;
        }

        snprintf(filename, sizeof(filename), "/dev/%s", entry->d_name);
        
        if ((fd = v4l2_open_device(filename)) < 0)
        {
            continue;
        }

        ioctl(fd, VIDIOC_QUERYCAP, &cap);

        close(fd);

        printf("index : %d, name : %s\r\n", count, cap.card);

        if (++count > MAX_VIDEO_DEV_NUMS)
        {
            break;
        }
    }
    
    closedir(dir);
}

int CLVideoCapture::getDeviceIndex(const char * name)
{
    int fd;
    int index = 0;
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    char filename[512] = {'\0'};
    struct v4l2_capability cap;

    dir = opendir("/dev");
    if (!dir) 
    {
        log_print(HT_LOG_ERR, "%s, opendir failed\r\n", __FUNCTION__);
        return 0;
    }
    
    while ((entry = readdir(dir))) 
    {
        if (strncmp(entry->d_name, "video", 5))
        {
            continue;
        }

        snprintf(filename, sizeof(filename), "/dev/%s", entry->d_name);
        
        if ((fd = v4l2_open_device(filename)) < 0)
        {
            continue;
        }

        ioctl(fd, VIDIOC_QUERYCAP, &cap);

        close(fd);
        
        if (strcasecmp((char *)cap.card, name) == 0)
        {
            index = count;
            break;
        }

        if (++count > MAX_VIDEO_DEV_NUMS)
        {
            break;
        }
    }
    
    closedir(dir);
    
    return index;
}

BOOL CLVideoCapture::getDeviceName(int index, char * name, int len)
{
    BOOL ret = FALSE;
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    char filename[512] = {'\0'};
    int fd;

    dir = opendir("/dev");
    if (!dir) 
    {
        log_print(HT_LOG_ERR, "%s, opendir failed\r\n", __FUNCTION__);
        return FALSE;
    }

    while ((entry = readdir(dir))) 
    {
        if (strncmp(entry->d_name, "video", 5))
        {
            continue;
        }

        snprintf(filename, sizeof(filename), "/dev/%s", entry->d_name);
        
        if ((fd = v4l2_open_device(filename)) < 0)
        {
            continue;
        }

        close(fd);
        
        if (count == index)
        {
            ret = TRUE;
            strncpy(name, filename, len);
            break;
        }
        
        count++;        
    }
    
    closedir(dir);
    
    return ret;
}

BOOL CLVideoCapture::initCapture(int codec, int width, int height, double framerate, int bitrate)
{
    int ret;
    char filename[512] = {'\0'};
    uint32 v4l2_format;

    CLock lock(m_pMutex);
    
    if (m_bInited)
    {
        return TRUE;
    }

    m_nFramerate = framerate;
    m_nBitrate = bitrate;

    if (!getDeviceName(m_nDevIndex, filename, sizeof(filename)))
    {
        return FALSE;
    }
    
    m_fd = v4l2_open_device(filename);
    if (m_fd < 0)
    {
        return FALSE;
    }    

    if (!width || !height) 
    {
        struct v4l2_format fmt;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        log_print(HT_LOG_DBG, "Querying the device for the current frame size\r\n");
        
        if (ioctl(m_fd, VIDIOC_G_FMT, &fmt) < 0) 
        {
            log_print(HT_LOG_ERR, "ioctl(VIDIOC_G_FMT) failed\r\n");
            return FALSE;
        }

        m_nWidth  = fmt.fmt.pix.width;
        m_nHeight = fmt.fmt.pix.height;
        log_print(HT_LOG_DBG, "Setting frame size to %dx%d\r\n", m_nWidth, m_nHeight);
    }
    else
    {
        m_nWidth = width;
        m_nHeight = height;
    }

    ret = initDevice(&m_nWidth, &m_nHeight, &v4l2_format);
    if (ret < 0)
    {
        return FALSE;
    }
    
    m_nVideoFormat = v4l2_fmt_to_ht_fmt(v4l2_format);
    if (VIDEO_FMT_NONE == m_nVideoFormat)
    {
        return FALSE;
    }

    ret = mmapInit();
    if (ret < 0)
    {
        return FALSE;
    }

    setCodecFlag(codec);

    if (m_bDecode)
    {
        if (!m_decoder.init(getVideoCodec(m_nVideoFormat), NULL, 0, HW_DECODING_AUTO))
        {
            return FALSE;
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
        params.DstFramerate = framerate;
        params.DstBitrate = bitrate;
            
        if (m_encoder.init(&params) == FALSE)
        {
            return FALSE;
        }
    }

    m_bInited = TRUE;
       
    return TRUE;
}

BOOL CLVideoCapture::capture()
{
    BOOL res = TRUE;
    int ret;
    struct v4l2_buffer buf;

    if (!m_bInited)
    {
        return FALSE;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    ret = ioctl(m_fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) 
    {
        log_print(HT_LOG_ERR, "VIDIOC_DQBUF failed (%d)\r\n", ret);
        return FALSE;
    }

    if (buf.index >= m_nBuffers) 
    {
        log_print(HT_LOG_ERR, "Invalid buffer index received.\r\n");
        return FALSE;
    }

    if (m_bDecode)
    {
        res = m_decoder.decode((uint8 *)m_pBufStart[buf.index], buf.bytesused, 0);
    }
    else if (m_bEncode)
    {
        res = m_encoder.encode((uint8 *)m_pBufStart[buf.index], buf.bytesused);
    }
    else 
    {
        m_encoder.procData((uint8 *)m_pBufStart[buf.index], buf.bytesused);
    }
    
    // re-queen buffer
    ret = ioctl(m_fd, VIDIOC_QBUF, &buf);
    if (ret < 0) 
    {
        log_print(HT_LOG_ERR, "VIDIOC_QBUF failed (%d)\r\n", ret);
        return FALSE;
    }

    return res;    
}

BOOL CLVideoCapture::startCapture()
{
    CLock lock(m_pMutex);
    
    if (m_bCapture)
    {
        return TRUE;
    }
    
    m_bCapture = TRUE;
    m_hCapture = sys_os_create_thread((void *)videoCaptureThread, this);

    return (m_hCapture ? TRUE : FALSE);
}

void CLVideoCapture::stopCapture()
{
    CLock lock(m_pMutex);
    
    m_bCapture = FALSE;

    // wait for capture thread exit
    sys_os_wait_thread(&m_hCapture);

    mmapUninit();

    uninitDevice();

    m_bInited = FALSE;
}

void CLVideoCapture::captureThread()
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


/****************************************************************************/
int CLVideoCapture::initDevice(int *width, int *height, uint32 *desired_format)
{
    int ret = -1, i;
    int w, h;

    for (i = 0; v4l2_fmt_table[i].ht_fmt != VIDEO_FMT_NONE; i++) 
    {
        w = *width;
        h = *height;
        *desired_format = v4l2_fmt_table[i].v4l2_fmt;
        ret = v4l2_init_device(m_fd, &w, &h, desired_format);
        if (ret >= 0 && v4l2_fmt_is_support(*desired_format))
        {
            *width = w;
            *height = h;
            break;
        }
        
        *desired_format = 0;
    }

    if (*desired_format == 0) 
    {
        log_print(HT_LOG_ERR, "Cannot find a proper format\r\n");
        ret = -1;
    }

    log_print(HT_LOG_INFO, "%s, desired_format = 0x%08X\r\n", __FUNCTION__, *desired_format);
    
    return ret;
}

void CLVideoCapture::uninitDevice()
{
    close(m_fd);
    m_fd = 0;
}

int CLVideoCapture::mmapInit()
{    
    int ret;
    uint32 i;
    struct v4l2_requestbuffers reqbuf;
        
    reqbuf.count = QBUF_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    
    ret = ioctl(m_fd , VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) 
    {
        log_print(HT_LOG_ERR, "ioctl(VIDIOC_REQBUFS) failed %d\r\n", ret);
        return ret;
    }

    if (reqbuf.count < 2) 
    {
        log_print(HT_LOG_ERR, "Insufficient buffer memory\r\n", ret);
        return -1;
    }

    m_nBuffers = reqbuf.count;
    m_pBufStart = (void **) malloc(m_nBuffers * sizeof(void *));
    if (NULL == m_pBufStart)
    {
        log_print(HT_LOG_ERR, "malloc failed\r\n", ret);
        return -1;
    }
    
    m_pBufLen = (uint32 *) malloc(m_nBuffers * sizeof(uint32));
    if (NULL == m_pBufLen)
    {
        log_print(HT_LOG_ERR, "malloc failed\r\n", ret);
        return -1;
    }
    
    struct v4l2_buffer buf;

    for (i = 0; i < reqbuf.count; i++) 
    {
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(m_fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) 
        {
            log_print(HT_LOG_ERR, "ioctl(VIDIOC_QUERYBUF failed) %d\r\n", ret);
            return ret;
        }
        
        // mmap buffer
        m_pBufLen[i] = buf.length;
        m_pBufStart[i] = (void *) mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);

        if (m_pBufStart[i] == MAP_FAILED) 
        {
            log_print(HT_LOG_ERR, "mmap (%d) failed: %s\r\n", i, strerror(errno));
            return -1;
        }
    
        // queen buffer
        ret = ioctl(m_fd , VIDIOC_QBUF, &buf);
        if (ret < 0) 
        {
            log_print(HT_LOG_ERR, "VIDIOC_QBUF (%d) failed (%d)\r\n", i, ret);
            return ret;
        }

        log_print(HT_LOG_INFO, "buffer %d: addr=0x%ul, len=%d\r\n", i, (unsigned long)m_pBufStart[i], m_pBufLen[i]);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(m_fd, VIDIOC_STREAMON, &type);
    if (ret < 0) 
    {
        log_print(HT_LOG_ERR, "VIDIOC_STREAMON failed (%d)\r\n", ret);
        return ret;
    }
    
    return 0;
}

void CLVideoCapture::mmapUninit()
{
    uint32 i;
    enum v4l2_buf_type type;    

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /* We do not check for the result, because we could
     * not do anything about it anyway...
     */
    ioctl(m_fd, VIDIOC_STREAMOFF, &type);

    if (m_pBufStart && m_pBufLen)
    {
        for (i = 0; i < m_nBuffers; i++) 
        {
            munmap(m_pBufStart[i], m_pBufLen[i]);
        }
    }

    if (m_pBufStart)
    {
        free(m_pBufStart);
        m_pBufStart = NULL;
    }

    if (m_pBufLen)
    {
        free(m_pBufLen);
        m_pBufLen = NULL;
    }
}

int CLVideoCapture::toLocalVideoFormat(int v4l2_pixfmt)
{
    int pixfmt = VIDEO_FMT_NONE;
    
    switch (v4l2_pixfmt)
    {
    case V4L2_PIX_FMT_YUYV:
        pixfmt = VIDEO_FMT_YUYV422;
        break;

    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
        pixfmt = VIDEO_FMT_YUV420P;
        break;

    case V4L2_PIX_FMT_YUV422P:
        pixfmt = VIDEO_FMT_YUV422P;
        break;
        
    case V4L2_PIX_FMT_UYVY:
        pixfmt = VIDEO_FMT_UYVY422;
        break;    

    case V4L2_PIX_FMT_BGR24:
        pixfmt = VIDEO_FMT_BGR24;
        break;
        
    case V4L2_PIX_FMT_RGB24:
        pixfmt = VIDEO_FMT_RGB24;
        break;

    case V4L2_PIX_FMT_BGR32:
        pixfmt = VIDEO_FMT_BGR32;
        break;

    case V4L2_PIX_FMT_RGB32:
        pixfmt = VIDEO_FMT_RGB32;
        break;
        
    case V4L2_PIX_FMT_NV12:
        pixfmt = VIDEO_FMT_NV12;
        break;
    }

    return pixfmt;
}


