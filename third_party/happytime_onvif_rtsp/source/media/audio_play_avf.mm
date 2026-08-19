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
#include "audio_play_avf.h"
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

/***************************************************************************************/

typedef struct
{
    BOOL                        play_flag;
    pthread_t                   play_thread;
    AudioQueueRef               audio_queue;
    int                         num_audio_buffers;
    AudioQueueBufferRef       * audio_buffer;
    void                      * buffer;
    uint32                      buffer_offset;
    uint32                      buffer_size;
    AudioStreamBasicDescription strdesc;
    AudioDeviceID               device_id;
    int                         samplerate;
    uint16                      channels;
    uint16                      samples;
    int                         samplefmt;
    avf_audio_play_callback     callback;
    void                      * userdata;
    void                      * mutex_cb;
} AVFAudioPlayContext;

/***************************************************************************************/

BOOL avf_audio_play_prepare_device(AVFAudioPlayContext * context)
{
    AudioDeviceID devid;
    OSStatus result = noErr;
    uint32 size = 0;
    uint32 alive = 0;
    pid_t pid = 0;

    AudioObjectPropertyAddress addr = {
        0,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    size = sizeof(AudioDeviceID);
    addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                        0, NULL, &size, &devid);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioObjectGetPropertyData failed\r\n", __FUNCTION__);
        return FALSE;
    }

    addr.mSelector = kAudioDevicePropertyDeviceIsAlive;
    addr.mScope = kAudioDevicePropertyScopeOutput;

    size = sizeof (alive);
    result = AudioObjectGetPropertyData(devid, &addr, 0, NULL, &size, &alive);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioObjectGetPropertyData failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    if (!alive) 
    {
        log_print(HT_LOG_ERR, "%s, requested device exists, but isn't alive\r\n", __FUNCTION__);
        return FALSE;
    }

    addr.mSelector = kAudioDevicePropertyHogMode;
    size = sizeof (pid);
    result = AudioObjectGetPropertyData(devid, &addr, 0, NULL, &size, &pid);

    /* some devices don't support this property, so errors are fine here. */
    if ((result == noErr) && (pid != -1)) 
    {
        log_print(HT_LOG_ERR, "%s, requested device is being hogged\r\n", __FUNCTION__);
        return FALSE;
    }

    context->device_id = devid;
    
    return TRUE;
}

int avf_audio_play_assign_device(AVFAudioPlayContext * context)
{
    const AudioObjectPropertyAddress prop = {
        kAudioDevicePropertyDeviceUID,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };

    OSStatus result;
    CFStringRef devuid;
    uint32 devuidsize = sizeof (devuid);
    
    result = AudioObjectGetPropertyData(context->device_id, &prop, 0, NULL, &devuidsize, &devuid);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioObjectGetPropertyData failed\r\n", __FUNCTION__);
        return 0;
    }
    
    result = AudioQueueSetProperty(context->audio_queue, kAudioQueueProperty_CurrentDevice, &devuid, devuidsize);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueSetProperty failed\r\n", __FUNCTION__);
        return 0;
    }

    return 1;
}

void avf_audio_play_cb(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
    AVFAudioPlayContext * context = (AVFAudioPlayContext *) inUserData;

    uint32 remaining = inBuffer->mAudioDataBytesCapacity;
    uint8 *ptr = (uint8 *) inBuffer->mAudioData;

    while (remaining > 0) 
    {
        uint32 len;
        
        if (context->buffer_offset >= context->buffer_size) 
        {
            sys_os_mutex_enter(context->mutex_cb);
            if (context->callback)
            {
                context->callback(context->buffer, context->buffer_size, context->userdata);
            }
            sys_os_mutex_leave(context->mutex_cb);
            context->buffer_offset = 0;
        }

        len = context->buffer_size - context->buffer_offset;
        if (len > remaining) 
        {
            len = remaining;
        }
        memcpy(ptr, (char *)context->buffer + context->buffer_offset, len);
        ptr = ptr + len;
        remaining -= len;
        context->buffer_offset += len;
    }
    
    AudioQueueEnqueueBuffer(context->audio_queue, inBuffer, 0, NULL);
    
    inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;
}

