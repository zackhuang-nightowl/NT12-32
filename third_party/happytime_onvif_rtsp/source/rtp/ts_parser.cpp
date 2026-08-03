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

#include "ts_parser.h"
#include "bs.h"


BOOL ts_parser_add_item_to_list(TSList * list, void * data)
{
    TSListItem * item = (TSListItem *)malloc(sizeof(TSListItem));
    if (NULL == item)
    {
        return FALSE;
    }
    
    item->mData = data;
    item->mNext = NULL;

    if (list->mTail != NULL)
    {
        list->mTail->mNext = item;
        list->mTail = item;
    }
    else
    {
        list->mHead = list->mTail = item;
    }

    return TRUE;
}

BOOL ts_parser_add_program(TSParser * parser, uint32 program_map_pid)
{
    TSProgram * program = (TSProgram *)malloc(sizeof(TSProgram));
    if (NULL == program)
    {
        return FALSE;
    }
    
    memset(program, 0, sizeof(TSProgram));

    program->mProgramMapPID = program_map_pid;

    return ts_parser_add_item_to_list(&parser->mPrograms, program);
}

BOOL ts_parser_add_stream(TSProgram * program, uint32 elementaryPID, uint32 streamType)
{
    TSStream * stream = (TSStream *)malloc(sizeof(TSStream));
    if (NULL == stream)
    {
        return FALSE;
    }
    
    memset(stream, 0, sizeof(TSStream));

    stream->mProgram = program;
    stream->mElementaryPID = elementaryPID;
    stream->mStreamType = streamType;

    // todo : The maximum receive buffer should be allocated based on streamType

    log_print(HT_LOG_INFO, "%s, add stream, pid: %d, stream type: %d (%X)\r\n", 
        __FUNCTION__, elementaryPID, streamType, streamType);

    return ts_parser_add_item_to_list(&program->mStreams, stream);
}

TSStream * ts_parser_get_stream_by_pid(TSProgram * program, uint32 pid)
{
    TSListItem *item;
    TSStream *stream;
    
    for (item = program->mStreams.mHead; item != NULL; item = item->mNext)
    {
        stream = (TSStream *)item->mData;
        
        if (stream != NULL && stream->mElementaryPID == pid)
        {
            return stream;
        }
    }
    
    return NULL;
}

int64 ts_parser_parse_timestamp(bs_t * bs)
{
    int64 result = ((uint64)bs_read(bs, 3)) << 30;
    bs_skip(bs, 1);
    result |= ((uint64)bs_read(bs, 15)) << 15;
    bs_skip(bs, 1);
    result |= bs_read(bs, 15);

    return result;
}

int64 ts_parser_pts_to_timestamp(TSStream * stream, uint64 PTS)
{
    if (!stream->mProgram->mFirstPTSValid)
    {
        stream->mProgram->mFirstPTSValid = 1;
        stream->mProgram->mFirstPTS = PTS;
        PTS = 0;
    }
    else if (PTS < stream->mProgram->mFirstPTS)
    {
        PTS = 0;
    }
    else
    {
        PTS -= stream->mProgram->mFirstPTS;
    }

    return (PTS * 100) / 9;
}

void ts_parser_on_payload_data(TSParser * parser, TSStream * stream, uint32 PTS_DTS_flag, uint64 PTS, uint64 DTS, uint8 *data, uint32 size)
{
    // int64 timeUs = ts_parser_pts_to_timestamp(stream, PTS);

    sys_os_mutex_enter(parser->mMutex);
    if (parser->mCallback)
    {
        parser->mCallback(data, size, stream->mStreamType, PTS, parser->mUserdata);
    }
    sys_os_mutex_leave(parser->mMutex);
}

