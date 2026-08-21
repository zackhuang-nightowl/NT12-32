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
#include "sys_buf.h"
#include "rtsp_parse.h"
#include "util.h"
#include "net_util.h"
#include "rtsp_rsua.h"
#include "rtsp_srv.h"
#include "rtp.h"
#include "rtsp_stream.h"
#include "rtsp_media.h"
#include "media_format.h"
#include "rtsp_cfg.h"
#include "rfc_md5.h"
#include "http_auth.h"
#include "rtsp_util.h"
#include "rtsp_timer.h"
#include "rtsp_mc.h"
#include "base64.h"

#if defined(RTSP_OVER_HTTP) || defined(RTSP_OVER_WEBSOCKET)
#include "rtsp_http.h"
#include "http_parse.h"
#include "http_srv.h"
#endif
#ifdef RTSP_BACKCHANNEL
#include "rtsp_srv_backchannel.h"
#endif
#ifdef RTSP_RTCP
#include "rtcp.h"
#endif
#ifdef MEDIA_PUSHER
#include "mpeg4.h"
#endif
#ifdef RTMP_PROXY
#include "log.h"
#endif
#if defined(RTSP_SRTP) || defined(SRTP)
#include "srtp2/srtp.h"
#endif
#ifdef RTSPS
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif

/**************************************************************************************/
RTSP_CLASS  g_rtsp;

/**************************************************************************************/
void rtsp_print_info()
{
    int port = 0;
    char rtsp_url4[164] = {'\0'};
    char rtsp_url6[164] = {'\0'};
    char rtsps_url4[164] = {'\0'};
    char rtsps_url6[164] = {'\0'};

    if (g_rtsp.rtsp.sockaddr.ipv4_flag)
    {
        port = get_sockaddr_port((struct sockaddr *) &g_rtsp.rtsp.sockaddr.ipv4_addr);

        if (port == 554)
        {
            snprintf(rtsp_url4, sizeof(rtsp_url4), "rtsp://%s", g_rtsp.rtsp.ipv4_host);
        }
        else
        {
            snprintf(rtsp_url4, sizeof(rtsp_url4), "rtsp://%s:%d", g_rtsp.rtsp.ipv4_host, port);
        }
    }

    if (g_rtsp.rtsp.sockaddr.ipv6_flag)
    {
        port = get_sockaddr_port((struct sockaddr *) &g_rtsp.rtsp.sockaddr.ipv6_addr);

        if (port == 554)
        {
            snprintf(rtsp_url6, sizeof(rtsp_url6), "rtsp://[%s]", g_rtsp.rtsp.ipv6_host);
        }
        else
        {
            snprintf(rtsp_url6, sizeof(rtsp_url6), "rtsp://[%s]:%d", g_rtsp.rtsp.ipv6_host, port);
        }
    }

#ifdef RTSPS
    if (g_rtsp.rtsps.sockaddr.ipv4_flag)
    {
        port = get_sockaddr_port((struct sockaddr *) &g_rtsp.rtsps.sockaddr.ipv4_addr);

        if (port == 322)
        {
            snprintf(rtsps_url4, sizeof(rtsps_url4), "rtsps://%s", g_rtsp.rtsps.ipv4_host);
        }
        else
        {
            snprintf(rtsps_url4, sizeof(rtsps_url4), "rtsps://%s:%d", g_rtsp.rtsps.ipv4_host, port);
        }
    }

    if (g_rtsp.rtsps.sockaddr.ipv6_flag)
    {
        port = get_sockaddr_port((struct sockaddr *) &g_rtsp.rtsps.sockaddr.ipv6_addr);

        if (port == 322)
        {
            snprintf(rtsps_url6, sizeof(rtsps_url6), "rtsps://[%s]", g_rtsp.rtsps.ipv6_host);
        }
        else
        {
            snprintf(rtsps_url6, sizeof(rtsps_url6), "rtsps://[%s]:%d", g_rtsp.rtsps.ipv6_host, port);
        }
    }
#endif

    printf("Happytime rtsp server %s\r\n", RTSP_VERSION_STRING);
    printf("Play streams from this server using the URL:\r\n");

#ifdef MEDIA_FILE
    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/<filename>\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/<filename>\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/<filename>\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/<filename>\r\n", rtsps_url6);
    }
    printf("where <filename> is a file present in the current directory.\r\n");
#endif

#ifdef MEDIA_DEVICE
    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/screenlive\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/screenlive\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/screenlive\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/screenlive\r\n", rtsps_url6);
    }
    printf("stream from live screen.\r\n");

    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/videodevice\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/videodevice\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/videodevice\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/videodevice\r\n", rtsps_url6);
    }
    printf("stream from camera device.\r\n");

    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/audiodevice\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/audiodevice\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/audiodevice\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/audiodevice\r\n", rtsps_url6);
    }
    printf("stream from audio device.\r\n");

    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/screenlive+audiodevice\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/screenlive+audiodevice\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/screenlive+audiodevice\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/screenlive+audiodevice\r\n", rtsps_url6);
    }
    printf("stream from live screen and audio device\r\n");

    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/videodevice+audiodevice\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/videodevice+audiodevice\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/videodevice+audiodevice\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/videodevice+audiodevice\r\n", rtsps_url6);
    }
    printf("stream from camera device and audio device.\r\n");

    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/window=[window title]\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/window=[window title]\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/window=[window title]\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/window=[window title]\r\n", rtsps_url6);
    }
    printf("stream from application window.\r\n");
#endif

#ifdef MEDIA_PROXY
    MEDIA_PRY * p_proxy = g_proxy;
    if (p_proxy)
    {
        printf("\r\nplay proxy streams from this server using the URL:\r\n");
    }
    
    while (p_proxy)
    {
        if (rtsp_url4[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsp_url4, p_proxy->cfg.key);
        }
        if (rtsps_url4[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsps_url4, p_proxy->cfg.key);
        }
        if (rtsp_url6[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsp_url6, p_proxy->cfg.key);
        }
        if (rtsps_url6[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsps_url6, p_proxy->cfg.key);
        }
        
        p_proxy = p_proxy->next;
    }
#endif

#ifdef MEDIA_PUSHER
    MEDIA_PUSH * p_pusher = g_pusher;
    if (p_pusher)
    {
        printf("\r\nplay pusher streams from this server using the URL:\r\n");
    }

    while (p_pusher)
    {
        if (rtsp_url4[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsp_url4, p_pusher->cfg.key);
        }
        if (rtsps_url4[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsps_url4, p_pusher->cfg.key);
        }
        if (rtsp_url6[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsp_url6, p_pusher->cfg.key);
        }
        if (rtsps_url6[0] != '\0')
        {
            printf("\t%s/%s\r\n", rtsps_url6, p_pusher->cfg.key);
        }
        
        p_pusher = p_pusher->next;
    }
#endif

#ifdef MEDIA_LIVE
    if (rtsp_url4[0] != '\0')
    {
        printf("\t%s/live\r\n", rtsp_url4);
    }
    if (rtsps_url4[0] != '\0')
    {
        printf("\t%s/live\r\n", rtsps_url4);
    }
    if (rtsp_url6[0] != '\0')
    {
        printf("\t%s/live\r\n", rtsp_url6);
    }
    if (rtsps_url6[0] != '\0')
    {
        printf("\t%s/live\r\n", rtsps_url6);
    }
#endif

#ifdef RTSP_OVER_HTTP
    if (g_rtsp_cfg.rtsp_over_http)
    {
#ifdef NO_ONVIF_SERVER
        printf("\r\n(We use port %d for optional RTSP-over-HTTP tunneling, or for HTTP live streaming)\r\n", g_rtsp_cfg.http_port);
#endif    
    }
#ifdef HTTPS    
    if (g_rtsp_cfg.rtsp_over_https)
    {
#ifdef NO_ONVIF_SERVER
        printf("(We use port %d for optional RTSP-over-HTTPS tunneling, or for HTTPS live streaming)\r\n", g_rtsp_cfg.https_port);
#endif    
    }    
#endif
#endif

#ifdef NO_ONVIF_SERVER
    printf("\r\nView the log file %s for additional information.\r\n", RTSP_LOG_FILE);
#endif
}

#ifdef RTMP_PROXY

void rtmp_log_callback(int level, const char *format, va_list vl)
{
    int lvl = HT_LOG_DBG;
    int offset = 0;
    char str[2048] = "";

    offset += vsnprintf(str, sizeof(str)-1, format, vl);

    offset += snprintf(str+offset, sizeof(str)-1-offset, "\r\n");

    if (level == RTMP_LOGCRIT)
    {
        lvl = HT_LOG_FATAL;
    }
    else if (level == RTMP_LOGERROR)
    {
        lvl = HT_LOG_ERR;
    }
    else if (level == RTMP_LOGWARNING)
    {
        lvl = HT_LOG_WARN;
    }
    else if (level == RTMP_LOGINFO)
    {
        lvl = HT_LOG_INFO;
    }
    else if (level == RTMP_LOGDEBUG || level == RTMP_LOGDEBUG2)
    {
        lvl = HT_LOG_DBG;
    }
    else if (level == RTMP_LOGALL)
    {
        lvl = HT_LOG_TRC;
    }

    log_print(lvl, str);
}

void rtsp_set_rtmp_log()
{
    RTMP_LogLevel lvl;
    
    RTMP_LogSetCallback(rtmp_log_callback);

    switch (g_rtsp_cfg.log_level)
    {
    case HT_LOG_TRC:
        lvl = RTMP_LOGINFO; // RTMP_LOGALL;
        break;
        
    case HT_LOG_DBG:
        lvl = RTMP_LOGINFO; // RTMP_LOGDEBUG;
        break;

    case HT_LOG_INFO:
        lvl = RTMP_LOGINFO;
        break;

    case HT_LOG_WARN:
        lvl = RTMP_LOGWARNING;
        break;

    case HT_LOG_ERR:
        lvl = RTMP_LOGERROR;
        break;

    case HT_LOG_FATAL:
        lvl = RTMP_LOGCRIT;
        break;

    default:
        lvl = RTMP_LOGERROR;
        break;
    }

    RTMP_LogSetLevel(lvl);
}

#endif // RTMP_PROXY

BOOL rtsp_init_sockaddr(RTSP_SRV * p_srv, int port)
{
    if (g_rtsp_cfg.serverip[0] == '\0')
    {
        struct sockaddr_in addr4;
        addr4.sin_family = AF_INET;
        
        if (get_default_if_ip((struct sockaddr *) &addr4))
        {
            if (!is_ipv4_local_address(&addr4.sin_addr))
            {
                p_srv->sockaddr.ipv4_flag = 1;
                p_srv->sockaddr.ipv4_addr.sin_family = AF_INET;
                p_srv->sockaddr.ipv4_addr.sin_addr.s_addr = htonl(INADDR_ANY);
                get_sockaddr_ip((struct sockaddr *) &addr4, p_srv->ipv4_host, sizeof(p_srv->ipv4_host));
            }
        }

        if (g_rtsp_cfg.ipv6_enable)
        {
            struct sockaddr_in6 addr6;
            addr6.sin6_family = AF_INET6;
            
            if (get_default_if_ip((struct sockaddr *) &addr6))
            {
                if (!is_ipv6_local_address(&addr6.sin6_addr))
                {
                    p_srv->sockaddr.ipv6_flag = 1;
                    p_srv->sockaddr.ipv6_addr.sin6_family = AF_INET6;
                    p_srv->sockaddr.ipv6_addr.sin6_addr = in6addr_any;
                    get_sockaddr_ip((struct sockaddr *) &addr6, p_srv->ipv6_host, sizeof(p_srv->ipv6_host));
                }
            }
        }
    }
    else
    {
        if (!is_ipv4_address(g_rtsp_cfg.serverip) && !is_ipv6_address(g_rtsp_cfg.serverip)) // domain or hostname
        {
            p_srv->sockaddr.ipv4_flag = 1;
            p_srv->sockaddr.ipv4_addr.sin_family = AF_INET;
            p_srv->sockaddr.ipv4_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            if (g_rtsp_cfg.ipv6_enable)
            {
                p_srv->sockaddr.ipv6_flag = 1;
                p_srv->sockaddr.ipv6_addr.sin6_family = AF_INET6;
                p_srv->sockaddr.ipv6_addr.sin6_addr = in6addr_any;
            }
        }
        else if (!get_address_by_name(g_rtsp_cfg.serverip, HT_PROTOCOL_TCP, &p_srv->sockaddr))
        {
            log_print(HT_LOG_ERR, "%s, invalid ip: %s\r\n", __FUNCTION__, g_rtsp_cfg.serverip);
            return FALSE;
        }

        strncpy(p_srv->ipv4_host, g_rtsp_cfg.serverip, sizeof(p_srv->ipv4_host)-1);
        strncpy(p_srv->ipv6_host, g_rtsp_cfg.serverip, sizeof(p_srv->ipv6_host)-1);
    }

    p_srv->sockaddr.ipv4_addr.sin_port = htons(port);
    p_srv->sockaddr.ipv6_addr.sin6_port = htons(port);

    return TRUE;
}

