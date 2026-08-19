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
#include "audio_capture_avf.h"
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

/***************************************************************************************/

typedef struct
{
    BOOL                        capture_flag;
    pthread_t                   capture_thread;
    AudioQueueRef               audio_queue;
    int                         num_audio_buffers;
    AudioQueueBufferRef       * audio_buffer;
    void                      * buffer;
    uint32                      buffer_offset;
    uint32                      buffer_size;
    AudioStreamBasicDescription strdesc;
    AudioDeviceID               device_id;
    int                         device_index;
    int                         samplerate;
    uint16                      channels;
    uint16                      samples;
    int                         samplefmt;
    avf_audio_callback          callback;
    void                      * userdata;
    void                      * mutex_cb;
} AVFAudioContext;

static const AudioObjectPropertyAddress devlist_address = 
{
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
};

/***************************************************************************************/

int avf_audio_device_nums()
{
    int count = 0;
    OSStatus result = noErr;
    uint32 size = 0;
    AudioDeviceID *devs = NULL;
    uint32 i = 0;
    uint32 max = 0;

    result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                            &devlist_address, 0, NULL, &size);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
    {
        return 0;
    }
    
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &devlist_address, 0, NULL, &size, devs);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    max = size / sizeof (AudioDeviceID);
    
    for (i = 0; i < max; i++) 
    {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[i];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;
        const AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        const AudioObjectPropertyAddress nameaddr = {
            kAudioObjectPropertyName,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        result = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size);
        if (result != noErr)
        {
            continue;
        }
        
        buflist = (AudioBufferList *) malloc(size);
        if (buflist == NULL)
        {
            continue;
        }
        
        result = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, buflist);
        if (result == noErr) 
        {
            uint32 j;
            
            for (j = 0; j < buflist->mNumberBuffers; j++) 
            {
                if (buflist->mBuffers[j].mNumberChannels > 0) 
                {
                    usable = 1;
                    break;
                }
            }
        }

        free(buflist);

        if (!usable)
        {
            continue;
        }

        size = sizeof (CFStringRef);
        result = AudioObjectGetPropertyData(dev, &nameaddr, 0, NULL, &size, &cfstr);
        if (result != kAudioHardwareNoError)
        {
            continue;
        }
        
        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) malloc(len + 1);
        usable = ((ptr != NULL) &&
                  (CFStringGetCString
                   (cfstr, ptr, len + 1, kCFStringEncodingUTF8)));

        CFRelease(cfstr);

        if (usable) 
        {
            len = strlen(ptr);
            
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len - 1] == ' ')) 
            {
                len--;
            }
            
            usable = (len > 0);
        }

        if (usable) 
        {
            count++;
        }

        free(ptr);
    }
    
    return count;
}

void avf_audio_device_list()
{
    int count = 0;
    OSStatus result = noErr;
    uint32 size = 0;
    AudioDeviceID *devs = NULL;
    uint32 i = 0;
    uint32 max = 0;

    result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                            &devlist_address, 0, NULL, &size);
    if (result != kAudioHardwareNoError)
    {
        return;
    }
    
    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
    {
        return;
    }
    
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &devlist_address, 0, NULL, &size, devs);
    if (result != kAudioHardwareNoError)
    {
        return;
    }

    printf("\r\nAvailable audio capture device : \r\n\r\n");
    
    max = size / sizeof (AudioDeviceID);
    
    for (i = 0; i < max; i++) 
    {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[i];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;
        const AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        const AudioObjectPropertyAddress nameaddr = {
            kAudioObjectPropertyName,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        result = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size);
        if (result != noErr)
        {
            continue;
        }
        
        buflist = (AudioBufferList *) malloc(size);
        if (buflist == NULL)
        {
            continue;
        }
        
        result = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, buflist);
        if (result == noErr) 
        {
            uint32 j;
            
            for (j = 0; j < buflist->mNumberBuffers; j++) 
            {
                if (buflist->mBuffers[j].mNumberChannels > 0) 
                {
                    usable = 1;
                    break;
                }
            }
        }

        free(buflist);

        if (!usable)
        {
            continue;
        }

        size = sizeof (CFStringRef);
        result = AudioObjectGetPropertyData(dev, &nameaddr, 0, NULL, &size, &cfstr);
        if (result != kAudioHardwareNoError)
        {
            continue;
        }
        
        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) malloc(len + 1);
        usable = ((ptr != NULL) &&
                  (CFStringGetCString
                   (cfstr, ptr, len + 1, kCFStringEncodingUTF8)));

        CFRelease(cfstr);

        if (usable) 
        {
            len = strlen(ptr);
            
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len - 1] == ' ')) 
            {
                len--;
            }
            
            usable = (len > 0);
        }

        if (usable) 
        {
            ptr[len] = '\0';

            printf("index : %d, name : %s\r\n", count++, ptr);
        }

        free(ptr);
    }
}

