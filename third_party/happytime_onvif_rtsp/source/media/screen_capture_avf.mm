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
#include "screen_capture_avf.h"
#include <libavutil/pixfmt.h>
#import <AVFoundation/AVFoundation.h>

/***************************************************************************************/

struct AVFPixelFormatSpec 
{
    enum AVPixelFormat ff_id;
    OSType avf_id;
};

static const struct AVFPixelFormatSpec avf_pixel_formats[] = 
{
    { AV_PIX_FMT_MONOBLACK,    kCVPixelFormatType_1Monochrome },
    { AV_PIX_FMT_RGB555BE,     kCVPixelFormatType_16BE555 },
    { AV_PIX_FMT_RGB555LE,     kCVPixelFormatType_16LE555 },
    { AV_PIX_FMT_RGB565BE,     kCVPixelFormatType_16BE565 },
    { AV_PIX_FMT_RGB565LE,     kCVPixelFormatType_16LE565 },
    { AV_PIX_FMT_RGB24,        kCVPixelFormatType_24RGB },
    { AV_PIX_FMT_BGR24,        kCVPixelFormatType_24BGR },
    { AV_PIX_FMT_0RGB,         kCVPixelFormatType_32ARGB },
    { AV_PIX_FMT_BGR0,         kCVPixelFormatType_32BGRA },
    { AV_PIX_FMT_0BGR,         kCVPixelFormatType_32ABGR },
    { AV_PIX_FMT_RGB0,         kCVPixelFormatType_32RGBA },
    { AV_PIX_FMT_BGR48BE,      kCVPixelFormatType_48RGB },
    { AV_PIX_FMT_UYVY422,      kCVPixelFormatType_422YpCbCr8 },
    { AV_PIX_FMT_YUVA444P,     kCVPixelFormatType_4444YpCbCrA8R },
    { AV_PIX_FMT_YUVA444P16LE, kCVPixelFormatType_4444AYpCbCr16 },
    { AV_PIX_FMT_YUV444P,      kCVPixelFormatType_444YpCbCr8 },
    { AV_PIX_FMT_YUV422P16,    kCVPixelFormatType_422YpCbCr16 },
    { AV_PIX_FMT_YUV422P10,    kCVPixelFormatType_422YpCbCr10 },
    { AV_PIX_FMT_YUV444P10,    kCVPixelFormatType_444YpCbCr10 },
    { AV_PIX_FMT_YUV420P,      kCVPixelFormatType_420YpCbCr8Planar },
    { AV_PIX_FMT_NV12,         kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange },
    { AV_PIX_FMT_YUYV422,      kCVPixelFormatType_422YpCbCr8_yuvs },
    { AV_PIX_FMT_NONE, 0 }
};

typedef struct
{
    int             frames_captured;
    void          * frame_lock;
    id              avf_delegate;

    double          framerate;
    int             width;
    int             height;
    
    int             drop_late_frames;
    int             capture_cursor;
    int             capture_mouse_clicks;

    int             device_index;

    enum AVPixelFormat        pixel_format;

    avf_screen_callback       callback;
    void                     *userdata;

    AVCaptureSession         *capture_session;
    AVCaptureVideoDataOutput *video_output;
    CMSampleBufferRef         current_frame;
} AVFScreenContext;

/***************************************************************************************/

/** FrameReciever class - delegate for AVCaptureSession
 */
@interface AVFScreenReceiver : NSObject
{
    AVFScreenContext * _context;
}

- (id)initWithContext:(AVFScreenContext*)context;

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)videoFrame
         fromConnection:(AVCaptureConnection *)connection;

@end

@implementation AVFScreenReceiver

- (id)initWithContext:(AVFScreenContext*)context
{
    if (self = [super init]) 
    {
        _context = context;
    }
    
    return self;
}

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)videoFrame
         fromConnection:(AVCaptureConnection *)connection
{
    sys_os_mutex_enter(_context->frame_lock);

    if (_context->current_frame != nil) 
    {
        CFRelease(_context->current_frame);
    }

    _context->current_frame = (CMSampleBufferRef)CFRetain(videoFrame);

    sys_os_mutex_leave(_context->frame_lock);

    ++_context->frames_captured;
}

@end


/***************************************************************************************/

int avf_screen_device_nums()
{
    uint32 num_screens = 0;
    
    CGGetActiveDisplayList(0, NULL, &num_screens);
    
    return num_screens;
}