void rtsp_deinit_sockaddr(RTSP_SRV * p_srv)
{
    if (p_srv->ipv4_fd > 0)
    {
#ifdef EPOLL
        epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_DEL, p_srv->ipv4_fd, NULL);
#endif

        closesocket(p_srv->ipv4_fd);
        p_srv->ipv4_fd = 0;
    }

    if (p_srv->ipv6_fd > 0)
    {
#ifdef EPOLL
        epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_DEL, p_srv->ipv6_fd, NULL);
#endif

        closesocket(p_srv->ipv6_fd);
        p_srv->ipv6_fd = 0;
    }

#ifdef RTSPS
    if (p_srv->ssl_ctx)
    {
        SSL_CTX_free((SSL_CTX *)p_srv->ssl_ctx);
        p_srv->ssl_ctx = NULL;
    }
#endif
}

int rtsp_srv_socket_init(struct sockaddr * addr, socklen_t addrlen)
{
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return 0;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    // On Linux, the default IPV6_V6ONLY is 1, and IPV4 and IPV6 cannot be bound to the same port at the same time
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&yes, sizeof(yes));

    if (bind(fd, addr, addrlen) < 0)
    {
        char ip[64] = {'\0'};
        log_print(HT_LOG_ERR, "%s, %s:%d, bind errno=%d!!!\r\n", 
            __FUNCTION__, get_sockaddr_ip(addr, ip, sizeof(ip)), get_sockaddr_port(addr), errno);
        closesocket(fd);
        return 0;
    }

    if (listen(fd, 10) < 0)
    {
        log_print(HT_LOG_ERR, "%s, listen errno=%d!!!\r\n", __FUNCTION__, errno);
        closesocket(fd);
        return 0;
    }

#ifdef EPOLL
    uint64 e_dat = fd;
    e_dat |= ((uint64)1 << 63);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = e_dat;
    epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_ADD, fd, &event);
#endif

    return fd;
}

