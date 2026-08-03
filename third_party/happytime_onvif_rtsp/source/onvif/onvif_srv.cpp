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
#include "hxml.h"
#include "xml_node.h"
#include "onvif_probe.h"
#include "http.h"
#include "http_srv.h"
#include "http_parse.h"
#include "onvif_device.h"
#include "onvif.h"
#include "onvif_timer.h"
#include "onvif_srv.h"
#include "onvif_event.h"
#include "onvif_cfg.h"
#include "soap.h"
#ifdef PROFILE_G_SUPPORT
#include "onvif_recording.h"
#endif
#ifdef HTTPD
#include "httpd.h"
#endif
#ifdef RTSP_OVER_HTTP
#include "rtsp_http.h"
#endif
#ifdef RTSP_OVER_WEBSOCKET
#include "rtsp_srv_ws.h"
#endif

/***************************************************************************************/
extern ONVIF_CLS g_onvif_cls;
extern ONVIF_CFG g_onvif_cfg;

/***************************************************************************************/

BOOL onvif_http_msg_cb(HTTPCLN * p_cln, HTTPMSG * p_msg, void * p_userdata)
{
    BOOL ret = TRUE;
    
    if (p_msg)
    {
#ifdef RTSP_OVER_HTTP    
        if (http_get_headline(p_msg, "x-sessioncookie"))
        {
            ret = rtsp_http_msg_process(p_cln, p_msg);
            http_free_msg(p_msg);
        }
        else
#endif

#ifdef RTSP_OVER_WEBSOCKET
        if (http_get_headline(p_msg, "Sec-WebSocket-Key"))
        {
            ret = rtsp_ws_msg_process(p_cln, p_msg);
            http_free_msg(p_msg);
        }
        else
#endif
        {
            OIMSG msg;
            memset(&msg, 0, sizeof(OIMSG));
            
            msg.msg_src = ONVIF_MSG_SRC;
            msg.msg_dua = (char *)p_cln;
            msg.msg_buf = (char *)p_msg;
            
            if (hqueue_put(g_onvif_cls.msg_queue, (char *)&msg) == FALSE)
            {
                http_free_msg(p_msg);
                log_print(HT_LOG_ERR, "%s, send rx msg to main task failed!!!\r\n", __FUNCTION__);
                return  FALSE;
            }
        }
    }
    else if (p_cln)
    {
        OIMSG msg;
        memset(&msg, 0, sizeof(OIMSG));
        
        msg.msg_src = ONVIF_DEL_UA_SRC;
        msg.msg_dua = (char *)p_cln;
        
        if (hqueue_put(g_onvif_cls.msg_queue, (char *)&msg) == FALSE)
        {
            log_print(HT_LOG_ERR, "%s, send rx msg to main task failed!!!\r\n", __FUNCTION__);
            return  FALSE;
        }
    }
    
    return ret;
}

void onvif_http_data_cb(HTTPCLN * p_cln, char * buff, int buflen, void * p_userdata)
{
    BOOL ret = FALSE;

    sys_os_mutex_enter(p_cln->userdata_mutex);
    
#ifdef RTSP_OVER_HTTP        
    if (PROTO_RTSP_OVER_HTTP == p_cln->protocol)
    {
        ret = rtsp_http_data_process(p_cln, buff, buflen);
    }
#endif    
#ifdef RTSP_OVER_WEBSOCKET    
    else if (PROTO_RTSP_OVER_WEBSOCKET == p_cln->protocol)
    {
        ret = rtsp_ws_data_process(p_cln, buff, buflen);
    }
#endif

    sys_os_mutex_leave(p_cln->userdata_mutex);
    
    if (!ret)
    {
        OIMSG msg;
        memset(&msg, 0, sizeof(OIMSG));
        
        msg.msg_src = ONVIF_DEL_UA_SRC;
        msg.msg_dua = (char *)p_cln;
        
        if (hqueue_put(g_onvif_cls.msg_queue, (char *)&msg) == FALSE)
        {
            log_print(HT_LOG_ERR, "%s, send rx msg to main task failed!!!\r\n", __FUNCTION__);
        }
    }
}