int avf_audio_play_prepare_queue(AVFAudioPlayContext * context)
{
    int i;
    OSStatus result;
    const AudioStreamBasicDescription *strdesc = &context->strdesc;

    result = AudioQueueNewOutput(strdesc, avf_audio_play_cb, context, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, &context->audio_queue);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueNewOutput failed\r\n", __FUNCTION__);
        return 0;
    }

    if (!avf_audio_play_assign_device(context)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_play_assign_device failed\r\n", __FUNCTION__);
        return 0;
    }

    AudioChannelLayout layout;
    memset(&layout, 0, sizeof(layout));
    
    layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

    result = AudioQueueSetProperty(context->audio_queue, kAudioQueueProperty_ChannelLayout, &layout, sizeof(layout));
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueSetProperty failed\r\n", __FUNCTION__);
        return 0;
    }

    uint32 size = strdesc->mBitsPerChannel / 8;
    size *= context->channels;
    size *= context->samples;
    
    /* Allocate a sample buffer */
    context->buffer_size = size;
    context->buffer_offset = context->buffer_size;

    context->buffer = malloc(context->buffer_size);
    if (context->buffer == NULL) 
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return 0;
    }

    /* Make sure we can feed the device a minimum amount of time */
    double MINIMUM_AUDIO_BUFFER_TIME_MS = 15.0;

    const double msecs = (context->samples / ((double) context->samplerate)) * 1000.0;
    int num_audio_buffers = 2;
    if (msecs < MINIMUM_AUDIO_BUFFER_TIME_MS) 
    {  
        /* use more buffers if we have a VERY small sample set. */
        num_audio_buffers = ((int)ceil(MINIMUM_AUDIO_BUFFER_TIME_MS / msecs) * 2);
    }

    context->num_audio_buffers = num_audio_buffers;
    context->audio_buffer = (AudioQueueBufferRef *) calloc(1, sizeof(AudioQueueBufferRef) * num_audio_buffers);
    if (context->audio_buffer == NULL) 
    {
        log_print(HT_LOG_ERR, "%s, calloc failed\r\n", __FUNCTION__);
        return 0;
    }

    for (i = 0; i < num_audio_buffers; i++) 
    {
        result = AudioQueueAllocateBuffer(context->audio_queue, size, &context->audio_buffer[i]);
        if (result != noErr)
        {
            log_print(HT_LOG_ERR, "%s, AudioQueueAllocateBuffer failed\r\n", __FUNCTION__);
            return 0;
        }
        
        memset(context->audio_buffer[i]->mAudioData, 0, context->audio_buffer[i]->mAudioDataBytesCapacity);
        context->audio_buffer[i]->mAudioDataByteSize = context->audio_buffer[i]->mAudioDataBytesCapacity;

        result = AudioQueueEnqueueBuffer(context->audio_queue, context->audio_buffer[i], 0, NULL);
        if (result != noErr)
        {
            log_print(HT_LOG_ERR, "%s, AudioQueueEnqueueBuffer failed\r\n", __FUNCTION__);
            return 0;
        }
    }

    result = AudioQueueStart(context->audio_queue, NULL);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueStart failed\r\n", __FUNCTION__);
        return 0;
    }

    /* We're running! */
    return 1;
}

void * avf_audio_play_thread(void * argv)
{
    AVFAudioPlayContext * context = (AVFAudioPlayContext *) argv;

    const int rc = avf_audio_play_prepare_queue(context);
    if (!rc) 
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_play_prepare_queue failed\r\n", __FUNCTION__);
        return NULL;
    }

    while (context->play_flag)
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.10, 1);
    }
    
    return NULL;
}

void * avf_audio_play_init(int samplerate, int channels)
{
    AVFAudioPlayContext * context = (AVFAudioPlayContext *)malloc(sizeof(AVFAudioPlayContext));
    if (NULL == context)
    {
        return NULL;
    }

    memset(context, 0, sizeof(AVFAudioPlayContext));

    context->samplerate = samplerate;
    context->channels = channels;
    context->samples = 1024;
    context->samplefmt = 1;     // AV_SAMPLE_FMT_S16

    AudioStreamBasicDescription *strdesc = &context->strdesc;

    memset(strdesc, 0, sizeof(AudioStreamBasicDescription));

    strdesc->mFormatID = kAudioFormatLinearPCM;
    strdesc->mFormatFlags = kLinearPCMFormatFlagIsPacked;
    strdesc->mChannelsPerFrame = channels;
    strdesc->mSampleRate = samplerate;
    strdesc->mFramesPerPacket = 1;
    strdesc->mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
    strdesc->mBitsPerChannel = 16;
    strdesc->mBytesPerFrame = strdesc->mChannelsPerFrame * strdesc->mBitsPerChannel / 8;
    strdesc->mBytesPerPacket = strdesc->mBytesPerFrame * strdesc->mFramesPerPacket;

    if (!avf_audio_play_prepare_device(context)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_prepare_device failed!\r\n", __FUNCTION__);
        goto fail;
    }

    context->play_flag = TRUE;
    context->play_thread = sys_os_create_thread((void *)avf_audio_play_thread, (void *)context);
    if (!context->play_thread) 
    {
        goto fail;
    }

    context->mutex_cb = sys_os_create_mutex();
    
    return context;

fail:

    avf_audio_play_uninit((void *)context);
    
    return NULL;
}

void avf_audio_play_uninit(void * ctx)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFAudioPlayContext * context = (AVFAudioPlayContext *)ctx;

    if (context->audio_queue) 
    {
        AudioQueueDispose(context->audio_queue, 1);
    }

    context->play_flag = FALSE;

    sys_os_wait_thread(&context->play_thread);

    free(context->audio_buffer);
    free(context->buffer);

    sys_os_destroy_mutex(context->mutex_cb);
    
    free(context);
}

void avf_audio_play_set_callback(void * ctx, avf_audio_play_callback cb, void * userdata)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFAudioPlayContext * context = (AVFAudioPlayContext *)ctx;

    sys_os_mutex_enter(context->mutex_cb);
    context->callback = cb;
    context->userdata = userdata;
    sys_os_mutex_leave(context->mutex_cb);
}

BOOL avf_audio_play_set_volume(void * ctx, double volume)
{
    if (NULL == ctx)
    {
        return FALSE;
    }

    AVFAudioPlayContext * context = (AVFAudioPlayContext *)ctx;

    if (!context->audio_queue)
    {
        return FALSE;
    }
    
    OSStatus result = AudioQueueSetParameter(context->audio_queue, kAudioQueueParam_Volume, volume);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueSetParameter failed\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

double avf_audio_play_get_volume(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    double volume = 0; 
    AVFAudioPlayContext * context = (AVFAudioPlayContext *)ctx;

    if (!context->audio_queue)
    {
        return 0;
    }
    
    OSStatus result = AudioQueueGetParameter(context->audio_queue, kAudioQueueParam_Volume, (float *)&volume);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueGetParameter failed\r\n", __FUNCTION__);
        return 0;
    }

    return volume;
}



