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
#include "http.h"
#include "http_parse.h"
#include "http_cln.h"
#include "http_mjpeg_cln.h"
#ifdef HTTPS
#include "openssl/ssl.h"
#endif


void * mjpeg_rx_thread(void * argv)
{
    CHttpMjpeg * p_mjpeg = (CHttpMjpeg *)argv;

    p_mjpeg->rx_thread();

    return NULL;
}


CHttpMjpeg::CHttpMjpeg(void)
: m_running(FALSE)
, m_rx_tid(0)
, m_header(FALSE)
, m_pNotify(NULL)
, m_pUserdata(NULL)
, m_pVideoCB(NULL)
, m_pMutex(NULL)
, m_nRxTimeout(10)
{
    memset(&m_http, 0, sizeof(m_http));
    memset(m_url, 0, sizeof(m_url));

    m_pMutex = sys_os_create_mutex();
}

CHttpMjpeg::~CHttpMjpeg(void)
{
    mjpeg_close();

    if (m_pMutex)
    {
        sys_os_destroy_mutex(m_pMutex);
        m_pMutex = NULL;
    }
}

BOOL CHttpMjpeg::mjpeg_start(const char * url, const char * user, const char * pass)
{
    int port, https=0;
    char proto[32], username[64], password[64], host[100], path[200];

    url_split(url, proto, sizeof(proto), username, sizeof(username), 
        password, sizeof(password), host, sizeof(host), &port, path, sizeof(path));

    if (strcasecmp(proto, "https") == 0)
    {
        https = 1;
        port = (port == -1) ? 443 : port;
    }
    else if (strcasecmp(proto, "http") == 0)
    {
        port = (port == -1) ? 80 : port;
    }
    else
    {
        return FALSE;
    }

    if (host[0] == '\0')
    {
        return FALSE;
    }

    strncpy(m_http.host, host, sizeof(m_http.host) - 1);
    
    if (username[0] != '\0')
    {
        strcpy(m_http.auth_info.auth_name, username);
    }
    else if (user && user[0] != '\0')
    {
        strcpy(m_http.auth_info.auth_name, user);
    }

    if (password[0] != '\0')
    {
        strcpy(m_http.auth_info.auth_pwd, password);
    }
    else if (pass && pass[0] != '\0')
    {
        strcpy(m_http.auth_info.auth_pwd, pass);
    }
    
    if (path[0] != '\0')
    {
        strcpy(m_http.url, path);
        strcpy(m_http.auth_info.auth_uri, path);
    }
    
    m_http.port = port;
    m_http.https = https;
    strncpy(m_url, url, sizeof(m_url)-1);

    m_running = TRUE;
    m_rx_tid = sys_os_create_thread((void *)mjpeg_rx_thread, this);
    if (m_rx_tid == 0)
    {
        log_print(HT_LOG_ERR, "%s, sys_os_create_thread failed!!!\r\n", __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

BOOL CHttpMjpeg::mjpeg_stop()
{
    return mjpeg_close();
}

BOOL CHttpMjpeg::mjpeg_close()
{
    sys_os_mutex_enter(m_pMutex);
    m_pVideoCB = NULL;
    m_pNotify = NULL;
    m_pUserdata = NULL;
    sys_os_mutex_leave(m_pMutex);

#ifdef HTTPS    
    if (m_http.https && m_http.ssl)
    {
        SSL_shutdown((SSL*)m_http.ssl);
    }
#endif

    if (m_http.cfd > 0)
    {
        closesocket(m_http.cfd);
        m_http.cfd = 0;
    }
    
    m_running = FALSE;

    sys_os_wait_thread(&m_rx_tid);
    
    http_cln_free_req(&m_http);

    m_header = 0;
    memset(&m_http, 0, sizeof(m_http));

    return TRUE;
}

BOOL CHttpMjpeg::mjpeg_req(HTTPREQ * p_http)
{
    int ret;
    int offset = 0;
    char bufs[1024];
    int buflen = sizeof(bufs);
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "Accept: */*\r\n"
                "User-Agent: happytimesoft/1.0\r\n"
                "Range: bytes=0-\r\n", 
                p_http->url,
                p_http->host, p_http->port);

    if (p_http->need_auth)
    {
        offset += http_build_auth_msg(p_http, "GET", bufs+offset, buflen-offset);        
    }

    offset += snprintf(bufs+offset, buflen-offset, "\r\n");
    
    bufs[offset] = '\0';

    log_print(HT_LOG_DBG, "TX >> %s\r\n", bufs);

    ret = http_cln_tx(p_http, bufs, offset);
    
    return (ret == offset) ? TRUE : FALSE;
}

BOOL CHttpMjpeg::mjpeg_conn(HTTPREQ * p_http, int timeout)
{
    HT_SOCKADDR sockaddr;
    
    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid host: %s\r\n", __FUNCTION__, p_http->host);
        return FALSE;
    }

    if (sockaddr.ipv4_flag)
    {
        sockaddr.ipv4_addr.sin_port = htons(p_http->port);

        p_http->family = AF_INET;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        sockaddr.ipv6_addr.sin6_port = htons(p_http->port);

        p_http->family = AF_INET6;
        p_http->cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
    
    if (p_http->cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, tcp_connect_timeout\r\n", __FUNCTION__);
        return FALSE;
    }

    if (p_http->https)
    {
#ifdef HTTPS    
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            return FALSE;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        return FALSE;
#endif        
    }

    int len = 2 * 1024 * 1024;
        
    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_RCVBUF error!\n", __FUNCTION__);
    }

    if (setsockopt(p_http->cfd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int)))
    {
        log_print(HT_LOG_WARN, "%s, setsockopt SO_SNDBUF error!\n", __FUNCTION__);
    }
    
    return TRUE;
}