BOOL rtsp_srv_net_init(RTSP_SRV * p_srv)
{
    if (p_srv->sockaddr.ipv4_flag)
    {
        p_srv->ipv4_fd = rtsp_srv_socket_init((struct sockaddr *) &p_srv->sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
        if (p_srv->ipv4_fd <= 0)
        {
            p_srv->sockaddr.ipv4_flag = 0;
        }
    }

    if (p_srv->sockaddr.ipv6_flag)
    {
        p_srv->ipv6_fd = rtsp_srv_socket_init((struct sockaddr *) &p_srv->sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
        if (p_srv->ipv6_fd <= 0)
        {
            p_srv->sockaddr.ipv6_flag = 0;
        }
    }

    return (p_srv->ipv4_fd > 0 || p_srv->ipv6_fd > 0) ? TRUE : FALSE;
}

BOOL rtsp_srv_init()
{
    if (!rtsp_init_sockaddr(&g_rtsp.rtsp, g_rtsp_cfg.rtsp_port))
    {
        log_print(HT_LOG_ERR, "%s, init sockaddr failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    if (!rtsp_srv_net_init(&g_rtsp.rtsp))
    {
        log_print(HT_LOG_ERR, "%s, init net failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

#ifdef RTSPS

/* initialize SSL server and create context */
SSL_CTX * rtsp_init_ssl_ctx()
{
    const SSL_METHOD * method;
    SSL_CTX * ctx;

    SSL_library_init();             /* load & register all cryptos, etc. */
    SSL_load_error_strings();       /* load all error messages */
    method = TLS_server_method();   /* create new server-method instance */
    ctx = SSL_CTX_new(method);      /* create new context from method */
    if (ctx == NULL)
    {
        log_print(HT_LOG_ERR, "%s, SSL_CTX_new failed\r\n", __FUNCTION__);
    }
    else
    {
        SSL_CTX_set_options(ctx, SSL_OP_ALL);
        SSL_CTX_set_default_verify_paths(ctx);
    }
    
    return ctx;
}

/* LoadCertificates - load from files */
BOOL rtsp_load_certificates(SSL_CTX * ctx, const char * cert_file, const char * key_file)
{
    /* set the local certificate from CertFile */
    if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, SSL_CTX_use_certificate_chain_file failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    /* set the private key from KeyFile (may be the same as CertFile) */
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, SSL_CTX_use_PrivateKey_file failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    /* verify private key */
    if (!SSL_CTX_check_private_key(ctx))
    {
        log_print(HT_LOG_ERR, "%s, Private key does not match the public certificate\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

BOOL rtsps_srv_init()
{
    if (!rtsp_init_sockaddr(&g_rtsp.rtsps, g_rtsp_cfg.rtsps_port))
    {
        log_print(HT_LOG_ERR, "%s, init sockaddr failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    /* initialize SSL */
    g_rtsp.rtsps.ssl_ctx = rtsp_init_ssl_ctx();   
    if (NULL == g_rtsp.rtsps.ssl_ctx)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_init_ssl_ctx failed\r\n", __FUNCTION__);
        return FALSE;
    }

    /* load certs */
    if (!rtsp_load_certificates((SSL_CTX *)g_rtsp.rtsps.ssl_ctx, g_rtsp_cfg.rtsps_cert, g_rtsp_cfg.rtsps_key))
    {
        log_print(HT_LOG_ERR, "%s, rtsp_load_certificates failed\r\n", __FUNCTION__);
        return FALSE;
    }

    if (!rtsp_srv_net_init(&g_rtsp.rtsps))
    {
        log_print(HT_LOG_ERR, "%s, init net failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

int rtsp_srv_ssl_tx(RSSUA * p_rua, const char * p_data, int len)
{
    int res = 0;
    int offset = 0;
    int ret = 0;

    if (NULL == p_rua || NULL == p_data || 0 == len)
    {
        return -1;
    }
    
    while (offset < len)
    {
        sys_os_mutex_enter(p_rua->ssl_mutex);
        
        if (p_rua->ssl)
        {
            res = SSL_write((SSL*)p_rua->ssl, p_data+offset, len-offset);
            ret = SSL_get_error((SSL*)p_rua->ssl, res);
        }
        else
        {
            ret = -1;
        }
        
        sys_os_mutex_leave(p_rua->ssl_mutex);
        
        if (ret == SSL_ERROR_NONE) 
        {
            if (res > 0) 
            {
                offset += res;
            }
        } 
        else if (ret == SSL_ERROR_WANT_READ) 
        {
            usleep(10*1000);
            continue;
        } 
        else if (ret == SSL_ERROR_WANT_WRITE) 
        {
            continue;
        } 
        else 
        {
            return -1;
        }
    } 
    
    return offset;
}

#endif // RTSPS

/**************************************************************************************
 *
 * rtsp server start
 *
 *************************************************************************************/
BOOL rtsp_start1()
{
    int bufs;
    uint32 i;

    memset(&g_rtsp, 0, sizeof(g_rtsp));

    if (g_rtsp_cfg.rtsp_port <= 0 || g_rtsp_cfg.rtsp_port > 65535)
    {
        g_rtsp_cfg.rtsp_port = 554;
    }

#ifdef RTSPS
    if (g_rtsp_cfg.rtsps_port <= 0 || g_rtsp_cfg.rtsps_port > 65535)
    {
        g_rtsp_cfg.rtsps_port = 322;
    }
#endif

    if (g_rtsp_cfg.udp_base_port == 0)
    {
        g_rtsp_cfg.udp_base_port = 22000;
    }

    snprintf(g_rtsp.srv_ver, sizeof(g_rtsp.srv_ver), "happytime rtsp server %s", RTSP_VERSION_STRING);

    rsua_proxy_init();

    rmc_proxy_init();

    g_rtsp.msg_queue = hqueue_create(MAX_NUM_RUA * 4, sizeof(RTSPMSG), HQ_GET_WAIT);
    if (g_rtsp.msg_queue == NULL)
    {
        log_print(HT_LOG_ERR, "%s, create rtsp task queue failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

#ifdef EPOLL
    g_rtsp.ep_event_num = MAX_NUM_RUA + NET_IF_NUM + 8;
    
    g_rtsp.ep_fd = epoll_create(g_rtsp.ep_event_num);
    if (g_rtsp.ep_fd < 0)
    {
        log_print(HT_LOG_ERR, "%s, epoll_create failed\r\n", __FUNCTION__);
        return FALSE;
    }

    g_rtsp.ep_events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * g_rtsp.ep_event_num);
    if (g_rtsp.ep_events == NULL)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
#endif

    if (!rtsp_srv_init())
    {
        log_print(HT_LOG_ERR, "%s, init rtsp server failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

#ifdef RTSPS
    if (g_rtsp_cfg.rtsps_enable)
    {
        if (!rtsps_srv_init())
        {
            log_print(HT_LOG_ERR, "%s, init rtsps server failed!!!\r\n", __FUNCTION__);
            return FALSE;
        }
    }
#endif

    bufs = MAX_NUM_RUA * 2;

#ifdef MEDIA_PROXY
    bufs += media_get_proxy_nums() * 2;
#endif

#ifdef MEDIA_PUSHER
    bufs += media_get_pusher_nums() * 2;
#endif

#ifdef NO_ONVIF_SERVER
    sys_buf_init(bufs);
#endif
    rtsp_parse_buf_init(bufs);

#ifdef NO_ONVIF_SERVER
#if defined(MEDIA_PROXY) || defined(RTSP_OVER_HTTP)
    http_msg_buf_init(bufs);
#endif
#endif

#ifdef MEDIA_PROXY
    media_init_proxies();
#endif

#ifdef MEDIA_PUSHER
    media_init_pushers();
#endif

#ifdef RTSP_OVER_HTTP
    if (g_rtsp_cfg.rtsp_over_http)
    {
        rtsp_http_init();
    }
#ifdef HTTPS
    if (g_rtsp_cfg.rtsp_over_https)
    {
        rtsp_https_init();
    }
#endif
#endif

#ifdef RTMP_PROXY
    rtsp_set_rtmp_log();
#endif

#if defined(RTSP_SRTP) || defined(SRTP)
    if (srtp_init() != srtp_err_status_ok) 
    {
        log_print(HT_LOG_ERR, "libsrtp init failed\r\n");
    }
#endif

    rtsp_print_info();

    srand((uint32)time(NULL));

    g_rtsp.sys_run_flag = 1;
    g_rtsp.tid_main = sys_os_create_thread((void *)rtsp_task, NULL);

#ifdef EPOLL
    g_rtsp.num_pkt_rx = 1;
#else
    g_rtsp.num_pkt_rx = MAX_NUM_RUA / RTSP_SOCKETS + 1;
#endif

    g_rtsp.data_rx_flag = 1;
    g_rtsp.tid_pkt_rx = (pthread_t *) malloc(sizeof(pthread_t) * g_rtsp.num_pkt_rx);
    
    for (i = 0; i < g_rtsp.num_pkt_rx; i++)
    {
        int * idx = (int *)malloc(sizeof(int));
        *idx = i;
        g_rtsp.tid_pkt_rx[i] = sys_os_create_thread((void *)rtsp_rx_thread, idx);
    }

    rtsp_timer_init();

    g_rtsp.session_timeout = 60;
    g_rtsp.sys_init_flag = 1;

    return TRUE;
}

BOOL rtsp_start(const char * config)
{
    const char * filename = NULL;
    
    memset(&g_rtsp_cfg, 0, sizeof(g_rtsp_cfg));

    if (NULL == config || config[0] == '\0')
    {
        filename = RTSP_DEF_CFG;
    }
    else
    {
        filename = config;
    }

#ifdef NO_ONVIF_SERVER
    log_init(RTSP_LOG_FILE);
#endif

    if (!rtsp_read_config(filename))
    {
        printf("Read config file %s failed!!!\r\n", filename);
        log_print(HT_LOG_ERR, "%s, read config file %s failed\r\n", 
            __FUNCTION__, filename);
        return FALSE;
    }

#ifdef NO_ONVIF_SERVER
    if (g_rtsp_cfg.log_enable)
    {
        log_set_level(g_rtsp_cfg.log_level);
    }
    else
    {
        log_close();
    }
#endif

    return rtsp_start1();
}

void rtsp_stop()
{
    uint32 i;

    rtsp_timer_deinit();
    
    g_rtsp.data_rx_flag = 0;

    if (g_rtsp.tid_pkt_rx)
    {
        for (i = 0; i < g_rtsp.num_pkt_rx; i++)
        {
            sys_os_wait_thread(&g_rtsp.tid_pkt_rx[i]);
        }

        g_rtsp.num_pkt_rx = 0;
        free(g_rtsp.tid_pkt_rx);
        g_rtsp.tid_pkt_rx = NULL;
    }

    RTSPMSG stm;
    memset(&stm, 0, sizeof(stm));

    stm.msg_src = RTSP_EXIT;

    hqueue_put(g_rtsp.msg_queue, (char *)&stm);

    g_rtsp.sys_run_flag = 0;

    sys_os_wait_thread(&g_rtsp.tid_main);

    rtsp_deinit_sockaddr(&g_rtsp.rtsp);

#ifdef RTSPS
    rtsp_deinit_sockaddr(&g_rtsp.rtsps);
#endif

    hqueue_delete(g_rtsp.msg_queue);
    g_rtsp.msg_queue = NULL;

    for (i = 0; i < MAX_NUM_RUA; i++)
    {
        RSSUA * p_rua = rsua_get_by_index(i);

        if (p_rua && p_rua->used_flag)
        {
            rtsp_close_rua(p_rua);
        }
    }

    rsua_proxy_deinit();

    rmc_proxy_deinit();

    rtsp_free_outputs(&g_rtsp_cfg.output);

#ifdef MEDIA_PROXY
    media_free_proxies();
#endif

#ifdef MEDIA_PUSHER
    media_free_pushers();
#endif

#ifdef EPOLL
    if (g_rtsp.ep_fd)
    {
        close(g_rtsp.ep_fd);
        g_rtsp.ep_fd = 0;        
    }

    if (g_rtsp.ep_events)
    {
        free(g_rtsp.ep_events);
        g_rtsp.ep_events = NULL;
    }
#endif

#ifdef RTSP_OVER_HTTP
    if (g_rtsp_cfg.rtsp_over_http)
    {
        rtsp_http_deinit();
    }
#ifdef HTTPS
    if (g_rtsp_cfg.rtsp_over_https)
    {
        rtsp_https_deinit();
    }
#endif
#endif

    rtsp_parse_buf_deinit();

#ifdef NO_ONVIF_SERVER
    sys_buf_deinit();

#if defined(MEDIA_PROXY) || defined(RTSP_OVER_HTTP)
    http_msg_buf_deinit();
#endif

    log_close();
#endif

#if defined(RTSP_SRTP) || defined(SRTP)
    srtp_shutdown();
#endif
}

void rtsp_listen_rx(RTSP_SRV * p_srv, int sfd, int family)
{
    int cfd = 0;
    char ip[64] = {'\0'};
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    RSSUA * p_rua = NULL;
#ifdef RTSPS
    SSL * ssl = NULL;
#endif
#ifdef EPOLL
    uint64 e_dat;
    struct epoll_event event;
#endif

    if (family == AF_INET)
    {
        addr = (struct sockaddr *) & addr4;
        addrlen = sizeof(struct sockaddr_in);
    }
    else if (family == AF_INET6)
    {
        addr = (struct sockaddr *) & addr6;
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        return;
    }
    
    cfd = accept(sfd, addr, &addrlen);
    if (cfd < 0)
    {
        log_print(HT_LOG_ERR, "%s, accept ret error[%d]\r\n", __FUNCTION__, errno);
        return;
    }

    int on = 1;
    int len = 2 * 1024 * 1024;

    if (setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt TCP_NODELAY error[%d]\r\n", __FUNCTION__, errno);
    }

    if (setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error[%d]\r\n", __FUNCTION__, errno);
    }

    if (setsockopt(cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)) < 0)
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error[%d]\r\n", __FUNCTION__, errno);
    }

#ifdef RTSPS
    if (p_srv->ssl_ctx)
    {
        ssl = SSL_new((SSL_CTX *)p_srv->ssl_ctx);
        if (NULL == ssl)
        {
            log_print(HT_LOG_ERR, "%s, SSL_new failed, %s\r\n", 
                __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
            goto cleanup;
        }
        
        SSL_set_fd(ssl, (int)cfd);
        
        if (-1 == SSL_accept(ssl))
        {
            log_print(HT_LOG_ERR, "%s, SSL_accept failed, %s\r\n", 
                __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
            goto cleanup;
        }
    }
#endif

    p_rua = rsua_get_idle_rua();
    if (p_rua == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rua_get_idle_rua return NULL, close cfd(%d)!!!\r\n", 
            __FUNCTION__, cfd);
        goto cleanup;
    }

    p_rua->fd = cfd;

    if (family == AF_INET)
    {
        p_rua->sockaddr.ipv4_flag = 1;
        memcpy(&p_rua->sockaddr.ipv4_addr, addr, addrlen);
    }
    else if (family == AF_INET6)
    {
        p_rua->sockaddr.ipv6_flag = 1;
        memcpy(&p_rua->sockaddr.ipv6_addr, addr, addrlen);
    }

    p_rua->last_rx_time = sys_os_get_uptime();

#ifdef RTSPS
    if (p_srv->ssl_ctx)
    {
        p_rua->rtsps = 1;
        p_rua->ssl = ssl;
        p_rua->ssl_mutex = sys_os_create_mutex();
    }
#endif

#ifdef EPOLL
    e_dat = rsua_get_index(p_rua);
    e_dat = e_dat << 32;
    e_dat = e_dat | cfd;

    event.events = EPOLLIN;
    event.data.u64 = e_dat;
    epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_ADD, cfd, &event);
#endif

    log_print(HT_LOG_INFO, "new user over tcp from [%s:%u]\r\n", 
        get_sockaddr_ip(addr, ip, sizeof(ip)), get_sockaddr_port(addr));

    rsua_set_online_rua(p_rua);

    return;

cleanup:

    closesocket(cfd);

#ifdef RTSPS
    if (ssl)
    {
        SSL_free(ssl);
    }
#endif
}

/***********************************************************************
 *
 * rtsp packet receive thread
 *
************************************************************************/
void * rtsp_rx_thread(void * argv)
{
    RSSUA * p_cln;
    int thread_idx = *(int *)argv;

    free(argv);
    
    log_print(HT_LOG_DBG, "%s, start, idx=%d\r\n", __FUNCTION__, thread_idx);

    while (g_rtsp.data_rx_flag)
    {
#ifdef EPOLL

        int i, fd, nfds;

        nfds = epoll_wait(g_rtsp.ep_fd, g_rtsp.ep_events, g_rtsp.ep_event_num, 1000);

        for (i=0; i<nfds; i++)
        {
            if (g_rtsp.ep_events[i].events & EPOLLIN)
            {
                fd = (int)(g_rtsp.ep_events[i].data.u64);
                if ((g_rtsp.ep_events[i].data.u64 & ((uint64)1 << 63)) != 0)
                {
                    if (fd == g_rtsp.rtsp.ipv4_fd)
                    {
                        rtsp_listen_rx(&g_rtsp.rtsp, fd, AF_INET);
                    }
                    else if (fd == g_rtsp.rtsp.ipv6_fd)
                    {
                        rtsp_listen_rx(&g_rtsp.rtsp, fd, AF_INET6);
                    }
#ifdef RTSPS                    
                    else if (fd == g_rtsp.rtsps.ipv4_fd)
                    {
                        rtsp_listen_rx(&g_rtsp.rtsps, fd, AF_INET);
                    }
                    else if (fd == g_rtsp.rtsps.ipv6_fd)
                    {
                        rtsp_listen_rx(&g_rtsp.rtsps, fd, AF_INET6);
                    }
#endif
                }
                else
                {
                    uint32 u_index = g_rtsp.ep_events[i].data.u64 >> 32;
                    p_cln = rsua_get_by_index(u_index);
                    if (p_cln && p_cln->fd > 0 && p_cln->fd == fd)
                    {
                        if (!rtsp_srv_rx(p_cln))
                        {
                            rtsp_stop_rua(p_cln);
                        }
                        else
                        {
                            p_cln->last_rx_time = sys_os_get_uptime();
                        }
                    }
                }
            }
        }

#else

        fd_set fdr;
        int max_fd = 0;
        uint32 i, s, e;

        FD_ZERO(&fdr);

        if (thread_idx == 0)
        {
            if (g_rtsp.rtsp.ipv4_fd > 0)
            {
                FD_SET(g_rtsp.rtsp.ipv4_fd, &fdr);
                max_fd = (int)g_rtsp.rtsp.ipv4_fd;
            }

            if (g_rtsp.rtsp.ipv6_fd > 0)
            {
                FD_SET(g_rtsp.rtsp.ipv6_fd, &fdr);
                max_fd = (int)(((int)g_rtsp.rtsp.ipv6_fd > max_fd) ? g_rtsp.rtsp.ipv6_fd : max_fd);
            }

#ifdef RTSPS
            if (g_rtsp.rtsps.ipv4_fd > 0)
            {
                FD_SET(g_rtsp.rtsps.ipv4_fd, &fdr);
                max_fd = (int)(((int)g_rtsp.rtsps.ipv4_fd > max_fd) ? g_rtsp.rtsps.ipv4_fd : max_fd);
            }

            if (g_rtsp.rtsps.ipv6_fd > 0)
            {
                FD_SET(g_rtsp.rtsps.ipv6_fd, &fdr);
                max_fd = (int)(((int)g_rtsp.rtsps.ipv6_fd > max_fd) ? g_rtsp.rtsps.ipv6_fd : max_fd);
            }
#endif
        }

        s = thread_idx * RTSP_SOCKETS;
        e = (thread_idx + 1) * RTSP_SOCKETS;

        e = e > MAX_NUM_RUA ? MAX_NUM_RUA : e;

        for (i = s; i < e; i++)
        {
            p_cln = rsua_get_by_index(i);
            if (p_cln && p_cln->fd > 0)
            {
                FD_SET(p_cln->fd, &fdr);
                max_fd = ((int)p_cln->fd > max_fd)? p_cln->fd : max_fd;
            }
        }

        if (max_fd == 0)
        {
            usleep(100*1000);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sret = select(max_fd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            log_print(HT_LOG_ERR, "%s, select err[%s], sret[%d]!!!\r\n", 
                __FUNCTION__, sys_os_get_socket_error(), sret);

            usleep(100*1000);
            continue;
        }

        if (g_rtsp.rtsp.ipv4_fd > 0 && FD_ISSET(g_rtsp.rtsp.ipv4_fd, &fdr))
        {
            rtsp_listen_rx(&g_rtsp.rtsp, g_rtsp.rtsp.ipv4_fd, AF_INET);
        }
        
        if (g_rtsp.rtsp.ipv6_fd > 0 && FD_ISSET(g_rtsp.rtsp.ipv6_fd, &fdr))
        {
            rtsp_listen_rx(&g_rtsp.rtsp, g_rtsp.rtsp.ipv6_fd, AF_INET6);
        }

#ifdef RTSPS
        if (g_rtsp.rtsps.ipv4_fd > 0 && FD_ISSET(g_rtsp.rtsps.ipv4_fd, &fdr))
        {
            rtsp_listen_rx(&g_rtsp.rtsps, g_rtsp.rtsps.ipv4_fd, AF_INET);
        }
        
        if (g_rtsp.rtsps.ipv6_fd > 0 && FD_ISSET(g_rtsp.rtsps.ipv6_fd, &fdr))
        {
            rtsp_listen_rx(&g_rtsp.rtsps, g_rtsp.rtsps.ipv6_fd, AF_INET6);
        }
#endif

        for (i = s; i < e; i++)
        {
            p_cln = rsua_get_by_index(i);

            if (p_cln && p_cln->fd > 0 && FD_ISSET(p_cln->fd, &fdr))
            {
                if (!rtsp_srv_rx(p_cln))
                {
                    rtsp_stop_rua(p_cln);
                }
                else
                {
                    p_cln->last_rx_time = sys_os_get_uptime();
                }
            }
        }

#endif
    }
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);

    return NULL;
}

/***********************************************************************
 *
 * rtsp main task
 *
***********************************************************************/
void * rtsp_task(void * argv)
{
    RTSPMSG stm;

    while (g_rtsp.sys_run_flag)
    {
        if (hqueue_get(g_rtsp.msg_queue, (char *)&stm))
        {
            RSSUA * p_rua = rsua_get_by_index(stm.msg_dua);
            switch (stm.msg_src)
            {
            case RTSP_MSG_SRC:
                rtsp_rx_msg(p_rua, (HRTSP_MSG *)stm.msg_buf);
                if (stm.msg_buf) rtsp_free_msg((HRTSP_MSG *)stm.msg_buf);
                break;

            case RTSP_DEL_UA_SRC:
                rtsp_close_rua(p_rua);
                break;

            case RTSP_TIMER_SRC:
                rtsp_timer();
                break;

            case RTSP_EXIT:
                goto EXIT;
            }
        }
    }

EXIT:

    return NULL;
}

int rtsp_rx_msg(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (p_rua == NULL || rx_msg == NULL)
    {
        return -1;
    }

    if (p_rua->used_flag == 0)
    {
        return -1;
    }

    return rtsp_server_state(p_rua, rx_msg);
}

int rtsp_msg_parser(RSSUA * p_rua)
{
    int rtsp_pkt_len = rtsp_pkt_find_end(p_rua->rcv_buf, p_rua->rcv_dlen);
    if (rtsp_pkt_len == 0)
    {
        return RTSP_PARSE_MOREDATA;
    }

    HRTSP_MSG * rx_msg = rtsp_get_msg_buf();
    if (rx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_get_msg_buf return null!!!\r\n", __FUNCTION__);
        return RTSP_PARSE_FAIL;
    }

    memcpy(rx_msg->msg_buf, p_rua->rcv_buf, rtsp_pkt_len);
    rx_msg->msg_buf[rtsp_pkt_len] = '\0';

    log_print(HT_LOG_DBG, "%s\r\n", rx_msg->msg_buf);

    int parse_len = rtsp_msg_parse_part1(rx_msg->msg_buf, rtsp_pkt_len, rx_msg);
    if (parse_len != rtsp_pkt_len)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_msg_parse_part1=%d, rtsp_pkt_len=%d!!!\r\n", 
            __FUNCTION__, parse_len, rtsp_pkt_len);
        rtsp_free_msg(rx_msg);
        return RTSP_PARSE_FAIL;
    }

    if (rx_msg->ctx_len > 0)
    {
        if (p_rua->rcv_dlen < (parse_len + rx_msg->ctx_len))
        {
            rtsp_free_msg(rx_msg);
            return RTSP_PARSE_MOREDATA;
        }

        memcpy(rx_msg->msg_buf+rtsp_pkt_len, p_rua->rcv_buf+rtsp_pkt_len, rx_msg->ctx_len);
        rx_msg->msg_buf[rtsp_pkt_len + rx_msg->ctx_len] = '\0';

        log_print(HT_LOG_DBG, "%s\r\n\r\n", rx_msg->msg_buf+rtsp_pkt_len);

        int sdp_parse_len = rtsp_msg_parse_part2(rx_msg->msg_buf+parse_len, rx_msg->ctx_len, rx_msg);
        if (sdp_parse_len != rx_msg->ctx_len)
        {
            log_print(HT_LOG_ERR, "%s, rtsp_msg_parse_part2 = %d, ctx_len = %d!!!\r\n", 
                __FUNCTION__, sdp_parse_len, rx_msg->ctx_len);
            rtsp_free_msg(rx_msg);
            return RTSP_PARSE_FAIL;
        }
        parse_len += sdp_parse_len;
    }

    if (parse_len < p_rua->rcv_dlen)
    {
        while (p_rua->rcv_buf[parse_len] == ' ' || 
            p_rua->rcv_buf[parse_len] == '\r' || 
            p_rua->rcv_buf[parse_len] == '\n')
        {
            parse_len++;
        }

        memmove(p_rua->rcv_buf, p_rua->rcv_buf + parse_len, p_rua->rcv_dlen - parse_len);
        p_rua->rcv_dlen -= parse_len;
        p_rua->rcv_buf[p_rua->rcv_dlen] = '\0';
    }
    else
    {
        p_rua->rcv_dlen = 0;
    } 

    RTSPMSG msg;
    memset(&msg, 0, sizeof(RTSPMSG));
    msg.msg_src = RTSP_MSG_SRC;
    msg.msg_dua = rsua_get_index(p_rua);
    msg.msg_buf = (char *)rx_msg;

    if (hqueue_put(g_rtsp.msg_queue, (char *)&msg) == FALSE)
    {
        rtsp_free_msg(rx_msg);
        log_print(HT_LOG_ERR, "%s, send rx msg to main task failed!!!\r\n", __FUNCTION__);
    }

    return RTSP_PARSE_SUCC;
}

void rtsp_data_rx(RSSUA * p_rua, uint8 * data, int rlen)
{
    uint8 * p_rtp = data + 4;
    int32 rtp_len = rlen - 4;

#ifdef RTSP_SRTP
    if (p_rua->srtp_enable || p_rua->media_info.is_pusher)
    {
        uint8 interleaved = net_read_uint8(data+1);

        for (int i = 0; i < AV_MAX_CHS; i++)
        {
            if (!p_rua->channels[i].setup)
            {
                continue;
            }
                
            if (interleaved == p_rua->channels[i].interleaved)
            {
                if (p_rua->channels[i].srtp)
                {
                    if (srtp_unprotect(p_rua->channels[i].srtp, p_rtp, &rtp_len) != srtp_err_status_ok)
                    {
                        return;
                    }

                    rlen = rtp_len + 4;
                }
                
                break;
            }
            else if (interleaved == p_rua->channels[i].interleaved+1)
            {
                return;
            }
        }
    }
#endif

    if (rtp_len >= 2 && RTP_PT_IS_RTCP(p_rtp[1]))
    {
        return;
    }

#ifdef RTSP_BACKCHANNEL
    if (p_rua->backchannel)
    {
        rtsp_bc_tcp_data_rx(p_rua, data, rlen);
    }
#endif

#ifdef MEDIA_PUSHER
    if (p_rua->media_info.is_pusher)
    {
        rtsp_tcp_data_rx(p_rua, data, rlen);
    }
#endif
}

BOOL rtsp_data_parser(RSSUA * p_rua)
{
rx_point:

    if (rtsp_is_rtsp_msg(p_rua->rcv_buf))
    {
        int ret = rtsp_msg_parser(p_rua);
        if (RTSP_PARSE_FAIL == ret)
        {
            log_print(HT_LOG_ERR, "%s, rtsp_msg_parser failed\r\n", __FUNCTION__);
            return FALSE;
        }
        else if (RTSP_PARSE_MOREDATA == ret)
        {
            return TRUE;
        }

        if (p_rua->rcv_dlen >= 16)
        {
            goto rx_point;
        }
    }
    else
    {
        uint8 magic = net_read_uint8((uint8*)p_rua->rcv_buf);
        if (magic != 0x24)
        {
            log_print(HT_LOG_WARN, "%s, magic[0x%02X]!!!\r\n", __FUNCTION__, magic);

            // Try to recover from wrong data

            for (int i = 1; i <= p_rua->rcv_dlen - 4; i++)
            {
                if (p_rua->rcv_buf[i] == 0x24 &&
                    (p_rua->rcv_buf[i+1] == p_rua->channels[AV_VIDEO_CH].interleaved ||
                     p_rua->rcv_buf[i+1] == p_rua->channels[AV_AUDIO_CH].interleaved))
                {
                    memmove(p_rua->rcv_buf, p_rua->rcv_buf+i, p_rua->rcv_dlen - i);
                    p_rua->rcv_dlen -= i;
                    goto rx_point;
                }
            }

            p_rua->rcv_dlen = 0;
            return TRUE;
        }

        uint16 rtp_len = net_read_uint16((uint8*)p_rua->rcv_buf+2);
        if (rtp_len > (p_rua->rcv_dlen - 4))
        {
            if (p_rua->rtp_rcv_buf)
            {
                free(p_rua->rtp_rcv_buf);
            }

            p_rua->rtp_rcv_buf = (char *)malloc(rtp_len+4);
            if (p_rua->rtp_rcv_buf == NULL) 
            {
                return FALSE;
            }

            memcpy(p_rua->rtp_rcv_buf, p_rua->rcv_buf, p_rua->rcv_dlen);
            p_rua->rtp_rcv_len = p_rua->rcv_dlen;
            p_rua->rtp_t_len = rtp_len + 4;

            p_rua->rcv_dlen = 0;

            return TRUE;
        }

        rtsp_data_rx(p_rua, (uint8*)p_rua->rcv_buf, rtp_len+4);

        p_rua->rcv_dlen -= rtp_len + 4;
        if (p_rua->rcv_dlen > 0)
        {
            memmove(p_rua->rcv_buf, p_rua->rcv_buf+rtp_len+4, p_rua->rcv_dlen);
        }

        if (p_rua->rcv_dlen >= 16)
        {
            goto rx_point;
        }
    }

    return TRUE;
}

BOOL rtsp_tcp_rx(RSSUA * p_rua)
{
    int rlen = 0;
    
    if (p_rua->fd <= 0)
    {
        return FALSE;
    }

    if (p_rua->rtp_rcv_buf == NULL || p_rua->rtp_t_len == 0)
    {
#ifdef RTSPS
        if (p_rua->rtsps)
        {
            sys_os_mutex_enter(p_rua->ssl_mutex);
            if (p_rua->ssl)
            {
                rlen = SSL_read((SSL *)p_rua->ssl, p_rua->rcv_buf+p_rua->rcv_dlen, sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->ssl_mutex);
        }
        else
#endif
        {
            rlen = recv(p_rua->fd, p_rua->rcv_buf+p_rua->rcv_dlen, sizeof(p_rua->rcv_buf)-4-p_rua->rcv_dlen, 0);
        }
        
        if (rlen <= 0)
        {
            log_print(HT_LOG_WARN, "%s, ret=%d, rcv_dlen=%d, err=%s\r\n", 
                __FUNCTION__, rlen, p_rua->rcv_dlen, sys_os_get_socket_error());
            return FALSE;
        }

        p_rua->rcv_dlen += rlen;

        if (p_rua->rcv_dlen < 16)
        {
            return TRUE;
        }
    }
    else
    {
#ifdef RTSPS
        if (p_rua->rtsps)
        {
            sys_os_mutex_enter(p_rua->ssl_mutex);
            if (p_rua->ssl)
            {
                rlen = SSL_read((SSL *)p_rua->ssl, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, p_rua->rtp_t_len-p_rua->rtp_rcv_len);
            }
            else
            {
                rlen = -1;
            }
            sys_os_mutex_leave(p_rua->ssl_mutex);
        }
        else
#endif
        {
            rlen = recv(p_rua->fd, p_rua->rtp_rcv_buf+p_rua->rtp_rcv_len, p_rua->rtp_t_len-p_rua->rtp_rcv_len, 0);
        }
        
        if (rlen <= 0) // recv error, connection maybe disconn?
        {
            log_print(HT_LOG_WARN, "%s, ret=%d, err=%s\r\n", 
                __FUNCTION__, rlen, sys_os_get_socket_error());    
            return FALSE;
        }

        p_rua->rtp_rcv_len += rlen;

        if (p_rua->rtp_rcv_len == p_rua->rtp_t_len)
        {
            rtsp_data_rx(p_rua, (uint8*)p_rua->rtp_rcv_buf, p_rua->rtp_rcv_len);

            free(p_rua->rtp_rcv_buf);
            p_rua->rtp_rcv_buf = NULL;
            p_rua->rtp_rcv_len = 0;
            p_rua->rtp_t_len = 0;
        }

        return TRUE;
    }

    return rtsp_data_parser(p_rua);
}

BOOL rtsp_srv_rx(RSSUA * p_rua)
{
    BOOL ret = TRUE;
    
#ifdef RTSPS
    if (p_rua->rtsps)
    {
        do 
        {
            if (!rtsp_tcp_rx(p_rua))
            {
                ret = FALSE;
                break;
            }

            sys_os_mutex_enter(p_rua->ssl_mutex);
            if (p_rua->ssl && SSL_pending((SSL*)p_rua->ssl) > 0)
            {
                sys_os_mutex_leave(p_rua->ssl_mutex);
                continue;
            }
            sys_os_mutex_leave(p_rua->ssl_mutex);
            
            break;
            
        } while (1);
    }
    else
#endif
    {
        ret = rtsp_tcp_rx(p_rua);
    }
    
    return ret;
}

BOOL rtsp_options_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = rsua_build_options_response(p_rua);
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua,tx_msg);

    return TRUE;
}

void rtsp_add_video_cap(RSSUA * p_rua, uint8 pt, const char * desc)
{
    p_rua->channels[AV_VIDEO_CH].cap[0] = pt;
    strcpy(p_rua->channels[AV_VIDEO_CH].cap_desc[0], desc);

    rtsp_media_get_video_sdp_line(p_rua, pt, 
        p_rua->channels[AV_VIDEO_CH].cap_desc[1], 
        sizeof(p_rua->channels[AV_VIDEO_CH].cap_desc[1])-1);
}

void rtsp_setup_video(RSSUA * p_rua)
{
    p_rua->channels[AV_VIDEO_CH].cap_count = 1;

    if (p_rua->media_info.v_info.codec == VIDEO_CODEC_H264)
    {
        rtsp_add_video_cap(p_rua, 96, "a=rtpmap:96 H264/90000");
    }
    else if (p_rua->media_info.v_info.codec == VIDEO_CODEC_H265)
    {
        rtsp_add_video_cap(p_rua, 96, "a=rtpmap:96 H265/90000");
    }
    else if (p_rua->media_info.v_info.codec == VIDEO_CODEC_MP4)
    {
        rtsp_add_video_cap(p_rua, 96, "a=rtpmap:96 MP4V-ES/90000");
    }
    else if (p_rua->media_info.v_info.codec == VIDEO_CODEC_JPEG)
    {
        rtsp_add_video_cap(p_rua, 26, "a=rtpmap:26 JPEG/90000");

        // rfc2435, when using RTP to transmit JPEG packets, 
        //  the maximum width and height are 2048.
        //  If the width or height exceeds 2048, 
        //  use a=x-dimensions to pass the video size

        if (p_rua->media_info.v_info.width > 2048 || 
            p_rua->media_info.v_info.height > 2048)
        {
            for (int i = 0; i < MAX_AVN; i++)
            {
                if (p_rua->channels[AV_VIDEO_CH].cap_desc[i][0] == '\0')
                {
                    snprintf(p_rua->channels[AV_VIDEO_CH].cap_desc[i], 
                        sizeof(p_rua->channels[AV_VIDEO_CH].cap_desc[i]),
                        "a=x-dimensions:%d,%d", 
                        p_rua->media_info.v_info.width,
                        p_rua->media_info.v_info.height);

                    break;
                }
            }
        }
    }

    p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_ts = 0;
    p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_pt = p_rua->channels[AV_VIDEO_CH].cap[0];
    p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_ssrc = rand();
    p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_cnt = rand() & 0xFFFF;
}

void rtsp_setup_audio(RSSUA * p_rua, int ch, AUDIO_INFO * p_info, uint8 rtp_pt, const char * direct)
{
    int desc_idx = 0;

    p_rua->channels[ch].cap_count = 1;

    if (p_info->codec == AUDIO_CODEC_G711A)
    {
        p_rua->channels[ch].cap[0] = 8;

        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d PCMA/%d/%d", 
            p_rua->channels[ch].cap[0],
            p_info->samplerate,
            p_info->channels);
        desc_idx++;
    }
    else if (p_info->codec == AUDIO_CODEC_G711U)
    {
        p_rua->channels[ch].cap[0] = 0;

        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d PCMU/%d/%d", 
            p_rua->channels[ch].cap[0],
            p_info->samplerate,
            p_info->channels);
        desc_idx++;
    }
    else if (p_info->codec == AUDIO_CODEC_G726)
    {
        int bitpersample;

        if (0 == p_info->bitrate)
        {
            bitpersample = 16;
        }
        else
        {
            bitpersample = p_info->bitrate / 8 * 8;
        }

        p_rua->channels[ch].cap[0] = rtp_pt;

        // G726 8000 1 16kbit/s
        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d G726-%d/%d/%d",
            p_rua->channels[ch].cap[0],
            bitpersample,
            p_info->samplerate,
            p_info->channels);
        desc_idx++;
    }
    else if (p_info->codec == AUDIO_CODEC_AAC)
    {
        p_rua->channels[ch].cap[0] = rtp_pt;

        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d MPEG4-GENERIC/%d/%d", 
            p_rua->channels[ch].cap[0],
            p_info->samplerate,
            p_info->channels);
        desc_idx++;

        rtsp_media_get_audio_sdp_line(p_rua, rtp_pt, 
            p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx])-1);
        desc_idx++;
    }
    else if (p_info->codec == AUDIO_CODEC_G722)
    {
        p_rua->channels[ch].cap[0] = 9;

        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d G722/%d/%d",
            p_rua->channels[ch].cap[0],
            p_info->samplerate,
            p_info->channels);
        desc_idx++;
    }
    else if (p_info->codec == AUDIO_CODEC_OPUS)
    {
        p_rua->channels[ch].cap[0] = rtp_pt;

        snprintf(p_rua->channels[ch].cap_desc[desc_idx], 
            sizeof(p_rua->channels[ch].cap_desc[desc_idx]),
            "a=rtpmap:%d opus/%d/%d",
            p_rua->channels[ch].cap[0],
            p_info->samplerate,
            p_info->channels);
        desc_idx++;
    }

    snprintf(p_rua->channels[ch].cap_desc[desc_idx],
        sizeof(p_rua->channels[ch].cap_desc[desc_idx]), 
        "a=%s",
        direct);
    desc_idx++;

    p_rua->channels[ch].rtp_info.rtp_ts = 0;
    p_rua->channels[ch].rtp_info.rtp_pt = p_rua->channels[ch].cap[0];
    p_rua->channels[ch].rtp_info.rtp_ssrc = rand();
    p_rua->channels[ch].rtp_info.rtp_cnt = rand() & 0xFFFF;
}

#ifdef RTSP_METADATA

void rtsp_setup_metadata(RSSUA * p_rua)
{
    p_rua->media_info.has_metadata = 1;

    strcpy(p_rua->channels[AV_METADATA_CH].ctl, "metadata");
    p_rua->channels[AV_METADATA_CH].interleaved = (AV_METADATA_CH + 1) * 2;

    p_rua->channels[AV_METADATA_CH].cap_count = 1;
    p_rua->channels[AV_METADATA_CH].cap[0] = 98;
    snprintf(p_rua->channels[AV_METADATA_CH].cap_desc[0], 
        sizeof(p_rua->channels[AV_METADATA_CH].cap_desc[0]),
        "a=rtpmap:98 vnd.onvif.metadata/90000");

    p_rua->channels[AV_METADATA_CH].rtp_info.rtp_ts = 0;
    p_rua->channels[AV_METADATA_CH].rtp_info.rtp_pt = p_rua->channels[AV_METADATA_CH].cap[0];
    p_rua->channels[AV_METADATA_CH].rtp_info.rtp_ssrc = rand();
    p_rua->channels[AV_METADATA_CH].rtp_info.rtp_cnt = rand() & 0xFFFF;
}

#endif // #ifdef RTSP_METADATA

#ifdef RTSP_BACKCHANNEL

void rtsp_setup_backchannel(RSSUA * p_rua)
{
    strcpy(p_rua->channels[AV_BACK_CH].ctl, "audioback");
    p_rua->channels[AV_BACK_CH].interleaved = (AV_BACK_CH + 1) * 2;

    rtsp_cfg_get_backchannel_info(&p_rua->media_info.bc_info);

    rtsp_bc_parse_url_parameters(p_rua);
    
    if (p_rua->media_info.bc_info.codec == AUDIO_CODEC_NONE)
    {
        p_rua->media_info.bc_info.codec = AUDIO_CODEC_G711U;
    }

    if (p_rua->media_info.bc_info.samplerate == 0)
    {
        p_rua->media_info.bc_info.samplerate = 8000;
    }

    if (p_rua->media_info.bc_info.channels == 0)
    {
        p_rua->media_info.bc_info.channels = 1;
    }

    rtsp_setup_audio(p_rua, AV_BACK_CH, &p_rua->media_info.bc_info, 99, "sendonly");
}

#endif // #ifdef RTSP_BACKCHANNEL

#ifdef HTTP_NOTIFY

BOOL rtsp_http_notify(RSSUA * p_rua, HTTP_NTF_EVENT evt)
{
    char addr[64] = {'\0'};
    char call[24] = {'\0'};
    char params[1024] = {'\0'};

    if (HTTP_NTF_EVE_CONNECT == evt && g_rtsp_cfg.http_notify.on_connect[0] != '\0')
    {
        strcpy(call, "connect");
    }
    else if (HTTP_NTF_EVE_PLAY == evt && g_rtsp_cfg.http_notify.on_play[0] != '\0')
    {
        strcpy(call, "play");
    }
    else if (HTTP_NTF_EVE_PUBLISH == evt && g_rtsp_cfg.http_notify.on_publish[0] != '\0')
    {
        strcpy(call, "publish");
    }
    else if (HTTP_NTF_EVE_DONE == evt && g_rtsp_cfg.http_notify.on_done[0] != '\0')
    {
        strcpy(call, "done");
    }
    else
    {
        return TRUE;
    }

    if (p_rua->sockaddr.ipv6_flag)
    {
        get_sockaddr_ip((struct sockaddr *)&p_rua->sockaddr.ipv6_addr, addr, sizeof(addr));
    }
    else
    {
        get_sockaddr_ip((struct sockaddr *)&p_rua->sockaddr.ipv4_addr, addr, sizeof(addr));
    }
    
    snprintf(params, sizeof(params)-1, "protocol=rtsp&&call=%s&addr=%s&clientid=%d&url=%s&name=%s", 
        call, addr, rsua_get_index(p_rua), p_rua->uri, p_rua->media_info.filename);
    
    if (HTTP_NOTIFY_OK != http_notify_handler(evt, &g_rtsp_cfg.http_notify, params))
    {
        return FALSE;
    }

    return TRUE;
}

#endif // HTTP_NOTIFY

#ifdef RTSP_SRTP

BOOL rtsp_setup_srtp(RSSUA * p_rua)
{
    int i, j;
    
    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].ctl[0] == '\0')
        {
            continue;
        }
        
        memset(&p_rua->channels[i].srtp_policy, 0, sizeof(p_rua->channels[i].srtp_policy));

        srtp_crypto_policy_set_rtp_default(&p_rua->channels[i].srtp_policy.rtp); 
        srtp_crypto_policy_set_rtcp_default(&p_rua->channels[i].srtp_policy.rtcp);

        for (j = 0; j < SRTP_KEY_LENGTH + SRTP_SALT_LEN; j++)
        {
            p_rua->channels[i].crypto_key[j] = (uint8) rand();
        }

        if (i == AV_BACK_CH)
        {
            p_rua->channels[i].srtp_policy.ssrc.type = ssrc_any_inbound;
        }
        else
        {
            p_rua->channels[i].srtp_policy.ssrc.type = ssrc_any_outbound;
        }
        
        p_rua->channels[i].srtp_policy.key = p_rua->channels[i].crypto_key;
        p_rua->channels[i].srtp_policy.next = NULL;

        if (srtp_create(&p_rua->channels[i].srtp, &p_rua->channels[i].srtp_policy) != srtp_err_status_ok) 
        {
            log_print(HT_LOG_ERR, "Error creating SRTP session\r\n");
        }
    }

    return TRUE;
}

#endif // RTSP_SRTP

void rtsp_setup_multicast(RSSUA * p_rua)
{
    RMCUA * p_mcua = rtsp_mc_add_ref(p_rua);
    if (p_mcua)
    {
        RSSUA * pmc = rsua_get_by_index(p_mcua->mcuaidx);
        if (NULL == pmc)
        {
            return;
        }
        
        p_rua->channels[AV_VIDEO_CH].rtp_info.rtp_ssrc = pmc->channels[AV_VIDEO_CH].rtp_info.rtp_ssrc;
        p_rua->channels[AV_AUDIO_CH].rtp_info.rtp_ssrc = pmc->channels[AV_AUDIO_CH].rtp_info.rtp_ssrc;
        
        p_rua->channels[AV_VIDEO_CH].l_port = pmc->channels[AV_VIDEO_CH].l_port;
        p_rua->channels[AV_AUDIO_CH].l_port = pmc->channels[AV_AUDIO_CH].l_port;
        strcpy(p_rua->channels[AV_VIDEO_CH].destination, pmc->channels[AV_VIDEO_CH].destination);
        strcpy(p_rua->channels[AV_AUDIO_CH].destination, pmc->channels[AV_AUDIO_CH].destination);

#ifdef RTSP_METADATA
        p_rua->channels[AV_METADATA_CH].rtp_info.rtp_ssrc = pmc->channels[AV_METADATA_CH].rtp_info.rtp_ssrc;
        p_rua->channels[AV_METADATA_CH].l_port = pmc->channels[AV_METADATA_CH].l_port;
        strcpy(p_rua->channels[AV_METADATA_CH].destination, pmc->channels[AV_METADATA_CH].destination);
#endif

#ifdef RTSP_BACKCHANNEL
        p_rua->channels[AV_BACK_CH].rtp_info.rtp_ssrc = pmc->channels[AV_BACK_CH].rtp_info.rtp_ssrc;
        p_rua->channels[AV_BACK_CH].l_port = pmc->channels[AV_BACK_CH].l_port;
        strcpy(p_rua->channels[AV_BACK_CH].destination, pmc->channels[AV_BACK_CH].destination);
#endif
    }
    else
    {
        uint16 port = g_rtsp_cfg.udp_base_port + rsua_get_index(p_rua) * 8;
        
        rtsp_mc_add_src(p_rua);
        
        p_rua->channels[AV_VIDEO_CH].l_port = port;
        p_rua->channels[AV_AUDIO_CH].l_port = port + 2;
#ifdef RTSP_METADATA
        p_rua->channels[AV_METADATA_CH].l_port = port + 4;
#endif
#ifdef RTSP_BACKCHANNEL
        p_rua->channels[AV_BACK_CH].l_port = port + 6;
#endif

        if (p_rua->sockaddr.ipv6_flag)
        {
            snprintf(p_rua->channels[AV_VIDEO_CH].destination, 
                sizeof(p_rua->channels[AV_VIDEO_CH].destination),
                "ff16:0:0:0:0:%x:%x:%x", rand()%0xFFFF, rand()%0xFFFF, rand()%0xFFFF);
            snprintf(p_rua->channels[AV_AUDIO_CH].destination, 
                sizeof(p_rua->channels[AV_AUDIO_CH].destination),
                "ff16:0:0:0:0:%x:%x:%x", rand()%0xFFFF, rand()%0xFFFF, rand()%0xFFFF);

#ifdef RTSP_METADATA
            snprintf(p_rua->channels[AV_METADATA_CH].destination, 
                sizeof(p_rua->channels[AV_METADATA_CH].destination),
                "ff16:0:0:0:0:%x:%x:%x", rand()%0xFFFF, rand()%0xFFFF, rand()%0xFFFF);
#endif

#ifdef RTSP_BACKCHANNEL
            snprintf(p_rua->channels[AV_BACK_CH].destination, 
                sizeof(p_rua->channels[AV_BACK_CH].destination),
                "ff16:0:0:0:0:%x:%x:%x", rand()%0xFFFF, rand()%0xFFFF, rand()%0xFFFF);
#endif
        }
        else
        {
            snprintf(p_rua->channels[AV_VIDEO_CH].destination, 
                sizeof(p_rua->channels[AV_VIDEO_CH].destination),
                "232.%u.%u.%u", rand()%256, rand()%256, rand()%256);
            snprintf(p_rua->channels[AV_AUDIO_CH].destination, 
                sizeof(p_rua->channels[AV_AUDIO_CH].destination),
                "232.%u.%u.%u", rand()%256, rand()%256, rand()%256);

#ifdef RTSP_METADATA
            snprintf(p_rua->channels[AV_METADATA_CH].destination, 
                sizeof(p_rua->channels[AV_METADATA_CH].destination),
                "232.%u.%u.%u", rand()%256, rand()%256, rand()%256);
#endif

#ifdef RTSP_BACKCHANNEL
            snprintf(p_rua->channels[AV_BACK_CH].destination, 
                sizeof(p_rua->channels[AV_BACK_CH].destination),
                "232.%u.%u.%u", rand()%256, rand()%256, rand()%256);
#endif
        }
    }
}

BOOL rtsp_describe_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    char buf[32];

    if (rtsp_get_headline_string(rx_msg, "Accept", buf, sizeof(buf)) == FALSE)
    {
        return FALSE;
    }

    if (strcasecmp(buf, "application/sdp") != 0)
    {
        return FALSE;
    }

#ifdef RTSP_SRTP
    char * p = strchr(p_rua->uri, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->uri, '&');
    }
    
    if (p)
    {
        p++; // skip  '?' or '&' char
        if (get_name_value_pair(p, strlen(p), "srtp", buf, sizeof(buf)))
        {
            p_rua->srtp_enable = atoi(buf);
        }
    }
#endif
   
#ifdef HTTP_NOTIFY
    if (!rtsp_http_notify(p_rua, HTTP_NTF_EVE_CONNECT))
    {
        return FALSE;
    }
#endif

    if (rtsp_media_init(p_rua) == FALSE)
    {
        return FALSE;
    }

    p_rua->backchannel = rtsp_is_line_exist(rx_msg, "Require", "www.onvif.org/ver20/backchannel");

    if (p_rua->backchannel)
    {
#ifndef RTSP_BACKCHANNEL
        HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "551 Option not supported");
        if (tx_msg)
        {
            rsua_send_free_rtsp_msg(p_rua,tx_msg);
        }

        return FALSE;
#endif
    }

    snprintf(p_rua->sid, sizeof(p_rua->sid), "%u", rand());
    snprintf(p_rua->cbase, sizeof(p_rua->cbase), "%s", p_rua->uri);

    strcpy(p_rua->channels[AV_VIDEO_CH].ctl, "realvideo");
    strcpy(p_rua->channels[AV_AUDIO_CH].ctl, "realaudio");

    p_rua->channels[AV_VIDEO_CH].interleaved = (AV_VIDEO_CH + 1) * 2;
    p_rua->channels[AV_AUDIO_CH].interleaved = (AV_AUDIO_CH + 1) * 2;

    // setup SDP
    if (p_rua->media_info.has_video)
    {
        rtsp_setup_video(p_rua);
    }

    if (p_rua->media_info.has_audio)
    {
        rtsp_setup_audio(p_rua, AV_AUDIO_CH, &p_rua->media_info.a_info, 97, "recvonly");
    }