int avf_audio_device_get_index(const char * name)
{
    int count = 0;
    OSStatus result = noErr;
    uint32 size = 0;
    AudioDeviceID *devs = NULL;
    uint32 i = 0;
    uint32 max = 0;

    result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                            &devlist_address, 0, NULL, &size);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
    {
        return 0;
    }
    
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &devlist_address, 0, NULL, &size, devs);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    max = size / sizeof (AudioDeviceID);
    
    for (i = 0; i < max; i++) 
    {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[i];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;
        const AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        const AudioObjectPropertyAddress nameaddr = {
            kAudioObjectPropertyName,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        result = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size);
        if (result != noErr)
        {
            continue;
        }
        
        buflist = (AudioBufferList *) malloc(size);
        if (buflist == NULL)
        {
            continue;
        }
        
        result = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, buflist);
        if (result == noErr) 
        {
            uint32 j;
            
            for (j = 0; j < buflist->mNumberBuffers; j++) 
            {
                if (buflist->mBuffers[j].mNumberChannels > 0) 
                {
                    usable = 1;
                    break;
                }
            }
        }

        free(buflist);

        if (!usable)
        {
            continue;
        }

        size = sizeof (CFStringRef);
        result = AudioObjectGetPropertyData(dev, &nameaddr, 0, NULL, &size, &cfstr);
        if (result != kAudioHardwareNoError)
        {
            continue;
        }
        
        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) malloc(len + 1);
        usable = ((ptr != NULL) &&
                  (CFStringGetCString
                   (cfstr, ptr, len + 1, kCFStringEncodingUTF8)));

        CFRelease(cfstr);

        if (usable) 
        {
            len = strlen(ptr);
            
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len - 1] == ' ')) 
            {
                len--;
            }
            
            usable = (len > 0);
        }

        if (usable) 
        {
            ptr[len] = '\0';

            if (strcasecmp(ptr, name) == 0)
            {
                free(ptr);
                break;
            }

            count++;
        }

        free(ptr);
    }

    return count;
}

BOOL avf_audio_device_get_name(int index, char * name, int namesize)
{
    BOOL ret = FALSE;
    OSStatus result = noErr;
    uint32 size = 0;
    AudioDeviceID *devs = NULL;
    uint32 max = 0;

    result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                            &devlist_address, 0, NULL, &size);
    if (result != kAudioHardwareNoError)
    {
        return FALSE;
    }
    
    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
    {
        return FALSE;
    }
    
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &devlist_address, 0, NULL, &size, devs);
    if (result != kAudioHardwareNoError)
    {
        return FALSE;
    }
    
    max = size / sizeof (AudioDeviceID);

    if (index < 0 || index >= max)
    {
        return FALSE;
    }

    {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[index];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;
        const AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        const AudioObjectPropertyAddress nameaddr = {
            kAudioObjectPropertyName,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        result = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size);
        if (result != noErr)
        {
            return FALSE;
        }
        
        buflist = (AudioBufferList *) malloc(size);
        if (buflist == NULL)
        {
            return FALSE;
        }
        
        result = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, buflist);
        if (result == noErr) 
        {
            uint32 j;
            
            for (j = 0; j < buflist->mNumberBuffers; j++) 
            {
                if (buflist->mBuffers[j].mNumberChannels > 0) 
                {
                    usable = 1;
                    break;
                }
            }
        }

        free(buflist);

        if (!usable)
        {
            return FALSE;
        }

        size = sizeof (CFStringRef);
        result = AudioObjectGetPropertyData(dev, &nameaddr, 0, NULL, &size, &cfstr);
        if (result != kAudioHardwareNoError)
        {
            return FALSE;
        }
        
        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) malloc(len + 1);
        usable = ((ptr != NULL) &&
                  (CFStringGetCString
                   (cfstr, ptr, len + 1, kCFStringEncodingUTF8)));

        CFRelease(cfstr);

        if (usable) 
        {
            len = strlen(ptr);
            
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len - 1] == ' ')) 
            {
                len--;
            }
            
            usable = (len > 0);
        }

        if (usable) 
        {
            ptr[len] = '\0';

            ret = TRUE;
            strncpy(name, ptr, namesize);
        }

        free(ptr);
    }
    
    return ret;
}