void onvif_http_msg_handler(HTTPCLN * p_cln, HTTPMSG * p_msg)
{
    char * post = p_msg->first_line.value_string;

    if (strstr(post, "FirmwareUpgrade"))    // must be the same with onvif_StartFirmwareUpgrade
    {
        soap_FirmwareUpgrade(p_cln, p_msg);
    }
    else if (strstr(post, "SystemRestore"))    // must be the same with onvif_StartSystemRestore
    {
        soap_SystemRestore(p_cln, p_msg);
    }
    else if (strstr(post, "snapshot"))            
    {
        soap_GetSnapshot(p_cln, p_msg);
    }
    else if (strstr(post, "SystemLog"))
    {
        soap_GetHttpSystemLog(p_cln, p_msg);
    }
    else if (strstr(post, "AccessLog"))
    {
        soap_GetHttpAccessLog(p_cln, p_msg);
    }
    else if (strstr(post, "SupportInfo"))
    {
        soap_GetSupportInfo(p_cln, p_msg);
    }
    else if (strstr(post, "SystemBackup"))
    {
        soap_GetSystemBackup(p_cln, p_msg);
    }
    else if (p_msg->ctt_type == CTT_XML)
    {
        soap_process_request(p_cln, p_msg);
    }
#ifdef HTTPD    
    else
    {
        httpd_process_request(g_onvif_cls.httpd, p_cln, p_msg);
    }
#endif
}

void onvif_httpd_on_auth(char * username, BOOL * need_auth, BOOL * user_exist, char * password, void * userdata)
{
    *need_auth = g_onvif_cfg.need_auth;

    onvif_User * p_user = onvif_find_user(username);
    if (p_user)
    {
        *user_exist = TRUE;
        strcpy(password, p_user->Password);
    }
    else
    {
        *user_exist = FALSE;
    }
}

void * onvif_task(void * argv)
{
    OIMSG stm;

    while (g_onvif_cls.sys_run_flag)
    {
        if (hqueue_get(g_onvif_cls.msg_queue, (char *)&stm))
        {
            HTTPCLN * p_cln = (HTTPCLN *)stm.msg_dua;
            
            switch (stm.msg_src)
            {
            case ONVIF_MSG_SRC:
                onvif_http_msg_handler(p_cln, (HTTPMSG *)stm.msg_buf);

                if (stm.msg_buf)
                {
                    http_free_msg((HTTPMSG *)stm.msg_buf);
                }
                
                if (p_cln && !p_cln->keep_alive) 
                {
                    http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
                }
                break;

            case ONVIF_DEL_UA_SRC:
                http_free_used_cln((HTTPSRV *)p_cln->http_srv, p_cln);
                break;

            case ONVIF_TIMER_SRC:
                onvif_timer();
                break;

            case ONVIF_EXIT:
                goto EXIT;
            }
        }
    }

EXIT:
    
    return NULL;
}

void onvif_start1()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    ONVIF_CLS * p_class = &g_onvif_cls;
    
#ifdef NO_RTSP_SERVER
    sys_buf_init(p_config->http_max_users);
    http_msg_buf_init(p_config->http_max_users);
