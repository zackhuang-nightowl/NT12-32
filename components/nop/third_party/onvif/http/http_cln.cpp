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
#include "rfc_md5.h"
#include "sha256.h"
#include "base64.h"
#ifdef HTTPS
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif

/***************************************************************************************/

#define MAX_CTT_LEN     (2*1024*1024)


/***************************************************************************************/

static inline int http_isspace(int c)
{
    return c == ' ' || c == '\f' || c == '\n' || 
           c == '\r' || c == '\t' || c == '\v';
}

void http_choose_qop(char * qop, int size)
{
    char * ptr = strstr(qop, "auth");
    char * end = ptr + strlen("auth");

    if (ptr && (!*end || http_isspace(*end) || *end == ',') &&
        (ptr == qop || http_isspace(ptr[-1]) || ptr[-1] == ',')) 
    {
        strncpy(qop, "auth", size);
    } 
    else 
    {
        qop[0] = 0;
    }
}

BOOL http_get_digest_params(char * p, int len, HD_AUTH_INFO * p_auth)
{
    char word_buf[128];
    
    if (get_name_value_pair(p, len, "algorithm", word_buf, sizeof(word_buf)))
    {
        strncpy(p_auth->auth_algorithm, word_buf, sizeof(p_auth->auth_algorithm)-1);
    }
    else
    {
        p_auth->auth_algorithm[0] = '\0';
    }
    
    if (get_name_value_pair(p, len, "realm", word_buf, sizeof(word_buf)))
    {
        strncpy(p_auth->auth_realm, word_buf, sizeof(p_auth->auth_realm)-1);
    }
    else
    {
        return FALSE;
    }

    if (get_name_value_pair(p, len, "nonce", word_buf, sizeof(word_buf)))
    {
        strncpy(p_auth->auth_nonce, word_buf, sizeof(p_auth->auth_nonce)-1);
    }
    else
    {
        return FALSE;
    }

    if (get_name_value_pair(p, len, "qop", word_buf, sizeof(word_buf)))
    {
        strncpy(p_auth->auth_qop, word_buf, sizeof(p_auth->auth_qop)-1);
    }
    else
    {
        p_auth->auth_qop[0] = '\0';
    }

    http_choose_qop(p_auth->auth_qop, sizeof(p_auth->auth_qop));
    
    if (get_name_value_pair(p, len, "opaque", word_buf, sizeof(word_buf)))
    {
        p_auth->auth_opaque_flag = 1;
        strncpy(p_auth->auth_opaque, word_buf, sizeof(p_auth->auth_opaque)-1);
    }
    else
    {
        p_auth->auth_opaque_flag = 0;
        p_auth->auth_opaque[0] = '\0';
    }
    
    return TRUE;
}

BOOL http_get_digest_info(HTTPMSG * rx_msg, HD_AUTH_INFO * p_auth)
{
    int len;
    int next_offset;
    char word_buf[128];
    HDRV * chap_id = NULL;
    char * p;

    p_auth->auth_response[0] = '\0';

RETRY:

    if (chap_id)
    {
        chap_id = http_find_headline_next(rx_msg, "WWW-Authenticate", chap_id);
    }
    else
    {
        chap_id = http_find_headline(rx_msg, "WWW-Authenticate");
    }
    
    if (chap_id == NULL)
    {
        return FALSE;
    }

    get_line_word(chap_id->value_string, 0, (int)strlen(chap_id->value_string),
        word_buf, sizeof(word_buf), &next_offset, WORD_TYPE_STRING);
    if (strcasecmp(word_buf, "digest") != 0)
    {
        goto RETRY;
    }

    p = chap_id->value_string + next_offset;
    len = (int)strlen(chap_id->value_string) - next_offset;

    if (!http_get_digest_params(p, len, p_auth))
    {
        goto RETRY;
    }

    if (p_auth->auth_algorithm[0] != '\0' && 
        strncasecmp(p_auth->auth_algorithm, "MD5", 3) != 0 &&  
        strncasecmp(p_auth->auth_algorithm, "SHA-256", 7) != 0)
    {
        goto RETRY;
    }
    
    return TRUE;
}

