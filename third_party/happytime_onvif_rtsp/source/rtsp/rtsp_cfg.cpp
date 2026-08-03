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
#include "rtsp_cfg.h"
#include "xml_node.h"
#include "media_format.h"
#ifdef MEDIA_PROXY
#include "media_proxy.h"
#endif

/**************************************************************************************/

// global rtsp configuration variable
RTSP_CFG g_rtsp_cfg;

/**************************************************************************************/

RTSP_USER * rtsp_get_idle_user()
{
    int i;
    
    for (i = 0; i < MAX_USERS; i++)
    {
        if (g_rtsp_cfg.users[i].username[0] == '\0')
        {
            return &g_rtsp_cfg.users[i];
        }
    }

    return NULL;
}

BOOL rtsp_add_user(RTSP_USER * p_user)
{
    RTSP_USER * p_idle_user;

    p_idle_user = rtsp_get_idle_user();
    if (p_idle_user)
    {
        memcpy(p_idle_user, p_user, sizeof(RTSP_USER));
        return TRUE;
    }

    return FALSE;
}

RTSP_USER * rtsp_find_user(const char * username)
{
    int i;
    
    for (i = 0; i < MAX_USERS; i++)
    {
        if (strcmp(g_rtsp_cfg.users[i].username, username) == 0)
        {
            return &g_rtsp_cfg.users[i];
        }
    }

    return NULL;
}

const char * rtsp_get_user_pass(const char * username)
{
    RTSP_USER * p_user;
    
    if (NULL == username || strlen(username) == 0)
    {
        return NULL;
    }
    
    p_user = rtsp_find_user(username);
    if (NULL != p_user)
    {
        return p_user->password;
    }

    return NULL;    
}

MEDIA_OUTPUT * rtsp_add_output(MEDIA_OUTPUT ** p_output)
{
    MEDIA_OUTPUT * p_tmp;
    MEDIA_OUTPUT * p_new_output = (MEDIA_OUTPUT *) malloc(sizeof(MEDIA_OUTPUT));
    if (NULL == p_new_output)
    {
        return NULL;
    }

    memset(p_new_output, 0, sizeof(MEDIA_OUTPUT));

    p_tmp = *p_output;
    if (NULL == p_tmp)
    {
        *p_output = p_new_output;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new_output;
    }    

    return p_new_output;
}

