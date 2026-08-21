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

#ifndef TS_PARSER_H
#define TS_PARSER_H

#include "sys_inc.h"

#define TS_PACKET_SIZE      188
#define TS_SYNC             0x47
#define TS_DISCONTINUITY    0x0

#define TS_PAT_TID          0x00
#define TS_PMT_TID          0x02

#define TS_STREAM_VIDEO     0x1b
#define TS_STREAM_AUDIO     0x0f

#define TS_AUDIO_AAC        0x0f
#define TS_AUDIO_AAC_LATM   0x11
#define TS_VIDEO_MPEG4      0x10
#define TS_VIDEO_H264       0x1b
#define TS_VIDEO_HEVC       0x24

typedef void (*payload_cb)(uint8 *, uint32, uint32, uint64, void *);

typedef struct _TSListItem
{
    void    *mData;
    struct _TSListItem  *mNext;
} TSListItem;

typedef struct _TSList
{
    TSListItem  *mHead;
    TSListItem  *mTail;
} TSList;

typedef struct
{
    uint32  mProgramMapPID;
    uint64  mFirstPTS;
    int     mFirstPTSValid;

    TSList  mStreams;
} TSProgram;

typedef struct
{
    TSProgram   *mProgram;

    uint32  mElementaryPID;
    uint32  mStreamType;
    uint32  mPayloadStarted;

    char    mBuffer[2*1024*1024];
    int     mBufferSize;
} TSStream;

typedef struct
{
    TSList      mPrograms;
    payload_cb  mCallback;
    void       *mUserdata;
    void       *mMutex;
} TSParser;

#ifdef __cplusplus
extern "C" {
#endif

BOOL ts_parser_init(TSParser * parser);
void ts_parser_set_cb(TSParser * parser, payload_cb cb, void * userdata);
BOOL ts_parser_parse(TSParser * parser, uint8 * data, int size);
BOOL ts_parser_parse_packet(TSParser * parser, uint8 * data, int size);
void ts_parser_free(TSParser * parser);

#ifdef __cplusplus
}
#endif

#endif


