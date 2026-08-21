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
#include "onvif_event.h"
#include "soap_parser.h"
#include "onvif_cln.h"
#include "http_srv.h"


/***************************************************************************************/
ONVIF_EVENT_CLS g_event_cls;


/***************************************************************************************/

void onvif_event_http_process(HTTPCLN * p_user, HTTPMSG * rx_msg)
{
    char * p_xml;
    XMLN * p_node;
    XMLN * p_body;

    p_xml = http_get_ctt(rx_msg);
    if (NULL == p_xml)
    {
        log_print(HT_LOG_ERR, "%s, http_get_ctt ret null!!!\r\n", __FUNCTION__);
        return;
    }

    p_node = xxx_hxml_parse(p_xml, rx_msg->ctt_len);
    if (NULL == p_node || NULL == p_node->name)
    {
        log_print(HT_LOG_ERR, "%s, xxx_hxml_parse ret null!!!\r\n", __FUNCTION__);
        return;
    }
    
    if (soap_strcmp(p_node->name, "Envelope") != 0)
    {
        log_print(HT_LOG_ERR, "%s, node name[%s] != [s:Envelope]!!!\r\n", __FUNCTION__, p_node->name);
        xml_node_del(p_node);
        return;
    }
    
    p_body = xml_node_soap_get(p_node, "Body");
    if (NULL == p_body)
    {
        log_print(HT_LOG_ERR, "%s, xml_node_soap_get[s:Body] ret null!!!\r\n", __FUNCTION__);
        xml_node_del(p_node);
        return;
    }

    if (NULL == p_body->f_child)
    {
        log_print(HT_LOG_ERR, "%s, body first child node is null!!!\r\n", __FUNCTION__);
    }    
    else if (NULL == p_body->f_child->name)
    {
        log_print(HT_LOG_ERR, "%s, body first child node name is null!!!\r\n", __FUNCTION__);
    }    
    else
    {
        log_print(HT_LOG_DBG, "%s, body first child node name[%s].\r\n", __FUNCTION__, p_body->f_child->name);

        if (soap_strcmp(p_body->f_child->name, "Notify") == 0)
        {
            XMLN * p_Notify = xml_node_soap_get(p_body, "Notify");
            if (p_Notify)
            {
                onvif_event_notify(p_user, rx_msg, p_Notify);
            }
        }
    }

    xml_node_del(p_node);
}

BOOL onvif_event_http_cb(HTTPCLN * p_cln, HTTPMSG * p_msg, void * p_userdata)
{
    if (p_msg)
    {
        onvif_event_http_process(p_cln, p_msg);
        
        http_free_msg(p_msg);
    }

    if (p_cln)
    {
        http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
    }
    
    return TRUE;
}

HT_API void onvif_set_event_notify_cb(onvif_event_notify_cb cb, void * pdata)
{
    sys_os_mutex_enter(g_event_cls.notify_cb_mutex);
    
    g_event_cls.notify_cb = cb;
    g_event_cls.notify_cb_data = pdata;

    sys_os_mutex_leave(g_event_cls.notify_cb_mutex);
}

HT_API void onvif_set_subscribe_disconnect_cb(onvif_subscribe_disconnect_cb cb, void * pdata)
{
    sys_os_mutex_enter(g_event_cls.disconnect_cb_mutex);
    
    g_event_cls.disconnect_cb = cb;
    g_event_cls.disconnect_cb_data = pdata;

    sys_os_mutex_leave(g_event_cls.disconnect_cb_mutex);
}

HT_API void onvif_event_notify(HTTPCLN * p_user, HTTPMSG * rx_msg, XMLN * p_xml)
{
    Notify_REQ req;
    
    memset(&req, 0, sizeof(req));
    strncpy(req.PostUrl, rx_msg->first_line.value_string, sizeof(req.PostUrl)-1);

    if (!parse_NotifyMessage(p_xml, &req.notify))
    {
        return;
    }

    sys_os_mutex_enter(g_event_cls.notify_cb_mutex);
    
    if (g_event_cls.notify_cb)    
    {
        g_event_cls.notify_cb(&req, g_event_cls.notify_cb_data);
    }

    sys_os_mutex_leave(g_event_cls.notify_cb_mutex);
}