int CHttpMjpeg::mjpeg_parse_header(HTTPREQ * p_http)
{
    if (http_is_http_msg(p_http->rbuf) == FALSE)
    {
        return -1;
    }
    
    HTTPMSG * rx_msg = NULL;

    int parse_len;
    int http_pkt_len;

    http_pkt_len = http_pkt_find_end(p_http->rbuf);
    if (http_pkt_len == 0) 
    {
        return 0;
    }

    rx_msg = http_get_msg_buf(http_pkt_len+1);
    if (rx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, get msg buf failed\r\n", __FUNCTION__);
        return -1;
    }

    memcpy(rx_msg->msg_buf, p_http->rbuf, http_pkt_len);
    rx_msg->msg_buf[http_pkt_len] = '\0';
    
    log_print(HT_LOG_DBG, "RX from %s << %s\r\n", p_http->host, rx_msg->msg_buf);
    
    parse_len = http_msg_parse_part1(rx_msg->msg_buf, http_pkt_len, rx_msg);
    if (parse_len != http_pkt_len)
    {
        log_print(HT_LOG_ERR, "%s, http_msg_parse_part1=%d, http_pkt_len=%d!!!\r\n", 
            __FUNCTION__, parse_len, http_pkt_len);

        http_free_msg(rx_msg);
        return -1;
    }

    p_http->rx_msg = rx_msg;

    p_http->rcv_dlen -= parse_len;
    if (p_http->rcv_dlen > 0)
    {
        memmove(p_http->rbuf, p_http->rbuf+parse_len, p_http->rcv_dlen);
    }

    return 1;
}

int CHttpMjpeg::mjpeg_parse_header_ex(HTTPREQ * p_http)
{
    HTTPMSG * rx_msg = NULL;

    int offset = 0;
    int http_pkt_len;    
    int line_len = 0;
    BOOL have_next_line;    
    char * p_buf;

    while (*(p_http->rbuf+offset) == '\r' || *(p_http->rbuf+offset) == '\n')
    {
        offset++;
    }
    
    http_pkt_len = http_pkt_find_end(p_http->rbuf+offset);
    if (http_pkt_len == 0) 
    {
        return 0;
    }
    p_http->hdr_len = http_pkt_len+offset;

    rx_msg = http_get_msg_buf(http_pkt_len+1);
    if (rx_msg == NULL)
    {
        log_print(HT_LOG_ERR, "%s, get msg buf failed\r\n", __FUNCTION__);
        return -1;
    }

    memcpy(rx_msg->msg_buf, p_http->rbuf+offset, http_pkt_len);
    rx_msg->msg_buf[http_pkt_len] = '\0';
    
    // log_print(HT_LOG_DBG, "RX from %s << %s\r\n", p_http->host, rx_msg->msg_buf);

    p_buf = rx_msg->msg_buf;    

READ_LINE:

    if (get_sip_line(p_buf, http_pkt_len, &line_len, &have_next_line) == FALSE)
    {
        return -1;
    }

    if (line_len < 2)
    {
        return -1;
    }
    
    offset = 0;
    while (*(p_buf+offset) == '-')
    {
        offset++;
    }
    
    if (strncmp(p_buf+offset, p_http->boundary, strlen(p_http->boundary)))
    {
        p_buf += line_len;
        goto READ_LINE;
    }
    
    p_buf += line_len;
    rx_msg->hdr_len = http_line_parse(p_buf, http_pkt_len-line_len, ':', &(rx_msg->hdr_ctx));
    if (rx_msg->hdr_len <= 0)
    {
        return -1;
    }
    
    http_ctt_parse(rx_msg);

    p_http->rx_msg = rx_msg;
    p_http->ctt_len = rx_msg->ctt_len;

    return 1;
}