BOOL http_calc_auth_md5_digest(HD_AUTH_INFO * p_auth, const char * method)
{
    uint8 ha1[16];
    uint8 ha2[16];
    uint8 ha3[16];
    char ha1hex[33];
    char ha2hex[33];
    char ha3hex[33];
    md5_context ctx;

    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)p_auth->auth_name, strlen(p_auth->auth_name));
    md5_update(&ctx, (uint8 *)&(":"), 1);
    md5_update(&ctx, (uint8 *)p_auth->auth_realm, strlen(p_auth->auth_realm));
    md5_update(&ctx, (uint8 *)&(":"), 1);
    md5_update(&ctx, (uint8 *)p_auth->auth_pwd, strlen(p_auth->auth_pwd));
    md5_finish(&ctx, ha1);

    bin_to_hex_str(ha1, 16, ha1hex, 33);

    if (!strcasecmp(p_auth->auth_algorithm, "MD5-sess"))
    {
        md5_starts(&ctx);
        md5_update(&ctx, (uint8 *)ha1hex, 32);
        md5_update(&ctx, (uint8 *)&(":"), 1);
        md5_update(&ctx, (uint8 *)p_auth->auth_nonce, strlen(p_auth->auth_nonce));
        md5_update(&ctx, (uint8 *)&(":"), 1);
        md5_update(&ctx, (uint8 *)p_auth->auth_cnonce, strlen(p_auth->auth_cnonce));
        md5_finish(&ctx, ha1);

        bin_to_hex_str(ha1, 16, ha1hex, 33);
    }
    
    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)method, strlen(method));
    md5_update(&ctx, (uint8 *)&(":"), 1);
    md5_update(&ctx, (uint8 *)p_auth->auth_uri, strlen(p_auth->auth_uri));
    md5_finish(&ctx, ha2);

    bin_to_hex_str(ha2, 16, ha2hex, 33);

    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)ha1hex, 32);
    md5_update(&ctx, (uint8 *)&(":"), 1);
    md5_update(&ctx, (uint8 *)p_auth->auth_nonce, strlen(p_auth->auth_nonce));
    md5_update(&ctx, (uint8 *)&(":"), 1);

    if (p_auth->auth_qop[0] != '\0') 
    {
        md5_update(&ctx, (uint8 *)p_auth->auth_ncstr, strlen(p_auth->auth_ncstr));
        md5_update(&ctx, (uint8 *)&(":"), 1);
        md5_update(&ctx, (uint8 *)p_auth->auth_cnonce, strlen(p_auth->auth_cnonce));
        md5_update(&ctx, (uint8 *)&(":"), 1);
        md5_update(&ctx, (uint8 *)p_auth->auth_qop, strlen(p_auth->auth_qop));
        md5_update(&ctx, (uint8 *)&(":"), 1);
    };
    
    md5_update(&ctx, (uint8 *)ha2hex, 32);
    md5_finish(&ctx, ha3);

    bin_to_hex_str(ha3, 16, ha3hex, 33);

    strcpy(p_auth->auth_response, ha3hex);
    
    return TRUE;
}

BOOL http_calc_auth_sha256_digest(HD_AUTH_INFO * p_auth, const char * method)
{
    uint8 ha1[32];
    uint8 ha2[32];
    uint8 ha3[32];
    char ha1hex[65];
    char ha2hex[65];
    char ha3hex[65];
    sha256_context ctx;

    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)p_auth->auth_name, strlen(p_auth->auth_name));
    sha256_update(&ctx, (uint8 *)&(":"), 1);
    sha256_update(&ctx, (uint8 *)p_auth->auth_realm, strlen(p_auth->auth_realm));
    sha256_update(&ctx, (uint8 *)&(":"), 1);
    sha256_update(&ctx, (uint8 *)p_auth->auth_pwd, strlen(p_auth->auth_pwd));
    sha256_finish(&ctx, ha1);

    bin_to_hex_str(ha1, 32, ha1hex, 65);

    if (!strcasecmp(p_auth->auth_algorithm, "SHA-256-sess"))
    {
        sha256_starts(&ctx);
        sha256_update(&ctx, (uint8 *)ha1hex, 64);
        sha256_update(&ctx, (uint8 *)&(":"), 1);
        sha256_update(&ctx, (uint8 *)p_auth->auth_nonce, strlen(p_auth->auth_nonce));
        sha256_update(&ctx, (uint8 *)&(":"), 1);
        sha256_update(&ctx, (uint8 *)p_auth->auth_cnonce, strlen(p_auth->auth_cnonce));
        sha256_finish(&ctx, ha1);

        bin_to_hex_str(ha1, 32, ha1hex, 65);
    }
    
    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)method, strlen(method));
    sha256_update(&ctx, (uint8 *)&(":"), 1);
    sha256_update(&ctx, (uint8 *)p_auth->auth_uri, strlen(p_auth->auth_uri));
    sha256_finish(&ctx, ha2);

    bin_to_hex_str(ha2, 32, ha2hex, 65);

    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)ha1hex, 64);
    sha256_update(&ctx, (uint8 *)&(":"), 1);
    sha256_update(&ctx, (uint8 *)p_auth->auth_nonce, strlen(p_auth->auth_nonce));
    sha256_update(&ctx, (uint8 *)&(":"), 1);

    if (p_auth->auth_qop[0] != '\0') 
    {
        sha256_update(&ctx, (uint8 *)p_auth->auth_ncstr, strlen(p_auth->auth_ncstr));
        sha256_update(&ctx, (uint8 *)&(":"), 1);
        sha256_update(&ctx, (uint8 *)p_auth->auth_cnonce, strlen(p_auth->auth_cnonce));
        sha256_update(&ctx, (uint8 *)&(":"), 1);
        sha256_update(&ctx, (uint8 *)p_auth->auth_qop, strlen(p_auth->auth_qop));
        sha256_update(&ctx, (uint8 *)&(":"), 1);
    };
    
    sha256_update(&ctx, (uint8 *)ha2hex, 64);
    sha256_finish(&ctx, ha3);

    bin_to_hex_str(ha3, 32, ha3hex, 65);

    strcpy(p_auth->auth_response, ha3hex);
    
    return TRUE;
}

