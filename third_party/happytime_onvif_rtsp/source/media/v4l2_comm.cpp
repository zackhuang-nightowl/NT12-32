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
#include "v4l2_comm.h"
#include "media_format.h"
#include <linux/videodev2.h>


const struct v4l2_fmt_map v4l2_fmt_table[] = 
{
    { VIDEO_FMT_YUV420P, V4L2_PIX_FMT_YUV420  },
    { VIDEO_FMT_YUV420P, V4L2_PIX_FMT_YVU420  },
    { VIDEO_FMT_YUV422P, V4L2_PIX_FMT_YUV422P },
    { VIDEO_FMT_YUYV422, V4L2_PIX_FMT_YUYV    },
    { VIDEO_FMT_UYVY422, V4L2_PIX_FMT_UYVY    },
    { VIDEO_FMT_BGR24,   V4L2_PIX_FMT_BGR24   },
    { VIDEO_FMT_RGB24,   V4L2_PIX_FMT_RGB24   },
    { VIDEO_FMT_BGR32,   V4L2_PIX_FMT_BGR32   },
    { VIDEO_FMT_RGB32,   V4L2_PIX_FMT_RGB32   },
    { VIDEO_FMT_NV12,    V4L2_PIX_FMT_NV12    },
    { VIDEO_FMT_MJPG,    V4L2_PIX_FMT_MJPEG   },
    { VIDEO_FMT_MJPG,    V4L2_PIX_FMT_JPEG    },
    { VIDEO_FMT_NONE,    0                    },
};

BOOL v4l2_fmt_is_support(uint32 v4l2_fmt)
{
    int i;

    for (i = 0; v4l2_fmt_table[i].ht_fmt != VIDEO_FMT_NONE; i++) 
    {
        if (v4l2_fmt == v4l2_fmt_table[i].v4l2_fmt) 
        {
            return TRUE;
        }
    }

    return FALSE;
}

int v4l2_fmt_to_ht_fmt(uint32 v4l2_fmt)
{
    int i;

    for (i = 0; v4l2_fmt_table[i].ht_fmt != VIDEO_FMT_NONE; i++) 
    {
        if (v4l2_fmt == v4l2_fmt_table[i].v4l2_fmt) 
        {
            return v4l2_fmt_table[i].ht_fmt;
        }
    }

    return VIDEO_FMT_NONE;
}



