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

#ifndef _RTSP_SRV_H
#define _RTSP_SRV_H

#include "rtsp_rsua.h"
#include "hqueue.h"
#ifdef RTSP_OVER_HTTP
#include "http.h"
#endif


/**************************************************************************************/
#define NET_IF_NUM          8
#define SRV_PORT_NUM        4

#define RTSP_MSG_SRC        1
#define RTSP_TIMER_SRC      2
#define RTSP_DEL_UA_SRC     3
#define RTSP_EXIT           4

#define RTSP_PARSE_FAIL     -1          // rtsp message parse failed 
#define RTSP_PARSE_MOREDATA 0           // need more data to parse rtsp message
#define RTSP_PARSE_SUCC     1           // rtsp message parse success

#define RTSP_VERSION_STRING "V7.8"

#define RTSP_LOG_FILE       "rtspserver.log"

#define RTSP_SOCKETS        60

/**************************************************************************************/

typedef struct
{
    HT_SOCKADDR     sockaddr;           // server socket address
    SOCKET          ipv4_fd;            // ipv4 socket
    SOCKET          ipv6_fd;            // ipv6 socket
    char            ipv4_host[64];      // ipv4 local server address
    char            ipv6_host[128];     // ipv6 local server address

#ifdef RTSPS
    void          * ssl_ctx;            // ssl context
#endif
} RTSP_SRV;

typedef struct rtsp_class
{
    uint32          sys_init_flag  : 8; // system init flag
    uint32          sys_timer_run  : 1; // system timer running flag
    uint32          sys_run_flag   : 1; // system running flag
    uint32          data_rx_flag   : 1; // network data receive flag
    uint32          reserved       : 21;

    HQUEUE        * msg_queue;          // message receive queue
    PPSN_CTX      * rua_fl;             // rua free list
    PPSN_CTX      * rua_ul;             // rua used list
    PPSN_CTX      * rmc_fl;             // rmc free list
    PPSN_CTX      * rmc_ul;             // rmc used list
    void          * rmc_mutex;          // rmc mutex
    char            srv_ver[64];        // happytimesoft rtsp server 1.0
    uint32          num_pkt_rx;         // packet receive thread numbers
    pthread_t     * tid_pkt_rx;         // packet receive thread
    pthread_t       tid_main;           // main task thread
    HTIMER          timer_id;           // timer task
    uint32          session_timeout;    // sesssion timeout, unit is second

    RTSP_SRV        rtsp;

#ifdef RTSPS    
    RTSP_SRV        rtsps;
#endif

#ifdef EPOLL
    int             ep_fd;
    struct epoll_event * ep_events;
    int             ep_event_num;
#endif

#ifdef RTSP_OVER_HTTP
    HTTPSRV         http_srv;           // http server
#ifdef HTTPS
    HTTPSRV         https_srv;          // https server
#endif
#endif
} RTSP_CLASS;


/*************************************************************************/
typedef struct rtsp_internal_msg
{
    uint32          msg_dua;            // message destination unit
    uint32          msg_evt;            // event / command value
    uint32          msg_src;            // message type
    int             msg_len;            // message buffer length
    char          * msg_buf;            // message buffer
} RTSPMSG;

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************/
BOOL    rtsp_start1();
BOOL    rtsp_start(const char * config);
void    rtsp_stop();
void *  rtsp_task(void * argv);
void *  rtsp_rx_thread(void * argv);
int     rtsp_rx_msg(RSSUA * p_rua, HRTSP_MSG * rx_msg);
int     rtsp_server_state(RSSUA * p_rua, HRTSP_MSG * rx_msg);
void    rtsp_stop_rua(RSSUA * p_rua);
void    rtsp_close_rua(RSSUA * p_rua);
BOOL    rtsp_tcp_rx(RSSUA * p_rua);
BOOL    rtsp_srv_rx(RSSUA * p_rua);
int     rtsp_srv_ssl_tx(RSSUA * p_rua, const char * p_data, int len);
BOOL    rtsp_data_parser(RSSUA * p_rua);
int     rtsp_msg_parser(RSSUA * p_rua);
void    rtsp_tcp_data_rx(RSSUA * p_rua, uint8 * data, int rlen);

#ifdef __cplusplus
}
#endif

#endif  // _RTSP_SRV_H