#ifdef RTSP_METADATA
    if (g_rtsp_cfg.metadata)
    {
        rtsp_setup_metadata(p_rua);
    }
#endif

#ifdef RTSP_BACKCHANNEL
    if (p_rua->backchannel)
    {
        rtsp_setup_backchannel(p_rua);
    }
#endif

#ifdef RTSP_SRTP
    if (p_rua->srtp_enable)
    {
        rtsp_setup_srtp(p_rua);
    }
#endif

    if (g_rtsp_cfg.multicast && 0 == p_rua->rtp_unicast)
    {
        rtsp_setup_multicast(p_rua);
    }

    HRTSP_MSG * tx_msg = rsua_build_descibe_response(p_rua);
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    p_rua->state = RSS_DESCRIBE;

    return TRUE;
}

BOOL rtsp_setup_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    int i, av_t = -1;
    char buf[256];
    
    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].ctl[0] != '\0' && strstr(p_rua->uri, p_rua->channels[i].ctl))
        {
            av_t = i;
            p_rua->channels[i].setup = 1;

            break;
        }
    }

    if (av_t < 0) 
    {
        return FALSE;
    }

    if (rtsp_get_headline_string(rx_msg, "Transport", buf, sizeof(buf)) == FALSE)
    {
        return FALSE;
    }

    if (rsua_get_transport_info(p_rua, buf, av_t) == FALSE)
    {
        return FALSE;
    }

    p_rua->replay = rtsp_is_line_exist(rx_msg, "Require", "onvif-replay");

