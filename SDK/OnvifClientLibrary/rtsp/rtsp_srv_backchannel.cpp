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
#include "media_format.h"   // AUDIO_CODEC_* (G711/G726/G722/OPUS/AAC) used in rtsp_bc_data_rx
#include "rtsp_srv_backchannel.h"
#include "rtp.h"
#include "pcm_rtp_rx.h"
#include "aac_rtp_rx.h"
#include "net_util.h"

// nop_rtsp facade hook: forward received talk audio to the registered sink.
#ifdef __cplusplus
extern "C" {
#endif
void nop_rtsp_server_on_bc_audio(const uint8 * data, int size, int nbsamples);
#ifdef __cplusplus
}
#endif

#ifdef RTSP_BACKCHANNEL

#ifdef MEDIA_DEVICE

#ifdef ANDROID
#include "audio_play_qt.h"
#elif defined(IOS)
#include "audio_play_mac.h"
#elif __WINDOWS_OS__
#include "audio_play_win.h"
#elif __LINUX_OS__
#include "audio_play_linux.h"
#endif

void rtsp_bc_decoder_cb(AVFrame * frame, void * puser)
{
    RSSUA * p_rua = (RSSUA *) puser;

    AVFrame * dst = av_frame_clone(frame);
    if (dst)
    {
        while (!hqueue_put(p_rua->audio_queue, (char *) dst))
        {
            usleep(10*1000);
        }
    }
}

void * rtsp_bc_audio_play_thread(void * argv)
{
    RSSUA * p_rua = (RSSUA *)argv;
    
    while (p_rua->audio_play_flag)
    {
        AVFrame frame;

        if (hqueue_get(p_rua->audio_queue, (char *) &frame))
        {
            if (frame.data[0] == NULL)
            {
                av_frame_unref(&frame);
                break;
            }
            
            if (p_rua->audio_player)
            {
                // Will wait for the playback to complete before returning
                p_rua->audio_player->playAudio(frame.data[0], frame.nb_samples * frame.channels * 2);
            }
            else
            {
                usleep(1000000 * frame.nb_samples / p_rua->media_info.bc_info.samplerate);
            }
            
            av_frame_unref(&frame);
        }
        else
        {
            // Fill the silence data
            
            uint8 buff[1024] = {0};
            p_rua->audio_player->playAudio(buff, 1024);
        }
    }
    
    return NULL;
}

#endif

int rtsp_bc_data_cb(uint8 * data, int size, uint32 ts, uint32 seq, void * p_userdata)
{
    RSSUA * p_rua = (RSSUA *)p_userdata;
    
    sys_os_mutex_enter(p_rua->bc_mutex);
    
#ifdef MEDIA_DEVICE
    if (p_rua->ad_inited && p_rua->audio_decoder)
    {
        p_rua->audio_decoder->decode(data, size);
    }
#else
    // Route decoded talk audio to the nop_rtsp facade sink (nbsamples unknown here).
    nop_rtsp_server_on_bc_audio(data, size, 0);
#endif

    sys_os_mutex_leave(p_rua->bc_mutex);

    return 0;
}

BOOL rtsp_bc_init_player(RSSUA * p_rua, int samplerate, int channels)
{    
#ifdef MEDIA_DEVICE
    if (p_rua->audio_player == NULL)
    {
#ifdef ANDROID
        p_rua->audio_player = new CQAudioPlay();
#elif defined(IOS)
        p_rua->audio_player = new CMAudioPlay();
#elif __WINDOWS_OS__    
        p_rua->audio_player = new CWAudioPlay();
#elif __LINUX_OS__
        p_rua->audio_player = new CLAudioPlay();
#endif
    }

    if (p_rua->audio_player == NULL)
    {
        return FALSE;
    }

    p_rua->audio_player->setVolume(HTVOLUME_MAX);
    p_rua->audio_player->startPlay(samplerate, channels);

    p_rua->audio_queue = hqueue_create(10, sizeof(AVFrame), HQ_GET_WAIT | HQ_PUT_WAIT);

    p_rua->audio_play_flag = TRUE;
    p_rua->audio_play_thread = sys_os_create_thread((void *)rtsp_bc_audio_play_thread, p_rua);

    return TRUE;
#else
    // todo : add your handler ...
    
    return TRUE;
#endif
}

