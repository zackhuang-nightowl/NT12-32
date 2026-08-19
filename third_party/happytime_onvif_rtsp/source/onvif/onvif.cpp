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
#include "onvif.h"
#include "onvif_device.h"
#include "xml_node.h"
#include "onvif_event.h"
#include "onvif_utils.h"
#include "onvif_cfg.h"
#include "util.h"
#include "http_srv.h"
#include <math.h>
#ifdef SECURITY_SUPPORT
#include "onvif_security.h"
#endif
#ifdef LIBICAL
#include "icalvcal.h"
#include "vcc.h"   
#endif

/***************************************************************************************/
ONVIF_CFG g_onvif_cfg;
ONVIF_CLS g_onvif_cls;
ONVIF_IDX g_onvif_idx;

/***************************************************************************************/

void onvif_get_best_ip(struct sockaddr * remote, char * sip, int len)
{
    int ret;
    uint32 size = 15000;
    char * buff;
    HT_IFINFOTABLE * table;

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

char * onvif_get_service_ip(struct sockaddr * local, struct sockaddr * remote, char * sip, int len)
{
    *sip = '\0';
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    if (p_config->server_ip[0] == '\0')
    {
        if (local->sa_family == AF_INET)
        {
            struct sockaddr_in * addr = (struct sockaddr_in *) local;

            if (addr->sin_addr.s_addr != 0)
            {
                // Receive data from this local IP address and use it
                get_sockaddr_ip(local, sip, len);
            }
            else
            {
                onvif_get_best_ip(remote, sip, len);
            }
        }
        else if (local->sa_family == AF_INET6)
        {
            struct sockaddr_in6 * addr = (struct sockaddr_in6 *) local;

            if (memcmp(&addr->sin6_addr, &in6addr_any, sizeof(struct in6_addr)) != 0)
            {
                // Receive data from this local IP address and use it
                get_sockaddr_ip(local, sip, len);
            }
            else
            {
                onvif_get_best_ip(remote, sip, len);
            }
        }
    }
    else
    {
        // The server ip address has been configured, use it
        strncpy(sip, p_config->server_ip, len);
    }

    if (sip[0] == '\0')
    {
        if (local->sa_family == AF_INET)
        {
            get_local_ip(AF_INET, sip, len);
        }
        else if (local->sa_family == AF_INET6)
        {
            get_local_ip(AF_INET6, sip, len);
        }
    }

    return sip;
}

char * onvif_get_service_ip_by_user(HTTPCLN * p_user, char * sip, int len)
{
    struct sockaddr * local_addr = NULL;
    struct sockaddr * remote_addr = NULL;
    
    http_get_peer_addr(p_user, &local_addr, &remote_addr);

    return onvif_get_service_ip(local_addr, remote_addr, sip, len);
}

char * onvif_get_service_addr(onvif_CapabilityCategory type, int https, struct sockaddr * local, struct sockaddr * remote, char * saddr, int len)
{
    int offset = 0;
    uint16 port;
    char proto[16];
    char sip1[64] = {'\0'};
    char sip2[64] = {'\0'};
    char suffix[64];
    ONVIF_CLS * p_class = &g_onvif_cls;
    ONVIF_CFG * p_config = &g_onvif_cfg;

    if (p_config->server_ip[0] != '\0')
    {
        // The server ip address has been configured, use it
        strncpy(sip1, p_config->server_ip, sizeof(sip1)-1);
    }
    else if (NULL == local || NULL == remote)
    {
        if (p_config->http_enable)
        {
            if (p_class->http_srv.ipv4_fd > 0)
            {
                get_local_ip(AF_INET, sip1, sizeof(sip1));
            }
            if (p_class->http_srv.ipv6_fd > 0 && p_config->ipv6_enable)
            {
                get_local_ip(AF_INET6, sip2, sizeof(sip2));
            }
        }
#ifdef HTTPS
        else if (p_config->https_enable)
        {
            if (p_class->https_srv.ipv4_fd > 0)
            {
                get_local_ip(AF_INET, sip1, sizeof(sip1));
            }
            if (p_class->https_srv.ipv6_fd > 0 && p_config->ipv6_enable)
            {
                get_local_ip(AF_INET6, sip2, sizeof(sip2));
            }
        }
#endif
    }
    else
    {
        onvif_get_service_ip(local, remote, sip1, sizeof(sip1)-1);
    }

    if (CapabilityCategory_Analytics == type)
    {
        strcpy(suffix, "/onvif/analytics_service");
    }
    else if (CapabilityCategory_Device == type)
    {
        strcpy(suffix, "/onvif/device_service");
    }
    else if (CapabilityCategory_Events == type)
    {
        strcpy(suffix, "/onvif/event_service");
    }
    else if (CapabilityCategory_Imaging == type)
    {
        strcpy(suffix, "/onvif/image_service");
    }
    else if (CapabilityCategory_Media == type)
    {
        strcpy(suffix, "/onvif/media_service");
    }
    else if (CapabilityCategory_PTZ == type)
    {
        strcpy(suffix, "/onvif/ptz_service");
    }
    else if (CapabilityCategory_Recording == type)
    {
        strcpy(suffix, "/onvif/recording_service");
    }
    else if (CapabilityCategory_Search == type)
    {
        strcpy(suffix, "/onvif/search_service");
    }
    else if (CapabilityCategory_Replay == type)
    {
        strcpy(suffix, "/onvif/replay_service");
    }
    else if (CapabilityCategory_AccessControl == type)
    {
        strcpy(suffix, "/onvif/accesscontrol_service");
    }
    else if (CapabilityCategory_DoorControl == type)
    {
        strcpy(suffix, "/onvif/doorcontrol_service");
    }
    else if (CapabilityCategory_DeviceIO == type)
    {
        strcpy(suffix, "/onvif/deviceio_service");
    }
    else if (CapabilityCategory_Media2 == type)
    {
        strcpy(suffix, "/onvif/media2_service");
    }
    else if (CapabilityCategory_Thermal == type)
    {
        strcpy(suffix, "/onvif/thermal_service");
    }
    else if (CapabilityCategory_Credential == type)
    {
        strcpy(suffix, "/onvif/credential_service");
    }
    else if (CapabilityCategory_AccessRules == type)
    {
        strcpy(suffix, "/onvif/accessrules_service");
    }
    else if (CapabilityCategory_Schedule == type)
    {
        strcpy(suffix, "/onvif/schedule_service");
    }
    else if (CapabilityCategory_Receiver == type)
    {
        strcpy(suffix, "/onvif/receiver_service");
    }
    else if (CapabilityCategory_Provisioning == type)
    {
        strcpy(suffix, "/onvif/provisioning_service");
    }
    else if (CapabilityCategory_Security == type)
    {
        strcpy(suffix, "/onvif/security_service");
    }
    else
    {
        strcpy(suffix, "/onvif/device_service");
    }

    if (https)
    {
#ifdef HTTPS
        if (!p_config->https_enable || !p_class->https_srv.enable)
        {
            https = 0;
        }
#else
        https = 0;
#endif
    }
    else if (!p_config->http_enable || !p_class->http_srv.enable)
    {
#ifdef HTTPS
        if (p_config->https_enable && p_class->https_srv.enable)
        {
            https = 1;
        }
#endif  
    }

#ifdef HTTPS
    if (https)
    {
        port = p_class->https_port;
        strcpy(proto, "https");
    }
    else
#endif
    {
        port = p_class->http_port;
        strcpy(proto, "http");
    }

    if (sip1[0] != '\0')
    {
        if (is_ipv6_address(sip1))
        {
            offset += snprintf(saddr+offset, len-offset, "%s://[%s]:%u%s", proto, sip1, port, suffix);
        }
        else
        {
            offset += snprintf(saddr+offset, len-offset, "%s://%s:%u%s", proto, sip1, port, suffix);
        }
    }

    if (sip2[0] != '\0')
    {
        if (offset > 0)
        {
            offset += snprintf(saddr+offset, len-offset, " ");
        }
    
        offset += snprintf(saddr+offset, len-offset, "%s://[%s]:%u%s", proto, sip2, port, suffix);
    }
    
    return saddr;
}

char * onvif_get_service_addr_by_user(onvif_CapabilityCategory type, HTTPCLN * p_user, char * saddr, int len)
{
    struct sockaddr * local_addr = NULL;
    struct sockaddr * remote_addr = NULL;

    http_get_peer_addr(p_user, &local_addr, &remote_addr);
    
    return onvif_get_service_addr(type, p_user->https, local_addr, remote_addr, saddr, len);
}

HT_API BOOL onvif_is_scope_exist(const char * scope)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.scopes); i++)
    {
        if (strcmp(scope, g_onvif_cfg.scopes[i].ScopeItem) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;    
}

HT_API ONVIF_RET onvif_add_scope(const char * scope, BOOL fixed)
{
    onvif_Scope * p_scope;
    
    if (onvif_is_scope_exist(scope) == TRUE)
    {
        return ONVIF_ERR_ScopeOverwrite;
    }

    p_scope = onvif_get_idle_scope();
    if (p_scope)
    {
        p_scope->ScopeDef = fixed ? ScopeDefinition_Fixed : ScopeDefinition_Configurable;
        strncpy(p_scope->ScopeItem, scope, sizeof(p_scope->ScopeItem)-1);
        return ONVIF_OK;
    }

    return ONVIF_ERR_TooManyScopes;
}

HT_API onvif_Scope * onvif_find_scope(const char * scope)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.scopes); i++)
    {
        if (strcmp(g_onvif_cfg.scopes[i].ScopeItem, scope) == 0)
        {
            return &g_onvif_cfg.scopes[i];
        }
    }

    return NULL;
}

HT_API onvif_Scope * onvif_get_idle_scope()
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.scopes); i++)
    {
        if (g_onvif_cfg.scopes[i].ScopeItem[0] == '\0')
        {
            return &g_onvif_cfg.scopes[i];
        }
    }

    return NULL;
}

HT_API BOOL onvif_is_user_exist(const char * username)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.users); i++)
    {
        if (g_onvif_cfg.users[i].Username[0] != '\0' && 
            strcmp(username, g_onvif_cfg.users[i].Username) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;    
}

HT_API ONVIF_RET onvif_add_user(onvif_User * p_user)
{
    onvif_User * p_idle_user;
    
    if (onvif_is_user_exist(p_user->Username) == TRUE)
    {
        return ONVIF_ERR_UsernameClash;
    }

    p_idle_user = onvif_get_idle_user();
    if (p_idle_user)
    {
        memcpy(p_idle_user, p_user, sizeof(onvif_User));
        return ONVIF_OK;
    }

    return ONVIF_ERR_TooManyUsers;
}

HT_API onvif_User * onvif_find_user(const char * username)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.users); i++)
    {
        if (g_onvif_cfg.users[i].Username[0] != '\0' && 
            strcmp(g_onvif_cfg.users[i].Username, username) == 0)
        {
            return &g_onvif_cfg.users[i];
        }
    }

    return NULL;
}

HT_API onvif_User * onvif_get_idle_user()
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.users); i++)
    {
        if (g_onvif_cfg.users[i].Username[0] == '\0')
        {
            return &g_onvif_cfg.users[i];
        }
    }

    return NULL;
}

HT_API const char * onvif_get_user_pass(const char * username)
{
    onvif_User * p_user;
    
    if (NULL == username || strlen(username) == 0)
    {
        return NULL;
    }
    
    p_user = onvif_find_user(username);
    if (NULL != p_user)
    {
        return p_user->Password;
    }

    return NULL;    
}

#ifdef GEOLOCATION_SUPPORT

