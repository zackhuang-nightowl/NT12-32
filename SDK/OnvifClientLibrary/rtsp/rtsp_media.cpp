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
#include "rtsp_media.h"
#include "rtsp_rsua.h"
#include "rtp_tx.h"
#include "hqueue.h"
#include "media_format.h"
#include "rtsp_cfg.h"
#include "rtsp_srv.h"
#include "rtsp_util.h"
#include "net_util.h"
#ifdef MEDIA_FILE
#include "media_codec.h"
#endif


/***************************************************************************************/

#define RTSP_QUEUE_SIZE 8

/***************************************************************************************/

BOOL rtsp_get_url_path(const char * url, char * path, int path_size)
{
    char host[128];
    
    url_split(url, NULL, 0, NULL, 0, NULL, 0, host, sizeof(host), NULL, path, path_size);

    if (host[0] == '\0')
    {
        return FALSE;
    }
    
    return TRUE;
}

void rtsp_media_get_video_sdp_line(void * rua, uint8 pt, char * buff, int size)
{
    RSSUA * p_rua = (RSSUA *)rua;

#ifdef MEDIA_PROXY
    if (p_rua->media_info.is_proxy)
    {
        if (p_rua->media_info.proxy)
        {
            p_rua->media_info.proxy->getVideoAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_PUSHER
    if (p_rua->media_info.is_pusher)
    {
        if (p_rua->media_info.pusher)
        {
            p_rua->media_info.pusher->getVideoAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_FILE    
    if (p_rua->media_info.is_file)
    {
        if (p_rua->media_info.file_demuxer)
        {
            p_rua->media_info.file_demuxer->getVideoAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_DEVICE
    if (p_rua->media_info.is_screen)
    {
        if (p_rua->media_info.screen_capture)
        {
            p_rua->media_info.screen_capture->getAuxSDPLine(buff, size, pt);
        }
    }
    else if (p_rua->media_info.is_window)
    {
        if (p_rua->media_info.window_capture)
        {
            p_rua->media_info.window_capture->getAuxSDPLine(buff, size, pt);
        }
    }
    else if (p_rua->media_info.video_capture)
    {
        p_rua->media_info.video_capture->getAuxSDPLine(buff, size, pt);
    }
    else
#endif
#ifdef MEDIA_LIVE
    if (p_rua->media_info.is_live)
    {
        if (p_rua->media_info.live_video)
        {
            p_rua->media_info.live_video->getAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
    {
    }
}

void rtsp_media_get_audio_sdp_line(void * rua, uint8 pt, char * buff, int size)
{
    RSSUA * p_rua = (RSSUA *)rua;

#ifdef MEDIA_PROXY
    if (p_rua->media_info.is_proxy)
    {
        if (p_rua->media_info.proxy)
        {
            p_rua->media_info.proxy->getAudioAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_PUSHER
    if (p_rua->media_info.is_pusher)
    {
        if (p_rua->media_info.pusher)
        {
            p_rua->media_info.pusher->getAudioAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_FILE    
    if (p_rua->media_info.is_file)
    {
        if (p_rua->media_info.file_demuxer)
        {
            p_rua->media_info.file_demuxer->getAudioAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_DEVICE
    if (p_rua->media_info.is_device)
    {
        if (p_rua->media_info.audio_capture)
        {
            p_rua->media_info.audio_capture->getAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
#ifdef MEDIA_LIVE
    if (p_rua->media_info.is_live)
    {
        if (p_rua->media_info.live_audio)
        {
            p_rua->media_info.live_audio->getAuxSDPLine(buff, size, pt);
        }
    }
    else
#endif
    {
    }
}

void rtsp_media_fix_audio_param(RSSUA * p_rua)
{
    if (AUDIO_CODEC_G726 == p_rua->media_info.a_info.codec)
    {
        p_rua->media_info.a_info.channels = 1; // G726 only support mono
        p_rua->media_info.a_info.samplerate = 8000;

        if (0 == p_rua->media_info.a_info.bitrate)
        {
            p_rua->media_info.a_info.bitrate = 16;
        }
        
        return;
    }

    if (AUDIO_CODEC_G722 == p_rua->media_info.a_info.codec)
    {
        p_rua->media_info.a_info.channels = 1; // G722 only support mono
        p_rua->media_info.a_info.samplerate = 16000;
        return;
    }

    if (AUDIO_CODEC_OPUS == p_rua->media_info.a_info.codec)
    {
        p_rua->media_info.a_info.channels = 2;
        p_rua->media_info.a_info.samplerate = 48000;
        return;
    }

    const int sample_rates[] = 
    {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
    };

    int i;
    int sample_rate_num = sizeof(sample_rates) / sizeof(int);

    for (i = 0; i < sample_rate_num; i++)
    {
        if (p_rua->media_info.a_info.samplerate <= sample_rates[i])
        {
            p_rua->media_info.a_info.samplerate = sample_rates[i];
            break;
        }
    }

    if (i == sample_rate_num)
    {
        p_rua->media_info.a_info.samplerate = 48000;
    }
}

BOOL rtsp_parse_url_transfer_parameters(RSSUA * p_rua)
{
    char value[32] = {'\0'};
    char * p = strchr(p_rua->media_info.filename, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->media_info.filename, '&');
    }
    
    if (NULL == p)
    {
        return FALSE;
    }

    p++; // skip  '?' or '&' char
    
    if (get_name_value_pair(p, strlen(p), "t", value, sizeof(value)))
    {
        if (strcasecmp(value, "unicast") == 0)
        {
            p_rua->rtp_unicast = 1;
        }
        else if (strcasecmp(value, "multicast") == 0)
        {
            p_rua->rtp_unicast = 0;
        }
    }

    if (get_name_value_pair(p, strlen(p), "p", value, sizeof(value)))
    {
        if (strcasecmp(value, "udp") == 0)
        {
            p_rua->rtp_tcp = 0;
        }
        else if (strcasecmp(value, "tcp") == 0)
        {
            p_rua->rtp_tcp = 1;
        }
        else if (strcasecmp(value, "rtsp") == 0)
        {
            p_rua->rtp_tcp = 1;
        }
        else if (strcasecmp(value, "http") == 0)
        {

        }
    }

    return TRUE;
}

BOOL rtsp_parse_url_video_parameters(RSSUA * p_rua)
{
    char value[32] = {'\0'};
    char * p = strchr(p_rua->media_info.filename, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->media_info.filename, '&');
    }
    
    if (NULL == p)
    {
        return FALSE;
    }

    p++; // skip  '?' or '&' char

    if (get_name_value_pair(p, strlen(p), "ve", value, sizeof(value)))
    {
        if (strcasecmp(value, "H264") == 0)
        {
            p_rua->media_info.v_info.codec = VIDEO_CODEC_H264;
        }
        else if (strcasecmp(value, "H265") == 0)
        {
            p_rua->media_info.v_info.codec = VIDEO_CODEC_H265;
        }
        else if (strcasecmp(value, "JPEG") == 0 || strcasecmp(value, "MJPEG") == 0)
        {
            p_rua->media_info.v_info.codec = VIDEO_CODEC_JPEG;
        }
        else if (strcasecmp(value, "MP4V-ES") == 0 || strcasecmp(value, "MP4") == 0)
        {
            p_rua->media_info.v_info.codec = VIDEO_CODEC_MP4;
        }
    }

    if (get_name_value_pair(p, strlen(p), "fps", value, sizeof(value)))
    {
        p_rua->media_info.v_info.framerate = atof(value);
    }
    
    if (get_name_value_pair(p, strlen(p), "w", value, sizeof(value)))
    {
        p_rua->media_info.v_info.width = atoi(value);
    }

    if (get_name_value_pair(p, strlen(p), "h", value, sizeof(value)))
    {
        p_rua->media_info.v_info.height = atoi(value);
    }

    if (get_name_value_pair(p, strlen(p), "vb", value, sizeof(value)))
    {
        p_rua->media_info.v_info.bitrate = atoi(value);
    }

    return TRUE;
}

BOOL rtsp_parse_url_audio_parameters(RSSUA * p_rua)
{
    char value[32] = {'\0'};
    char * p = strchr(p_rua->media_info.filename, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->media_info.filename, '&');
    }
    
    if (NULL == p)
    {
        return FALSE;
    }

    p++; // skip  '?' or '&' char

    if (get_name_value_pair(p, strlen(p), "ae", value, sizeof(value)))
    {
        if (strcasecmp(value, "PCMU") == 0 || strcasecmp(value, "G711U") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_G711U;
        }
        else if (strcasecmp(value, "PCMA") == 0 || strcasecmp(value, "G711A") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_G711A;
        }
        else if (strcasecmp(value, "G726") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_G726;
        }
        else if (strcasecmp(value, "G722") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_G722;
        }
        else if (strcasecmp(value, "OPUS") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_OPUS;
        }
        else if (strcasecmp(value, "MP4A-LATM") == 0 || strcasecmp(value, "AAC") == 0)
        {
            p_rua->media_info.a_info.codec = AUDIO_CODEC_AAC;
        }
    }

    if (get_name_value_pair(p, strlen(p), "sr", value, sizeof(value)))
    {
        p_rua->media_info.a_info.samplerate = atoi(value);
    }

    if (get_name_value_pair(p, strlen(p), "ch", value, sizeof(value)))
    {
        p_rua->media_info.a_info.channels = atoi(value);
    }

    if (get_name_value_pair(p, strlen(p), "ab", value, sizeof(value)))
    {
        p_rua->media_info.a_info.bitrate = atoi(value);
    }

    return TRUE;
}

void rtsp_media_clear_queue(HQUEUE * queue)
{
    UA_PACKET packet;
    
    while (!hqueue_is_empty(queue))
    {
        if (hqueue_get(queue, (char *)&packet))
        {
            if (packet.data != NULL && packet.size != 0)
            {
                free(packet.buff);
            }
        }
        else
        {
            // should be not to here
            log_print(HT_LOG_ERR, "%s, hqueue_get failed\r\n", __FUNCTION__);
            break;
        }
    }
}

#ifdef RTSP_REPLAY

void rtsp_update_replay_ntp_time(RSSUA * p_rua, int av_t)
{
    uint64 ntp_time;

    if (p_rua->play_range && p_rua->play_range_type == 1)
    {
        uint32 diff = sys_os_get_ms() - p_rua->s_replay_time;

        if (p_rua->play_range_end != 0)
        {
            if (p_rua->play_range_begin + diff / 1000 <= p_rua->play_range_end)
            {
                ntp_time = p_rua->play_range_begin * 1000000 + diff * 1000;
                ntp_time = (ntp_time / 1000) * 1000 + NTP_OFFSET_US;
            }
            else
            {
                ntp_time = p_rua->play_range_end * 1000000;
                ntp_time = (ntp_time / 1000) * 1000 + NTP_OFFSET_US;
            }
        }
        else
        {
            ntp_time = p_rua->play_range_begin * 1000000 + diff * 1000;
            ntp_time = (ntp_time / 1000) * 1000 + NTP_OFFSET_US;
        }
    }
    else
    {
        ntp_time = get_ntp_time();
    }
                
    p_rua->channels[av_t].rep_hdr.ntp_sec = ntp_time / 1000000;
    p_rua->channels[av_t].rep_hdr.ntp_frac = ((ntp_time % 1000000) << 32) / 1000000;
}

#endif

#ifdef RTSP_METADATA

BOOL rtsp_media_get_metadata(RSSUA * p_rua, UA_PACKET * p_packet)
{
    int offset;
    int buflen = 1024;
    
    // todo : here just generate test metadata
    
    p_packet->buff = (uint8*) malloc(buflen + RTSP_RESV_HDR_SIZE);
    if (NULL == p_packet->buff)
    {
        return FALSE;
    }
    
    p_packet->data = p_packet->buff + RTSP_RESV_HDR_SIZE;
    p_packet->waitnext = 1;

    time_t t = time(NULL);
    struct tm * t1 = localtime(&t);
    char utctime[64] = {'\0'};

    snprintf(utctime, sizeof(utctime), "%04d-%02d-%02dT%02d:%02d:%02d",
        t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
        t1->tm_hour, t1->tm_min, t1->tm_sec);
        
    offset = snprintf((char *)p_packet->data, buflen, 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<tt:MetadataStream xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<tt:Event>"
                "<wsnt:NotificationMessage xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\">"
                    "<wsnt:Topic Dialect=\"%s\">%s</wsnt:Topic>"
                    "<wsnt:Message>"
                        "<tt:Message PropertyOperation=\"Changed\" UtcTime=\"%s\">"
                        "<tt:Source>"
                            "<tt:SimpleItem Name=\"InputToken\" Value=\"DIGIT_INPUT_000\" />"
                        "</tt:Source>"
                        "<tt:Data>"
                            "<tt:SimpleItem Name=\"LogicalState\" Value=\"true\" />"
                        "</tt:Data>"
                        "</tt:Message>"
                    "</wsnt:Message>"
                "</wsnt:NotificationMessage>"
            "</tt:Event>"
        "</tt:MetadataStream>",
        "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet",
        "tns1:Device/Trigger/DigitalInput",
        utctime);

    p_packet->size = offset;
    
    return TRUE;
}

void * rtsp_media_metadata_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *)argv;
    UA_PACKET packet;
    int sret = -1;
    int64 cur_delay = 0;
    int64 pre_delay = 0;
    int timeout;
    uint32 cur_time = 0;
    uint32 pre_time = 0;
    BOOL dflag;
    BOOL mflag = FALSE;

    if (p_rua->media_info.v_info.framerate)
    {
        timeout = 1000000.0 / p_rua->media_info.v_info.framerate;
    }
    else
    {
        timeout = 1000000.0 / 25;
    }
    
#ifdef MEDIA_PROXY
    if (p_rua->media_info.is_proxy && p_rua->media_info.proxy)
    {
        CRtspClient * p_rtsp = p_rua->media_info.proxy->m_rtsp;
        if (p_rtsp)
        {
            mflag = p_rtsp->get_rua()->channels[AV_METADATA_CH].setup;
        }
    }
#endif
#ifdef MEDIA_PUSHER
    if (p_rua->media_info.is_pusher && p_rua->media_info.pusher)
    {
        mflag = TRUE;
    }
#endif

    while (p_rua->rtp_tx)
    {
        if (p_rua->rtp_pause)
        {
            pre_time = 0;
            usleep(10*1000);
            continue;
        }

        // The rtsp proxy or rtsp pusher stream has metadata data, 
        //  retrieve metadata data from the queue, 
        //  otherwise generate test metadata data
        if (mflag)
        {
            dflag = hqueue_get(p_rua->media_info.m_queue, (char *)&packet);
        }
        else
        {
            dflag = rtsp_media_get_metadata(p_rua, &packet);
        }
        
        if (dflag)
        {
            if (packet.data == NULL || packet.size == 0)
            {
                break;
            }

#ifdef RTSP_REPLAY
            if (p_rua->replay)
            {
                rtsp_update_replay_ntp_time(p_rua, AV_METADATA_CH);
            }
#endif

            sret = rtp_metadata_tx(p_rua, packet.data, packet.size, get_rtp_timestamp(90000));

            free(packet.buff);

            if (sret < 0)
            {
                rtsp_stop_rua(p_rua);
                break;
            }

            if (packet.waitnext)
            {
                cur_time = sys_os_get_ms();
                cur_delay = timeout;

                if (p_rua->scale_flag)
                {
                    cur_delay /= p_rua->scale / 100.0f;
                }

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
        }
        else
        {
            // should be not to here
            log_print(HT_LOG_ERR, "%s, hqueue_get failed\r\n", __FUNCTION__);
            break;
        }
    }

    p_rua->media_info.metadata_tx = 0;
    
    rtsp_media_clear_queue(p_rua->media_info.m_queue);

    log_print(HT_LOG_DBG, "%s, exit, p_rua = %p\r\n", __FUNCTION__, p_rua);
    
    return NULL;
}

void rtsp_media_create_metadata_queue(RSSUA * p_rua, uint32 size, uint32 flag)
{
    if (p_rua->media_info.m_queue)
    {
        return;
    }
    
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        p_rua->media_info.m_queue = hqueue_create(size, sizeof(UA_PACKET), flag);

        p_rua->media_info.metadata_tx = 1;
        p_rua->media_info.m_thread = sys_os_create_thread((void *)rtsp_media_metadata_thread, (void*)p_rua);
    }
}

void rtsp_media_put_metadata(RSSUA * p_rua, uint8 *data, int size, int waitnext = 1)
{
    UA_PACKET packet;

    if (!p_rua->media_info.metadata_tx)
    {
        return;
    }
    
    if (NULL == data || 0 == size)
    {
        memset(&packet, 0, sizeof(packet));

        hqueue_put(p_rua->media_info.m_queue, (char *)&packet);
    }
    else if (p_rua->rtp_tx)
    {
        packet.buff = (uint8*)malloc(size + RTSP_RESV_HDR_SIZE);
        if (packet.buff)
        {
            packet.data = packet.buff + RTSP_RESV_HDR_SIZE;
            memcpy(packet.data, data, size);
            packet.size = size;
            packet.waitnext = waitnext;

            if (hqueue_put(p_rua->media_info.m_queue, (char *)&packet) == FALSE)
            {
                free(packet.buff);
            }
        }
    }
}

void rtsp_media_free_metadata_queue(RSSUA * p_rua)
{
    if (p_rua->media_info.m_queue)
    {
        rtsp_media_put_metadata(p_rua, NULL, 0, 0);
    }

    sys_os_wait_thread(&p_rua->media_info.m_thread);

    rtsp_media_clear_queue(p_rua->media_info.m_queue);
    
    hqueue_delete(p_rua->media_info.m_queue);
    p_rua->media_info.m_queue = NULL;
}

#endif // RTSP_METADATA

void * rtsp_media_video_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *)argv;
    UA_PACKET packet;
    int sret = -1;
    int64 cur_delay = 0;
    int64 pre_delay = 0;
    int timeout = 1000000.0 / p_rua->media_info.v_info.framerate;
    uint32 cur_time = 0;
    uint32 pre_time = 0;

    while (p_rua->rtp_tx)
    {
        if (p_rua->rtp_pause)
        {
            pre_time = 0;
            usleep(10*1000);
            continue;
        }
        
        if (hqueue_get(p_rua->media_info.v_queue, (char *)&packet))
        {
            if (packet.data == NULL || packet.size == 0)
            {
                break;
            }

#ifdef RTSP_REPLAY
            if (p_rua->replay)
            {
                rtsp_update_replay_ntp_time(p_rua, AV_VIDEO_CH);
            }
#endif

            if (VIDEO_CODEC_H264 == p_rua->media_info.v_info.codec)
            {
                sret = rtp_h264_video_tx(p_rua, packet.data, packet.size, get_rtp_timestamp(90000));
            }
            else if (VIDEO_CODEC_H265 == p_rua->media_info.v_info.codec)
            {
                sret = rtp_h265_video_tx(p_rua, packet.data, packet.size, get_rtp_timestamp(90000));
            }
            else if (VIDEO_CODEC_MP4 == p_rua->media_info.v_info.codec)
            {
                sret = rtp_video_tx(p_rua, packet.data, packet.size, get_rtp_timestamp(90000));
            }
            else if (VIDEO_CODEC_JPEG == p_rua->media_info.v_info.codec)
            {
                sret = rtp_jpeg_video_tx(p_rua, packet.data, packet.size, get_rtp_timestamp(90000));
            }

            free(packet.buff);

            if (sret < 0)
            {
                rtsp_stop_rua(p_rua);
                break;
            }

            if (packet.waitnext)
            {
                cur_time = sys_os_get_ms();
                cur_delay = timeout;

                if (p_rua->scale_flag)
                {
                    cur_delay /= p_rua->scale / 100.0f;
                }

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
        }
        else
        {
            // should be not to here
            log_print(HT_LOG_ERR, "%s, hqueue_get failed\r\n", __FUNCTION__);
            break;
        }
    }

    p_rua->media_info.video_tx = 0;
    
    rtsp_media_clear_queue(p_rua->media_info.v_queue);

    log_print(HT_LOG_DBG, "%s, exit, p_rua = %p\r\n", __FUNCTION__, p_rua);
    
    return NULL;
}

void * rtsp_media_audio_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *)argv;
    UA_PACKET packet;
    int sret = -1;
    int samplerate = p_rua->media_info.a_info.samplerate;
    int64 cur_delay = 0;
    int64 pre_delay = 0;
    uint32 cur_time = 0;
    uint32 pre_time = 0;

    while (p_rua->rtp_tx)
    {
        if (p_rua->rtp_pause)
        {
            pre_time = 0;
            usleep(10*1000);
            continue;
        }
        
        if (hqueue_get(p_rua->media_info.a_queue, (char *)&packet))
        {
            if (packet.data == NULL || packet.size == 0)
            {
                break;
            }

#ifdef RTSP_REPLAY
            if (p_rua->replay)
            {
                rtsp_update_replay_ntp_time(p_rua, AV_AUDIO_CH);
            }
#endif

            if (packet.nbsamples > 0)
            {
                p_rua->audio_ts += packet.nbsamples;
            }
            else
            {
                p_rua->audio_ts = get_rtp_timestamp(samplerate);
            }
            
            if (AUDIO_CODEC_AAC == p_rua->media_info.a_info.codec)
            {
                sret = rtp_aac_audio_tx(p_rua, packet.data, packet.size, p_rua->audio_ts);
            }
            else
            {
                sret = rtp_audio_tx(p_rua, packet.data, packet.size, p_rua->audio_ts);
            }

            free(packet.buff);

            if (sret < 0)
            {
                rtsp_stop_rua(p_rua);
                break;
            }

            if (packet.waitnext)
            {
                cur_time = sys_os_get_ms();
                cur_delay = 1000000.0 / samplerate * packet.nbsamples;

                if (p_rua->scale_flag)
                {
                    cur_delay /= p_rua->scale / 100.0f;
                }
                
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
        }
        else
        {
            // should be not to here
            log_print(HT_LOG_ERR, "%s, hqueue_get failed\r\n", __FUNCTION__);
            break;
        }
    }

    p_rua->media_info.audio_tx = 0;
    
    rtsp_media_clear_queue(p_rua->media_info.a_queue);

    log_print(HT_LOG_DBG, "%s, exit, p_rua = %p\r\n", __FUNCTION__, p_rua);
    
    return NULL;
}

void rtsp_media_put_video(RSSUA * p_rua, uint8 *data, int size, int waitnext = 1)
{
    UA_PACKET packet;

    if (!p_rua->media_info.video_tx)
    {
        return;
    }

    if (NULL == data || 0 == size)
    {
        memset(&packet, 0, sizeof(packet));

        hqueue_put(p_rua->media_info.v_queue, (char *)&packet);
    }
    else if (p_rua->rtp_tx)
    {
        packet.buff = (uint8*)malloc(size + RTSP_RESV_HDR_SIZE);
        if (packet.buff)
        {
            packet.data = packet.buff + RTSP_RESV_HDR_SIZE;
            memcpy(packet.data, data, size);
            packet.size = size;
            packet.waitnext = waitnext;

            if (hqueue_put(p_rua->media_info.v_queue, (char *)&packet) == FALSE)
            {
                free(packet.buff);
            }
        }
    }
}

void rtsp_media_put_audio(RSSUA * p_rua, uint8 *data, int size, int nbsamples, int waitnext = 1)
{
    UA_PACKET packet;

    if (!p_rua->media_info.audio_tx)
    {
        return;
    }
    
    if (NULL == data || 0 == size)
    {
        memset(&packet, 0, sizeof(packet));

        hqueue_put(p_rua->media_info.a_queue, (char *)&packet);
    }
    else if (p_rua->rtp_tx)
    {
        packet.buff = (uint8*)malloc(size + RTSP_RESV_HDR_SIZE);
        if (packet.buff)
        {
            packet.data = packet.buff + RTSP_RESV_HDR_SIZE;
            memcpy(packet.data, data, size);
            packet.size = size;
            packet.nbsamples = nbsamples;
            packet.waitnext = waitnext;

            if (hqueue_put(p_rua->media_info.a_queue, (char *)&packet) == FALSE)
            {
                free(packet.buff);
            }
        }
    }
}

void rtsp_media_create_video_queue(RSSUA * p_rua, uint32 size, uint32 flag)
{
    if (p_rua->media_info.v_queue)
    {
        return;
    }
    
    if (p_rua->media_info.has_video && p_rua->channels[AV_VIDEO_CH].setup)
    {
        p_rua->media_info.v_queue = hqueue_create(size, sizeof(UA_PACKET), flag);

        p_rua->media_info.video_tx = 1;
        p_rua->media_info.v_thread = sys_os_create_thread((void *)rtsp_media_video_thread, (void*)p_rua);
    }
}

void rtsp_media_create_audio_queue(RSSUA * p_rua, uint32 size, uint32 flag)
{
    if (p_rua->media_info.a_queue)
    {
        return;
    }
    
    if (p_rua->media_info.has_audio && p_rua->channels[AV_AUDIO_CH].setup)
    {
        p_rua->media_info.a_queue = hqueue_create(size, sizeof(UA_PACKET), flag);

        p_rua->media_info.audio_tx = 1;
        p_rua->media_info.a_thread = sys_os_create_thread((void *)rtsp_media_audio_thread, (void*)p_rua);
    }
}

void rtsp_media_create_queue(RSSUA * p_rua, uint32 size, uint32 flag)
{
    rtsp_media_create_video_queue(p_rua, size, flag);

    rtsp_media_create_audio_queue(p_rua, size, flag);

#ifdef RTSP_METADATA
    rtsp_media_create_metadata_queue(p_rua, size, flag);
#endif
}

void rtsp_media_free_video_queue(RSSUA * p_rua)
{
    if (p_rua->media_info.v_queue)
    {
        rtsp_media_put_video(p_rua, NULL, 0, 0);
    }

    sys_os_wait_thread(&p_rua->media_info.v_thread);

    rtsp_media_clear_queue(p_rua->media_info.v_queue);
    
    hqueue_delete(p_rua->media_info.v_queue);
    p_rua->media_info.v_queue = NULL;
}

void rtsp_media_free_audio_queue(RSSUA * p_rua)
{
    if (p_rua->media_info.a_queue)
    {
        rtsp_media_put_audio(p_rua, NULL, 0, 0, 0);
    }

    sys_os_wait_thread(&p_rua->media_info.a_thread);

    rtsp_media_clear_queue(p_rua->media_info.a_queue);
    
    hqueue_delete(p_rua->media_info.a_queue);
    p_rua->media_info.a_queue = NULL;
}

void rtsp_media_free_queue(RSSUA * p_rua)
{
    rtsp_media_free_video_queue(p_rua);

    rtsp_media_free_audio_queue(p_rua);

#ifdef RTSP_METADATA
    rtsp_media_free_metadata_queue(p_rua);
#endif
}

BOOL rtsp_media_video_param_check(RSSUA * p_rua, int codec, double framerate, int width, int height)
{
    if (VIDEO_CODEC_NONE == p_rua->media_info.v_info.codec)
    {
        p_rua->media_info.v_info.codec = codec;
    }

    if (VIDEO_CODEC_NONE == p_rua->media_info.v_info.codec)
    {
        p_rua->media_info.v_info.codec = VIDEO_CODEC_H264;
    }

    if (0 == p_rua->media_info.v_info.framerate)
    {
        p_rua->media_info.v_info.framerate = framerate;
    }

    if (0 == p_rua->media_info.v_info.framerate)
    {
        p_rua->media_info.v_info.framerate = 25;
    }

    if (0 == p_rua->media_info.v_info.width || 0 == p_rua->media_info.v_info.height)
    {
        p_rua->media_info.v_info.width = width;
        p_rua->media_info.v_info.height = height;
    }
    
    return TRUE;
}

BOOL rtsp_media_audio_param_check(RSSUA * p_rua, int codec, int samplerate, int channels)
{
    if (AUDIO_CODEC_NONE == p_rua->media_info.a_info.codec)
    {
        p_rua->media_info.a_info.codec = codec;
    }

    if (AUDIO_CODEC_NONE == p_rua->media_info.a_info.codec)
    {
        p_rua->media_info.a_info.codec = AUDIO_CODEC_G711U;
    }

    if (0 == p_rua->media_info.a_info.samplerate)
    {
        p_rua->media_info.a_info.samplerate = samplerate;
    }

    if (0 == p_rua->media_info.a_info.channels)
    {
        if (AUDIO_CODEC_G711U == p_rua->media_info.a_info.codec ||
            AUDIO_CODEC_G711A == p_rua->media_info.a_info.codec)
        {    
            p_rua->media_info.a_info.channels = 1;
        }
        else
        {
            p_rua->media_info.a_info.channels = channels;
        }
    }

    if (p_rua->media_info.a_info.channels > 2)
    {
        p_rua->media_info.a_info.channels = 2;
    }

    rtsp_media_fix_audio_param(p_rua);

    return TRUE;
}

#ifdef MEDIA_FILE

BOOL rtsp_media_file_init(RSSUA * p_rua, const char * p_url)
{
    p_rua->media_info.file_demuxer = new CFileDemux(p_url, g_rtsp_cfg.loop_nums);
    if (NULL == p_rua->media_info.file_demuxer)
    {
        return FALSE;
    }

    p_rua->media_info.is_file = 1;
    p_rua->media_info.has_video = p_rua->media_info.file_demuxer->hasVideo();
    p_rua->media_info.has_audio = p_rua->media_info.file_demuxer->hasAudio();
    p_rua->media_info.duration = p_rua->media_info.file_demuxer->getDuration();
    
    if (p_rua->media_info.has_video)
    {
        // get video output configuration
        rtsp_cfg_get_video_info(p_rua->media_info.filename, &p_rua->media_info.v_info);

        rtsp_parse_url_video_parameters(p_rua);

        rtsp_media_video_param_check(p_rua, 
            to_video_codec(p_rua->media_info.file_demuxer->getVideoCodec()),
            p_rua->media_info.file_demuxer->getFramerate(), 
            p_rua->media_info.file_demuxer->getWidth(),
            p_rua->media_info.file_demuxer->getHeight());

        if (p_rua->media_info.file_demuxer->setVideoFormat(p_rua->media_info.v_info.codec,
                                            p_rua->media_info.v_info.width,
                                            p_rua->media_info.v_info.height,
                                            p_rua->media_info.v_info.framerate,
                                            p_rua->media_info.v_info.bitrate) == FALSE)
        {
            p_rua->media_info.has_video = 0;
            log_print(HT_LOG_ERR, "%s, setVideoFormat failed\r\n", __FUNCTION__);
        }
    }

    if (p_rua->media_info.has_audio)
    {
        // get audio output configuration
        rtsp_cfg_get_audio_info(p_rua->media_info.filename, &p_rua->media_info.a_info);

        rtsp_parse_url_audio_parameters(p_rua);

        rtsp_media_audio_param_check(p_rua,
            to_audio_codec(p_rua->media_info.file_demuxer->getAudioCodec()),
            p_rua->media_info.file_demuxer->getSamplerate(), 
            p_rua->media_info.file_demuxer->getChannels());

        if (p_rua->media_info.file_demuxer->setAudioFormat(p_rua->media_info.a_info.codec,
                                           p_rua->media_info.a_info.samplerate,
                                           p_rua->media_info.a_info.channels,
                                           p_rua->media_info.a_info.bitrate) == FALSE)
        {
            p_rua->media_info.has_audio = 0;
            log_print(HT_LOG_ERR, "%s, setAudioFormat failed\r\n", __FUNCTION__);
        }
    }

    // parse the url parameters
    rtsp_parse_url_transfer_parameters(p_rua);

    if (!p_rua->media_info.has_video && !p_rua->media_info.has_audio)
    {
        delete p_rua->media_info.file_demuxer;
        p_rua->media_info.file_demuxer = NULL;
    }
    
    return (p_rua->media_info.has_video || p_rua->media_info.has_audio);
}

void rtsp_file_demux_callback(uint8 * data, int size, int type, int nbsamples, BOOL waitnext, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *) pUserdata;

    if (type == DATA_TYPE_VIDEO && p_rua->channels[AV_VIDEO_CH].setup)
    {
        rtsp_media_put_video(p_rua, data, size, waitnext);
    }
    else if (type == DATA_TYPE_AUDIO && p_rua->channels[AV_AUDIO_CH].setup)
    {
        rtsp_media_put_audio(p_rua, data, size, nbsamples, waitnext);
    }
}

void rtsp_media_file_send(RSSUA * p_rua)
{
    BOOL readframe = FALSE;
    CFileDemux * pDemux = p_rua->media_info.file_demuxer;
    if (NULL == pDemux)
    {
        log_print(HT_LOG_ERR, "%s, pDemux object is null\r\n", __FUNCTION__);
        return;
    }

    pDemux->setCallback(rtsp_file_demux_callback, p_rua);

    rtsp_media_create_queue(p_rua, 30, HQ_GET_WAIT | HQ_PUT_WAIT);

    if (p_rua->channels[AV_VIDEO_CH].setup || p_rua->channels[AV_AUDIO_CH].setup)
    {
        readframe = TRUE;
    }
    
    while (p_rua->rtp_tx)
    {
        if (p_rua->rtp_pause)
        {
            usleep(10*1000);
            continue;
        }

        if (readframe)
        {
            if (p_rua->media_info.seek_flag)
            {
                if (pDemux->seekStream(p_rua->play_range_begin))
                {
                    p_rua->media_info.curpos = pDemux->getCurPos();
                }

                p_rua->media_info.seek_flag = 0;
            }
        
            if (pDemux->readFrame() == FALSE)
            {
                break;
            }
        }
        else
        {
            usleep(200*1000);
        }
    }

    pDemux->setCallback(NULL, NULL);
    
    rtsp_media_free_queue(p_rua);
}

#endif // MEDIA_FILE

#ifdef MEDIA_PROXY

BOOL rtsp_media_proxy_init(RSSUA * p_rua, MEDIA_PRY * p_proxy)
{
    CMediaProxy * proxy = CMediaProxy::getInstance(p_proxy->cfg.key, &p_proxy->cfg);
    if (NULL == proxy)
    {
        return FALSE;
    }

    int count = 0;
    
    while (!proxy->m_bInited
#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
        || (proxy->m_info.has_video && 0 == proxy->m_nVideoRecodec)
        || (proxy->m_info.has_audio && 0 == proxy->m_nAudioRecodec)
#endif
        )
    {
        usleep(1000*1000);

        if (++count >= 5)
        {
            break;
        }
    }

    if (!proxy->m_bInited)
    {
        proxy->freeInstance(p_proxy->cfg.key);
        
        log_print(HT_LOG_WARN, "%s, proxy uninit, key = %s, url = %s\r\n", 
            __FUNCTION__, p_proxy->cfg.key, p_proxy->cfg.url);
        return FALSE;
    }

#ifdef RTMP_PROXY
    if (proxy->m_rtmp && p_proxy->cfg.on_demand && 
        (!proxy->m_info.has_video || !proxy->m_info.has_audio))
    {
        // Rtmp audio and video READY separately notice, here it need to wait a moment
        sleep(2);
    }
#endif
#ifdef SRT_PROXY
    if (proxy->m_srt && p_proxy->cfg.on_demand && 
        (!proxy->m_info.has_video || !proxy->m_info.has_audio))
    {
        // SRT audio and video READY separately notice, here it need to wait a moment
        sleep(2);
    }
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    int vrecodec = proxy->getVideoRecodec();
    int arecodec = proxy->getAudioRecodec();
#endif

    p_rua->media_info.is_proxy = 1;
    p_rua->media_info.proxy = proxy;

    if (proxy->m_info.has_video)
    {
        p_rua->media_info.has_video = proxy->m_info.has_video;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
        if (1 == vrecodec && proxy->m_pConfig)
        {
            memcpy(&p_rua->media_info.v_info, &proxy->m_pConfig->output.video, sizeof(VIDEO_INFO));
        }
        else
#endif
        {
            memcpy(&p_rua->media_info.v_info, &proxy->m_info.video, sizeof(VIDEO_INFO));
        }
    }

    if (proxy->m_info.has_audio)
    {
        p_rua->media_info.has_audio = proxy->m_info.has_audio;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
        if (1 == arecodec && proxy->m_pConfig)
        {
            memcpy(&p_rua->media_info.a_info, &proxy->m_pConfig->output.audio, sizeof(AUDIO_INFO));
        }
        else
#endif
        {
            memcpy(&p_rua->media_info.a_info, &proxy->m_info.audio, sizeof(AUDIO_INFO));
        }
    }
    
    return TRUE;
}

void rtsp_media_proxy_callback(uint8 * data, int size, int type, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *) pUserdata;

    if (type == DATA_TYPE_VIDEO && p_rua->channels[AV_VIDEO_CH].setup)
    {
        rtsp_media_put_video(p_rua, data, size, 0);
    }
    else if (type == DATA_TYPE_AUDIO && p_rua->channels[AV_AUDIO_CH].setup)
    {
        rtsp_media_put_audio(p_rua, data, size, 0, 0);
    }
#ifdef RTSP_METADATA    
    else if (type == DATA_TYPE_METADATA && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_put_metadata(p_rua, data, size, 0);
    }
#endif
}

void rtsp_media_proxy_send(RSSUA * p_rua)
{
    CMediaProxy * p_proxy = p_rua->media_info.proxy;
    if (NULL == p_proxy)
    {
        log_print(HT_LOG_ERR, "%s, proxy object is null\r\n", __FUNCTION__);
        return;
    }

    rtsp_media_create_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    p_proxy->addCallback(rtsp_media_proxy_callback, p_rua);

    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    p_proxy->delCallback(rtsp_media_proxy_callback, p_rua);

    rtsp_media_free_queue(p_rua);
}

#endif // MEDIA_PROXY

#ifdef MEDIA_PUSHER

BOOL rtsp_media_pusher_init(RSSUA * p_rua, MEDIA_PUSH * p_pusher)
{
    CMediaPusher * pusher = CMediaPusher::getInstance(p_pusher->cfg.key, &p_pusher->cfg);
    
    if (NULL == pusher || pusher->isInited() == FALSE)
    {    
        log_print(HT_LOG_WARN, "%s, pusher uninit, key = %s\r\n", __FUNCTION__, p_pusher->cfg.key);
        return FALSE;
    }

    MEDIA_INFO * p_input = &pusher->getConfig()->input;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    MEDIA_INFO * p_output = &pusher->getConfig()->output;
    
    int vrecodec = pusher->getVideoRecodec();
    int arecodec = pusher->getAudioRecodec();
#endif

    p_rua->media_info.is_pusher = 1;
    p_rua->media_info.pusher = pusher;

    if (p_input->has_video)
    {
        p_rua->media_info.has_video = p_input->has_video;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
        if (1 == vrecodec)
        {
            memcpy(&p_rua->media_info.v_info, &p_output->video, sizeof(VIDEO_INFO));
        }
        else
#endif
        {
            memcpy(&p_rua->media_info.v_info, &p_input->video, sizeof(VIDEO_INFO));
        }
    }

    if (p_input->has_audio)
    {
        p_rua->media_info.has_audio = p_input->has_audio;

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
        if (1 == arecodec)
        {
            memcpy(&p_rua->media_info.a_info, &p_output->audio, sizeof(AUDIO_INFO));
        }
        else
#endif
        {
            memcpy(&p_rua->media_info.a_info, &p_input->audio, sizeof(AUDIO_INFO));
        }
    }
    
    return TRUE;
}

void rtsp_media_pusher_callback(uint8 * data, int size, int type, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *) pUserdata;

    if (type == DATA_TYPE_VIDEO && p_rua->channels[AV_VIDEO_CH].setup)
    {
        rtsp_media_put_video(p_rua, data, size, 0);
    }
    else if (type == DATA_TYPE_AUDIO && p_rua->channels[AV_AUDIO_CH].setup)
    {
        rtsp_media_put_audio(p_rua, data, size, 0, 0);
    }
#ifdef RTSP_METADATA    
    else if (type == DATA_TYPE_METADATA && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_put_metadata(p_rua, data, size, 0);
    }
#endif    
}

void rtsp_media_pusher_send(RSSUA * p_rua)
{
    CMediaPusher * p_pusher = p_rua->media_info.pusher;
    if (NULL == p_pusher)
    {
        log_print(HT_LOG_ERR, "%s, pusher object is null\r\n", __FUNCTION__);
        return;
    }

    rtsp_media_create_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    p_pusher->addCallback(rtsp_media_pusher_callback, p_rua);

    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    p_pusher->delCallback(rtsp_media_pusher_callback, p_rua);
    
    rtsp_media_free_queue(p_rua);
}

#endif // MEDIA_PUSHER

#ifdef MEDIA_DEVICE

int rtsp_get_device_nums(int type)
{
    int nums = 0;
    
    if (0 == type)  // screen
    {
#if __WINDOWS_OS__
        nums = CWScreenCapture::getDeviceNums();    
#elif defined(ANDROID)
#elif defined(IOS)
        nums = CMScreenCapture::getDeviceNums();
#elif __LINUX_OS__
        nums = CLScreenCapture::getDeviceNums();
#endif    
    }
    else if (1 == type) // video
    {
#if __WINDOWS_OS__
        nums = CWVideoCapture::getDeviceNums();    
#elif defined(ANDROID)
        nums = CQVideoCapture::getDeviceNums();
#elif defined(IOS)
        nums = CMVideoCapture::getDeviceNums();
#elif __LINUX_OS__
        nums = CLVideoCapture::getDeviceNums();
#endif    
    }
    else if (2 == type) // audio
    {
#if __WINDOWS_OS__
        nums = CWAudioCapture::getDeviceNums();    
#elif defined(ANDROID)
        nums = CAAudioCapture::getDeviceNums();
#elif defined(IOS)
        nums = CMAudioCapture::getDeviceNums();
#elif __LINUX_OS__
        nums = CLAudioCapture::getDeviceNums();
#endif    
    }

    return nums;
}

int rtsp_get_video_device_index(char * name)
{
    int index = 0;
    
#if __WINDOWS_OS__
    index = CWVideoCapture::getDeviceIndex(name);
#elif defined(ANDROID)
    index = CQVideoCapture::getDeviceIndex(name);
#elif defined(IOS)
    index = CMVideoCapture::getDeviceIndex(name);
#elif __LINUX_OS__
    index = CLVideoCapture::getDeviceIndex(name);
#endif

    return index;
}

int rtsp_get_audio_device_index(char * name)
{
    int index = 0;
    
#if __WINDOWS_OS__
    index = CWAudioCapture::getDeviceIndex(name);
#elif defined(ANDROID)
    index = CAAudioCapture::getDeviceIndex(name);
#elif defined(IOS)
    index = CMAudioCapture::getDeviceIndex(name);
#elif __LINUX_OS__
    index = CLAudioCapture::getDeviceIndex(name);
#endif

    return index;    
}

int rtsp_parse_device_index(char * buff, int flag)
{
    int i = 0;
    int index = 0;
    char name[256];
    char * p = buff;
        
    if (*p != '\0')
    {
        if (*p == '=')
        {
            p++;
            
            while (*p != '\0')
            {
                if (*p != '"' && *p != '\'')
                {
                    name[i++] = *p;
                }

                p++;
            }

            name[i] = '\0';

            if (i > 0)
            {
                url_decode(name, name, strlen(name));
                
                if (0 == flag)  // video
                {
                    index = rtsp_get_video_device_index(name);
                }
                else if (1 == flag) // audio
                {
                    index = rtsp_get_audio_device_index(name);
                }
            }
        }
        else
        {
            index = atoi(p);
        }
    }    

    return index;
}

BOOL rtsp_parse_window_title(char * buff, char * title, int title_size)
{
    int i = 0;
    BOOL ret = FALSE;
    char name[256];
    char * p = buff;
        
    if (*p == '=')
    {
        p++;
        
        while (*p != '\0')
        {
            if (*p != '"' && *p != '\'')
            {
                name[i++] = *p;
            }

            p++;
        }

        name[i] = '\0';

        if (i > 0)
        {
            url_decode(name, name, strlen(name));
            
            strncpy(title, name, title_size);

            ret = TRUE;
        }
    }

    return ret;
}

BOOL rtsp_parse_device_url(RSSUA * p_rua, const char * p_url)
{
    int i = 0;
    char buff[512];
    char * p;
    
    while (p_url[i] != '\0')
    {
        if (p_url[i] == '+')
        {
            break;
        }
        else
        {
            buff[i] = p_url[i];
        }

        i++;
    }

    buff[i] = '\0';

    int screen = 0;
    int window = 0;
    int vdevice = 0;
    int adevice = 0;
    int vindex = 0;
    int aindex = 0;
    char title[256] = {'\0'};
    
    if (strncasecmp(buff, "videodevice", strlen("videodevice")) == 0)
    {
        vdevice = 1;
        vindex = rtsp_parse_device_index(buff + strlen("videodevice"), 0);
    }
    else if (strncasecmp(buff, "audiodevice", strlen("audiodevice")) == 0)
    {
        adevice = 1;
        aindex = rtsp_parse_device_index(buff + strlen("audiodevice"), 1);
    }
    else if (strncasecmp(buff, "screenlive", strlen("screenlive")) == 0)
    {
        screen = 1;
        p = buff + strlen("screenlive");
        if (*p != '\0')
        {
            vindex = atoi(p);
        }
    }
    else if (strncasecmp(buff, "window", strlen("window")) == 0)
    {
        window = 1;
        if (!rtsp_parse_window_title(buff + strlen("window"), title, sizeof(title)))
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

    if (p_url[i] == '+')
    {
        strcpy(buff, p_url+i+1);

        if (strncasecmp(buff, "videodevice", strlen("videodevice")) == 0)
        {
            vdevice = 1;
            vindex = rtsp_parse_device_index(buff + strlen("videodevice"), 0);
        }
        else if (strncasecmp(buff, "audiodevice", strlen("audiodevice")) == 0)
        {
            adevice = 1;
            aindex = rtsp_parse_device_index(buff + strlen("audiodevice"), 1);
        }
        else if (strncasecmp(buff, "screenlive", strlen("screenlive")) == 0)
        {
            screen = 1;
            p = buff + strlen("screenlive");
            if (*p != '\0')
            {
                vindex = atoi(p);
            }
        }
        else if (strncasecmp(buff, "window", strlen("window")) == 0)
        {
            window = 1;
            if (!rtsp_parse_window_title(buff + strlen("window"), title, sizeof(title)))
            {
                return FALSE;
            }
        }
        else
        {
            return FALSE;
        }
    }

    p_rua->media_info.is_device = 1;
    
    if (adevice)
    {
        p_rua->media_info.has_audio = 1;
        p_rua->media_info.a_index = aindex;
    }

    if (vdevice)
    {
        p_rua->media_info.has_video = 1;
        p_rua->media_info.v_index = vindex;
    }
    else if (screen)
    {
        p_rua->media_info.has_video = 1;
        p_rua->media_info.is_screen = 1;
        p_rua->media_info.v_index = vindex;
    }
    else if (window)
    {
        p_rua->media_info.has_video = 1;
        p_rua->media_info.is_window = 1;
        strcpy(p_rua->media_info.window_title, title);
    }

    return TRUE;
}

BOOL rtsp_media_device_screen_init(RSSUA * p_rua)
{
    if (p_rua->media_info.v_index >= rtsp_get_device_nums(0))
    {
        log_print(HT_LOG_ERR, "%s, index=%d, device nums=%d\r\n", 
            __FUNCTION__, p_rua->media_info.v_index, rtsp_get_device_nums(0));
        return FALSE;
    }

    // get video output configuration
    rtsp_cfg_get_video_info(p_rua->media_info.filename, &p_rua->media_info.v_info);

    rtsp_parse_url_video_parameters(p_rua);

    rtsp_media_video_param_check(p_rua, VIDEO_CODEC_H264, 25, 0, 0);

    // get screen capture instance
#if __WINDOWS_OS__
    p_rua->media_info.screen_capture = CWScreenCapture::getInstance(p_rua->media_info.v_index);
#elif defined(ANDROID)
    // android does not yet support
#elif defined(IOS)
    p_rua->media_info.screen_capture = CMScreenCapture::getInstance(p_rua->media_info.v_index);
#elif __LINUX_OS__
    p_rua->media_info.screen_capture = CLScreenCapture::getInstance(p_rua->media_info.v_index);
#endif

    if (NULL == p_rua->media_info.screen_capture)
    {
        log_print(HT_LOG_ERR, "%s, get screen capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.screen_capture->initCapture(p_rua->media_info.v_info.codec,
                                            p_rua->media_info.v_info.width,
                                            p_rua->media_info.v_info.height,
                                            p_rua->media_info.v_info.framerate,
                                            p_rua->media_info.v_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init screen capture failed\r\n", __FUNCTION__);
        p_rua->media_info.screen_capture->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.screen_capture = NULL;
        return FALSE;
    }

    // get video size
    if (p_rua->media_info.v_info.width == 0 || p_rua->media_info.v_info.height == 0)
    {
        p_rua->media_info.v_info.width = p_rua->media_info.screen_capture->getWidth();
        p_rua->media_info.v_info.height = p_rua->media_info.screen_capture->getHeight();
    }

    return TRUE;
}

BOOL rtsp_media_device_window_init(RSSUA * p_rua)
{
    // get video output configuration
    rtsp_cfg_get_video_info(p_rua->media_info.filename, &p_rua->media_info.v_info);

    rtsp_parse_url_video_parameters(p_rua);

    rtsp_media_video_param_check(p_rua, VIDEO_CODEC_H264, 25, 0, 0);

#if __WINDOWS_OS__
    p_rua->media_info.window_capture = CWWindowCapture::getInstance(p_rua->media_info.window_title);
#elif defined(ANDROID)
    // android does not yet support
#elif defined(IOS)
    p_rua->media_info.window_capture = CMWindowCapture::getInstance(p_rua->media_info.window_title);
#elif __LINUX_OS__
    p_rua->media_info.window_capture = CLWindowCapture::getInstance(p_rua->media_info.window_title);
#endif

    if (NULL == p_rua->media_info.window_capture)
    {
        log_print(HT_LOG_ERR, "%s, get window capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.window_capture->initCapture(p_rua->media_info.v_info.codec,
                                            p_rua->media_info.v_info.width,
                                            p_rua->media_info.v_info.height,
                                            p_rua->media_info.v_info.framerate,
                                            p_rua->media_info.v_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init window capture failed\r\n", __FUNCTION__);
        p_rua->media_info.window_capture->freeInstance(p_rua->media_info.window_title);
        p_rua->media_info.window_capture = NULL;
        return FALSE;
    }

    // get video size
    if (p_rua->media_info.v_info.width == 0 || p_rua->media_info.v_info.height == 0)
    {
        p_rua->media_info.v_info.width = p_rua->media_info.window_capture->getWidth();
        p_rua->media_info.v_info.height = p_rua->media_info.window_capture->getHeight();
    }

    return TRUE;
}

BOOL rtsp_media_device_video_init(RSSUA * p_rua)
{
    if (p_rua->media_info.v_index >= rtsp_get_device_nums(1))
    {
        log_print(HT_LOG_ERR, "%s, index=%d, device nums=%d\r\n", 
            __FUNCTION__, p_rua->media_info.v_index, rtsp_get_device_nums(1));
        return FALSE;
    }

    // get video output configuration
    rtsp_cfg_get_video_info(p_rua->media_info.filename, &p_rua->media_info.v_info);

    rtsp_parse_url_video_parameters(p_rua);

    rtsp_media_video_param_check(p_rua, VIDEO_CODEC_H264, 25, 0, 0);

    // get video capture instance
#if __WINDOWS_OS__
    p_rua->media_info.video_capture = CWVideoCapture::getInstance(p_rua->media_info.v_index);
#elif defined(ANDROID)
    p_rua->media_info.video_capture = CQVideoCapture::getInstance(p_rua->media_info.v_index);
#elif defined(IOS)
    p_rua->media_info.video_capture = CMVideoCapture::getInstance(p_rua->media_info.v_index);
#elif __LINUX_OS__
    p_rua->media_info.video_capture = CLVideoCapture::getInstance(p_rua->media_info.v_index);
#endif

    if (NULL == p_rua->media_info.video_capture)
    {
        log_print(HT_LOG_ERR, "%s, get video capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.video_capture->initCapture(p_rua->media_info.v_info.codec,
                                            p_rua->media_info.v_info.width,
                                            p_rua->media_info.v_info.height,
                                            p_rua->media_info.v_info.framerate,
                                            p_rua->media_info.v_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init video capture failed\r\n", __FUNCTION__);
        p_rua->media_info.video_capture->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.video_capture = NULL;
        return FALSE;
    }

    // get video sizes
    if (p_rua->media_info.v_info.width == 0 || p_rua->media_info.v_info.height == 0)
    {
        p_rua->media_info.v_info.width = p_rua->media_info.video_capture->getWidth();
        p_rua->media_info.v_info.height = p_rua->media_info.video_capture->getHeight();
    }

    return TRUE;
}

BOOL rtsp_media_device_audio_init(RSSUA * p_rua)
{
    if (p_rua->media_info.a_index >= rtsp_get_device_nums(2))
    {
        log_print(HT_LOG_ERR, "%s, index=%d, device nums=%d\r\n", 
            __FUNCTION__, p_rua->media_info.a_index, rtsp_get_device_nums(2));
        return FALSE;
    }

    // get audio output configuration
    rtsp_cfg_get_audio_info(p_rua->media_info.filename, &p_rua->media_info.a_info);

    rtsp_parse_url_audio_parameters(p_rua);

    rtsp_media_audio_param_check(p_rua, AUDIO_CODEC_G711U, 8000, 1);

    // get audio capture instance
#if __WINDOWS_OS__
    p_rua->media_info.audio_capture = CWAudioCapture::getInstance(p_rua->media_info.a_index);
#elif defined(ANDROID)
    p_rua->media_info.audio_capture = CAAudioCapture::getInstance(p_rua->media_info.a_index);
#elif defined(IOS)
    p_rua->media_info.audio_capture = CMAudioCapture::getInstance(p_rua->media_info.a_index);
#elif __LINUX_OS__
    p_rua->media_info.audio_capture = CLAudioCapture::getInstance(p_rua->media_info.a_index);
#endif

    if (NULL == p_rua->media_info.audio_capture)
    {
        log_print(HT_LOG_ERR, "%s, get audio capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.audio_capture->initCapture(p_rua->media_info.a_info.codec,
                                            p_rua->media_info.a_info.samplerate,
                                            p_rua->media_info.a_info.channels,
                                            p_rua->media_info.a_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init audio capture failed\r\n", __FUNCTION__);
        p_rua->media_info.audio_capture->freeInstance(p_rua->media_info.a_index);
        p_rua->media_info.audio_capture = NULL;
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_media_device_init(RSSUA * p_rua)
{
    BOOL vret = FALSE, aret = FALSE;

    rtsp_parse_url_transfer_parameters(p_rua);
    
    if (p_rua->media_info.has_video)
    {
        if (p_rua->media_info.is_screen)
        {
            vret = rtsp_media_device_screen_init(p_rua);
        }
        else if (p_rua->media_info.is_window)
        {
            vret = rtsp_media_device_window_init(p_rua);
        }
        else
        {
            vret = rtsp_media_device_video_init(p_rua);
        }
    }

    if (p_rua->media_info.has_audio)
    {
        aret = rtsp_media_device_audio_init(p_rua);
    }

    return (vret || aret);
}

void rtsp_media_video_callback(uint8 * data, int size, int waitnext, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *)pUserdata;

    if (p_rua->channels[AV_VIDEO_CH].setup)
    {
        rtsp_media_put_video(p_rua, data, size, 0);
    }
}

void rtsp_media_audio_callback(uint8 * data, int size, int nbsamples, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *)pUserdata;
    
    if (p_rua->channels[AV_AUDIO_CH].setup)
    {
        rtsp_media_put_audio(p_rua, data, size, nbsamples, 0);
    }
}

void rtsp_media_device_screen_send(RSSUA * p_rua)
{
    CScreenCapture * capture = p_rua->media_info.screen_capture;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_video_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_video_callback, p_rua);
    capture->startCapture();
    
    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_video_callback, p_rua);

    rtsp_media_free_video_queue(p_rua);
}

void rtsp_media_device_window_send(RSSUA * p_rua)
{
    CWindowCapture * capture = p_rua->media_info.window_capture;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_video_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_video_callback, p_rua);
    capture->startCapture();
    
    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_video_callback, p_rua);

    rtsp_media_free_video_queue(p_rua);
}

void rtsp_media_device_video_send(RSSUA * p_rua)
{
    CVideoCapture * capture = p_rua->media_info.video_capture;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_video_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_video_callback, p_rua);
    capture->startCapture();
    
    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_video_callback, p_rua);

    rtsp_media_free_video_queue(p_rua);
}

void rtsp_media_device_audio_send(RSSUA * p_rua)
{
    CAudioCapture * capture = p_rua->media_info.audio_capture;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_audio_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_audio_callback, p_rua);
    capture->startCapture();

    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_audio_callback, p_rua);

    rtsp_media_free_audio_queue(p_rua);
}

void * rtsp_media_device_audio_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *)argv;

    rtsp_media_device_audio_send(p_rua);

    return NULL;
}

void rtsp_media_device_send(RSSUA * p_rua)
{
#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_create_metadata_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);
    }
#endif

    if (p_rua->media_info.has_video)
    {
        if (p_rua->media_info.has_audio)
        {
            p_rua->tid_audio = sys_os_create_thread((void *)rtsp_media_device_audio_thread, (void *)p_rua);
        }

        if (p_rua->media_info.is_screen)
        {
            rtsp_media_device_screen_send(p_rua);
        }
        else if (p_rua->media_info.is_window)
        {
            rtsp_media_device_window_send(p_rua);
        }
        else
        {
            rtsp_media_device_video_send(p_rua);
        }

        // wait audio send thread exit ...
        sys_os_wait_thread(&p_rua->tid_audio);
    }
    else if (p_rua->media_info.has_audio)
    {
        rtsp_media_device_audio_send(p_rua);
    }

#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_free_metadata_queue(p_rua);
    }
