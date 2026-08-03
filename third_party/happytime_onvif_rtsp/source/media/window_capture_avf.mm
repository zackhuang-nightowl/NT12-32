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
#include "window_capture_avf.h"
#include <libavutil/pixfmt.h>
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

/***************************************************************************************/

typedef struct
{
    double              framerate;
    int                 width;
    int                 height;
    char                window_title[256];
    int                 window_id;
    id                  avf_delegate;
    
    enum AVPixelFormat  pixel_format;

    avf_window_callback callback;
    void              * userdata;

    SCStream          * stream;
    CMSampleBufferRef   last_sample;
} AVFWindowContext;

/***************************************************************************************/

/** AVFWindowReceiver class - delegate
 */
@interface AVFWindowReceiver : NSObject
{
    AVFWindowContext * _context;
}

- (id)initWithContext:(AVFWindowContext*)context;

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type;

@end

@implementation AVFWindowReceiver

- (id)initWithContext:(AVFWindowContext*)context
{
    if (self = [super init]) 
    {
        _context = context;
    }
    
    return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    CVImageBufferRef imgBufferRef = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (imgBufferRef)
    {
        if (_context->last_sample)
        {
            CFRelease(_context->last_sample);
            _context->last_sample = NULL;
        }
        
        _context->last_sample = sampleBuffer;
        
        CFRetain(_context->last_sample);
    }
    else
    {
       imgBufferRef = CMSampleBufferGetImageBuffer(_context->last_sample);
    }
    
    if (imgBufferRef) 
    {
        CVPixelBufferLockBaseAddress(imgBufferRef, 0);

        uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(imgBufferRef);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imgBufferRef);

        size_t width = CVPixelBufferGetWidth(imgBufferRef);
        size_t height = CVPixelBufferGetHeight(imgBufferRef);

        if (_context->callback)
        {
            avf_window_data data;
            memset(&data, 0, sizeof(data));

            data.data[0] = baseAddress;
            data.linesize[0] = bytesPerRow;
            data.width = width;
            data.height = height;
            data.format = _context->pixel_format;
            
            _context->callback(&data, _context->userdata);
        }

        CVPixelBufferUnlockBaseAddress(imgBufferRef, 0);
    }
}

@end

/***************************************************************************************/

#define WINDOW_NAME     ((NSString *)kCGWindowName)
#define WINDOW_NUMBER   ((NSString *)kCGWindowNumber)
#define WINDOW_LAYER    ((NSString *)kCGWindowLayer)
#define OWNER_NAME      ((NSString *)kCGWindowOwnerName)
#define OWNER_PID       ((NSString *)kCGWindowOwnerPID)

static NSComparator win_info_cmp = ^(NSDictionary *o1, NSDictionary *o2) 
{
    NSComparisonResult res = [o1[OWNER_NAME] compare:o2[OWNER_NAME]];
    if (res != NSOrderedSame)
        return res;

    res = [o1[OWNER_PID] compare:o2[OWNER_PID]];
    if (res != NSOrderedSame)
        return res;

    res = [o1[WINDOW_NAME] compare:o2[WINDOW_NAME]];
    if (res != NSOrderedSame)
        return res;

    return [o1[WINDOW_NUMBER] compare:o2[WINDOW_NUMBER]];
};

NSArray * avf_window_enumerate()
{
    NSArray *arr = (NSArray *)CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);

    [arr autorelease];

    return [arr sortedArrayUsingComparator:win_info_cmp];
}

void avf_window_list()
{
    NSArray *arr = avf_window_enumerate();

    printf("\r\nAvailable window name : \r\n\r\n");

    for (NSDictionary *dict in arr) 
    {
        NSNumber *layer = (NSNumber *)dict[WINDOW_LAYER];
        if (0 != layer.intValue)
        {
            continue;
        }

        NSString *name = (NSString *)dict[WINDOW_NAME];

        printf("%s\r\n", name.UTF8String);
    }
}