BOOL http_calc_auth_digest(HD_AUTH_INFO * p_auth, const char * method)
{
    BOOL ret = FALSE;

    p_auth->auth_nc++;
    snprintf(p_auth->auth_ncstr, sizeof(p_auth->auth_ncstr), "%08X", p_auth->auth_nc);
    snprintf(p_auth->auth_cnonce, sizeof(p_auth->auth_cnonce), "%08X%08X", rand(), rand());
    
    if (p_auth->auth_algorithm[0] == '\0' || 
        strncasecmp(p_auth->auth_algorithm, "MD5", 3) == 0)
    {
        ret = http_calc_auth_md5_digest(p_auth, method);
    }
    else if (strncasecmp(p_auth->auth_algorithm, "SHA-256", 7) == 0)
    {
        ret = http_calc_auth_sha256_digest(p_auth, method);
    }

    return ret;
}

int http_build_auth_msg(HTTPREQ * p_http, const char * method, char * buff, int buflen)
{
    int offset = 0;
    HD_AUTH_INFO * p_auth = &p_http->auth_info;
    
    if (p_http->auth_mode == 1) // digest auth
    {
        http_calc_auth_digest(p_auth, method);

        offset += snprintf(buff+offset, buflen-offset, 
            "Authorization: Digest username=\"%s\", realm=\"%s\", "
            "nonce=\"%s\", uri=\"%s\", response=\"%s\"",
            p_auth->auth_name, p_auth->auth_realm, 
            p_auth->auth_nonce, p_auth->auth_uri, p_auth->auth_response);

        if (p_auth->auth_opaque_flag)
        {
            offset += snprintf(buff+offset, buflen-offset, 
                ", opaque=\"%s\"", p_auth->auth_opaque);
        }
        
        if (p_auth->auth_qop[0] != '\0')
        {
            offset += snprintf(buff+offset, buflen-offset, 
                ", qop=\"%s\", cnonce=\"%s\", nc=%s",
                p_auth->auth_qop, p_auth->auth_cnonce, p_auth->auth_ncstr);
        }

        if (p_auth->auth_algorithm[0] != '\0')
        {
            offset += snprintf(buff+offset, buflen-offset, 
                ", algorithm=%s", p_auth->auth_algorithm);
        }

        offset += snprintf(buff+offset, buflen-offset, "\r\n");
    }
    else if (p_http->auth_mode == 0)    // basic auth
    {
        char auth[132] = {'\0'};
        char basic[256] = {'\0'};
        
        snprintf(auth, sizeof(auth)-1, "%s:%s", p_auth->auth_name, p_auth->auth_pwd);
        
        base64_encode((uint8 *)auth, (int)strlen(auth), basic, sizeof(basic)-1);
        
        offset += snprintf(buff+offset, buflen-offset, "Authorization: Basic %s\r\n", basic);
    }

    return offset;
}

int http_cln_auth_set(HTTPREQ * p_http, const char * user, const char * pass)
{
    if (user)
    {
        strncpy(p_http->auth_info.auth_name, user, sizeof(p_http->auth_info.auth_name)-1);
    }

    if (pass)
    {
        strncpy(p_http->auth_info.auth_pwd, pass, sizeof(p_http->auth_info.auth_pwd)-1);
    }

    if (http_get_digest_info(p_http->rx_msg, &p_http->auth_info))
    {
        p_http->auth_mode = 1;  // digest
        strcpy(p_http->auth_info.auth_uri, p_http->url);
    }
    else
    {
        p_http->auth_mode = 0;  // basic
    }
    
    p_http->need_auth = TRUE;

    return p_http->auth_mode;
}

