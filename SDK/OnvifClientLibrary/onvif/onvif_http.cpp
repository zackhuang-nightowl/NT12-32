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
#include "onvif_http.h"
#include "onvif_pkt.h"
#include "onvif_ver.h"
#include "soap.h"
#include "soap_parser.h"
#include "http_cln.h"
#include "rfc_md5.h"
#include "base64.h"


/***************************************************************************************/

BOOL http_onvif_req(HTTPREQ * p_user, char * action, char * p_xml, int len)
{
    int slen;
    int offset = 0;
    char bufs[2048];
    int buflen = sizeof(bufs);

    if (p_user->family == AF_INET6)
    {
        offset += snprintf(bufs+offset, buflen-offset, 
                "POST %s HTTP/1.1\r\n"
                "Host: [%s]:%d\r\n",
                p_user->url,
                p_user->host, p_user->port);
    }
    else
    {
        offset += snprintf(bufs+offset, buflen-offset, 
                "POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n",
                p_user->url,
                p_user->host, p_user->port);
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "User-Agent: Happytime onvif client %s\r\n"
                "Content-Type: application/soap+xml; charset=utf-8; action=\"%s\"\r\n"
                "Content-Length: %d\r\n",
                ONVIF_CLIENT_VERSION, action, len);

    if (p_user->need_auth)
    {
        offset += http_build_auth_msg(p_user, "POST", bufs+offset, buflen-offset);        
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "Connection: close\r\n"
                "\r\n");

    log_print(HT_LOG_DBG, "TX >> %s", bufs);
    
    slen = http_cln_tx(p_user, bufs, offset);

    if (p_xml && len > 0)
    {
        log_print(HT_LOG_DBG, "%s\r\n\r\n", p_xml);
        
        slen += http_cln_tx(p_user, p_xml, len);
    }
    
    return (slen == offset + len) ? TRUE : FALSE;
}

HT_API BOOL http_onvif_trans(HTTPREQ * p_http, int timeout, eOnvifAction act, ONVIF_DEVICE * p_dev, void * p_req, void * p_res)
{
    int  xml_len;
    int  buf_len;
    int  autherr;
    BOOL ret =  FALSE;
    char * p_buf = NULL;
    char * rx_xml = NULL;
    XMLN * p_node = NULL;
    XMLN * p_body = NULL;
    OVFACTS * p_ent;

    p_dev->authFailed = 0;
    p_dev->errCode = ONVIF_OK;    

    if (timeout <= 0)
    {
        timeout = 10 * 1000; // default is 10s
    }
    
#ifdef DEMO
#if __WINDOWS_OS__
    static int cnt = 0;
    if (cnt++ >= 40)
    {
        cnt = 0;
        ShellExecute(NULL, NULL, L"www.happytimesoft.com", NULL, L".", 5);
    }
#endif
#endif

    buf_len = 40 * 1024;
    
    p_buf = (char *)malloc(buf_len);
    if (NULL == p_buf)
    {
        p_dev->errCode = ONVIF_ERR_MallocFailure;
        goto FAILED;
    }
    
    p_ent = onvif_find_action_by_type(act);
    if (p_ent == NULL)
    {
        // it should not to here!!!
        goto FAILED;
    }

    HT_SOCKADDR sockaddr;
    
RETRY:

    // build xml bufs    
    xml_len = build_onvif_req_xml(p_buf, buf_len, act, p_dev, p_req);
    
RECONN:

    // connect onvif device

    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        log_print(HT_LOG_WARN, "%s, invalid host %s\r\n", __FUNCTION__, p_http->host);
        p_dev->errCode = ONVIF_ERR_ConnFailure;
        goto FAILED;
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
        // connect failed, retry the binfo connect address
        if (strcasecmp(p_http->host, p_dev->binfo.XAddr.host))
        {
            p_http->https = p_dev->binfo.XAddr.https;
            p_http->port = p_dev->binfo.XAddr.port;
            strcpy(p_http->host, p_dev->binfo.XAddr.host);

            goto RECONN;
        }
        
        p_dev->errCode = ONVIF_ERR_ConnFailure;
        goto FAILED;
    }

    if (p_http->https)
    {
#ifdef HTTPS
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            p_dev->errCode = ONVIF_ERR_ConnFailure;
            
            log_print(HT_LOG_ERR, "%s, http_cln_ssl_conn failed!\r\n", __FUNCTION__);
            goto FAILED;
        }
