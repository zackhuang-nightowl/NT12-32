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

#include "http_notify.h"
#include "http_cln.h"

#ifdef HTTP_NOTIFY

BOOL http_notify_init_http_req(HTTP_NTF_EVENT evt, HTTP_NOTIFY_CFG * hook, HTTPREQ * p_http)
{
    char url[HTTP_URL_MAX_LEN] = {'\0'};

    if (evt == HTTP_NTF_EVE_CONNECT)
    {
        strcpy(url, hook->on_connect);
    }
    else if (evt == HTTP_NTF_EVE_PLAY)
    {
        strcpy(url, hook->on_play);
    }
    else if (evt == HTTP_NTF_EVE_PUBLISH)
    {
        strcpy(url, hook->on_publish);
    }
    else if (evt == HTTP_NTF_EVE_DONE)
    {
        strcpy(url, hook->on_done);
    }
    else
    {
        return FALSE;
    }

    int port = -1;
    char user[32] = {'\0'};
    char pass[32] = {'\0'};
    char host[64] = {'\0'};
    char path[256] = {'\0'};
    char proto[12] = {'\0'};
    
    url_split(url, proto, sizeof(proto), user, sizeof(user), pass, sizeof(pass), host, sizeof(host), &port, path, sizeof(path));

    if (strcasecmp(proto, "https") == 0)
    {
        p_http->https = 1;
    }
    else if (strcasecmp(proto, "http") != 0)
    {
        return FALSE;
    }

    if (port == -1)
    {
        if (p_http->https)
        {
            p_http->port = 443;
        }
        else
        {
            p_http->port = 80;
        }
    }
    else
    {
        p_http->port = port;
    }

    strcpy(p_http->host, host);
    strcpy(p_http->url, path);

    if (user[0] != '\0')
    {
        strcpy(p_http->auth_info.auth_name, user);
    }

    if (pass[0] != '\0')
    {
        strcpy(p_http->auth_info.auth_pwd, pass);
    }

    return TRUE;
}

int http_notify_build_http_req(HTTPREQ * p_http, const char * method, char * buff, int buflen, const char * params, int params_len)
{
    int offset = 0;

    offset += snprintf(buff+offset, buflen-offset, "%s %s HTTP/1.1\r\n", method, p_http->url);
                
    if (p_http->family == AF_INET6)
    {
        offset += snprintf(buff+offset, buflen-offset, "Host: [%s]:%u\r\n", p_http->host, p_http->port);
    }
    else
    {
        offset += snprintf(buff+offset, buflen-offset, "Host: %s:%u\r\n", p_http->host, p_http->port);
    }
    
    offset += snprintf(buff+offset, buflen-offset, 
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Connection: Close\r\n"
                "Content-Length: %d\r\n", 
                params_len);

    if (p_http->need_auth)
    {
        offset += http_build_auth_msg(p_http, method, buff+offset, buflen-offset);        
    }
    
    offset += snprintf(buff+offset, buflen-offset, "\r\n");

    if (strcmp(method, "POST") == 0)
    {
        offset += snprintf(buff+offset, buflen-offset, "%s", params);
    }

    log_print(HT_LOG_DBG, "TX >> %s\r\n\r\n", buff);

    return offset;
}

int http_notify_handler(HTTP_NTF_EVENT evt, HTTP_NOTIFY_CFG * hook, const char * params)
{
    HTTPREQ http;
    memset(&http, 0, sizeof(http));

    if (!http_notify_init_http_req(evt, hook, &http))
    {
        return 0;
    }

    if (hook->notify_method == HTTP_NTF_METHOD_GET)
    {
        strcat(http.url, "?");
        strcat(http.url, params);
    }

    int timeout = 2*1000;
    int ret = 0;
    int offset = 0;
    char buff[2048] = {'\0'};
    HT_SOCKADDR sockaddr;

RETRY:

    if (!get_address_by_name(http.host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_ERR, "%s, invalid host: %s\r\n", __FUNCTION__, http.host);
        return 0;
    }

    if (sockaddr.ipv4_flag)
    {
        sockaddr.ipv4_addr.sin_port = htons(http.port);

        http.family = AF_INET;
        http.cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv4_addr, timeout);
    }
    else if (sockaddr.ipv6_flag)
    {
        sockaddr.ipv6_addr.sin6_port = htons(http.port);

        http.family = AF_INET6;
        http.cfd = tcp_connect_timeout((struct sockaddr *) &sockaddr.ipv6_addr, timeout);
    }
    
    if (http.cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, %s:%d connect failed\r\n", 
            __FUNCTION__, http.host, http.port);
        return 0;
    }

    if (http.https)
    {
#ifdef HTTPS    
        if (!http_cln_ssl_conn(&http, timeout))
        {
            log_print(HT_LOG_ERR, "%s, https connect failed\r\n", __FUNCTION__);
            goto FAILED;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif
    }

    offset = http_notify_build_http_req(&http, hook->notify_method == HTTP_NTF_METHOD_GET ? "GET" : "POST",
                                      buff, sizeof(buff), params, 
                                      hook->notify_method == HTTP_NTF_METHOD_GET ? 0 : strlen(params));
    
    if (http_cln_tx(&http, buff, offset) <= 0)
    {
        log_print(HT_LOG_ERR, "%s, http_cln_tx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (!http_cln_rx_timeout(&http, timeout))
    {
        log_print(HT_LOG_ERR, "%s, http_cln_rx_timeout failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (http.rx_msg->msg_sub_type == 401)
    {
        if (http.need_auth == FALSE)
        {
            if (http_get_digest_info(http.rx_msg, &http.auth_info))
            {
                http.auth_mode = 1;  // digest
                strcpy(http.auth_info.auth_uri, http.url);
            }
            else
            {
                http.auth_mode = 0;  // basic
            }
            
            http.need_auth = TRUE;
    
            http_cln_free_req(&http);

            goto RETRY;
        }

        goto FAILED;
    }

    ret = http.rx_msg->msg_sub_type;

FAILED:

    http_cln_free_req(&http);

    return ret;
}

#endif