BOOL http_cln_ssl_conn(HTTPREQ * p_http, int timeout)
{
#ifdef HTTPS
    SSL_CTX * ctx = NULL;
    const SSL_METHOD * method = NULL;

    SSL_library_init();  
    SSL_load_error_strings();
    
    method = TLS_client_method();  
    ctx = SSL_CTX_new(method);
    if (NULL == ctx)
    {
        log_print(HT_LOG_ERR, "%s, SSL_CTX_new failed!, %s\r\n", __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    else
    {
        SSL_CTX_set_options(ctx, SSL_OP_ALL);
        SSL_CTX_set_default_verify_paths(ctx);
    }
    
#if __WINDOWS_OS__
    int tv = timeout;
#else
    struct timeval tv = {timeout / 1000, (timeout % 1000) * 1000};
#endif

    setsockopt(p_http->cfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    
    p_http->ssl = SSL_new(ctx); 
    if (NULL == p_http->ssl)
    {
        SSL_CTX_free(ctx);
        log_print(HT_LOG_ERR, "%s, SSL_new failed!, %s\r\n", __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    SSL_set_fd((SSL *)p_http->ssl, p_http->cfd); 

    if (SSL_connect((SSL*)p_http->ssl) == -1)
    {
        SSL_CTX_free(ctx);
        log_print(HT_LOG_ERR, "%s, SSL_connect failed!, %s\r\n", __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    SSL_CTX_free(ctx);

    p_http->ssl_mutex = sys_os_create_mutex();
    
    return TRUE;
#else
    return FALSE;
#endif
}

BOOL http_cln_rx(HTTPREQ * p_http)
{
    int rlen;
    HTTPMSG * rx_msg;

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

    if (0 == rlen)
    {
        if (p_http->rcv_dlen < p_http->ctt_len + p_http->hdr_len && p_http->ctt_len == MAX_CTT_LEN)
        {
            // without Content-Length filed, when recv finish, fix the ctt length
            p_http->ctt_len = p_http->rcv_dlen - p_http->hdr_len;
        }
        else
        {
            log_print(HT_LOG_INFO, "%s, recv return = %d, dlen[%d], mlen[%d]\r\n", 
                __FUNCTION__, rlen, p_http->rcv_dlen, p_http->mlen);
            return FALSE;
        }
    }

    if (p_http->rcv_dlen < 16)
    {
        return TRUE;
    }
    
    if (http_is_http_msg(p_http->rbuf) == FALSE)
    {
        return FALSE;
    }
    
    rx_msg = NULL;

    if (p_http->hdr_len == 0)
    {
        int parse_len;
        int http_pkt_len;

        http_pkt_len = http_pkt_find_end(p_http->rbuf);
        if (http_pkt_len == 0) 
        {
            return TRUE;
        }
        p_http->hdr_len = http_pkt_len;

        rx_msg = http_get_msg_buf(http_pkt_len+1);
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

        if (p_http->ctt_len == 0)
        {
            if (rx_msg->ctt_type == CTT_JPG || p_http->rcv_dlen > p_http->hdr_len)
            {
                // without Content-Length field
                p_http->ctt_len = MAX_CTT_LEN;
            }
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
            http_free_msg(rx_msg);
            return FALSE;
        }
        
        memcpy(p_http->dyn_recv_buf, p_http->rcv_buf, p_http->rcv_dlen);
        p_http->rbuf = p_http->dyn_recv_buf;
        p_http->mlen = p_http->ctt_len + p_http->hdr_len;

        http_free_msg(rx_msg);

        // Need more data
        return TRUE;
    }

    if (p_http->rcv_dlen >= (p_http->ctt_len + p_http->hdr_len))
    {
        if (rx_msg == NULL)
        {
            int nlen;
            int parse_len;

            nlen = p_http->ctt_len + p_http->hdr_len;
            
            rx_msg = http_get_msg_buf(nlen+1);
            if (rx_msg == NULL)
            {
                log_print(HT_LOG_ERR, "%s, get msg buf failed\r\n", __FUNCTION__);
                return FALSE;
            }

            memcpy(rx_msg->msg_buf, p_http->rbuf, p_http->hdr_len);
            rx_msg->msg_buf[p_http->hdr_len] = '\0';
            
            parse_len = http_msg_parse_part1(rx_msg->msg_buf, p_http->hdr_len, rx_msg);
            if (parse_len != p_http->hdr_len)
            {
                log_print(HT_LOG_ERR, "%s, http_msg_parse_part1=%d, hdr_len=%d!!!\r\n", 
                    __FUNCTION__, parse_len, p_http->hdr_len);

                http_free_msg(rx_msg);
                return FALSE;
            }
        }

        if (p_http->ctt_len > 0)
        {
            int parse_len;
            
            memcpy(rx_msg->msg_buf+p_http->hdr_len, p_http->rbuf+p_http->hdr_len, p_http->ctt_len);
            rx_msg->msg_buf[p_http->hdr_len + p_http->ctt_len] = '\0';

            if (ctt_is_string(rx_msg->ctt_type))
            {
                log_print(HT_LOG_DBG, "%s\r\n\r\n", rx_msg->msg_buf+p_http->hdr_len);
            }
            
            parse_len = http_msg_parse_part2(rx_msg->msg_buf+p_http->hdr_len, p_http->ctt_len, rx_msg);
            if (parse_len != p_http->ctt_len)
            {
                log_print(HT_LOG_ERR, "%s, http_msg_parse_part2=%d, ctt_len=%d!!!\r\n", 
                    __FUNCTION__, parse_len, p_http->ctt_len);

                http_free_msg(rx_msg);
                return FALSE;
            }
        }

        p_http->rx_msg = rx_msg;
    }

    if (p_http->rx_msg != rx_msg) 
    {
        http_free_msg(rx_msg);
    }
    
    return TRUE;
}

int http_cln_tx(HTTPREQ * p_http, const char * p_data, int len)
{
    int slen = 0;
    
    if (p_http->cfd <= 0)
    {
        return -1;
    }
    
#ifdef HTTPS
    if (p_http->https)
    {
        sys_os_mutex_enter(p_http->ssl_mutex);
        if (p_http->ssl)
        {
            slen = SSL_write((SSL*)p_http->ssl, p_data, len);
        }
        sys_os_mutex_leave(p_http->ssl_mutex);
    }
    else 
#endif
    {
#if __WINDOWS_OS__
        slen = send(p_http->cfd, p_data, len, 0);
#else
        slen = send(p_http->cfd, p_data, len, MSG_DONTWAIT);
#endif
    }
    
    return slen;
}

BOOL http_cln_rx_timeout(HTTPREQ * p_http, int timeout)
{
    int count = 0;
    int sret;
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
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            FD_ZERO(&fdr);
            FD_SET(p_http->cfd, &fdr);

            sret = select((int)(p_http->cfd+1), &fdr, NULL, NULL, &tv);
            if (sret == 0)
            {
                count++;
                
                if (count >= timeout / 1000)
                {
                    log_print(HT_LOG_WARN, "%s, timeout!!!\r\n", __FUNCTION__);
                    break;
                }
                
                continue;
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
        
        if (http_cln_rx(p_http) == FALSE)
        {
            log_print(HT_LOG_ERR, "%s, http_cln_rx failed\r\n", __FUNCTION__);
            break;
        }
        else if (p_http->rx_msg != NULL)
        {
            ret = TRUE;
            break;
        }
    }

    return ret;
}

void http_cln_free_req(HTTPREQ * p_http)
{
#ifdef HTTPS
    if (p_http->https)
    {
        sys_os_mutex_enter(p_http->ssl_mutex);
        if (p_http->ssl)
        {
            SSL_shutdown((SSL*)p_http->ssl);
            SSL_free((SSL*)p_http->ssl);
            p_http->ssl = NULL;
        }
        sys_os_mutex_leave(p_http->ssl_mutex);

        if (p_http->ssl_mutex)
        {
            sys_os_destroy_mutex(p_http->ssl_mutex);
            p_http->ssl_mutex = NULL;
        }
    }
#endif

    if (p_http->cfd > 0)
    {
        closesocket(p_http->cfd);
        p_http->cfd = 0;
    }
    
    if (p_http->dyn_recv_buf)
    {
        free(p_http->dyn_recv_buf);
        p_http->dyn_recv_buf = NULL;
    }
    
    if (p_http->rx_msg)
    {
        http_free_msg(p_http->rx_msg);
        p_http->rx_msg = NULL;
    }

    p_http->rcv_dlen = 0;
    p_http->hdr_len = 0;
    p_http->ctt_len = 0;
    p_http->rbuf = NULL;
    p_http->mlen = 0;
}