void onvif_event_timer()
{
    int i;
    tev_Renew_REQ req;
    ONVIF_DEVICE * p_dev;
    time_t curtime = time(NULL);    

    for (i = 0; i < g_event_cls.event_dev_max; i++)
    {
        ONVIF_TIMER_DEV * p_node = (ONVIF_TIMER_DEV *) pps_get_node_by_index(g_event_cls.event_dev_fl, i);
        if (NULL == p_node || !p_node->used_flag)
        {
            continue;
        }

        p_dev = p_node->p_dev;
        
        if (NULL == p_dev || !p_dev->events.subscribe)
        {
            continue;
        }
        
        if ((curtime - p_dev->events.renew_time) >= p_dev->events.init_term_time / 2)
        {
            req.TerminationTime = p_dev->events.init_term_time;
            
            if (!onvif_tev_Renew(p_dev, &req, NULL))
            {
                log_print(HT_LOG_ERR, "onvif event renew failed, %s\r\n", p_dev->binfo.XAddr.host);

                sys_os_mutex_enter(g_event_cls.disconnect_cb_mutex);
                
                if (g_event_cls.disconnect_cb)    
                {
                    p_dev->events.subscribe = FALSE;
                    
                    pps_ctx_ul_del(g_event_cls.event_dev_ul, p_node);    
                    pps_fl_push(g_event_cls.event_dev_fl, p_node);
                    
                    g_event_cls.disconnect_cb(p_dev, g_event_cls.disconnect_cb_data);
                }

                sys_os_mutex_leave(g_event_cls.disconnect_cb_mutex);
            }
            else
            {
                p_dev->events.renew_time = curtime;
            }
        }        
    }
    
}

#if __WINDOWS_OS__

#pragma comment(lib, "winmm.lib")