#ifdef RTSP_REPLAY
    if (p_rua->media_info.is_replay)
    {
        if (!p_rua->replay)
        {
            // Require: onvif-replay must appear in playback SETUP request
            return FALSE;
        }
    }
#else
    if (p_rua->replay)
    {
        HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "551 Option not supported");
        if (tx_msg)
        {
            rsua_send_free_rtsp_msg(p_rua, tx_msg);
        }

        return FALSE;
    }
#endif
    
    if (p_rua->rtp_tcp == 0)
    {
        struct sockaddr * addr;
        RTSP_SRV * p_srv = &g_rtsp.rtsp;

#ifdef RTSPS
        if (p_rua->rtsps)
        {
            p_srv = &g_rtsp.rtsps;
        }
#endif

        if (p_rua->sockaddr.ipv4_flag)
        {
            addr = (struct sockaddr *) &p_srv->sockaddr.ipv4_addr;
        }
        else if (p_rua->sockaddr.ipv6_flag)
        {
            addr = (struct sockaddr *) &p_srv->sockaddr.ipv6_addr;
        }
        else
        {
            return FALSE;
        }
        
        if (p_rua->rtp_unicast)
        {
            p_rua->channels[av_t].l_port = g_rtsp_cfg.udp_base_port + rsua_get_index(p_rua) * 8 + av_t * 2;

            p_rua->channels[av_t].udp_fd = rsua_init_udp_connection(addr, p_rua->channels[av_t].l_port);
#ifdef RTSP_RTCP
            p_rua->channels[av_t].rtcp_fd = rsua_init_udp_connection(addr, p_rua->channels[av_t].l_port+1);
#endif
        }
        else
        {
            if (p_rua->channels[av_t].r_port == 0)
            {
                p_rua->channels[av_t].r_port = p_rua->channels[av_t].l_port;
            }
            else if (p_rua->channels[av_t].r_port != p_rua->channels[av_t].l_port)
            {
                p_rua->channels[av_t].l_port = p_rua->channels[av_t].r_port;
            }

            p_rua->channels[av_t].udp_fd = rsua_init_mc_connection(addr, p_rua->channels[av_t].l_port, p_rua->channels[av_t].destination);
#ifdef RTSP_RTCP
            p_rua->channels[av_t].rtcp_fd = rsua_init_mc_connection(addr, p_rua->channels[av_t].l_port+1, p_rua->channels[av_t].destination);
#endif
        }

        if (p_rua->channels[av_t].udp_fd <= 0)
        {
            HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "461 Unsupported Transport");
            if (tx_msg == NULL)
            {
                return FALSE;
            }

            rsua_send_free_rtsp_msg(p_rua, tx_msg);
            return TRUE;
        }
    }

    HRTSP_MSG * tx_msg = rsua_build_setup_response(p_rua, av_t);
    if (tx_msg == NULL)
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    p_rua->state = RSS_INIT_V;

    return TRUE;
}

