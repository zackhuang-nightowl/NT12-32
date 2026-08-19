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

#ifndef V4L2_COMMON_H
#define V4L2_COMMON_H

struct v4l2_fmt_map 
{
    int     ht_fmt;
    uint32  v4l2_fmt;
};

#ifdef __cplusplus
extern "C" {
#endif

extern const struct v4l2_fmt_map v4l2_fmt_table[];

BOOL v4l2_fmt_is_support(uint32 v4l2_fmt);
int  v4l2_fmt_to_ht_fmt(uint32 v4l2_fmt);

#ifdef __cplusplus
}
#endif

#endif /* V4L2_COMMON_H */

