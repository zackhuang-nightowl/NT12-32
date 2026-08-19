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
#include "video_capture_avf.h"
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

    int             device_index;

    enum AVPixelFormat        pixel_format;

    avf_video_callback        callback;
    void                    * userdata;
    void                    * mutex_cb;

    AVCaptureSession        * capture_session;
    AVCaptureVideoDataOutput* video_output;
    CMSampleBufferRef         current_frame;
} AVFVideoContext;

/***************************************************************************************/

/** FrameReciever class - delegate for AVCaptureSession
 */
@interface AVFVideoReceiver : NSObject
{
    AVFVideoContext * _context;
}

- (id)initWithContext:(AVFVideoContext*)context;

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)videoFrame
         fromConnection:(AVCaptureConnection *)connection;

@end

@implementation AVFVideoReceiver

- (id)initWithContext:(AVFVideoContext*)context
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

int avf_video_device_nums()
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVCaptureDeviceDiscoverySession *avdds = [AVCaptureDeviceDiscoverySession  
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera] 
        mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    NSArray *devices  = avdds.devices;
    
    int count = [devices count];

    [pool release];
    
    return count;
}

void avf_video_device_list()
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVCaptureDeviceDiscoverySession *avdds = [AVCaptureDeviceDiscoverySession  
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera] 
        mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    NSArray *devices  = avdds.devices;
    
    printf("\r\nAvailable video capture device : \r\n\r\n");

    int index = 0;
    
    for (AVCaptureDevice *device in devices) 
    {
        const char * name = [[device localizedName] UTF8String];
        
        printf("index : %d, name : %s\r\n", index++, name);
    }
        
    [pool release];
}

int avf_video_device_get_index(const char * name)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVCaptureDeviceDiscoverySession *avdds = [AVCaptureDeviceDiscoverySession  
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera] 
        mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    NSArray *devices  = avdds.devices;
    
    int i = 0, index = 0;
    
    for (AVCaptureDevice *device in devices) 
    {
        const char * devname = [[device localizedName] UTF8String];
        
        if (strcasecmp(devname, name) == 0)
        {
            index = i;
            break;
        }

        i++;
    }
        
    [pool release];

    return index;
}

BOOL avf_video_config_device(AVFVideoContext * context, AVCaptureDevice * video_device)
{
    NSObject *range = nil;
    NSObject *format = nil;
    NSObject *selected_range = nil;
    NSObject *selected_format = nil;

    int dw = 0, dh = 0;
    
    // try to configure format by formats list
    // might raise an exception if no format list is given
    // (then fallback to default, no configuration)
    @try 
    {
        for (format in [video_device valueForKey:@"formats"]) 
        {
            CMFormatDescriptionRef formatDescription;
            CMVideoDimensions dimensions;

            formatDescription = (CMFormatDescriptionRef) [format performSelector:@selector(formatDescription)];
            dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);

            if (dimensions.width == context->width && dimensions.height == context->height) 
            {
                selected_format = format;

                for (range in [format valueForKey:@"videoSupportedFrameRateRanges"]) 
                {
                    double max_framerate;

                    [[range valueForKey:@"maxFrameRate"] getValue:&max_framerate];
                    if (fabs (context->framerate - max_framerate) < 0.01) 
                    {
                        selected_range = range;
                        break;
                    }
                }

                if (selected_range)
                {
                    break;
                }
            }
            else if (dw * dh < dimensions.width * dimensions.height)
            {
                dw = dimensions.width;
                dh = dimensions.height;

                selected_format = format;

                for (range in [format valueForKey:@"videoSupportedFrameRateRanges"]) 
                {
                    double max_framerate;

                    [[range valueForKey:@"maxFrameRate"] getValue:&max_framerate];
                    if (fabs (context->framerate - max_framerate) < 0.01) 
                    {
                        selected_range = range;
                        break;
                    }
                }
            }
        }

        if (!selected_format) 
        {
            log_print(HT_LOG_ERR, "%s, Selected video size (%dx%d) is not supported by the device\r\n",
                __FUNCTION__, context->width, context->height);
            goto unsupported_format;
        }

        if (!selected_range) 
        {
            log_print(HT_LOG_INFO, "%s, Selected framerate (%f) is not supported by the device\r\n",
                __FUNCTION__, context->framerate);

            // Unsupported fps, use default fps
        }

        if ([video_device lockForConfiguration:NULL] == YES) 
        {
            [video_device setValue:selected_format forKey:@"activeFormat"];

            if (selected_range)
            {
                NSValue *min_frame_duration = [selected_range valueForKey:@"minFrameDuration"];
            
                [video_device setValue:min_frame_duration forKey:@"activeVideoMinFrameDuration"];
                [video_device setValue:min_frame_duration forKey:@"activeVideoMaxFrameDuration"];
            }
        } 
        else 
        {
            log_print(HT_LOG_ERR, "%s, Could not lock device for configuration\r\n", __FUNCTION__);
            return FALSE;
        }
    } 
    @catch(NSException *e) 
    {
        log_print(HT_LOG_WARN, "%s, Configuration of video device failed, falling back to default\r\n", __FUNCTION__);
    }

    return TRUE;