BOOL rtsp_play_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    char range[256];

    p_rua->replay = rtsp_is_line_exist(rx_msg, "Require", "onvif-replay");

#ifdef RTSP_REPLAY
    if (p_rua->media_info.is_replay)
    {
        if (!p_rua->replay)
        {
            // Require: onvif-replay must appear in playback PLAY request
            return FALSE;
        }
    }
#else
    if (p_rua->replay)
    {
        HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "551 Option not supported");
        if (tx_msg)
        {
            rsua_send_free_rtsp_msg(p_rua, tx_msg);
        }
        
        return FALSE;
    }
#endif

    if (p_rua->media_info.is_file)
    {
        p_rua->scale_flag = rtsp_get_scale_info(rx_msg, &p_rua->scale);
    }
    
#ifdef RTSP_REPLAY
    rtsp_get_rate_control(rx_msg, &p_rua->rate_control);
    rtsp_get_immediate(rx_msg, &p_rua->immediate);
    rtsp_get_frame_info(rx_msg, &p_rua->frame, &p_rua->frame_interval);

    // If start a new play command immediately, update the cseq
    p_rua->channels[AV_VIDEO_CH].rep_hdr.seq = (uint8) (p_rua->cseq & 0xFF);
    p_rua->channels[AV_AUDIO_CH].rep_hdr.seq = (uint8) (p_rua->cseq & 0xFF);
#ifdef RTSP_METADATA    
    p_rua->channels[AV_METADATA_CH].rep_hdr.seq = (uint8) (p_rua->cseq & 0xFF);
#endif

    if (p_rua->immediate)
    {
        p_rua->channels[AV_VIDEO_CH].rep_hdr.d = 1;
        p_rua->channels[AV_AUDIO_CH].rep_hdr.d = 1;
#ifdef RTSP_METADATA        
        p_rua->channels[AV_METADATA_CH].rep_hdr.d = 1;
#endif        
    }
#endif

    if (rtsp_get_headline_string(rx_msg, "Range", range, sizeof(range)))
    {
        p_rua->play_range = rsua_get_play_range_info(p_rua, range);
    }
    else
    {
        p_rua->play_range = 0;
    }

#ifdef MEDIA_FILE
    if (p_rua->play_range)
    {
        p_rua->media_info.seek_flag = 1;
    }
#endif

#ifdef RTSP_BACKCHANNEL
    if (p_rua->backchannel)
    {
        if (!rtsp_bc_init_audio(p_rua))
        {
            log_print(HT_LOG_ERR, "%s, rtsp_bc_init_audio failed\r\n", __FUNCTION__);
            return FALSE;
        }

        if (!p_rua->rtp_tcp && !p_rua->rtp_rx)
        {
            p_rua->rtp_rx = 1;
            p_rua->tid_udp_rx = sys_os_create_thread((void *)rtsp_bc_udp_rx_thread, p_rua);
        }
    }
#endif

#ifdef RTSP_RTCP
    if (!p_rua->rtp_tcp && !p_rua->tid_rtcp_rx)
    {
        p_rua->rtcp_rx = 1;
        p_rua->tid_rtcp_rx = sys_os_create_thread((void *)rtsp_rtcp_rx_thread, p_rua);
    }
#endif

    HRTSP_MSG * tx_msg = rsua_build_play_response(p_rua);
    if (tx_msg == NULL)
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    if (p_rua->mc_ref)
    {
        if (p_rua->rtp_tcp || p_rua->rtp_unicast) 
        {
            // non multicast, remove the multicast reference
            rtsp_mc_del_ref(p_rua);
        }
    }
    else if (p_rua->mc_src)
    {
        if (p_rua->rtp_tcp || p_rua->rtp_unicast)
        {
            // non multicast source, remove the multicast ua
            rtsp_mc_del_src(p_rua);
        }
    }
    
    if (p_rua->state == RSS_PAUSE)
    {
        rtsp_restart_stream_tx(p_rua);

        p_rua->state = RSS_PLAYING;
    }
    else if (p_rua->state != RSS_PLAYING)
    {
        if (0 == p_rua->mc_ref)
        {
            rtsp_start_stream_tx(p_rua);
        }
        else
        {
            // reference the other multicast source stream
        }

        p_rua->state = RSS_PLAYING;

#ifdef HTTP_NOTIFY
        if (!rtsp_http_notify(p_rua, HTTP_NTF_EVE_PLAY))
        {
            return FALSE;
        }
#endif
    }

    return TRUE;
}

BOOL rtsp_pause_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (p_rua->state == RSS_PLAYING)
    {
        HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
        if (tx_msg == NULL)
        {
            return FALSE;
        }

        rsua_send_free_rtsp_msg(p_rua, tx_msg);

        if (rtsp_pause_stream_tx(p_rua))
        {
            p_rua->state = RSS_PAUSE;
        }
    }

    return TRUE;
}

BOOL rtsp_teardown_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua,tx_msg);

    p_rua->state = RSS_NULL;

    return TRUE;
}

BOOL rtsp_get_parameter_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    return TRUE;
}

BOOL rtsp_set_parameter_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    return TRUE;
}

BOOL rtsp_auth_process(RSSUA * p_rua, const char * method, HD_AUTH_INFO * p_auth)
{
    const char * pass = rtsp_get_user_pass(p_auth->auth_name);
    if (NULL == pass)    // user not exist
    {
        return FALSE;
    }

    return digest_auth_process(p_auth, &p_rua->auth_info, method, pass);
}

