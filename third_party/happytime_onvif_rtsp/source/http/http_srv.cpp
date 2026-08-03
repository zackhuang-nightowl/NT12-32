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
#include "http_srv.h"
#include "http_parse.h"
#ifdef HTTPS
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif
#ifdef HTTPD
#include "httpd.h"
#endif

/***************************************************************************************/

#define HTTP_SOCKETS        60
#define KEEPALIVE_TIMEOUT   5   // 5 second timeout

typedef struct
{
    uint32    idx;
    HTTPSRV * srv;
} HttpThreadParam;

/***************************************************************************************/

#ifdef HTTPS

/* initialize SSL server and create context */
SSL_CTX * http_init_ssl_ctx()
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
BOOL http_load_certificates(SSL_CTX * ctx, const char * cert_file, const char * key_file)
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

#endif // end of HTTPS

/***************************************************************************************/

BOOL http_commit_rx_msg(HTTPCLN * p_user, HTTPMSG * rx_msg)
{
    BOOL ret = FALSE;
    HTTPSRV * p_srv = (HTTPSRV *)p_user->http_srv;
    
    sys_os_mutex_enter(p_srv->mutex_cb);
    
    if (p_srv->msg_cb)
    {
        ret = p_srv->msg_cb(p_user, rx_msg, p_srv->msg_user);
    }
    
    sys_os_mutex_leave(p_srv->mutex_cb);

    return ret;
}

/*
 * The message previously received by the client may not have been processed yet and 
 *  needs to be sent to the main task queue for deletion
 */
void http_commit_free_cln(HTTPSRV * p_srv, HTTPCLN * p_cln)
{
    if (p_cln->cfd > 0)
    {
#ifdef EPOLL    
        epoll_ctl(p_srv->ep_fd, EPOLL_CTL_DEL, p_cln->cfd, NULL);
#endif

        closesocket(p_cln->cfd);
        p_cln->cfd = 0;
    }
    
    sys_os_mutex_enter(p_srv->mutex_cb);
    
    if (p_srv->msg_cb)
    {
        p_srv->msg_cb(p_cln, NULL, p_srv->msg_user);
    }
    
    sys_os_mutex_leave(p_srv->mutex_cb);
}

void http_commit_rx_data(HTTPCLN * p_user, char * buff, int buflen)
{
    HTTPSRV * p_srv = (HTTPSRV *)p_user->http_srv;
    if (NULL == p_srv)
    {
        return;
    }
    
    sys_os_mutex_enter(p_srv->mutex_cb);
    
    if (p_srv->data_cb)
    {
        p_srv->data_cb(p_user, buff, buflen, p_srv->data_user);
    }
    
    sys_os_mutex_leave(p_srv->mutex_cb);   
}

BOOL http_commit_connection(HTTPSRV * p_srv, struct sockaddr * addr)
{
    BOOL ret = TRUE;
    
    sys_os_mutex_enter(p_srv->mutex_cb);
    
    if (p_srv->conn_cb)
    {
        ret = p_srv->conn_cb(p_srv, addr, p_srv->conn_user);
    }
    
    sys_os_mutex_leave(p_srv->mutex_cb);

    return ret;
}