void rtsp_free_outputs(MEDIA_OUTPUT ** p_output)
{
    MEDIA_OUTPUT * p_next;
    MEDIA_OUTPUT * p_tmp = *p_output;

    while (p_tmp)
    {
        p_next = p_tmp->next;        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_output = NULL;
}

/**************************************************************************************/
BOOL rtsp_parse_user(XMLN * p_node, RTSP_USER * p_user)
{
    XMLN * p_username;
    XMLN * p_password;
    
    p_username = xml_node_get(p_node, "username");
    if (p_username && p_username->data)
    {
        strncpy(p_user->username, p_username->data, sizeof(p_user->username)-1);
    }
    else
    {
        return FALSE;
    }

    p_password = xml_node_get(p_node, "password");
    if (p_password && p_password->data)
    {
        strncpy(p_user->password, p_password->data, sizeof(p_user->password)-1);
    }

    return TRUE;
}

int rtsp_parse_video_codec(const char * buff)
{
    if (strcasecmp(buff, "H264") == 0)
    {
        return VIDEO_CODEC_H264;
    }
    else if (strcasecmp(buff, "H265") == 0)
    {
        return VIDEO_CODEC_H265;
    }
    else if (strcasecmp(buff, "MP4") == 0)
    {
        return VIDEO_CODEC_MP4;
    }
    else if (strcasecmp(buff, "JPEG") == 0)
    {
        return VIDEO_CODEC_JPEG;
    }

    return VIDEO_CODEC_NONE;
}

int rtsp_parse_audio_codec(const char * buff)
{
    if (strcasecmp(buff, "G711") == 0)
    {
        return AUDIO_CODEC_G711A;
    }
    else if (strcasecmp(buff, "G711A") == 0)
    {
        return AUDIO_CODEC_G711A;
    }
    else if (strcasecmp(buff, "G711U") == 0)
    {
        return AUDIO_CODEC_G711U;
    }
    else if (strcasecmp(buff, "G726") == 0)
    {
        return AUDIO_CODEC_G726;
    }
    else if (strcasecmp(buff, "AAC") == 0)
    {
        return AUDIO_CODEC_AAC;
    }
    else if (strcasecmp(buff, "G722") == 0)
    {
        return AUDIO_CODEC_G722;
    }
    else if (strcasecmp(buff, "OPUS") == 0)
    {
        return AUDIO_CODEC_OPUS;
    }

    return AUDIO_CODEC_NONE;
}

BOOL rtsp_parse_video_info(XMLN * p_node, VIDEO_INFO * p_info)
{
    XMLN * p_codec;
    XMLN * p_width;
    XMLN * p_height;
    XMLN * p_framerate;
    XMLN * p_bitrate;

    p_codec = xml_node_get(p_node, "codec");
    if (p_codec && p_codec->data)
    {
        p_info->codec = rtsp_parse_video_codec(p_codec->data);
    }

    p_width = xml_node_get(p_node, "width");
    if (p_width && p_width->data)
    {
        p_info->width = atoi(p_width->data);

        if (p_info->width < 0)
        {
            p_info->width = 0;
        }
    }

    p_height = xml_node_get(p_node, "height");
    if (p_height && p_height->data)
    {
        p_info->height = atoi(p_height->data);

        if (p_info->height < 0)
        {
            p_info->height = 0;
        }
    }

    p_framerate = xml_node_get(p_node, "framerate");
    if (p_framerate && p_framerate->data)
    {
        p_info->framerate = atof(p_framerate->data);

        if (p_info->framerate < 0)
        {
            p_info->framerate = 0;
        }
    }

    p_bitrate = xml_node_get(p_node, "bitrate");
    if (p_bitrate && p_bitrate->data)
    {
        p_info->bitrate = atoi(p_bitrate->data);

        if (p_info->bitrate < 0)
        {
            p_info->bitrate = 0;
        }
    }

    return TRUE;
}

BOOL rtsp_parse_audio_info(XMLN * p_node, AUDIO_INFO * p_info)
{
    XMLN * p_codec;
    XMLN * p_samplerate;
    XMLN * p_channels;
    XMLN * p_bitrate;

    p_codec = xml_node_get(p_node, "codec");
    if (p_codec && p_codec->data)
    {
        p_info->codec = rtsp_parse_audio_codec(p_codec->data);
    }

    p_samplerate = xml_node_get(p_node, "samplerate");
    if (p_samplerate && p_samplerate->data)
    {
        p_info->samplerate = atoi(p_samplerate->data);

        if (p_info->samplerate < 0)
        {
            p_info->samplerate = 0;
        }
    }

    p_channels = xml_node_get(p_node, "channels");
    if (p_channels && p_channels->data)
    {
        p_info->channels = atoi(p_channels->data);

        if (p_info->channels < 0)
        {
            p_info->channels = 0;
        }
        else if (p_info->channels > 2)
        {
            p_info->channels = 2;
        }
    }

    p_bitrate = xml_node_get(p_node, "bitrate");
    if (p_bitrate && p_bitrate->data)
    {
        p_info->bitrate = atoi(p_bitrate->data);

        if (p_info->bitrate < 0)
        {
            p_info->bitrate = 0;
        }
    }

    return TRUE;
}

BOOL rtsp_parse_media_info(XMLN * p_node, MEDIA_INFO * p_info)
{
    XMLN * p_video;
    XMLN * p_audio;

    p_video = xml_node_get(p_node, "video");
    if (p_video)
    {
        p_info->has_video = rtsp_parse_video_info(p_video, &p_info->video);
    }

    p_audio = xml_node_get(p_node, "audio");
    if (p_audio)
    {
        p_info->has_audio = rtsp_parse_audio_info(p_audio, &p_info->audio);
    }
    
    return TRUE;
}

BOOL rtsp_parse_output(XMLN * p_node, MEDIA_OUTPUT * p_output)
{
    XMLN * p_url;
    XMLN * p_video;
    XMLN * p_audio;

    p_url = xml_node_get(p_node, "url");
    if (p_url && p_url->data)
    {
        strncpy(p_output->url, p_url->data, sizeof(p_output->url)-1);
    }

    p_video = xml_node_get(p_node, "video");
    if (p_video)
    {
        rtsp_parse_video_info(p_video, &p_output->video);
    }

    p_audio = xml_node_get(p_node, "audio");
    if (p_audio)
    {
        rtsp_parse_audio_info(p_audio, &p_output->audio);
    }
    
    return TRUE;
}

#ifdef HTTP_NOTIFY
BOOL rtsp_parse_http_notify(XMLN * p_node, HTTP_NOTIFY_CFG * p_cfg)
{
    XMLN * p_on_connect;
    XMLN * p_on_play;
    XMLN * p_on_publish;
    XMLN * p_on_done;
    XMLN * p_notify_method;

    p_on_connect = xml_node_get(p_node, "on_connect");
    if (p_on_connect && p_on_connect->data)
    {
        strncpy(p_cfg->on_connect, p_on_connect->data, sizeof(p_cfg->on_connect)-1);
    }

    p_on_play = xml_node_get(p_node, "on_play");
    if (p_on_play && p_on_play->data)
    {
        strncpy(p_cfg->on_play, p_on_play->data, sizeof(p_cfg->on_play)-1);
    }

    p_on_publish = xml_node_get(p_node, "on_publish");
    if (p_on_publish && p_on_publish->data)
    {
        strncpy(p_cfg->on_publish, p_on_publish->data, sizeof(p_cfg->on_publish)-1);
    }

    p_on_done = xml_node_get(p_node, "on_done");
    if (p_on_done && p_on_done->data)
    {
        strncpy(p_cfg->on_done, p_on_done->data, sizeof(p_cfg->on_done)-1);
    }

    p_cfg->notify_method = HTTP_NTF_METHOD_POST;
    
    p_notify_method = xml_node_get(p_node, "notify_method");
    if (p_notify_method && p_notify_method->data)
    {
        if (strcasecmp(p_notify_method->data, "GET") == 0)
        {
            p_cfg->notify_method = HTTP_NTF_METHOD_GET;
        }
    }

    return TRUE;
}
#endif // HTTP_NOTIFY

#ifdef MEDIA_PROXY
int rtsp_parse_rc_transfer(const char * buff)
{
    if (strcasecmp(buff, "UDP") == 0)
    {
        return RC_TRANSFER_UDP;
    }
    else if (strcasecmp(buff, "MULTICAST") == 0)
    {
        return RC_TRANSFER_MULTICAST;
    }

    return RC_TRANSFER_TCP;
}

BOOL rtsp_parse_proxy_cfg(XMLN * p_node, PROXY_CFG * p_proxy)
{
    XMLN * p_suffix;
    XMLN * p_url;
    XMLN * p_user;
    XMLN * p_pass;
    XMLN * p_transfer;
    XMLN * p_ondemand;
    XMLN * p_output;

    p_suffix = xml_node_get(p_node, "suffix");
    if (p_suffix && p_suffix->data)
    {
        strncpy(p_proxy->key, p_suffix->data, sizeof(p_proxy->key)-1);
    }
    else
    {
        return FALSE;
    }
    
    p_url = xml_node_get(p_node, "url");
    if (p_url && p_url->data)
    {
        strncpy(p_proxy->url, p_url->data, sizeof(p_proxy->url)-1);
    }
    else
    {
        return FALSE;
    }

    p_user = xml_node_get(p_node, "user");
    if (p_user && p_user->data)
    {
        strncpy(p_proxy->user, p_user->data, sizeof(p_proxy->user)-1);
    }

    p_pass = xml_node_get(p_node, "pass");
    if (p_pass && p_pass->data)
    {
        strncpy(p_proxy->pass, p_pass->data, sizeof(p_proxy->pass)-1);
    }

    p_transfer = xml_node_get(p_node, "transfer");
    if (p_transfer && p_transfer->data)
    {
        p_proxy->transfer = rtsp_parse_rc_transfer(p_transfer->data);
    }

    p_ondemand = xml_node_get(p_node, "ondemand");
    if (p_ondemand && p_ondemand->data)
    {
        p_proxy->on_demand = atoi(p_ondemand->data);
    }

    p_output = xml_node_get(p_node, "output");
    if (p_output)
    {
        p_proxy->has_output = rtsp_parse_media_info(p_output, &p_proxy->output);
    }
    
    return TRUE;
}
#endif // end of  MEDIA_PROXY   

#ifdef MEDIA_PUSHER
int rtsp_parse_transfer_mode(const char * str)
{
    if (strcasecmp(str, "TCP") == 0)
    {
        return TRANSFER_MODE_TCP;
    }
    else if (strcasecmp(str, "UDP") == 0)
    {
        return TRANSFER_MODE_UDP;
    }
    else if (strcasecmp(str, "RTSP") == 0)
    {
        return TRANSFER_MODE_RTSP;
    }

    return -1;
}

BOOL rtsp_parse_pusher_cfg(XMLN * p_node, PUSHER_CFG * p_pusher)
{
    XMLN * p_suffix;
    XMLN * p_video;
    XMLN * p_audio;
    XMLN * p_transfer;
    XMLN * p_output;

    p_suffix = xml_node_get(p_node, "suffix");
    if (p_suffix && p_suffix->data)
    {
        strncpy(p_pusher->key, p_suffix->data, sizeof(p_pusher->key)-1);
    }
    else
    {
        return FALSE;
    }

    p_video = xml_node_get(p_node, "video");
    if (p_video) 
    {
        p_pusher->input.has_video = rtsp_parse_video_info(p_video, &p_pusher->input.video);
    }

    p_audio = xml_node_get(p_node, "audio");
    if (p_audio) 
    {
        p_pusher->input.has_audio = rtsp_parse_audio_info(p_audio, &p_pusher->input.audio);
    }

    p_transfer = xml_node_get(p_node, "transfer");
    if (p_transfer)
    {
        XMLN * p_mode;
        XMLN * p_ip;
        XMLN * p_vport;
        XMLN * p_aport;

        p_mode = xml_node_get(p_transfer, "mode");
        if (p_mode && p_mode->data)
        {
            p_pusher->transfer.mode = rtsp_parse_transfer_mode(p_mode->data);
        }

        p_ip = xml_node_get(p_transfer, "ip");
        if (p_ip && p_ip->data)
        {
            strncpy(p_pusher->transfer.ip, p_ip->data, sizeof(p_pusher->transfer.ip)-1);
        }

        p_vport = xml_node_get(p_transfer, "vport");
        if (p_vport && p_vport->data)
        {
            p_pusher->transfer.vport = atoi(p_vport->data);
        }

        p_aport = xml_node_get(p_transfer, "aport");
        if (p_aport && p_aport->data)
        {
            p_pusher->transfer.aport = atoi(p_aport->data);
        }
    }

    p_output = xml_node_get(p_node, "output");
    if (p_output)
    {
        p_pusher->has_output = rtsp_parse_media_info(p_output, &p_pusher->output);
    }
    
    return TRUE;
}
#endif // end of  MEDIA_PUSHER  

#ifdef RTSP_BACKCHANNEL
BOOL rtsp_parse_backchannel(XMLN * p_node, RTSP_BC_CFG * p_req)
{
    XMLN * p_codec;
    XMLN * p_samplerate;
    XMLN * p_channels;

    p_codec = xml_node_get(p_node, "codec");
    if (p_codec && p_codec->data)
    {
        p_req->codec = rtsp_parse_audio_codec(p_codec->data);
    }

    p_samplerate = xml_node_get(p_node, "samplerate");
    if (p_samplerate && p_samplerate->data)
    {
        p_req->samplerate = atoi(p_samplerate->data);

        if (p_req->samplerate < 0)
        {
            p_req->samplerate = 8000; // default is 8000
        }
    }

    p_channels = xml_node_get(p_node, "channels");
    if (p_channels && p_channels->data)
    {
        p_req->channels = atoi(p_channels->data);

        if (p_req->channels < 0 || p_req->channels > 2)
        {
            p_req->channels = 1;
        }
    }

    return TRUE;
}
#endif // end of RTSP_BACKCHANNEL

BOOL rtsp_parse_config(char * xml_buff, int rlen)
{
    XMLN * p_node;
    XMLN * p_serverip;
    XMLN * p_rtsp_port;
#ifdef RTSPS
    XMLN * p_rtsps_enable;
    XMLN * p_rtsps_port;
    XMLN * p_rtsps_cert;
    XMLN * p_rtsps_key;
#endif
    XMLN * p_loop_nums;
    XMLN * p_multicast;
    XMLN * p_udp_base_port;
    XMLN * p_ipv6_enable;
    XMLN * p_need_auth;
    XMLN * p_crypt;
    XMLN * p_log_enable;
    XMLN * p_log_level;
#ifdef HTTP_NOTIFY
    XMLN * p_http_notify;
#endif
#ifdef RTSP_METADATA
    XMLN * p_metadata;
#endif
#ifdef RTSP_OVER_HTTP
    XMLN * p_rtsp_over_http;
    XMLN * p_http_port;
#ifdef HTTPS
    XMLN * p_rtsp_over_https;
    XMLN * p_https_port;
    XMLN * p_https_cert;
    XMLN * p_https_key;
#endif    
#endif
    XMLN * p_user;
    XMLN * p_output;
#ifdef MEDIA_PROXY    
    XMLN * p_proxy;
#endif
#ifdef MEDIA_PUSHER    
    XMLN * p_pusher;
#endif
#ifdef RTSP_BACKCHANNEL
    XMLN * p_backchannel;
#endif

    p_node = xxx_hxml_parse(xml_buff, rlen);
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_serverip = xml_node_get(p_node, "serverip");
    if (p_serverip && p_serverip->data)
    {
        strncpy(g_rtsp_cfg.serverip, p_serverip->data, sizeof(g_rtsp_cfg.serverip)-1);
    }
    
    p_rtsp_port = xml_node_get(p_node, "rtsp_port");
    if (!p_rtsp_port)
    {
        // Compatible with old configuration files
        p_rtsp_port = xml_node_get(p_node, "serverport");
    }
    
    if (p_rtsp_port && p_rtsp_port->data)
    {
        g_rtsp_cfg.rtsp_port = atoi(p_rtsp_port->data);
    }

#ifdef RTSPS
    p_rtsps_enable = xml_node_get(p_node, "rtsps_enable");
    if (p_rtsps_enable && p_rtsps_enable->data)
    {
        g_rtsp_cfg.rtsps_enable = atoi(p_rtsps_enable->data);
    }

    p_rtsps_port = xml_node_get(p_node, "rtsps_port");
    if (p_rtsps_port && p_rtsps_port->data)
    {
        g_rtsp_cfg.rtsps_port = atoi(p_rtsps_port->data);
    }

    p_rtsps_cert = xml_node_get(p_node, "rtsps_cert");
    if (p_rtsps_cert && p_rtsps_cert->data)
    {
        strncpy(g_rtsp_cfg.rtsps_cert, p_rtsps_cert->data, sizeof(g_rtsp_cfg.rtsps_cert)-1);
    }

    p_rtsps_key = xml_node_get(p_node, "rtsps_key");
    if (p_rtsps_key && p_rtsps_key->data)
    {
        strncpy(g_rtsp_cfg.rtsps_key, p_rtsps_key->data, sizeof(g_rtsp_cfg.rtsps_key)-1);
    }
#endif // RTSPS

    p_loop_nums = xml_node_get(p_node, "loop_nums");
    if (p_loop_nums && p_loop_nums->data)
    {
        g_rtsp_cfg.loop_nums = (uint32) atoi(p_loop_nums->data);
    }

    p_multicast = xml_node_get(p_node, "multicast");
    if (p_multicast && p_multicast->data)
    {
        g_rtsp_cfg.multicast = (uint32) atoi(p_multicast->data);
    }

    p_udp_base_port = xml_node_get(p_node, "udp_base_port");
    if (p_udp_base_port && p_udp_base_port->data)
    {
        g_rtsp_cfg.udp_base_port = (uint16) atoi(p_udp_base_port->data);
    }

    p_ipv6_enable = xml_node_get(p_node, "ipv6_enable");
    if (p_ipv6_enable && p_ipv6_enable->data)
    {
        g_rtsp_cfg.ipv6_enable = atoi(p_ipv6_enable->data);
    }

    p_need_auth = xml_node_get(p_node, "need_auth");
    if (p_need_auth && p_need_auth->data)
    {
        g_rtsp_cfg.need_auth = atoi(p_need_auth->data);
    }

    p_crypt = xml_node_get(p_node, "crypt");
    if (p_crypt && p_crypt->data)
    {
        g_rtsp_cfg.crypt = atoi(p_crypt->data);
    }
    
    p_log_enable = xml_node_get(p_node, "log_enable");
    if (p_log_enable && p_log_enable->data)
    {
        g_rtsp_cfg.log_enable = atoi(p_log_enable->data);
    }

    p_log_level = xml_node_get(p_node, "log_level");
    if (p_log_level && p_log_level->data)
    {
        g_rtsp_cfg.log_level = atoi(p_log_level->data);
    }

#ifdef HTTP_NOTIFY
    p_http_notify = xml_node_get(p_node, "http_notify");
    if (p_http_notify)
    {
        rtsp_parse_http_notify(p_http_notify, &g_rtsp_cfg.http_notify);
    }
#endif

#ifdef RTSP_METADATA
    p_metadata = xml_node_get(p_node, "metadata");
    if (p_metadata && p_metadata->data)
    {
        g_rtsp_cfg.metadata = atoi(p_metadata->data);
    }
#endif

#ifdef RTSP_OVER_HTTP
    p_rtsp_over_http = xml_node_get(p_node, "rtsp_over_http");
    if (p_rtsp_over_http && p_rtsp_over_http->data)
    {
        g_rtsp_cfg.rtsp_over_http = atoi(p_rtsp_over_http->data);
    }

    p_http_port = xml_node_get(p_node, "http_port");
    if (p_http_port && p_http_port->data)
    {
        g_rtsp_cfg.http_port = atoi(p_http_port->data);
    }

#ifdef HTTPS
    p_rtsp_over_https = xml_node_get(p_node, "rtsp_over_https");
    if (p_rtsp_over_https && p_rtsp_over_https->data)
    {
        g_rtsp_cfg.rtsp_over_https = atoi(p_rtsp_over_https->data);
    }

    p_https_port = xml_node_get(p_node, "https_port");
    if (p_https_port && p_https_port->data)
    {
        g_rtsp_cfg.https_port = atoi(p_https_port->data);
    }

    p_https_cert = xml_node_get(p_node, "https_cert");
    if (!p_https_cert)
    {
        // Compatible with old configuration files
        p_https_cert = xml_node_get(p_node, "cert_file");
    }
    
    if (p_https_cert && p_https_cert->data)
    {
        strncpy(g_rtsp_cfg.https_cert, p_https_cert->data, sizeof(g_rtsp_cfg.https_cert)-1);
    }

    p_https_key = xml_node_get(p_node, "https_key");
    if (!p_https_key)
    {
        // Compatible with old configuration files
        p_https_key = xml_node_get(p_node, "key_file");
    }
    
    if (p_https_key && p_https_key->data)
    {
        strncpy(g_rtsp_cfg.https_key, p_https_key->data, sizeof(g_rtsp_cfg.https_key)-1);
    }
#endif // HTTPS

#endif // RTSP_OVER_HTTP
    
    p_user = xml_node_get(p_node, "user");
    while (p_user && strcmp(p_user->name, "user") == 0)
    {
        RTSP_USER user;
        memset(&user, 0, sizeof(user));
        
        if (rtsp_parse_user(p_user, &user))
        {
            rtsp_add_user(&user);
        }

        p_user = p_user->next;
    }

    p_output = xml_node_get(p_node, "output");
    while (p_output && strcasecmp(p_output->name, "output") == 0)
    {
        MEDIA_OUTPUT output;
        memset(&output, 0, sizeof(output));
        
        if (rtsp_parse_output(p_output, &output))
        {
            MEDIA_OUTPUT * p_info = rtsp_add_output(&g_rtsp_cfg.output);
            if (p_info)
            {
                memcpy(p_info, &output, sizeof(MEDIA_OUTPUT));
            }
        }
        
        p_output = p_output->next;
    }

#ifdef MEDIA_PROXY
    p_proxy = xml_node_get(p_node, "proxy");
    while (p_proxy && strcasecmp(p_proxy->name, "proxy") == 0)
    {
        MEDIA_PRY proxy;
        memset(&proxy, 0, sizeof(proxy));
        
        if (rtsp_parse_proxy_cfg(p_proxy, &proxy.cfg))
        {
            MEDIA_PRY * p_info = media_add_proxy();
            if (p_info)
            {
                memcpy(&p_info->cfg, &proxy.cfg, sizeof(PROXY_CFG));
            }
        }

        p_proxy = p_proxy->next;
    }
#endif // end of MEDIA_PROXY

#ifdef MEDIA_PUSHER
    p_pusher = xml_node_get(p_node, "pusher");
    while (p_pusher && strcasecmp(p_pusher->name, "pusher") == 0)
    {
        MEDIA_PUSH pusher;
        memset(&pusher, 0, sizeof(pusher));
        
        if (rtsp_parse_pusher_cfg(p_pusher, &pusher.cfg))
        {
            MEDIA_PUSH * p_info = media_add_pusher(TRUE);
            if (p_info)
            {
                memcpy(&p_info->cfg, &pusher.cfg, sizeof(PUSHER_CFG));
            }
        }

        p_pusher = p_pusher->next;
    }
#endif // end of MEDIA_PUSHER

#ifdef RTSP_BACKCHANNEL
    p_backchannel = xml_node_get(p_node, "backchannel");
    if (p_backchannel)
    {
        rtsp_parse_backchannel(p_backchannel, &g_rtsp_cfg.backchannel);
    }
#endif

    xml_node_del(p_node);
    
    return TRUE;
}

BOOL rtsp_read_config(const char * config)
{
    BOOL ret = FALSE;
    int len, rlen;
    FILE * fp = NULL;
    char * xml_buff = NULL;
    const char * filename = NULL;

    if (NULL == config || config[0] == '\0')
    {
        filename = RTSP_DEF_CFG;
    }
    else
    {
        filename = config;
    }

    // read config file
    
    fp = fopen(filename, "r");
    if (NULL == fp)
    {
        goto FAILED;
    }
    
    fseek(fp, 0, SEEK_END);
    
    len = ftell(fp);
    if (len <= 0)
    {
        goto FAILED;
    }
    fseek(fp, 0, SEEK_SET);
    
    xml_buff = (char *) malloc(len + 1);    
    if (NULL == xml_buff)
    {
        goto FAILED;
    }

    rlen = fread(xml_buff, 1, len, fp);
    if (rlen > 0)
    {
        xml_buff[rlen] = '\0';
        ret = rtsp_parse_config(xml_buff, rlen);
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, rlen = %d, len=%d\r\n", __FUNCTION__, rlen, len);
    }

FAILED:

    if (fp)
    {
        fclose(fp);
    }

    if (xml_buff)
    {
        free(xml_buff);
    }

    return ret;
}


/**************************************************************************************/

BOOL rtsp_url_match(const char * rtspurl, const char * cfgurl)
{
    if (cfgurl == NULL || cfgurl[0] == '\0')
    {
        return TRUE;
    }

    // match the externsion name
    if (cfgurl[0] == '*' && cfgurl[1] == '.')
    {
        int len = strlen(cfgurl) - 1;
        int rtspurl_len = strlen(rtspurl);

        if (rtspurl_len > len)
        {
            int i;
            
            for (i = 0; i < len; i++)
            {
                if (rtspurl[rtspurl_len - len + i] != cfgurl[i + 1])
                {
                    break;
                }
            }

            if (i == len)
            {
                return TRUE;
            }
        }
    }
    else if (strstr(rtspurl, cfgurl))
    {
        return TRUE;
    }

    return FALSE;
}

BOOL rtsp_cfg_get_video_info(const char * url, VIDEO_INFO * info)
{
    MEDIA_OUTPUT * p_output = g_rtsp_cfg.output;
    while (p_output)
    {
        if (rtsp_url_match(url, p_output->url))
        {
            if (info)
            {
                memcpy(info, &p_output->video, sizeof(VIDEO_INFO));
            }
            
            return TRUE;
        }
        
        p_output = p_output->next;
    }

    return FALSE;
}

BOOL rtsp_cfg_get_audio_info(const char * url, AUDIO_INFO * info)
{
    MEDIA_OUTPUT * p_output = g_rtsp_cfg.output;
    while (p_output)
    {
        if (rtsp_url_match(url, p_output->url))
        {
            if (info)
            {
                memcpy(info, &p_output->audio, sizeof(AUDIO_INFO));
            }
            
            return TRUE;
        }
        
        p_output = p_output->next;
    }

    return FALSE;
}

#ifdef RTSP_BACKCHANNEL
BOOL rtsp_cfg_get_backchannel_info(AUDIO_INFO * info)
{
    if (NULL == info)
    {
        return FALSE;
    }
    
    info->codec = g_rtsp_cfg.backchannel.codec;
    info->samplerate = g_rtsp_cfg.backchannel.samplerate;
    info->channels = g_rtsp_cfg.backchannel.channels;

    return TRUE;
}
#endif 




