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
#include "alsa.h"


BOOL alsa_get_device_name(int index, char * filename, int len)
{
    int card = -1, count = 0, ret;
    snd_ctl_card_info_t * info;

    snd_ctl_card_info_alloca(&info);
    
    ret = snd_card_next(&card);
    
    while (ret == 0 && card != -1) 
    {
        char name[16];
        snd_ctl_t * ctl;
        
        snprintf(name, sizeof(name), "hw:%d", card);
        
        if (snd_ctl_open(&ctl, name, SND_CTL_NONBLOCK) < 0)
        {
            ret = snd_card_next(&card);
            continue;
        }

        if (snd_ctl_card_info(ctl, info) < 0)
        {
            snd_ctl_close(ctl);
            ret = snd_card_next(&card);
            continue;
        }

        snd_ctl_close(ctl);
        
        if (index == count)
        {
            strncpy(filename, name, len);
            return TRUE;
        }
                
        ret = snd_card_next(&card);
    }

    return FALSE;
}

BOOL alsa_open_device(ALSACTX * p_alsa, snd_pcm_stream_t mode, uint32 * sample_rate, int channels)
{
    int res, flags;
    snd_pcm_format_t format;
    snd_pcm_t *h;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_uframes_t buffer_size, period_size = 0;

    if (NULL == p_alsa)
    {
        return FALSE;
    }

    flags = SND_PCM_NONBLOCK;
    format = SND_PCM_FORMAT_S16_LE;

    p_alsa->framesize = 16 / 8 * channels;
    
    res = snd_pcm_open(&h, p_alsa->devname, mode, flags);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot open audio device %s (%s)\r\n", p_alsa->devname, snd_strerror(res));
        return FALSE;
    }

    res = snd_pcm_hw_params_malloc(&hw_params);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot allocate hardware parameter structure (%s)\r\n", snd_strerror(res));
        goto fail1;
    }
    
    res = snd_pcm_hw_params_any(h, hw_params);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot initialize hardware parameter structure (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_access(h, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set access type (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_format(h, hw_params, format);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set sample format %d (%s)\r\n", format, snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_rate_near(h, hw_params, sample_rate, 0);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set sample rate (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_channels(h, hw_params, channels);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set channel count to %d (%s)\r\n", channels, snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_get_buffer_size_max(hw_params, &buffer_size);
    
    buffer_size = (buffer_size > ALSA_BUFFER_SIZE_MAX ? ALSA_BUFFER_SIZE_MAX : buffer_size);

    res = snd_pcm_hw_params_set_buffer_size_near(h, hw_params, &buffer_size);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set ALSA buffer size (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_get_period_size_min(hw_params, &period_size, NULL);
    
    if (!period_size)
    {
        period_size = buffer_size / 4;
    }
    
    if (period_size < 64)
    {
        period_size = 64;
    }
    
    res = snd_pcm_hw_params_set_period_size_near(h, hw_params, &period_size, NULL);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set ALSA period size (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    p_alsa->period_size = period_size;
    
    res = snd_pcm_hw_params(h, hw_params);
    if (res < 0) 
    {
        log_print(HT_LOG_ERR, "cannot set parameters (%s)\r\n", snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_free(hw_params);
        
    p_alsa->handler = h;
    
    return TRUE;

fail:
    snd_pcm_hw_params_free(hw_params);
fail1:
    snd_pcm_close(h);
    return FALSE;
}

void alsa_close_device(ALSACTX * p_alsa)
{
    if (p_alsa->handler)
    {
        if (snd_pcm_stream(p_alsa->handler) == SND_PCM_STREAM_PLAYBACK) 
        {
            snd_pcm_nonblock(p_alsa->handler, 0);
            snd_pcm_drain(p_alsa->handler);
        }
        
        snd_pcm_close(p_alsa->handler);
        p_alsa->handler = NULL;
    }
}

int alsa_xrun_recover(snd_pcm_t * handler, int err)
{
    if (err == -EPIPE) 
    {
        err = snd_pcm_prepare(handler);
        if (err < 0) 
        {
            log_print(HT_LOG_ERR, "snd_pcm_prepare failed: %s\r\n", snd_strerror(err));
        }
    } 
    else if (err == -ESTRPIPE) 
    {
        log_print(HT_LOG_ERR, "-ESTRPIPE... Unsupported!\n");
        return -1;
    }
    
    return err;
}