BOOL http_data_rx(HTTPCLN * p_user)
{
    int rlen = 0;
    HTTPMSG * rx_msg = NULL;
    
    if (p_user->rbuf == NULL)
    {
        p_user->rbuf = p_user->rcv_buf;
        p_user->mlen = sizeof(p_user->rcv_buf)-4;
        p_user->rcv_dlen = 0;
        p_user->ctt_len = 0;
        p_user->hdr_len = 0;
    }

#ifdef HTTPS
    if (p_user->https)
    {
        sys_os_mutex_enter(p_user->ssl_mutex);
        if (p_user->ssl)
        {
            rlen = SSL_read((SSL *)p_user->ssl, p_user->rbuf+p_user->rcv_dlen, p_user->mlen-p_user->rcv_dlen);
        }
        else
        {
            rlen = -1;
        }
        sys_os_mutex_leave(p_user->ssl_mutex);
    }
    else 
#endif
    {
        rlen = recv(p_user->cfd, p_user->rbuf+p_user->rcv_dlen, p_user->mlen-p_user->rcv_dlen, 0);
    }

    if (rlen <= 0)
    {
        log_print(HT_LOG_WARN, "%s, recv return = %d, dlen[%d], mlen[%d]\r\n", 
            __FUNCTION__, rlen, p_user->rcv_dlen, p_user->mlen);
        return FALSE;
    }

    p_user->rcv_dlen += rlen;
    p_user->rbuf[p_user->rcv_dlen] = '\0';
    p_user->last_time = sys_os_get_uptime();

    if (p_user->pass_through)
    {
        http_commit_rx_data(p_user, p_user->rbuf, p_user->rcv_dlen);
        
        p_user->hdr_len = 0;
        p_user->ctt_len = 0;
        p_user->rbuf = 0;
        p_user->rcv_dlen = 0;

        return TRUE;
    }

    if (p_user->rcv_dlen < 16)
    {
        return TRUE;
    }
    
    if (http_is_http_msg(p_user->rbuf) == FALSE)
    {
        return FALSE;
    }

    if (p_user->hdr_len == 0)
    {
        int parse_len;
        int http_pkt_len;
        
        http_pkt_len = http_pkt_find_end(p_user->rbuf);
        if (http_pkt_len == 0)
        {
            return TRUE;
        }
        p_user->hdr_len = http_pkt_len;

        rx_msg = http_get_msg_buf(http_pkt_len+1);
        if (rx_msg == NULL)
        {
            log_print(HT_LOG_ERR, "%s, get_msg_buf ret null!!!\r\n", __FUNCTION__);
            return FALSE;
        }

        memcpy(rx_msg->msg_buf, p_user->rbuf, http_pkt_len);
        rx_msg->msg_buf[http_pkt_len] = '\0';
        
        log_print(HT_LOG_DBG, "RX << %s\r\n", rx_msg->msg_buf);

        parse_len = http_msg_parse_part1(rx_msg->msg_buf, http_pkt_len, rx_msg);
        if (parse_len != http_pkt_len)
        {
            log_print(HT_LOG_ERR, "%s, http_msg_parse_part1=%d, http_pkt_len=%d!!!\r\n", 
                __FUNCTION__, parse_len, http_pkt_len);
            http_free_msg(rx_msg);
            return FALSE;
        }
        
        p_user->ctt_len = rx_msg->ctt_len;
        p_user->ctt_type = rx_msg->ctt_type;
        p_user->keep_alive = rx_msg->keep_alive;

        if (CTT_RTSP_TUNNELLED == p_user->ctt_type)
        {
            if (http_commit_rx_msg(p_user, rx_msg) == FALSE)
            {
                return FALSE;
            }

            if (p_user->rcv_dlen - http_pkt_len > 0 && p_user->pass_through)
            {
                http_commit_rx_data(p_user, p_user->rbuf+p_user->hdr_len, p_user->rcv_dlen-http_pkt_len);
            }
            
            p_user->hdr_len = 0;
            p_user->ctt_len = 0;
            p_user->rbuf = 0;
            p_user->rcv_dlen = 0;
            
            return TRUE;
        }
    }

    if ((p_user->ctt_len + p_user->hdr_len) > p_user->mlen)
    {
        if (p_user->dyn_recv_buf)
        {
            log_print(HT_LOG_INFO, "%s, dyn_recv_buf=%p, mlen=%d!!!\r\n", 
                __FUNCTION__, p_user->dyn_recv_buf, p_user->mlen);
            free(p_user->dyn_recv_buf);
        }

        p_user->dyn_recv_buf = (char *)malloc(p_user->ctt_len + p_user->hdr_len + 1);
        if (NULL == p_user->dyn_recv_buf)
        {
            http_free_msg(rx_msg);
        
            log_print(HT_LOG_INFO, "%s, malloc failed\r\n", __FUNCTION__);
            return FALSE;
        }

        memcpy(p_user->dyn_recv_buf, p_user->rcv_buf, p_user->rcv_dlen);

        p_user->rbuf = p_user->dyn_recv_buf;
        p_user->mlen = p_user->ctt_len + p_user->hdr_len;

        http_free_msg(rx_msg);

        return TRUE;
    }

    if (p_user->rcv_dlen >= (p_user->ctt_len + p_user->hdr_len))
    {
        if (rx_msg == NULL)
        {
            int parse_len;
            int nlen;

            nlen = p_user->ctt_len + p_user->hdr_len;

            rx_msg = http_get_msg_buf(nlen+1);
            if (rx_msg == NULL)
            {
                log_print(HT_LOG_ERR, "%s, get msg buf failed\r\n", __FUNCTION__);
                return FALSE;
            }    

            memcpy(rx_msg->msg_buf, p_user->rbuf, p_user->hdr_len);
            rx_msg->msg_buf[p_user->hdr_len] = '\0';
            
            parse_len = http_msg_parse_part1(rx_msg->msg_buf, p_user->hdr_len, rx_msg);
            if (parse_len != p_user->hdr_len)
            {
                log_print(HT_LOG_ERR, "%s, http_msg_parse_part1=%d, hdr_len=%d!!!\r\n", 
                    __FUNCTION__, parse_len, p_user->hdr_len);
                http_free_msg(rx_msg);
                return FALSE;
            }
        }

        if (p_user->ctt_len > 0)
        {
            int parse_len;
            
            memcpy(rx_msg->msg_buf+p_user->hdr_len, p_user->rbuf+p_user->hdr_len, p_user->ctt_len);
            rx_msg->msg_buf[p_user->hdr_len + p_user->ctt_len] = '\0';

            if (ctt_is_string(rx_msg->ctt_type))
            {
                log_print(HT_LOG_DBG, "%s\r\n\r\n", rx_msg->msg_buf+p_user->hdr_len);
            }
            
            parse_len = http_msg_parse_part2(rx_msg->msg_buf+p_user->hdr_len, p_user->ctt_len, rx_msg);
            if (parse_len != p_user->ctt_len)
            {
                log_print(HT_LOG_WARN, "%s, http_msg_parse_part2=%d, ctt_len=%d!!!\r\n", 
                    __FUNCTION__, parse_len, p_user->ctt_len);
            }
        }
        
        p_user->rcv_dlen -= p_user->hdr_len + p_user->ctt_len;

        if (p_user->dyn_recv_buf == NULL)
        {
            if (p_user->rcv_dlen > 0)
            {
                memmove(p_user->rcv_buf, p_user->rcv_buf + p_user->hdr_len + p_user->ctt_len, p_user->rcv_dlen);
                p_user->rcv_buf[p_user->rcv_dlen] = '\0';
            }
            
            p_user->rbuf = p_user->rcv_buf;
            p_user->mlen = sizeof(p_user->rcv_buf)-4;
            p_user->hdr_len = 0;
            p_user->ctt_len = 0;
        }
        else
        {
            free(p_user->dyn_recv_buf);
            p_user->dyn_recv_buf = NULL;
            p_user->hdr_len = 0;
            p_user->ctt_len = 0;
            p_user->rbuf = 0;
            p_user->rcv_dlen = 0;
        }

        if (http_commit_rx_msg(p_user, rx_msg) == FALSE)
        {
            return FALSE;
        }
    }
    else if (rx_msg)
    {
        http_free_msg(rx_msg);
    }
    
    return TRUE;
}