#ifdef _WIN64
void CALLBACK onvif_event_timer_win(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
#else
void CALLBACK onvif_event_timer_win(UINT uID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
#endif
{
    onvif_event_timer();
}

HT_API void onvif_event_timer_init()
{
    g_event_cls.timer_run = TRUE;
    
    g_event_cls.event_timer_id = timeSetEvent(1000, 0, onvif_event_timer_win, NULL, TIME_PERIODIC);
}

HT_API void onvif_event_timer_deinit()
{
    g_event_cls.timer_run = FALSE;
    
    if (g_event_cls.event_timer_id != 0)
    {
        timeKillEvent(g_event_cls.event_timer_id);
        g_event_cls.event_timer_id = 0;
    }
}

#elif __LINUX_OS__

void * onvif_event_timer_task(void * argv)
{
    struct timespec ts;
    
    while (g_event_cls.timer_run)
    {        
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        
        nanosleep(&ts, NULL);
        
        onvif_event_timer();
    }
    
    return NULL;
}

void onvif_event_timer_init()
{
    g_event_cls.timer_run = TRUE;
    
    pthread_t tid = sys_os_create_thread((void *)onvif_event_timer_task, NULL);
    if (tid == 0)
    {
        log_print(HT_LOG_ERR, "%s, create onvif_event_timer_task failed\r\n", __FUNCTION__);
        return;
    }

    g_event_cls.event_timer_id = tid;
}

void onvif_event_timer_deinit()
{
    g_event_cls.timer_run = FALSE;
    
    sys_os_wait_thread(&g_event_cls.event_timer_id);
}

#endif

HT_API BOOL onvif_event_init(BOOL http_enable, const char * http_srv_addr, uint16 http_srv_port, 
                        BOOL https_enable, const char * https_srv_addr, uint16 https_srv_port, 
                        const char * cert_file, const char * key_file, int ipv6, int max_clients)
{
    if (g_event_cls.init_flag)
    {
        return TRUE;
    }
    
    memset(&g_event_cls, 0, sizeof(ONVIF_EVENT_CLS));

    if (http_enable)
    {
        if (!http_srv_init(&g_event_cls.http_srv, http_srv_addr, http_srv_port, max_clients, 0, NULL, NULL, ipv6))
        {
            return FALSE;
        }
        else
        {
            http_set_msg_cb(&g_event_cls.http_srv, onvif_event_http_cb, NULL);
        }
    }

    if (https_enable)
    {
#ifdef HTTPS
        if (!http_srv_init(&g_event_cls.https_srv, https_srv_addr, https_srv_port, max_clients, 1, cert_file, key_file, ipv6))
        {
            return FALSE;
        }
        else
        {
            http_set_msg_cb(&g_event_cls.https_srv, onvif_event_http_cb, NULL);
        }
#endif
    }

    if (http_srv_addr && http_srv_addr[0] != '\0')
    {
        strncpy(g_event_cls.http_host, http_srv_addr, sizeof(g_event_cls.http_host)-1);
    }

    if (https_srv_addr && https_srv_addr[0] != '\0')
    {
        strncpy(g_event_cls.https_host, https_srv_addr, sizeof(g_event_cls.https_host)-1);
    }

    g_event_cls.http_enable = http_enable;
#ifdef HTTPS
    g_event_cls.https_enable = https_enable;
#endif

    g_event_cls.notify_cb_mutex = sys_os_create_mutex();
    g_event_cls.disconnect_cb_mutex = sys_os_create_mutex();
    
    g_event_cls.event_dev_max = max_clients * 2;
    g_event_cls.event_dev_fl = pps_ctx_fl_init(g_event_cls.event_dev_max, sizeof(ONVIF_TIMER_DEV), TRUE);
    g_event_cls.event_dev_ul = pps_ctx_ul_init(g_event_cls.event_dev_fl, TRUE);
    
    onvif_event_timer_init();

    g_event_cls.init_flag = 1;
    
    return TRUE;
}

HT_API void onvif_event_deinit()
{
    onvif_event_timer_deinit();

    http_srv_deinit(&g_event_cls.http_srv);

    http_srv_deinit(&g_event_cls.https_srv);

    if (g_event_cls.event_dev_ul)
    {
        pps_ul_free(g_event_cls.event_dev_ul);
        g_event_cls.event_dev_ul = NULL;
    }
    
    if (g_event_cls.event_dev_fl)
    {
        pps_fl_free(g_event_cls.event_dev_fl);    
        g_event_cls.event_dev_fl = NULL;
    }

    sys_os_destroy_mutex(g_event_cls.notify_cb_mutex);

    sys_os_destroy_mutex(g_event_cls.disconnect_cb_mutex);
}

ONVIF_TIMER_DEV * onvif_event_dev_find(ONVIF_DEVICE * p_dev)
{
    ONVIF_TIMER_DEV * p_node = (ONVIF_TIMER_DEV *)pps_lookup_start(g_event_cls.event_dev_ul);
    while (p_node)
    {
        if (p_node->p_dev == p_dev)
        {
            break;
        }

        p_node = (ONVIF_TIMER_DEV *)pps_lookup_next(g_event_cls.event_dev_ul, p_node);
    }
    pps_lookup_end(g_event_cls.event_dev_ul);

    return p_node;
}

HT_API BOOL onvif_event_timer_add(ONVIF_DEVICE * p_dev)
{
    if (NULL == g_event_cls.event_dev_fl)
    {
        return FALSE;
    }
    
    ONVIF_TIMER_DEV * p_node = (ONVIF_TIMER_DEV *)pps_fl_pop(g_event_cls.event_dev_fl);
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_node->used_flag = TRUE;
    p_node->p_dev = p_dev;
    
    return pps_ctx_ul_add(g_event_cls.event_dev_ul, p_node);
}

HT_API BOOL onvif_event_timer_del(ONVIF_DEVICE * p_dev)
{
    if (NULL == g_event_cls.event_dev_fl)
    {
        return FALSE;
    }
    
    ONVIF_TIMER_DEV * p_node = onvif_event_dev_find(p_dev);
    if (p_node)
    {
        pps_ctx_ul_del(g_event_cls.event_dev_ul, p_node);
    
        pps_fl_push(g_event_cls.event_dev_fl, p_node);
    }

    return TRUE;
}