#else
        p_dev->errCode = ONVIF_ERR_NotSupportHttps;
        
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif        
    }
    
    if (!http_onvif_req(p_http, p_ent->action_url, p_buf, xml_len))
    {
        log_print(HT_LOG_WARN, "%s, send request failed\r\n", __FUNCTION__);
        p_dev->errCode = ONVIF_ERR_MallocFailure;
        goto FAILED;
    }

    if (!http_cln_rx_timeout(p_http, timeout))
    {
        log_print(HT_LOG_WARN, "%s, data rx timeout\r\n", __FUNCTION__);
        p_dev->errCode = ONVIF_ERR_RecvTimeout;
        goto FAILED;
    }
    
    if (p_http->rx_msg->msg_sub_type != 200)
    {
        if (p_http->rx_msg->msg_sub_type == 401 && p_http->need_auth == FALSE)
        {
            http_cln_auth_set(p_http, p_dev->username, p_dev->password);

            http_cln_free_req(p_http);
            
            goto RETRY;
        }

        if (p_http->rx_msg->msg_sub_type == 401)
        {
            p_dev->authFailed = TRUE;
            p_dev->errCode = ONVIF_ERR_HttpResponseError;
        }
    }
    else
    {
        p_dev->authFailed = FALSE;
        p_dev->errCode = ONVIF_OK;
    }

    rx_xml = http_get_ctt(p_http->rx_msg);
    if (NULL == rx_xml)
    {
        p_dev->errCode = ONVIF_ERR_NullContent;
        goto FAILED;
    }

    p_node = xxx_hxml_parse(rx_xml, p_http->rx_msg->ctt_len);
    if (p_node == NULL || p_node->name == NULL)
    {
        p_dev->errCode = ONVIF_ERR_ParseFailed;
        
        log_print(HT_LOG_WARN, "%s, xxx_hxml_parse ret null!!!\r\n", __FUNCTION__);
        goto FAILED;
    }
    
    if (soap_strcmp(p_node->name, "s:Envelope") != 0)
    {
        p_dev->errCode = ONVIF_ERR_ParseFailed;
        
        log_print(HT_LOG_WARN, "%s, node name[%s] != [s:Envelope]!!!\r\n", __FUNCTION__, p_node->name);
        goto FAILED;
    }

    p_body = xml_node_soap_get(p_node, "s:Body");
    if (p_body == NULL)
    {
        p_dev->errCode = ONVIF_ERR_ParseFailed;
        
        log_print(HT_LOG_WARN, "%s, xml_node_soap_get[s:Body] ret null!!!\r\n", __FUNCTION__);
        goto FAILED;
    }

    autherr = 0;
    memset(&p_dev->fault, 0, sizeof(onvif_Fault));

    if (p_http->rx_msg->msg_sub_type == 401)
    {
        autherr = 1;
    }
    else if (parse_Fault(p_body, &p_dev->fault))
    {
        if (soap_strcmp(p_dev->fault.Code, "Sender") == 0 && 
            (soap_strcmp(p_dev->fault.Subcode, "NotAuthorized") == 0 || 
             soap_strcmp(p_dev->fault.Subcode, "Unauthorized") == 0 || 
             soap_strcmp(p_dev->fault.Subcode, "InvalidSecurity") == 0 || 
             soap_strcmp(p_dev->fault.Subcode, "FailedAuthentication") == 0 ||
             soap_strcmp(p_dev->fault.Subcode, "SecurityError") == 0))
        {
            autherr = 1;
        }
    }

    if (autherr)
    {
        if (p_dev->needAuth == FALSE || p_dev->retryCount++ <= 3)
        {
            http_cln_free_req(p_http);
            p_http->auth_mode = -1;
            
            p_dev->needAuth = TRUE;
            
            xml_node_del(p_node);
            p_node = NULL;

            if (p_dev->retryCount > 0)
            {
                usleep(500*1000);
            }
            
            goto RETRY;
        }

        p_dev->authFailed = TRUE;
    }

    if (p_http->rx_msg->msg_sub_type != 200)
    {
        p_dev->errCode = ONVIF_ERR_HttpResponseError;
        goto FAILED;
    }
    else
    {
        p_dev->retryCount = 0;
    }
    
    if (!onvif_rly_handler(p_body, act, p_dev, p_res))
    {
        p_dev->errCode = ONVIF_ERR_HandleFailed;        
        goto FAILED;
    }
    
    ret = TRUE;