void http_srv_rx(HTTPSRV * p_srv, HTTPCLN * p_cln)
{
#ifdef HTTPS
    if (p_cln->https)
    {
        do 
        {
            if (http_data_rx(p_cln) == FALSE)
            {
                http_commit_free_cln(p_srv, p_cln);
                break;
            }

            sys_os_mutex_enter(p_cln->ssl_mutex);
            if (p_cln->ssl && SSL_pending((SSL*)p_cln->ssl) > 0)
            {
                sys_os_mutex_leave(p_cln->ssl_mutex);
                continue;
            }
            sys_os_mutex_leave(p_cln->ssl_mutex);
            
            break;
            
        } while (1);
    }
    else
#endif
    if (http_data_rx(p_cln) == FALSE)
    {                            
        http_commit_free_cln(p_srv, p_cln);
    }
}

int http_listen_rx(HTTPSRV * p_srv, SOCKET sfd, int family)
{
    char ip[64];
    SOCKET cfd;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    socklen_t addrlen;
    HTTPCLN * p_cln;
    int on = 1;
    int len = 2 * 1024 * 1024;
#ifdef HTTPS
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
        return -1;
    }
    
    cfd = accept(sfd, addr, &addrlen);
    if (cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, accept, cfd(%d), %s\r\n", __FUNCTION__, cfd, sys_os_get_socket_error());
        return -1;
    }

