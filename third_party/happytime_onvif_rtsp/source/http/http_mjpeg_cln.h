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

#ifndef HTTP_MJPEG_CLN_H
#define HTTP_MJPEG_CLN_H

#include "http.h"
#include "http_parse.h"


typedef int (*mjpeg_notify_cb)(int, void *);
typedef int (*mjpeg_video_cb)(uint8 *, int, void *);

#define MJPEG_EVE_STOPPED    20
#define MJPEG_EVE_CONNECTING 21
#define MJPEG_EVE_CONNFAIL   22
#define MJPEG_EVE_CONNSUCC   23
#define MJPEG_EVE_NOSIGNAL   24
#define MJPEG_EVE_RESUME     25
#define MJPEG_EVE_AUTHFAILED 26
#define MJPEG_EVE_NODATA     27

#define MJPEG_RX_ERR         -1
#define MJPEG_PARSE_ERR      -2
#define MJPEG_AUTH_ERR       -3
#define MJPEG_MALLOC_ERR     -4
#define MJPEG_MORE_DATA      0
#define MJPEG_RX_SUCC        1
#define MJPEG_NEED_AUTH      2


class CHttpMjpeg
{
public:
    CHttpMjpeg(void);
    ~CHttpMjpeg(void);

public:
    BOOL    mjpeg_start(const char * url, const char * user, const char * pass);
    BOOL    mjpeg_stop();
    BOOL    mjpeg_close();

    char *  get_url() {return m_url;}
    char *  get_user() {return m_http.auth_info.auth_name;}
    char *  get_pass() {return m_http.auth_info.auth_pwd;}

    void    set_notify_cb(mjpeg_notify_cb notify, void * userdata);
    void    set_video_cb(mjpeg_video_cb cb);
    void    set_rx_timeout(int timeout);

    void    rx_thread();
    
private:
    BOOL    mjpeg_req(HTTPREQ * p_http);
    BOOL    mjpeg_conn(HTTPREQ * p_http, int timeout);
    void    mjpeg_data_rx(uint8 * data, int len);
    int     mjpeg_parse_header(HTTPREQ * p_http);
    int     mjpeg_parse_header_ex(HTTPREQ * p_http);
    int     mjpeg_rx(HTTPREQ * p_http);

    void    send_notify(int event);
    
private:
    HTTPREQ         m_http;
    BOOL            m_running;
    pthread_t       m_rx_tid;
    BOOL            m_header;
    char            m_url[512];

    mjpeg_notify_cb m_pNotify;
    void          * m_pUserdata;
    mjpeg_video_cb  m_pVideoCB;
    void          * m_pMutex;
    int             m_nRxTimeout;
};

#endif // end of HTTP_MJPEG_CLN_H


