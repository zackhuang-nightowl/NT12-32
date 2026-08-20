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

#include "onvif_api.h"
#include "onvif_cln.h"
#include "http_srv.h"
#include "onvif_event.h"
#include "onvif_utils.h"
#include "soap_parser.h"
#include "http_cln.h"
#include "onvif_http.h"

/***************************************************************************************/
extern ONVIF_EVENT_CLS g_event_cls;

/***************************************************************************************/
HT_API BOOL GetCapabilities(ONVIF_DEVICE * p_dev)
{
    tds_GetCapabilities_RES res;
    memcpy(&res.Capabilities, &p_dev->Capabilities, sizeof(onvif_Capabilities));
    
    if (!onvif_tds_GetCapabilities(p_dev, NULL, &res))
    {
        return FALSE;
    }
    else
    {
        memcpy(&p_dev->Capabilities, &res.Capabilities, sizeof(onvif_Capabilities));
    }

    return TRUE;
}

HT_API BOOL GetServices(ONVIF_DEVICE * p_dev)
{
    tds_GetServices_REQ req;
    tds_GetServices_RES res;

    req.IncludeCapability = TRUE;
    memcpy(&res.Capabilities, &p_dev->Capabilities, sizeof(onvif_Capabilities));
    
    if (!onvif_tds_GetServices(p_dev, &req, &res))
    {
        return FALSE;
    }
    else
    {
        memcpy(&p_dev->Capabilities, &res.Capabilities, sizeof(onvif_Capabilities));
    }

    return TRUE;
}

HT_API BOOL GetSystemDateAndTime(ONVIF_DEVICE * p_dev)
{
    tds_GetSystemDateAndTime_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tds_GetSystemDateAndTime(p_dev, NULL, &res))
    {
        return FALSE;
    }
    
    if (res.UTCDateTimeFlag)
    {
        p_dev->fetchTime = time(NULL);
        p_dev->timeType = TIME_TYPE_UTC;
        memcpy(&p_dev->devTime, &res.UTCDateTime, sizeof(onvif_DateTime));
    }
    else if (res.LocalDateTimeFlag)
    {
        p_dev->fetchTime = time(NULL);
        p_dev->timeType = TIME_TYPE_LOCAL;
        memcpy(&p_dev->devTime, &res.LocalDateTime, sizeof(onvif_DateTime));
    }
    else
    {
        p_dev->timeType = TIME_TYPE_INVALID;
    }
    
    return TRUE;
}

HT_API BOOL SetSystemDateAndTime(ONVIF_DEVICE * p_dev)
{
    tds_SetSystemDateAndTime_REQ req;
    memset(&req, 0, sizeof(req));

    time_t nowtime = time(NULL);
    struct tm gm, lt;
    gmtime_r(&nowtime, &gm);
    localtime_r(&nowtime, &lt);   /* 本进程已按 NVR 时区(/etc/localtime)→ 取相机应随的时区 */

    req.SystemDateTime.DateTimeType = SetDateTimeType_Manual;
    /* 默认: 固定偏移、不启用夏令(DST 规则由 NVR 经 nop_onvif_set_system_datetime_cfg 下发)。 */
    req.SystemDateTime.DaylightSavings = FALSE;
    /* ★ 补 TimeZone(POSIX):否则相机保留自身时区,即便 UTC 已设,本地显示时间仍与 NVR 不一致
     * (=用户报的"改系统时间没同步 IPC 时间")。tm_gmtoff=UTC 以东秒;POSIX 符号相反(UTC+8→"GMT-8")。 */
    {
        long off = lt.tm_gmtoff;                  /* 秒,东为正 */
        int  sign = (off < 0) ? 1 : -1;           /* POSIX 反号 */
        long a = (off < 0) ? -off : off;
        int  hh = (int)(a / 3600), mm = (int)((a % 3600) / 60);
        if (mm)
            snprintf(req.SystemDateTime.TimeZone.TZ, sizeof(req.SystemDateTime.TimeZone.TZ),
                     "GMT%+d:%02d", sign * hh, mm);
        else
            snprintf(req.SystemDateTime.TimeZone.TZ, sizeof(req.SystemDateTime.TimeZone.TZ),
                     "GMT%+d", sign * hh);
        req.SystemDateTime.TimeZoneFlag = 1;      /* 序列化器仅在此 flag 置位时才发 <TimeZone> */
    }
    req.UTCDateTimeFlag = 1;
    req.UTCDateTime.Date.Year = gm.tm_year+1900;
    req.UTCDateTime.Date.Month = gm.tm_mon+1;
    req.UTCDateTime.Date.Day = gm.tm_mday;
    req.UTCDateTime.Time.Hour = gm.tm_hour;
    req.UTCDateTime.Time.Minute = gm.tm_min;
    req.UTCDateTime.Time.Second = gm.tm_sec;

    return onvif_tds_SetSystemDateAndTime(p_dev, &req, NULL);
}