#ifdef HTTPS
    if (p_srv->https)
    {
        ssl = SSL_new((SSL_CTX *)p_srv->ssl_ctx);
        if (NULL == ssl)
        {
            log_print(HT_LOG_ERR, "%s, SSL_new failed, %s\r\n", 
                __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
            goto err;
        }
        
        SSL_set_fd(ssl, (int)cfd);
        
        if (-1 == SSL_accept(ssl))
        {
            log_print(HT_LOG_ERR, "%s, SSL_accept failed, %s\r\n", 
                __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
            goto err;
        }
    }
#endif

    if (!http_commit_connection(p_srv, addr))
    {
        log_print(HT_LOG_INFO, "%s, http reject connection!\r\n", __FUNCTION__);
        goto err;
    }
    
    p_cln = http_get_idle_cln(p_srv);
    if (NULL == p_cln)
    {
        log_print(HT_LOG_ERR, "%s, http_get_idle_cln::ret null!!!\r\n", __FUNCTION__);
        goto err;
    }

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
    
    p_cln->cfd = cfd;

    if (family == AF_INET)
    {
        p_cln->sockaddr.ipv4_flag = 1;
        memcpy(&p_cln->sockaddr.ipv4_addr, addr, addrlen);
    }
    else if (family == AF_INET6)
    {
        p_cln->sockaddr.ipv6_flag = 1;
        memcpy(&p_cln->sockaddr.ipv6_addr, addr, addrlen);
    }

    p_cln->http_srv = p_srv;
    p_cln->last_time = sys_os_get_uptime();
    
#ifdef HTTPS
    if (p_srv->https)
    {
        p_cln->https = 1;
        p_cln->ssl = ssl;
        p_cln->ssl_mutex = sys_os_create_mutex();
    }
#endif

    pps_ctx_ul_add(p_srv->cln_ul, p_cln);

    log_print(HT_LOG_INFO, "http user over tcp from [%s:%u]\r\n", 
        get_sockaddr_ip(addr, ip, sizeof(ip)), get_sockaddr_port(addr));

#ifdef EPOLL
    e_dat = http_cln_index(p_srv, p_cln);
    e_dat = e_dat << 32;
    e_dat = e_dat | cfd;

    event.events = EPOLLIN;
    event.data.u64 = e_dat;
    epoll_ctl(p_srv->ep_fd, EPOLL_CTL_ADD, cfd, &event);
#endif

    return 0;

err:

#ifdef HTTPS
    if (ssl)
    {
        SSL_free(ssl);
    }
#endif

    closesocket(cfd);
    
    return -1;
}

void * http_rx_thread(void * argv)
{    
    HttpThreadParam * param = (HttpThreadParam *)argv;
    HTTPSRV * p_srv = param->srv;
#ifndef EPOLL
    uint32 idx = param->idx;
#endif

    free(argv);
    
    if (p_srv == NULL)
    {
        return NULL;
    }

    while (p_srv->rxflag)
    {
        if (!p_srv->enable)
        {
            usleep(100*1000);
            continue;
        }
        
#ifdef EPOLL

        int i, fd, nfds;
        uint32 cur_time = sys_os_get_uptime();
        HTTPCLN * p_cln;
        struct epoll_event * ep_events = (struct epoll_event *) p_srv->ep_events;
        
        nfds = epoll_wait(p_srv->ep_fd, ep_events, p_srv->ep_event_num, 100);

        for (i=0; i<nfds; i++)
        {
            if (ep_events[i].events & EPOLLIN)
            {
                fd = (int)(ep_events[i].data.u64);
                if ((ep_events[i].data.u64 & ((uint64)1 << 63)) != 0)
                {
                    if (fd == p_srv->ipv4_fd)
                    {
                        http_listen_rx(p_srv, fd, AF_INET);
                    }
                    else if (fd == p_srv->ipv6_fd)
                    {
                        http_listen_rx(p_srv, fd, AF_INET6);
                    }
                }
                else
                {
                    uint32 u_index = ep_events[i].data.u64 >> 32;
                    p_cln = http_get_cln_by_index(p_srv, u_index);
                    if (NULL == p_cln)
                    {
                        break;
                    }

                    if (p_cln->cfd > 0 && p_cln->cfd == fd)
                    {
                        http_srv_rx(p_srv, p_cln);
                    }
                    else
                    {
                        log_print(HT_LOG_WARN, "%s, event fd[%d] not match user fd[%d]!!!\r\n", 
                            __FUNCTION__, fd, p_cln->cfd);
                    }
                }
            }
        }

        for (i = 0; i < (int)p_srv->max_cln_nums; i++)
        {
            p_cln = http_get_cln_by_index(p_srv, i);
            if (p_cln && p_cln->cfd > 0)
            {
                int diff = cur_time - p_cln->last_time;
                
                if (p_cln->keep_alive && diff > KEEPALIVE_TIMEOUT)
                {
                    http_commit_free_cln(p_srv, p_cln);
                }
            }
        }
        
#else

        int sret;
        int max_fd = 0;
        uint32 i, s, e;
        fd_set fdr;
        HTTPCLN * p_cln;
        struct timeval tv;
        uint32 cur_time = sys_os_get_uptime();

        FD_ZERO(&fdr);
        
        if (idx == 0)
        {
            if (p_srv->ipv4_fd > 0)
            {
                FD_SET(p_srv->ipv4_fd, &fdr);
                max_fd = (int)p_srv->ipv4_fd;
            }

            if (p_srv->ipv6_fd > 0)
            {
                FD_SET(p_srv->ipv6_fd, &fdr);
                max_fd = (int)(((int)p_srv->ipv6_fd > max_fd)? p_srv->ipv6_fd : max_fd);
            }
        }

        s = idx * HTTP_SOCKETS;
        e = (idx + 1) * HTTP_SOCKETS;

        e = e > p_srv->max_cln_nums ? p_srv->max_cln_nums : e;

        for (i = s; i < e; i++)
        {
            p_cln = http_get_cln_by_index(p_srv, i);
            if (p_cln && p_cln->cfd > 0)
            {
                int diff = cur_time - p_cln->last_time;
                
                if (p_cln->keep_alive && diff > KEEPALIVE_TIMEOUT)
                {
                    http_commit_free_cln(p_srv, p_cln);
                }
                else
                {
                    FD_SET(p_cln->cfd, &fdr);
                    max_fd = (int)(((int)p_cln->cfd > max_fd)? p_cln->cfd : max_fd);
                }
            }
        }

        if (max_fd == 0)
        {
            usleep(100*1000);
            continue;
        }
        
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        
        sret = select(max_fd+1, &fdr, NULL, NULL, &tv);
        if (sret == 0)
        {
            continue;
        }
        else if (sret < 0)
        {
            usleep(1000);
            
            log_print(HT_LOG_ERR, "%s, select err, max fd[%d], sret[%d], [%d,%s]\r\n", 
                __FUNCTION__, max_fd, sret, sys_os_get_socket_error_num(), sys_os_get_socket_error());
            continue;
        }

        if (p_srv->ipv4_fd > 0 && FD_ISSET(p_srv->ipv4_fd, &fdr))
        {
            http_listen_rx(p_srv, p_srv->ipv4_fd, AF_INET);
        }
        
        if (p_srv->ipv6_fd > 0 && FD_ISSET(p_srv->ipv6_fd, &fdr))
        {
            http_listen_rx(p_srv, p_srv->ipv6_fd, AF_INET6);
        }

        for (i = s; i < e; i++)
        {
            p_cln = http_get_cln_by_index(p_srv, i);

            if (p_cln && p_cln->cfd > 0 && FD_ISSET(p_cln->cfd, &fdr))
            {
                http_srv_rx(p_srv, p_cln);               
            }
        }

#endif
    }
    
    log_print(HT_LOG_DBG, "%s, exit\r\n", __FUNCTION__);

    return NULL;
}

int http_srv_socket_init(HTTPSRV * p_srv, struct sockaddr * addr, socklen_t addrlen)
{
    int yes = 1;
    SOCKET fd;
    
    fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, socket err[%s]!!!\r\n", __FUNCTION__, sys_os_get_socket_error());
        return 0;
    }
    
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));

    // On Linux, the default IPV6_V6ONLY is 1, and IPV4 and IPV6 cannot be bound to the same port at the same time
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&yes, sizeof(yes));

    if (bind(fd, addr, addrlen) == -1)
    {
        char ip[64] = {'\0'};
        log_print(HT_LOG_ERR, "%s, bind tcp socket fail,err[%s]!!!\n", 
            __FUNCTION__, sys_os_get_socket_error());
        printf("Bind %s:%d failed\r\n", get_sockaddr_ip(addr, ip, sizeof(ip)), get_sockaddr_port(addr));
        closesocket(fd);
        return 0;
    }

    if (listen(fd, 10) < 0)
    {
        log_print(HT_LOG_ERR, "%s, listen tcp socket fail,err[%s]!!!\r\n", 
            __FUNCTION__, sys_os_get_socket_error());
        closesocket(fd);
        return 0;
    }
    