AudioDeviceID avf_audio_get_deviceid(int index)
{
    int count = 0;
    OSStatus result = noErr;
    uint32 size = 0;
    AudioDeviceID *devs = NULL;
    uint32 i = 0;
    uint32 max = 0;

    result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                            &devlist_address, 0, NULL, &size);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
    {
        return 0;
    }
    
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &devlist_address, 0, NULL, &size, devs);
    if (result != kAudioHardwareNoError)
    {
        return 0;
    }
    
    max = size / sizeof (AudioDeviceID);
    
    for (i = 0; i < max; i++) 
    {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[i];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;
        const AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        const AudioObjectPropertyAddress nameaddr = {
            kAudioObjectPropertyName,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };

        result = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size);
        if (result != noErr)
        {
            continue;
        }
        
        buflist = (AudioBufferList *) malloc(size);
        if (buflist == NULL)
        {
            continue;
        }
        
        result = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, buflist);
        if (result == noErr) 
        {
            uint32 j;
            
            for (j = 0; j < buflist->mNumberBuffers; j++) 
            {
                if (buflist->mBuffers[j].mNumberChannels > 0) 
                {
                    usable = 1;
                    break;
                }
            }
        }

        free(buflist);

        if (!usable)
        {
            continue;
        }

        size = sizeof (CFStringRef);
        result = AudioObjectGetPropertyData(dev, &nameaddr, 0, NULL, &size, &cfstr);
        if (result != kAudioHardwareNoError)
        {
            continue;
        }
        
        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) malloc(len + 1);
        usable = ((ptr != NULL) &&
                  (CFStringGetCString
                   (cfstr, ptr, len + 1, kCFStringEncodingUTF8)));

        CFRelease(cfstr);

        if (usable) 
        {
            len = strlen(ptr);
            
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len - 1] == ' ')) 
            {
                len--;
            }
            
            usable = (len > 0);
        }

        if (usable) 
        {
            ptr[len] = '\0';

            if (count == index)
            {
                free(ptr);
                return dev;
            }

            count++;
        }

        free(ptr);
    }

    return 0;
}