BOOL rtsp_security_rly(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    HRTSP_MSG * tx_msg = rsua_build_security_response(p_rua);
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua, tx_msg);

    return TRUE;
}

BOOL rtsp_auth_handler(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    BOOL auth = FALSE;
    HD_AUTH_INFO auth_info;

    memset(&auth_info, 0, sizeof(auth_info));

    // check rtsp digest auth information
    if (rtsp_get_auth_digest_info(rx_msg, &auth_info))
    {
        auth = rtsp_auth_process(p_rua, rx_msg->first_line.header, &auth_info);
    }

    if (auth == FALSE)
    {
        rtsp_security_rly(p_rua, rx_msg);
    }

    return auth;
}

#ifdef MEDIA_PUSHER

BOOL rtsp_get_media_info(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (rx_msg == NULL || p_rua == NULL)
    {
        return FALSE;
    }

    if (!rtsp_msg_with_sdp(rx_msg))
    {
        return FALSE;
    }

    rtsp_get_remote_cap(rx_msg, "video", &(p_rua->channels[AV_VIDEO_CH].cap_count), 
        p_rua->channels[AV_VIDEO_CH].cap, &p_rua->channels[AV_VIDEO_CH].r_port);
    rtsp_get_remote_cap_desc(rx_msg, "video", p_rua->channels[AV_VIDEO_CH].cap_desc);

    rtsp_get_remote_cap(rx_msg, "audio", &(p_rua->channels[AV_AUDIO_CH].cap_count), 
        p_rua->channels[AV_AUDIO_CH].cap, &p_rua->channels[AV_AUDIO_CH].r_port);
    rtsp_get_remote_cap_desc(rx_msg, "audio", p_rua->channels[AV_AUDIO_CH].cap_desc);

#ifdef METADATA
    rtsp_get_remote_cap(rx_msg, "application", &(p_rua->channels[AV_METADATA_CH].cap_count), 
        p_rua->channels[AV_METADATA_CH].cap, &p_rua->channels[AV_METADATA_CH].r_port);
    rtsp_get_remote_cap_desc(rx_msg, "application", p_rua->channels[AV_METADATA_CH].cap_desc);
#endif

    return TRUE;
}

BOOL rtsp_get_video_media_info(RSSUA * p_rua)
{
    if (p_rua->channels[AV_VIDEO_CH].cap_count == 0)
    {
        return FALSE;
    }

    if (p_rua->channels[AV_VIDEO_CH].cap[0] == 26)
    {
        p_rua->media_info.v_info.codec = VIDEO_CODEC_JPEG;
    }

    for (int i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[AV_VIDEO_CH].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", 9) == 0)
        {
            int next = 0;
            char buff[64];

            ptr += 9;

            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;

            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff),  &next, WORD_TYPE_STRING);

            if (strcasecmp(buff, "H264/90000") == 0)
            {
                p_rua->media_info.v_info.codec = VIDEO_CODEC_H264;
            }
            else if (strcasecmp(buff, "JPEG/90000") == 0)
            {
                p_rua->media_info.v_info.codec = VIDEO_CODEC_JPEG;
            }
            else if (strcasecmp(buff, "MP4V-ES/90000") == 0)
            {
                p_rua->media_info.v_info.codec = VIDEO_CODEC_MP4;
            }
            else if (strcasecmp(buff, "H265/90000") == 0)
            {
                p_rua->media_info.v_info.codec = VIDEO_CODEC_H265;
            }

            break;
        }
    }

    return TRUE;
}

BOOL rtsp_get_audio_media_info(RSSUA * p_rua, int ch, AUDIO_INFO * p_info)
{
    if (p_rua->channels[ch].cap_count == 0)
    {
        return FALSE;
    }

    if (p_rua->channels[ch].cap[0] == 0)
    {
        p_info->codec = AUDIO_CODEC_G711U;
    }
    else if (p_rua->channels[ch].cap[0] == 8)
    {
        p_info->codec = AUDIO_CODEC_G711A;
    }
    else if (p_rua->channels[ch].cap[0] == 9)
    {
        p_info->codec = AUDIO_CODEC_G722;
    }

    for (int i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_rua->channels[ch].cap_desc[i];
        if (memcmp(ptr, "a=rtpmap:", 9) == 0)
        {
            int next = 0;
            char buff[64];
            
            ptr += 9;

            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;

            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff),  &next, WORD_TYPE_STRING);

            uppercase(buff);

            if (strstr(buff, "G726"))
            {
                p_info->codec = AUDIO_CODEC_G726;
                p_info->bitpersample = atoi(buff+5) / 8; // G726-16, G726-24,G726-32,G726-48

                if (p_info->bitpersample == 0)
                {
                    p_info->bitpersample = 2;
                }
            }
            else if (strstr(buff, "G722"))
            {
                p_info->codec = AUDIO_CODEC_G722;
            }
            else if (strstr(buff, "PCMU"))
            {
                p_info->codec = AUDIO_CODEC_G711U;
            }
            else if (strstr(buff, "PCMA"))
            {
                p_info->codec = AUDIO_CODEC_G711A;
            }
            else if (strstr(buff, "MPEG4-GENERIC"))
            {
                p_info->codec = AUDIO_CODEC_AAC;
            }
            else if (strstr(buff, "OPUS"))
            {
                p_info->codec = AUDIO_CODEC_OPUS;
            }

            char * p = strchr(buff, '/');
            if (p)
            {
                p++;

                char * p1 = strchr(p, '/');
                if (p1)
                {
                    *p1 = '\0';
                    p_info->samplerate = atoi(p);

                    p1++;
                    if (NULL != p1 && *p1 != '\0')
                    {
                        p_info->channels = atoi(p1);
                    }
                    else
                    {
                        p_info->channels = 1;
                    }
                }
                else
                {
                    p_info->samplerate = atoi(p);
                    p_info->channels = 1;
                }
            }

            break;
        }
    }

    if (p_info->codec == AUDIO_CODEC_G722)
    {
        p_info->samplerate = 16000;
        p_info->channels = 1;
    }

    return TRUE;
}

void rtsp_get_video_size(RSSUA * p_rua)
{
    int i;

    for (i = 0; i < MAX_AVN; i++)
    {
        char * sdpline = p_rua->channels[AV_VIDEO_CH].cap_desc[i];
        
        if (sdpline[0] == '\0')
        {
            break;
        }
        
        if (strncasecmp(sdpline, "a=x-dimensions:", 15) == 0)
        {
            int width, height;
            
            if (sscanf(sdpline, "a=x-dimensions:%d,%d", &width, &height) == 2) 
            {
                p_rua->media_info.v_info.width = width;
                p_rua->media_info.v_info.height = height;

                break;
            }
        }
    }
}

#ifdef RTSP_SRTP

BOOL rtsp_get_crypto_info(RSSUA * p_rua, int ch)
{
    if (ch < 0 || ch >= AV_MAX_CHS)
    {
        return FALSE;
    }
    
    int i;
    RSMCH * p_channel = &p_rua->channels[ch];

    for (i=0; i<MAX_AVN; i++)
    {
        char * ptr = p_channel->cap_desc[i];
        if (memcmp(ptr, "a=crypto:", 9) == 0)
        {
            int next = 0;
            char buff[100];
            
            ptr += 9;
    
            if (!get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff), &next, WORD_TYPE_NUM))
            {
                return FALSE;
            }

            ptr += next;
            
            if (!get_line_word(ptr, 0, (int)strlen(ptr), p_channel->crypto_suite, 
                             sizeof(p_channel->crypto_suite), &next, WORD_TYPE_STRING))
            {
                return FALSE;
            }
            
            ptr += next;

            while (*ptr == ' ') ptr++;

            if (memcmp(ptr, "inline:", 7) != 0)
            {
                return FALSE;
            }

            ptr += 7;

            get_line_word(ptr, 0, (int)strlen(ptr), buff, sizeof(buff),  &next, WORD_TYPE_STRING);

            if (base64_decode(buff, strlen(buff), p_channel->crypto_key, sizeof(p_channel->crypto_key)) <= 0)
            {
                return FALSE;
            }

            p_channel->srtp_enable = 1;

            break;
        }
    }

    return p_channel->srtp_enable ? TRUE : FALSE;
}

BOOL rtsp_setup_srtp(RSSUA * p_rua, int ch)
{
    if (ch < 0 || ch >= AV_MAX_CHS)
    {
        return FALSE;
    }

    RSMCH * p_channel = &p_rua->channels[ch];

    memset(&p_channel->srtp_policy, 0, sizeof(p_channel->srtp_policy));

    srtp_crypto_policy_set_rtp_default(&p_channel->srtp_policy.rtp); 
    srtp_crypto_policy_set_rtcp_default(&p_channel->srtp_policy.rtcp);
        
    if (strcasecmp(p_channel->crypto_suite, "AES_CM_128_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES128_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_128_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES128_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_256_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES256_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_256_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES256_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_192_HMAC_SHA1_80") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES192_CM_HMAC_SHA1_80") == 0)
    {
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(&p_channel->srtp_policy.rtcp);
    }
    else if (strcasecmp(p_channel->crypto_suite, "AES_CM_192_HMAC_SHA1_32") == 0 || 
        strcasecmp(p_channel->crypto_suite, "SRTP_AES192_CM_HMAC_SHA1_32") == 0)
    {
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32(&p_channel->srtp_policy.rtp);
        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32(&p_channel->srtp_policy.rtcp);
    }
    else
    {
        log_print(HT_LOG_ERR, "unsupport crypto suite : %s\r\n", p_channel->crypto_suite);
        return FALSE;
    }
    
    p_channel->srtp_policy.ssrc.type = ssrc_any_inbound;
    
    p_channel->srtp_policy.key = p_channel->crypto_key;
    p_channel->srtp_policy.next = NULL;

    if (srtp_create(&p_channel->srtp, &p_channel->srtp_policy) != srtp_err_status_ok) 
    {
        log_print(HT_LOG_ERR, "Error creating inbound SRTP session\r\n");
        return FALSE;
    }

    return TRUE;
}

#endif

BOOL rtsp_announce_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    snprintf(p_rua->cbase, sizeof(p_rua->cbase), "%s", p_rua->uri);

    if (rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_VIDEO_CH].ctl, "video", sizeof(p_rua->channels[AV_VIDEO_CH].ctl)-1))
    {
        p_rua->media_info.has_video = 1;
    }

    if (rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_AUDIO_CH].ctl, "audio", sizeof(p_rua->channels[AV_AUDIO_CH].ctl)-1))
    {
        p_rua->media_info.has_audio = 1;
    }

#ifdef METADATA
    if (rtsp_find_sdp_control(rx_msg, p_rua->channels[AV_METADATA_CH].ctl, "application", sizeof(p_rua->channels[AV_METADATA_CH].ctl)-1))
    {
        p_rua->media_info.has_metadata = 1;
    }
#endif

    p_rua->media_info.is_publish = 1;

    if (rtsp_media_init(p_rua) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, media init failed\r\n", __FUNCTION__);
        return FALSE;
    }

    if (!p_rua->media_info.pusher)
    {
        return FALSE;
    }

    if (p_rua->media_info.pusher->getRua())
    {
        // Prevent multiple clients from pushing at the same time 
        return FALSE;
    }

    p_rua->media_info.pusher->setRua(p_rua);

    if (rtsp_get_media_info(p_rua, rx_msg))
    {
        if (rtsp_get_video_media_info(p_rua))
        {
            if (VIDEO_CODEC_JPEG == p_rua->media_info.v_info.codec)
            {
                // Try to obtain the video size from the SDP based on a=x-dimensions
                rtsp_get_video_size(p_rua);
            }
            
            p_rua->media_info.pusher->setVideoInfo(&p_rua->media_info.v_info);
        }

        if (rtsp_get_audio_media_info(p_rua, AV_AUDIO_CH, &p_rua->media_info.a_info))
        {
            p_rua->media_info.pusher->setAudioInfo(&p_rua->media_info.a_info);
        }

#ifdef RTSP_SRTP
        for (int i = 0; i < AV_MAX_CHS; i++)
        {
            if (rtsp_get_crypto_info(p_rua, i))
            {
                rtsp_setup_srtp(p_rua, i);
            }
        }
#endif
    }

    if (p_rua->media_info.a_info.codec == AUDIO_CODEC_AAC)
    {
        int sizelength = 13;
        int indexlength = 3;
        int indexdeltalength = 3;
        char config[128];

        rsua_get_sdp_aac_params(p_rua, NULL, &sizelength, &indexlength, &indexdeltalength, config, sizeof(config));

        p_rua->media_info.pusher->setAACConfig(sizelength, indexlength, indexdeltalength);
    }

    if (p_rua->media_info.v_info.codec == VIDEO_CODEC_MP4)
    {
        char config[1000];

        if (rsua_get_sdp_mp4_params(p_rua, NULL, config, sizeof(config)-1))
        {
            uint32  configLen;
            uint8 * configData = mpeg4_parse_config(config, configLen);
            if (configData)
            {
                p_rua->media_info.pusher->setMpeg4Config(configData, configLen);

                free(configData);   
            }
        }
    }
    else if (p_rua->media_info.v_info.codec == VIDEO_CODEC_H264)
    {
        char sdp[1024];

        if (rsua_get_sdp_h264_desc(p_rua, NULL, sdp, sizeof(sdp)))
        {
            p_rua->media_info.pusher->setH264Params(sdp);
        }
    }
    else if (p_rua->media_info.v_info.codec == VIDEO_CODEC_H265)
    {
        char sdp[1024];

        if (rsua_get_sdp_h265_desc(p_rua, NULL, sdp, sizeof(sdp)))
        {
            p_rua->media_info.pusher->setH265Params(sdp);
        }
    }