#ifdef EPOLL
    uint64 e_dat = fd;
    e_dat |= ((uint64)1 << 63);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = e_dat;
    epoll_ctl(p_srv->ep_fd, EPOLL_CTL_ADD, fd, &event);
#endif

    return fd;
}

int http_srv_net_init(HTTPSRV * p_srv)
{
    if (p_srv->sockaddr.ipv4_flag)
    {
        p_srv->ipv4_fd = http_srv_socket_init(p_srv, (struct sockaddr *) &p_srv->sockaddr.ipv4_addr, sizeof(struct sockaddr_in));
        if (p_srv->ipv4_fd <= 0)
        {
            p_srv->sockaddr.ipv4_flag = 0;
        }
    }

    if (p_srv->sockaddr.ipv6_flag)
    {
        p_srv->ipv6_fd = http_srv_socket_init(p_srv, (struct sockaddr *) &p_srv->sockaddr.ipv6_addr, sizeof(struct sockaddr_in6));
        if (p_srv->ipv6_fd <= 0)
        {
            p_srv->sockaddr.ipv6_flag = 0;
        }
    }

    return (p_srv->ipv4_fd > 0 || p_srv->ipv6_fd > 0) ? 0 : -1;
}

HT_API BOOL http_srv_init(HTTPSRV * p_srv, const char * saddr, uint16 sport, int cln_num, BOOL https, const char * cert_file, const char * key_file, BOOL ipv6)
{
    uint32 i;
    
    memset(p_srv, 0, sizeof(HTTPSRV));

    if (saddr && saddr[0] != '\0')
    {
        if (!is_ipv4_address(saddr) && !is_ipv6_address(saddr)) // domain or hostname
        {
            p_srv->sockaddr.ipv4_flag = 1;
            p_srv->sockaddr.ipv4_addr.sin_family = AF_INET;
            p_srv->sockaddr.ipv4_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            if (ipv6)
            {
                p_srv->sockaddr.ipv6_flag = 1;
                p_srv->sockaddr.ipv6_addr.sin6_family = AF_INET6;
                p_srv->sockaddr.ipv6_addr.sin6_addr = in6addr_any;
            }
        }
        else if (!get_address_by_name(saddr, HT_PROTOCOL_TCP, &p_srv->sockaddr))
        {
            log_print(HT_LOG_ERR, "%s, get_address_by_name (%s) failed\r\n", __FUNCTION__, saddr);
            return FALSE;
        }
        
        strncpy(p_srv->ipv4_host, saddr, sizeof(p_srv->ipv4_host)-1);
        strncpy(p_srv->ipv6_host, saddr, sizeof(p_srv->ipv6_host)-1);
    }
    else
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

        if (ipv6)
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

    p_srv->sockaddr.ipv4_addr.sin_port = htons(sport);
    p_srv->sockaddr.ipv6_addr.sin6_port = htons(sport);
    
    p_srv->https = https;
    p_srv->ipv6 = ipv6;
    p_srv->max_cln_nums = cln_num;

    p_srv->cln_fl = pps_ctx_fl_init(cln_num, sizeof(HTTPCLN), TRUE);
    if (p_srv->cln_fl == NULL)
    {
        return FALSE;
    }
    
    p_srv->cln_ul = pps_ctx_ul_init(p_srv->cln_fl, TRUE);
    if (p_srv->cln_ul == NULL)
    {
        goto FAILED;
    }
    
#ifdef EPOLL
    p_srv->ep_event_num = cln_num + 16;
    
    p_srv->ep_fd = epoll_create(p_srv->ep_event_num);
    if (p_srv->ep_fd < 0)
    {
        log_print(HT_LOG_ERR, "%s, epoll_create failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    p_srv->ep_events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * p_srv->ep_event_num);
    if (p_srv->ep_events == NULL)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        goto FAILED;
    }
#endif
    
#ifdef HTTPS
    if (p_srv->https)
    {
        /* initialize SSL */
        p_srv->ssl_ctx = http_init_ssl_ctx();   
        if (NULL == p_srv->ssl_ctx)
        {
            log_print(HT_LOG_ERR, "%s, http_init_ssl_ctx failed\r\n", __FUNCTION__);
            goto FAILED;
        }

        /* load certs */
        if (!http_load_certificates((SSL_CTX *)p_srv->ssl_ctx, cert_file, key_file))
        {
            log_print(HT_LOG_ERR, "%s, http_load_certificates failed\r\n", __FUNCTION__);
            goto FAILED;
        }
    }
#endif

    if (http_srv_net_init(p_srv) != 0)
    {
        goto FAILED;
    }

    p_srv->mutex_cb = sys_os_create_mutex();
    
    p_srv->rxflag = 1;
    p_srv->enable = 1;

#ifdef EPOLL
    p_srv->rx_num = 1;
#else
    p_srv->rx_num = p_srv->max_cln_nums / HTTP_SOCKETS + 1;
#endif

    p_srv->rx_tid = (pthread_t *) malloc(sizeof(pthread_t) * p_srv->rx_num);

    for (i = 0; i < p_srv->rx_num; i++)
    {
        HttpThreadParam * param = (HttpThreadParam *) malloc(sizeof(HttpThreadParam));
        if (param)
        {
            param->idx = i;
            param->srv = p_srv;

            p_srv->rx_tid[i] = sys_os_create_thread((void *)http_rx_thread, param);
        }
    }
    
    return TRUE;

FAILED:

    http_srv_deinit(p_srv);
    
    return FALSE;
}

HT_API void http_srv_deinit(HTTPSRV * p_srv)
{
    uint32 i;   
    HTTPCLN * p_cln;
    
    p_srv->rxflag = 0;
    
    if (p_srv->rx_tid)
    {
        for (i = 0; i < p_srv->rx_num; i++)
        {
            sys_os_wait_thread(&p_srv->rx_tid[i]);
        }

        p_srv->rx_num = 0;
        free(p_srv->rx_tid);
        p_srv->rx_tid = NULL;
    }

    for (i = 0; i < p_srv->max_cln_nums; i++)
    {
        p_cln = http_get_cln_by_index(p_srv, i);
        if (p_cln && p_cln->cfd > 0)
        {
            http_free_used_cln(p_srv, p_cln);
        }
    }
    
    if (p_srv->cln_ul)
    {
        pps_ul_free(p_srv->cln_ul);
        p_srv->cln_ul = NULL;
    }

    if (p_srv->cln_fl)
    {
        pps_fl_free(p_srv->cln_fl);
        p_srv->cln_fl = NULL;
    }

    if (p_srv->mutex_cb)
    {
        sys_os_destroy_mutex(p_srv->mutex_cb);
        p_srv->mutex_cb = NULL;
    }
    
#ifdef HTTPS
    if (p_srv->ssl_ctx)
    {
        SSL_CTX_free((SSL_CTX *)p_srv->ssl_ctx);
        p_srv->ssl_ctx = NULL;
    }
#endif

    if (p_srv->ipv4_fd > 0)
    {
#ifdef EPOLL
        epoll_ctl(p_srv->ep_fd, EPOLL_CTL_DEL, p_srv->ipv4_fd, NULL);
#endif
        closesocket(p_srv->ipv4_fd);
        p_srv->ipv4_fd = 0;
    }

    if (p_srv->ipv6_fd > 0)
    {
#ifdef EPOLL
        epoll_ctl(p_srv->ep_fd, EPOLL_CTL_DEL, p_srv->ipv6_fd, NULL);
#endif
        closesocket(p_srv->ipv6_fd);
        p_srv->ipv6_fd = 0;
    }

#ifdef EPOLL
    if (p_srv->ep_fd)
    {
        close(p_srv->ep_fd);
        p_srv->ep_fd = 0;        
    }

    if (p_srv->ep_events)
    {
        free(p_srv->ep_events);
        p_srv->ep_events = NULL;
    }
#endif
}

HT_API void http_set_msg_cb(HTTPSRV * p_srv, http_msg_cb cb, void * p_userdata)
{
    sys_os_mutex_enter(p_srv->mutex_cb);
    p_srv->msg_cb = cb;
    p_srv->msg_user = p_userdata;
    sys_os_mutex_leave(p_srv->mutex_cb);
}

HT_API void http_set_data_cb(HTTPSRV * p_srv, http_data_cb cb, void * p_userdata)
{
    sys_os_mutex_enter(p_srv->mutex_cb);
    p_srv->data_cb = cb;
    p_srv->data_user = p_userdata;
    sys_os_mutex_leave(p_srv->mutex_cb);
}

HT_API void http_set_conn_cb(HTTPSRV * p_srv, http_conn_cb cb, void * p_userdata)
{
    sys_os_mutex_enter(p_srv->mutex_cb);
    p_srv->conn_cb = cb;
    p_srv->conn_user = p_userdata;
    sys_os_mutex_leave(p_srv->mutex_cb);
}

HT_API int http_srv_cln_tx(HTTPCLN * p_user, const char * p_data, int len)
{
    int res = 0;
    int offset = 0;
#ifdef HTTPS
    int ret = 0;
#endif

    if (NULL == p_user || NULL == p_data || 0 == len)
    {
        return -1;
    }
    
#ifdef HTTPS
    if (p_user->https)
    {
        while (offset < len)
        {
            sys_os_mutex_enter(p_user->ssl_mutex);
            
            if (p_user->ssl)
            {
                res = SSL_write((SSL*)p_user->ssl, p_data+offset, len-offset);
                ret = SSL_get_error((SSL*)p_user->ssl, res);
            }
            else
            {
                ret = -1;
            }
            
            sys_os_mutex_leave(p_user->ssl_mutex);
            
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
    }
    else 
#endif
    while (offset < len)
    {
        res = send(p_user->cfd, p_data+offset, len-offset, 0);
        if (res > 0)
        {
            offset += res;
        }
        else
        {
            int sockerr = sys_os_get_socket_error_num();
            if (sockerr == EINTR || sockerr == EAGAIN)
            {
                usleep(1000);
                continue;
            }
            
            log_print(HT_LOG_ERR, "%s, send failed, tlen[%d,%d],err[%d][%s]!!!\r\n",
                __FUNCTION__, res, len-offset, sockerr, sys_os_get_socket_error());            
            return -1;
        }
    }

    p_user->last_time = sys_os_get_uptime();
    
    return offset;
}

HT_API BOOL http_get_peer_addr(HTTPCLN * p_user, struct sockaddr ** local, struct sockaddr ** remote)
{
    static struct sockaddr_in addr4;
    static struct sockaddr_in6 addr6;
    HTTPSRV * p_srv = (HTTPSRV *) p_user->http_srv;

    if (p_user->sockaddr.ipv4_flag)
    {
        if (p_srv->sockaddr.ipv4_addr.sin_addr.s_addr == htonl(INADDR_ANY))
        {
            socklen_t addrlen = sizeof(addr4);

            memset(&addr4, 0, sizeof(addr4));
            addr4.sin_family = AF_INET;
            
            getsockname(p_user->cfd, (struct sockaddr *)&addr4, &addrlen);

            *local = (struct sockaddr *) &addr4;
        }
        else
        {
            *local = (struct sockaddr *) &p_srv->sockaddr.ipv4_addr;
        }

        *remote = (struct sockaddr *) &p_user->sockaddr.ipv4_addr;
    }
    else if (p_user->sockaddr.ipv6_flag)
    {
        if (memcmp(&p_srv->sockaddr.ipv6_addr.sin6_addr, &in6addr_any, sizeof(struct in6_addr)) == 0)
        {
            socklen_t addrlen = sizeof(addr6);

            memset(&addr6, 0, sizeof(addr6));
            addr6.sin6_family = AF_INET6;
            
            getsockname(p_user->cfd, (struct sockaddr *)&addr6, &addrlen);

            *local = (struct sockaddr *) &addr6;
        }
        else
        {
            *local = (struct sockaddr *) &p_srv->sockaddr.ipv6_addr;
        }

        *remote = (struct sockaddr *) &p_user->sockaddr.ipv6_addr;
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

HT_API void http_set_enable(HTTPSRV * p_srv, BOOL enable)
{
    p_srv->enable = enable;
}

/***************************************************************************************/

HT_API uint32 http_cln_index(HTTPSRV * p_srv, HTTPCLN * p_cln)
{
    return pps_get_index(p_srv->cln_fl, p_cln);
}

HT_API HTTPCLN * http_get_cln_by_index(HTTPSRV * p_srv, unsigned long index)
{
    return (HTTPCLN *)pps_get_node_by_index(p_srv->cln_fl, index);
}

HT_API HTTPCLN * http_get_idle_cln(HTTPSRV * p_srv)
{    
    HTTPCLN * p_cln = (HTTPCLN *)pps_fl_pop(p_srv->cln_fl);
    if (p_cln)
    {
        memset(p_cln, 0, sizeof(HTTPCLN));

        p_cln->use_count = 1;
        p_cln->use_count_mutex = sys_os_create_mutex();

        log_print(HT_LOG_INFO, "%s, p_cln=%p, index[%u]\r\n", 
            __FUNCTION__, p_cln, http_cln_index(p_srv, p_cln));
    }

    return p_cln;
}

HT_API void http_free_used_cln(HTTPSRV * p_srv, HTTPCLN * p_cln)
{
    sys_os_mutex_enter(p_cln->use_count_mutex);
    if (p_cln->use_count > 0)
    {
        p_cln->use_count--;
        if (p_cln->use_count > 0)
        {
            sys_os_mutex_leave(p_cln->use_count_mutex);
            return;
        }
    }
    else
    {
        sys_os_mutex_leave(p_cln->use_count_mutex);
        return;
    }
    sys_os_mutex_leave(p_cln->use_count_mutex);

    log_print(HT_LOG_INFO, "%s, p_cln=%p, index[%u]\r\n", 
        __FUNCTION__, p_cln, http_cln_index(p_srv, p_cln));

    if (p_cln->use_count_mutex)
    {
        sys_os_destroy_mutex(p_cln->use_count_mutex);
        p_cln->use_count_mutex = NULL;
    }

    if (p_cln->cfd > 0)
    {
#ifdef EPOLL    
        epoll_ctl(p_srv->ep_fd, EPOLL_CTL_DEL, p_cln->cfd, NULL);
#endif

        closesocket(p_cln->cfd);
        p_cln->cfd = 0;
    }
    
    if (p_cln->dyn_recv_buf)
    {
        free(p_cln->dyn_recv_buf);
        p_cln->dyn_recv_buf = NULL;
    }

    sys_os_mutex_enter(p_cln->userdata_mutex);
    p_cln->userdata = NULL;
    sys_os_mutex_leave(p_cln->userdata_mutex);
        
    if (p_cln->userdata_mutex)
    {
        sys_os_destroy_mutex(p_cln->userdata_mutex);
        p_cln->userdata_mutex = NULL;
    }
    
#ifdef HTTPS
    if (p_cln->https)
    {
        sys_os_mutex_enter(p_cln->ssl_mutex);
        if (p_cln->ssl)
        {
            SSL_free((SSL*)p_cln->ssl);
            p_cln->ssl = NULL;
        }
        sys_os_mutex_leave(p_cln->ssl_mutex);

        if (p_cln->ssl_mutex)
        {
            sys_os_destroy_mutex(p_cln->ssl_mutex);
            p_cln->ssl_mutex = NULL;
        }
    }
#endif

    pps_ctx_ul_del(p_srv->cln_ul, p_cln);
    
    pps_fl_push_tail(p_srv->cln_fl, p_cln);
}