#endif

    onvif_init();

    printf("Happytime onvif server version %s\r\n", ONVIF_VERSION_STRING);

    p_class->msg_queue = hqueue_create(p_config->http_max_users * 4, sizeof(OIMSG), HQ_GET_WAIT);
    if (p_class->msg_queue == NULL)
    {
        log_print(HT_LOG_ERR, "%s, create task queue failed!!!\r\n", __FUNCTION__);
        return;
    }

    p_class->sys_run_flag = 1;
    p_class->tid_main = sys_os_create_thread((void *)onvif_task, NULL);

    if (p_config->http_enable)
    {
        if (!http_srv_init(&p_class->http_srv, 
                           p_config->server_ip, 
                           p_class->http_port, 
                           p_config->http_max_users, 
                           0, NULL, NULL,
                           p_config->ipv6_enable))
        {
            if (p_class->http_srv.sockaddr.ipv4_flag)
            {
                printf("http server listen on http://%s:%u failed\r\n", 
                    p_class->http_srv.ipv4_host, p_class->http_port);
            }

            if (p_class->http_srv.sockaddr.ipv6_flag)
            {
                printf("http server listen on http://[%s]:%u failed\r\n", 
                    p_class->http_srv.ipv6_host, p_class->http_port);
            }
        }
        else
        {
            http_set_msg_cb(&p_class->http_srv, onvif_http_msg_cb, NULL);
            http_set_data_cb(&p_class->http_srv, onvif_http_data_cb, NULL);

#ifdef IPFILTER_SUPPORT
            http_set_conn_cb(&p_class->http_srv, onvif_http_conn_cb, NULL);
#endif

            if (p_class->http_srv.sockaddr.ipv4_flag)
            {
                printf("Onvif server running at http://%s:%u\r\n", 
                    p_class->http_srv.ipv4_host, p_class->http_port);
            }

            if (p_class->http_srv.sockaddr.ipv6_flag)
            {
                printf("Onvif server running at http://[%s]:%u\r\n", 
                    p_class->http_srv.ipv6_host, p_class->http_port);
            }
        }
    }

#ifdef HTTPS
    if (p_config->https_enable)
    {
        if (!http_srv_init(&p_class->https_srv, 
                           p_config->server_ip, 
                           p_class->https_port, 
                           p_config->http_max_users, 
                           1, 
                           p_config->cert_file, 
                           p_config->key_file,
                           p_config->ipv6_enable))
        {
            if (p_class->https_srv.sockaddr.ipv4_flag)
            {
                printf("http server listen on https://%s:%u failed\r\n", 
                    p_class->https_srv.ipv4_host, p_class->https_port);
            }

            if (p_class->https_srv.sockaddr.ipv6_flag)
            {
                printf("http server listen on https://[%s]:%u failed\r\n", 
                    p_class->https_srv.ipv6_host, p_class->https_port);
            }
        }
        else
        {
            http_set_msg_cb(&p_class->https_srv, onvif_http_msg_cb, NULL);
            http_set_data_cb(&p_class->https_srv, onvif_http_data_cb, NULL);

#ifdef IPFILTER_SUPPORT
            http_set_conn_cb(&p_class->https_srv, onvif_http_conn_cb, NULL);
#endif

            if (p_class->https_srv.sockaddr.ipv4_flag)
            {
                printf("Onvif server running at https://%s:%u\r\n", 
                    p_class->https_srv.ipv4_host, p_class->https_port);
            }

            if (p_class->https_srv.sockaddr.ipv6_flag)
            {
                printf("Onvif server running at https://[%s]:%u\r\n", 
                    p_class->https_srv.ipv6_host, p_class->https_port);
            }
        }
    }
#endif

#ifdef HTTPD
    p_class->httpd = httpd_init();
    if (p_class->httpd)
    {
        httpd_set_on_auth(p_class->httpd, onvif_httpd_on_auth, NULL);
    }
#endif

    onvif_timer_init();

    onvif_start_discovery();
}

void onvif_start(const char * filename)
{
    log_init(ONVIF_LOG_FILE);
    
    onvif_init_def_cfg();
    
    if (!onvif_load_cfg(filename))
    {
        printf("Read config file %s failed!!!\r\n", filename);
        log_print(HT_LOG_ERR, "%s, read config file %s failed\r\n", __FUNCTION__, filename);
    }
    
    onvif_init_cfg();

    if (g_onvif_cfg.log_enable)
    {
        log_set_level(g_onvif_cfg.log_level);
    }
    else
    {
        log_close();
    }
    
    onvif_start1();
}

void onvif_free_device()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    onvif_free_NetworkInterfaces(&p_config->network.interfaces);
    
    onvif_free_VideoSources(&p_config->v_src);

    onvif_free_VideoSourceConfigurations(&p_config->v_src_cfg);

    onvif_free_VideoEncoder2Configurations(&p_config->v_enc_cfg);