#ifdef HTTP_NOTIFY
    if (!rtsp_http_notify(p_rua, HTTP_NTF_EVE_CONNECT))
    {
        return FALSE;
    }
#endif

    HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua,tx_msg);

    p_rua->state = RSS_ANNOUNCE;

    return TRUE;
}

BOOL rtsp_record_req(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    if (p_rua->state != RSS_RECORDING)
    {
        if (!p_rua->rtp_tcp)
        {
            CMediaPusher * p_pusher = p_rua->media_info.pusher;

            p_pusher->startUdpRx(p_rua->channels[AV_VIDEO_CH].udp_fd, p_rua->channels[AV_AUDIO_CH].udp_fd);
        }

#ifdef HTTP_NOTIFY
        if (!rtsp_http_notify(p_rua, HTTP_NTF_EVE_PUBLISH))
        {
            return FALSE;
        }
#endif
    }

    p_rua->rtp_tx = 1;

    HRTSP_MSG * tx_msg = rsua_build_response(p_rua, "200 OK");
    if (tx_msg == NULL) 
    {
        return FALSE;
    }

    rsua_send_free_rtsp_msg(p_rua,tx_msg);

    p_rua->state = RSS_RECORDING;

    return TRUE;
}

void rtsp_tcp_data_rx(RSSUA * p_rua, uint8 * data, int rlen)
{
    uint8   interleaved = net_read_uint8(data+1);
    uint8 * p_rtp = data + 4;
    uint32  rtp_len = rlen - 4;

    if (!p_rua->media_info.is_pusher || NULL == p_rua->media_info.pusher || !p_rua->rtp_tx)
    {
        return;
    }

    if (interleaved == p_rua->channels[AV_VIDEO_CH].interleaved)
    {
        p_rua->media_info.pusher->videoRtpRx(p_rtp, rtp_len);
    }
    else if (interleaved == p_rua->channels[AV_AUDIO_CH].interleaved)
    {
        p_rua->media_info.pusher->audioRtpRx(p_rtp, rtp_len);
    }
#ifdef RTSP_METADATA
    else if (interleaved == p_rua->channels[AV_METADATA_CH].interleaved)
    {
        p_rua->media_info.pusher->metadataRtpRx(p_rtp, rtp_len);
    }
#endif
}

#endif // end of MEDIA_PUSHER

/***********************************************************************
 *
 * rtsp server state machine
 *
***********************************************************************/
int rtsp_server_state(RSSUA * p_rua, HRTSP_MSG * rx_msg)
{
    char buf[32];

    if (rx_msg->msg_type != 0)
    {
        goto err_del_rua;
    }

    if (rtsp_get_msg_cseq(rx_msg, buf, sizeof(buf)) == FALSE)
    {
        goto err_del_rua;
    }

    p_rua->cseq = atoi(buf);

    if (g_rtsp_cfg.need_auth && !rtsp_auth_handler(p_rua, rx_msg))
    {
        return 0;
    }

    if (rtsp_get_headline_uri(rx_msg, p_rua->uri, sizeof(p_rua->uri)) == FALSE)
    {
        goto err_del_rua;
    }

    switch (rx_msg->msg_sub_type)
    {
    case RTSP_MT_OPTIONS:
        if (rtsp_options_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;

    case RTSP_MT_DESCRIBE:
        if (rtsp_describe_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;

    case RTSP_MT_SETUP:
        if (rtsp_setup_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;

    case RTSP_MT_PLAY:
        if (rtsp_play_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;

    case RTSP_MT_PAUSE:
        rtsp_pause_req(p_rua, rx_msg);
        break;

    case RTSP_MT_TEARDOWN:
        rtsp_teardown_req(p_rua, rx_msg);
        goto err_del_rua;
        break;

    case RTSP_MT_GET_PARAMETER:
        rtsp_get_parameter_req(p_rua, rx_msg);
        break;

    case RTSP_MT_SET_PARAMETER:
        rtsp_set_parameter_req(p_rua, rx_msg);
        break;

#ifdef MEDIA_PUSHER        
    case RTSP_MT_ANNOUNCE:
        if (rtsp_announce_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;

    case RTSP_MT_RECORD:
        if (rtsp_record_req(p_rua, rx_msg) == FALSE)
        {
            goto err_del_rua;
        }
        break;
#endif

    case RTSP_MT_REDIRECT:
        break;
    }

    return 0;

err_del_rua:

    rtsp_close_rua(p_rua);
    return -1;
}

void rtsp_stop_rua(RSSUA * p_rua)
{
    if (p_rua->fd > 0)
    {
#ifdef EPOLL    
        epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_DEL, p_rua->fd, NULL);
#endif

        closesocket(p_rua->fd);
        p_rua->fd = 0;

#ifdef HTTP_NOTIFY
        rtsp_http_notify(p_rua, HTTP_NTF_EVE_DONE);
#endif
    }
    else
    {
        return;
    }

    RTSPMSG msg;
    memset(&msg,0,sizeof(RTSPMSG));
    msg.msg_src = RTSP_DEL_UA_SRC;
    msg.msg_dua = rsua_get_index(p_rua);
    msg.msg_buf = NULL;

    if (hqueue_put(g_rtsp.msg_queue, (char *)&msg) == FALSE)
    {
        log_print(HT_LOG_ERR, "%s, send msg to main task failed!!!\r\n", __FUNCTION__);
    }
}

void rtsp_close_session(RSSUA * p_rua)
{
    int i;
    char * p;

    p = strchr(p_rua->media_info.filename, '?');
    if (NULL == p)
    {
        p = strchr(p_rua->media_info.filename, '&');
    }

    if (p)
    {
        *p = 0;
    }

#ifdef RTSP_OVER_HTTP
    if (p_rua->rtsp_recv)
    {
        HTTPCLN * p_cln = (HTTPCLN *)p_rua->rtsp_recv;

        sys_os_mutex_enter(p_cln->userdata_mutex);
        p_cln->userdata = NULL;
        sys_os_mutex_leave(p_cln->userdata_mutex);

        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
        p_rua->rtsp_recv = NULL;
    }

    if (p_rua->rtsp_send)
    {
        HTTPCLN * p_cln = (HTTPCLN *)p_rua->rtsp_send;

        sys_os_mutex_enter(p_cln->userdata_mutex);
        p_cln->userdata = NULL;
        sys_os_mutex_leave(p_cln->userdata_mutex);
        
        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
        p_rua->rtsp_send = NULL;
    }

    if (p_rua->base64_buff)
    {
        free(p_rua->base64_buff);
        p_rua->base64_buff = NULL;
    }
#endif

#ifdef RTSP_OVER_WEBSOCKET
    if (p_rua->http_cln)
    {
        HTTPCLN * p_cln = (HTTPCLN *)p_rua->http_cln;

        sys_os_mutex_enter(p_cln->userdata_mutex);
        p_cln->userdata = NULL;
        sys_os_mutex_leave(p_cln->userdata_mutex);
        
        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
        p_rua->http_cln = NULL;
    }
    
    if (p_rua->ws_msg.buff)
    {
        free(p_rua->ws_msg.buff);
        p_rua->ws_msg.buff = NULL;
    }
#endif

#ifdef RTSPS
    if (p_rua->rtsps)
    {
        sys_os_mutex_enter(p_rua->ssl_mutex);
        if (p_rua->ssl)
        {
            SSL_free((SSL*)p_rua->ssl);
            p_rua->ssl = NULL;
        }
        sys_os_mutex_leave(p_rua->ssl_mutex);

        if (p_rua->ssl_mutex)
        {
            sys_os_destroy_mutex(p_rua->ssl_mutex);
            p_rua->ssl_mutex = NULL;
        }
    }
#endif

    if (p_rua->fd > 0)
    {
#ifdef EPOLL
        epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_DEL, p_rua->fd, NULL);
#endif

        closesocket(p_rua->fd);
        p_rua->fd = 0;
    }

#ifdef MEDIA_PUSHER
    if (p_rua->media_info.pusher)
    {
        if (p_rua->media_info.is_publish)
        {
            p_rua->media_info.pusher->stopUdpRx();
            p_rua->media_info.pusher->setRua(NULL);

            media_free_pusher(p_rua->media_info.filename);
        }

        if (p_rua->media_info.pusher)
        {
            p_rua->media_info.pusher->freeInstance(p_rua->media_info.filename);
            p_rua->media_info.pusher = NULL;
        }
    }
#endif

    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].udp_fd > 0)
        {
            closesocket(p_rua->channels[i].udp_fd);
            p_rua->channels[i].udp_fd = 0;
        }

#ifdef RTSP_RTCP
        if (p_rua->channels[i].rtcp_fd > 0)
        {
            closesocket(p_rua->channels[i].rtcp_fd);
            p_rua->channels[i].rtcp_fd = 0;
        }
#endif
    }

#ifdef RTSP_BACKCHANNEL
    p_rua->rtp_rx = 0;

    sys_os_wait_thread(&p_rua->tid_udp_rx);

    rtsp_bc_uninit_audio(p_rua);
#endif

#ifdef RTSP_RTCP
    p_rua->rtcp_rx = 0;

    sys_os_wait_thread(&p_rua->tid_rtcp_rx);
#endif

#ifdef MEDIA_PROXY
    if (p_rua->media_info.proxy)
    {
        p_rua->media_info.proxy->freeInstance(p_rua->media_info.filename);
        p_rua->media_info.proxy = NULL;
    }
#endif

#ifdef MEDIA_FILE
    if (p_rua->media_info.file_demuxer)
    {
        delete p_rua->media_info.file_demuxer;
        p_rua->media_info.file_demuxer = NULL;
    }
#endif

#ifdef MEDIA_DEVICE
    if (p_rua->media_info.video_capture)
    {
        p_rua->media_info.video_capture->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.video_capture = NULL;
    }

    if (p_rua->media_info.screen_capture)
    {
        p_rua->media_info.screen_capture->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.screen_capture = NULL;
    }

    if (p_rua->media_info.window_capture)
    {
        p_rua->media_info.window_capture->freeInstance(p_rua->media_info.window_title);
        p_rua->media_info.window_capture = NULL;
    }

    if (p_rua->media_info.audio_capture)
    {
        p_rua->media_info.audio_capture->freeInstance(p_rua->media_info.a_index);
        p_rua->media_info.audio_capture = NULL;
    }
#endif

#ifdef MEDIA_LIVE
    if (p_rua->media_info.live_video)
    {
        p_rua->media_info.live_video->freeInstance(p_rua->media_info.v_index);
        p_rua->media_info.live_video = NULL;
    }

    if (p_rua->media_info.live_audio)
    {
        p_rua->media_info.live_audio->freeInstance(p_rua->media_info.a_index);
        p_rua->media_info.live_audio = NULL;
    }
#endif

#ifdef RTSP_SRTP
    for (i = 0; i < AV_MAX_CHS; i++)
    {
        if (p_rua->channels[i].srtp)
        {
            srtp_dealloc(p_rua->channels[i].srtp);
            p_rua->channels[i].srtp = NULL;
        }
    }
#endif

    if (p_rua->rtp_rcv_buf)
    {
        free(p_rua->rtp_rcv_buf);
        p_rua->rtp_rcv_buf = NULL;
    }
}

void rtsp_close_rua(RSSUA * p_rua)
{
    log_print(HT_LOG_DBG, "%s, p_rua = %p\r\n", __FUNCTION__, p_rua);

    if (p_rua == NULL)
    {
        return;
    }

    if (p_rua->fd > 0)
    {
#ifdef EPOLL    
        epoll_ctl(g_rtsp.ep_fd, EPOLL_CTL_DEL, p_rua->fd, NULL);        
#endif

        closesocket(p_rua->fd);
        p_rua->fd = 0;

#ifdef HTTP_NOTIFY
        rtsp_http_notify(p_rua, HTTP_NTF_EVE_DONE);
#endif
    }

    if (p_rua->mc_ref)
    {
        rtsp_mc_del_ref(p_rua);
    }
    else if (p_rua->mc_src)
    {
        rtsp_mc_del_src(p_rua);
    }
    
    rtsp_stop_stream_tx(p_rua);
    rtsp_close_session(p_rua);
    rsua_set_idle_rua(p_rua);
}