HT_API BOOL GetDeviceInformation(ONVIF_DEVICE * p_dev)
{
    tds_GetDeviceInformation_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tds_GetDeviceInformation(p_dev, NULL, &res))
    {
        return FALSE;
    }
    else
    {
        memcpy(&p_dev->DeviceInformation, &res.DeviceInformation, sizeof(onvif_DeviceInformation));
    }

    return TRUE;
}

HT_API BOOL GetEndpointReference(ONVIF_DEVICE * p_dev)
{
    tds_GetEndpointReference_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tds_GetEndpointReference(p_dev, NULL, &res))
    {
        return FALSE;
    }
    else
    {
        strncpy(p_dev->binfo.EndpointReference, res.GUID, sizeof(p_dev->binfo.EndpointReference)-1);
    }

    return TRUE;
}

HT_API BOOL GetProfiles(ONVIF_DEVICE * p_dev)
{
    trt_GetProfiles_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetProfiles(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_profiles(&p_dev->profiles);

    p_dev->profiles = res.Profiles;

    return TRUE;
}

HT_API BOOL GetStreamUris(ONVIF_DEVICE * p_dev, onvif_TransportProtocol proto)
{
    ONVIF_PROFILE * p_profile = p_dev->profiles;

    while (p_profile)
    {
        trt_GetStreamUri_REQ req;
        trt_GetStreamUri_RES res;

        memset(&res, 0, sizeof(res));
        
        req.StreamSetup.Transport.Protocol = proto;
        req.StreamSetup.Stream = StreamType_RTP_Unicast;
        strcpy(req.ProfileToken, p_profile->token);

        if (onvif_trt_GetStreamUri(p_dev, &req, &res))
        {
            strcpy(p_profile->stream_uri, res.Uri);
        }
        else
        {
            return FALSE;
        }

        p_profile = p_profile->next;
    }

    return TRUE;
}

HT_API BOOL GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev)
{
    trt_GetVideoSourceConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetVideoSourceConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_VideoSourceConfigurations(&p_dev->v_src_cfg);

    p_dev->v_src_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev)
{
    trt_GetAudioSourceConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetAudioSourceConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_AudioSourceConfigurations(&p_dev->a_src_cfg);

    p_dev->a_src_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev)
{
    trt_GetVideoEncoderConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetVideoEncoderConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_VideoEncoderConfigurations(&p_dev->v_enc_cfg);

    p_dev->v_enc_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev)
{
    trt_GetAudioEncoderConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetAudioEncoderConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_AudioEncoderConfigurations(&p_dev->a_enc_cfg);

    p_dev->a_enc_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL GetNodes(ONVIF_DEVICE * p_dev)
{
    ptz_GetNodes_RES res;
    memset(&res, 0, sizeof(res));
    
    if (p_dev->Capabilities.ptz.support == 0)
    {
        return FALSE;
    }
    
    if (!onvif_ptz_GetNodes(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_free_PTZNodes(&p_dev->ptz_node);

    p_dev->ptz_node = res.PTZNode;

    return TRUE;
}

HT_API BOOL GetConfigurations(ONVIF_DEVICE * p_dev)
{
    ptz_GetConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (p_dev->Capabilities.ptz.support == 0)
    {
        return FALSE;
    }
    
    if (!onvif_ptz_GetConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_free_PTZConfigurations(&p_dev->ptz_cfg);

    p_dev->ptz_cfg = res.PTZConfiguration;

    return TRUE;
}

HT_API BOOL GetVideoSources(ONVIF_DEVICE * p_dev)
{
    trt_GetVideoSources_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetVideoSources(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_free_VideoSources(&p_dev->v_src);

    p_dev->v_src = res.VideoSources;

    return TRUE;
}

HT_API BOOL GetAudioSources(ONVIF_DEVICE * p_dev)
{
    trt_GetAudioSources_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_trt_GetAudioSources(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_free_AudioSources(&p_dev->a_src);

    p_dev->a_src = res.AudioSources;

    return TRUE;
}

HT_API BOOL GetImagingSettings(ONVIF_DEVICE * p_dev)
{
    VideoSourceList * p_v_src;
    
    if (p_dev->Capabilities.image.support == 0)
    {
        return FALSE;
    }

    p_v_src = p_dev->v_src;

    while (p_v_src)
    {
        img_GetImagingSettings_REQ req;
        img_GetImagingSettings_RES res;

        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        
        strcpy(req.VideoSourceToken, p_v_src->VideoSource.token);
        
        if (onvif_img_GetImagingSettings(p_dev, &req, &res))
        {
            memcpy(&p_v_src->VideoSource.ImagingSettings, &res.ImagingSettings, sizeof(onvif_ImagingSettings));
        }

        p_v_src = p_v_src->next;
    }    

    return TRUE;
}

void GetBestIp(const char * host, char * sip, int len)
{
    int ret;
    uint32 size = 15000;
    char * buff;
    HT_IFINFOTABLE * table;
    HT_SOCKADDR sockaddr;
    struct sockaddr * remote;

    if (!get_address_by_name(host, HT_PROTOCOL_TCP, &sockaddr))
    {
        return;
    }

    if (sockaddr.ipv4_flag)
    {
        remote = (struct sockaddr *) &sockaddr.ipv4_addr;
    }
    else if (sockaddr.ipv6_flag)
    {
        remote = (struct sockaddr *) &sockaddr.ipv6_addr;
    }
    else
    {
        return;
    }
    
    buff = (char *) malloc(size);
    if (NULL == buff)
    {
        return;
    }
    
    table = (HT_IFINFOTABLE *) buff;
    ret = get_ifinfo_table(table, &size);
    if (ret == 0)
    {
        free(buff);
        buff = (char *) malloc(size);
        if (NULL == buff)
        {
            return;
        }
        
        table = (HT_IFINFOTABLE *) buff;
        ret = get_ifinfo_table(table, &size);
    }

    if (ret > 0)
    {
        uint32 i;

        for (i = 0; i < table->count; i++)
        {
            if (table->table[i].family != remote->sa_family)
            {
                continue;
            }
            
            if (table->table[i].family == AF_INET)
            {
                struct in_addr local;
                struct sockaddr_in * addr = (struct sockaddr_in *) remote;
                
                inet_pton(AF_INET, table->table[i].ip, &local);

                if (is_same_ipv4_network(&local, &addr->sin_addr, table->table[i].prefix_len))
                {
                    strncpy(sip, table->table[i].ip, len);
                    break;
                }
            }
            else if (table->table[i].family == AF_INET6)
            {
                struct in6_addr local;
                struct sockaddr_in6 * addr = (struct sockaddr_in6 *) remote;
                
                inet_pton(AF_INET6, table->table[i].ip, &local);

                if (is_same_ipv6_network(&local, &addr->sin6_addr, table->table[i].prefix_len))
                {
                    strncpy(sip, table->table[i].ip, len);
                    break;
                }
            }
        }
    }

    free(buff);
}

char * GetEventServiceIp(int https, int ipv6, const char * remote, char * sip, int len)
{
    *sip = '\0';

    if (https)
    {
        if (g_event_cls.https_host[0] != '\0')
        {
            strncpy(sip, g_event_cls.https_host, len);
        }
        else
        {
            GetBestIp(remote, sip, len);
        }
    }
    else
    {
        if (g_event_cls.http_host[0] != '\0')
        {
            strncpy(sip, g_event_cls.http_host, len);
        }
        else
        {
            GetBestIp(remote, sip, len);
        }
    }

    if (sip[0] == '\0')
    {
        HTTPSRV * p_srv;

        if (https)
        {
            p_srv = &g_event_cls.https_srv;
        }
        else
        {
            p_srv = &g_event_cls.http_srv;
        }
        
        if (ipv6 && p_srv->ipv6_fd > 0)
        {
            strncpy(sip, p_srv->ipv6_host, len);
        }
        else
        {
            strncpy(sip, p_srv->ipv4_host, len);
        }
    }

    return sip;
}

HT_API BOOL Subscribe(ONVIF_DEVICE * p_dev, int index)
{
    BOOL ipv6 = FALSE;
    BOOL https = FALSE;
    tev_Subscribe_REQ req;
    tev_Subscribe_RES res;
    HTTPSRV * p_srv;
    char sip[128] = {'\0'};
    
    if (p_dev->binfo.XAddr.https && g_event_cls.https_enable)
    {
        https = TRUE;
        p_srv = &g_event_cls.https_srv;
    }
    else
    {
        if (g_event_cls.http_enable)
        {
            p_srv = &g_event_cls.http_srv;
        }
        else if (g_event_cls.https_enable)
        {
            https = TRUE;
            p_srv = &g_event_cls.https_srv;
        }
        else
        {
            return FALSE;
        }
    }

    if (is_ipv6_address(p_dev->binfo.XAddr.host))
    {
        ipv6 = TRUE;
    }

    GetEventServiceIp(https, ipv6, p_dev->binfo.XAddr.host, sip, sizeof(sip)-1);
    
#if __WINDOWS_OS__
    if (ipv6 && p_srv->ipv6_fd > 0)
    {
        snprintf(p_dev->events.reference_addr, sizeof(p_dev->events.reference_addr), 
            "%s://[%s]:%d/subscription%d/%llu", 
            https ? "https" : "http", sip, 
            get_sockaddr_port((struct sockaddr *) &p_srv->sockaddr.ipv6_addr),
            index, time(NULL));
    }
    else
    {
        snprintf(p_dev->events.reference_addr, sizeof(p_dev->events.reference_addr), 
            "%s://%s:%d/subscription%d/%llu", 
            https ? "https" : "http", sip, 
            get_sockaddr_port((struct sockaddr *) &p_srv->sockaddr.ipv4_addr),
            index, time(NULL));
    }
#else
    if (ipv6 && p_srv->ipv6_fd > 0)
    {
        snprintf(p_dev->events.reference_addr, sizeof(p_dev->events.reference_addr), 
            "%s://[%s]:%d/subscription%d/%lu", 
            https ? "https" : "http", sip, 
            get_sockaddr_port((struct sockaddr *) &p_srv->sockaddr.ipv6_addr),
            index, time(NULL));
    }
    else
    {
        snprintf(p_dev->events.reference_addr, sizeof(p_dev->events.reference_addr), 
            "%s://%s:%d/subscription%d/%lu", 
            https ? "https" : "http", sip, 
            get_sockaddr_port((struct sockaddr *) &p_srv->sockaddr.ipv4_addr),
            index, time(NULL));
    }
#endif

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    if (p_dev->events.init_term_time <= 0)
    {
        p_dev->events.init_term_time = 60;
    }
    
    req.InitialTerminationTime = p_dev->events.init_term_time;
    strcpy(req.ConsumerReference, p_dev->events.reference_addr);    
    
    if (!onvif_tev_Subscribe(p_dev, &req, &res))
    {
        return FALSE;
    }
    
    strcpy(p_dev->events.producter_addr, res.producter_addr);
    strcpy(p_dev->events.producter_parameters, res.ReferenceParameters);

    p_dev->events.renew_time = time(NULL);
    p_dev->events.subscribe = TRUE;
    
    onvif_event_timer_add(p_dev);
    
    return TRUE;
}

HT_API BOOL Unsubscribe(ONVIF_DEVICE * p_dev)
{
    onvif_event_timer_del(p_dev);
    
    onvif_tev_Unsubscribe(p_dev, NULL, NULL);

    p_dev->events.subscribe = FALSE;
    p_dev->events.pullpoint = FALSE;
    
    return TRUE;
}

HT_API BOOL CreatePullPointSubscription(ONVIF_DEVICE * p_dev)
{
    tev_CreatePullPointSubscription_REQ req;
    tev_CreatePullPointSubscription_RES res;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    if (p_dev->events.init_term_time <= 0)
    {
        p_dev->events.init_term_time = 60;
    }
    
    req.InitialTerminationTime = p_dev->events.init_term_time;
    
    if (!onvif_tev_CreatePullPointSubscription(p_dev, &req, &res))
    {
        return FALSE;
    }
    
    strcpy(p_dev->events.producter_addr, res.producter_addr);
    strcpy(p_dev->events.producter_parameters, res.ReferenceParameters);

    p_dev->events.pullpoint = TRUE;
    
    return TRUE;
}

HT_API BOOL PullMessages(ONVIF_DEVICE * p_dev, int timeout, int message_limit, tev_PullMessages_RES * p_res)
{
    tev_PullMessages_REQ req;

    memset(&req, 0, sizeof(req));

    req.Timeout = timeout;
    req.MessageLimit = message_limit;

    if (p_res)
    {
        memset(p_res, 0, sizeof(tev_PullMessages_RES));
    }
    
    return onvif_tev_PullMessages(p_dev, &req, p_res);
}

HT_API BOOL GetSnapshot(ONVIF_DEVICE * p_dev, const char * profile_token, unsigned char ** p_buf, int * buflen)
{
    if (NULL == profile_token)
    {
        return FALSE;
    }

    trt_GetSnapshotUri_REQ req;
    trt_GetSnapshotUri_RES res;
    
    strcpy(req.ProfileToken, profile_token);
    memset(&res, 0, sizeof(res));    
    
    if (!onvif_trt_GetSnapshotUri(p_dev, &req, &res))
    {
        return FALSE;
    }

    onvif_parse_uri(res.Uri, res.Uri, sizeof(res.Uri));

    HTTPREQ http_req;
    memset(&http_req, 0, sizeof(http_req));

    onvif_XAddr xaddr;
    memset(&xaddr, 0, sizeof(xaddr));
    parse_XAddr(res.Uri, &xaddr);

    http_req.https = xaddr.https;
    http_req.port = xaddr.port;
    strcpy(http_req.host, xaddr.host);    
    strcpy(http_req.url, xaddr.url);
    strcpy(http_req.auth_info.auth_name, p_dev->username);
    strcpy(http_req.auth_info.auth_pwd, p_dev->password);

    return http_onvif_download(&http_req, 60000, p_buf, buflen, p_dev);
}

HT_API void FreeBuff(void * p_buf)
{
    if (p_buf)
    {
        free(p_buf);
    }
}

HT_API BOOL FirmwareUpgrade(ONVIF_DEVICE * p_dev, const char * filename)
{    
    tds_StartFirmwareUpgrade_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tds_StartFirmwareUpgrade(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_parse_uri(res.UploadUri, res.UploadUri, sizeof(res.UploadUri));
    
    HTTPREQ req;
    memset(&req, 0, sizeof(req));

    onvif_XAddr xaddr;
    memset(&xaddr, 0, sizeof(xaddr));
    parse_XAddr(res.UploadUri, &xaddr);

    req.https = xaddr.https;
    req.port = xaddr.port;
    strcpy(req.host, xaddr.host);    
    strcpy(req.url, xaddr.url);
    strcpy(req.auth_info.auth_name, p_dev->username);
    strcpy(req.auth_info.auth_pwd, p_dev->password);
    
    return http_onvif_file_upload(&req, 60000, filename, p_dev);
}

HT_API BOOL SystemBackup(ONVIF_DEVICE * p_dev, const char * filename)
{
    tds_GetSystemUris_RES res;
    memset(&res, 0, sizeof(res));    
    
    if (!onvif_tds_GetSystemUris(p_dev, NULL, &res))
    {
        return FALSE;
    }

    if (0 == res.SystemBackupUriFlag)
    {
        return FALSE;
    }

    onvif_parse_uri(res.SystemBackupUri, res.SystemBackupUri, sizeof(res.SystemBackupUri));

    HTTPREQ http_req;
    memset(&http_req, 0, sizeof(http_req));

    onvif_XAddr xaddr;
    memset(&xaddr, 0, sizeof(xaddr));
    parse_XAddr(res.SystemBackupUri, &xaddr);

    http_req.https = xaddr.https;
    http_req.port = xaddr.port;
    strcpy(http_req.host, xaddr.host);    
    strcpy(http_req.url, xaddr.url);
    strcpy(http_req.auth_info.auth_name, p_dev->username);
    strcpy(http_req.auth_info.auth_pwd, p_dev->password);

    int buflen = 0;
    unsigned char * p_buf = NULL;

    if (!http_onvif_download(&http_req, 60000, &p_buf, &buflen, p_dev))
    {
        return FALSE;
    }

    if (NULL == p_buf)
    {
        return FALSE;
    }

    FILE * fp = fopen(filename, "wb");
    if (fp)
    {
        fwrite(p_buf, 1, buflen, fp);
        fclose(fp);
    }
    else
    {
        free(p_buf);
        return FALSE;
    }

    free(p_buf);

    return TRUE;
}

HT_API BOOL SystemRestore(ONVIF_DEVICE * p_dev, const char * filename)
{
    tds_StartSystemRestore_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tds_StartSystemRestore(p_dev, NULL, &res))
    {
        return FALSE;
    }

    onvif_parse_uri(res.UploadUri, res.UploadUri, sizeof(res.UploadUri));
    
    HTTPREQ req;
    memset(&req, 0, sizeof(req));

    onvif_XAddr xaddr;
    memset(&xaddr, 0, sizeof(xaddr));
    parse_XAddr(res.UploadUri, &xaddr);

    req.https = xaddr.https;
    req.port = xaddr.port;
    strcpy(req.host, xaddr.host);    
    strcpy(req.url, xaddr.url);
    strcpy(req.auth_info.auth_name, p_dev->username);
    strcpy(req.auth_info.auth_pwd, p_dev->password);
    
    return http_onvif_file_upload(&req, 60000, filename, p_dev);
}

HT_API BOOL tr2_GetProfiles(ONVIF_DEVICE * p_dev)
{
    tr2_GetProfiles_REQ req;
    tr2_GetProfiles_RES res;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    req.sizeType = 1;
    strcpy(req.Type[0], "ALL");
    
    if (!onvif_tr2_GetProfiles(p_dev, &req, &res))
    {
        return FALSE;
    }
   
    onvif_free_MediaProfiles(&p_dev->media_profiles);

    p_dev->media_profiles = res.Profiles;

    return TRUE;
}

HT_API BOOL tr2_GetStreamUris(ONVIF_DEVICE * p_dev, const char * proto)
{
    MediaProfileList * p_profile = p_dev->media_profiles;

    while (p_profile)
    {
        tr2_GetStreamUri_REQ req;
        tr2_GetStreamUri_RES res;

        memset(&res, 0, sizeof(res));
        
        strncpy(req.Protocol, proto, sizeof(req.Protocol)-1);
        strcpy(req.ProfileToken, p_profile->MediaProfile.token);

        if (onvif_tr2_GetStreamUri(p_dev, &req, &res))
        {
            strcpy(p_profile->MediaProfile.stream_uri, res.Uri);
        }
        else
        {
            return FALSE;
        }

        p_profile = p_profile->next;
    }

    return TRUE;
}

HT_API BOOL tr2_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev)
{
    tr2_GetVideoSourceConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tr2_GetVideoSourceConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_VideoSourceConfigurations(&p_dev->v_src_cfg);

    p_dev->v_src_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL tr2_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev)
{
    tr2_GetAudioSourceConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tr2_GetAudioSourceConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_AudioSourceConfigurations(&p_dev->a_src_cfg);

    p_dev->a_src_cfg = res.Configurations;

    return TRUE;
}

HT_API BOOL tr2_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev)
{
    tr2_GetVideoEncoderConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tr2_GetVideoEncoderConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_VideoEncoder2Configurations(&p_dev->v_enc_cfg2);

    p_dev->v_enc_cfg2 = res.Configurations;

    return TRUE;
}

HT_API BOOL tr2_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev)
{
    tr2_GetAudioEncoderConfigurations_RES res;
    memset(&res, 0, sizeof(res));
    
    if (!onvif_tr2_GetAudioEncoderConfigurations(p_dev, NULL, &res))
    {
        return FALSE;
    }
   
    onvif_free_AudioEncoder2Configurations(&p_dev->a_enc_cfg2);

    p_dev->a_enc_cfg2 = res.Configurations;

    return TRUE;
}




