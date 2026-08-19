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
#ifndef NO_ONVIF_SERVER
#include "onvif_srv.h"
#include "http_parse.h"
#include "onvif.h"
#endif
#ifndef NO_RTSP_SERVER
#include "rtsp_srv.h"
#include "rtsp_cfg.h"
#endif
#include "getopt.h"
#if __WINDOWS_OS__ && defined(MEDIA_DEVICE)
#include "mfapi.h"
#endif

/*****************************************************************/
#ifndef NO_ONVIF_SERVER
extern "C" ONVIF_CLS    g_onvif_cls;
#endif

#ifndef NO_RTSP_SERVER
extern RTSP_CFG         g_rtsp_cfg;
#endif

/*****************************************************************/

void print_help(const char * cmd)
{
    printf("Usage : %s {option}\r\n\r\n"
#if defined(MEDIA_DEVICE)
           "-l [device|videodevice|audiodevice]\tlist available video or audio capture device\r\n"
           "-l [window]\t\t\t\tlist available application window\r\n"
#endif
           "-h \t\t\t\t\tprint this information\r\n\r\n",
           cmd);
}

#ifndef NO_ONVIF_SERVER
void set_onvif_rtsp_port()
{
#ifndef NO_RTSP_SERVER
    g_onvif_cls.rtsp_port = g_rtsp_cfg.rtsp_port;
#else
    g_onvif_cls.rtsp_port = 554;
#endif    
}
#endif

#if __LINUX_OS__

void sig_handler(int sig)
{
    log_print(HT_LOG_DBG, "%s, sig=%d\r\n", __FUNCTION__, sig);

#ifndef NO_RTSP_SERVER
    rtsp_stop();
#endif
#ifndef NO_ONVIF_SERVER
    onvif_stop();
#endif

    exit(0);
}

#elif __WINDOWS_OS__

BOOL WINAPI sig_handler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:      // Ctrl+C
        log_print(HT_LOG_DBG, "%s, CTRL+C\r\n", __FUNCTION__);
        break;
        
    case CTRL_BREAK_EVENT: // Ctrl+Break
        log_print(HT_LOG_DBG, "%s, CTRL+Break\r\n", __FUNCTION__);
        break;
        
    case CTRL_CLOSE_EVENT: // Closing the consolewindow
        log_print(HT_LOG_DBG, "%s, Closing\r\n", __FUNCTION__);
        break;
    }

#ifndef NO_RTSP_SERVER
    rtsp_stop();
#endif
#ifndef NO_ONVIF_SERVER
    onvif_stop();
#endif

    // Return TRUE if handled this message, further handler functions won't be called.
    // Return FALSE to pass this message to further handlers until default handler calls ExitProcess().
    
     return FALSE;
}

#endif

#if defined(MEDIA_DEVICE)

void list_device(const char * opt)
{
    int vdevice = 0;
    int adevice = 0;
    int window = 0;

    if (strcasecmp(opt, "device") == 0)
    {
        vdevice = 1;
        adevice = 1;
    }
    else if (strcasecmp(opt, "videodevice") == 0)
    {
        vdevice = 1;
    }
    else if (strcasecmp(opt, "audiodevice") == 0)
    {
        adevice = 1;
    }
    else if (strcasecmp(opt, "window") == 0)
    {
        window = 1;
    }
    else
    {
        printf("Invalid options %s\r\n", opt);
        return;
    }
    
    if (vdevice)
    {
#if __WINDOWS_OS__
        CWVideoCapture::listDevice();
#elif defined(IOS)
        CMVideoCapture::listDevice();
#elif __LINUX_OS__
        CLVideoCapture::listDevice();
#endif
    }

    if (adevice)
    {
#if __WINDOWS_OS__
        CWAudioCapture::listDevice();
#elif defined(IOS)
        CMAudioCapture::listDevice();
#elif __LINUX_OS__
        CLAudioCapture::listDevice();
#endif    
    }

    if (window)
    {
#if __WINDOWS_OS__
        CWWindowCapture::listWindow();
#elif defined(IOS)
        CMWindowCapture::listWindow();
#elif __LINUX_OS__
        CLWindowCapture::listWindow();
#endif    
    }
}