#endif
}

#endif // MEDIA_DEVICE

#ifdef MEDIA_LIVE

BOOL rtsp_parse_live_url(RSSUA * p_rua, const char * p_url)
{
    // todo : parse the URL suffix, set the p_rua->media_info struct

    if (strncasecmp(p_url, "live", strlen("live")) == 0 || 
        strncasecmp(p_url, "live1", strlen("live1")) == 0)
    {
        p_rua->media_info.is_live = 1;

        p_rua->media_info.has_video = 1;
        p_rua->media_info.v_index = 0;
        p_rua->media_info.v_info.framerate = 25;
        p_rua->media_info.v_info.width = 1920;
        p_rua->media_info.v_info.height = 1080;
        p_rua->media_info.v_info.codec = VIDEO_CODEC_H264;

        // todo : If there is no audio, comment out the following statements

        p_rua->media_info.has_audio = 1;
        p_rua->media_info.a_index = 0;
        p_rua->media_info.a_info.samplerate = 8000;
        p_rua->media_info.a_info.channels = 1;
        p_rua->media_info.a_info.codec = AUDIO_CODEC_G711U;
    }
    else if (strncasecmp(p_url, "live2", strlen("live2")) == 0)
    {
        p_rua->media_info.is_live = 1;

        p_rua->media_info.has_video = 1;
        p_rua->media_info.v_index = 1;
        p_rua->media_info.v_info.framerate = 25;
        p_rua->media_info.v_info.width = 1280;
        p_rua->media_info.v_info.height = 720;
        p_rua->media_info.v_info.codec = VIDEO_CODEC_H264;

        // todo : If there is no audio, comment out the following statements

        p_rua->media_info.has_audio = 1;
        p_rua->media_info.a_index = 0;
        p_rua->media_info.a_info.samplerate = 8000;
        p_rua->media_info.a_info.channels = 1;
        p_rua->media_info.a_info.codec = AUDIO_CODEC_G711U;
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_media_live_video_init(RSSUA * p_rua)
{
    if (p_rua->media_info.v_index >= CLiveVideo::getStreamNums())
    {
        log_print(HT_LOG_ERR, "%s, index=%d, stream nums=%d\r\n", __FUNCTION__, 
            p_rua->media_info.v_index, CLiveVideo::getStreamNums());
        return FALSE;
    }

    rtsp_parse_url_video_parameters(p_rua);

    rtsp_media_video_param_check(p_rua, VIDEO_CODEC_H264, 25, 1280, 720);

    // get the live video instance
    p_rua->media_info.live_video = CLiveVideo::getInstance(p_rua->media_info.v_index);

    if (NULL == p_rua->media_info.live_video)
    {
        log_print(HT_LOG_ERR, "%s, get live video capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.live_video->initCapture(p_rua->media_info.v_info.codec,
                                            p_rua->media_info.v_info.width, 
                                            p_rua->media_info.v_info.height, 
                                            p_rua->media_info.v_info.framerate,
                                            p_rua->media_info.v_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init live video capture failed\r\n", __FUNCTION__);
        p_rua->media_info.live_video->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.live_video = NULL;
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_media_live_audio_init(RSSUA * p_rua)
{
    if (p_rua->media_info.a_index >= CLiveAudio::getStreamNums())
    {
        log_print(HT_LOG_ERR, "%s, index=%d, stream nums=%d\r\n", __FUNCTION__, 
            p_rua->media_info.a_index, CLiveAudio::getStreamNums());
        return FALSE;
    }

    rtsp_parse_url_audio_parameters(p_rua);

    rtsp_media_audio_param_check(p_rua, AUDIO_CODEC_G711U, 8000, 1);

    // get audio capture instance
    p_rua->media_info.live_audio = CLiveAudio::getInstance(p_rua->media_info.a_index);

    if (NULL == p_rua->media_info.live_audio)
    {
        log_print(HT_LOG_ERR, "%s, get live audio capture object failed\r\n", __FUNCTION__);
        return FALSE;
    }
    else if (p_rua->media_info.live_audio->initCapture(p_rua->media_info.a_info.codec,
                                            p_rua->media_info.a_info.samplerate, 
                                            p_rua->media_info.a_info.channels,
                                            p_rua->media_info.a_info.bitrate) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, init live audio capture failed\r\n", __FUNCTION__);
        p_rua->media_info.live_audio->freeInstance(p_rua->media_info.a_index);
        p_rua->media_info.live_audio = NULL;
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_media_live_init(RSSUA * p_rua)
{
    BOOL vret = FALSE, aret = FALSE;

    rtsp_parse_url_transfer_parameters(p_rua);
    
    if (p_rua->media_info.has_video)
    {
        vret = rtsp_media_live_video_init(p_rua);
    }

    if (p_rua->media_info.has_audio)
    {
        aret = rtsp_media_live_audio_init(p_rua);
    }

    return (vret || aret);
}

void rtsp_media_live_video_callback(uint8 * data, int size, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *)pUserdata;

    if (p_rua->channels[AV_VIDEO_CH].setup)
    {
        rtsp_media_put_video(p_rua, data, size, 0);
    }
}

void rtsp_media_live_audio_callback(uint8 * data, int size, int nbsamples, void * pUserdata)
{
    RSSUA * p_rua = (RSSUA *)pUserdata;
    
    if (p_rua->channels[AV_AUDIO_CH].setup)
    {
        rtsp_media_put_audio(p_rua, data, size, nbsamples, 0);
    }
}

void rtsp_media_live_video_send(RSSUA * p_rua)
{
    CLiveVideo * capture = p_rua->media_info.live_video;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_video_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_live_video_callback, p_rua);
    capture->startCapture();
    
    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_live_video_callback, p_rua);

    rtsp_media_free_video_queue(p_rua);

    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void rtsp_media_live_audio_send(RSSUA * p_rua)
{
    CLiveAudio * capture = p_rua->media_info.live_audio;
    if (NULL == capture)
    {
        log_print(HT_LOG_ERR, "%s, capture object is null\r\n", __FUNCTION__);
        return;
    }
    
    rtsp_media_create_audio_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);

    capture->addCallback(rtsp_media_live_audio_callback, p_rua);
    capture->startCapture();

    while (p_rua->rtp_tx)
    {
        usleep(200*1000);
    }

    capture->delCallback(rtsp_media_live_audio_callback, p_rua);

    rtsp_media_free_audio_queue(p_rua);

    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void * rtsp_media_live_audio_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *) argv;

    rtsp_media_live_audio_send(p_rua);

    return NULL;
}

void rtsp_media_live_send(RSSUA * p_rua)
{
#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_create_metadata_queue(p_rua, RTSP_QUEUE_SIZE, HQ_GET_WAIT | HQ_PUT_WAIT);
    }
#endif

    if (p_rua->media_info.has_video)
    {
        if (p_rua->media_info.has_audio)
        {
            p_rua->tid_audio = sys_os_create_thread((void *)rtsp_media_live_audio_thread, (void *)p_rua);
        }

        rtsp_media_live_video_send(p_rua);

        // wait audio send thread exit ...
        sys_os_wait_thread(&p_rua->tid_audio);
    }
    else if (p_rua->media_info.has_audio)
    {
        rtsp_media_live_audio_send(p_rua);
    }

#ifdef RTSP_METADATA
    if (p_rua->media_info.has_metadata && p_rua->channels[AV_METADATA_CH].setup)
    {
        rtsp_media_free_metadata_queue(p_rua);
    }
#endif
}

#endif // MEDIA_LIVE

#ifdef RTSP_REPLAY

BOOL rtsp_parse_url_replay_parameters(RSSUA * p_rua)
{
    char value[32] = {'\0'};
    char * p = strchr(p_rua->media_info.filename, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->media_info.filename, '&');
    }
    
    if (NULL == p)
    {
        return FALSE;
    }

    p++; // skip  '?' or '&' char

    if (get_name_value_pair(p, strlen(p), "earliest", value, sizeof(value)))
    {
        p_rua->media_info.earliest = strtoul(value, (char**)NULL, 10);
    }
    else
    {
        return FALSE;
    }

    if (get_name_value_pair(p, strlen(p), "latest", value, sizeof(value)))
    {
        p_rua->media_info.latest = strtoul(value, (char**)NULL, 10);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL rtsp_media_replay_init(RSSUA * p_rua)
{
    if (!rtsp_parse_url_replay_parameters(p_rua))
    {
        return FALSE;
    }

    log_print(HT_LOG_INFO, "%s, st:%lld, et:%lld\r\n", 
        __FUNCTION__, p_rua->media_info.earliest, p_rua->media_info.latest);

    // todo : Find the corresponding recording based on the start time and end time, 
    //        and perform the initialization operation of reading

    // todo : Read the test file here

    p_rua->media_info.is_replay = 1;

#ifdef MEDIA_FILE
    return rtsp_media_file_init(p_rua, "test.mp4");
#else
    return FALSE;
#endif
}

void rtsp_media_replay_send(RSSUA * p_rua)
{
    p_rua->s_replay_time = sys_os_get_ms();

#ifdef MEDIA_FILE
    rtsp_media_file_send(p_rua);
#endif
}

#endif // RTSP_REPLAY

BOOL rtsp_media_init(void * rua)
{
    RSSUA * p_rua = (RSSUA *)rua;
    char path[512];
    char filename[512] = {'\0'};
    char * p_suffix;
    char * p;
    
    if (!rtsp_get_url_path(p_rua->uri, path, sizeof(path)))
    {
        return FALSE;
    }

    if (path[0] == '/')
    {
        p_suffix = path + 1;
    }
    else
    {
        p_suffix = path;
    }

    strncpy(p_rua->media_info.filename, p_suffix, sizeof(p_rua->media_info.filename)-1);

    p = strchr(p_suffix, '?');
    if (NULL == p)
    {
        p = strchr(p_suffix, '&');
    }

    if (p)
    {
        if (p - p_suffix < (int) sizeof(filename))
        {
            strncpy(filename, p_suffix, p - p_suffix);
        }
        else
        {
            strncpy(filename, p_suffix, sizeof(filename)-1);
        }
    }
    else
    {
        strncpy(filename, p_suffix, sizeof(filename)-1);
    }
    
#ifdef MEDIA_PROXY
    MEDIA_PRY * p_proxy = media_proxy_match(filename);
    if (p_proxy)
    {
        return rtsp_media_proxy_init(p_rua, p_proxy);
    }
#endif

#ifdef MEDIA_PUSHER
    MEDIA_PUSH * p_pusher = media_pusher_match(filename);
    if (p_pusher)
    {
        if (p_rua->media_info.is_publish)
        {
            if (p_pusher->is_fixed) // Configured Pusher session
            {
            }
            else // Unconfigured Pusher session
            {
                // Two pushes are not allowed to push to the same address 
                return FALSE;
            }
        }

        return rtsp_media_pusher_init(p_rua, p_pusher);
    }
    else if (p_rua->media_info.is_publish)
    {
        // Unconfigured Pusher session
        
        p_pusher = media_add_pusher(FALSE);
        if (p_pusher)
        {
            p_pusher->cfg.input.has_video = p_rua->media_info.has_video;
            p_pusher->cfg.input.has_audio = p_rua->media_info.has_audio;
            p_pusher->cfg.transfer.mode = TRANSFER_MODE_RTSP;
            strncpy(p_pusher->cfg.key, filename, sizeof(p_pusher->cfg.key)-1);

            return rtsp_media_pusher_init(p_rua, p_pusher);
        }
    }
#endif

#ifdef MEDIA_DEVICE
    if (rtsp_parse_device_url(p_rua, filename))
    {
        return rtsp_media_device_init(p_rua);
    }
#endif

#ifdef MEDIA_LIVE
    if (rtsp_parse_live_url(p_rua, filename))
    {
        return rtsp_media_live_init(p_rua);
    }
#endif

#ifdef RTSP_REPLAY
    if (strcasecmp(filename, "replay") == 0)
    {
        return rtsp_media_replay_init(p_rua);
    }
#endif

#ifdef MEDIA_FILE
    return rtsp_media_file_init(p_rua, filename);
#endif

    return FALSE;
}

void rtsp_media_send_thread(void * rua)
{
    RSSUA * p_rua = (RSSUA *)rua;

#ifdef RTSP_REPLAY
    if (p_rua->media_info.is_replay)
    {
        rtsp_media_replay_send(p_rua);
    }
    else
#endif

#ifdef MEDIA_FILE
    if (p_rua->media_info.is_file)
    {
        rtsp_media_file_send(p_rua);
    }
    else
#endif

#ifdef MEDIA_PROXY
    if (p_rua->media_info.is_proxy)
    {
        rtsp_media_proxy_send(p_rua);
    }
    else
#endif

#ifdef MEDIA_PUSHER
    if (p_rua->media_info.is_pusher)
    {
        rtsp_media_pusher_send(p_rua);
    }
    else 
#endif

#ifdef MEDIA_DEVICE
    if (p_rua->media_info.is_device)
    {
        rtsp_media_device_send(p_rua);
    }
    else
#endif

#ifdef MEDIA_LIVE
    if (p_rua->media_info.is_live)
    {
        rtsp_media_live_send(p_rua);
    }
    else
#endif

    {
    }

    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}