HT_API LocationEntityList * onvif_add_LocationEntity(LocationEntityList ** p_head)
{
    LocationEntityList * p_tmp;
    LocationEntityList * p_new = (LocationEntityList *) malloc(sizeof(LocationEntityList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(LocationEntityList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API LocationEntityList * onvif_find_LocationEntity(LocationEntityList * p_head, const char * Entity, const char * Token)
{
    LocationEntityList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Location.Entity, Entity) == 0 && 
            strcmp(p_tmp->Location.Token, Token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_LocationEntity(LocationEntityList ** p_head, LocationEntityList * p_node)
{
    LocationEntityList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    free(p_node);
}

HT_API void onvif_free_LocationEntitis(LocationEntityList ** p_head)
{
    LocationEntityList * p_next;
    LocationEntityList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API int onvif_get_LocationEntity_nums(LocationEntityList * p_head)
{
    int nums = 0;
    LocationEntityList * p_tmp = p_head;

    while (p_tmp)
    {
        nums++;
        p_tmp = p_tmp->next;
    }

    return nums;
}

#endif // GEOLOCATION_SUPPORT

#ifdef STORAGE_SUPPORT

HT_API void onvif_get_StorageConfiguration_Token(StorageConfigurationList * p_head, char * token, int size)
{
    StorageConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "StorageConfigurationToken_%u", ++g_onvif_idx.storage_idx);

        p_tmp = onvif_find_StorageConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API StorageConfigurationList * onvif_add_StorageConfiguration(StorageConfigurationList ** p_head)
{
    StorageConfigurationList * p_tmp;
    StorageConfigurationList * p_new = (StorageConfigurationList *) malloc(sizeof(StorageConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(StorageConfigurationList));

    onvif_get_StorageConfiguration_Token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API StorageConfigurationList * onvif_find_StorageConfiguration(StorageConfigurationList * p_head, const char * token)
{
    StorageConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_StorageConfiguration(StorageConfigurationList ** p_head, StorageConfigurationList * p_node)
{
    StorageConfigurationList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    free(p_node);
}

HT_API void onvif_free_StorageConfigurations(StorageConfigurationList ** p_head)
{
    StorageConfigurationList * p_next;
    StorageConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

#endif

HT_API void onvif_get_profile_token(ONVIF_PROFILE * p_head, char * token, int size)
{
    ONVIF_PROFILE * p_tmp = NULL;

    do {
        snprintf(token, size, "ProfileToken_%u", ++g_onvif_idx.profile_idx);

        p_tmp = onvif_find_profile(p_head, token);
    } while (p_tmp);
}

HT_API ONVIF_PROFILE * onvif_add_profile(ONVIF_PROFILE ** p_head, BOOL fixed)
{
    ONVIF_PROFILE * p_tmp;
    ONVIF_PROFILE * p_new = (ONVIF_PROFILE *) malloc(sizeof(ONVIF_PROFILE));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ONVIF_PROFILE));

    p_new->fixed = fixed;

    onvif_get_profile_token(*p_head, p_new->token, sizeof(p_new->token));
    
    snprintf(p_new->name, sizeof(p_new->name), "ProfileName_%u", g_onvif_idx.profile_idx);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API ONVIF_PROFILE * onvif_find_profile(ONVIF_PROFILE * p_head, const char * token)
{
    ONVIF_PROFILE * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_profiles(ONVIF_PROFILE ** p_head)
{
    ONVIF_PROFILE * p_next;
    ONVIF_PROFILE * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

#ifdef PTZ_SUPPORT
        onvif_free_PTZPresets(&p_tmp->presets);
        onvif_free_PresetTours(&p_tmp->preset_tour);
#endif

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoSource_token(VideoSourceList * p_head, char * token, int size)
{
    VideoSourceList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoSourceToken_%u", ++g_onvif_idx.v_src_idx);

        p_tmp = onvif_find_VideoSource(p_head, token);
    } while (p_tmp);
}

HT_API VideoSourceList * onvif_add_VideoSource(VideoSourceList ** p_head, int w, int h)
{
    VideoSourceList * p_tmp;
    VideoSourceList * p_new = (VideoSourceList *) malloc(sizeof(VideoSourceList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoSourceList));

    p_new->VideoSource.Framerate = 25;
    p_new->VideoSource.Resolution.Width = w;
    p_new->VideoSource.Resolution.Height = h;

    onvif_get_VideoSource_token(*p_head, p_new->VideoSource.token, sizeof(p_new->VideoSource.token));

    // init video source mode
    p_new->VideoSourceMode.Enabled = 1;
    p_new->VideoSourceMode.Reboot = 1;
    p_new->VideoSourceMode.MaxFramerate = 30.0f;
    p_new->VideoSourceMode.MaxResolution.Width = w;
    p_new->VideoSourceMode.MaxResolution.Height = h;
    snprintf(p_new->VideoSourceMode.token, sizeof(p_new->VideoSourceMode.token), 
        "VideoSourceModeToken_%u", g_onvif_idx.v_src_idx);
    strcpy(p_new->VideoSourceMode.Encodings, "H264 MJPEG");
#ifdef MPEG4_SUPPORT
    strcat(p_new->VideoSourceMode.Encodings, " MP4");
#endif
#ifdef MEDIA2_SUPPORT
    strcat(p_new->VideoSourceMode.Encodings, " H265");
#endif

#ifdef IMAGE_SUPPORT
    onvif_init_ImagingSettings(p_new, &p_new->ImagingSettings);
    onvif_init_ImagingOptions(p_new, &p_new->ImagingOptions);
#endif

#ifdef THERMAL_SUPPORT
    p_new->ThermalSupport = onvif_init_Thermal(p_new);    
#endif

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API VideoSourceList * onvif_find_VideoSource(VideoSourceList * p_head, const char * token)
{
    VideoSourceList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->VideoSource.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API VideoSourceList * onvif_find_VideoSource_by_size(VideoSourceList * p_head, int w, int h)
{
    VideoSourceList * p_tmp = p_head;
    while (p_tmp)
    {
        if (p_tmp->VideoSource.Resolution.Width == w && p_tmp->VideoSource.Resolution.Height == h)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_VideoSource(VideoSourceList * p_node)
{
#ifdef THERMAL_SUPPORT
    if (p_node->ThermalConfigurationOptions.ColorPalette)
    {
        onvif_free_ColorPalettes(&p_node->ThermalConfigurationOptions.ColorPalette);
    }

    if (p_node->ThermalConfigurationOptions.NUCTable)
    {
        onvif_free_NUCTables(&p_node->ThermalConfigurationOptions.NUCTable);
    }
#endif    
}

HT_API void onvif_free_VideoSources(VideoSourceList ** p_head)
{
    VideoSourceList * p_next;
    VideoSourceList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_VideoSource(p_tmp);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoSourceConfiguration_token(VideoSourceConfigurationList * p_head, char * token, int size)
{
    VideoSourceConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoSourceConfigurationToken_%u", ++g_onvif_idx.v_src_cfg_idx);

        p_tmp = onvif_find_VideoSourceConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API VideoSourceConfigurationList * onvif_add_VideoSourceConfiguration(VideoSourceConfigurationList ** p_head, int w, int h)
{
    VideoSourceConfigurationList * p_tmp;
    VideoSourceConfigurationList * p_new = (VideoSourceConfigurationList *) malloc(sizeof(VideoSourceConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoSourceConfigurationList));

    p_new->Configuration.Bounds.width = w;
    p_new->Configuration.Bounds.height = h;

    onvif_get_VideoSourceConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "VideoSourceConfigurationName_%d", g_onvif_idx.v_src_cfg_idx);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    onvif_init_VideoSourceConfigurationOptions(p_new);

    return p_new;
}

HT_API VideoSourceConfigurationList * onvif_find_VideoSourceConfiguration(VideoSourceConfigurationList * p_head, const char * token)
{
    VideoSourceConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API VideoSourceConfigurationList * onvif_find_VideoSourceConfiguration_by_size(VideoSourceConfigurationList * p_head, int w, int h)
{
    VideoSourceConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (p_tmp->Configuration.Bounds.width == w && p_tmp->Configuration.Bounds.height == h)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_VideoSourceConfigurations(VideoSourceConfigurationList ** p_head)
{
    VideoSourceConfigurationList * p_next;
    VideoSourceConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoEncoder2Configuration_token(VideoEncoder2ConfigurationList * p_head, char * token, int size)
{
    VideoEncoder2ConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoEncoderConfigurationToken_%u", ++g_onvif_idx.v_enc_idx);

        p_tmp = onvif_find_VideoEncoder2Configuration(p_head, token);
    } while (p_tmp);
}

HT_API VideoEncoder2ConfigurationList * onvif_add_VideoEncoder2Configuration(VideoEncoder2ConfigurationList ** p_head, VideoEncoder2ConfigurationList * p_node)
{
    VideoEncoder2ConfigurationList * p_tmp;
    VideoEncoder2ConfigurationList * p_new = (VideoEncoder2ConfigurationList *) malloc(sizeof(VideoEncoder2ConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoEncoder2ConfigurationList));

    if (p_node)
    {
        memcpy(&p_new->Configuration, &p_node->Configuration, sizeof(onvif_VideoEncoder2Configuration));
    }

    onvif_get_VideoEncoder2Configuration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "VideoEncoderConfigurationName_%d", g_onvif_idx.v_enc_idx);

    p_new->Configuration.MulticastFlag = 1;
    onvif_init_MulticastConfiguration(&p_new->Configuration.Multicast);
    onvif_init_VideoEncoderConfigurationOptions(p_new);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API VideoEncoder2ConfigurationList * onvif_find_VideoEncoder2Configuration(VideoEncoder2ConfigurationList * p_head, const char * token)
{
    VideoEncoder2ConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API VideoEncoder2ConfigurationList * onvif_find_VideoEncoder2Configuration_by_param(VideoEncoder2ConfigurationList * p_head, VideoEncoder2ConfigurationList * p_node)
{
    VideoEncoder2ConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (p_tmp->Configuration.Resolution.Width == p_node->Configuration.Resolution.Width && 
            p_tmp->Configuration.Resolution.Height == p_node->Configuration.Resolution.Height && 
            fabs(p_tmp->Configuration.Quality - p_node->Configuration.Quality) < 0.1 && 
            p_tmp->Configuration.SessionTimeout == p_node->Configuration.SessionTimeout && 
            fabs(p_tmp->Configuration.RateControl.FrameRateLimit - p_node->Configuration.RateControl.FrameRateLimit) < 0.1 && 
            p_tmp->Configuration.RateControl.EncodingInterval == p_node->Configuration.RateControl.EncodingInterval && 
            p_tmp->Configuration.RateControl.BitrateLimit == p_node->Configuration.RateControl.BitrateLimit && 
            strcmp(p_tmp->Configuration.Encoding, p_node->Configuration.Encoding) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_VideoEncoder2Configurations(VideoEncoder2ConfigurationList ** p_head)
{
    VideoEncoder2ConfigurationList * p_next;
    VideoEncoder2ConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_VideoEncoder2ConfigurationOptions(&p_tmp->Options2);

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_NetworkInterface_token(NetworkInterfaceList * p_head, char * token, int size)
{
    NetworkInterfaceList * p_tmp = NULL;

    do {
        snprintf(token, size, "NetworkInterfaceToken_%u", ++g_onvif_idx.netinf_idx);

        p_tmp = onvif_find_NetworkInterface(p_head, token);
    } while (p_tmp);
}

HT_API NetworkInterfaceList * onvif_add_NetworkInterface(NetworkInterfaceList ** p_head)
{
    NetworkInterfaceList * p_tmp;
    NetworkInterfaceList * p_new = (NetworkInterfaceList *) malloc(sizeof(NetworkInterfaceList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(NetworkInterfaceList));

    onvif_get_NetworkInterface_token(*p_head, p_new->NetworkInterface.token, sizeof(p_new->NetworkInterface.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API NetworkInterfaceList * onvif_find_NetworkInterface(NetworkInterfaceList * p_head, const char * token)
{
    NetworkInterfaceList * p_tmp = p_head;

    while (p_tmp)
    {
        if (strcmp(p_tmp->NetworkInterface.token, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API NetworkInterfaceList * onvif_find_NetworkInterface_by_Name(NetworkInterfaceList * p_head, const char * name)
{
    NetworkInterfaceList * p_tmp = p_head;

    while (p_tmp)
    {
        if (p_tmp->NetworkInterface.Info.NameFlag && strcmp(p_tmp->NetworkInterface.Info.Name, name) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_NetworkInterfaces(NetworkInterfaceList ** p_head)
{
    NetworkInterfaceList * p_next;
    NetworkInterfaceList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_OSDConfiguration_token(OSDConfigurationList * p_head, char * token, int size)
{
    OSDConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "OSDConfigurationToken_%u", ++g_onvif_idx.osd_idx);

        p_tmp = onvif_find_OSDConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API OSDConfigurationList * onvif_add_OSDConfiguration(OSDConfigurationList ** p_head)
{
    OSDConfigurationList * p_tmp;
    OSDConfigurationList * p_new = (OSDConfigurationList *) malloc(sizeof(OSDConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(OSDConfigurationList));

    onvif_get_OSDConfiguration_token(*p_head, p_new->OSD.token, sizeof(p_new->OSD.token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API OSDConfigurationList * onvif_find_OSDConfiguration(OSDConfigurationList * p_head, const char * token)
{
    OSDConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->OSD.token, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_OSDConfigurations(OSDConfigurationList ** p_head)
{
    OSDConfigurationList * p_next;
    OSDConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_MetadataConfiguration_token(MetadataConfigurationList * p_head, char * token, int size)
{
    MetadataConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "MetadataConfigurationToken_%u", ++g_onvif_idx.metadata_idx);

        p_tmp = onvif_find_MetadataConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API MetadataConfigurationList * onvif_add_MetadataConfiguration(MetadataConfigurationList ** p_head)
{
    MetadataConfigurationList * p_tmp;
    MetadataConfigurationList * p_new = (MetadataConfigurationList *) malloc(sizeof(MetadataConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(MetadataConfigurationList));

    onvif_get_MetadataConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "MetadataConfigurationName_%u", g_onvif_idx.metadata_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API MetadataConfigurationList * onvif_find_MetadataConfiguration(MetadataConfigurationList * p_head, const char * token)
{
    MetadataConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_MetadataConfigurations(MetadataConfigurationList ** p_head)
{
    MetadataConfigurationList * p_next;
    MetadataConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API VideoEncoder2ConfigurationOptionsList * onvif_add_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList ** p_head)
{
    VideoEncoder2ConfigurationOptionsList * p_tmp;
    VideoEncoder2ConfigurationOptionsList * p_new = (VideoEncoder2ConfigurationOptionsList *) malloc(sizeof(VideoEncoder2ConfigurationOptionsList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoEncoder2ConfigurationOptionsList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API VideoEncoder2ConfigurationOptionsList * onvif_find_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList * p_head, const char * encoding)
{
    VideoEncoder2ConfigurationOptionsList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Options.Encoding, encoding) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList ** p_head)
{
    VideoEncoder2ConfigurationOptionsList * p_next;
    VideoEncoder2ConfigurationOptionsList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API NotificationMessageList * onvif_add_NotificationMessage(NotificationMessageList ** p_head)
{
    NotificationMessageList * p_tmp;
    NotificationMessageList * p_new = (NotificationMessageList *) malloc(sizeof(NotificationMessageList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(NotificationMessageList));

    if (p_head)
    {
        p_tmp = *p_head;
        if (NULL == p_tmp)
        {
            *p_head = p_new;
        }
        else
        {
            while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

            p_tmp->next = p_new;
        }    
    }

    p_new->refcnt++;

    return p_new;
}

HT_API void onvif_free_NotificationMessage(NotificationMessageList * p_message)
{
    if (p_message)
    {
        p_message->refcnt--;
        
        if (p_message->refcnt <= 0)
        {
            onvif_free_SimpleItems(&p_message->NotificationMessage.Message.Source.SimpleItem);
            onvif_free_SimpleItems(&p_message->NotificationMessage.Message.Key.SimpleItem);
            onvif_free_SimpleItems(&p_message->NotificationMessage.Message.Data.SimpleItem);

            onvif_free_ElementItems(&p_message->NotificationMessage.Message.Source.ElementItem);
            onvif_free_ElementItems(&p_message->NotificationMessage.Message.Key.ElementItem);
            onvif_free_ElementItems(&p_message->NotificationMessage.Message.Data.ElementItem);

            free(p_message);
        }
    }
}

HT_API void onvif_free_NotificationMessages(NotificationMessageList ** p_head)
{
    NotificationMessageList * p_next;
    NotificationMessageList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_NotificationMessage(p_tmp);
        
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API SimpleItemList * onvif_add_SimpleItem(SimpleItemList ** p_head)
{
    SimpleItemList * p_tmp;
    SimpleItemList * p_new = (SimpleItemList *) malloc(sizeof(SimpleItemList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(SimpleItemList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_SimpleItems(SimpleItemList ** p_head)
{
    SimpleItemList * p_next;
    SimpleItemList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API ElementItemList * onvif_add_ElementItem(ElementItemList ** p_head)
{
    ElementItemList * p_tmp;
    ElementItemList * p_new = (ElementItemList *) malloc(sizeof(ElementItemList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ElementItemList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_ElementItems(ElementItemList ** p_head)
{
    ElementItemList * p_next;
    ElementItemList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->ElementItem.Any)
        {
            free(p_tmp->ElementItem.Any);
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

#ifdef IMAGE_SUPPORT

HT_API void onvif_get_ImagingPreset_token(ImagingPresetList * p_head, char * token, int size)
{
    ImagingPresetList * p_tmp = NULL;

    do {
        snprintf(token, size, "ImagingPresetToken_%u", ++g_onvif_idx.image_preset_idx);

        p_tmp = onvif_find_ImagingPreset(p_head, token);
    } while (p_tmp);
}

HT_API ImagingPresetList * onvif_add_ImagingPreset(ImagingPresetList ** p_head)
{
    ImagingPresetList * p_tmp;
    ImagingPresetList * p_new = (ImagingPresetList *) malloc(sizeof(ImagingPresetList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ImagingPresetList));

    onvif_get_ImagingPreset_token(*p_head, p_new->Preset.token, sizeof(p_new->Preset.token));

    snprintf(p_new->Preset.Name, sizeof(p_new->Preset.Name), "ImagingPresetName_%u", g_onvif_idx.image_preset_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API ImagingPresetList * onvif_find_ImagingPreset(ImagingPresetList * p_head, const char * token)
{
    ImagingPresetList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Preset.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_ImagingPresets(ImagingPresetList ** p_head)
{
    ImagingPresetList * p_next;
    ImagingPresetList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

#endif // IMAGE_SUPPORT

#ifdef AUDIO_SUPPORT

HT_API void onvif_get_AudioSource_token(AudioSourceList * p_head, char * token, int size)
{
    AudioSourceList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioSourceToken_%u", ++g_onvif_idx.a_src_idx);

        p_tmp = onvif_find_AudioSource(p_head, token);
    } while (p_tmp);
}

HT_API AudioSourceList * onvif_add_AudioSource(AudioSourceList ** p_head)
{
    AudioSourceList * p_tmp;
    AudioSourceList * p_new = (AudioSourceList *) malloc(sizeof(AudioSourceList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioSourceList));

    onvif_get_AudioSource_token(*p_head, p_new->AudioSource.token, sizeof(p_new->AudioSource.token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API AudioSourceList * onvif_find_AudioSource(AudioSourceList * p_head, const char * token)
{
    AudioSourceList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->AudioSource.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_AudioSources(AudioSourceList ** p_head)
{
    AudioSourceList * p_next;
    AudioSourceList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_AudioSourceConfiguration_token(AudioSourceConfigurationList * p_head, char * token, int size)
{
    AudioSourceConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioSourceConfigurationToken_%u", ++g_onvif_idx.a_src_cfg_idx);

        p_tmp = onvif_find_AudioSourceConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API AudioSourceConfigurationList * onvif_add_AudioSourceConfiguration(AudioSourceConfigurationList ** p_head)
{
    AudioSourceConfigurationList * p_tmp;
    AudioSourceConfigurationList * p_new = (AudioSourceConfigurationList *) malloc(sizeof(AudioSourceConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioSourceConfigurationList));

    onvif_get_AudioSourceConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "AudioSourceConfigurationName_%u", g_onvif_idx.a_src_cfg_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API AudioSourceConfigurationList * onvif_find_AudioSourceConfiguration(AudioSourceConfigurationList * p_head, const char * token)
{
    AudioSourceConfigurationList * p_tmp = p_head;

    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_AudioSourceConfigurations(AudioSourceConfigurationList ** p_head)
{
    AudioSourceConfigurationList * p_next;
    AudioSourceConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_AudioEncoder2Configuration_token(AudioEncoder2ConfigurationList * p_head, char * token, int size)
{
    AudioEncoder2ConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioEncoderConfigurationToken_%u", ++g_onvif_idx.a_enc_idx);

        p_tmp = onvif_find_AudioEncoder2Configuration(p_head, token);
    } while (p_tmp);
}

HT_API AudioEncoder2ConfigurationList * onvif_add_AudioEncoder2Configuration(AudioEncoder2ConfigurationList ** p_head, AudioEncoder2ConfigurationList * p_node)
{
    AudioEncoder2ConfigurationList * p_tmp;
    AudioEncoder2ConfigurationList * p_new = (AudioEncoder2ConfigurationList *) malloc(sizeof(AudioEncoder2ConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioEncoder2ConfigurationList));
    
    if (p_node)
    {
        memcpy(&p_new->Configuration, &p_node->Configuration, sizeof(onvif_AudioEncoder2Configuration));
    }

    onvif_get_AudioEncoder2Configuration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "AudioEncoderConfigurationName_%u", g_onvif_idx.a_enc_idx);
    
    onvif_init_MulticastConfiguration(&p_new->Configuration.Multicast);
    onvif_init_AudioEncoderConfigurationOptions(p_new);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API AudioEncoder2ConfigurationList * onvif_find_AudioEncoder2Configuration(AudioEncoder2ConfigurationList * p_head, const char * token)
{
    AudioEncoder2ConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API AudioEncoder2ConfigurationList * onvif_find_AudioEncoder2Configuration_by_param(AudioEncoder2ConfigurationList * p_head, AudioEncoder2ConfigurationList * p_node)
{
    AudioEncoder2ConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (p_tmp->Configuration.SessionTimeout == p_node->Configuration.SessionTimeout &&
            p_tmp->Configuration.SampleRate == p_node->Configuration.SampleRate && 
            p_tmp->Configuration.Bitrate == p_node->Configuration.Bitrate && 
            strcmp(p_tmp->Configuration.Encoding, p_node->Configuration.Encoding) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_AudioEncoder2Configurations(AudioEncoder2ConfigurationList ** p_head)
{
    AudioEncoder2ConfigurationList * p_next;
    AudioEncoder2ConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_AudioEncoder2ConfigurationOptions(&p_tmp->Options);

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API AudioEncoder2ConfigurationOptionsList * onvif_add_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList ** p_head)
{
    AudioEncoder2ConfigurationOptionsList * p_tmp;
    AudioEncoder2ConfigurationOptionsList * p_new = (AudioEncoder2ConfigurationOptionsList *) malloc(sizeof(AudioEncoder2ConfigurationOptionsList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioEncoder2ConfigurationOptionsList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API AudioEncoder2ConfigurationOptionsList * onvif_find_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList * p_head, const char * encoding)
{
    AudioEncoder2ConfigurationOptionsList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Options.Encoding, encoding) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList ** p_head)
{
    AudioEncoder2ConfigurationOptionsList * p_next;
    AudioEncoder2ConfigurationOptionsList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_AudioDecoderConfiguration_token(AudioDecoderConfigurationList * p_head, char * token, int size)
{
    AudioDecoderConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioDecoderConfigurationToken_%u", ++g_onvif_idx.a_dec_idx);

        p_tmp = onvif_find_AudioDecoderConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API AudioDecoderConfigurationList * onvif_add_AudioDecoderConfiguration(AudioDecoderConfigurationList ** p_head)
{
    AudioDecoderConfigurationList * p_tmp;
    AudioDecoderConfigurationList * p_new = (AudioDecoderConfigurationList *) malloc(sizeof(AudioDecoderConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioDecoderConfigurationList));

    onvif_get_AudioDecoderConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "AudioDecoderConfigurationName_%u", g_onvif_idx.a_dec_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API AudioDecoderConfigurationList * onvif_find_AudioDecoderConfiguration(AudioDecoderConfigurationList * p_head, const char * token)
{
    AudioDecoderConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_AudioDecoderConfigurations(AudioDecoderConfigurationList ** p_head)
{
    AudioDecoderConfigurationList * p_next;
    AudioDecoderConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

#if defined(MEDIA2_SUPPORT)
        onvif_free_AudioEncoder2ConfigurationOptions(&p_tmp->Options2);
#endif

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

/*
 * Initialize the audio source
 * 
 */
void onvif_init_AudioSource()
{
    AudioSourceList * p_node;
    
    if (g_onvif_cfg.a_src)
    {
        return;
    }
    
    // todo : here init one audio source (2 channels)

    p_node = onvif_add_AudioSource(&g_onvif_cfg.a_src);
    if (p_node)
    {
        p_node->AudioSource.Channels = 2;
    }
}

void onvif_init_AudioSourceConfiguration()
{
    AudioSourceConfigurationList * p_item;

    if (g_onvif_cfg.a_src_cfg)
    {
        return;
    }

    p_item = onvif_add_AudioSourceConfiguration(&g_onvif_cfg.a_src_cfg);
    if (p_item)
    {
        AudioSourceList * p_a_src = g_onvif_cfg.a_src;
        if (NULL == p_a_src)
        {
            p_a_src = onvif_add_AudioSource(&g_onvif_cfg.a_src);
            if (p_a_src)
            {
                p_a_src->AudioSource.Channels = 2;
            }
        }

        if (p_a_src)
        {
            strcpy(p_item->Configuration.SourceToken, p_a_src->AudioSource.token);
        }
    }
}

void onvif_init_AudioEncoderConfiguration()
{
    AudioEncoder2ConfigurationList * p_item;

    if (g_onvif_cfg.a_enc_cfg)
    {
        return;
    }

    p_item = onvif_add_AudioEncoder2Configuration(&g_onvif_cfg.a_enc_cfg, NULL);
    if (p_item)
    {
        strcpy(p_item->Configuration.Encoding, "PCMU");
        p_item->Configuration.AudioEncoding = AudioEncoding_G711;
        p_item->Configuration.SampleRate = 8;
        p_item->Configuration.Bitrate = 64;
        p_item->Configuration.SessionTimeout = 10;
    }
}

void onvif_init_AudioEncoder2ConfigurationOptions(onvif_AudioEncoder2ConfigurationOptions * p_option, const char * Encoding)
{
    strcpy(p_option->Encoding, Encoding);
    if (strcasecmp(Encoding, "PCMU") == 0)
    {
        p_option->AudioEncoding = AudioEncoding_G711;
    }
    else if (strcasecmp(Encoding, "G726") == 0)
    {
        p_option->AudioEncoding = AudioEncoding_G726;
    }
    else if (strcasecmp(Encoding, "MP4A-LATM") == 0)
    {
        p_option->AudioEncoding = AudioEncoding_AAC;
    }
    else
    {
        p_option->AudioEncoding = AudioEncoding_Unknown;
    }

    p_option->BitrateList.sizeItems = 5;
    p_option->BitrateList.Items[0] = 16;
    p_option->BitrateList.Items[1] = 24;
    p_option->BitrateList.Items[2] = 32;
    p_option->BitrateList.Items[3] = 48;
    p_option->BitrateList.Items[4] = 64;
    
    // specify the supported samplerate
    p_option->SampleRateList.sizeItems = 5;
    p_option->SampleRateList.Items[0] = 8;
    p_option->SampleRateList.Items[1] = 16;
    p_option->SampleRateList.Items[2] = 24;
    p_option->SampleRateList.Items[3] = 32;
    p_option->SampleRateList.Items[4] = 48;
}

/*
 * Initialize the audio encoder configuration options
 * 
 */
HT_API void onvif_init_AudioEncoderConfigurationOptions(AudioEncoder2ConfigurationList * p_item)
{
    AudioEncoder2ConfigurationOptionsList * p_option;

    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_item->Options);

    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "PCMU");

    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_item->Options);

    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "G726");

    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_item->Options);

    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "MP4A-LATM");

    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_item->Options);

    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "OPUS");
}

void onvif_init_AudioDecoderConfigurations()
{
    AudioDecoderConfigurationList * p_a_dec_cfg;
#ifdef MEDIA2_SUPPORT
    AudioEncoder2ConfigurationOptionsList * p_option;
#endif

    if (g_onvif_cfg.a_dec_cfg)
    {
        return;
    }
    
    p_a_dec_cfg = onvif_add_AudioDecoderConfiguration(&g_onvif_cfg.a_dec_cfg);
    
    p_a_dec_cfg->Options.G711DecOptionsFlag = 1;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.sizeItems = 6;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[0] = 8;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[1] = 12;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[2] = 20;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[3] = 25;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[4] = 32;
    p_a_dec_cfg->Options.G711DecOptions.Bitrate.Items[5] = 40;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.sizeItems = 5;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.Items[0] = 8;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.Items[1] = 12;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.Items[2] = 24;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.Items[3] = 32;
    p_a_dec_cfg->Options.G711DecOptions.SampleRateRange.Items[4] = 48;

#ifdef MEDIA2_SUPPORT
    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_a_dec_cfg->Options2);
    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "PCMU");
#endif

    p_a_dec_cfg = onvif_add_AudioDecoderConfiguration(&g_onvif_cfg.a_dec_cfg);

    p_a_dec_cfg->Options.G726DecOptionsFlag = 1;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.sizeItems = 6;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[0] = 8;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[1] = 12;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[2] = 20;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[3] = 25;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[4] = 32;
    p_a_dec_cfg->Options.G726DecOptions.Bitrate.Items[5] = 40;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.sizeItems = 5;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.Items[0] = 8;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.Items[1] = 12;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.Items[2] = 24;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.Items[3] = 32;
    p_a_dec_cfg->Options.G726DecOptions.SampleRateRange.Items[4] = 48;

#ifdef MEDIA2_SUPPORT
    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_a_dec_cfg->Options2);
    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "G726");
#endif

    p_a_dec_cfg = onvif_add_AudioDecoderConfiguration(&g_onvif_cfg.a_dec_cfg);

    p_a_dec_cfg->Options.AACDecOptionsFlag = 1;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.sizeItems = 6;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[0] = 8;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[1] = 12;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[2] = 20;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[3] = 25;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[4] = 32;
    p_a_dec_cfg->Options.AACDecOptions.Bitrate.Items[5] = 40;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.sizeItems = 5;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.Items[0] = 8;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.Items[1] = 12;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.Items[2] = 24;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.Items[3] = 32;
    p_a_dec_cfg->Options.AACDecOptions.SampleRateRange.Items[4] = 48;

#ifdef MEDIA2_SUPPORT
    p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_a_dec_cfg->Options2);
    onvif_init_AudioEncoder2ConfigurationOptions(&p_option->Options, "AAC");
#endif
}

#endif // end of AUDIO_SUPPORT

#ifdef MEDIA2_SUPPORT

HT_API void onvif_get_Mask_token(MaskList * p_head, char * token, int size)
{
    MaskList * p_tmp = NULL;

    do {
        snprintf(token, size, "MaskToken_%u", ++g_onvif_idx.mask_idx);

        p_tmp = onvif_find_Mask(p_head, token);
    } while (p_tmp);
}

HT_API MaskList * onvif_add_Mask(MaskList ** p_head)
{
    MaskList * p_tmp;
    MaskList * p_new = (MaskList *) malloc(sizeof(MaskList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(MaskList));

    onvif_get_Mask_token(*p_head, p_new->Mask.token, sizeof(p_new->Mask.token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API MaskList * onvif_find_Mask(MaskList * p_head, const char * token)
{
    MaskList * p_tmp = p_head;

    if (NULL == token)
    {
        return NULL;
    }
    
    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Mask.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Masks(MaskList ** p_head)
{
    MaskList * p_next;
    MaskList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_MaskOptions()
{
    onvif_MaskOptions * p_opt = &g_onvif_cfg.MaskOptions;
    
    p_opt->MaxMasks = 10;
    p_opt->MaxPoints = 10;

    p_opt->sizeTypes = 3;
    strcpy(p_opt->Types[0], "Color");
    strcpy(p_opt->Types[1], "Pixelated");
    strcpy(p_opt->Types[2], "Blurred");

    p_opt->Color.sizeColorList = 1;
    p_opt->Color.ColorList[0].X = 100;
    p_opt->Color.ColorList[0].Y = 100;
    p_opt->Color.ColorList[0].Z = 100;
    p_opt->Color.ColorList[0].ColorspaceFlag = 1;
    strcpy(p_opt->Color.ColorList[0].Colorspace, "http://www.onvif.org/ver10/colorspace/YCbCr");
    p_opt->Color.sizeColorspaceRange = 0;

    p_opt->RectangleOnly = TRUE;
    p_opt->SingleColorOnly = FALSE;
}

#endif // end of MEDIA2_SUPPORT

#ifdef PTZ_SUPPORT

HT_API void onvif_get_PTZNode_token(PTZNodeList * p_head, char * token, int size)
{
    PTZNodeList * p_tmp = NULL;

    do {
        snprintf(token, size, "PTZNodeToken_%u", ++g_onvif_idx.ptznode_idx);

        p_tmp = onvif_find_PTZNode(p_head, token);
    } while (p_tmp);
}

HT_API PTZNodeList * onvif_add_PTZNode(PTZNodeList ** p_head)
{
    PTZNodeList * p_tmp;
    PTZNodeList * p_new = (PTZNodeList *) malloc(sizeof(PTZNodeList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZNodeList));

    onvif_get_PTZNode_token(*p_head, p_new->PTZNode.token, sizeof(p_new->PTZNode.token));

    snprintf(p_new->PTZNode.Name, sizeof(p_new->PTZNode.Name), "PTZNodeName_%u", g_onvif_idx.ptznode_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API PTZNodeList * onvif_find_PTZNode(PTZNodeList * p_head, const char * token)
{
    PTZNodeList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->PTZNode.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_PTZNodes(PTZNodeList ** p_head)
{
    PTZNodeList * p_next;
    PTZNodeList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_PTZConfiguration_token(PTZConfigurationList * p_head, char * token, int size)
{
    PTZConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "PTZConfigurationToken_%u", ++g_onvif_idx.ptzcfg_idx);

        p_tmp = onvif_find_PTZConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API PTZConfigurationList * onvif_add_PTZConfiguration(PTZConfigurationList ** p_head)
{
    PTZConfigurationList * p_tmp;
    PTZConfigurationList * p_new = (PTZConfigurationList *) malloc(sizeof(PTZConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZConfigurationList));

    onvif_get_PTZConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));
    
    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "PTZConfigurationName_%u", g_onvif_idx.ptzcfg_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    onvif_init_PTZConfigurationOptions(p_new);
    
    return p_new;
}

HT_API PTZConfigurationList * onvif_find_PTZConfiguration(PTZConfigurationList * p_head, const char * token)
{
    PTZConfigurationList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_PTZConfigurations(PTZConfigurationList ** p_head)
{
    PTZConfigurationList * p_next;
    PTZConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_PTZPreset_token(PTZPresetList * p_head, char * token, int size)
{
    PTZPresetList * p_tmp = NULL;

    do {
        snprintf(token, size, "PTZPresetToken_%u", ++g_onvif_idx.preset_idx);

        p_tmp = onvif_find_PTZPreset(p_head, token);
    } while (p_tmp);
}

HT_API PTZPresetList * onvif_add_PTZPreset(PTZPresetList ** p_head)
{
    PTZPresetList * p_tmp;
    PTZPresetList * p_new = (PTZPresetList *) malloc(sizeof(PTZPresetList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZPresetList));

    onvif_get_PTZPreset_token(*p_head, p_new->PTZPreset.token, sizeof(p_new->PTZPreset.token));
    
    snprintf(p_new->PTZPreset.Name, sizeof(p_new->PTZPreset.Name), "PTZPresetName_%u", g_onvif_idx.preset_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API PTZPresetList * onvif_find_PTZPreset(PTZPresetList * p_head, const char  * preset_token)
{
    PTZPresetList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->PTZPreset.token, preset_token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_PTZPreset(PTZPresetList ** p_head, PTZPresetList * p_node)
{
    PTZPresetList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    free(p_node);
}

HT_API void onvif_free_PTZPresets(PTZPresetList ** p_head)
{
    PTZPresetList * p_next;
    PTZPresetList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API PTZPresetTourSpotList * onvif_add_PTZPresetTourSpot(PTZPresetTourSpotList ** p_head)
{
    PTZPresetTourSpotList * p_tmp;
    PTZPresetTourSpotList * p_new = (PTZPresetTourSpotList *) malloc(sizeof(PTZPresetTourSpotList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZPresetTourSpotList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API void onvif_free_PTZPresetTourSpots(PTZPresetTourSpotList ** p_head)
{
    PTZPresetTourSpotList * p_next;
    PTZPresetTourSpotList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_PresetTour_token(PresetTourList * p_head, char * token, int size)
{
    PresetTourList * p_tmp = NULL;

    do {
        snprintf(token, size, "PresetTourToken_%u", ++g_onvif_idx.preset_tour_idx);

        p_tmp = onvif_find_PresetTour(p_head, token);
    } while (p_tmp);
}

HT_API PresetTourList * onvif_add_PresetTour(PresetTourList ** p_head)
{
    PresetTourList * p_tmp;
    PresetTourList * p_new = (PresetTourList *) malloc(sizeof(PresetTourList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PresetTourList));

    onvif_get_PresetTour_token(*p_head, p_new->PresetTour.token, sizeof(p_new->PresetTour.token));
    
    snprintf(p_new->PresetTour.Name, sizeof(p_new->PresetTour.Name), "PresetTourName_%u", g_onvif_idx.preset_tour_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }
    
    return p_new;
}

HT_API PresetTourList * onvif_find_PresetTour(PresetTourList * p_head, const char * token)
{
    PresetTourList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->PresetTour.token, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_PresetTour(PresetTourList ** p_head, PresetTourList * p_node)
{
    PresetTourList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    onvif_free_PTZPresetTourSpots(&p_node->PresetTour.TourSpot);
    
    free(p_node);
}

HT_API void onvif_free_PresetTours(PresetTourList ** p_head)
{
    PresetTourList * p_next;
    PresetTourList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_PTZPresetTourSpots(&p_tmp->PresetTour.TourSpot);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API int onvif_count_PresetTours(PresetTourList * p_head)
{
    int count = 0;
    
    PresetTourList * p_tmp = p_head;

    while (p_tmp)
    {
        count++;
        
        p_tmp = p_tmp->next;
    }

    return count;
}

/**
 * init PTZ node
 */
void onvif_init_PTZNode()
{
    PTZNodeList * p_node;

    if (g_onvif_cfg.ptz_node)
    {
        return;
    }
    
    // todo : init one ptz node

    p_node = onvif_add_PTZNode(&g_onvif_cfg.ptz_node);
    if (NULL == p_node)
    {
        return;
    }

    p_node->PTZNode.NameFlag = 1;
    
    p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.XRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.XRange.Max = 1.0;
    p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.YRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.YRange.Max = 1.0;
    
    p_node->PTZNode.SupportedPTZSpaces.AbsoluteZoomPositionSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.AbsoluteZoomPositionSpace.XRange.Min = 0.0;
    p_node->PTZNode.SupportedPTZSpaces.AbsoluteZoomPositionSpace.XRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.XRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.XRange.Max = 1.0;
    p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.YRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.YRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.RelativeZoomTranslationSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.RelativeZoomTranslationSpace.XRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.RelativeZoomTranslationSpace.XRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.XRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.XRange.Max = 1.0;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.YRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.YRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.ContinuousZoomVelocitySpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousZoomVelocitySpace.XRange.Min = -1.0;
    p_node->PTZNode.SupportedPTZSpaces.ContinuousZoomVelocitySpace.XRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.PanTiltSpeedSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.PanTiltSpeedSpace.XRange.Min = 0.0;
    p_node->PTZNode.SupportedPTZSpaces.PanTiltSpeedSpace.XRange.Max = 1.0;

    p_node->PTZNode.SupportedPTZSpaces.ZoomSpeedSpaceFlag = 1;
    p_node->PTZNode.SupportedPTZSpaces.ZoomSpeedSpace.XRange.Min = 0.0;
    p_node->PTZNode.SupportedPTZSpaces.ZoomSpeedSpace.XRange.Max = 1.0;

    p_node->PTZNode.MaximumNumberOfPresets = MAX_PTZ_PRESETS;
    p_node->PTZNode.HomeSupported = TRUE;
    
    p_node->PTZNode.ExtensionFlag = 1;
    p_node->PTZNode.Extension.SupportedPresetTourFlag = 1;
    p_node->PTZNode.Extension.SupportedPresetTour.MaximumNumberOfPresetTours = 10;
    p_node->PTZNode.Extension.SupportedPresetTour.PTZPresetTourOperation_Start = 1;
    p_node->PTZNode.Extension.SupportedPresetTour.PTZPresetTourOperation_Stop = 1;
    p_node->PTZNode.Extension.SupportedPresetTour.PTZPresetTourOperation_Pause = 1;
    p_node->PTZNode.Extension.SupportedPresetTour.PTZPresetTourOperation_Extended = 0;

    p_node->PTZNode.FixedHomePosition = FALSE;
    p_node->PTZNode.GeoMove = TRUE;

    p_node->PTZNode.sizeAuxiliaryCommands = 2;
    strcpy(p_node->PTZNode.AuxiliaryCommands[0], "Wiper start");
    strcpy(p_node->PTZNode.AuxiliaryCommands[1], "Wiper stop");
}

/**
 * init ptz configuration
 */
void onvif_init_PTZConfiguration()
{
    PTZConfigurationList * p_node;
    
    if (g_onvif_cfg.ptz_cfg)
    {
        return;
    }
    
    p_node = onvif_add_PTZConfiguration(&g_onvif_cfg.ptz_cfg);
    if (NULL == p_node)
    {
        return;
    }

    if (g_onvif_cfg.ptz_node)
    {
        strcpy(p_node->Configuration.NodeToken, g_onvif_cfg.ptz_node->PTZNode.token);
    }
    else
    {
        log_print(HT_LOG_WARN, "%s, PTZ node is empty!!!\r\n", __FUNCTION__);
    }
    
    p_node->Configuration.DefaultPTZSpeedFlag = 1;
    p_node->Configuration.DefaultPTZSpeed.PanTiltFlag = 1;
    p_node->Configuration.DefaultPTZSpeed.PanTilt.x = 0.5;
    p_node->Configuration.DefaultPTZSpeed.PanTilt.y = 0.5;
    p_node->Configuration.DefaultPTZSpeed.ZoomFlag = 1;
    p_node->Configuration.DefaultPTZSpeed.Zoom.x = 0.5;

    p_node->Configuration.DefaultPTZTimeoutFlag = 1;
    p_node->Configuration.DefaultPTZTimeout = 5;

    p_node->Configuration.PanTiltLimitsFlag = 1;
    p_node->Configuration.PanTiltLimits.XRange.Min = -1.0;
    p_node->Configuration.PanTiltLimits.XRange.Max = 1.0;
    p_node->Configuration.PanTiltLimits.YRange.Min = -1.0;
    p_node->Configuration.PanTiltLimits.YRange.Max = 1.0;

    p_node->Configuration.ZoomLimitsFlag = 1;
    p_node->Configuration.ZoomLimits.XRange.Min = 0;
    p_node->Configuration.ZoomLimits.XRange.Max = 1.0;

    p_node->Configuration.ExtensionFlag = 1;
    p_node->Configuration.Extension.PTControlDirectionFlag = 1;
    p_node->Configuration.Extension.PTControlDirection.EFlipFlag = 1;
    p_node->Configuration.Extension.PTControlDirection.EFlip = EFlipMode_OFF;
    p_node->Configuration.Extension.PTControlDirection.ReverseFlag = 1;
    p_node->Configuration.Extension.PTControlDirection.Reverse= ReverseMode_OFF;
}

HT_API void onvif_init_PTZConfigurationOptions(PTZConfigurationList * p_item)
{
    p_item->Options.PTZTimeout.Min = 1;
    p_item->Options.PTZTimeout.Max = 100;

    p_item->Options.PTControlDirectionFlag = 1;
    p_item->Options.PTControlDirection.EFlipMode_OFF = 1;
    p_item->Options.PTControlDirection.EFlipMode_ON = 1;
    p_item->Options.PTControlDirection.ReverseMode_OFF = 1;
    p_item->Options.PTControlDirection.ReverseMode_ON = 1;
    p_item->Options.PTControlDirection.ReverseMode_AUTO = 1;
}

#endif // end of PTZ_SUPPORT

#ifdef VIDEO_ANALYTICS

HT_API ConfigList * onvif_add_Config(ConfigList ** p_head)
{
    ConfigList * p_tmp;
    ConfigList * p_new = (ConfigList *) malloc(sizeof(ConfigList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ConfigList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_Config(ConfigList * p_node)
{
    onvif_free_SimpleItems(&p_node->Config.Parameters.SimpleItem);
    onvif_free_ElementItems(&p_node->Config.Parameters.ElementItem);
}

HT_API void onvif_free_Configs(ConfigList ** p_head)
{
    ConfigList * p_next;
    ConfigList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_Config(p_tmp);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API ConfigList * onvif_find_Config(ConfigList * p_head, const char * name)
{
    ConfigList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->Config.Name, name) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API ConfigList * onvif_find_Config_by_type(ConfigList * p_head, const char * type)
{
    ConfigList * p_tmp = p_head;
    while (p_tmp)
    {
        if (soap_strcmp(p_tmp->Config.Type, type) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_remove_Config(ConfigList ** p_head, ConfigList * p_remove)
{
    BOOL found = FALSE;
    ConfigList * p_prev = NULL;
    ConfigList * p_cfg = *p_head;    
    
    while (p_cfg)
    {
        if (p_cfg == p_remove)
        {
            found = TRUE;
            break;
        }

        p_prev = p_cfg;
        p_cfg = p_cfg->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_cfg->next;
        }
        else
        {
            p_prev->next = p_cfg->next;
        }

        onvif_free_Config(p_cfg);
        free(p_cfg);
    }
}

HT_API ConfigList * onvif_get_prev_Config(ConfigList * p_head, ConfigList * p_found)
{
    ConfigList * p_prev = p_head;
    
    if (p_found == p_head)
    {
        return NULL;
    }

    while (p_prev)
    {
        if (p_prev->next == p_found)
        {
            break;
        }
        
        p_prev = p_prev->next;
    }

    return p_prev;
}

HT_API ConfigDescriptionList * onvif_add_ConfigDescription(ConfigDescriptionList ** p_head)
{
    ConfigDescriptionList * p_tmp;
    ConfigDescriptionList * p_new = (ConfigDescriptionList *) malloc(sizeof(ConfigDescriptionList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ConfigDescriptionList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_ConfigDescriptions(ConfigDescriptionList ** p_head)
{
    ConfigDescriptionList * p_next;
    ConfigDescriptionList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_SimpleItemDescriptions(&p_tmp->ConfigDescription.Parameters.SimpleItemDescription);
        onvif_free_SimpleItemDescriptions(&p_tmp->ConfigDescription.Parameters.ElementItemDescription);
        
        onvif_free_ConfigDescription_Messages(&p_tmp->ConfigDescription.Messages);

        onvif_free_ConfigOptions(&p_tmp->ConfigOptions);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API ConfigDescription_MessagesList * onvif_add_ConfigDescription_Message(ConfigDescription_MessagesList ** p_head)
{
    ConfigDescription_MessagesList * p_tmp;
    ConfigDescription_MessagesList * p_new = (ConfigDescription_MessagesList *) malloc(sizeof(ConfigDescription_MessagesList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ConfigDescription_MessagesList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_ConfigDescription_Message(ConfigDescription_MessagesList * p_item)
{
    onvif_free_SimpleItemDescriptions(&p_item->Messages.Source.SimpleItemDescription);
    onvif_free_SimpleItemDescriptions(&p_item->Messages.Source.ElementItemDescription);

    onvif_free_SimpleItemDescriptions(&p_item->Messages.Key.SimpleItemDescription);
    onvif_free_SimpleItemDescriptions(&p_item->Messages.Key.ElementItemDescription);

    onvif_free_SimpleItemDescriptions(&p_item->Messages.Data.SimpleItemDescription);
    onvif_free_SimpleItemDescriptions(&p_item->Messages.Data.ElementItemDescription);
}

HT_API void onvif_free_ConfigDescription_Messages(ConfigDescription_MessagesList ** p_head)
{
    ConfigDescription_MessagesList * p_next;
    ConfigDescription_MessagesList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        onvif_free_ConfigDescription_Message(p_tmp);

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API ConfigOptionsList * onvif_add_ConfigOptions(ConfigOptionsList ** p_head)
{
    ConfigOptionsList * p_tmp;
    ConfigOptionsList * p_new = (ConfigOptionsList *) malloc(sizeof(ConfigOptionsList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ConfigOptionsList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_ConfigOptions(ConfigOptionsList ** p_head)
{
    ConfigOptionsList * p_next;
    ConfigOptionsList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->Options.any)
        {
            free(p_tmp->Options.any);
        }

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API SimpleItemDescriptionList * onvif_add_SimpleItemDescription(SimpleItemDescriptionList ** p_head)
{
    SimpleItemDescriptionList * p_tmp;
    SimpleItemDescriptionList * p_new = (SimpleItemDescriptionList *) malloc(sizeof(SimpleItemDescriptionList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(SimpleItemDescriptionList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_SimpleItemDescriptions(SimpleItemDescriptionList ** p_head)
{
    SimpleItemDescriptionList * p_next;
    SimpleItemDescriptionList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoAnalyticsConfiguration_token(VideoAnalyticsConfigurationList * p_head, char * token, int size)
{
    VideoAnalyticsConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoAnalyticsConfigurationToken_%u", ++g_onvif_idx.va_idx);

        p_tmp = onvif_find_VideoAnalyticsConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API VideoAnalyticsConfigurationList * onvif_add_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList ** p_head)
{
    VideoAnalyticsConfigurationList * p_tmp;
    VideoAnalyticsConfigurationList * p_new = (VideoAnalyticsConfigurationList *) malloc(sizeof(VideoAnalyticsConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoAnalyticsConfigurationList));

    onvif_get_VideoAnalyticsConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));

    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "VideoAnalyticsConfigurationName_%u", g_onvif_idx.va_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API VideoAnalyticsConfigurationList * onvif_find_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList * p_head, const char * token)
{
    VideoAnalyticsConfigurationList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->Configuration.token, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_VideoAnalyticsConfigurations(VideoAnalyticsConfigurationList ** p_head)
{
    VideoAnalyticsConfigurationList * p_next;
    VideoAnalyticsConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_Configs(&p_tmp->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
        onvif_free_Configs(&p_tmp->Configuration.RuleEngineConfiguration.Rule);

        onvif_free_ConfigDescriptions(&p_tmp->SupportedRules.RuleDescription);
        onvif_free_ConfigDescriptions(&p_tmp->SupportedAnalyticsModules.AnalyticsModuleDescription);

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_rule_CellMotionDetector(onvif_SupportedRules * p_item)
{
    ConfigOptionsList * p_options;
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:CellMotionDetector");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 10;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "MinCount");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:integer");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "AlarmOnDelay");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:integer");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "AlarmOffDelay");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:integer");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "ActiveCells");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:base64Binary");
        }

        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/CellMotionDetector/Motion");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSourceConfigurationToken");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoAnalyticsConfigurationToken");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Rule");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "IsMotion");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:boolean");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:CellMotionDetector");
            strcpy(p_options->Options.Name, "MinCount");
            strcpy(p_options->Options.Type, "tt:IntegerRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:IntegerRange>"
                        "<tt:Min>1</tt:Min>"
                        "<tt:Max>100</tt:Max>"
                    "</tt:IntegerRange>");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:CellMotionDetector");
            strcpy(p_options->Options.Name, "AlarmOnDelay");
            strcpy(p_options->Options.Type, "tt:IntegerRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:IntegerRange>"
                        "<tt:Min>1000</tt:Min>"
                        "<tt:Max>100000</tt:Max>"
                    "</tt:IntegerRange>");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:CellMotionDetector");
            strcpy(p_options->Options.Name, "AlarmOffDelay");
            strcpy(p_options->Options.Type, "tt:IntegerRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:IntegerRange>"
                        "<tt:Min>1000</tt:Min>"
                        "<tt:Max>100000</tt:Max>"
                    "</tt:IntegerRange>");
            }
        }
    }
}

void onvif_init_rule_MotionRegionDetector(onvif_SupportedRules * p_item)
{
    ConfigOptionsList * p_options;
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:MotionRegionDetector");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 10;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.ElementItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "MotionRegion");
            strcpy(p_desc->SimpleItemDescription.Type, "axt:MotionRegionConfig");
        }

        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/MotionRegionDetector/Motion");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSource");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "RuleName");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "State");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:boolean");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            strcpy(p_options->Options.RuleType, "tt:MotionRegionDetector");
            strcpy(p_options->Options.Name, "MotionRegion");
            strcpy(p_options->Options.Type, "axt:MotionRegionConfigOptions");

            p_options->Options.any = (char *) malloc(1024);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<axt:MotionRegionConfigOptions>\r\n"
                        "<tan:DisarmSupport>true</tan:DisarmSupport>\r\n"
                        "<tan:PolygonSupport>true</tan:PolygonSupport>\r\n"
                        "<tan:PolygonLimits>\r\n"
                            "<tt:Min>1</tt:Min>\r\n"
                            "<tt:Max>20</tt:Max>\r\n"
                        "</tan:PolygonLimits>\r\n"
                        "<tan:RuleNotification>true</tan:RuleNotification>\r\n"
                        "<tan:SingleSensitivitySupport>true</tan:SingleSensitivitySupport>\r\n"
                        "<tan:PTZPresetMotionSupport>true</tan:PTZPresetMotionSupport>\r\n"
                    "</axt:MotionRegionConfigOptions>\r\n");
            }    
        }
    }
}

void onvif_init_rule_FaceRecognition(onvif_SupportedRules * p_item)
{
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:FaceRecognition");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "IncludeImage");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
        }
        
        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/Recognition/Face");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSource");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "AnalyticsConfiguration");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Rule");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Likelihood");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:float");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Label");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "ImageUri");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:anyURI");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "EnrollmentID");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "RefImageUri");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:anyURI");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Image");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:base64Binary");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "BoundingBox");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:Rectangle");
            }
        }
    }
}

void onvif_init_rule_LicensePlateRecognition(onvif_SupportedRules * p_item)
{
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:LicensePlateRecognition");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "IncludeImage");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "PlateLocation");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.ElementItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Region");
            strcpy(p_desc->SimpleItemDescription.Type, "tt:Polygon");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.ElementItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "SnapLine");
            strcpy(p_desc->SimpleItemDescription.Type, "tt:Polyline");
        }
        
        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/Recognition/LicensePlate");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSource");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "AnalyticsConfiguration");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Rule");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Likelihood");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:float");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Label");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "ImageUri");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:anyURI");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VehicleImageURI");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:anyURI");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "BoundingBox");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:Rectangle");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Image");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:base64Binary");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "LicensePlateInfo");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:LicensePlateInfo");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VehicleInfo");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:VehicleInfo");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.ElementItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VehicleImage");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:base64Binary");
            }
        }
    }
}

void onvif_init_rule_LineCounting(onvif_SupportedRules * p_item)
{
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:LineCounting");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "ReportTimeInterval");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:duration");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "ResetTime");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:time");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Direction");
            strcpy(p_desc->SimpleItemDescription.Type, "tt:Direction");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "PassAllPolylines");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:boolean");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.ElementItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Segments");
            strcpy(p_desc->SimpleItemDescription.Type, "tt:Polyline");
        }

        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/CountAggregation/Counter");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSource");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "AnalyticsConfiguration");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Rule");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Count");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:int");
            }
        }
    }
}

void onvif_init_rule_TamperingDetection(onvif_SupportedRules * p_item)
{
    ConfigOptionsList * p_options;
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->RuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:TamperingDetection");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Mode");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Threshold");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:float");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Duration");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:duration");
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:TamperingDetection");
            strcpy(p_options->Options.Name, "Mode");
            strcpy(p_options->Options.Type, "tt:StringItems");

            p_options->Options.any = (char *) malloc(500);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:StringItems>\r\n"
                        "<tt:Item>GlobalSceneChange</tt:Item>\r\n"
                        "<tt:Item>ImageTooDark</tt:Item>\r\n"
                        "<tt:Item>ImageTooBright</tt:Item>\r\n"
                        "<tt:Item>SignalLoss</tt:Item>\r\n"
                        "<tt:Item>ImageTooBlurry</tt:Item>\r\n"
                    "</tt:StringItems>\r\n");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:TamperingDetection");
            strcpy(p_options->Options.Name, "Threshold");
            strcpy(p_options->Options.Type, "tt:FloatRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:FloatRange>\r\n"
                        "<tt:Min>0.0</tt:Min>\r\n"
                        "<tt:Max>100.0</tt:Max>\r\n"
                    "</tt:FloatRange>\r\n");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            p_options->Options.RuleTypeFlag = 1;
            strcpy(p_options->Options.RuleType, "tt:TamperingDetection");
            strcpy(p_options->Options.Name, "Duration");
            strcpy(p_options->Options.Type, "tt:DurationRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:DurationRange>\r\n"
                        "<tt:Min>PT1S</tt:Min>\r\n"
                        "<tt:Max>PT1M</tt:Max>\r\n"
                    "</tt:DurationRange>\r\n");
            }
        }
    }
}

void onvif_init_SupportedRules(onvif_SupportedRules * p_item)
{
    p_item->sizeRuleContentSchemaLocation = 1;
    strcpy(p_item->RuleContentSchemaLocation[0], "http://www.w3.org/2001/XMLSchema");

    // add tt:CellMotionDetector rule
    onvif_init_rule_CellMotionDetector(p_item);

    // add tt:MotionRegionDetector rule
    onvif_init_rule_MotionRegionDetector(p_item);

    // add tt:FaceRecognition rule
    onvif_init_rule_FaceRecognition(p_item);

    // add tt:LicensePlateRecognition rule
    onvif_init_rule_LicensePlateRecognition(p_item);

    // add tt:LineCounting rule
    onvif_init_rule_LineCounting(p_item);

    // add tt:TamperingDetection
    onvif_init_rule_TamperingDetection(p_item);
}

void onvif_init_CellMotionEngine(onvif_SupportedAnalyticsModules * p_item)
{
    ConfigOptionsList * p_options;
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->AnalyticsModuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:CellMotionEngine");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;
        
        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Sensitivity");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:integer");
        }

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.ElementItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Layout");
            strcpy(p_desc->SimpleItemDescription.Type, "tt:CellLayout");
        }

        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/CellMotionDetector/Motion");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSourceConfigurationToken");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoAnalyticsConfigurationToken");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "Rule");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "IsMotion");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:boolean");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            strcpy(p_options->Options.RuleType, "tt:CellMotionEngine");
            strcpy(p_options->Options.Name, "Sensitivity");
            strcpy(p_options->Options.Type, "tt:IntRange");

            p_options->Options.any = (char *) malloc(128);
            if (p_options->Options.any)
            {
                strcpy(p_options->Options.any, 
                    "<tt:IntRange>\r\n"
                        "<tt:Min>0</tt:Min>\r\n"
                        "<tt:Max>100</tt:Max>\r\n"
                    "</tt:IntRange>\r\n");
            }    
        }
    }
}

void onvif_init_MotionRegionDetector(onvif_SupportedAnalyticsModules * p_item)
{
    ConfigOptionsList * p_options;
    ConfigDescriptionList * p_cfg_desc;
    SimpleItemDescriptionList * p_desc;
    ConfigDescription_MessagesList * p_message;

    p_cfg_desc = onvif_add_ConfigDescription(&p_item->AnalyticsModuleDescription);
    if (p_cfg_desc)
    {
        strcpy(p_cfg_desc->ConfigDescription.Name, "tt:MotionRegionDetector");
        p_cfg_desc->ConfigDescription.maxInstancesFlag = 1;
        p_cfg_desc->ConfigDescription.maxInstances = 1;

        p_desc = onvif_add_SimpleItemDescription(&p_cfg_desc->ConfigDescription.Parameters.SimpleItemDescription);
        if (p_desc)
        {
            strcpy(p_desc->SimpleItemDescription.Name, "Sensitivity");
            strcpy(p_desc->SimpleItemDescription.Type, "xs:integer");
        }

        p_message = onvif_add_ConfigDescription_Message(&p_cfg_desc->ConfigDescription.Messages);
        if (p_message)
        {
            p_message->Messages.IsPropertyFlag = 1;
            p_message->Messages.IsProperty = TRUE;
            strcpy(p_message->Messages.ParentTopic, "tns1:RuleEngine/MotionRegionDetector/Motion");

            p_message->Messages.SourceFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "VideoSource");
                strcpy(p_desc->SimpleItemDescription.Type, "tt:ReferenceToken");
            }

            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Source.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "RuleName");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:string");
            }

            p_message->Messages.DataFlag = 1;
            
            p_desc = onvif_add_SimpleItemDescription(&p_message->Messages.Data.SimpleItemDescription);
            if (p_desc)
            {
                strcpy(p_desc->SimpleItemDescription.Name, "State");
                strcpy(p_desc->SimpleItemDescription.Type, "xs:boolean");
            }
        }

        p_options = onvif_add_ConfigOptions(&p_cfg_desc->ConfigOptions);
        if (p_options)
        {
            strcpy(p_options->Options.RuleType, "tt:MotionRegionDetector");
            strcpy(p_options->Options.Name, "Sensitivity");
            strcpy(p_options->Options.Type, "tt:IntRange");

            p_options->Options.any = (char *) malloc(128);

            strcpy(p_options->Options.any, 
                "<tt:IntRange>\r\n"
                    "<tt:Min>0</tt:Min>\r\n"
                    "<tt:Max>10</tt:Max>\r\n"
                "</tt:IntRange>\r\n");
        }
    }
}

void onvif_init_SupportedAnalyticsModules(onvif_SupportedAnalyticsModules * p_item)
{
    p_item->sizeAnalyticsModuleContentSchemaLocation = 1;
    strcpy(p_item->AnalyticsModuleContentSchemaLocation[0], "http://www.w3.org/2001/XMLSchema");

    // add tt:CellMotionEngine AnalyticsModule
    onvif_init_CellMotionEngine(p_item);

    // add tt:MotionRegionDetector AnalyticsModule
    onvif_init_MotionRegionDetector(p_item);
}

void onvif_init_VideoAnalyticsConfiguration()
{
    // todo : here init video analytics configurations ...
    
    ConfigList * p_config;
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    VideoAnalyticsConfigurationList * p_va_cfg;
    
    if (g_onvif_cfg.va_cfg)
    {
        return;
    }
    
    p_va_cfg = onvif_add_VideoAnalyticsConfiguration(&g_onvif_cfg.va_cfg);
    if (NULL == p_va_cfg)
    {
        return;
    }
    
    // todo : here init analytics engine configuration ...
    
    p_config = onvif_add_Config(&p_va_cfg->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
    if (p_config)
    {
        strcpy(p_config->Config.Name, "MyCellMotionEngine");
        strcpy(p_config->Config.Type, "tt:CellMotionEngine");

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Sensitivity");
            strcpy(p_simpleitem->SimpleItem.Value, "6");
        }

        p_elementitem = onvif_add_ElementItem(&p_config->Config.Parameters.ElementItem);
        if (p_elementitem)
        {
            strcpy(p_elementitem->ElementItem.Name, "Layout");
            
            p_elementitem->ElementItem.Any = (char *)malloc(1024);
            if (p_elementitem->ElementItem.Any)
            {
                p_elementitem->ElementItem.AnyFlag = 1;
                strcpy(p_elementitem->ElementItem.Any, 
                    "<tt:CellLayout Columns=\"22\" Rows=\"18\">\r\n"
                        "<tt:Transformation>\r\n"
                            "<tt:Translate x=\"-1.000000\" y=\"-1.000000\"/>\r\n"
                            "<tt:Scale x=\"0.090909\" y=\"0.111111\"/>\r\n"
                        "</tt:Transformation>\r\n"
                    "</tt:CellLayout>\r\n");
            }
        }
    }

    p_config = onvif_add_Config(&p_va_cfg->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
    if (p_config)
    {
        strcpy(p_config->Config.Name, "MyMotionRegionDetector");
        strcpy(p_config->Config.Type, "tt:MotionRegionDetector");

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Sensitivity");
            strcpy(p_simpleitem->SimpleItem.Value, "6");
        }
    }

    // todo : here init rule engine configuration ...
    
    p_config = onvif_add_Config(&p_va_cfg->Configuration.RuleEngineConfiguration.Rule);
    if (p_config)
    {
        strcpy(p_config->Config.Name, "MyCellMotionDetector");
        strcpy(p_config->Config.Type, "tt:CellMotionDetector");

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "MinCount");
            strcpy(p_simpleitem->SimpleItem.Value, "5");
        }

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "AlarmOnDelay");
            strcpy(p_simpleitem->SimpleItem.Value, "1000");
        }

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "AlarmOffDelay");
            strcpy(p_simpleitem->SimpleItem.Value, "1000");
        }

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "ActiveCells");
            strcpy(p_simpleitem->SimpleItem.Value, "zwA=");
        }
    }

    p_config = onvif_add_Config(&p_va_cfg->Configuration.RuleEngineConfiguration.Rule);
    if (p_config)
    {
        strcpy(p_config->Config.Name, "TamperingDetection");
        strcpy(p_config->Config.Type, "tt:TamperingDetection");

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Mode");
            strcpy(p_simpleitem->SimpleItem.Value, "SignalLoss");
        }

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Threshold");
            strcpy(p_simpleitem->SimpleItem.Value, "50.0");
        }

        p_simpleitem = onvif_add_SimpleItem(&p_config->Config.Parameters.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Duration");
            strcpy(p_simpleitem->SimpleItem.Value, "PT10S");
        }
    }
}

#endif    // end of VIDEO_ANALYTICS

#ifdef PROFILE_G_SUPPORT

HT_API void onvif_get_Recording_token(RecordingList * p_head, char * token, int size)
{
    RecordingList * p_tmp = NULL;

    do {
        snprintf(token, size, "RecordingToken_%u", ++g_onvif_idx.recording_idx);

        p_tmp = onvif_find_Recording(p_head, token);
    } while (p_tmp);
}

HT_API RecordingList * onvif_add_Recording(RecordingList ** p_head)
{
    RecordingList * p_tmp;
    RecordingList * p_new = (RecordingList *) malloc(sizeof(RecordingList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RecordingList));

    onvif_get_Recording_token(*p_head, p_new->Recording.RecordingToken, sizeof(p_new->Recording.RecordingToken));    

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API RecordingList * onvif_find_Recording(RecordingList * p_head, const char * token)
{
    RecordingList * p_tmp = p_head;

    if (NULL == token || token[0] == '\0')
    {
        return p_tmp;
    }
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->Recording.RecordingToken, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_Recording(RecordingList ** p_head, RecordingList * p_node)
{
    RecordingList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    onvif_free_Tracks(&p_node->Recording.Tracks);

    free(p_node);
}

HT_API void onvif_free_Recordings(RecordingList ** p_head)
{
    RecordingList * p_next;
    RecordingList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_Tracks(&p_tmp->Recording.Tracks);

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_Track_token(TrackList * p_head, char * token, int size)
{
    TrackList * p_tmp = NULL;

    do {
        snprintf(token, size, "TrackToken_%u", ++g_onvif_idx.track_idx);

        p_tmp = onvif_find_Track(p_head, token);
    } while (p_tmp);
}

HT_API TrackList * onvif_add_Track(TrackList ** p_head)
{
    TrackList * p_tmp;
    TrackList * p_new = (TrackList *) malloc(sizeof(TrackList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(TrackList));
    
    onvif_get_Track_token(*p_head, p_new->Track.TrackToken, sizeof(p_new->Track.TrackToken));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_Track(TrackList ** p_head, TrackList * p_track)
{
    TrackList * p_prev;
    
    p_prev = *p_head;
    if (p_track == p_prev)
    {
        *p_head = p_track->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_track)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_track->next;
    }

    free(p_track);
}

HT_API void onvif_free_Tracks(TrackList ** p_head)
{
    TrackList * p_next;
    TrackList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API TrackList * onvif_find_Track(TrackList * p_head, const char * token)
{
    TrackList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->Track.TrackToken, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API int    onvif_get_track_nums_by_type(TrackList * p_head, onvif_TrackType type)
{
    int nums = 0;
    
    TrackList * p_tmp = p_head;
    while (p_tmp)
    {
        if (p_tmp->Track.Configuration.TrackType == type)
        {
            nums++;
        }

        p_tmp = p_tmp->next;
    }

    return nums;
}

HT_API void onvif_get_RecordingJob_token(RecordingJobList * p_head, char * token, int size)
{
    RecordingJobList * p_tmp = NULL;

    do {
        snprintf(token, size, "RecordingJobToken_%u", ++g_onvif_idx.recordingjob_idx);

        p_tmp = onvif_find_RecordingJob(p_head, token);
    } while (p_tmp);
}

HT_API RecordingJobList * onvif_add_RecordingJob(RecordingJobList ** p_head)
{
    RecordingJobList * p_tmp;
    RecordingJobList * p_new = (RecordingJobList *) malloc(sizeof(RecordingJobList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RecordingJobList));

    onvif_get_RecordingJob_token(*p_head, p_new->RecordingJob.JobToken, sizeof(p_new->RecordingJob.JobToken));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API RecordingJobList * onvif_find_RecordingJob(RecordingJobList * p_head, const char * token)
{
    RecordingJobList * p_tmp = p_head;
    while (p_tmp)
    {
        if (strcmp(p_tmp->RecordingJob.JobToken, token) == 0)
        {
            break;
        }

        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_RecordingJob(RecordingJobList ** p_head, RecordingJobList * p_node)
{
    RecordingJobList * p_prev;
    
    if (NULL == p_node)
    {
        return;
    }
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    free(p_node);
}

HT_API void onvif_free_RecordingJobs(RecordingJobList ** p_head)
{
    RecordingJobList * p_next;
    RecordingJobList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API RecordingInformationList * onvif_add_RecordingInformation(RecordingInformationList ** p_head)
{
    RecordingInformationList * p_tmp;
    RecordingInformationList * p_new = (RecordingInformationList *) malloc(sizeof(RecordingInformationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RecordingInformationList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API void onvif_free_RecordingInformations(RecordingInformationList ** p_head)
{
    RecordingInformationList * p_next;
    RecordingInformationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API FindEventResultList * onvif_add_FindEventResult(FindEventResultList ** p_head)
{
    FindEventResultList * p_tmp;
    FindEventResultList * p_new = (FindEventResultList *) malloc(sizeof(FindEventResultList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(FindEventResultList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_FindEventResult(FindEventResultList ** p_head, FindEventResultList * p_node)
{
    FindEventResultList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    onvif_free_SimpleItems(&p_node->Result.Event.Message.Source.SimpleItem);
    onvif_free_SimpleItems(&p_node->Result.Event.Message.Key.SimpleItem);
    onvif_free_SimpleItems(&p_node->Result.Event.Message.Data.SimpleItem);

    onvif_free_ElementItems(&p_node->Result.Event.Message.Source.ElementItem);
    onvif_free_ElementItems(&p_node->Result.Event.Message.Key.ElementItem);
    onvif_free_ElementItems(&p_node->Result.Event.Message.Data.ElementItem);
        
    free(p_node);
}

HT_API void onvif_free_FindEventResults(FindEventResultList ** p_head)
{
    FindEventResultList * p_next;
    FindEventResultList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_SimpleItems(&p_tmp->Result.Event.Message.Source.SimpleItem);
        onvif_free_SimpleItems(&p_tmp->Result.Event.Message.Key.SimpleItem);
        onvif_free_SimpleItems(&p_tmp->Result.Event.Message.Data.SimpleItem);

        onvif_free_ElementItems(&p_tmp->Result.Event.Message.Source.ElementItem);
        onvif_free_ElementItems(&p_tmp->Result.Event.Message.Key.ElementItem);
        onvif_free_ElementItems(&p_tmp->Result.Event.Message.Data.ElementItem);
            
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API FindMetadataResultList * onvif_add_FindMetadataResult(FindMetadataResultList ** p_head)
{
    FindMetadataResultList * p_tmp;
    FindMetadataResultList * p_new = (FindMetadataResultList *) malloc(sizeof(FindMetadataResultList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(FindMetadataResultList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_FindMetadataResult(FindMetadataResultList ** p_head, FindMetadataResultList * p_node)
{
    FindMetadataResultList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    free(p_node);
}

HT_API void onvif_free_FindMetadataResults(FindMetadataResultList ** p_head)
{
    FindMetadataResultList * p_next;
    FindMetadataResultList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API FindPTZPositionResultList * onvif_add_FindPTZPositionResult(FindPTZPositionResultList ** p_head)
{
    FindPTZPositionResultList * p_tmp;
    FindPTZPositionResultList * p_new = (FindPTZPositionResultList *) malloc(sizeof(FindPTZPositionResultList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(FindPTZPositionResultList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_FindPTZPositionResult(FindPTZPositionResultList ** p_head, FindPTZPositionResultList * p_node)
{
    FindPTZPositionResultList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }
    
    free(p_node);
}

HT_API void onvif_free_FindPTZPositionResults(FindPTZPositionResultList ** p_head)
{
    FindPTZPositionResultList * p_next;
    FindPTZPositionResultList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_Recording()
{
    RecordingList * p_recording;
    
    if (g_onvif_cfg.recordings)
    {
        return;
    }
    
    p_recording = onvif_add_Recording(&g_onvif_cfg.recordings);
    if (p_recording)
    {
        TrackList * p_track;
        
        strcpy(p_recording->Recording.Configuration.Source.SourceId, "http://localhost/sourceID");
        strcpy(p_recording->Recording.Configuration.Source.Name, "CameraName");
        strcpy(p_recording->Recording.Configuration.Source.Location, "LocationDescription");
        strcpy(p_recording->Recording.Configuration.Source.Description, "SourceDescription");
        strcpy(p_recording->Recording.Configuration.Source.Address, "http://www.onvif.org/ver10/schema/Profile");

        strcpy(p_recording->Recording.Configuration.Content, "Recording from device");
        p_recording->Recording.Configuration.MaximumRetentionTimeFlag = 1;
        p_recording->Recording.Configuration.MaximumRetentionTime = 0;

        p_recording->EarliestRecording = time(NULL) - 3600;
        p_recording->LatestRecording = time(NULL);
        
        p_track = onvif_add_Track(&p_recording->Recording.Tracks);
        if (p_track)
        {
            strcpy(p_track->Track.TrackToken, "VIDEO001");
            p_track->Track.Configuration.TrackType = TrackType_Video;

            p_track->EarliestRecording = p_recording->EarliestRecording;
            p_track->LatestRecording = p_recording->LatestRecording;
        }    
        
        p_track = onvif_add_Track(&p_recording->Recording.Tracks);
        if (p_track)
        {
            strcpy(p_track->Track.TrackToken, "AUDIO001");
            p_track->Track.Configuration.TrackType = TrackType_Audio;

            p_track->EarliestRecording = p_recording->EarliestRecording;
            p_track->LatestRecording = p_recording->LatestRecording;
        }
        
        p_track = onvif_add_Track(&p_recording->Recording.Tracks);
        if (p_track)
        {
            strcpy(p_track->Track.TrackToken, "META001");
            p_track->Track.Configuration.TrackType = TrackType_Metadata;

            p_track->EarliestRecording = p_recording->EarliestRecording;
            p_track->LatestRecording = p_recording->LatestRecording;
        }
    }
}

void onvif_init_RecordingJob()
{
    RecordingJobList * p_recordingjob;
    
    if (g_onvif_cfg.recording_jobs)
    {
        return;
    }
    
    p_recordingjob = onvif_add_RecordingJob(&g_onvif_cfg.recording_jobs);
    if (p_recordingjob)
    {
        strcpy(p_recordingjob->RecordingJob.JobConfiguration.Mode, "Active");
        p_recordingjob->RecordingJob.JobConfiguration.Priority = 1;

        if (g_onvif_cfg.recordings)
        {
            strcpy(p_recordingjob->RecordingJob.JobConfiguration.RecordingToken, g_onvif_cfg.recordings->Recording.RecordingToken);
        }
        
        if (g_onvif_cfg.profiles)
        {
            p_recordingjob->RecordingJob.JobConfiguration.sizeSource = 1;

            p_recordingjob->RecordingJob.JobConfiguration.Source[0].SourceTokenFlag = 1;
            
            p_recordingjob->RecordingJob.JobConfiguration.Source[0].SourceToken.TypeFlag = 1;
            strcpy(p_recordingjob->RecordingJob.JobConfiguration.Source[0].SourceToken.Type, "http://www.onvif.org/ver10/schema/Profile");
            strcpy(p_recordingjob->RecordingJob.JobConfiguration.Source[0].SourceToken.Token, g_onvif_cfg.profiles->token);

            p_recordingjob->RecordingJob.JobConfiguration.Source[0].sizeTracks = 1;
            strcpy(p_recordingjob->RecordingJob.JobConfiguration.Source[0].Tracks[0].SourceTag, "SourceTag");
            strcpy(p_recordingjob->RecordingJob.JobConfiguration.Source[0].Tracks[0].Destination, "VIDEO001");
        }
    }
}

#endif    // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

HT_API void onvif_get_AccessPoint_token(AccessPointList * p_head, char * token, int size)
{
    AccessPointList * p_tmp = NULL;

    do {
        snprintf(token, size, "AccessPointToken_%u", ++g_onvif_idx.aceess_point_idx);

        p_tmp = onvif_find_AccessPoint(p_head, token);
    } while (p_tmp);
}

HT_API AccessPointList * onvif_add_AccessPoint(AccessPointList ** p_head)
{
    AccessPointList * p_tmp;
    AccessPointList * p_new = (AccessPointList *) malloc(sizeof(AccessPointList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AccessPointList));

    onvif_get_AccessPoint_token(*p_head, p_new->AccessPointInfo.token, sizeof(p_new->AccessPointInfo.token));

    snprintf(p_new->AccessPointInfo.Name, sizeof(p_new->AccessPointInfo.Name), 
        "AccessPointName_%u", g_onvif_idx.aceess_point_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API AccessPointList * onvif_find_AccessPoint(AccessPointList * p_head, const char * token)
{
    AccessPointList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->AccessPointInfo.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_AccessPoint(AccessPointList ** p_head, AccessPointList * p_node)
{
    AccessPointList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    free(p_node);
}

HT_API void onvif_free_AccessPoints(AccessPointList ** p_head)
{
    AccessPointList * p_next;
    AccessPointList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API DoorInfoList * onvif_add_DoorInfo(DoorInfoList ** p_head)
{
    DoorInfoList * p_tmp;
    DoorInfoList * p_new = (DoorInfoList *) malloc(sizeof(DoorInfoList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(DoorInfoList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API DoorInfoList * onvif_find_DoorInfo(DoorInfoList * p_head, const char * token)
{
    DoorInfoList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcasecmp(token, p_tmp->DoorInfo.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_DoorInfos(DoorInfoList ** p_head)
{
    DoorInfoList * p_next;
    DoorInfoList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_Door_token(DoorList * p_head, char * token, int size)
{
    DoorList * p_tmp = NULL;

    do {
        snprintf(token, size, "DoorToken_%u", ++g_onvif_idx.door_idx);

        p_tmp = onvif_find_Door(p_head, token);
    } while (p_tmp);
}

HT_API DoorList * onvif_add_Door(DoorList ** p_head)
{
    DoorList * p_tmp;
    DoorList * p_new = (DoorList *) malloc(sizeof(DoorList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(DoorList));

    onvif_get_Door_token(*p_head, p_new->Door.DoorInfo.token, sizeof(p_new->Door.DoorInfo.token));
    
    snprintf(p_new->Door.DoorInfo.Name, sizeof(p_new->Door.DoorInfo.Name), "DoorName_%u", g_onvif_idx.door_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API DoorList * onvif_find_Door(DoorList * p_head, const char * token)
{
    DoorList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Door.DoorInfo.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Door(DoorList ** p_head, DoorList * p_node)
{
    DoorList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    free(p_node);
}

HT_API void onvif_free_Doors(DoorList ** p_head)
{
    DoorList * p_next;
    DoorList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_Area_token(AreaList * p_head, char * token, int size)
{
    AreaList * p_tmp = NULL;

    do {
        snprintf(token, size, "AreaToken_%u", ++g_onvif_idx.area_idx);

        p_tmp = onvif_find_Area(p_head, token);
    } while (p_tmp);
}

HT_API AreaList * onvif_add_Area(AreaList ** p_head)
{
    AreaList * p_tmp;
    AreaList * p_new = (AreaList *) malloc(sizeof(AreaList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AreaList));

    onvif_get_Area_token(*p_head, p_new->AreaInfo.token, sizeof(p_new->AreaInfo.token));
    
    snprintf(p_new->AreaInfo.Name, sizeof(p_new->AreaInfo.Name), "AreaName_%u", g_onvif_idx.area_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API AreaList * onvif_find_Area(AreaList * p_head, const char * token)
{
    AreaList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->AreaInfo.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Area(AreaList ** p_head, AreaList * p_node)
{
    AreaList * p_prev;
    
    p_prev = *p_head;
    if (p_node == p_prev)
    {
        *p_head = p_node->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_node)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_node->next;
    }

    free(p_node);
}

HT_API void onvif_free_Areas(AreaList ** p_head)
{
    AreaList * p_next;
    AreaList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_AccessPoint(AccessPointList * p_accesspoint, DoorList * p_door, AreaList * p_area)
{
    p_accesspoint->Enabled = TRUE;
    p_accesspoint->AccessPointInfo.DescriptionFlag = 1;
    snprintf(p_accesspoint->AccessPointInfo.Description, sizeof(p_accesspoint->AccessPointInfo.Description), 
        "Access point %d", g_onvif_idx.aceess_point_idx);

    if (p_area)
    {
        p_accesspoint->AccessPointInfo.AreaFromFlag = 1;
        strcpy(p_accesspoint->AccessPointInfo.AreaFrom, p_area->AreaInfo.token);

        p_area = p_area->next;
    }

    if (p_area)
    {
        p_accesspoint->AccessPointInfo.AreaToFlag = 1;
        strcpy(p_accesspoint->AccessPointInfo.AreaTo, p_area->AreaInfo.token);
    }

    if (p_door)
    {
        strcpy(p_accesspoint->AccessPointInfo.Entity, p_door->Door.DoorInfo.token);

        p_accesspoint->AccessPointInfo.EntityTypeFlag = 1;
        strcpy(p_accesspoint->AccessPointInfo.EntityType, "tdc:Door");
    }

    p_accesspoint->AccessPointInfo.Capabilities.DisableAccessPoint = TRUE;
    p_accesspoint->AccessPointInfo.Capabilities.Duress = TRUE;
    p_accesspoint->AccessPointInfo.Capabilities.AnonymousAccess = TRUE;
    p_accesspoint->AccessPointInfo.Capabilities.AccessTaken = TRUE;
    p_accesspoint->AccessPointInfo.Capabilities.ExternalAuthorization = FALSE;
}

void onvif_init_AccessPointList()
{
    // here, init two access point for two door ...

    DoorList * p_door = g_onvif_cfg.doors;
    AreaList * p_area = g_onvif_cfg.areas;
    AccessPointList * p_accesspoint;
    
    if (g_onvif_cfg.access_points)
    {
        return;
    }
    
    p_accesspoint = onvif_add_AccessPoint(&g_onvif_cfg.access_points);
    if (p_accesspoint)
    {
        onvif_init_AccessPoint(p_accesspoint, p_door, p_area);

        if (p_door)
        {
            p_door = p_door->next;
        }

        if (p_area)
        {
            p_area = p_area->next;
        }

        if (p_area)
        {
            p_area = p_area->next;
        }
    }

    p_accesspoint = onvif_add_AccessPoint(&g_onvif_cfg.access_points);
    if (p_accesspoint)
    {
        onvif_init_AccessPoint(p_accesspoint, p_door, p_area);
    }
}

void onvif_init_Door(DoorList * p_door)
{
    strcpy(p_door->Door.DoorType, "pt:Door");
    
    p_door->Door.DoorInfo.DescriptionFlag = 1;
    snprintf(p_door->Door.DoorInfo.Description, sizeof(p_door->Door.DoorInfo.Description), 
        "Door %d", g_onvif_idx.door_idx);

    p_door->Door.DoorInfo.Capabilities.Access = TRUE;
    p_door->Door.DoorInfo.Capabilities.AccessTimingOverride = TRUE;
    p_door->Door.DoorInfo.Capabilities.Lock = TRUE;
    p_door->Door.DoorInfo.Capabilities.Unlock = TRUE;
    p_door->Door.DoorInfo.Capabilities.Block = TRUE;
    p_door->Door.DoorInfo.Capabilities.DoubleLock = TRUE;
    p_door->Door.DoorInfo.Capabilities.LockDown = TRUE;
    p_door->Door.DoorInfo.Capabilities.LockOpen = TRUE;
    p_door->Door.DoorInfo.Capabilities.DoorMonitor = TRUE;
    p_door->Door.DoorInfo.Capabilities.LockMonitor = TRUE;
    p_door->Door.DoorInfo.Capabilities.DoubleLockMonitor = TRUE;
    p_door->Door.DoorInfo.Capabilities.Alarm = TRUE;
    p_door->Door.DoorInfo.Capabilities.Tamper = TRUE;
    p_door->Door.DoorInfo.Capabilities.Fault = TRUE;

    p_door->DoorState.DoorPhysicalStateFlag = 1;
    p_door->DoorState.DoorPhysicalState = DoorPhysicalState_Closed;
    p_door->DoorState.LockPhysicalStateFlag = 1;
    p_door->DoorState.LockPhysicalState = LockPhysicalState_Locked;
    p_door->DoorState.DoubleLockPhysicalStateFlag = 1;
    p_door->DoorState.DoubleLockPhysicalState = LockPhysicalState_Locked;
    p_door->DoorState.AlarmFlag = 1;
    p_door->DoorState.Alarm = DoorAlarmState_Normal;
    p_door->DoorState.FaultFlag = 1;
    p_door->DoorState.Fault.State = DoorFaultState_NotInFault;
    p_door->DoorState.DoorMode = DoorMode_Locked;
    p_door->DoorState.TamperFlag = 1;
    p_door->DoorState.Tamper.State = DoorTamperState_NotInTamper;
}

void onvif_init_DoorList()
{
    DoorList * p_door;
    
    if (g_onvif_cfg.doors)
    {
        return;
    }
    
    // here, init two door ...
    
    p_door = onvif_add_Door(&g_onvif_cfg.doors);
    if (p_door)
    {
        onvif_init_Door(p_door);
    }

    p_door = onvif_add_Door(&g_onvif_cfg.doors);
    if (p_door)
    {
        onvif_init_Door(p_door);
    }
}

void onvif_init_AreaList()
{
    AreaList * p_info;
    
    if (g_onvif_cfg.areas)
    {
        return;
    }
    
    // here, init four area for two door ...
    
    p_info = onvif_add_Area(&g_onvif_cfg.areas);
    if (p_info)
    {
        p_info->AreaInfo.DescriptionFlag = 1;
        snprintf(p_info->AreaInfo.Description, sizeof(p_info->AreaInfo.Description), "Area %d", g_onvif_idx.area_idx);
    }

    p_info = onvif_add_Area(&g_onvif_cfg.areas);
    if (p_info)
    {
        p_info->AreaInfo.DescriptionFlag = 1;
        snprintf(p_info->AreaInfo.Description, sizeof(p_info->AreaInfo.Description), "Area %d", g_onvif_idx.area_idx);
    }
    
    p_info = onvif_add_Area(&g_onvif_cfg.areas);
    if (p_info)
    {
        p_info->AreaInfo.DescriptionFlag = 1;
        snprintf(p_info->AreaInfo.Description, sizeof(p_info->AreaInfo.Description), "Area %d", g_onvif_idx.area_idx);
    }
    
    p_info = onvif_add_Area(&g_onvif_cfg.areas);
    if (p_info)
    {
        p_info->AreaInfo.DescriptionFlag = 1;
        snprintf(p_info->AreaInfo.Description, sizeof(p_info->AreaInfo.Description), "Area %d", g_onvif_idx.area_idx);
    }
}

#endif // end of PROFILE_C_SUPPORT

#ifdef DEVICEIO_SUPPORT

HT_API PaneLayoutList * onvif_add_PaneLayout(PaneLayoutList ** p_head)
{
    PaneLayoutList * p_tmp;
    PaneLayoutList * p_new = (PaneLayoutList *) malloc(sizeof(PaneLayoutList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PaneLayoutList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API PaneLayoutList * onvif_find_PaneLayout(PaneLayoutList * p_head, const char * token)
{
    PaneLayoutList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->PaneLayout.Pane) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_PaneLayouts(PaneLayoutList ** p_head)
{
    PaneLayoutList * p_next;
    PaneLayoutList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoOutput_token(VideoOutputList * p_head, char * token, int size)
{
    VideoOutputList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoOutputToken_%u", ++g_onvif_idx.v_out_idx);

        p_tmp = onvif_find_VideoOutput(p_head, token);
    } while (p_tmp);
}

HT_API VideoOutputList * onvif_add_VideoOutput(VideoOutputList ** p_head)
{
    VideoOutputList * p_tmp;
    VideoOutputList * p_new = (VideoOutputList *) malloc(sizeof(VideoOutputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoOutputList));

    onvif_get_VideoOutput_token(*p_head, p_new->VideoOutput.token, sizeof(p_new->VideoOutput.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API VideoOutputList * onvif_find_VideoOutput(VideoOutputList * p_head, const char * token)
{
    VideoOutputList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->VideoOutput.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_VideoOutputs(VideoOutputList ** p_head)
{
    VideoOutputList * p_next;
    VideoOutputList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_PaneLayouts(&p_tmp->VideoOutput.Layout.PaneLayout);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_VideoOutputConfiguration_token(VideoOutputConfigurationList * p_head, char * token, int size)
{
    VideoOutputConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "VideoOutputConfigurationToken_%u", ++g_onvif_idx.v_out_cfg_idx);

        p_tmp = onvif_find_VideoOutputConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API VideoOutputConfigurationList * onvif_add_VideoOutputConfiguration(VideoOutputConfigurationList ** p_head)
{
    VideoOutputConfigurationList * p_tmp;
    VideoOutputConfigurationList * p_new = (VideoOutputConfigurationList *) malloc(sizeof(VideoOutputConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoOutputConfigurationList));

    onvif_get_VideoOutputConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));

    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "VideoOutputConfigurationName_%u", g_onvif_idx.v_out_cfg_idx);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API VideoOutputConfigurationList * onvif_find_VideoOutputConfiguration(VideoOutputConfigurationList * p_head, const char * token)
{
    VideoOutputConfigurationList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Configuration.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API VideoOutputConfigurationList * onvif_find_VideoOutputConfiguration_by_OutputToken(VideoOutputConfigurationList * p_head, const char * token)
{
    VideoOutputConfigurationList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Configuration.OutputToken) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_VideoOutputConfigurations(VideoOutputConfigurationList ** p_head)
{
    VideoOutputConfigurationList * p_next;
    VideoOutputConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_AudioOutput_token(AudioOutputList * p_head, char * token, int size)
{
    AudioOutputList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioOutputToken_%u", ++g_onvif_idx.a_out_idx);

        p_tmp = onvif_find_AudioOutput(p_head, token);
    } while (p_tmp);
}

HT_API AudioOutputList * onvif_add_AudioOutput(AudioOutputList ** p_head)
{
    AudioOutputList * p_tmp;
    AudioOutputList * p_new = (AudioOutputList *) malloc(sizeof(AudioOutputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioOutputList));

    onvif_get_AudioOutput_token(*p_head, p_new->AudioOutput.token, sizeof(p_new->AudioOutput.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API AudioOutputList * onvif_find_AudioOutput(AudioOutputList * p_head, const char * token)
{
    AudioOutputList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->AudioOutput.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_AudioOutputs(AudioOutputList ** p_head)
{
    AudioOutputList * p_next;
    AudioOutputList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_AudioOutputConfiguration_token(AudioOutputConfigurationList * p_head, char * token, int size)
{
    AudioOutputConfigurationList * p_tmp = NULL;

    do {
        snprintf(token, size, "AudioOutputConfigurationToken_%u", ++g_onvif_idx.a_out_cfg_idx);

        p_tmp = onvif_find_AudioOutputConfiguration(p_head, token);
    } while (p_tmp);
}

HT_API AudioOutputConfigurationList * onvif_add_AudioOutputConfiguration(AudioOutputConfigurationList ** p_head)
{
    AudioOutputConfigurationList * p_tmp;
    AudioOutputConfigurationList * p_new = (AudioOutputConfigurationList *) malloc(sizeof(AudioOutputConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioOutputConfigurationList));

    onvif_get_AudioOutputConfiguration_token(*p_head, p_new->Configuration.token, sizeof(p_new->Configuration.token));

    snprintf(p_new->Configuration.Name, sizeof(p_new->Configuration.Name), 
        "AudioOutputConfigurationName_%u", g_onvif_idx.a_out_cfg_idx);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API AudioOutputConfigurationList * onvif_find_AudioOutputConfiguration(AudioOutputConfigurationList * p_head, const char * token)
{
    AudioOutputConfigurationList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Configuration.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API AudioOutputConfigurationList * onvif_find_AudioOutputConfiguration_by_OutputToken(AudioOutputConfigurationList * p_head, const char * token)
{
    AudioOutputConfigurationList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Configuration.OutputToken) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_AudioOutputConfigurations(AudioOutputConfigurationList ** p_head)
{
    AudioOutputConfigurationList * p_next;
    AudioOutputConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_RelayOutput_token(RelayOutputList * p_head, char * token, int size)
{
    RelayOutputList * p_tmp = NULL;

    do {
        snprintf(token, size, "RelayOutputToken_%u", ++g_onvif_idx.relay_idx);

        p_tmp = onvif_find_RelayOutput(p_head, token);
    } while (p_tmp);
}

HT_API RelayOutputList * onvif_add_RelayOutput(RelayOutputList ** p_head)
{
    RelayOutputList * p_tmp;
    RelayOutputList * p_new = (RelayOutputList *) malloc(sizeof(RelayOutputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RelayOutputList));

    onvif_get_RelayOutput_token(*p_head, p_new->RelayOutput.token, sizeof(p_new->RelayOutput.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API RelayOutputList * onvif_find_RelayOutput(RelayOutputList * p_head, const char * token)
{
    RelayOutputList * p_tmp = p_head;

    if (NULL == token)
    {
        return NULL;
    }
    
    while (p_tmp)
    {
        if (strcmp(token, p_tmp->RelayOutput.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_RelayOutputs(RelayOutputList ** p_head)
{
    RelayOutputList * p_next;
    RelayOutputList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_DigitalInput_token(DigitalInputList * p_head, char * token, int size)
{
    DigitalInputList * p_tmp = NULL;

    do {
        snprintf(token, size, "DigitalInputToken_%u", ++g_onvif_idx.digit_input_idx);

        p_tmp = onvif_find_DigitalInput(p_head, token);
    } while (p_tmp);
}

HT_API DigitalInputList * onvif_add_DigitalInput(DigitalInputList ** p_head)
{
    DigitalInputList * p_tmp;
    DigitalInputList * p_new = (DigitalInputList *) malloc(sizeof(DigitalInputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(DigitalInputList));

    onvif_get_DigitalInput_token(*p_head, p_new->DigitalInput.token, sizeof(p_new->DigitalInput.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API DigitalInputList * onvif_find_DigitalInput(DigitalInputList * p_head, const char * token)
{
    DigitalInputList * p_tmp = p_head;

    if (NULL == token)
    {
        return NULL;
    }
    
    while (p_tmp)
    {
        if (strcmp(token, p_tmp->DigitalInput.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_DigitalInputs(DigitalInputList ** p_head)
{
    DigitalInputList * p_next;
    DigitalInputList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_SerialPort_token(SerialPortList * p_head, char * token, int size)
{
    SerialPortList * p_tmp = NULL;

    do {
        snprintf(token, size, "SerialPortToken_%u", ++g_onvif_idx.serial_port_idx);

        p_tmp = onvif_find_SerialPort(p_head, token);
    } while (p_tmp);
}

HT_API SerialPortList * onvif_add_SerialPort(SerialPortList ** p_head)
{
    SerialPortList * p_tmp;
    SerialPortList * p_new = (SerialPortList *) malloc(sizeof(SerialPortList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(SerialPortList));

    onvif_get_SerialPort_token(*p_head, p_new->SerialPort.token, sizeof(p_new->SerialPort.token));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API SerialPortList * onvif_find_SerialPort(SerialPortList * p_head, const char * token)
{
    SerialPortList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->SerialPort.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API SerialPortList * onvif_find_SerialPort_by_ConfigurationToken(SerialPortList * p_head, const char * token)
{
    SerialPortList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Configuration.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_SerialPorts(SerialPortList ** p_head)
{
    SerialPortList * p_next;
    SerialPortList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_malloc_SerialData(onvif_SerialData * p_data, int union_SerialData, int size)
{
    if (NULL == p_data)
    {
        return;
    }
    
    if (union_SerialData == 0)
    {
        p_data->_union_SerialData = 0;
        p_data->union_SerialData.Binary = (char *)malloc(size);
        if (p_data->union_SerialData.Binary)
        {
            memset(p_data->union_SerialData.Binary, 0, size);
        }
    }
    else
    {
        p_data->_union_SerialData = 1;
        p_data->union_SerialData.String = (char *)malloc(size);
        if (p_data->union_SerialData.String)
        {
            memset(p_data->union_SerialData.String, 0, size);
        }
    }
}

HT_API void onvif_free_SerialData(onvif_SerialData * p_data)
{
    if (NULL == p_data)
    {
        return;
    }

    if (p_data->_union_SerialData == 0)
    {
        if (p_data->union_SerialData.Binary)
        {
            free(p_data->union_SerialData.Binary);
            p_data->union_SerialData.Binary = NULL;
        }
    }
    else
    {
        if (p_data->union_SerialData.String)
        {
            free(p_data->union_SerialData.String);
            p_data->union_SerialData.String = NULL;
        }
    }
}

#ifdef AUDIO_SUPPORT
void onvif_init_AudioOutput()
{
    if (g_onvif_cfg.a_output)
    {
        return;
    }
    
    onvif_add_AudioOutput(&g_onvif_cfg.a_output);
}

void onvif_init_AudioOutputConfiguration(AudioOutputList * p_output)
{
    AudioOutputConfigurationList * p_cfg;
    
    if (g_onvif_cfg.a_output_cfg || !p_output)
    {
        return;
    }
    
    p_cfg = onvif_add_AudioOutputConfiguration(&g_onvif_cfg.a_output_cfg);

    strcpy(p_cfg->Configuration.OutputToken, p_output->AudioOutput.token);
    p_cfg->Configuration.SendPrimacyFlag = 1;
    strcpy(p_cfg->Configuration.SendPrimacy, "www.onvif.org/ver20/HalfDuplex/Server");
    p_cfg->Configuration.OutputLevel = 100;

    p_cfg->Options.sizeOutputTokensAvailable = 1;
    strcpy(p_cfg->Options.OutputTokensAvailable[0], p_output->AudioOutput.token);
    p_cfg->Options.sizeSendPrimacyOptions = 3;
    strcpy(p_cfg->Options.SendPrimacyOptions[0], "www.onvif.org/ver20/HalfDuplex/Server");
    strcpy(p_cfg->Options.SendPrimacyOptions[1], "www.onvif.org/ver20/HalfDuplex/Client");
    strcpy(p_cfg->Options.SendPrimacyOptions[2], "www.onvif.org/ver20/HalfDuplex/Auto");

    p_cfg->Options.OutputLevelRange.Min = 0;
    p_cfg->Options.OutputLevelRange.Max = 100;
}
#endif

void onvif_init_RelayOutput()
{
    RelayOutputList * p_output;
    
    if (g_onvif_cfg.relay_output)
    {
        return;
    }
    
    p_output = onvif_add_RelayOutput(&g_onvif_cfg.relay_output);

    p_output->RelayOutput.Properties.Mode = RelayMode_Monostable;
    p_output->RelayOutput.Properties.DelayTime = 10;
    p_output->RelayOutput.Properties.IdleState = RelayIdleState_closed;

    p_output->RelayLogicalState = RelayLogicalState_inactive;

    strcpy(p_output->Options.token, p_output->RelayOutput.token);
    
    p_output->Options.RelayMode_BistableFlag = 1;
    p_output->Options.RelayMode_MonostableFlag = 1;

    p_output->Options.DelayTimesFlag = 1;
    strcpy(p_output->Options.DelayTimes, "1 120");
}

void onvif_init_DigitInput()
{
    DigitalInputList * p_input;
    
    if (g_onvif_cfg.digit_input)
    {
        return;
    }
    
    p_input = onvif_add_DigitalInput(&g_onvif_cfg.digit_input);

    p_input->DigitalInput.IdleStateFlag = 1;
    p_input->DigitalInput.IdleState = DigitalIdleState_closed;

    p_input->Options.DigitalIdleState_openFlag = 1;
    p_input->Options.DigitalIdleState_closedFlag = 1;
}

void onvif_init_SerialPort()
{
    SerialPortList * p_port;

    if (g_onvif_cfg.serial_port)
    {
        return;
    }
    
    p_port = onvif_add_SerialPort(&g_onvif_cfg.serial_port);

    p_port->Configuration.BaudRate = 115200;
    p_port->Configuration.CharacterLength = 8;
    p_port->Configuration.StopBit = 1;
    p_port->Configuration.ParityBit = ParityBit_Odd;
    p_port->Configuration.type = SerialPortType_RS485FullDuplex;

    strcpy(p_port->Options.token, p_port->SerialPort.token);
    
    p_port->Options.BaudRateList.sizeItems = 2;
    p_port->Options.BaudRateList.Items[0] = 115200;
    p_port->Options.BaudRateList.Items[1] = 98000;

    p_port->Options.CharacterLengthList.sizeItems = 1;
    p_port->Options.CharacterLengthList.Items[0] = 8;

    p_port->Options.ParityBitList.sizeItems = 2;
    p_port->Options.ParityBitList.Items[0] = ParityBit_Odd;
    p_port->Options.ParityBitList.Items[1] = ParityBit_Even;

    p_port->Options.StopBitList.sizeItems = 1;
    p_port->Options.StopBitList.Items[0] = 1;    
}

#endif // end of DEVICEIO_SUPPORT

#ifdef THERMAL_SUPPORT

HT_API void onvif_get_ColorPalette_token(ColorPaletteList * p_head, char * token, int size)
{
    ColorPaletteList * p_tmp = NULL;

    do {
        snprintf(token, size, "ColorPaletteToken_%u", ++g_onvif_idx.color_palette_idx);

        p_tmp = onvif_find_ColorPalette(p_head, token);
    } while (p_tmp);
}

HT_API ColorPaletteList * onvif_add_ColorPalette(ColorPaletteList ** p_head)
{
    ColorPaletteList * p_tmp;
    ColorPaletteList * p_new = (ColorPaletteList *) malloc(sizeof(ColorPaletteList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ColorPaletteList));

    onvif_get_ColorPalette_token(*p_head, p_new->ColorPalette.token, sizeof(p_new->ColorPalette.token));

    snprintf(p_new->ColorPalette.Name, sizeof(p_new->ColorPalette.Name), 
        "ColorPaletteName_%u", g_onvif_idx.color_palette_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API ColorPaletteList * onvif_find_ColorPalette(ColorPaletteList * p_head, const char * token)
{
    ColorPaletteList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->ColorPalette.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_ColorPalettes(ColorPaletteList ** p_head)
{
    ColorPaletteList * p_next;
    ColorPaletteList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_NUCTable_token(NUCTableList * p_head, char * token, int size)
{
    NUCTableList * p_tmp = NULL;

    do {
        snprintf(token, size, "NUCTableToken_%u", ++g_onvif_idx.nuctable_idx);

        p_tmp = onvif_find_NUCTable(p_head, token);
    } while (p_tmp);
}

HT_API NUCTableList * onvif_add_NUCTable(NUCTableList ** p_head)
{
    NUCTableList * p_tmp;
    NUCTableList * p_new = (NUCTableList *) malloc(sizeof(NUCTableList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(NUCTableList));

    onvif_get_NUCTable_token(*p_head, p_new->NUCTable.token, sizeof(p_new->NUCTable.token));

    snprintf(p_new->NUCTable.Name, sizeof(p_new->NUCTable.Name), "NUCTableName_%u", g_onvif_idx.nuctable_idx);
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API NUCTableList * onvif_find_NUCTable(NUCTableList * p_head, const char * token)
{
    NUCTableList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->NUCTable.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_NUCTables(NUCTableList ** p_head)
{
    NUCTableList * p_next;
    NUCTableList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_ThermalConfiguration(onvif_ThermalConfiguration * p_req)
{
    strcpy(p_req->ColorPalette.token, "ColorPaletteToken_1");
    strcpy(p_req->ColorPalette.Name, "ColorPaletteName_1");
    strcpy(p_req->ColorPalette.Type, "WhiteHot");

    p_req->Polarity = Polarity_WhiteHot;

    p_req->NUCTableFlag = 1;
    strcpy(p_req->NUCTable.token, "NUCTableToken_1");
    strcpy(p_req->NUCTable.Name, "NUCTableName_1");
    p_req->NUCTable.LowTemperatureFlag = 1;
    p_req->NUCTable.LowTemperature = 0;
    p_req->NUCTable.HighTemperatureFlag = 1;
    p_req->NUCTable.HighTemperature = 100;

    p_req->CoolerFlag = 1;
    p_req->Cooler.Enabled = TRUE;
    p_req->Cooler.RunTimeFlag = 1;
    p_req->Cooler.RunTime = 0;
}

void onvif_init_ThermalConfigurationOptions(onvif_ThermalConfigurationOptions * p_req)
{
    ColorPaletteList * p_ColorPalette1;
    ColorPaletteList * p_ColorPalette2;
    NUCTableList * p_NUCTable1;
    NUCTableList * p_NUCTable2;
    
    p_ColorPalette1 = onvif_add_ColorPalette(&p_req->ColorPalette);
    strcpy(p_ColorPalette1->ColorPalette.Type, "WhiteHot");

    p_ColorPalette2 = onvif_add_ColorPalette(&p_req->ColorPalette);
    strcpy(p_ColorPalette2->ColorPalette.Type, "BlackHot");
        
    p_NUCTable1 = onvif_add_NUCTable(&p_req->NUCTable);
    p_NUCTable1->NUCTable.LowTemperatureFlag = 1;
    p_NUCTable1->NUCTable.LowTemperature = 0;
    p_NUCTable1->NUCTable.HighTemperatureFlag = 1;
    p_NUCTable1->NUCTable.HighTemperature = 100;

    p_NUCTable2 = onvif_add_NUCTable(&p_req->NUCTable);
    p_NUCTable2->NUCTable.LowTemperatureFlag = 1;
    p_NUCTable2->NUCTable.LowTemperature = 0;
    p_NUCTable2->NUCTable.HighTemperatureFlag = 1;
    p_NUCTable2->NUCTable.HighTemperature = 100;

    p_req->CoolerOptionsFlag = 1;
    p_req->CoolerOptions.Enabled = TRUE;
}

void onvif_init_RadiometryConfiguration(onvif_RadiometryConfiguration * p_req)
{
    p_req->RadiometryGlobalParametersFlag = 1;
    p_req->RadiometryGlobalParameters.ReflectedAmbientTemperature = 10;
    p_req->RadiometryGlobalParameters.Emissivity = 10;
    p_req->RadiometryGlobalParameters.DistanceToObject = 10;
    p_req->RadiometryGlobalParameters.RelativeHumidityFlag = 1;
    p_req->RadiometryGlobalParameters.RelativeHumidity = 10;
    p_req->RadiometryGlobalParameters.AtmosphericTemperatureFlag = 1;
    p_req->RadiometryGlobalParameters.AtmosphericTemperature = 10;
    p_req->RadiometryGlobalParameters.AtmosphericTransmittanceFlag = 1;
    p_req->RadiometryGlobalParameters.AtmosphericTransmittance = 10;
    p_req->RadiometryGlobalParameters.ExtOpticsTemperatureFlag = 1;
    p_req->RadiometryGlobalParameters.ExtOpticsTemperature = 10;
    p_req->RadiometryGlobalParameters.ExtOpticsTransmittanceFlag = 1;
    p_req->RadiometryGlobalParameters.ExtOpticsTransmittance = 10;
}

void onvif_init_RadiometryConfigurationOptions(onvif_RadiometryConfigurationOptions * p_req)
{
    p_req->RadiometryGlobalParameterOptionsFlag = 1;
    p_req->RadiometryGlobalParameterOptions.ReflectedAmbientTemperature.Min = 0;
    p_req->RadiometryGlobalParameterOptions.ReflectedAmbientTemperature.Max = 100;
    p_req->RadiometryGlobalParameterOptions.Emissivity.Min = 0;
    p_req->RadiometryGlobalParameterOptions.Emissivity.Max = 100;
    p_req->RadiometryGlobalParameterOptions.DistanceToObject.Min = 0;
    p_req->RadiometryGlobalParameterOptions.DistanceToObject.Max = 100;
    p_req->RadiometryGlobalParameterOptions.RelativeHumidityFlag = 1;
    p_req->RadiometryGlobalParameterOptions.RelativeHumidity.Min = 0;
    p_req->RadiometryGlobalParameterOptions.RelativeHumidity.Max = 100;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTemperatureFlag = 1;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTemperature.Min = 0;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTemperature.Max = 100;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTransmittanceFlag = 1;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTransmittance.Min = 0;
    p_req->RadiometryGlobalParameterOptions.AtmosphericTransmittance.Max = 100;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTemperatureFlag = 1;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTemperature.Min = 0;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTemperature.Max = 100;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTransmittanceFlag = 1;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTransmittance.Min = 0;
    p_req->RadiometryGlobalParameterOptions.ExtOpticsTransmittance.Max = 100;
}

HT_API BOOL onvif_init_Thermal(VideoSourceList * p_req)
{
    onvif_init_ThermalConfiguration(&p_req->ThermalConfiguration);
    onvif_init_ThermalConfigurationOptions(&p_req->ThermalConfigurationOptions);
    onvif_init_RadiometryConfiguration(&p_req->RadiometryConfiguration);
    onvif_init_RadiometryConfigurationOptions(&p_req->RadiometryConfigurationOptions);

    return TRUE;
}

#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

HT_API void onvif_get_Credential_token(CredentialList * p_head, char * token, int size)
{
    CredentialList * p_tmp = NULL;

    do {
        snprintf(token, size, "CredentialToken_%u", ++g_onvif_idx.credential_idx);

        p_tmp = onvif_find_Credential(p_head, token);
    } while (p_tmp);
}

HT_API CredentialList * onvif_add_Credential(CredentialList ** p_head)
{
    CredentialList * p_tmp;
    CredentialList * p_new = (CredentialList *) malloc(sizeof(CredentialList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(CredentialList));

    onvif_get_Credential_token(*p_head, p_new->Credential.token, sizeof(p_new->Credential.token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API CredentialList * onvif_find_Credential(CredentialList * p_head, const char * token)
{
    CredentialList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Credential.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Credential(CredentialList ** p_head, CredentialList * p_node)
{
    BOOL found = FALSE;
    CredentialList * p_prev = NULL;
    CredentialList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        free(p_tmp);
    }
}

HT_API void onvif_free_Credentials(CredentialList ** p_head)
{
    CredentialList * p_next;
    CredentialList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API BOOL onvif_init_Credential()
{
    CredentialList * p_tmp;
    
    if (g_onvif_cfg.credential)
    {
        return TRUE;
    }
    
    p_tmp = onvif_add_Credential(&g_onvif_cfg.credential);
    if (p_tmp)
    {
        p_tmp->Credential.DescriptionFlag = 1;
        strcpy(p_tmp->Credential.Description, "Credentia");
        strcpy(p_tmp->Credential.CredentialHolderReference, "testuser");

        p_tmp->Credential.sizeCredentialIdentifier = 1;
        p_tmp->Credential.CredentialIdentifier[0].Used = TRUE;
        p_tmp->Credential.CredentialIdentifier[0].ExemptedFromAuthentication = FALSE;
        strcpy(p_tmp->Credential.CredentialIdentifier[0].Type.Name, "pt:Card");
        strcpy(p_tmp->Credential.CredentialIdentifier[0].Type.FormatType, "GUID");
        strcpy(p_tmp->Credential.CredentialIdentifier[0].Value, "31343031303834323633000000000000");

#ifdef ACCESS_RULES
        if (g_onvif_cfg.access_rules)
        {
            p_tmp->Credential.sizeCredentialAccessProfile = 1;
            p_tmp->Credential.CredentialAccessProfile[0].Used = 1;
            strcpy(p_tmp->Credential.CredentialAccessProfile[0].AccessProfileToken, 
                g_onvif_cfg.access_rules->AccessProfile.token);
        }
#endif

        p_tmp->State.AntipassbackStateFlag = 1;
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

HT_API CredentialIdentifierItemList * onvif_add_CredentialIdentifierItem(CredentialIdentifierItemList ** p_head)
{
    CredentialIdentifierItemList * p_tmp;
    CredentialIdentifierItemList * p_new = (CredentialIdentifierItemList *) malloc(sizeof(CredentialIdentifierItemList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(CredentialIdentifierItemList));

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API void onvif_free_CredentialIdentifierItem(CredentialIdentifierItemList ** p_head, CredentialIdentifierItemList * p_node)
{
    BOOL found = FALSE;
    CredentialIdentifierItemList * p_prev = NULL;
    CredentialIdentifierItemList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (memcpy(&p_tmp->Item, &p_node->Item, sizeof(onvif_CredentialIdentifierItem)) == 0)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        free(p_tmp);
    }
}

HT_API void onvif_free_CredentialIdentifierItems(CredentialIdentifierItemList ** p_head)
{
    CredentialIdentifierItemList * p_next;
    CredentialIdentifierItemList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

HT_API void onvif_get_AccessProfile_token(AccessProfileList * p_head, char * token, int size)
{
    AccessProfileList * p_tmp = NULL;

    do {
        snprintf(token, size, "AccessProfileToken_%u", ++g_onvif_idx.accessrule_idx);

        p_tmp = onvif_find_AccessProfile(p_head, token);
    } while (p_tmp);
}

HT_API AccessProfileList * onvif_add_AccessProfile(AccessProfileList ** p_head)
{
    AccessProfileList * p_tmp;
    AccessProfileList * p_new = (AccessProfileList *) malloc(sizeof(AccessProfileList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AccessProfileList));

    onvif_get_AccessProfile_token(*p_head, p_new->AccessProfile.token, sizeof(p_new->AccessProfile.token));

    snprintf(p_new->AccessProfile.Name, sizeof(p_new->AccessProfile.Name), 
        "AccessProfileName_%u", g_onvif_idx.accessrule_idx);

    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API AccessProfileList * onvif_find_AccessProfile(AccessProfileList * p_head, const char * token)
{
    AccessProfileList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->AccessProfile.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_AccessProfile(AccessProfileList ** p_head, AccessProfileList * p_node)
{
    BOOL found = FALSE;
    AccessProfileList * p_prev = NULL;
    AccessProfileList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        free(p_tmp);
    }
}

HT_API void onvif_free_AccessProfiles(AccessProfileList ** p_head)
{
    AccessProfileList * p_next;
    AccessProfileList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API BOOL onvif_init_AccessProfile()
{
    AccessProfileList * p_tmp;
    
    if (g_onvif_cfg.access_rules)
    {
        return TRUE;
    }
    
    p_tmp = onvif_add_AccessProfile(&g_onvif_cfg.access_rules);
    if (p_tmp)
    {
        p_tmp->AccessProfile.DescriptionFlag = 1;
        snprintf(p_tmp->AccessProfile.Description, sizeof(p_tmp->AccessProfile.Description), "test");

        p_tmp->AccessProfile.sizeAccessPolicy = 1;
        strcpy(p_tmp->AccessProfile.AccessPolicy[0].ScheduleToken, "test");

#ifdef PROFILE_C_SUPPORT
        if (g_onvif_cfg.access_points)
        {
            strcpy(p_tmp->AccessProfile.AccessPolicy[0].Entity, g_onvif_cfg.access_points->AccessPointInfo.token);
        }
        else
        {
            strcpy(p_tmp->AccessProfile.AccessPolicy[0].Entity, "test");
        }
#else
        strcpy(p_tmp->AccessProfile.AccessPolicy[0].Entity, "test");
#endif
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

HT_API void onvif_get_Schedule_token(ScheduleList * p_head, char * token, int size)
{
    ScheduleList * p_tmp = NULL;

    do {
        snprintf(token, size, "ScheduleToken_%u", ++g_onvif_idx.schedule_idx);

        p_tmp = onvif_find_Schedule(p_head, token);
    } while (p_tmp);
}

HT_API ScheduleList * onvif_add_Schedule(ScheduleList ** p_head)
{
    ScheduleList * p_tmp;
    ScheduleList * p_new = (ScheduleList *) malloc(sizeof(ScheduleList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ScheduleList));

    onvif_get_Schedule_token(*p_head, p_new->Schedule.token, sizeof(p_new->Schedule.token));

    snprintf(p_new->Schedule.Name, sizeof(p_new->Schedule.Name), "ScheduleName_%u", g_onvif_idx.schedule_idx);
        
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API ScheduleList * onvif_find_Schedule(ScheduleList * p_head, const char * token)
{
    ScheduleList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Schedule.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Schedule(ScheduleList ** p_head, ScheduleList * p_node)
{
    BOOL found = FALSE;
    ScheduleList * p_prev = NULL;
    ScheduleList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

#ifdef LIBICAL
        if (p_tmp->comp)
        {
            icalcomponent_free(p_tmp->comp);
        }
#endif
        free(p_tmp);
    }
}

HT_API void onvif_free_Schedules(ScheduleList ** p_head)
{
    ScheduleList * p_next;
    ScheduleList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

#ifdef LIBICAL
        if (p_tmp->comp)
        {
            icalcomponent_free(p_tmp->comp);
        }
#endif

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API BOOL onvif_init_Schedule()
{
    ScheduleList * p_tmp;
    
    if (g_onvif_cfg.schedule)
    {
        return TRUE;
    }
    
    p_tmp = onvif_add_Schedule(&g_onvif_cfg.schedule);
    if (p_tmp)
    {
        p_tmp->Schedule.DescriptionFlag = 1;
        snprintf(p_tmp->Schedule.Description, sizeof(p_tmp->Schedule.Description), 
            "Schedule %d", g_onvif_idx.schedule_idx);
        snprintf(p_tmp->Schedule.Standard, sizeof(p_tmp->Schedule.Standard),
            "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20171125T200000\r\n"
            "DTEND:20171126T020000\r\nEND:VEVENT\r\nEND:VCALENDAR");
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

HT_API void onvif_get_SpecialDayGroup_token(SpecialDayGroupList * p_head, char * token, int size)
{
    SpecialDayGroupList * p_tmp = NULL;

    do {
        snprintf(token, size, "SpecialDayGroupToken_%u", ++g_onvif_idx.specialdaygroup_idx);

        p_tmp = onvif_find_SpecialDayGroup(p_head, token);
    } while (p_tmp);
}

HT_API SpecialDayGroupList * onvif_add_SpecialDayGroup(SpecialDayGroupList ** p_head)
{
    SpecialDayGroupList * p_tmp;
    SpecialDayGroupList * p_new = (SpecialDayGroupList *) malloc(sizeof(SpecialDayGroupList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(SpecialDayGroupList));

    onvif_get_SpecialDayGroup_token(*p_head, p_new->SpecialDayGroup.token, sizeof(p_new->SpecialDayGroup.token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API SpecialDayGroupList * onvif_find_SpecialDayGroup(SpecialDayGroupList * p_head, const char * token)
{
    SpecialDayGroupList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->SpecialDayGroup.token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_SpecialDayGroup(SpecialDayGroupList ** p_head, SpecialDayGroupList * p_node)
{
    BOOL found = FALSE;
    SpecialDayGroupList * p_prev = NULL;
    SpecialDayGroupList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

#ifdef LIBICAL
        if (p_tmp->comp)
        {
            icalcomponent_free(p_tmp->comp);
        }
#endif

        free(p_tmp);
    }
}

HT_API void onvif_free_SpecialDayGroups(SpecialDayGroupList ** p_head)
{
    SpecialDayGroupList * p_next;
    SpecialDayGroupList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

#ifdef LIBICAL
        if (p_tmp->comp)
        {
            icalcomponent_free(p_tmp->comp);
        }
#endif

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API BOOL onvif_init_SpecialDayGroup()
{    
    return TRUE;
}

#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

HT_API void onvif_get_Receiver_token(ReceiverList * p_head, char * token, int size)
{
    ReceiverList * p_tmp = NULL;

    do {
        snprintf(token, size, "ReceiverToken_%u", ++g_onvif_idx.receiver_idx);

        p_tmp = onvif_find_Receiver(p_head, token);
    } while (p_tmp);
}

HT_API ReceiverList * onvif_add_Receiver(ReceiverList ** p_head)
{
    ReceiverList * p_tmp;
    ReceiverList * p_new = (ReceiverList *) malloc(sizeof(ReceiverList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ReceiverList));

    onvif_get_Receiver_token(*p_head, p_new->Receiver.Token, sizeof(p_new->Receiver.Token));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }    

    return p_new;
}

HT_API ReceiverList * onvif_find_Receiver(ReceiverList * p_head, const char * token)
{
    ReceiverList * p_tmp = p_head;  
    
    if (NULL == token)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(token, p_tmp->Receiver.Token) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Receiver(ReceiverList ** p_head, ReceiverList * p_node)
{
    BOOL found = FALSE;
    ReceiverList * p_prev = NULL;
    ReceiverList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }
        
        free(p_tmp);
    }
}

HT_API void onvif_free_Receivers(ReceiverList ** p_head)
{
    ReceiverList * p_next;
    ReceiverList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API int onvif_get_Receiver_nums(ReceiverList * p_head)
{
    int nums = 0;
    ReceiverList * p_tmp = p_head;

    while (p_tmp)
    {
        nums++;
        p_tmp = p_tmp->next;
    }

    return nums;
}

#endif // end of RECEIVER_SUPPORT

#ifdef IPFILTER_SUPPORT

HT_API BOOL onvif_is_ipaddr_filter_exist(onvif_PrefixedIPAddress * p_head, int size, onvif_PrefixedIPAddress * p_item)
{
    int i;
    for (i = 0; i < size; i++)
    {
        if (strcmp(p_item->Address, p_head[i].Address) == 0 && p_item->PrefixLength == p_head[i].PrefixLength)
        {
            return TRUE;
        }
    }

    return FALSE;    
}

HT_API onvif_PrefixedIPAddress * onvif_find_ipaddr_filter(onvif_PrefixedIPAddress * p_head, int size, onvif_PrefixedIPAddress * p_item)
{
    int i;
    for (i = 0; i < size; i++)
    {
        if (strcmp(p_item->Address, p_head[i].Address) == 0 && p_item->PrefixLength == p_head[i].PrefixLength)
        {
            return &p_head[i];
        }
    }

    return NULL;
}

HT_API onvif_PrefixedIPAddress * onvif_get_idle_ipaddr_filter(onvif_PrefixedIPAddress * p_head, int size)
{
    int i;
    
    for (i = 0; i < size; i++)
    {
        if (p_head[i].Address[0] == '\0')
        {
            return &p_head[i];
        }
    }

    return NULL;
}

HT_API ONVIF_RET onvif_add_ipaddr_filter(onvif_PrefixedIPAddress * p_head, int size, onvif_PrefixedIPAddress * p_item)
{
    onvif_PrefixedIPAddress * p_ipfilter;
    
    if (onvif_is_ipaddr_filter_exist(p_head, size, p_item) == TRUE)
    {
        return ONVIF_OK;
    }

    p_ipfilter = onvif_get_idle_ipaddr_filter(p_head, size);
    if (p_ipfilter)
    {
        p_ipfilter->PrefixLength = p_item->PrefixLength;
        strcpy(p_ipfilter->Address, p_item->Address);
        
        return ONVIF_OK;
    }

    return ONVIF_ERR_IPFilterListIsFull;
}

#endif // IPFILTER_SUPPORT

#ifdef SECURITY_SUPPORT

HT_API void onvif_get_KeyID(KeyList * p_head, char * keyid, int size)
{
    KeyList * p_tmp = NULL;

    do {
        snprintf(keyid, size, "keyid_%u", ++g_onvif_idx.key_idx);

        p_tmp = onvif_find_Key(p_head, keyid);
    } while (p_tmp);
}

HT_API KeyList * onvif_add_Key(KeyList ** p_head)
{
    KeyList * p_tmp;
    KeyList * p_new = (KeyList *) malloc(sizeof(KeyList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(KeyList));

    onvif_get_KeyID(*p_head, p_new->KeyAttribute.KeyID, sizeof(p_new->KeyAttribute.KeyID));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API KeyList * onvif_find_Key(KeyList * p_head, const char * keyid)
{
    KeyList * p_tmp = p_head;  
    
    if (NULL == keyid)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(keyid, p_tmp->KeyAttribute.KeyID) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API KeyList * onvif_find_key_by_pkey(KeyList * p_head, const char * pkey)
{
    KeyList * p_tmp = p_head;  
    
    if (NULL == pkey)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (p_tmp->public_key && strncmp(pkey, p_tmp->public_key, p_tmp->public_key_length) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Key(KeyList ** p_head, KeyList * p_node)
{
    BOOL found = FALSE;
    KeyList * p_prev = NULL;
    KeyList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        if (p_tmp->public_key)
        {
            free(p_tmp->public_key);
        }

        if (p_tmp->private_key)
        {
            free(p_tmp->private_key);
        }
        
        free(p_tmp);
    }
}

HT_API void onvif_free_Keys(KeyList ** p_head)
{
    KeyList * p_next;
    KeyList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->public_key)
        {
            free(p_tmp->public_key);
        }

        if (p_tmp->private_key)
        {
            free(p_tmp->private_key);
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_PassphraseID(PassphraseList * p_head, char * passphraseid, int size)
{
    PassphraseList * p_tmp = NULL;

    do {
        snprintf(passphraseid, size, "Passphrase_%u", ++g_onvif_idx.passphrase_idx);

        p_tmp = onvif_find_Passphrase(p_head, passphraseid);
    } while (p_tmp);
}

HT_API PassphraseList * onvif_add_Passphrase(PassphraseList ** p_head)
{
    PassphraseList * p_tmp;
    PassphraseList * p_new = (PassphraseList *) malloc(sizeof(PassphraseList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PassphraseList));

    onvif_get_PassphraseID(*p_head, p_new->PassphraseAttribute.PassphraseID, sizeof(p_new->PassphraseAttribute.PassphraseID));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API PassphraseList * onvif_find_Passphrase(PassphraseList * p_head, const char * passphraseid)
{
    PassphraseList * p_tmp = p_head;  
    
    if (NULL == passphraseid)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(passphraseid, p_tmp->PassphraseAttribute.PassphraseID) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Passphrase(PassphraseList ** p_head, PassphraseList * p_node)
{
    BOOL found = FALSE;
    PassphraseList * p_prev = NULL;
    PassphraseList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }
        
        free(p_tmp);
    }
}

HT_API void onvif_free_Passphrases(PassphraseList ** p_head)
{
    PassphraseList * p_next;
    PassphraseList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_CertificateID(CertificateList * p_head, char * certificateid, int size)
{
    CertificateList * p_tmp = NULL;

    do {
        snprintf(certificateid, size, "Certificate_%u", ++g_onvif_idx.certificate_idx);

        p_tmp = onvif_find_Certificate(p_head, certificateid);
    } while (p_tmp);
}

HT_API CertificateList * onvif_add_Certificate(CertificateList ** p_head)
{
    CertificateList * p_tmp;
    CertificateList * p_new = (CertificateList *) malloc(sizeof(CertificateList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(CertificateList));

    onvif_get_CertificateID(*p_head, p_new->Certificate.CertificateID, sizeof(p_new->Certificate.CertificateID));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API CertificateList * onvif_find_Certificate(CertificateList * p_head, const char * certificateid)
{
    CertificateList * p_tmp = p_head;  
    
    if (NULL == certificateid)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(certificateid, p_tmp->Certificate.CertificateID) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_Certificate(CertificateList ** p_head, CertificateList * p_node)
{
    BOOL found = FALSE;
    CertificateList * p_prev = NULL;
    CertificateList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }

        if (p_tmp->Certificate.CertificateContent.ptr)
        {
            free(p_tmp->Certificate.CertificateContent.ptr);
        }
        
        free(p_tmp);
    }
}

HT_API void onvif_free_Certificates(CertificateList ** p_head)
{
    CertificateList * p_next;
    CertificateList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->Certificate.CertificateContent.ptr)
        {
            free(p_tmp->Certificate.CertificateContent.ptr);
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API void onvif_get_CertificationPathID(CertificationPathList * p_head, char * certificatepathid, int size)
{
    CertificationPathList * p_tmp = NULL;

    do {
        snprintf(certificatepathid, size, "Certificatepath_%u", ++g_onvif_idx.certificatepath_idx);

        p_tmp = onvif_find_CertificationPath(p_head, certificatepathid);
    } while (p_tmp);
}

HT_API CertificationPathList * onvif_add_CertificationPath(CertificationPathList ** p_head)
{
    CertificationPathList * p_tmp;
    CertificationPathList * p_new = (CertificationPathList *) malloc(sizeof(CertificationPathList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(CertificationPathList));

    onvif_get_CertificationPathID(*p_head, p_new->CertificationPathID, sizeof(p_new->CertificationPathID));
    
    p_tmp = *p_head;
    if (NULL == p_tmp)
    {
        *p_head = p_new;
    }
    else
    {
        while (p_tmp && p_tmp->next) p_tmp = p_tmp->next;

        p_tmp->next = p_new;
    }

    return p_new;
}

HT_API CertificationPathList * onvif_find_CertificationPath(CertificationPathList * p_head, const char * certificatepathid)
{
    CertificationPathList * p_tmp = p_head;
    
    if (NULL == certificatepathid)
    {
        return NULL;
    }

    while (p_tmp)
    {
        if (strcmp(certificatepathid, p_tmp->CertificationPathID) == 0)
        {
            return p_tmp;
        }
        
        p_tmp = p_tmp->next;
    }

    return NULL;
}

HT_API void onvif_free_CertificationPath(CertificationPathList ** p_head, CertificationPathList * p_node)
{
    BOOL found = FALSE;
    CertificationPathList * p_prev = NULL;
    CertificationPathList * p_tmp = *p_head;    
    
    while (p_tmp)
    {
        if (p_tmp == p_node)
        {
            found = TRUE;
            break;
        }

        p_prev = p_tmp;
        p_tmp = p_tmp->next;
    }

    if (found)
    {
        if (NULL == p_prev)
        {
            *p_head = p_tmp->next;
        }
        else
        {
            p_prev->next = p_tmp->next;
        }
        
        free(p_tmp);
    }
}

HT_API void onvif_free_CertificationPaths(CertificationPathList ** p_head)
{
    CertificationPathList * p_next;
    CertificationPathList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

void onvif_init_Security()
{
    strcpy(g_onvif_cfg.tlsversions[0], "1.0");
    strcpy(g_onvif_cfg.tlsversions[1], "1.1");
    strcpy(g_onvif_cfg.tlsversions[2], "1.2");
    strcpy(g_onvif_cfg.tlsversions[3], "1.3");
}

void onvif_init_certification(void * ssl_ctx)
{
    int size = 0;
    char * buff = NULL;
    RSA * rsa = NULL;
    X509 * cert = SSL_CTX_get0_certificate((SSL_CTX *)ssl_ctx);
    EVP_PKEY * pkey = SSL_CTX_get0_privatekey((SSL_CTX *)ssl_ctx);
    KeyList * p_key;
    CertificateList * p_cert = NULL;
    CertificationPathList * p_certpath = NULL;

    if (NULL == cert || NULL == pkey)
    {
        return;
    }

    rsa = EVP_PKEY_get1_RSA(pkey);
    if (NULL == rsa)
    {
        return;
    }

    p_key = onvif_add_Key(&g_onvif_cfg.keys);
    if (NULL == p_key)
    {
        return;
    }

    if (!onvif_save_public_key(rsa, p_key))
    {
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
        return;
    }

    if (!onvif_save_private_key(rsa, p_key))
    {
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
        return;
    }

    if (!onvif_get_x509_base64_buff(cert, &buff, &size))
    {
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
        return;
    }

    p_cert = onvif_add_Certificate(&g_onvif_cfg.certificates);
    if (NULL == p_cert)
    {
        free(buff);
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
        return;
    }

    p_certpath = onvif_add_CertificationPath(&g_onvif_cfg.certificatepaths);
    if (NULL == p_certpath)
    {
        free(buff);
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
        onvif_free_Certificate(&g_onvif_cfg.certificates, p_cert);
        return;
    }

    p_key->KeyAttribute.hasPrivateKey = 1;
    strcpy(p_key->KeyAttribute.KeyStatus, "ok");
    
    p_cert->Certificate.CertificateContent.ptr = buff;
    p_cert->Certificate.CertificateContent.size = size;

    strcpy(p_cert->Certificate.KeyID, p_key->KeyAttribute.KeyID);

    strcpy(g_onvif_cfg.certpathid[0], p_certpath->CertificationPathID);
    strcpy(g_onvif_cfg.curcertpathid, p_certpath->CertificationPathID);
}

#endif // SECURITY_SUPPORT

HT_API void onvif_init_MulticastConfiguration(onvif_MulticastConfiguration * p_cfg)
{
    p_cfg->Port = 32002;
    p_cfg->TTL = 128;
    p_cfg->AutoStart = FALSE;
    strcpy(p_cfg->IPv4Address, "239.0.1.0");
}

/*
 * Initialize the video source
 * 
 */
void onvif_init_VideoSource()
{
    VideoSourceList * p_item;
    
    if (g_onvif_cfg.v_src)
    {
        return;
    }
    
    // todo : here init one video source (1280*720*25)
    
    p_item = onvif_add_VideoSource(&g_onvif_cfg.v_src, 1280, 720);
    if (p_item)
    {
    }
}

/*
 * Initialize the video source configuration options
 * 
 */
HT_API void onvif_init_VideoSourceConfigurationOptions(VideoSourceConfigurationList * p_item)
{
    // Specify the range can be configured to  the video source
    
    p_item->Options.BoundsRange.XRange.Min = p_item->Configuration.Bounds.x;
    p_item->Options.BoundsRange.XRange.Max = 100;

    p_item->Options.BoundsRange.YRange.Min = p_item->Configuration.Bounds.y;
    p_item->Options.BoundsRange.YRange.Max = 100;

    p_item->Options.BoundsRange.WidthRange.Min = 320;
    p_item->Options.BoundsRange.WidthRange.Max = p_item->Configuration.Bounds.width;

    p_item->Options.BoundsRange.HeightRange.Min = 240;
    p_item->Options.BoundsRange.HeightRange.Max = p_item->Configuration.Bounds.height;

    p_item->Options.sizeVideoSourceTokensAvailable = 1;
    strcpy(p_item->Options.VideoSourceTokensAvailable[0], p_item->Configuration.SourceToken);
}

void onvif_init_VideoSourceConfiguration()
{
    VideoSourceConfigurationList * p_item;
    
    if (g_onvif_cfg.v_src_cfg)
    {
        return;
    }

    p_item = onvif_add_VideoSourceConfiguration(&g_onvif_cfg.v_src_cfg, 1280, 720);
    if (p_item)
    {
        VideoSourceList * p_v_src = onvif_find_VideoSource_by_size(g_onvif_cfg.v_src, 1280, 720);
        if (NULL == p_v_src)
        {
            p_v_src = onvif_add_VideoSource(&g_onvif_cfg.v_src, 1280, 720);
        }

        if (p_v_src)
        {
            strcpy(p_item->Configuration.SourceToken, p_v_src->VideoSource.token);
            strcpy(p_item->Options.VideoSourceTokensAvailable[0], p_v_src->VideoSource.token);
            p_item->Options.sizeVideoSourceTokensAvailable = 1;
        }
    }
}

void onvif_init_VideoEncoderConfiguration()
{
    VideoEncoder2ConfigurationList * p_item;

    if (g_onvif_cfg.v_enc_cfg)
    {
        return;
    }

    p_item = onvif_add_VideoEncoder2Configuration(&g_onvif_cfg.v_enc_cfg, NULL);
    if (p_item)
    {
        strcpy(p_item->Configuration.Encoding, "H264");
        p_item->Configuration.VideoEncoding = VideoEncoding_H264;
        p_item->Configuration.Resolution.Width = 1280;
        p_item->Configuration.Resolution.Height = 720;
        p_item->Configuration.Quality = 4;
        p_item->Configuration.RateControlFlag = 1;
        p_item->Configuration.RateControl.FrameRateLimit = 25;
        p_item->Configuration.RateControl.EncodingInterval = 1;
        p_item->Configuration.RateControl.BitrateLimit = 2048;
        p_item->Configuration.GovLengthFlag = 1;
        p_item->Configuration.GovLength = 25;
        p_item->Configuration.ProfileFlag = 1;
        strcpy(p_item->Configuration.Profile, "Main");
        p_item->Configuration.SessionTimeout = 10;
        p_item->Configuration.MulticastFlag = 1;
        onvif_init_MulticastConfiguration(&p_item->Configuration.Multicast);
    }

    p_item = onvif_add_VideoEncoder2Configuration(&g_onvif_cfg.v_enc_cfg, NULL);
    if (p_item)
    {
        strcpy(p_item->Configuration.Encoding, "H264");
        p_item->Configuration.VideoEncoding = VideoEncoding_H264;
        p_item->Configuration.Resolution.Width = 640;
        p_item->Configuration.Resolution.Height = 480;
        p_item->Configuration.Quality = 4;
        p_item->Configuration.RateControlFlag = 1;
        p_item->Configuration.RateControl.FrameRateLimit = 25;
        p_item->Configuration.RateControl.EncodingInterval = 1;
        p_item->Configuration.RateControl.BitrateLimit = 2048;
        p_item->Configuration.GovLengthFlag = 1;
        p_item->Configuration.GovLength = 25;
        p_item->Configuration.ProfileFlag = 1;
        strcpy(p_item->Configuration.Profile, "Main");
        p_item->Configuration.SessionTimeout = 10;
        p_item->Configuration.MulticastFlag = 1;
        onvif_init_MulticastConfiguration(&p_item->Configuration.Multicast);
    }
}

HT_API void onvif_init_VideoEncoder2ConfigurationOptions(onvif_VideoEncoder2Configuration * p_cfg, onvif_VideoEncoder2ConfigurationOptions * p_option, const char * Encoding)
{
    strcpy(p_option->Encoding, Encoding);
    if (strcasecmp(Encoding, "JPEG") == 0)
    {
        p_option->VideoEncoding = VideoEncoding_JPEG;
    }
    else if (strcasecmp(Encoding, "MP4V-ES") == 0)
    {
        p_option->VideoEncoding = VideoEncoding_MPEG4;

        p_option->GovLengthRangeFlag = 1;
        strcpy(p_option->GovLengthRange, "1 60");

        p_option->ProfilesSupportedFlag = 1;
        strcpy(p_option->ProfilesSupported, "Simple AdvancedSimple");
    }
    else if (strcasecmp(Encoding, "H264") == 0)
    {
        p_option->VideoEncoding = VideoEncoding_H264;

        p_option->GovLengthRangeFlag = 1;
        strcpy(p_option->GovLengthRange, "1 60");

        p_option->ProfilesSupportedFlag = 1;
        strcpy(p_option->ProfilesSupported, "Baseline Main High");
    }
    else if (strcasecmp(Encoding, "H265") == 0)
    {
        // The onvif media 1 service does not support H265, 
        //  but the onvif media 2 service sets the video encoding to H265. 
        //  So, here, the onvif media 1 service field is set to H264
        p_option->VideoEncoding = VideoEncoding_H264;

        p_option->GovLengthRangeFlag = 1;
        strcpy(p_option->GovLengthRange, "1 60");

        p_option->ProfilesSupportedFlag = 1;
        strcpy(p_option->ProfilesSupported, "Main Main10");
    }

    p_option->FrameRatesSupportedFlag = 1;
    strcpy(p_option->FrameRatesSupported, "30 29 25 23 20 19 18 15 12 10 8 5");

    p_option->ConstantBitRateSupported = 0;
    
    p_option->MaxAnchorFrameDistanceFlag = 0;
    p_option->MaxAnchorFrameDistance = 0;

    p_option->GuaranteedFrameRateSupported = 0;
    
    p_option->QualityRange.Min = 0;
    p_option->QualityRange.Max = 100;

    p_option->BitrateRange.Min = 64;
    p_option->BitrateRange.Max = 4096;

    // todo : Different video resolution sizes can be set based on 
    //  the video encoder configuration information pointed to by p_cfg
    
    p_option->ResolutionsAvailable[0].Width = 1920;
    p_option->ResolutionsAvailable[0].Height = 1080;
    p_option->ResolutionsAvailable[1].Width = 1280;
    p_option->ResolutionsAvailable[1].Height = 720;
    p_option->ResolutionsAvailable[2].Width = 640;
    p_option->ResolutionsAvailable[2].Height = 480;
    p_option->ResolutionsAvailable[3].Width = 352;
    p_option->ResolutionsAvailable[3].Height = 288;
    p_option->ResolutionsAvailable[4].Width = 320;
    p_option->ResolutionsAvailable[4].Height = 240;
}

HT_API void onvif_init_VideoEncoderConfigurationOptions(VideoEncoder2ConfigurationList * p_item)
{
#ifdef MEDIA2_SUPPORT
    VideoEncoder2ConfigurationOptionsList * p_option;

    p_option = onvif_add_VideoEncoder2ConfigurationOptions(&p_item->Options2);

    onvif_init_VideoEncoder2ConfigurationOptions(&p_item->Configuration, &p_option->Options, "JPEG");

#ifdef MPEG4_SUPPORT
    p_option = onvif_add_VideoEncoder2ConfigurationOptions(&p_item->Options2);

    onvif_init_VideoEncoder2ConfigurationOptions(&p_item->Configuration, &p_option->Options, "MP4V-ES");
#endif

    p_option = onvif_add_VideoEncoder2ConfigurationOptions(&p_item->Options2);

    onvif_init_VideoEncoder2ConfigurationOptions(&p_item->Configuration, &p_option->Options, "H264");

    p_option = onvif_add_VideoEncoder2ConfigurationOptions(&p_item->Options2);

    onvif_init_VideoEncoder2ConfigurationOptions(&p_item->Configuration, &p_option->Options, "H265");
#endif

    // video encoder config options
#ifndef ANDROID
    p_item->Options.JPEGFlag = 1;
#ifdef MPEG4_SUPPORT    
    p_item->Options.MPEG4Flag = 1;
#endif
#endif
    p_item->Options.H264Flag = 1;
    p_item->Options.QualityRange.Min = 0;
    p_item->Options.QualityRange.Max = 100;    

    // todo : Different video resolution sizes can be set based on 
    //  the video encoder configuration information pointed to by 
    //  p_item->Configuration
    
    // jpeg config options
    p_item->Options.JPEG.ResolutionsAvailable[0].Width = 1920;
    p_item->Options.JPEG.ResolutionsAvailable[0].Height = 1080;
    p_item->Options.JPEG.ResolutionsAvailable[1].Width = 1280;
    p_item->Options.JPEG.ResolutionsAvailable[1].Height = 720;
    p_item->Options.JPEG.ResolutionsAvailable[2].Width = 640;
    p_item->Options.JPEG.ResolutionsAvailable[2].Height = 480;
    p_item->Options.JPEG.ResolutionsAvailable[3].Width = 352;
    p_item->Options.JPEG.ResolutionsAvailable[3].Height = 288;
    p_item->Options.JPEG.ResolutionsAvailable[4].Width = 320;
    p_item->Options.JPEG.ResolutionsAvailable[4].Height = 240;
    p_item->Options.JPEG.FrameRateRange.Min = 1;
    p_item->Options.JPEG.FrameRateRange.Max = 30;
    p_item->Options.JPEG.EncodingIntervalRange.Min = 1;
    p_item->Options.JPEG.EncodingIntervalRange.Max = 60;

#ifdef MPEG4_SUPPORT
    // mpeg4 config options
    p_item->Options.MPEG4.ResolutionsAvailable[0].Width = 1920;
    p_item->Options.MPEG4.ResolutionsAvailable[0].Height = 1080;
    p_item->Options.MPEG4.ResolutionsAvailable[1].Width = 1280;
    p_item->Options.MPEG4.ResolutionsAvailable[1].Height = 720;
    p_item->Options.MPEG4.ResolutionsAvailable[2].Width = 640;
    p_item->Options.MPEG4.ResolutionsAvailable[2].Height = 480;
    p_item->Options.MPEG4.ResolutionsAvailable[3].Width = 352;
    p_item->Options.MPEG4.ResolutionsAvailable[3].Height = 288;
    p_item->Options.MPEG4.ResolutionsAvailable[4].Width = 320;
    p_item->Options.MPEG4.ResolutionsAvailable[4].Height = 240;
    p_item->Options.MPEG4.Mpeg4Profile_SP = 1;
    p_item->Options.MPEG4.GovLengthRange.Min = 1;
    p_item->Options.MPEG4.GovLengthRange.Max = 60;
    p_item->Options.MPEG4.FrameRateRange.Min = 1;
    p_item->Options.MPEG4.FrameRateRange.Max = 30;
    p_item->Options.MPEG4.EncodingIntervalRange.Min = 1;
    p_item->Options.MPEG4.EncodingIntervalRange.Max = 60;
#endif

    // h264 config options
    p_item->Options.H264.ResolutionsAvailable[0].Width = 1920;
    p_item->Options.H264.ResolutionsAvailable[0].Height = 1080;
    p_item->Options.H264.ResolutionsAvailable[1].Width = 1280;
    p_item->Options.H264.ResolutionsAvailable[1].Height = 720;
    p_item->Options.H264.ResolutionsAvailable[2].Width = 640;
    p_item->Options.H264.ResolutionsAvailable[2].Height = 480;
#ifndef ANDROID    
    p_item->Options.H264.ResolutionsAvailable[3].Width = 352;
    p_item->Options.H264.ResolutionsAvailable[3].Height = 288;
    p_item->Options.H264.ResolutionsAvailable[4].Width = 320;
    p_item->Options.H264.ResolutionsAvailable[4].Height = 240;
#endif    
    p_item->Options.H264.H264Profile_Baseline = 1;
    p_item->Options.H264.H264Profile_Main = 1;
    p_item->Options.H264.GovLengthRange.Min = 1;
    p_item->Options.H264.GovLengthRange.Max = 60;
    p_item->Options.H264.FrameRateRange.Min = 1;
    p_item->Options.H264.FrameRateRange.Max = 30;
    p_item->Options.H264.EncodingIntervalRange.Min = 1;
    p_item->Options.H264.EncodingIntervalRange.Max = 60;
}

void onvif_init_MetadataConfiguration()
{
    MetadataConfigurationList * p_node;
    
    if (g_onvif_cfg.metadata_cfg)
    {
        return;
    }
    
    p_node = onvif_add_MetadataConfiguration(&g_onvif_cfg.metadata_cfg);
    if (NULL == p_node)
    {
        return;
    }

    p_node->Configuration.AnalyticsFlag = 1;
    p_node->Configuration.Analytics = FALSE;

    p_node->Configuration.SessionTimeout = 60;
    p_node->Configuration.PTZStatusFlag = 1;
    p_node->Configuration.PTZStatus.Status = 1;
    p_node->Configuration.PTZStatus.Position = 0;

    onvif_init_MulticastConfiguration(&p_node->Configuration.Multicast);
}

void onvif_init_MetadataConfigurationOptions()
{
    onvif_MetadataConfigurationOptions * p_opt = &g_onvif_cfg.MetadataConfigurationOptions;
    
    p_opt->PTZStatusFilterOptions.PanTiltPositionSupported = FALSE;
    p_opt->PTZStatusFilterOptions.ZoomPositionSupported = FALSE;
    p_opt->PTZStatusFilterOptions.PanTiltStatusSupported = TRUE;
    p_opt->PTZStatusFilterOptions.ZoomStatusSupported = TRUE;
}

void onvif_init_OSDConfigurations()
{
    OSDConfigurationList * p_osd;
    
    if (g_onvif_cfg.OSDs)
    {
        return;
    }
    
    p_osd = onvif_add_OSDConfiguration(&g_onvif_cfg.OSDs);
    if (p_osd)
    {
        if (g_onvif_cfg.v_src_cfg)
        {
            strcpy(p_osd->OSD.VideoSourceConfigurationToken, g_onvif_cfg.v_src_cfg->Configuration.token);
        }

        p_osd->OSD.Type = OSDType_Text;
        p_osd->OSD.Position.Type = OSDPosType_UpperLeft;
        p_osd->OSD.TextStringFlag = 1;
        p_osd->OSD.TextString.Type = OSDTextType_Plain;
        p_osd->OSD.TextString.PlainTextFlag = 1;
        strcpy(p_osd->OSD.TextString.PlainText, "OSD Test");
    }
}

void onvif_init_OSDConfigurationOptions()
{
    onvif_OSDConfigurationOptions * p_opt = &g_onvif_cfg.OSDConfigurationOptions;
    
    p_opt->OSDType_Text = 1;
    p_opt->OSDType_Image = 0;
    p_opt->OSDType_Extended = 0;
    p_opt->OSDPosType_UpperLeft = 1;
    p_opt->OSDPosType_UpperRight = 1;
    p_opt->OSDPosType_LowerLeft = 1;
    p_opt->OSDPosType_LowerRight = 1;
    p_opt->OSDPosType_Custom = 1;
    p_opt->TextOptionFlag = 1;
    p_opt->ImageOptionFlag = 0;
    
    p_opt->MaximumNumberOfOSDs.ImageFlag = 0;
    p_opt->MaximumNumberOfOSDs.PlainTextFlag = 1;
    p_opt->MaximumNumberOfOSDs.DateFlag = 1;
    p_opt->MaximumNumberOfOSDs.TimeFlag = 1;
    p_opt->MaximumNumberOfOSDs.DateAndTimeFlag = 1;

    p_opt->MaximumNumberOfOSDs.Total = 5;
    p_opt->MaximumNumberOfOSDs.Image = 0;
    p_opt->MaximumNumberOfOSDs.PlainText = 4;
    p_opt->MaximumNumberOfOSDs.Date = 1;
    p_opt->MaximumNumberOfOSDs.Time = 1;
    p_opt->MaximumNumberOfOSDs.DateAndTime = 1;
    
    p_opt->TextOption.OSDTextType_Plain = 1;
    p_opt->TextOption.OSDTextType_Date = 1;
    p_opt->TextOption.OSDTextType_Time = 1;
    p_opt->TextOption.OSDTextType_DateAndTime = 1;
    p_opt->TextOption.FontSizeRangeFlag = 1;
    p_opt->TextOption.FontColorFlag = 0;
    p_opt->TextOption.BackgroundColorFlag = 0;

    p_opt->TextOption.FontSizeRange.Min = 16;
    p_opt->TextOption.FontSizeRange.Max = 64;

    p_opt->TextOption.DateFormatSize = 4;
    strcpy(p_opt->TextOption.DateFormat[0], "MM/dd/yyyy");
    strcpy(p_opt->TextOption.DateFormat[1], "dd/MM/yyyy");
    strcpy(p_opt->TextOption.DateFormat[2], "yyyy/MM/dd");
    strcpy(p_opt->TextOption.DateFormat[3], "yyyy-MM-dd");

    p_opt->TextOption.TimeFormatSize = 2;
    strcpy(p_opt->TextOption.TimeFormat[0], "hh:mm:ss tt");
    strcpy(p_opt->TextOption.TimeFormat[1], "HH:mm:ss");
}

void onvif_init_profile()
{
    ONVIF_PROFILE * p_item;
    ONVIF_CFG * p_config = &g_onvif_cfg;

    if (p_config->profiles)
    {
        return;
    }

    p_item = onvif_add_profile(&p_config->profiles, TRUE);
    if (p_item)
    {
        p_item->v_src_cfg = p_config->v_src_cfg;
        if (p_item->v_src_cfg)
        {
            p_item->v_src_cfg->Configuration.UseCount++;
        }
        
        p_item->v_enc_cfg = p_config->v_enc_cfg;
        if (p_item->v_enc_cfg)
        {
            p_item->v_enc_cfg->Configuration.UseCount++;
        }

#ifdef AUDIO_SUPPORT        
        p_item->a_src_cfg = p_config->a_src_cfg;
        if (p_item->a_src_cfg)
        {
            p_item->a_src_cfg->Configuration.UseCount++;
        }
        
        p_item->a_enc_cfg = p_config->a_enc_cfg;
        if (p_item->a_enc_cfg)
        {
            p_item->a_enc_cfg->Configuration.UseCount++;
        }
#endif        
    }

    p_item = onvif_add_profile(&p_config->profiles, TRUE);
    if (p_item)
    {
        p_item->v_src_cfg = p_config->v_src_cfg;
        if (p_item->v_src_cfg)
        {
            p_item->v_src_cfg->Configuration.UseCount++;
        }

        if (p_config->v_enc_cfg)
        {
            p_item->v_enc_cfg = p_config->v_enc_cfg->next;
        }
        else
        {
            p_item->v_enc_cfg = p_config->v_enc_cfg;
        }

        if (p_item->v_enc_cfg)
        {
            p_item->v_enc_cfg->Configuration.UseCount++;
        }

#ifdef AUDIO_SUPPORT        
        p_item->a_src_cfg = p_config->a_src_cfg;
        if (p_item->a_src_cfg)
        {
            p_item->a_src_cfg->Configuration.UseCount++;
        }
        
        p_item->a_enc_cfg = p_config->a_enc_cfg;
        if (p_item->a_enc_cfg)
        {
            p_item->a_enc_cfg->Configuration.UseCount++;
        }
#endif
    }
}

#ifdef IMAGE_SUPPORT

HT_API void onvif_init_ImagingSettings(VideoSourceList * p_vsrc, onvif_ImagingSettings * p_item)
{
    // init image setting    
    // note : Optional field flag is set to 0, this option will not appear

    p_item->BacklightCompensationFlag = 1;
    p_item->BacklightCompensation.Mode = BacklightCompensationMode_OFF;
    p_item->BacklightCompensation.LevelFlag = 1;
    p_item->BacklightCompensation.Level = 10;
    
    p_item->BrightnessFlag = 1;
    p_item->Brightness = 50;
    
    p_item->ColorSaturationFlag = 1;
    p_item->ColorSaturation = 50;
    
    p_item->ContrastFlag = 1;
    p_item->Contrast = 50;
    
    p_item->ExposureFlag = 1;
    p_item->Exposure.Mode = ExposureMode_AUTO;
    
    p_item->Exposure.PriorityFlag = 1;
    p_item->Exposure.Priority = ExposurePriority_LowNoise;

    p_item->Exposure.WindowFlag = 1;
    p_item->Exposure.Window.bottom = 1;
    p_item->Exposure.Window.top = 0;
    p_item->Exposure.Window.right = 1;
    p_item->Exposure.Window.left = 0;
    
    p_item->Exposure.MinExposureTimeFlag = 1;
    p_item->Exposure.MinExposureTime = 10;
    
    p_item->Exposure.MaxExposureTimeFlag = 1;
    p_item->Exposure.MaxExposureTime = 40000;
    
    p_item->Exposure.MinGainFlag = 1;
    p_item->Exposure.MinGain = 0;
    
    p_item->Exposure.MaxGainFlag = 1;
    p_item->Exposure.MaxGain = 100;
    
    p_item->Exposure.MinIrisFlag = 1;
    p_item->Exposure.MinIris = 0;
    
    p_item->Exposure.MaxIrisFlag = 1;
    p_item->Exposure.MaxIris = 10;
    
    p_item->Exposure.ExposureTimeFlag = 1;
    p_item->Exposure.ExposureTime = 4000;
    
    p_item->Exposure.GainFlag = 1;
    p_item->Exposure.Gain = 100;
    
    p_item->Exposure.IrisFlag = 1;
    p_item->Exposure.Iris = 10;
    
    p_item->FocusFlag = 1;
    p_item->Focus.AutoFocusMode = AutoFocusMode_AUTO;
    
    p_item->Focus.DefaultSpeedFlag = 1;
    p_item->Focus.DefaultSpeed = 100;
    
    p_item->Focus.NearLimitFlag = 1;
    p_item->Focus.NearLimit = 100;
    
    p_item->Focus.FarLimitFlag = 1;
    p_item->Focus.FarLimit = 1000;
    
    p_item->IrCutFilterFlag = 1;
    p_item->IrCutFilter = IrCutFilterMode_AUTO;
    
    p_item->SharpnessFlag = 1;
    p_item->Sharpness = 50;
    
    p_item->WideDynamicRangeFlag = 1;
    p_item->WideDynamicRange.Mode = WideDynamicMode_OFF;
    
    p_item->WideDynamicRange.LevelFlag = 1;
    p_item->WideDynamicRange.Level = 50;
    
    p_item->WhiteBalanceFlag = 1;
    p_item->WhiteBalance.Mode = WhiteBalanceMode_AUTO;
    
    p_item->WhiteBalance.CbGainFlag = 1;
    p_item->WhiteBalance.CbGain = 10;
    
    p_item->WhiteBalance.CrGainFlag = 1;
    p_item->WhiteBalance.CrGain = 10;
}

HT_API void onvif_init_ImagingOptions(VideoSourceList * p_vsrc, onvif_ImagingOptions * p_item)
{
    // init image config options
    // note : Optional field flag is set to 0, this option will not appear

    p_item->BacklightCompensationFlag = 1;
    p_item->BacklightCompensation.Mode_OFF = 1;
    p_item->BacklightCompensation.Mode_ON = 1;
    p_item->BacklightCompensation.LevelFlag = 1;
    p_item->BacklightCompensation.Level.Min = 0;
    p_item->BacklightCompensation.Level.Max = 100;
    
    p_item->BrightnessFlag = 1;
    p_item->Brightness.Min = 0;
    p_item->Brightness.Max = 100;
    
    p_item->ColorSaturationFlag = 1;
    p_item->ColorSaturation.Min = 0;
    p_item->ColorSaturation.Max = 100;
    
    p_item->ContrastFlag = 1;
    p_item->Contrast.Min = 0;
    p_item->Contrast.Max = 100;
    
    p_item->ExposureFlag = 1;
    p_item->Exposure.Mode_AUTO = 1;
    p_item->Exposure.Mode_MANUAL = 1;
    p_item->Exposure.Priority_LowNoise = 1;
    p_item->Exposure.Priority_FrameRate = 1;
    
    p_item->Exposure.MinExposureTimeFlag = 1;
    p_item->Exposure.MinExposureTime.Min = 10;
    p_item->Exposure.MinExposureTime.Max = 10;
    
    p_item->Exposure.MaxExposureTimeFlag = 1;
    p_item->Exposure.MaxExposureTime.Min = 10;
    p_item->Exposure.MaxExposureTime.Max = 320000;

    p_item->Exposure.MinGainFlag = 1;
    p_item->Exposure.MinGain.Min = 0;
    p_item->Exposure.MinGain.Max = 0;

    p_item->Exposure.MaxGainFlag = 1;
    p_item->Exposure.MaxGain.Min = 0;
    p_item->Exposure.MaxGain.Max = 100;

    p_item->Exposure.MinIrisFlag = 1;
    p_item->Exposure.MinIris.Min = 0;
    p_item->Exposure.MinIris.Max = 10;

    p_item->Exposure.MaxIrisFlag = 1;
    p_item->Exposure.MaxIris.Min = 0;
    p_item->Exposure.MaxIris.Max = 10;

    p_item->Exposure.ExposureTimeFlag = 1;
    p_item->Exposure.ExposureTime.Min = 0;
    p_item->Exposure.ExposureTime.Max = 40000;

    p_item->Exposure.GainFlag = 1;
    p_item->Exposure.Gain.Min = 0;
    p_item->Exposure.Gain.Max = 100;

    p_item->Exposure.IrisFlag = 1;
    p_item->Exposure.Iris.Min = 0;
    p_item->Exposure.Iris.Max = 100;

    p_item->FocusFlag = 1;
    p_item->Focus.AutoFocusModes_AUTO = 1;
    p_item->Focus.AutoFocusModes_MANUAL = 1;

    p_item->Focus.DefaultSpeedFlag = 1;
    p_item->Focus.DefaultSpeed.Min = 0;
    p_item->Focus.DefaultSpeed.Max = 100;

    p_item->Focus.NearLimitFlag = 1;
    p_item->Focus.NearLimit.Min = 0;
    p_item->Focus.NearLimit.Max = 100;

    p_item->Focus.FarLimitFlag = 1;
    p_item->Focus.FarLimit.Min = 0;
    p_item->Focus.FarLimit.Max = 1000;    
    
    p_item->IrCutFilterMode_ON = 1;
    p_item->IrCutFilterMode_OFF = 1;
    p_item->IrCutFilterMode_AUTO = 1;

    p_item->SharpnessFlag = 1;
    p_item->Sharpness.Min = 0;
    p_item->Sharpness.Max = 100;

    p_item->WideDynamicRangeFlag = 1;    
    p_item->WideDynamicRange.Mode_OFF = 1;
    p_item->WideDynamicRange.Mode_ON = 1;

    p_item->WideDynamicRange.LevelFlag = 1;
    p_item->WideDynamicRange.Level.Min = 0;
    p_item->WideDynamicRange.Level.Max = 100;

    p_item->WhiteBalanceFlag = 1;
    p_item->WhiteBalance.Mode_AUTO = 1;
    p_item->WhiteBalance.Mode_MANUAL = 1;

    p_item->WhiteBalance.YrGainFlag = 1;
    p_item->WhiteBalance.YrGain.Min = 0;
    p_item->WhiteBalance.YrGain.Max = 100;

    p_item->WhiteBalance.YbGainFlag = 1;
    p_item->WhiteBalance.YbGain.Min = 0;
    p_item->WhiteBalance.YbGain.Max = 100;
}

#endif // end of IMAGE_SUPPORT

HT_API void onvif_init_capabilities()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
#ifdef DEVICEIO_SUPPORT
    int vsrc = 0, vout = 0;
    int relay_output = 0, serial_port = 0, digit_input = 0;
    VideoSourceList    * p_vsrc;
    VideoOutputList    * p_vout;
    RelayOutputList    * p_relay_output;
    SerialPortList     * p_serial_port;
    DigitalInputList   * p_digital_input;    
#ifdef AUDIO_SUPPORT
    int asrc = 0, aout = 0;
    AudioSourceList    * p_asrc;
    AudioOutputList    * p_aout;
#endif    
#endif

    // network capabilities

#ifdef IPFILTER_SUPPORT    
    p_config->Capabilities.device.IPFilter = 1;
#endif
    p_config->Capabilities.device.ZeroConfiguration = 1;
    p_config->Capabilities.device.DynDNS = 1;
    p_config->Capabilities.device.HostnameFromDHCP = 1;
    p_config->Capabilities.device.IPVersion6 = p_config->ipv6_enable;
    p_config->Capabilities.device.DHCPv6 = p_config->ipv6_enable;

    // system capabilities
    p_config->Capabilities.device.DiscoveryResolve = 1;
    p_config->Capabilities.device.DiscoveryBye = 1;
    p_config->Capabilities.device.RemoteDiscovery = 0;
    p_config->Capabilities.device.SystemBackup = 1;
    p_config->Capabilities.device.SystemLogging = 1;
    p_config->Capabilities.device.FirmwareUpgrade = 1;
    p_config->Capabilities.device.HttpFirmwareUpgrade = 1;
    p_config->Capabilities.device.HttpSystemBackup = 1;
    p_config->Capabilities.device.HttpSystemLogging = 1;
    p_config->Capabilities.device.HttpSupportInformation = 1;
    p_config->Capabilities.device.DiscoveryNotSupported = 0;
    p_config->Capabilities.device.NetworkConfigNotSupported = 0;
    p_config->Capabilities.device.UserConfigNotSupported = 0;

    p_config->Capabilities.device.sizeSupportedVersions = 10;
    p_config->Capabilities.device.SupportedVersions[0].Major = 24;
    p_config->Capabilities.device.SupportedVersions[0].Minor = 12;
    p_config->Capabilities.device.SupportedVersions[1].Major = 24;
    p_config->Capabilities.device.SupportedVersions[1].Minor = 6;
    p_config->Capabilities.device.SupportedVersions[2].Major = 23;
    p_config->Capabilities.device.SupportedVersions[2].Minor = 12;
    p_config->Capabilities.device.SupportedVersions[3].Major = 23;
    p_config->Capabilities.device.SupportedVersions[3].Minor = 6;
    p_config->Capabilities.device.SupportedVersions[4].Major = 22;
    p_config->Capabilities.device.SupportedVersions[4].Minor = 12;
    p_config->Capabilities.device.SupportedVersions[5].Major = 22;
    p_config->Capabilities.device.SupportedVersions[5].Minor = 6;
    p_config->Capabilities.device.SupportedVersions[6].Major = 21;
    p_config->Capabilities.device.SupportedVersions[6].Minor = 12;
    p_config->Capabilities.device.SupportedVersions[7].Major = 21;
    p_config->Capabilities.device.SupportedVersions[7].Minor = 6;
    p_config->Capabilities.device.SupportedVersions[8].Major = 20;
    p_config->Capabilities.device.SupportedVersions[8].Minor = 12;
    p_config->Capabilities.device.SupportedVersions[9].Major = 20;
    p_config->Capabilities.device.SupportedVersions[9].Minor = 6;

    p_config->Capabilities.device.Version.Major = 23;
    p_config->Capabilities.device.Version.Minor = 12;
    
#ifdef STORAGE_SUPPORT
    p_config->Capabilities.device.StorageConfiguration = 1;
    p_config->Capabilities.device.MaxStorageConfigurations = 10;
#endif

#ifdef GEOLOCATION_SUPPORT
    p_config->Capabilities.device.GeoLocationEntries = 10;
#endif

#ifdef DOT11_SUPPORT
    p_config->Capabilities.device.Dot11Configuration = 1;
    p_config->Capabilities.device.Dot1XConfigurations = 1;
    // dot11 capabilities
    p_config->Capabilities.dot11.TKIP = 1;
    p_config->Capabilities.dot11.ScanAvailableNetworks = 1;
    p_config->Capabilities.dot11.MultipleConfiguration = 0;
    p_config->Capabilities.dot11.AdHocStationMode = 1;
    p_config->Capabilities.dot11.WEP = 1;
#endif

    // scurity capabilities
#ifdef HTTPS
    if (p_config->https_enable)
    {
        p_config->Capabilities.device.TLS10 = 1;
        p_config->Capabilities.device.TLS11 = 1;
        p_config->Capabilities.device.TLS12 = 1;
    }
#endif
    
    p_config->Capabilities.device.OnboardKeyGeneration = 0;
    p_config->Capabilities.device.AccessPolicyConfig = 1;
    p_config->Capabilities.device.DefaultAccessPolicy = 1;
    p_config->Capabilities.device.Dot1X = 1;
    p_config->Capabilities.device.RemoteUserHandling = 1;
    p_config->Capabilities.device.X509Token = 0;
    p_config->Capabilities.device.SAMLToken = 0;
    p_config->Capabilities.device.KerberosToken = 0;
    p_config->Capabilities.device.UsernameToken = 1;
    p_config->Capabilities.device.HttpDigest = 1;
    p_config->Capabilities.device.RELToken = 0;
    p_config->Capabilities.device.JsonWebToken = 0;

    p_config->Capabilities.device.Dot1XConfigurations = 1;
    p_config->Capabilities.device.NTP = MAX_NTP_SERVER;
    p_config->Capabilities.device.SupportedEAPMethods = 0;
    p_config->Capabilities.device.MaxUsers = MAX_USERS;
    p_config->Capabilities.device.MaxUserNameLength = 32;
    p_config->Capabilities.device.MaxPasswordLength = 32;

    if (p_config->md5_hashing && p_config->sha256_hashing)
    {
        strcpy(p_config->Capabilities.device.HashingAlgorithms, "MD5,SHA-256");
    }
    else if (p_config->md5_hashing)
    {
        strcpy(p_config->Capabilities.device.HashingAlgorithms, "MD5");
    }
    else if (p_config->sha256_hashing)
    {
        strcpy(p_config->Capabilities.device.HashingAlgorithms, "SHA-256");
    }
    
#ifdef DEVICEIO_SUPPORT
    p_config->Capabilities.device.InputConnectors = 1;
    p_config->Capabilities.device.RelayOutputs = 1;
#endif

#ifdef SECURITY_SUPPORT
    strcpy(p_config->Capabilities.device.Addons, "TLSServerConfiguration");
#endif

#ifdef MEDIA_SUPPORT
    // media capabilities
    p_config->Capabilities.media.SnapshotUri = 1;
    p_config->Capabilities.media.Rotation = 0;
    p_config->Capabilities.media.VideoSourceMode = 1;
    p_config->Capabilities.media.OSD = 1;
    p_config->Capabilities.media.EXICompression = 0;
    p_config->Capabilities.media.RTPMulticast = 0;
    p_config->Capabilities.media.RTP_TCP = 1;
    p_config->Capabilities.media.RTP_RTSP_TCP = 1;
    p_config->Capabilities.media.NonAggregateControl = 0;
    p_config->Capabilities.media.NoRTSPStreaming = 0;
    p_config->Capabilities.media.support = 1;
    p_config->Capabilities.media.Version.Major = 22;
    p_config->Capabilities.media.Version.Minor = 12;

    p_config->Capabilities.media.MaximumNumberOfProfiles = 10;
#endif // MEDIA_SUPPORT

#ifdef MEDIA2_SUPPORT
    // media2 capabilities
    p_config->Capabilities.media2.StreamingCapabilities.RTP_RTSP_TCP = 1;
    p_config->Capabilities.media2.StreamingCapabilities.RTSPStreaming = 1;
    p_config->Capabilities.media2.StreamingCapabilities.RTPMulticast = 1;
    p_config->Capabilities.media2.StreamingCapabilities.AutoStartMulticast = 0;
    p_config->Capabilities.media2.StreamingCapabilities.SecureRTSPStreaming = 0;
    p_config->Capabilities.media2.SnapshotUri = 1;
    p_config->Capabilities.media2.Rotation = 0;
    p_config->Capabilities.media2.VideoSourceMode = 1;
    p_config->Capabilities.media2.OSD = 1;
    p_config->Capabilities.media2.TemporaryOSDText = 1;
    p_config->Capabilities.media2.Mask = 1;
    p_config->Capabilities.media2.SourceMask = 1;
    p_config->Capabilities.media2.ProfileCapabilities.MaximumNumberOfProfilesFlag = 1;
    p_config->Capabilities.media2.ProfileCapabilities.MaximumNumberOfProfiles = 10;
    p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupportedFlag = 1;

#ifdef RTSP_OVER_WEBSOCKET
    strcpy(p_config->Capabilities.media2.StreamingCapabilities.RTSPWebSocketUri, "ws:/websocket");
#endif

    strcpy(p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupported, "VideoSource VideoEncoder Metadata");

#ifdef PTZ_SUPPORT
    strcat(p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupported, " PTZ");
#endif
#ifdef AUDIO_SUPPORT
    strcat(p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupported, " AudioSource AudioEncoder");
#ifdef DEVICEIO_SUPPORT
    strcat(p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupported, " AudioOutput AudioDecoder");
#endif
#endif

#ifdef VIDEO_ANALYTICS
    strcat(p_config->Capabilities.media2.ProfileCapabilities.ConfigurationsSupported, " Analytics");
#endif

    p_config->Capabilities.media2.support = 1;
    p_config->Capabilities.media2.Version.Major = 23;
    p_config->Capabilities.media2.Version.Minor = 6;
#endif // end of MEDIA2_SUPPORT

    // event capabilities
    p_config->Capabilities.events.WSSubscriptionPolicySupport = 1;
    p_config->Capabilities.events.WSPullPointSupport = 1;
    p_config->Capabilities.events.WSPausableSubscriptionManagerInterfaceSupport = 0;
    p_config->Capabilities.events.PersistentNotificationStorage = 0;
    p_config->Capabilities.events.support = 1;
    p_config->Capabilities.events.Version.Major = 22;
    p_config->Capabilities.events.Version.Minor = 6;

    p_config->Capabilities.events.MaxNotificationProducers = 10;
    p_config->Capabilities.events.MaxPullPoints = 10;

#ifdef IMAGE_SUPPORT
    // image capabilities
    p_config->Capabilities.image.ImageStabilization = 0;
    p_config->Capabilities.image.Presets = 1;
    p_config->Capabilities.image.AdaptablePreset = 1;
    p_config->Capabilities.image.support = 1;
    p_config->Capabilities.image.Version.Major = 22;
    p_config->Capabilities.image.Version.Minor = 6;
#endif // IMAGE_SUPPORT

#ifdef PTZ_SUPPORT
    // ptz capabilities
    p_config->Capabilities.ptz.EFlip = 1;
    p_config->Capabilities.ptz.Reverse = 1;
    p_config->Capabilities.ptz.GetCompatibleConfigurations = 1;
    p_config->Capabilities.ptz.MoveStatus = 1;
    p_config->Capabilities.ptz.StatusPosition = 1;
    p_config->Capabilities.ptz.support = 1;
    p_config->Capabilities.ptz.Version.Major = 23;
    p_config->Capabilities.ptz.Version.Minor = 6;
#endif // end of PTZ_SUPPORT

#ifdef VIDEO_ANALYTICS
    // analytics capabilities
    p_config->Capabilities.analytics.RuleSupport = 1;
    p_config->Capabilities.analytics.AnalyticsModuleSupport = 1;
    p_config->Capabilities.analytics.CellBasedSceneDescriptionSupported = 1;
    p_config->Capabilities.analytics.RuleOptionsSupported = 1;
    p_config->Capabilities.analytics.AnalyticsModuleOptionsSupported = 1;
    p_config->Capabilities.analytics.SupportedMetadata = 1;
    p_config->Capabilities.analytics.support = 1;
    p_config->Capabilities.analytics.Version.Major = 23;
    p_config->Capabilities.analytics.Version.Minor = 12;
#endif // end of VIDEO_ANALYTICS

#ifdef PROFILE_G_SUPPORT
    // record capabilities
    p_config->Capabilities.recording.ReceiverSource = 0;
    p_config->Capabilities.recording.MediaProfileSource = 1;
    p_config->Capabilities.recording.DynamicRecordings = 1;
    p_config->Capabilities.recording.DynamicTracks = 1;
    p_config->Capabilities.recording.Options = 1;
    p_config->Capabilities.recording.MetadataRecording = 1;
    p_config->Capabilities.recording.JPEG = 1;
#ifdef MPEG4_SUPPORT    
    p_config->Capabilities.recording.MPEG4 = 1;
#endif    
    p_config->Capabilities.recording.H264 = 1;
    p_config->Capabilities.recording.H265 = 1;
#ifdef AUDIO_SUPPORT    
    p_config->Capabilities.recording.G711 = 1;
    p_config->Capabilities.recording.G726 = 1;
    p_config->Capabilities.recording.AAC = 1;
#endif
    p_config->Capabilities.recording.support = 1;
    p_config->Capabilities.recording.Version.Major = 23;
    p_config->Capabilities.recording.Version.Minor = 6;

    p_config->Capabilities.recording.MaxStringLength = 256;
    p_config->Capabilities.recording.MaxRate = 200;
    p_config->Capabilities.recording.MaxTotalRate = 2000;
    p_config->Capabilities.recording.MaxRecordings = 5;
    p_config->Capabilities.recording.MaxRecordingJobs = 5;

    // search capabilities
    p_config->Capabilities.search.MetadataSearch = 1;
    p_config->Capabilities.search.GeneralStartEvents = 1;
    p_config->Capabilities.search.support = 1;
    p_config->Capabilities.search.Version.Major = 22;
    p_config->Capabilities.search.Version.Minor = 6;

    // replay capabilities
    p_config->Capabilities.replay.ReversePlayback = 0;
    p_config->Capabilities.replay.RTP_RTSP_TCP = 1;
    p_config->Capabilities.replay.support = 1;
    p_config->Capabilities.replay.Version.Major = 21;
    p_config->Capabilities.replay.Version.Minor = 12;

    p_config->Capabilities.replay.SessionTimeoutRange.Min = 10;
    p_config->Capabilities.replay.SessionTimeoutRange.Max = 100;
#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT
    // accesscontrol capabilities
    p_config->Capabilities.accesscontrol.support = 1;
    p_config->Capabilities.accesscontrol.Version.Major = 21;
    p_config->Capabilities.accesscontrol.Version.Minor = 6;
    p_config->Capabilities.accesscontrol.MaxLimit = ACCESS_CTRL_MAX_LIMIT;
    p_config->Capabilities.accesscontrol.MaxAccessPoints = ACCESS_CTRL_MAX_LIMIT;
    p_config->Capabilities.accesscontrol.MaxAreas = ACCESS_CTRL_MAX_LIMIT;
    p_config->Capabilities.accesscontrol.ClientSuppliedTokenSupported = 1;
    p_config->Capabilities.accesscontrol.AccessPointManagementSupported = 1;
    p_config->Capabilities.accesscontrol.AreaManagementSupported = 1;

    // doorcontrol capabilities
    p_config->Capabilities.doorcontrol.support = 1;
    p_config->Capabilities.doorcontrol.Version.Major = 21;
    p_config->Capabilities.doorcontrol.Version.Minor = 6;
    p_config->Capabilities.doorcontrol.MaxLimit = DOOR_CTRL_MAX_LIMIT;
    p_config->Capabilities.doorcontrol.MaxDoors = DOOR_CTRL_MAX_LIMIT;
    p_config->Capabilities.doorcontrol.ClientSuppliedTokenSupported = 1;
    p_config->Capabilities.doorcontrol.DoorManagementSupported = 1;
#endif // end of PROFILE_C_SUPPORT

#ifdef DEVICEIO_SUPPORT
    p_vsrc = p_config->v_src;
    while (p_vsrc)
    {
        vsrc++;

        p_vsrc = p_vsrc->next;
    }

    p_vout = p_config->v_output;
    while (p_vout)
    {
        vout++;

        p_vout = p_vout->next;
    }

#ifdef AUDIO_SUPPORT
    p_asrc = p_config->a_src;
    while (p_asrc)
    {
        asrc++;

        p_asrc = p_asrc->next;
    }

    p_aout = p_config->a_output;
    while (p_aout)
    {
        aout++;

        p_aout = p_aout->next;
    }
#endif

    p_relay_output = p_config->relay_output;
    while (p_relay_output)
    {
        relay_output++;

        p_relay_output = p_relay_output->next;
    }

    p_serial_port = p_config->serial_port;
    while (p_serial_port)
    {
        serial_port++;

        p_serial_port = p_serial_port->next;
    }

    p_digital_input = p_config->digit_input;
    while (p_digital_input)
    {
        digit_input++;

        p_digital_input = p_digital_input->next;
    }
    
    // deviceIO capabilities
    p_config->Capabilities.deviceIO.support = 1;
    p_config->Capabilities.deviceIO.Version.Major = 22;
    p_config->Capabilities.deviceIO.Version.Minor = 6;
    p_config->Capabilities.deviceIO.VideoSourcesFlag = 1;
    p_config->Capabilities.deviceIO.VideoSources = vsrc;
    p_config->Capabilities.deviceIO.VideoOutputsFlag = 1;
    p_config->Capabilities.deviceIO.VideoOutputs = vout;
#ifdef AUDIO_SUPPORT    
    p_config->Capabilities.deviceIO.AudioSourcesFlag = 1;
    p_config->Capabilities.deviceIO.AudioSources = asrc;
    p_config->Capabilities.deviceIO.AudioOutputsFlag = 1;
    p_config->Capabilities.deviceIO.AudioOutputs = aout;
#endif    
    p_config->Capabilities.deviceIO.RelayOutputsFlag = 1;
    p_config->Capabilities.deviceIO.RelayOutputs = relay_output;
    p_config->Capabilities.deviceIO.SerialPortsFlag = 1;
    p_config->Capabilities.deviceIO.SerialPorts = serial_port;
    p_config->Capabilities.deviceIO.DigitalInputsFlag = 1;
    p_config->Capabilities.deviceIO.DigitalInputs = digit_input;
    p_config->Capabilities.deviceIO.DigitalInputOptionsFlag = 1;
    p_config->Capabilities.deviceIO.DigitalInputOptions = TRUE;
#endif // end of DEVICEIO_SUPPORT

#ifdef THERMAL_SUPPORT
    // thermal capabilities
    p_config->Capabilities.thermal.support = 1;
    p_config->Capabilities.thermal.Version.Major = 22;
    p_config->Capabilities.thermal.Version.Minor = 6;
    p_config->Capabilities.thermal.Radiometry = 1;
#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT
    // credential capabilities
    p_config->Capabilities.credential.support = 1;
    p_config->Capabilities.credential.Version.Major = 21;
    p_config->Capabilities.credential.Version.Minor = 6;
    p_config->Capabilities.credential.CredentialValiditySupported = 1;
    p_config->Capabilities.credential.CredentialAccessProfileValiditySupported = 1;
    p_config->Capabilities.credential.ValiditySupportsTimeValue = 1;
    p_config->Capabilities.credential.ResetAntipassbackSupported = 1;
    p_config->Capabilities.credential.ClientSuppliedTokenSupported = 1;
    p_config->Capabilities.credential.MaxLimit = CREDENTIAL_MAX_LIMIT;
    p_config->Capabilities.credential.MaxCredentials = CREDENTIAL_MAX_LIMIT;
    p_config->Capabilities.credential.MaxAccessProfilesPerCredential = CREDENTIAL_MAX_LIMIT;

    p_config->Capabilities.credential.sizeSupportedIdentifierType = 3;
    strcpy(p_config->Capabilities.credential.SupportedIdentifierType[0], "pt:Card");
    strcpy(p_config->Capabilities.credential.SupportedIdentifierType[1], "pt:PIN");
    strcpy(p_config->Capabilities.credential.SupportedIdentifierType[2], "pt:Fingerprint");

    strcpy(p_config->Capabilities.credential.DefaultCredentialSuspensionDuration, "PT5M");

    p_config->Capabilities.credential.MaxWhitelistedItems = 0;
    p_config->Capabilities.credential.MaxBlacklistedItems = 0;
    
    p_config->Capabilities.credential.ExtensionFlag = 0;
    p_config->Capabilities.credential.Extension.sizeSupportedExemptionType = 0;
#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES
    // access rules capabilities
    p_config->Capabilities.accessrules.support = 1;
    p_config->Capabilities.accessrules.Version.Major = 19;
    p_config->Capabilities.accessrules.Version.Minor = 6;
    p_config->Capabilities.accessrules.MaxLimit = ACCESSRULES_MAX_LIMIT;
    p_config->Capabilities.accessrules.MaxAccessProfiles = ACCESSRULES_MAX_LIMIT;
    p_config->Capabilities.accessrules.MaxAccessPoliciesPerAccessProfile = 1;
    p_config->Capabilities.accessrules.MultipleSchedulesPerAccessPointSupported = 1;
    p_config->Capabilities.accessrules.ClientSuppliedTokenSupported = 1;
#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT
    p_config->Capabilities.schedule.support = 1;
    p_config->Capabilities.schedule.Version.Major = 18;
    p_config->Capabilities.schedule.Version.Minor = 12;
    p_config->Capabilities.schedule.MaxLimit = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.MaxSchedules = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.MaxTimePeriodsPerDay = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.MaxSpecialDayGroups = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.MaxDaysInSpecialDayGroup = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.MaxSpecialDaysSchedules = SCHEDULE_MAX_LIMIT;
    p_config->Capabilities.schedule.ExtendedRecurrenceSupported = 1;
    p_config->Capabilities.schedule.SpecialDaysSupported = 1;
    p_config->Capabilities.schedule.StateReportingSupported = 0;
    p_config->Capabilities.schedule.ClientSuppliedTokenSupported = 0;
#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT
    p_config->Capabilities.receiver.support = 1;
    p_config->Capabilities.receiver.Version.Major = 21;
    p_config->Capabilities.receiver.Version.Minor = 12;
    p_config->Capabilities.receiver.RTP_USCOREMulticast = 1;
    p_config->Capabilities.receiver.RTP_USCORETCP = 1;
    p_config->Capabilities.receiver.RTP_USCORERTSP_USCORETCP = 1;

    p_config->Capabilities.receiver.SupportedReceivers = 10;
    p_config->Capabilities.receiver.MaximumRTSPURILength = 256;
#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT
    p_config->Capabilities.provisioning.support = 1;
    p_config->Capabilities.provisioning.Version.Major = 18;
    p_config->Capabilities.provisioning.Version.Minor = 12;
    p_config->Capabilities.provisioning.DefaultTimeout = 60;
    p_config->Capabilities.provisioning.sizeSource = 1;

    if (p_config->v_src)
    {
        strcpy(p_config->Capabilities.provisioning.Source[0].VideoSourceToken, p_config->v_src->VideoSource.token);
    }

    p_config->Capabilities.provisioning.Source[0].MaximumPanMovesFlag = 1;
    p_config->Capabilities.provisioning.Source[0].MaximumPanMoves = 60;
    p_config->Capabilities.provisioning.Source[0].MaximumTiltMovesFlag = 1;
    p_config->Capabilities.provisioning.Source[0].MaximumTiltMoves = 60;
    p_config->Capabilities.provisioning.Source[0].MaximumZoomMovesFlag = 1;
    p_config->Capabilities.provisioning.Source[0].MaximumZoomMoves = 60;
    p_config->Capabilities.provisioning.Source[0].MaximumRollMovesFlag = 1;
    p_config->Capabilities.provisioning.Source[0].MaximumRollMoves = 60;
    p_config->Capabilities.provisioning.Source[0].AutoLevelFlag = 1;
    p_config->Capabilities.provisioning.Source[0].AutoLevel = TRUE;
    p_config->Capabilities.provisioning.Source[0].MaximumFocusMovesFlag = 1;
    p_config->Capabilities.provisioning.Source[0].MaximumFocusMoves = 60;
    p_config->Capabilities.provisioning.Source[0].AutoFocusFlag = 1;
    p_config->Capabilities.provisioning.Source[0].AutoFocus = TRUE;
#endif // end of PROVISIONING_SUPPORT

#ifdef SECURITY_SUPPORT
    p_config->Capabilities.security.support = 1;
    p_config->Capabilities.security.KeystoreCapabilities.RSAKeyPairGeneration = 1;
    p_config->Capabilities.security.KeystoreCapabilities.PKCS10 = 1;
    p_config->Capabilities.security.KeystoreCapabilities.PKCS10ExternalCertificationWithRSA = 1;
    p_config->Capabilities.security.KeystoreCapabilities.PKCS12 = 1;
    p_config->Capabilities.security.KeystoreCapabilities.PKCS12CertificateWithRSAPrivateKeyUpload = 1;
    p_config->Capabilities.security.KeystoreCapabilities.SelfSignedCertificateCreation = 1;
    p_config->Capabilities.security.KeystoreCapabilities.SelfSignedCertificateCreationWithRSA = 1;
    p_config->Capabilities.security.KeystoreCapabilities.MaximumNumberOfKeys = 16;
    p_config->Capabilities.security.KeystoreCapabilities.MaximumNumberOfCertificates = 16;
    p_config->Capabilities.security.KeystoreCapabilities.MaximumNumberOfCertificationPaths = 4;
    p_config->Capabilities.security.KeystoreCapabilities.MaximumNumberOfPassphrases = 16;
    p_config->Capabilities.security.KeystoreCapabilities.RSAKeyLengthsFlag = 1;
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.RSAKeyLengths, "1024 2048");
    p_config->Capabilities.security.KeystoreCapabilities.sizeSignatureAlgorithms = 2;
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.SignatureAlgorithms[0].algorithm, "1.2.840.113549.1.1.5");
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.SignatureAlgorithms[1].algorithm, "1.2.840.113549.1.1.11");
    p_config->Capabilities.security.KeystoreCapabilities.PasswordBasedEncryptionAlgorithmsFlag = 1;
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.PasswordBasedEncryptionAlgorithms, "pbeWithSHAAnd3-KeyTripleDES-CBC");
    p_config->Capabilities.security.KeystoreCapabilities.PasswordBasedMACAlgorithmsFlag = 1;
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.PasswordBasedMACAlgorithms, "hmacWithSHA256");
    p_config->Capabilities.security.KeystoreCapabilities.X509VersionsFlag = 1;
    strcpy(p_config->Capabilities.security.KeystoreCapabilities.X509Versions, "1 2 3");
    
    p_config->Capabilities.security.TLSServerCapabilities.MaximumNumberOfTLSCertificationPaths = ARRAY_SIZE(g_onvif_cfg.certpathid);
    p_config->Capabilities.security.TLSServerCapabilities.TLSServerSupportedFlag = 1;
    strcpy(p_config->Capabilities.security.TLSServerCapabilities.TLSServerSupported, "1.0 1.1 1.2 1.3");
    p_config->Capabilities.security.TLSServerCapabilities.EnabledVersionsSupported = 1;
#endif // end of SECURITY_SUPPORT
}

void onvif_init_DeviceInformation()
{
    if (g_onvif_cfg.DeviceInformationFlag)
    {
        return;
    }
    
    strcpy(g_onvif_cfg.DeviceInformation.Manufacturer, "Happytimesoft");
    strcpy(g_onvif_cfg.DeviceInformation.Model, "IPCamera");
    strcpy(g_onvif_cfg.DeviceInformation.FirmwareVersion, "1.0");
    strcpy(g_onvif_cfg.DeviceInformation.SerialNumber, "123456");
    strcpy(g_onvif_cfg.DeviceInformation.HardwareId, "1.0");
}

void onvif_init_SystemDateTime()
{
    g_onvif_cfg.SystemDateTime.TimeZoneFlag = 1;
    
    onvif_get_timezone(g_onvif_cfg.SystemDateTime.TimeZone.TZ, 
        sizeof(g_onvif_cfg.SystemDateTime.TimeZone.TZ),
        &g_onvif_cfg.SystemDateTime.DaylightSavings);

    if (g_onvif_cfg.SystemDateTimeFlag)
    {
        return;
    }
    
    g_onvif_cfg.SystemDateTime.DateTimeType = SetDateTimeType_NTP;
}

void onvif_init_User()
{
    onvif_User user;
    
    if (g_onvif_cfg.UsersFlag)
    {
        return;
    }

    user.fixed = 1;
    user.PasswordFlag = 1;
    user.UserLevel = UserLevel_Administrator;
    strcpy(user.Username, "admin");
    strcpy(user.Password, "admin");
    
    onvif_add_user(&user);

    user.UserLevel = UserLevel_User;
    strcpy(user.Username, "user");
    strcpy(user.Password, "123456");

    onvif_add_user(&user);
}

void onvif_init_Scope()
{
    if (g_onvif_cfg.ScopesFlag)
    {
        return;
    }

    onvif_add_scope("onvif://www.onvif.org/location/country/china", FALSE);
    onvif_add_scope("onvif://www.onvif.org/type/video_encoder", FALSE);
    onvif_add_scope("onvif://www.onvif.org/name/IP-Camera", FALSE);
    onvif_add_scope("onvif://www.onvif.org/hardware/HI3518C", FALSE);
}

BOOL onvif_init_ZeroConfiguration()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    if (p_config->network.interfaces)
    {
        strcpy(p_config->network.ZeroConfiguration.InterfaceToken, 
            p_config->network.interfaces->NetworkInterface.token);
    }
    
    if (p_config->network.ZeroConfiguration.Enabled)
    {
        if (p_config->network.interfaces)
        {
            p_config->network.ZeroConfiguration.sizeAddresses = 1;
            strcpy(p_config->network.ZeroConfiguration.Addresses[0], 
                p_config->network.interfaces->NetworkInterface.IPv4.Config.Address[0].Address);
        }
    }
    else
    {
        p_config->network.ZeroConfiguration.sizeAddresses = 0;
    }

    return TRUE;
}

#if __WINDOWS_OS__

void onvif_add_NetworkInterfaceByAdapter(PIP_ADAPTER_ADDRESSES adapter)
{
    uint32 i;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    NetworkInterfaceList * p_net_inf;
    PIP_ADAPTER_UNICAST_ADDRESS unicast;
            
    p_net_inf = onvif_add_NetworkInterface(&p_config->network.interfaces);
    if (NULL == p_net_inf)
    {
       return;
    }

    p_net_inf->NetworkInterface.Enabled = TRUE;
    p_net_inf->NetworkInterface.InfoFlag = 1;           
    p_net_inf->NetworkInterface.Info.MTUFlag = 1;
    p_net_inf->NetworkInterface.Info.NameFlag = 1;
    p_net_inf->NetworkInterface.IPv4Flag = 1;
    p_net_inf->NetworkInterface.IPv6Flag = p_config->ipv6_enable;

    snprintf(p_net_inf->NetworkInterface.Info.Name, sizeof(p_net_inf->NetworkInterface.Info.Name)-1, 
        "%ws", adapter->FriendlyName);
    
    snprintf(p_net_inf->NetworkInterface.Info.HwAddress, sizeof(p_net_inf->NetworkInterface.Info.HwAddress), 
        "%02X-%02X-%02X-%02X-%02X-%02X", 
        adapter->PhysicalAddress[0], adapter->PhysicalAddress[1], adapter->PhysicalAddress[2], 
        adapter->PhysicalAddress[3], adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);

    p_net_inf->NetworkInterface.Info.MTU = adapter->Mtu;
    p_net_inf->NetworkInterface.IPv4.Enabled = (adapter->Flags & IP_ADAPTER_IPV4_ENABLED);
    p_net_inf->NetworkInterface.IPv6.Enabled = (adapter->Flags & IP_ADAPTER_IPV6_ENABLED);
    p_net_inf->NetworkInterface.IPv4.Config.DHCP = (adapter->Flags & IP_ADAPTER_DHCP_ENABLED);

    if (adapter->Flags & IP_ADAPTER_DHCP_ENABLED)
    {
        p_net_inf->NetworkInterface.IPv6.Config.DHCP = IPv6DHCPConfiguration_Auto;
    }
    else
    {
        p_net_inf->NetworkInterface.IPv6.Config.DHCP = IPv6DHCPConfiguration_Off;
    }

    for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next)
    {
        if (unicast->Address.lpSockaddr->sa_family == AF_INET)
        {
            struct in_addr server_ip;
            struct sockaddr_in * addr = (struct sockaddr_in *)unicast->Address.lpSockaddr;
            onvif_IPv4NetworkInterface * ipv4 = &p_net_inf->NetworkInterface.IPv4;

            if (p_config->server_ip[0] != '\0')
            {
                if (1 == inet_pton(AF_INET, p_config->server_ip, &server_ip))
                {
                    if (memcmp(&server_ip, &addr->sin_addr, sizeof(struct in_addr)) != 0)
                    {
                        continue;
                    }
                }
            }

            i = ipv4->Config.sizeAddress;

            get_sockaddr_ip(unicast->Address.lpSockaddr, ipv4->Config.Address[i].Address, sizeof(ipv4->Config.Address[i].Address));
            ipv4->Config.Address[i].PrefixLength = unicast->OnLinkPrefixLength;

            ipv4->Config.sizeAddress++;
            if (ipv4->Config.sizeAddress >= ARRAY_SIZE(ipv4->Config.Address))
            {
                break;
            }
        }
        else if (unicast->Address.lpSockaddr->sa_family == AF_INET6 && p_config->ipv6_enable)
        {
            struct in6_addr server_ip;
            struct sockaddr_in6 * addr = (struct sockaddr_in6 *)unicast->Address.lpSockaddr;
            onvif_IPv6NetworkInterface * ipv6 = &p_net_inf->NetworkInterface.IPv6;

            if (p_config->server_ip[0] != '\0')
            {
                if (1 == inet_pton(AF_INET6, p_config->server_ip, &server_ip))
                {
                    if (memcmp(&server_ip, &addr->sin6_addr, sizeof(struct in6_addr)) != 0)
                    {
                        continue;
                    }
                }
            }
            
            if (is_ipv6_linklocal_address(&addr->sin6_addr))
            {
                i = ipv6->Config.sizeLinkLocal;

                if (i < ARRAY_SIZE(ipv6->Config.LinkLocal))
                {
                    get_sockaddr_ip(unicast->Address.lpSockaddr, ipv6->Config.LinkLocal[i].Address, sizeof(ipv6->Config.LinkLocal[i].Address));
                    ipv6->Config.LinkLocal[i].PrefixLength = unicast->OnLinkPrefixLength;

                    ipv6->Config.sizeLinkLocal++;
                }
            }

            i = ipv6->Config.sizeAddress;

            get_sockaddr_ip(unicast->Address.lpSockaddr, ipv6->Config.Address[i].Address, sizeof(ipv6->Config.Address[i].Address));
            ipv6->Config.Address[i].PrefixLength = unicast->OnLinkPrefixLength;

            ipv6->Config.sizeAddress++;
            if (ipv6->Config.sizeAddress >= ARRAY_SIZE(ipv6->Config.Address))
            {
                break;
            }
        }
    }
}

#endif

#if __LINUX_OS__

BOOL onvif_get_InterfaceInfo(const char *ifname, int *mtu, char *mac, int mac_len) 
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return FALSE;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    
    // get mtu
    if (ioctl(fd, SIOCGIFMTU, &ifr) == 0)
    {
        *mtu = ifr.ifr_mtu;
    }
    else
    {
        *mtu = 1500;
    }
    
#ifndef IOS
    // get hwaddr
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) 
    {
        snprintf(mac, mac_len, "%02X-%02X-%02X-%02X-%02X-%02X", 
            (uint8)ifr.ifr_hwaddr.sa_data[0], (uint8)ifr.ifr_hwaddr.sa_data[1], (uint8)ifr.ifr_hwaddr.sa_data[2],
            (uint8)ifr.ifr_hwaddr.sa_data[3], (uint8)ifr.ifr_hwaddr.sa_data[4], (uint8)ifr.ifr_hwaddr.sa_data[5]);
    }
    else
#endif
    {
        snprintf(mac, mac_len, "%02X-%02X-%02X-%02X-%02X-%02X",
            rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff);
    }

    return TRUE;
}

void onvif_add_NetworkInterfaceByIfaddr(struct ifaddrs *if_addr)
{
    uint32 i;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    NetworkInterfaceList * p_net_inf;

    p_net_inf = onvif_find_NetworkInterface_by_Name(p_config->network.interfaces, if_addr->ifa_name);
    if (NULL == p_net_inf)
    {
        p_net_inf = onvif_add_NetworkInterface(&p_config->network.interfaces);
    }

    if (NULL == p_net_inf)
    {
       return;
    }

    p_net_inf->NetworkInterface.Enabled = TRUE;
    p_net_inf->NetworkInterface.InfoFlag = 1;           
    p_net_inf->NetworkInterface.Info.MTUFlag = 1;
    p_net_inf->NetworkInterface.Info.NameFlag = 1;
    p_net_inf->NetworkInterface.IPv4Flag = 1;
    p_net_inf->NetworkInterface.IPv6Flag = p_config->ipv6_enable;

    strncpy(p_net_inf->NetworkInterface.Info.Name, if_addr->ifa_name, sizeof(p_net_inf->NetworkInterface.Info.Name)-1);
    p_net_inf->NetworkInterface.Info.MTUFlag = 1;

    if (!onvif_get_InterfaceInfo(if_addr->ifa_name, &p_net_inf->NetworkInterface.Info.MTU, 
            p_net_inf->NetworkInterface.Info.HwAddress, sizeof(p_net_inf->NetworkInterface.Info.HwAddress)))
    {
        p_net_inf->NetworkInterface.Info.MTU = 1500;
    
        snprintf(p_net_inf->NetworkInterface.Info.HwAddress, sizeof(p_net_inf->NetworkInterface.Info.HwAddress), 
            "%02X-%02X-%02X-%02X-%02X-%02X",
            rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff, rand()&0xff);
    }
    
    if (if_addr->ifa_addr->sa_family == AF_INET)
    {
        onvif_IPv4NetworkInterface * ipv4 = &p_net_inf->NetworkInterface.IPv4;
        
        ipv4->Enabled = 1;
        ipv4->Config.DHCP = 0;
        
        if (ipv4->Config.sizeAddress < ARRAY_SIZE(ipv4->Config.Address))
        {
            i = ipv4->Config.sizeAddress;
            
            get_sockaddr_ip(if_addr->ifa_addr, ipv4->Config.Address[i].Address, sizeof(ipv4->Config.Address[i].Address));
            
            if (if_addr->ifa_netmask != NULL)
            {
                ipv4->Config.Address[i].PrefixLength = get_mask_length_by_sockaddr(if_addr->ifa_netmask);
            }

            ipv4->Config.sizeAddress++;
        }
    }
    else if (if_addr->ifa_addr->sa_family == AF_INET6 && p_config->ipv6_enable)
    {
        struct sockaddr_in6 * addr = (struct sockaddr_in6 *)if_addr->ifa_addr;
        onvif_IPv6NetworkInterface * ipv6 = &p_net_inf->NetworkInterface.IPv6;
        
        ipv6->Enabled = 1;
        ipv6->Config.DHCP = IPv6DHCPConfiguration_Auto;
        
        if (is_ipv6_linklocal_address(&addr->sin6_addr))
        {
            i = ipv6->Config.sizeLinkLocal;

            if (i < ARRAY_SIZE(ipv6->Config.LinkLocal))
            {
                get_sockaddr_ip(if_addr->ifa_addr, ipv6->Config.LinkLocal[i].Address, sizeof(ipv6->Config.LinkLocal[i].Address));

                if (if_addr->ifa_netmask != NULL)
                {
                    ipv6->Config.LinkLocal[i].PrefixLength = get_mask_length_by_sockaddr(if_addr->ifa_netmask);
                }

                ipv6->Config.sizeLinkLocal++;
            }
        }
        
        if (ipv6->Config.sizeAddress < ARRAY_SIZE(ipv6->Config.Address))
        {
            i = ipv6->Config.sizeAddress;
            
            get_sockaddr_ip(if_addr->ifa_addr, ipv6->Config.Address[i].Address, sizeof(ipv6->Config.Address[i].Address));
            
            if (if_addr->ifa_netmask != NULL)
            {
                ipv6->Config.Address[i].PrefixLength = get_mask_length_by_sockaddr(if_addr->ifa_netmask);
            }

            ipv6->Config.sizeAddress++;
        }
    }
}

#endif

void onvif_init_NetworkInterface()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;

#if __WINDOWS_OS__

    BOOL found = FALSE;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;
    DWORD retVal;
    
    adapterAddresses = (PIP_ADAPTER_ADDRESSES) malloc(bufLen);
    if (NULL == adapterAddresses)
    {
        return;
    }

    retVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapterAddresses, &bufLen);
    if (retVal == ERROR_BUFFER_OVERFLOW) 
    {
        free(adapterAddresses);
        
        adapterAddresses = (PIP_ADAPTER_ADDRESSES) malloc(bufLen);
        if (NULL == adapterAddresses)
        {
            return;
        }

        retVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapterAddresses, &bufLen);
    }

    if (NO_ERROR == retVal)
    {
        PIP_ADAPTER_ADDRESSES adapter = adapterAddresses;
        
        while (adapter)
        {
            PIP_ADAPTER_UNICAST_ADDRESS unicast;
            
            if (adapter->IfType & IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_PROP_VIRTUAL)
            {
                adapter = adapter->Next;
                continue;
            }

            for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next)
            {
                if (unicast->Address.lpSockaddr->sa_family != AF_INET)
                {
                    if (!p_config->ipv6_enable || unicast->Address.lpSockaddr->sa_family != AF_INET6)
                    {
                        continue;
                    }
                }
                
                if (p_config->server_ip[0] != '\0')
                {
                    if (unicast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        struct in_addr server_ip;
                        struct sockaddr_in * addr = (struct sockaddr_in *)unicast->Address.lpSockaddr;
            
                        if (1 == inet_pton(AF_INET, p_config->server_ip, &server_ip))
                        {
                            if (memcmp(&server_ip, &addr->sin_addr, sizeof(struct in_addr)) == 0)
                            {
                                onvif_add_NetworkInterfaceByAdapter(adapter);
                                goto EXIT;
                            }
                        }
                    }
                    else if (unicast->Address.lpSockaddr->sa_family == AF_INET6)
                    {
                        struct in6_addr server_ip;
                        struct sockaddr_in6 * addr = (struct sockaddr_in6 *)unicast->Address.lpSockaddr;
            
                        if (1 == inet_pton(AF_INET6, p_config->server_ip, &server_ip))
                        {
                            if (memcmp(&server_ip, &addr->sin6_addr, sizeof(struct in6_addr)) == 0)
                            {
                                onvif_add_NetworkInterfaceByAdapter(adapter);
                                goto EXIT;
                            }
                        }
                    }
                }

                if (is_local_address(unicast->Address.lpSockaddr))
                {
                    goto NEXT;
                }
            }

            if (p_config->server_ip[0] != '\0' && !found)
            {
                // Only report the configured IP address, if <server_ip> is configured
                goto NEXT;
            }

            onvif_add_NetworkInterfaceByAdapter(adapter);
            
NEXT:       
            adapter = adapter->Next;
        }
    }

EXIT:
    free(adapterAddresses);
    
#elif __LINUX_OS__

    struct ifaddrs *if_addr;
    struct ifaddrs *if_addrs = NULL;

    if (getifaddrs(&if_addrs) != 0)
    {
        return;
    }
    
    for (if_addr = if_addrs; if_addr != NULL; if_addr = if_addr->ifa_next)
    {
        if (NULL == if_addr->ifa_addr)
        {
            continue;
        }
        
        if (if_addr->ifa_flags & IFF_LOOPBACK) // loopback interface
        {
            continue;
        }

        if (if_addr->ifa_addr->sa_family != AF_INET)
        {
            if (!p_config->ipv6_enable || if_addr->ifa_addr->sa_family != AF_INET6)
            {
                continue;
            }
        }

        if (p_config->server_ip[0] != '\0')
        {
            // Only report the configured IP address, if <server_ip> is configured
            
            if (if_addr->ifa_addr->sa_family == AF_INET)
            {
                struct in_addr server_ip;
                struct sockaddr_in * addr = (struct sockaddr_in *) if_addr->ifa_addr;
    
                if (1 == inet_pton(AF_INET, p_config->server_ip, &server_ip))
                {
                    if (memcmp(&server_ip, &addr->sin_addr, sizeof(struct in_addr)) == 0)
                    {
                        onvif_add_NetworkInterfaceByIfaddr(if_addr);
                        break;
                    }
                }
            }
            else if (if_addr->ifa_addr->sa_family == AF_INET6)
            {
                struct in6_addr server_ip;
                struct sockaddr_in6 * addr = (struct sockaddr_in6 *) if_addr->ifa_addr;
    
                if (1 == inet_pton(AF_INET6, p_config->server_ip, &server_ip))
                {
                    if (memcmp(&server_ip, &addr->sin6_addr, sizeof(struct in6_addr)) == 0)
                    {
                        onvif_add_NetworkInterfaceByIfaddr(if_addr);
                        break;
                    }
                }
            }

            continue;
        }

        if (is_local_address(if_addr->ifa_addr))
        {
            continue;
        }

        onvif_add_NetworkInterfaceByIfaddr(if_addr);
    }
    
    freeifaddrs(if_addrs);
#endif
}

void onvif_init_net()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    ONVIF_NET * p_network = &g_onvif_cfg.network;

    if (!p_network->DiscoveryModeFlag)
    {
        p_network->DiscoveryMode = DiscoveryMode_Discoverable;
    }
    
    if (!p_network->HostnameInformationFlag)
    {
        p_network->HostnameInformation.FromDHCP = FALSE;
        p_network->HostnameInformation.RebootNeeded = FALSE;
    }

    // init host name
    p_network->HostnameInformation.NameFlag = 1;
    gethostname(p_network->HostnameInformation.Name, sizeof(p_network->HostnameInformation.Name));

    if (!p_network->DNSInformationFlag)
    {
        const char * dns;
        
        // init dns setting
        p_network->DNSInformation.SearchDomainFlag = 1;
        p_network->DNSInformation.FromDHCP = FALSE;
        
        dns = get_dns_server();
        if (dns && strlen(dns) > 0)
        {
            strncpy(p_network->DNSInformation.DNSServer[0], dns, sizeof(p_network->DNSInformation.DNSServer[0])-1);
        }
        else
        {
            strcpy(p_network->DNSInformation.DNSServer[0], "192.168.1.1");
        }
    }

    if (!p_network->NTPInformationFlag)
    {
        // init ntp settting
        p_network->NTPInformation.FromDHCP = FALSE;
        strcpy(p_network->NTPInformation.NTPServer[0], "time.windows.com");
    }
    
    if (!p_network->NetworkProtocolFlag)
    {
        // init network protocol
        p_network->NetworkProtocol.HTTPFlag = 1;
        p_network->NetworkProtocol.HTTPEnabled = p_config->http_enable;
#ifdef HTTPS    
        p_network->NetworkProtocol.HTTPSFlag = 1;
        p_network->NetworkProtocol.HTTPSEnabled = p_config->https_enable;
#endif
        p_network->NetworkProtocol.RTSPFlag = 1;
        p_network->NetworkProtocol.RTSPEnabled = 1;
        
        p_network->NetworkProtocol.HTTPPort[0] = p_config->http_port;
#ifdef HTTPS        
        p_network->NetworkProtocol.HTTPSPort[0] = p_config->https_port;
#endif
        p_network->NetworkProtocol.RTSPPort[0] = 554;
    }

    if (!p_network->NetworkGatewayFlag)
    {
        const char * gw;
        
        // init default gateway
        gw = get_default_gateway();
        if (gw && strlen(gw) > 0)
        {
            strncpy(p_network->NetworkGateway.IPv4Address[0], gw, sizeof(p_network->NetworkGateway.IPv4Address[0])-1);
        }
        else
        {
            strcpy(p_network->NetworkGateway.IPv4Address[0], "192.168.1.1");
        }
    }
    
    // init network interface
    onvif_init_NetworkInterface();

    onvif_init_ZeroConfiguration();
}

HT_API void onvif_init_def_cfg()
{
    memset(&g_onvif_cfg, 0, sizeof(ONVIF_CFG));
    memset(&g_onvif_cls, 0, sizeof(ONVIF_CLS));
    memset(&g_onvif_idx, 0, sizeof(ONVIF_IDX));

    g_onvif_cfg.log_enable = 1;
    g_onvif_cfg.log_level = HT_LOG_ERR;
    g_onvif_cfg.http_enable = 1;
    g_onvif_cfg.http_port = 8000;
    g_onvif_cfg.evt_sim_flag = 1;
    g_onvif_cfg.evt_renew_time = 60;
    g_onvif_cfg.http_max_users = 16;
    g_onvif_cfg.md5_hashing = 1;
    g_onvif_cfg.sha256_hashing = 0;
    strcpy(g_onvif_cfg.snapshot, "snapshot.jpg");
}

HT_API void onvif_init_cfg()
{
    char uuid[100] = {'\0'};
    
    if (g_onvif_cfg.EndpointReference[0] == 0)
    {
        strncpy(g_onvif_cfg.EndpointReference, onvif_uuid_create(uuid, sizeof(uuid)), 
            sizeof(g_onvif_cfg.EndpointReference)-1);
    }

    onvif_init_DeviceInformation();
    onvif_init_SystemDateTime();
    onvif_init_User();
    onvif_init_Scope();

#if defined(MEDIA_SUPPORT) || defined(MEDIA2_SUPPORT)

    onvif_init_VideoSource();
    onvif_init_VideoSourceConfiguration();
    onvif_init_VideoEncoderConfiguration();

#ifdef MEDIA2_SUPPORT
    onvif_init_MaskOptions();
#endif

#ifdef AUDIO_SUPPORT
    onvif_init_AudioSource();
    onvif_init_AudioSourceConfiguration();
    onvif_init_AudioEncoderConfiguration();
    onvif_init_AudioDecoderConfigurations();
#endif

    onvif_init_MetadataConfiguration();
    onvif_init_MetadataConfigurationOptions();

    onvif_init_OSDConfigurations();
    onvif_init_OSDConfigurationOptions();

#ifdef VIDEO_ANALYTICS
    onvif_init_VideoAnalyticsConfiguration();
    if (g_onvif_cfg.va_cfg)
    {
        onvif_init_SupportedRules(&g_onvif_cfg.va_cfg->SupportedRules);
        onvif_init_SupportedAnalyticsModules(&g_onvif_cfg.va_cfg->SupportedAnalyticsModules);
    }
#endif

#endif // defined(MEDIA_SUPPORT) || defined(MEDIA2_SUPPORT)

#ifdef PTZ_SUPPORT
    onvif_init_PTZNode();
    onvif_init_PTZConfiguration();
#endif

    onvif_init_profile();

#ifdef PROFILE_C_SUPPORT    
    onvif_init_DoorList();
    onvif_init_AreaList();
    onvif_init_AccessPointList();
#endif

#ifdef DEVICEIO_SUPPORT
#ifdef AUDIO_SUPPORT
    // If do not support rtsp back channel, you can comment it out
    onvif_init_AudioOutput();
    onvif_init_AudioOutputConfiguration(g_onvif_cfg.a_output);
#endif    
    onvif_init_RelayOutput();
    onvif_init_DigitInput();
    onvif_init_SerialPort();
#endif

#ifdef PROFILE_G_SUPPORT
    onvif_init_Recording();
    onvif_init_RecordingJob();

    g_onvif_cfg.replay_session_timeout = 60;
#endif

#ifdef ACCESS_RULES
    onvif_init_AccessProfile();
#endif

#ifdef CREDENTIAL_SUPPORT
    onvif_init_Credential();
#endif

#ifdef SCHEDULE_SUPPORT
    onvif_init_Schedule();
#endif

#ifdef SECURITY_SUPPORT
    onvif_init_Security();
#endif
}

void onvif_chk_server_cfg()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    ONVIF_CLS * p_class = &g_onvif_cls;

    if (p_config->http_enable)
    {
        p_class->http_port = p_config->http_port;

        if (p_class->http_port <= 0 || p_class->http_port > 65535) 
        {
            p_class->http_port = 8000;
        }
    }

#ifdef HTTPS
    if (p_config->https_enable)
    {
        p_class->https_port = p_config->https_port;

        if (p_class->https_port <= 0 || p_class->https_port > 65535) 
        {
            p_class->https_port = 8443;
        }
    }
#endif

    if (p_config->evt_renew_time < 60)
    {
        p_config->evt_renew_time = 60;
    }
}

HT_API void onvif_init()
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    ONVIF_PROFILE * p_profile;

    onvif_chk_server_cfg();
    
    onvif_init_net();    

    onvif_eua_init();
    
    p_profile = p_config->profiles;
    while (p_profile)
    {
#ifdef PTZ_SUPPORT    
        if (NULL == p_profile->ptz_cfg && p_config->ptz_cfg)
        {
            // add PTZ configuration to profile
            p_profile->ptz_cfg = p_config->ptz_cfg;
            p_profile->ptz_cfg->Configuration.UseCount++;
        }
#endif

        if (NULL == p_profile->metadata_cfg && p_config->metadata_cfg)
        {
            // add metadata configuration to profile
            p_profile->metadata_cfg = p_config->metadata_cfg;
            p_profile->metadata_cfg->Configuration.UseCount++;
        }

#ifdef VIDEO_ANALYTICS
        if (NULL == p_profile->va_cfg && p_config->va_cfg)
        {
            // add video analytics configuration to profile
            p_profile->va_cfg = p_config->va_cfg;
            p_profile->va_cfg->Configuration.UseCount++;
        }
#endif

        p_profile = p_profile->next;
    }

    onvif_init_capabilities();
}