#ifdef AUDIO_SUPPORT
    onvif_free_AudioSources(&p_config->a_src);

    onvif_free_AudioSourceConfigurations(&p_config->a_src_cfg);

    onvif_free_AudioEncoder2Configurations(&p_config->a_enc_cfg);

    onvif_free_AudioDecoderConfigurations(&p_config->a_dec_cfg);
#endif 

    onvif_free_profiles(&p_config->profiles);

    onvif_free_OSDConfigurations(&p_config->OSDs);

    onvif_free_MetadataConfigurations(&p_config->metadata_cfg);

#ifdef MEDIA2_SUPPORT
    onvif_free_Masks(&p_config->mask);
#endif 

#ifdef PTZ_SUPPORT
    onvif_free_PTZNodes(&p_config->ptz_node);

    onvif_free_PTZConfigurations(&p_config->ptz_cfg);
#endif

#ifdef VIDEO_ANALYTICS
    onvif_free_VideoAnalyticsConfigurations(&p_config->va_cfg);
#endif

#ifdef PROFILE_G_SUPPORT
    onvif_free_Recordings(&p_config->recordings);

    onvif_free_RecordingJobs(&p_config->recording_jobs);
#endif

#ifdef PROFILE_C_SUPPORT
    onvif_free_AccessPoints(&p_config->access_points);
    onvif_free_Doors(&p_config->doors);
    onvif_free_Areas(&p_config->areas);
#endif

#ifdef DEVICEIO_SUPPORT
    onvif_free_VideoOutputs(&p_config->v_output);
    onvif_free_VideoOutputConfigurations(&p_config->v_output_cfg);
    onvif_free_AudioOutputs(&p_config->a_output);
    onvif_free_AudioOutputConfigurations(&p_config->a_output_cfg);
    onvif_free_RelayOutputs(&p_config->relay_output);
    onvif_free_DigitalInputs(&p_config->digit_input);
    onvif_free_SerialPorts(&p_config->serial_port);
#endif

#ifdef CREDENTIAL_SUPPORT
    onvif_free_Credentials(&p_config->credential);
#endif

#ifdef ACCESS_RULES
    onvif_free_AccessProfiles(&p_config->access_rules);
#endif

#ifdef SCHEDULE_SUPPORT
    onvif_free_Schedules(&p_config->schedule);
    onvif_free_SpecialDayGroups(&p_config->specialdaygroup);
#endif

#ifdef RECEIVER_SUPPORT
    onvif_free_Receivers(&p_config->receiver);
#endif

#ifdef SECURITY_SUPPORT
    onvif_free_Keys(&p_config->keys);
    onvif_free_Passphrases(&p_config->passphrases);
    onvif_free_Certificates(&p_config->certificates);
    onvif_free_CertificationPaths(&p_config->certificatepaths);
#endif
}

void onvif_stop()
{
    OIMSG stm;
    memset(&stm, 0, sizeof(stm));

    g_onvif_cls.sys_run_flag = 0;
    
    stm.msg_src = ONVIF_EXIT;

    hqueue_put(g_onvif_cls.msg_queue, (char *)&stm);

    sys_os_wait_thread(&g_onvif_cls.tid_main);
    
    onvif_bye();
    onvif_stop_discovery();

    onvif_timer_deinit();

    http_srv_deinit(&g_onvif_cls.http_srv);

#ifdef HTTPS
    http_srv_deinit(&g_onvif_cls.https_srv);
#endif

    hqueue_delete(g_onvif_cls.msg_queue);
    g_onvif_cls.msg_queue = NULL;

    onvif_save_cfg(RUNTIME_CONFIG_FILE);
    
    onvif_eua_deinit();

#ifdef PROFILE_G_SUPPORT
    onvif_FreeSearchs();
#endif

#ifdef HTTPD
    if (g_onvif_cls.httpd)
    {
        httpd_deinit(g_onvif_cls.httpd);
        g_onvif_cls.httpd = NULL;
    }
#endif

    onvif_free_device();

#ifdef NO_RTSP_SERVER
    sys_buf_deinit();
    http_msg_buf_deinit();
#endif

    log_close();
}