FAILED:

    if (p_buf)
    {
        free(p_buf);
    }
    
    http_cln_free_req(p_http);

    if (p_node)
    {
        xml_node_del(p_node);
    }
    
    return ret;    
}

BOOL http_onvif_file_upload_req(HTTPREQ * p_http, const char * filename)
{
    int slen, rlen;
    int filesize, offset = 0, pktsize;
    char bufs[4*1024];
    int buflen = sizeof(bufs);
    FILE * fp = NULL;
    
    fp = fopen(filename, "rb");
    if (NULL == fp)
    {
        return FALSE;
    }
    
    fseek(fp, 0, SEEK_END);
    
    filesize = ftell(fp);
    if (filesize <= 0)
    {
        fclose(fp);
        return FALSE;
    }
    fseek(fp, 0, SEEK_SET);

    if (p_http->family == AF_INET6)
    {
        offset += snprintf(bufs+offset, buflen-offset, 
                "POST %s HTTP/1.1\r\n"
                "Host: [%s]:%d\r\n",
                p_http->url,
                p_http->host, p_http->port);
    }
    else
    {

        offset += snprintf(bufs+offset, buflen-offset, 
                "POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n",
                p_http->url,
                p_http->host, p_http->port);
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "User-Agent: Happytimes onvif client %s\r\n", 
                ONVIF_CLIENT_VERSION);

    if (p_http->need_auth)
    {
        offset += http_build_auth_msg(p_http, "POST", bufs+offset, buflen-offset);
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n",
                filesize);

    log_print(HT_LOG_DBG, "TX >> %s\r\n", bufs);
    
    slen = http_cln_tx(p_http, bufs, offset);
    if (slen != offset)
    {
        fclose(fp);
        return FALSE;
    }

    offset = 0;
    pktsize = sizeof(bufs);

    while (offset < filesize)
    {
        rlen = (int)fread(bufs, 1, pktsize, fp);
        if (rlen > 0)
        {
            slen = http_cln_tx(p_http, bufs, rlen);
            if (slen != rlen)
            {
                break;
            }
            else
            {
                offset += slen;
            }
        }

        if (rlen < pktsize)
        {
            break;
        }
    }

    fclose(fp);

    return (offset == filesize) ? TRUE : FALSE;    
}