BOOL avf_audio_prepare_device(AVFAudioContext * context)
{
    AudioDeviceID devid = (AudioDeviceID) avf_audio_get_deviceid(context->device_index);
    OSStatus result = noErr;
    uint32 size = 0;
    uint32 alive = 0;
    pid_t pid = 0;

    AudioObjectPropertyAddress addr = {
        0,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    addr.mSelector = kAudioDevicePropertyDeviceIsAlive;
    addr.mScope = kAudioDevicePropertyScopeInput;

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

int avf_audio_assign_device(AVFAudioContext * context)
{
    const AudioObjectPropertyAddress prop = {
        kAudioDevicePropertyDeviceUID,
        kAudioDevicePropertyScopeInput,
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

void avf_audio_input_cb(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer,
              const AudioTimeStamp *inStartTime, uint32 inNumberPacketDescriptions,
              const AudioStreamPacketDescription *inPacketDescs)
{
    AVFAudioContext * context = (AVFAudioContext *) inUserData;

    const uint8 * ptr = (const uint8 *) inBuffer->mAudioData;
    uint32 remaining = inBuffer->mAudioDataByteSize;
    while (remaining > 0) 
    {
        uint32 len = context->buffer_size - context->buffer_offset;
        if (len > remaining) 
        {
            len = remaining;
        }

        memcpy((char *)context->buffer + context->buffer_offset, ptr, len);
        ptr += len;
        remaining -= len;
        context->buffer_offset += len;

        if (context->buffer_offset >= context->buffer_size) 
        {
            if (context->callback)
            {
                avf_audio_data data;
                memset(&data, 0, sizeof(data));

                data.data[0] = (uint8 *)context->buffer;
                data.linesize[0] = context->buffer_size;
                data.samplerate = context->samplerate;
                data.channels = context->channels;
                data.samples = context->samples;
                data.format = context->samplefmt;
                
                context->callback(&data, context->userdata);
            }

            context->buffer_offset = 0;
        }
    }
    
    AudioQueueEnqueueBuffer(context->audio_queue, inBuffer, 0, NULL);
}

int avf_audio_prepare_queue(AVFAudioContext * context)
{
    int i;
    OSStatus result;
    const AudioStreamBasicDescription *strdesc = &context->strdesc;

    result = AudioQueueNewInput(strdesc, avf_audio_input_cb, context, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, &context->audio_queue);
    if (result != noErr)
    {
        log_print(HT_LOG_ERR, "%s, AudioQueueNewInput failed\r\n", __FUNCTION__);
        return 0;
    }

    if (!avf_audio_assign_device(context)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_assign_device failed\r\n", __FUNCTION__);
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
    context->buffer_offset = 0;

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

void * avf_audio_thread(void * argv)
{
    AVFAudioContext * context = (AVFAudioContext *) argv;

    const int rc = avf_audio_prepare_queue(context);
    if (!rc) 
    {
        log_print(HT_LOG_ERR, "%s, avf_audio_prepare_queue failed\r\n", __FUNCTION__);
        return NULL;
    }

    while (context->capture_flag)
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.10, 1);
    }
    
    return NULL;
}

void * avf_audio_init(int device_index, int samplerate, int channels)
{
    AVFAudioContext * context = (AVFAudioContext *)malloc(sizeof(AVFAudioContext));
    if (NULL == context)
    {
        return NULL;
    }

    memset(context, 0, sizeof(AVFAudioContext));

    context->device_index = device_index;
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

    if (!avf_audio_prepare_device(context)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_prepare_device failed!\r\n", __FUNCTION__);
        goto fail;
    }

    context->capture_flag = TRUE;
    context->capture_thread = sys_os_create_thread((void *)avf_audio_thread, (void *)context);
    if (!context->capture_thread) 
    {
        goto fail;
    }

    context->mutex_cb = sys_os_create_mutex();
    
    return context;

fail:

    avf_audio_uninit((void *)context);
    
    return NULL;
}

void avf_audio_uninit(void * ctx)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFAudioContext * context = (AVFAudioContext *)ctx;

    if (context->audio_queue) 
    {
        AudioQueueDispose(context->audio_queue, 1);
    }

    context->capture_flag = FALSE;

    sys_os_wait_thread(&context->capture_thread);

    free(context->audio_buffer);
    free(context->buffer);

    sys_os_destroy_mutex(context->mutex_cb);
    
    free(context);
}

void avf_audio_set_callback(void * ctx, avf_audio_callback cb, void * userdata)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFAudioContext * context = (AVFAudioContext *)ctx;

    sys_os_mutex_enter(context->mutex_cb);
    context->callback = cb;
    context->userdata = userdata;
    sys_os_mutex_leave(context->mutex_cb);
}

int avf_audio_get_samplerate(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFAudioContext * context = (AVFAudioContext *)ctx;

    return context->samplerate;
}

int avf_audio_get_channels(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFAudioContext * context = (AVFAudioContext *)ctx;

    return context->channels;
}

int avf_audio_get_samplefmt(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFAudioContext * context = (AVFAudioContext *)ctx;

    return context->samplefmt;
}

BOOL avf_audio_read(void * ctx, int * samples)
{
    return FALSE;    
}