BOOL rtsp_bc_init_audio(RSSUA * p_rua)
{
    BOOL ret = FALSE;

    if (p_rua->ad_inited)
    {
        return TRUE;
    }

#ifdef MEDIA_DEVICE    
    p_rua->audio_decoder = new CAudioDecoder();
    if (p_rua->audio_decoder)
    {    
        ret = p_rua->audio_decoder->init(p_rua->media_info.bc_info.codec, 
                p_rua->media_info.bc_info.samplerate, p_rua->media_info.bc_info.channels);
    }

    if (ret)
    {
        p_rua->audio_decoder->setCallback(rtsp_bc_decoder_cb, p_rua);
        
        ret = rtsp_bc_init_player(p_rua, p_rua->media_info.bc_info.samplerate, p_rua->media_info.bc_info.channels);
    }
#else
    // todo : add your handler ...
    
    ret = TRUE;
#endif

    if (ret)
    {
        aac_rxi_init(&p_rua->aacrxi, rtsp_bc_data_cb, p_rua);

        p_rua->aacrxi.size_length = 13;
        p_rua->aacrxi.index_length = 3;
        p_rua->aacrxi.index_delta_length = 3;
    
        p_rua->ad_inited = 1;
        p_rua->bc_mutex = sys_os_create_mutex();
    }
    else
    {
        rtsp_bc_uninit_audio(p_rua);
    }

    return ret;
}

void rtsp_bc_uninit_audio(RSSUA * p_rua)
{
    if (NULL == p_rua)
    {
        return;
    }

    sys_os_mutex_enter(p_rua->bc_mutex);
    
#ifdef MEDIA_DEVICE
    p_rua->audio_play_flag = FALSE;

    AVFrame * frame = av_frame_alloc();
    if (frame)
    {
        frame->data[0] = NULL;
        
        if (!hqueue_put(p_rua->audio_queue, (char *) frame))
        {
            av_frame_free(&frame);
        }
    }
    
    // Wait for the audio playback thread to exit
    sys_os_wait_thread(&p_rua->audio_play_thread);

    while (!hqueue_is_empty(p_rua->audio_queue))
    {
        AVFrame frame;

        if (hqueue_get(p_rua->audio_queue, (char *)&frame))
        {
            av_frame_unref(&frame);
        }
    }

    if (p_rua->audio_queue)
    {
        hqueue_delete(p_rua->audio_queue);
        p_rua->audio_queue = NULL;
    }
    
    if (p_rua->audio_decoder)
    {
        delete p_rua->audio_decoder;
        p_rua->audio_decoder = NULL;
    }

    if (p_rua->audio_player)
    {
        delete p_rua->audio_player;
        p_rua->audio_player = NULL;
    }
#else
    // todo : add your handler ...
    
#endif

    aac_rxi_deinit(&p_rua->aacrxi);

    p_rua->ad_inited = 0;

    sys_os_mutex_leave(p_rua->bc_mutex);
    
    sys_os_destroy_mutex(p_rua->bc_mutex);
    p_rua->bc_mutex = NULL;
}

void rtsp_bc_data_rx(RSSUA * p_rua, uint8 * data, int rlen)
{
    if (AUDIO_CODEC_G726  == p_rua->media_info.bc_info.codec ||
        AUDIO_CODEC_G711A == p_rua->media_info.bc_info.codec ||
        AUDIO_CODEC_G711U == p_rua->media_info.bc_info.codec ||
        AUDIO_CODEC_G722  == p_rua->media_info.bc_info.codec ||
        AUDIO_CODEC_OPUS  == p_rua->media_info.bc_info.codec)
    {
        pcm_rtp_rx(&p_rua->pcmrxi, data, rlen);
    }
    else if (AUDIO_CODEC_AAC == p_rua->media_info.bc_info.codec)
    {
        aac_rtp_rx(&p_rua->aacrxi, data, rlen);
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, unsupport audio codec\r\n", 
            __FUNCTION__, p_rua->media_info.bc_info.codec);
    }
}