#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)

static void av_log_callback(void* ptr, int level, const char* fmt, va_list vl)   
{
    int htlv = HT_LOG_INFO;
    char buff[4096];

    if (av_log_get_level() < level)
    {
        return;
    }

    vsnprintf(buff, sizeof(buff), fmt, vl);

    if (AV_LOG_TRACE == level || AV_LOG_VERBOSE == level)
    {
        htlv = HT_LOG_TRC;
    }
    else if (AV_LOG_DEBUG == level)
    {
        htlv = HT_LOG_DBG;
    }
    else if (AV_LOG_INFO == level)
    {
        htlv = HT_LOG_INFO;
    }
    else if (AV_LOG_WARNING == level)
    {
        htlv = HT_LOG_WARN;
    }
    else if (AV_LOG_ERROR == level)
    {
        htlv = HT_LOG_ERR;
    }
    else if (AV_LOG_FATAL == level || AV_LOG_PANIC == level)
    {
        htlv = HT_LOG_FATAL;
    }

    log_print(htlv, "%s", buff);
}

#endif

#ifndef NO_RTSP_SERVER

BOOL parse_options(int argc, char * argv[])
{
    int ch;
#if defined(MEDIA_DEVICE)
    const char * optstring = "l:h";
#else
    const char * optstring = "h";
#endif

    while ((ch = getopt2(argc, argv, (char *)optstring)) != -1)
    {
        switch (ch)
        {
#if defined(MEDIA_DEVICE)
        case 'l':
            list_device(optarg);
            return FALSE;
#endif

        default:
            print_help(argv[0]);
            return FALSE;
        }
    }

    return TRUE;
}

#endif

int main(int argc, char * argv[])
{
#ifndef NO_RTSP_SERVER

#if __WINDOWS_OS__ && defined(MEDIA_DEVICE)
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
#endif

    if (!parse_options(argc, argv))
    {
        return -1;
    }
#endif

    network_init();

#if __LINUX_OS__

#if !defined(DEBUG) && !defined(_DEBUG)
    daemon_init();
#endif

    // Ignore broken pipes
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sig_handler);
    signal(SIGKILL, sig_handler);
    signal(SIGTERM, sig_handler);
#elif __WINDOWS_OS__
    SetConsoleCtrlHandler(sig_handler, TRUE);
#endif

#if defined(MEDIA_FILE) || defined(MEDIA_DEVICE)
    av_log_set_callback(av_log_callback);
    av_log_set_level(AV_LOG_QUIET);
#endif

#if !defined(NO_ONVIF_SERVER) && !defined(NO_RTSP_SERVER)
    sys_buf_init(MAX_NUM_RUA * 2 + 16);
    http_msg_buf_init(MAX_NUM_RUA * 2 + 16);
#endif

#ifndef NO_ONVIF_SERVER
    if ((access(RUNTIME_CONFIG_FILE, 0)) == 0)  
    {
        onvif_start(RUNTIME_CONFIG_FILE); 
    }
    else
    {
        onvif_start(DEFAULT_CONFIG_FILE); 
    }
#endif

#ifndef NO_RTSP_SERVER
    rtsp_start("rtsp.cfg");

#ifndef NO_ONVIF_SERVER
    printf("\r\nSee the log file %s for additional information.\r\n", ONVIF_LOG_FILE);
#endif
#endif

#ifndef NO_ONVIF_SERVER
    set_onvif_rtsp_port();
#endif

    for (;;)
    {
#if defined(DEBUG) || defined(_DEBUG)
        if (getchar() == 'q')
        {
#ifndef NO_RTSP_SERVER        
            rtsp_stop();
#endif
#ifndef NO_ONVIF_SERVER
            onvif_stop();
#endif            
            break;
        }
#endif

        sleep(1);
    }

#if !defined(NO_ONVIF_SERVER) && !defined(NO_RTSP_SERVER)
    sys_buf_deinit();
    http_msg_buf_deinit();
#endif

    return 0;
}