unsupported_format:

    log_print(HT_LOG_ERR, "Supported modes:\n");
    
    for (format in [video_device valueForKey:@"formats"]) 
    {
        CMFormatDescriptionRef formatDescription;
        CMVideoDimensions dimensions;

        formatDescription = (CMFormatDescriptionRef) [format performSelector:@selector(formatDescription)];
        dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);

        for (range in [format valueForKey:@"videoSupportedFrameRateRanges"]) 
        {
            double min_framerate;
            double max_framerate;

            [[range valueForKey:@"minFrameRate"] getValue:&min_framerate];
            [[range valueForKey:@"maxFrameRate"] getValue:&max_framerate];

            log_print(HT_LOG_ERR, "  %dx%d@[%f %f]fps\n", 
                dimensions.width, dimensions.height,
                min_framerate, max_framerate);
        }
    }
    
    return FALSE;
}

BOOL avf_video_add_device(AVFVideoContext * context, AVCaptureDevice * video_device)
{
    NSError * error  = nil;
    AVCaptureInput * capture_input = nil;
    struct AVFPixelFormatSpec pxl_fmt_spec;
    NSNumber * pixel_format;
    NSDictionary * capture_dict;
    dispatch_queue_t queue;

    capture_input = (AVCaptureInput*) [[[AVCaptureDeviceInput alloc] initWithDevice:video_device error:&error] autorelease];
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

    // Configure device framerate and video size
    @try 
    {
        if ((!avf_video_config_device(context, video_device))) 
        {
            return FALSE;
        }
    } 
    @catch (NSException *exception) 
    {
        if (![[exception name] isEqualToString:NSUndefinedKeyException]) 
        {
            log_print(HT_LOG_ERR, "%s, An error occurred: %s\r\n", __FUNCTION__, [exception.reason UTF8String]);
            return FALSE;
        }
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

    context->avf_delegate = [[AVFVideoReceiver alloc] initWithContext:context];

    queue = dispatch_queue_create("video_queue", NULL);
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

BOOL avf_video_get_config(AVFVideoContext * context, AVCaptureDevice * video_device)
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

void * avf_video_init(int device_index, int width, int height, double framerate)
{
    AVFVideoContext * context = (AVFVideoContext *)malloc(sizeof(AVFVideoContext));
    if (NULL == context)
    {
        return NULL;
    }

    memset(context, 0, sizeof(AVFVideoContext));

    context->device_index = device_index;
    context->width = width;
    context->height = height;
    context->framerate = framerate;
    context->frame_lock = sys_os_create_mutex();

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVCaptureDevice   *video_device = nil;
    AVCaptureDeviceDiscoverySession *avdds = [AVCaptureDeviceDiscoverySession  
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera] 
        mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    NSArray *devices  = avdds.devices;

    int count = [devices count];

    if (context->device_index < 0 || context->device_index >= count)
    {
        log_print(HT_LOG_ERR, "%s, invalid device index %d\r\n", __FUNCTION__, context->device_index);
        goto fail;
    }

    video_device = [devices objectAtIndex:context->device_index];
    
    if (video_device == nil)
    {
        log_print(HT_LOG_ERR, "%s, video device is null!\r\n", __FUNCTION__);
        goto fail;
    }

    // Initialize capture session
    context->capture_session = [[AVCaptureSession alloc] init];
    if (context->capture_session == nil)
    {
        log_print(HT_LOG_ERR, "%s, capture session is null!\r\n", __FUNCTION__);
        goto fail;
    }

    if (!avf_video_add_device(context, video_device)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_video_add_device failed\r\n", __FUNCTION__);
        goto fail;
    }

    [context->capture_session startRunning];

    /* Unlock device configuration only after the session is started so it does not reset the capture formats */
    [video_device unlockForConfiguration];

    if (!avf_video_get_config(context, video_device)) 
    {
        log_print(HT_LOG_ERR, "%s, avf_video_get_config failed\r\n", __FUNCTION__);
        goto fail;
    }

    [pool release];

    context->mutex_cb = sys_os_create_mutex();
    
    return context;

fail:
    [pool release];

    avf_video_uninit((void *) context);
    
    return NULL;
}

void avf_video_uninit(void * ctx)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

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

    if (context->mutex_cb)
    {
        sys_os_destroy_mutex(context->mutex_cb);
    }
    
    free(context);
}

void avf_video_set_callback(void * ctx, avf_video_callback cb, void * userdata)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

    sys_os_mutex_enter(context->mutex_cb);
    context->callback = cb;
    context->userdata = userdata;
    sys_os_mutex_leave(context->mutex_cb);
}

int avf_video_get_width(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

    return context->width;
}

int avf_video_get_height(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

    return context->height;
}

int avf_video_get_pixfmt(void * ctx)
{
    if (NULL == ctx)
    {
        return AV_PIX_FMT_NONE;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

    return context->pixel_format;
}

BOOL avf_video_read(void * ctx)
{
    if (NULL == ctx)
    {
        return FALSE;
    }

    AVFVideoContext * context = (AVFVideoContext *)ctx;

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
        avf_video_data frame;

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

        sys_os_mutex_enter(context->mutex_cb);
        if (context->callback)
        {
            context->callback(&frame, context->userdata);
        }
        sys_os_mutex_leave(context->mutex_cb);
        
        ret = TRUE;

        CVPixelBufferUnlockBaseAddress(image_buffer, 0);
    }
    
    CFRelease(context->current_frame);
    context->current_frame = nil;

    sys_os_mutex_leave(context->frame_lock);
    
    return ret;
}