void rtsp_bc_tcp_data_rx(RSSUA * p_rua, uint8 * data, int rlen)
{
    uint8   interleaved = net_read_uint8(data+1);
    uint8 * p_rtp = data + 4;
    uint32  rtp_len = rlen - 4;

    if (interleaved != p_rua->channels[AV_BACK_CH].interleaved)
    {
        return;
    }
    
    rtsp_bc_data_rx(p_rua, p_rtp, rtp_len);
}

void rtsp_bc_udp_data_rx(RSSUA * p_rua, uint8 * data, int rlen)
{
    rtsp_bc_data_rx(p_rua, data, rlen);
}

void * rtsp_bc_udp_rx_thread(void * argv)
{
    fd_set fdr;
    RSSUA * p_rua = (RSSUA *)argv;
    int fd = p_rua->channels[AV_BACK_CH].udp_fd;
    char buf[2048];
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    
    while (p_rua->rtp_rx)
    {
        FD_ZERO(&fdr);
        FD_SET(fd, &fdr);

        struct timeval tv = {1, 0};

        int sret = select(fd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            usleep(10*1000);
            continue;
        }

        if (!FD_ISSET(fd, &fdr))
        {
            continue;
        }

        if (p_rua->sockaddr.ipv6_flag)
        {
            addr = (struct sockaddr *) &addr6;
            addrlen = sizeof(struct sockaddr_in6);
        }
        else
        {
            addr = (struct sockaddr *) &addr4;
            addrlen = sizeof(struct sockaddr_in);
        }

        int rlen = recvfrom(fd, buf, sizeof(buf), 0, addr, &addrlen);
        if (rlen <= 12)
        {
            log_print(HT_LOG_ERR, "%s, recvfrom return %d, err[%s]!!!\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());
        }
        else
        {
#ifdef RTSP_SRTP
            if (p_rua->srtp_enable)
            {
                if (!p_rua->channels[AV_BACK_CH].srtp)
                {
                    continue;
                }
                
                if (srtp_unprotect(p_rua->channels[AV_BACK_CH].srtp, buf, &rlen) != srtp_err_status_ok)
                {
                    continue;
                }
            }
#endif
            rtsp_bc_udp_data_rx(p_rua, (uint8*)buf, rlen);
        }
    }

    log_print(HT_LOG_DBG, "%s, exit!!!\r\n", __FUNCTION__);
    
    return NULL;
}

BOOL rtsp_bc_parse_url_parameters(RSSUA * p_rua)
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

    p++; // skip '&' char

    if (get_name_value_pair(p, strlen(p), "bce", value, sizeof(value)))
    {
        if (strcasecmp(value, "G711") == 0 || strcasecmp(value, "PCMU") == 0)
        {
            p_rua->media_info.bc_info.codec = AUDIO_CODEC_G711U;
        }
        else if (strcasecmp(value, "G726") == 0)
        {
            p_rua->media_info.bc_info.codec = AUDIO_CODEC_G726;
        }
        else if (strcasecmp(value, "AAC") == 0 || strcasecmp(value, "MP4A-LATM") == 0)
        {
            p_rua->media_info.bc_info.codec = AUDIO_CODEC_AAC;
        }
    }

    if (get_name_value_pair(p, strlen(p), "bcsr", value, sizeof(value)))
    {
        p_rua->media_info.bc_info.samplerate = atoi(value);
    }

    if (get_name_value_pair(p, strlen(p), "bcch", value, sizeof(value)))
    {
        p_rua->media_info.bc_info.channels = atoi(value);
    }

    return TRUE;
}

#endif // end of RTSP_BACKCHANNEL