void ts_parser_parse_pes(TSParser * parser, TSStream * stream, bs_t * bs)
{
    bs_read(bs, 24); // packet_startcode_prefix
    
    uint32 stream_id = bs_read(bs, 8);
    uint32 PES_packet_length = bs_read(bs, 16);

    if (stream_id != 0xbc  // program_stream_map
        && stream_id != 0xbe  // padding_stream
        && stream_id != 0xbf  // private_stream_2
        && stream_id != 0xf0  // ECM
        && stream_id != 0xf1  // EMM
        && stream_id != 0xff  // program_stream_directory
        && stream_id != 0xf2  // DSMCC
        && stream_id != 0xf8)   // H.222.1 type E
    {
        uint32 PTS_DTS_flags;
        uint32 ESCR_flag;
        uint32 ES_rate_flag;
        uint32 PES_header_data_length;
        uint32 optional_bytes_remaining;
        uint64 PTS = 0, DTS = 0;

        bs_skip(bs, 8);

        PTS_DTS_flags = bs_read(bs, 2);
        ESCR_flag = bs_read(bs, 1);
        ES_rate_flag = bs_read(bs, 1);
        bs_read(bs, 1); // DSM_trick_mode_flag
        bs_read(bs, 1); // additional_copy_info_flag

        bs_skip(bs, 2);

        PES_header_data_length = bs_read(bs, 8);
        optional_bytes_remaining = PES_header_data_length;

        if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3)
        {
            bs_skip(bs, 4);
            PTS = ts_parser_parse_timestamp(bs);
            bs_skip(bs, 1);

            optional_bytes_remaining -= 5;

            if (PTS_DTS_flags == 3)
            {
                bs_skip(bs, 4);

                DTS = ts_parser_parse_timestamp(bs);
                bs_skip(bs, 1);

                optional_bytes_remaining -= 5;
            }
        }

        if (ESCR_flag)
        {
            bs_skip(bs, 2);

            ts_parser_parse_timestamp(bs);

            bs_skip(bs, 11);

            optional_bytes_remaining -= 6;
        }

        if (ES_rate_flag)
        {
            bs_skip(bs, 24);
            optional_bytes_remaining -= 3;
        }

        bs_skip(bs, optional_bytes_remaining * 8);

        // ES data follows.
        if (PES_packet_length != 0)
        {
            uint32 dataLength = PES_packet_length - 3 - PES_header_data_length;

            // Signaling we have payload data
            ts_parser_on_payload_data(parser, stream, PTS_DTS_flags, PTS, DTS, bs_data(bs), dataLength);

            bs_skip(bs, dataLength * 8);
        }
        else
        {
            // Signaling we have payload data
            ts_parser_on_payload_data(parser, stream, PTS_DTS_flags, PTS, DTS, bs_data(bs), bs_left(bs) / 8);
        }
    }
    else if (stream_id == 0xbe)
    {  
        // padding_stream
        bs_skip(bs, PES_packet_length * 8);
    }
    else
    {
        bs_skip(bs, PES_packet_length * 8);
    }
}

void ts_parser_flush_stream_data(TSParser * parser, TSStream * stream)
{
    bs_t bs;
    bs_init(&bs, stream->mBuffer, stream->mBufferSize);

    ts_parser_parse_pes(parser, stream, &bs);

    stream->mBufferSize = 0;
}

BOOL ts_parser_parse_stream(TSParser * parser, TSStream * stream, uint32 payload_unit_start_indicator, bs_t * bs)
{
    uint32 payload_size;

    if (payload_unit_start_indicator)
    {
        if (stream->mPayloadStarted)
        {
            ts_parser_flush_stream_data(parser, stream);
        }

        stream->mPayloadStarted = 1;
    }

    if (!stream->mPayloadStarted)
    {
        return FALSE;
    }

    payload_size = bs_left(bs) / 8;

    if (stream->mBufferSize + payload_size > ARRAY_SIZE(stream->mBuffer))
    {
        log_print(HT_LOG_WARN, "%s, packet size(%d) is too big!!!\r\n",
            __FUNCTION__, stream->mBufferSize + payload_size);
        return FALSE;
    }
    
    memcpy(stream->mBuffer + stream->mBufferSize, bs_data(bs), payload_size);
    stream->mBufferSize += payload_size;

    return TRUE;
}