HT_API BOOL http_onvif_file_upload(HTTPREQ * p_http, int timeout, const char * filename, ONVIF_DEVICE * p_dev)
{
    BOOL ret = FALSE;
    HT_SOCKADDR sockaddr;
    
RECONN:

    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        goto FAILED;
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
        // connect failed, retry the binfo connect address
        if (strcasecmp(p_http->host, p_dev->binfo.XAddr.host))
        {
            p_http->https = p_dev->binfo.XAddr.https;
            p_http->port = p_dev->binfo.XAddr.port;
            strcpy(p_http->host, p_dev->binfo.XAddr.host);

            goto RECONN;
        }
        
        log_print(HT_LOG_ERR, "%s, connnect to %s:%u failed\r\n", __FUNCTION__, p_http->host, p_http->port);
        goto FAILED;
    }

    if (p_http->https)
    {
#ifdef HTTPS
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            log_print(HT_LOG_ERR, "%s, http_cln_ssl_conn failed!\r\n", __FUNCTION__);
            goto FAILED;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif        
    }

    if (!http_onvif_file_upload_req(p_http, filename))
    {
        log_print(HT_LOG_ERR, "%s, send req failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (!http_cln_rx_timeout(p_http, timeout))
    {
        log_print(HT_LOG_ERR, "%s, data rx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (p_http->rx_msg->msg_sub_type == 401 && p_http->need_auth == FALSE)
    {
        http_cln_auth_set(p_http, NULL, NULL);

        http_cln_free_req(p_http);
        
        goto RECONN;
    }
    
    ret = (p_http->rx_msg->msg_sub_type == 200) ? TRUE : FALSE;

FAILED:
    
    http_cln_free_req(p_http);

    return ret;    
}

BOOL http_onvif_download_req(HTTPREQ * p_http)
{
    int ret;
    int offset = 0;
    char bufs[2048] = {'\0'};
    int buflen = sizeof(bufs);

    if (p_http->family == AF_INET6)
    {
        offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: [%s]:%d\r\n",
                p_http->url,
                p_http->host, p_http->port);
    }
    else
    {
        offset += snprintf(bufs+offset, buflen-offset, 
                "GET %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n",
                p_http->url,
                p_http->host, p_http->port);
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "User-Agent: Happytime onvif client %s\r\n", 
                ONVIF_CLIENT_VERSION);

    if (p_http->need_auth)
    {
        offset += http_build_auth_msg(p_http, "GET", bufs+offset, buflen-offset);
    }
    
    offset += snprintf(bufs+offset, buflen-offset, 
                "Connection: close\r\n"
                "\r\n");

    log_print(HT_LOG_DBG, "TX >> %s\r\n", bufs);

    ret = http_cln_tx(p_http, bufs, offset);
    
    return (ret == offset) ? TRUE : FALSE;
}

HT_API BOOL http_onvif_download(HTTPREQ * p_http, int timeout, uint8 ** pp_buf, int * buflen, ONVIF_DEVICE * p_dev)
{
    BOOL ret = FALSE;
    char * p_ctx;
    HT_SOCKADDR sockaddr;
    
    if (NULL == pp_buf || NULL == buflen)
    {
        return FALSE;
    }

RECONN:

    if (!get_address_by_name(p_http->host, HT_PROTOCOL_TCP, &sockaddr))
    {
        goto FAILED;
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
        // connect failed, retry the binfo connect address
        if (strcasecmp(p_http->host, p_dev->binfo.XAddr.host))
        {
            p_http->https = p_dev->binfo.XAddr.https;
            p_http->port = p_dev->binfo.XAddr.port;
            strcpy(p_http->host, p_dev->binfo.XAddr.host);

            goto RECONN;
        }
        
        log_print(HT_LOG_ERR, "%s, connnect to %s:%u failed\r\n", __FUNCTION__, p_http->host, p_http->port);
        goto FAILED;
    }

    if (p_http->https)
    {
#ifdef HTTPS
        if (!http_cln_ssl_conn(p_http, timeout))
        {
            log_print(HT_LOG_ERR, "%s, http_cln_ssl_conn failed!\r\n", __FUNCTION__);
            goto FAILED;
        }
#else
        log_print(HT_LOG_ERR, "%s, the server require ssl connection, unsupport!\r\n", __FUNCTION__);
        goto FAILED;
#endif        
    }

    if (!http_onvif_download_req(p_http))
    {
        log_print(HT_LOG_ERR, "%s, send request failed\r\n", __FUNCTION__);
        goto FAILED;
    }
    
    if (!http_cln_rx_timeout(p_http, timeout))
    {
        log_print(HT_LOG_ERR, "%s, data rx failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    if (p_http->rx_msg->msg_sub_type == 401 && p_http->need_auth == FALSE)
    {
        http_cln_auth_set(p_http, NULL, NULL);

        http_cln_free_req(p_http);
        
        goto RECONN;
    }
    
    if (p_http->rx_msg->msg_sub_type != 200)
    {
        log_print(HT_LOG_ERR, "%s, msg_sub_type=%u\r\n", 
            __FUNCTION__, p_http->rx_msg->msg_sub_type);
        goto FAILED;
    }
    
    p_ctx = http_get_ctt(p_http->rx_msg);
    if (NULL == p_ctx)
    {
        log_print(HT_LOG_ERR, "%s, http_get_ctt failed\r\n", __FUNCTION__);
        goto FAILED;
    }

    *buflen = p_http->ctt_len;
    *pp_buf = (uint8 *) malloc(*buflen);
    if (*pp_buf)
    {
        ret = TRUE;
        memcpy(*pp_buf, p_ctx, *buflen);
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
    }

FAILED:

    http_cln_free_req(p_http);

    return ret;
}



