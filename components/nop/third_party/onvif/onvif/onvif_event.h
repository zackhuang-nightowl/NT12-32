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

#ifndef ONVIF_EVENT_H
#define ONVIF_EVENT_H

#include "sys_inc.h"
#include "onvif.h"
#include "xml_node.h"
#include "onvif_req.h"
#include "http.h"

typedef struct
{
    BOOL    used_flag;                  // used flag
    
    ONVIF_DEVICE * p_dev;               // pointer to ONVIF_DEVICE
} ONVIF_TIMER_DEV;

typedef void (* onvif_event_notify_cb)(Notify_REQ * p_req, void * pdata);
typedef void (* onvif_subscribe_disconnect_cb)(ONVIF_DEVICE * p_dev, void * pdata);

typedef struct
{
    uint32  init_flag   : 1;            // init flag
    uint32  http_enable : 1;            // http enable flag
    uint32  https_enable: 1;            // https enable flag
    uint32  timer_run   : 1;            // timer running flag
    uint32  reserved    : 28;
    
    onvif_event_notify_cb notify_cb;    // event notify callback
    void  * notify_cb_data;             // event notify callback data
    void  * notify_cb_mutex;            // event notify callback mutex

    onvif_subscribe_disconnect_cb disconnect_cb;
    void  * disconnect_cb_data;         // disconnect callback data
    void  * disconnect_cb_mutex;        // disconnect callback mutex

    char        http_host[128];         // http host
    char        https_host[128];        // https host
    HTTPSRV     http_srv;               // http server for receiving event notify
    HTTPSRV     https_srv;              // https server for receiving event notify
    HTIMER      event_timer_id;         // event timer id
    PPSN_CTX  * event_dev_fl;           // event device free list
    PPSN_CTX  * event_dev_ul;           // event device used list
    int         event_dev_max;          // max event devices
} ONVIF_EVENT_CLS;


#ifdef __cplusplus
extern "C" {
#endif


/**
 * init event handler
 *
 * init http and https server for receiving event notify messages
 *
 * http_enable : http enable flag
 * http_srv_addr : http server listen address, NULL means listening on all interfaces 
 * http_srv_port : http server listen port
 * https_enable : https enable flag
 * https_srv_addr : https server listen address, NULL means listening on all interfaces 
 * https_srv_port : https server listen port
 * cert_file : cert file for https 
 * key_file : key file for https
 * ipv6 : is IPv6 enabled for HTTP and HTTPS
 * max_clients : max support client numbers
 *
 */
HT_API BOOL onvif_event_init(BOOL http_enable, const char * http_srv_addr, uint16 http_srv_port, 
                        BOOL https_enable, const char * https_srv_addr, uint16 https_srv_port, 
                        const char * cert_file, const char * key_file, int ipv6, int max_clients);

/**
 * deinit event handler
 */
HT_API void onvif_event_deinit();

/**
 * set event notify callback
 *
 * set cb to NULL, disable callback
 */
HT_API void onvif_set_event_notify_cb(onvif_event_notify_cb cb, void * pdata);

/**
 * set event subscribe disconnect callback
 *
 * set cb to NULL, disable callback
 */
HT_API void onvif_set_subscribe_disconnect_cb(onvif_subscribe_disconnect_cb cb, void * pdata);

/**
 * event notify handler, called by http_soap_process
 */
HT_API void onvif_event_notify(HTTPCLN * p_user, HTTPMSG * rx_msg, XMLN * p_xml);

/**
 * add p_dev to event timer list, send renew request
 */
HT_API BOOL onvif_event_timer_add(ONVIF_DEVICE * p_dev);

/**
 * del p_dev from event timer list
 */
HT_API BOOL onvif_event_timer_del(ONVIF_DEVICE * p_dev);

#ifdef __cplusplus
}
#endif


#endif