BOOL ts_parser_parse_program_map(TSParser * parser, TSProgram * program, bs_t * bs)
{
    uint32 table_id;
    uint32 section_length;
    uint32 program_info_length;
    size_t infoBytesRemaining;
    uint32 streamType;
    uint32 elementaryPID;
    uint32 ES_info_length;
    uint32 info_bytes_remaining;

    table_id = bs_read(bs, 8);
    if (TS_PMT_TID != table_id)
    {
        log_print(HT_LOG_ERR, "%s, table_id=%d\r\n", __FUNCTION__, table_id);
        return FALSE;
    }

    bs_read(bs, 1); // section_syntax_indicator

    bs_skip(bs, 3); // Reserved

    section_length = bs_read(bs, 12);

    bs_skip(bs, 16);// program_number
    bs_skip(bs, 2); // reserved
    bs_skip(bs, 5); // version_number
    bs_skip(bs, 1); // current_next_indicator
    bs_skip(bs, 8); // section_number
    bs_skip(bs, 8); // last_section_number
    bs_skip(bs, 3); // reserved
    bs_skip(bs, 13);// PCR_PID
    bs_skip(bs, 4); // reserved

    program_info_length = bs_read(bs, 12);

    bs_skip(bs, program_info_length * 8);   // skip descriptors

    // infoBytesRemaining is the number of bytes that make up the
    // variable length section of ES_infos. It does not include the
    // final CRC.
    infoBytesRemaining = section_length - 9 - program_info_length - 4;

    while (infoBytesRemaining > 0)
    {
        streamType = bs_read(bs, 8);

        bs_skip(bs, 3); // reserved

        elementaryPID = bs_read(bs, 13);

        bs_skip(bs, 4); // reserved

        ES_info_length = bs_read(bs, 12);

        info_bytes_remaining = ES_info_length;
        while (info_bytes_remaining >= 2)
        {
            uint32 descLength;

            bs_skip(bs, 8); // tag

            descLength = bs_read(bs, 8);

            bs_skip(bs, descLength * 8);

            info_bytes_remaining -= descLength + 2;
        }

        if (ts_parser_get_stream_by_pid(program, elementaryPID) == NULL)
        {
            ts_parser_add_stream(program, elementaryPID, streamType);
        }

        infoBytesRemaining -= 5 + ES_info_length;
    }

    bs_skip(bs, 32); // CRC

    return TRUE;
}

BOOL ts_parser_parse_pmt(TSParser * parser, bs_t * bs)
{
    size_t i;
    uint32 table_id = bs_read(bs, 8);
    if (TS_PAT_TID != table_id)
    {
        log_print(HT_LOG_ERR, "%s, table_id=%d\r\n", __FUNCTION__, table_id);
        return FALSE;
    }

    bs_read(bs, 1); // section_syntax_indicator
    bs_skip(bs, 1);
    bs_skip(bs, 2); // reserved

    uint32 section_length = bs_read(bs, 12);

    bs_skip(bs, 16);// transport_stream_id
    bs_skip(bs, 2); // reserved
    bs_skip(bs, 5); // version_number
    bs_skip(bs, 1); // current_next_indicator
    bs_skip(bs, 8); // section_number
    bs_skip(bs, 8); // last_section_number

    size_t numProgramBytes = (section_length - 5 /* header */ - 4 /* crc */);

    for (i = 0; i < numProgramBytes / 4; ++i)
    {
        uint32 program_number = bs_read(bs, 16);

        bs_skip(bs, 3); // reserved

        if (program_number == 0)
        {
            bs_skip(bs, 13); // network_PID
        }
        else
        {
            uint32 programMapPID = bs_read(bs, 13);
            
            ts_parser_add_program(parser, programMapPID);
        }
    }
    
    bs_skip(bs, 32); // CRC

    return TRUE;
}

BOOL ts_parser_parse_program(TSParser * parser, bs_t * bs, uint32 pid, uint32 payload_unit_start_indicator)
{
    BOOL handled = 0;
    TSListItem *item;
    TSProgram *program;

    if (pid == 0)
    {
        if (payload_unit_start_indicator)
        {
            uint32 skip = bs_read(bs, 8);
            bs_skip(bs, skip * 8);
        }

        return ts_parser_parse_pmt(parser, bs);
    }

    for (item = parser->mPrograms.mHead; item != NULL; item = item->mNext)
    {
        program = (TSProgram *)item->mData;
        
        if (pid == program->mProgramMapPID)
        {
            if (payload_unit_start_indicator)
            {
                uint32 skip = bs_read(bs, 8);
                bs_skip(bs, skip * 8);
            }

            handled = ts_parser_parse_program_map(parser, program, bs);
            break;
        }
        else
        {
            TSStream * pStream = ts_parser_get_stream_by_pid(program, pid);
            if (pStream != NULL)
            {
                handled = ts_parser_parse_stream(parser, pStream, payload_unit_start_indicator, bs);
                break;
            }
        }
    }

    if (!handled)
    {
        // log_print(HT_LOG_WARN, "%s, PID 0x%04x not handled.\n", __FUNCTION__, pid);
    }

    return handled;
}

