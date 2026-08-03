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
#include "http_test.h"
#include "http.h"
#include "http_cln.h"
#include "http_parse.h"
#ifdef HTTPS
#include "openssl/ssl.h"
#endif


BOOL http_test_req(HTTPREQ * p_http)
{
    int ret;
    int offset = 0;
    char bufs[1024];
    char cookie[100];
    int buflen = sizeof(bufs);

    snprintf(cookie, sizeof(cookie), "%x%x%x", rand(), rand(), sys_os_get_ms());
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "Accept: */*\r\n"
                "x-sessioncookie: %s\r\n"
                "User-Agent: happytimesoft\r\n", 
                p_http->url,
                p_http->host, p_http->port,
                cookie);

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

BOOL http_test_rx_timeout(HTTPREQ * p_http, int timeout)
{
    int sret, rlen;
    BOOL selflag = TRUE;
    BOOL ret = FALSE;
    fd_set fdr;
    struct timeval tv;
    
    while (1)
    {
#ifdef HTTPS
        if (p_http->https)
        {
            sys_os_mutex_enter(p_http->ssl_mutex);
            if (p_http->ssl)
            {
                if (SSL_pending((SSL*)p_http->ssl) > 0)
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
            sys_os_mutex_leave(p_http->ssl_mutex);
        }
#endif

        if (selflag)
        {
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
            
            FD_ZERO(&fdr);
            FD_SET(p_http->cfd, &fdr);

            sret = select((int)(p_http->cfd+1), &fdr, NULL, NULL, &tv);
            if (sret == 0)
            {
                log_print(HT_LOG_WARN, "%s, timeout!!!\r\n", __FUNCTION__);
                break;
            }
            else if (sret < 0)
            {
                log_print(HT_LOG_ERR, "%s, select err[%s], sret[%d]!!!\r\n", 
                    __FUNCTION__, sys_os_get_socket_error(), sret);
                break;
            }
            else if (!FD_ISSET(p_http->cfd, &fdr))
            {
                continue;
            }
        }

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
        
        if (rlen < 0)
        {
            log_print(HT_LOG_INFO, "%s, recv return = %d, dlen[%d], mlen[%d]\r\n", 
                __FUNCTION__, rlen, p_http->rcv_dlen, p_http->mlen);    
            return FALSE;
        }

        p_http->rcv_dlen += rlen;
        p_http->rbuf[p_http->rcv_dlen] = '\0';

        if (p_http->rcv_dlen < 16)
        {
            continue;
        }
        
        if (http_is_http_msg(p_http->rbuf) == FALSE)
        {
            break;
        }

        if (p_http->hdr_len == 0)
        {
            int parse_len;
            int http_pkt_len;

            http_pkt_len = http_pkt_find_end(p_http->rbuf);
            if (http_pkt_len == 0) 
            {
                continue;
            }
            p_http->hdr_len = http_pkt_len;

            HTTPMSG * rx_msg = http_get_msg_buf(http_pkt_len+1);
            if (rx_msg == NULL)
            {
                log_print(HT_LOG_ERR, "%s, get msg buf failed\r\n", __FUNCTION__);
                return FALSE;
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
                return FALSE;
            }
            
            p_http->ctt_len = rx_msg->ctt_len;
            p_http->rx_msg = rx_msg;

            ret = TRUE;
            break;
        }
    }

    return ret;
}

BOOL http_test(const char * url, const char * user, const char * pass, HTTPCTT * ctt, int timeout)
{
    BOOL ret = FALSE;
    int  port, https=0;
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

    HTTPREQ hreq;
    memset(&hreq, 0, sizeof(hreq));
    
    strncpy(hreq.host, host, sizeof(hreq.host) - 1);
    
    if (username[0] != '\0')
    {
        strcpy(hreq.auth_info.auth_name, username);
    }
    else if (user && user[0] != '\0')
    {
        strcpy(hreq.auth_info.auth_name, user);
    }

    if (password[0] != '\0')
    {
        strcpy(hreq.auth_info.auth_pwd, password);
    }
    else if (pass && pass[0] != '\0')
    {
        strcpy(hreq.auth_info.auth_pwd, pass);
    }
    
    if (path[0] != '\0')
    {
        strcpy(hreq.url, path);
        strcpy(hreq.auth_info.auth_uri, path);
    }
    
    hreq.port = port;
    hreq.https = https;

RETRY:

    HT_SOCKADDR sockaddr;
    
    if (!get_address_by_name(hreq.host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid host: %s\r\n", __FUNCTION__, hreq.host);
        return FALSE;
    }

    if (sockaddr.ipv4_flag)
    {
        sockaddr.ipv4_addr.sin_port = htons(hreq.port);

        hreq.family = AF_INET;
        hreq.cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        sockaddr.ipv6_addr.sin6_port = htons(hreq.port);

        hreq.family = AF_INET6;
        hreq.cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
    
    if (hreq.cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, tcp_connect_timeout\r\n", __FUNCTION__);
        return FALSE;
    }

    if (hreq.https)
    {
#ifdef HTTPS    
        if (!http_cln_ssl_conn(&hreq, timeout))
        {
            return FALSE;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        return FALSE;
#endif        
    }

    if (!http_test_req(&hreq))
    {
        return FALSE;
    }
    
    if (!http_test_rx_timeout(&hreq, timeout))
    {
        return FALSE;
    }
    
    if (hreq.rx_msg->msg_sub_type == 200)
    {
        if (ctt)
        {
            *ctt = hreq.rx_msg->ctt_type;
        }

        ret = TRUE;
    }
    else if (hreq.rx_msg->msg_sub_type == 401)
    {
        if (!hreq.need_auth)
        {
            http_cln_auth_set(&hreq, NULL, NULL);

            http_cln_free_req(&hreq);

            goto RETRY;
        }
    }
    
    http_cln_free_req(&hreq);

    return ret;
}