void * avf_window_init(char * title, int width, int height, double framerate)
{
    AVFWindowContext * context = (AVFWindowContext *)malloc(sizeof(AVFWindowContext));
    if (NULL == context)
    {
        return NULL;
    }
    
    memset(context, 0, sizeof(AVFWindowContext));

    strncpy(context->window_title, title, sizeof(context->window_title));
    context->width = width;
    context->height = height;
    context->framerate = framerate;

    NSArray *arr = avf_window_enumerate();
    
    for (NSDictionary *dict in arr) 
    {
        NSNumber *layer = (NSNumber *)dict[WINDOW_LAYER];
        if (0 != layer.intValue)
        {
            continue;
        }
        
        NSString *name = (NSString *)dict[WINDOW_NAME];
        NSNumber *wid = (NSNumber *)dict[WINDOW_NUMBER];

        if (strncasecmp(name.UTF8String, title, strlen(title)) == 0)
        {
            context->window_id = wid.intValue;
            break;
        }
    }

    if (0 == context->window_id)
    {
        free(context);
        log_print(HT_LOG_ERR, "%s, not found window, %s\r\n", __FUNCTION__, title);
        return NULL;
    }

    dispatch_group_t group = dispatch_group_create();
    dispatch_group_enter(group);

    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                            onScreenWindowsOnly:YES
                            completionHandler:^(SCShareableContent *shareableContent, NSError *error) 
    {
        if (error) 
        {
        }
        else
        {
            NSArray<SCWindow *> *windows = [shareableContent windows];
            for (SCWindow *window in windows) 
            {
                if (window.windowID != context->window_id)
                {
                    continue;
                }
                
                SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
                if (filter == nil)
                {
                    continue;
                }

                int width, height;

                width = window.frame.size.width;
                height = window.frame.size.height;
                
                SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
                [config setWidth:width];
                [config setHeight:height];
                [config setScalesToFit:YES];
                [config setPreservesAspectRatio:YES];
                [config setCapturesAudio:NO];
                [config setShowsCursor:YES];
                [config setQueueDepth:8];
                [config setPixelFormat:'BGRA'];
                if (context->framerate > 0)
                {
                    [config setMinimumFrameInterval:CMTimeMake(1, context->framerate)];
                }

                context->avf_delegate = [[AVFWindowReceiver alloc] initWithContext:context];

                NSError *err;
                SCStream *stream = NULL;

                stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:context->avf_delegate];
                if (stream)
                {
                    [stream addStreamOutput:context->avf_delegate type:SCStreamOutputTypeScreen sampleHandlerQueue:nil error: &err];
                }

                [filter release];
                [config release];
                
                context->stream = stream;
                context->pixel_format = AV_PIX_FMT_BGRA;

                if (context->width == 0 || context->height == 0)
                {
                    context->width = width;
                    context->height = height;
                }
                
                break;
            }
        }

        dispatch_group_leave(group);
    }];
    
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    dispatch_release(group);

    if (context->stream == NULL)
    {
        free(context);
        log_print(HT_LOG_ERR, "%s, init failed\r\n", __FUNCTION__);
        return NULL;
    }

    return context;
}

void avf_window_uninit(void * ctx)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    if (context->stream)
    {
        dispatch_group_t group = dispatch_group_create();
        dispatch_group_enter(group);
    
        [context->stream stopCaptureWithCompletionHandler:^(NSError *_Nullable error) {
            if (error) {
            }
            dispatch_group_leave(group);
        }];

        dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
        dispatch_release(group);

        [context->stream release];
        context->stream = NULL;
    }
    
    if (context->avf_delegate)
    {
        [context->avf_delegate release];
        context->avf_delegate = NULL;
    }

    if (context->last_sample)
    {
        CFRelease(context->last_sample);
        context->last_sample = NULL;
    }
    
    free(context);
}

void avf_window_set_callback(void * ctx, avf_window_callback cb, void * userdata)
{
    if (NULL == ctx)
    {
        return;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    context->callback = cb;
    context->userdata = userdata;
}

int avf_window_get_width(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    return context->width;
}

int avf_window_get_height(void * ctx)
{
    if (NULL == ctx)
    {
        return 0;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    return context->height;
}

int avf_window_get_pixfmt(void * ctx)
{
    if (NULL == ctx)
    {
        return AV_PIX_FMT_NONE;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    return context->pixel_format;
}

BOOL avf_window_start(void * ctx)
{
    if (NULL == ctx)
    {
        return FALSE;
    }

    AVFWindowContext * context = (AVFWindowContext *)ctx;

    if (context->stream)
    {
        [context->stream startCaptureWithCompletionHandler:^(NSError *_Nullable error) {
            if (error) {
            }
        }];
    }
    
    return TRUE;
}



