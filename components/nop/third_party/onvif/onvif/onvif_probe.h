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

#ifndef __H_ONVIF_PROBE_H__
#define __H_ONVIF_PROBE_H__

#include "onvif.h"

#define PROBE_MSGTYPE_MATCH     0
#define PROBE_MSGTYPE_HELLO     1
#define PROBE_MSGTYPE_BYE       2

#define MAX_PROBE_FD            8

/**
 * @brief
 *  onvif probe callback function prototype.
 *
 * @param p_res 
 *  the DEVICE_BINFO struct pointer
 * @param pdata 
 *  user data
 * @param msgypte
 *   PROBE_MSGTYPE_MATCH - probe match
 *   PROBE_MSGTYPE_HELLO - hello 
 *   PROBE_MSGTYPE_BYE - bye
 *   
 *  if msgtype = PROBE_MSGTYPE_BYE, only p_res->EndpointReference field 
 *  is valid, the other fields is invalid
 *
 **/
typedef void (* onvif_probe_cb)(DEVICE_BINFO * p_res, int msgtype, void * pdata);

typedef struct
{
    onvif_probe_cb  probe_cb;
    void *          probe_cb_data;
    void *          probe_mutex;
    pthread_t       probe_thread;
    SOCKET          probe_fd[MAX_PROBE_FD];
    int             probe_interval;
    BOOL            probe_running;
    char            monitor_reference[100];
    int             monitor_msgtype;
    onvif_probe_cb  monitor_cb;
    void *          monitor_cb_data;
    void *          monitor_mutex;
} ONVIF_PROBE_CLS;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 *  Set monitor callback function
 * 
 * @param reference
 *  endpoint reference
 * @param msgtype
 *  probe message type
 * @param cb
 *  callback function
 * @param pdata
 *  user data
 *
 **/
HT_API void set_monitor_cb(char * reference, int msgtype, onvif_probe_cb cb, void * pdata);

/**
 * @brief
 *  Set probe callback function
 * 
 * @param cb
 *  callback function
 * @param pdata
 *  user data
 *
 **/
HT_API void set_probe_cb(onvif_probe_cb cb, void * pdata);

/**
 * @brief
 *  Set probe interval
 * 
 * @param interval
 *  probe interval, unit is second, default is 30s
 *
 **/
HT_API void set_probe_interval(int interval);

/**
 * @brief
 *  Start probe thread
 * 
 * @param ip
 *  start probe thread on the specify ip address, it can be NULL, 
 *  if ip is NULL, it will use the default ip.
 * @param interval
 *  probe interval, unit is second, default is 30s
 *
 **/
HT_API int  start_probe(const char * ip, int interval);

/**
 * @brief
 *  Stop probe thread
 * 
 **/
HT_API void stop_probe();

/**
 * @brief
 *  Send probe request
 * 
 **/
HT_API void send_probe_req();


#ifdef __cplusplus
}
#endif

#endif