BOOL avf_screen_add_device(AVFScreenContext * context, AVCaptureDevice * video_device)
{
    NSError * error  = nil;
    AVCaptureInput * capture_input = nil;
    struct AVFPixelFormatSpec pxl_fmt_spec;
    NSNumber * pixel_format;
    NSDictionary * capture_dict;
    dispatch_queue_t queue;

    capture_input = (AVCaptureInput*) video_device;
    if (!capture_input) 
    {
        log_print(HT_LOG_ERR, "%s, Failed to create AV capture input device: %s\r\n",
            __FUNCTION__, [[error localizedDescription] UTF8String]);
        return FALSE;
    }

    if ([context->capture_session canAddInput:capture_input]) 
    {
        [context->capture_session addInput:capture_input];
    } 
    else 
    {
        log_print(HT_LOG_ERR, "%s, can't add video input to capture session\r\n", __FUNCTION__);
        return FALSE;
    }

    // Attaching output
    context->video_output = [[AVCaptureVideoDataOutput alloc] init];
    if (!context->video_output) 
    {
        log_print(HT_LOG_ERR, "%s, Failed to init AV video output\r\n", __FUNCTION__);
        return FALSE;
    }

    for (NSNumber *pxl_fmt in [context->video_output availableVideoCVPixelFormatTypes]) 
    {
        pxl_fmt_spec.ff_id = AV_PIX_FMT_NONE;
        
        for (int i = 0; avf_pixel_formats[i].ff_id != AV_PIX_FMT_NONE; i++) 
        {
            if ([pxl_fmt intValue] == avf_pixel_formats[i].avf_id) 
            {
                pxl_fmt_spec = avf_pixel_formats[i];
                break;
            }
        }

        // select first supported pixel format
        if (pxl_fmt_spec.ff_id != AV_PIX_FMT_NONE) 
        {
            break;
        }
    }

    // fail if there is no appropriate pixel format
    if (pxl_fmt_spec.ff_id == AV_PIX_FMT_NONE) 
    {
        log_print(HT_LOG_ERR, "%s, there is no appropriate pixel format\r\n", __FUNCTION__);
        return FALSE;
    }

    context->pixel_format = pxl_fmt_spec.ff_id;
    pixel_format = [NSNumber numberWithUnsignedInt:pxl_fmt_spec.avf_id];
    capture_dict = [NSDictionary dictionaryWithObject:pixel_format forKey:(id)kCVPixelBufferPixelFormatTypeKey];

    [context->video_output setVideoSettings:capture_dict];

    [context->video_output setAlwaysDiscardsLateVideoFrames:context->drop_late_frames];

    context->avf_delegate = [[AVFScreenReceiver alloc] initWithContext:context];

    queue = dispatch_queue_create("screen_queue", NULL);
    [context->video_output setSampleBufferDelegate:context->avf_delegate queue:queue];
    dispatch_release(queue);

    if ([context->capture_session canAddOutput:context->video_output]) 
    {
        [context->capture_session addOutput:context->video_output];
    } 
    else 
    {
        log_print(HT_LOG_ERR, "%s, can't add video output to capture session\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

BOOL avf_screen_get_config(AVFScreenContext * context, AVCaptureDevice * video_device)
{
    BOOL ret = FALSE;
    CVImageBufferRef image_buffer;
    CGSize image_buffer_size;

    // Take stream info from the first frame.
    while (context->frames_captured < 1) 
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, YES);
    }

    sys_os_mutex_enter(context->frame_lock);

    image_buffer = CMSampleBufferGetImageBuffer(context->current_frame);

    if (image_buffer) 
    {
        image_buffer_size = CVImageBufferGetEncodedSize(image_buffer);

        context->width  = (int)image_buffer_size.width;
        context->height = (int)image_buffer_size.height;
        ret = TRUE;
    } 

    CFRelease(context->current_frame);
    context->current_frame = nil;

    sys_os_mutex_leave(context->frame_lock);

    return ret;
}

void * avf_screen_init(int device_index, int width, int height, double framerate)
{
    AVFScreenContext * context = (AVFScreenContext *)malloc(sizeof(AVFScreenContext));
    if (NULL == context)
    {
        return NULL;
    }
    
    memset(context, 0, sizeof(AVFScreenContext));

    context->device_index = device_index;
    context->width = width;
    context->height = height;
    context->framerate = framerate;
    context->frame_lock = sys_os_create_mutex();
    context->capture_cursor = 1;

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVCaptureDevice   *video_device = nil;
    AVCaptureScreenInput* capture_input = nil;
    
    uint32 num_screens = 0;

    CGGetActiveDisplayList(0, NULL, &num_screens);

    CGDirectDisplayID screens[num_screens];
    
    if (context->device_index < 0 || context->device_index >= num_screens)
    {
        log_print(HT_LOG_ERR, "%s, invalid device index %d\r\n", __FUNCTION__, context->device_index);
        goto fail;
    }
    
    CGGetActiveDisplayList(num_screens, screens, &num_screens);
    capture_input = [[[AVCaptureScreenInput alloc] initWithDisplayID:screens[device_index]] autorelease];
    if (capture_input == nil)
    {
        log_print(HT_LOG_ERR, "%s, alloc screen capture input failed %d\r\n", __FUNCTION__);
        goto fail;
    }
    
    if (framerate > 0) 
    {
        capture_input.minFrameDuration = CMTimeMake(1, framerate);
    }

    if (context->capture_cursor) 
    {
        capture_input.capturesCursor = YES;
    } 
    else 
    {
        capture_input.capturesCursor = NO;
    }

    if (context->capture_mouse_clicks) 
    {
        capture_input.capturesMouseClicks = YES;
    } 
    else 
    {
        capture_input.capturesMouseClicks = NO;
    }

    video_device = (AVCaptureDevice*) capture_input;
    
    // Initialize capture session
    context->capture_session = [[AVCaptureSession alloc] init];
    if (context->capture_session == nil)
    {
        log_print(HT_LOG_ERR, "%s, capture session is null!\r\n", __FUNCTION__);
        goto fail;
    }
    
    if (!avf_screen_add_device(context, video_device)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_screen_add_device failed\r\n", __FUNCTION__);
        goto fail;
    }
    
    [context->capture_session startRunning];
    
    if (!avf_screen_get_config(context, video_device)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_screen_get_config failed\r\n", __FUNCTION__);
        goto fail;
    }

    [pool release];
    
    return context;

fail:
    [pool release];

    avf_screen_uninit((void *)context);
    
    return NULL;
}

void avf_screen_uninit(void * ctx)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    if (context->capture_session)
    {
        [context->capture_session stopRunning];
        [context->capture_session release];
        context->capture_session = NULL;
    }
    
    if (context->video_output)
    {
        [context->video_output release];
        context->video_output = NULL;
    }
    
    if (context->avf_delegate)
    {
        [context->avf_delegate release];
        context->avf_delegate = NULL;
    }

    if (context->frame_lock)
    {
        sys_os_destroy_mutex(context->frame_lock);
    }

    if (context->current_frame)
    {
        CFRelease(context->current_frame);
    }
    
    free(context);
}

