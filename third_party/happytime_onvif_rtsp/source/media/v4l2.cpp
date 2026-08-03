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
#include "v4l2.h"
#include <linux/videodev2.h>


int v4l2_open_device(const char * filename)
{ 
    int fd;
    int flags = O_RDWR;
    struct v4l2_format fmt;
    struct v4l2_capability cap;

    fd = open(filename, flags, 0);
    if (fd < 0) 
    {
        log_print(HT_LOG_ERR, "%s, open device(%s) failed. %s\r\n", 
            __FUNCTION__, filename, strerror(errno));
        return -1;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) 
    {
        log_print(HT_LOG_ERR, "%s, %s, ioctl(VIDIOC_QUERYCAP) failed. %s\r\n", 
            __FUNCTION__, filename, strerror(errno));
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
    {
        log_print(HT_LOG_ERR, "%s, %s, Not a video capture device.\r\n", 
            __FUNCTION__, filename);
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) 
    {
        log_print(HT_LOG_ERR, "%s, %s, The device does not support the streaming I/O method.\r\n", 
            __FUNCTION__, filename);
        goto fail;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) 
    {
        log_print(HT_LOG_ERR, "%s, %s, ioctl(VIDIOC_G_FMT) failed\r\n",
            __FUNCTION__, filename);
        goto fail;
    }

    return fd;

fail:
    close(fd);
    return -1;
}


int v4l2_init_device(int fd, int *width, int *height, uint32 *pixelformat)
{
    struct v4l2_format fmt;

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = *pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    /* Some drivers will fail and return EINVAL when the pixelformat
       is not supported (even if type field is valid and supported) */
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        log_print(HT_LOG_ERR, "ioctl(VIDIOC_S_FMT) failed\r\n");
        return -1;
    }
    
    if ((*width != (int) fmt.fmt.pix.width) || (*height != (int) fmt.fmt.pix.height)) 
    {
        log_print(HT_LOG_INFO, "The V4L2 driver changed the video from %dx%d to %dx%d\n", 
            *width, *height, fmt.fmt.pix.width, fmt.fmt.pix.height);
            
        *width = fmt.fmt.pix.width;
        *height = fmt.fmt.pix.height;
    }

    if (*pixelformat != fmt.fmt.pix.pixelformat) 
    {
        log_print(HT_LOG_DBG, "The V4L2 driver changed the pixel format from 0x%08X to 0x%08X\n",
            *pixelformat, fmt.fmt.pix.pixelformat);
            
        *pixelformat = fmt.fmt.pix.pixelformat;
    }
    else
    {
        log_print(HT_LOG_DBG, "%s, pixelformat = 0x%08X\r\n", __FUNCTION__, pixelformat);
    }

    if (fmt.fmt.pix.field == V4L2_FIELD_INTERLACED) 
    {
        log_print(HT_LOG_DBG, "The V4L2 driver is using the interlaced mode\n");
    }

    return 0;
}