void ts_parser_parse_adaptation_field(TSParser * parser, bs_t * bs)
{
    uint32 adaptation_field_length = bs_read(bs, 8);
    if (adaptation_field_length > 0)
    {
        bs_skip(bs, adaptation_field_length * 8);
    }
}

BOOL ts_parser_parse_packet(TSParser * parser, uint8 * data, int size)
{
    uint32 transport_error_indicator;
    uint32 payload_unit_start_indicator;
    uint32 pid;
    uint32 adaptation_field_control;

    bs_t bs;
    bs_init(&bs, data, size);

    bs_read(&bs, 8); // sync_byte

    transport_error_indicator = bs_read(&bs, 1);
    if (transport_error_indicator != 0)
    {
        log_print(HT_LOG_WARN, "%s, transport error indicator: %u\n", 
            __FUNCTION__, transport_error_indicator);
    }

    payload_unit_start_indicator = bs_read(&bs, 1); 
    bs_read(&bs, 1); // transport_priority
    pid = bs_read(&bs, 13);
    bs_read(&bs, 2); // transport_scrambling_control
    adaptation_field_control = bs_read(&bs, 2);
    bs_read(&bs, 4); // continuity_counter

    if (adaptation_field_control == 2 || adaptation_field_control == 3)
    {
        ts_parser_parse_adaptation_field(parser, &bs);
    }

    if (adaptation_field_control == 1 || adaptation_field_control == 3)
    {
        ts_parser_parse_program(parser, &bs, pid, payload_unit_start_indicator);
    }

    return TRUE;
}

BOOL ts_parser_parse(TSParser * parser, uint8 * data, int size)
{
    for (;;) 
    {
        if (size == 0)
        {
            break;
        }
        else if (size < TS_PACKET_SIZE)
        {
            return FALSE;
        }
        
        if (data[0] != TS_SYNC) 
        {
            data++;
            size--;
        } 
        else 
        {
            if (!ts_parser_parse_packet(parser, data, TS_PACKET_SIZE))
            {
                return FALSE;
            }
            
            data += TS_PACKET_SIZE;
            size -= TS_PACKET_SIZE;
        }
    }

    return TRUE;
}

BOOL ts_parser_init(TSParser * parser)
{
    if (NULL == parser)
    {
        return FALSE;
    }

    memset(parser, 0, sizeof(TSParser));

    parser->mMutex = sys_os_create_mutex();

    return TRUE;
}

void ts_parser_set_cb(TSParser * parser, payload_cb cb, void * userdata)
{
    if (NULL == parser)
    {
        return;
    }

    sys_os_mutex_enter(parser->mMutex);
    parser->mCallback = cb;
    parser->mUserdata = userdata;
    sys_os_mutex_leave(parser->mMutex);
}

void ts_parser_free_program(TSProgram * program)
{
    // Free Streams
    TSListItem *item;
    TSStream *stream;
    
    while (program->mStreams.mHead != NULL)
    {
        item = program->mStreams.mHead;
        program->mStreams.mHead = item->mNext;

        if(item->mData != NULL)
        {
            stream = (TSStream *)item->mData;
            free(stream);
        }

        free(item);
    }
}

void ts_parser_free(TSParser * parser)
{
    // Free Programs
    TSListItem *item;
    TSProgram *program;
    
    while (parser->mPrograms.mHead != NULL)
    {
        item = parser->mPrograms.mHead;
        parser->mPrograms.mHead = item->mNext;

        if (item->mData != NULL)
        {
            program = (TSProgram *)item->mData;
            ts_parser_free_program(program);
            free(program);
        }

        free(item);
    }

    sys_os_destroy_mutex(parser->mMutex);
}