void avf_screen_set_callback(void * ctx, avf_screen_callback cb, void * userdata)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    context->callback = cb;
    context->userdata = userdata;
}

int avf_screen_get_width(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    return context->width;
}

int avf_screen_get_height(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    return context->height;
}

int avf_screen_get_pixfmt(void * ctx)
{
    if (NULL == ctx)
    {
        return AV_PIX_FMT_NONE;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    return context->pixel_format;
}

BOOL avf_screen_read(void * ctx)
{
    if (NULL == ctx)
    {
        return FALSE;
    }

    AVFScreenContext * context = (AVFScreenContext *)ctx;

    BOOL ret = FALSE;
    CVImageBufferRef image_buffer;
    
    sys_os_mutex_enter(context->frame_lock);

    if (context->current_frame == nil)
    {
        sys_os_mutex_leave(context->frame_lock);
        return FALSE;
    }

    image_buffer = CMSampleBufferGetImageBuffer(context->current_frame);
    if (image_buffer != nil) 
    {
        int status;
        avf_screen_data frame;

        memset(&frame, 0, sizeof(frame));

        status = CVPixelBufferLockBaseAddress(image_buffer, 0);
        if (status != kCVReturnSuccess) 
        {
            sys_os_mutex_leave(context->frame_lock);
            log_print(HT_LOG_ERR, "%s, Could not lock base address: %d\r\n", __FUNCTION__, status);
            return FALSE;
        }

        if (CVPixelBufferIsPlanar(image_buffer)) 
        {
            int i;
            size_t plane_count = CVPixelBufferGetPlaneCount(image_buffer);
            
            for (i = 0; i < plane_count; i++)
            {
                frame.linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(image_buffer, i);
                frame.data[i] = (uint8 *)CVPixelBufferGetBaseAddressOfPlane(image_buffer, i);
            }
        } 
        else 
        {
            frame.linesize[0] = CVPixelBufferGetBytesPerRow(image_buffer);
            frame.data[0] = (uint8 *)CVPixelBufferGetBaseAddress(image_buffer);
        }

        frame.width  = CVPixelBufferGetWidth(image_buffer);
        frame.height = CVPixelBufferGetHeight(image_buffer);
        frame.format = context->pixel_format;

        if (context->callback)
        {
            context->callback(&frame, context->userdata);
        }
        
        ret = TRUE;

        CVPixelBufferUnlockBaseAddress(image_buffer, 0);
    }
    
    CFRelease(context->current_frame);
    context->current_frame = nil;

    sys_os_mutex_leave(context->frame_lock);
    
    return ret;
}