void CHttpMjpeg::mjpeg_data_rx(uint8 * data, int len)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pVideoCB)
    {
        m_pVideoCB(data, len, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}

int CHttpMjpeg::mjpeg_rx(HTTPREQ * p_http)
{
    int ret;
    int rlen;
    
    if (p_http->rbuf == NULL)
    {
        p_http->rbuf = p_http->rcv_buf;
        p_http->mlen = sizeof(p_http->rcv_buf)-4;
        p_http->rcv_dlen = 0;
        p_http->ctt_len = 0;
        p_http->hdr_len = 0;
    }

#ifdef HTTPS
    if (p_http->https)
    {
        sys_os_mutex_enter(p_http->ssl_mutex);
        if (p_http->ssl)
        {
            rlen = SSL_read((SSL*)p_http->ssl, p_http->rbuf+p_http->rcv_dlen, p_http->mlen-p_http->rcv_dlen);
        }
        else
        {
            rlen = -1;
        }
        sys_os_mutex_leave(p_http->ssl_mutex);
    }
    else 
#endif
    {
        rlen = recv(p_http->cfd, p_http->rbuf+p_http->rcv_dlen, p_http->mlen-p_http->rcv_dlen, 0);
    }
    
    if (rlen <= 0)
    {
        log_print(HT_LOG_INFO, "%s, recv return = %d, dlen[%d], mlen[%d]\r\n", 
            __FUNCTION__, rlen, p_http->rcv_dlen, p_http->mlen);    

        closesocket(p_http->cfd);
        p_http->cfd = 0;
        return MJPEG_RX_ERR;
    }

    p_http->rcv_dlen += rlen;
    p_http->rbuf[p_http->rcv_dlen] = '\0';
    
rx_analyse_point:

    if (p_http->rcv_dlen < 16)
    {
        return MJPEG_MORE_DATA;
    }

    if (!m_header)
    {
        ret = mjpeg_parse_header(p_http);
        if (ret > 0)
        {
            if (p_http->rx_msg->msg_sub_type == 401 && p_http->need_auth == FALSE)
            {
                http_cln_auth_set(p_http, NULL, NULL);

                http_cln_free_req(p_http);
                
                return MJPEG_NEED_AUTH;
            }
            else if (p_http->rx_msg->msg_sub_type == 401 && p_http->need_auth == TRUE)
            {
                return MJPEG_AUTH_ERR;
            }
            
            if (p_http->rx_msg->ctt_type != CTT_MULTIPART)
            {
                http_free_msg(p_http->rx_msg);
                p_http->rx_msg = NULL;

                return MJPEG_PARSE_ERR;
            }

            m_header = 1;
            strcpy(p_http->boundary, p_http->rx_msg->boundary);

            http_free_msg(p_http->rx_msg);
            p_http->rx_msg = NULL;

            send_notify(MJPEG_EVE_CONNSUCC);
        }
        else if (ret < 0)
        {
            return MJPEG_PARSE_ERR;
        }
    }

    if (!m_header)
    {
        return MJPEG_MORE_DATA;
    }

    if (p_http->rcv_dlen < 16)
    {
        return MJPEG_MORE_DATA;
    }
    
    if (p_http->hdr_len == 0)
    {
        ret = mjpeg_parse_header_ex(p_http);
        if (ret < 0)
        {
            return MJPEG_PARSE_ERR;
        }
        else if (ret == 0)
        {
            return MJPEG_MORE_DATA;
        }
        else 
        {
            if (p_http->rx_msg->ctt_type != CTT_JPG)
            {
                http_free_msg(p_http->rx_msg);
                p_http->rx_msg = NULL;

                return MJPEG_PARSE_ERR;         
            }
            
            http_free_msg(p_http->rx_msg);
            p_http->rx_msg = NULL;
        }
    }
    
    if ((p_http->ctt_len + p_http->hdr_len) > p_http->mlen)
    {
        if (p_http->dyn_recv_buf)
        {
            free(p_http->dyn_recv_buf);
        }

        p_http->dyn_recv_buf = (char *)malloc(p_http->ctt_len + p_http->hdr_len + 1);
        if (NULL == p_http->dyn_recv_buf)
        {
            return MJPEG_MALLOC_ERR;
        }
        
        memcpy(p_http->dyn_recv_buf, p_http->rcv_buf, p_http->rcv_dlen);
        p_http->rbuf = p_http->dyn_recv_buf;
        p_http->mlen = p_http->ctt_len + p_http->hdr_len;

        return MJPEG_MORE_DATA;
    }

    if (p_http->rcv_dlen >= (p_http->ctt_len + p_http->hdr_len))
    {
        mjpeg_data_rx((uint8 *)p_http->rbuf+p_http->hdr_len, p_http->ctt_len);
        
        p_http->rcv_dlen -= p_http->hdr_len + p_http->ctt_len;

        if (p_http->dyn_recv_buf == NULL)
        {
            if (p_http->rcv_dlen > 0)
            {
                memmove(p_http->rcv_buf, p_http->rcv_buf+p_http->hdr_len + p_http->ctt_len, p_http->rcv_dlen);
                p_http->rcv_buf[p_http->rcv_dlen] = '\0';
            }
            
            p_http->rbuf = p_http->rcv_buf;
            p_http->mlen = sizeof(p_http->rcv_buf)-4;
            p_http->hdr_len = 0;
            p_http->ctt_len = 0;

            if (p_http->rcv_dlen > 16)
            {
                goto rx_analyse_point;
            }
        }
        else
        {
            free(p_http->dyn_recv_buf);
            p_http->dyn_recv_buf = NULL;
            p_http->hdr_len = 0;
            p_http->ctt_len = 0;
            p_http->rbuf = NULL;
            p_http->rcv_dlen = 0;
        }
    }
    
    return MJPEG_RX_SUCC;
}

void CHttpMjpeg::rx_thread()
{
    int  ret;
    int  tm_count = 0;
    BOOL selflag = TRUE;
    BOOL nodata_notify = FALSE;    
    int  sret;
    fd_set fdr;
    struct timeval tv;

    send_notify(MJPEG_EVE_CONNECTING);

RETRY:

    if (!mjpeg_conn(&m_http, 10*1000))
    {
        send_notify(MJPEG_EVE_CONNFAIL);
        goto FAILED;
    }

    if (!mjpeg_req(&m_http))
    {
        send_notify(MJPEG_EVE_CONNFAIL);
        goto FAILED;
    }
    
    while (m_running)
    {
#ifdef HTTPS
        if (m_http.https)
        {
            sys_os_mutex_enter(m_http.ssl_mutex);
            if (m_http.ssl)
            {
                if (SSL_pending((SSL*)m_http.ssl) > 0)
                {
                    selflag = FALSE; // There is data to read 
                }
                else
                {
                    selflag = TRUE;
                }
            }
            else
            {
                selflag = TRUE;
            }
            sys_os_mutex_leave(m_http.ssl_mutex);
        }
#endif

        if (selflag)
        {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            FD_ZERO(&fdr);
            FD_SET(m_http.cfd, &fdr);

            sret = select((int)(m_http.cfd+1), &fdr, NULL, NULL, &tv);
            if (sret == 0)
            {
                tm_count++;
                if (tm_count >= m_nRxTimeout && !nodata_notify)    // in 10s without data
                {
                    nodata_notify = TRUE;
                    send_notify(MJPEG_EVE_NODATA);
                }
                
                continue;
            }
            else if (sret < 0)
            {
                log_print(HT_LOG_ERR, "%s, select err[%s], sret[%d]!!!\r\n", 
                    __FUNCTION__, sys_os_get_socket_error(), sret);
                break;
            }
            else if (!FD_ISSET(m_http.cfd, &fdr))
            {
                continue;
            }
            else
            {
                if (nodata_notify)
                {
                    nodata_notify = FALSE;
                    send_notify(MJPEG_EVE_RESUME);
                }
                    
                tm_count = 0;
            }
        }
        
        ret = mjpeg_rx(&m_http);
        if (ret < 0)
        {
            if (ret == MJPEG_AUTH_ERR)
            {
                send_notify(MJPEG_EVE_AUTHFAILED);
            }
            
            log_print(HT_LOG_ERR, "%s, mjpeg_rx failed. ret = %d\r\n", __FUNCTION__, ret);
            break;
        }
        else if (ret == MJPEG_NEED_AUTH)
        {
            goto RETRY;
        }
    }

    send_notify(MJPEG_EVE_STOPPED);
    
FAILED:

    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);
}

void CHttpMjpeg::set_notify_cb(mjpeg_notify_cb notify, void * userdata) 
{
    sys_os_mutex_enter(m_pMutex);
    m_pNotify = notify;
    m_pUserdata = userdata;
    sys_os_mutex_leave(m_pMutex);
}

void CHttpMjpeg::set_video_cb(mjpeg_video_cb cb) 
{
    sys_os_mutex_enter(m_pMutex);
    m_pVideoCB = cb;
    sys_os_mutex_leave(m_pMutex);
}

void CHttpMjpeg::set_rx_timeout(int timeout)
{
    m_nRxTimeout = timeout;
}

void CHttpMjpeg::send_notify(int event)
{
    sys_os_mutex_enter(m_pMutex);
    if (m_pNotify)
    {
        m_pNotify(event, m_pUserdata);
    }
    sys_os_mutex_leave(m_pMutex);
}



