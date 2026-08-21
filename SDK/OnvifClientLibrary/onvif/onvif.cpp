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
#include "onvif_api.h"


HT_API void onvif_free_device(ONVIF_DEVICE * p_dev)
{
    if (NULL == p_dev)
    {
        return;
    }

    if (p_dev->events.subscribe || p_dev->events.pullpoint)
    {
        Unsubscribe(p_dev);
    }
    
    onvif_free_VideoSources(&p_dev->v_src);
    onvif_free_AudioSources(&p_dev->a_src);
    onvif_free_profiles(&p_dev->profiles);
    onvif_free_MediaProfiles(&p_dev->media_profiles);
    onvif_free_VideoSourceConfigurations(&p_dev->v_src_cfg);
    onvif_free_AudioSourceConfigurations(&p_dev->a_src_cfg);
    onvif_free_VideoEncoderConfigurations(&p_dev->v_enc_cfg);
    onvif_free_AudioEncoderConfigurations(&p_dev->a_enc_cfg);
    onvif_free_PTZNodes(&p_dev->ptz_node);
    onvif_free_PTZConfigurations(&p_dev->ptz_cfg);    

    onvif_free_NotificationMessages(&p_dev->events.notify);
}

HT_API UserList * onvif_add_User(UserList ** p_head)
{
    UserList * p_tmp;
    UserList * p_new = (UserList *) malloc(sizeof(UserList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(UserList));

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

HT_API void onvif_free_Users(UserList ** p_head)
{
    UserList * p_next;
    UserList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

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

HT_API StorageConfigurationList * onvif_add_StorageConfiguration(StorageConfigurationList ** p_head)
{
    StorageConfigurationList * p_tmp;
    StorageConfigurationList * p_new = (StorageConfigurationList *) malloc(sizeof(StorageConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(StorageConfigurationList));

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

HT_API ONVIF_PROFILE * onvif_add_profile(ONVIF_PROFILE ** p_head)
{
    ONVIF_PROFILE * p_tmp;
    ONVIF_PROFILE * p_new = (ONVIF_PROFILE *) malloc(sizeof(ONVIF_PROFILE));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ONVIF_PROFILE));

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

HT_API void onvif_free_profile(ONVIF_PROFILE * p_profile)
{
    if (NULL == p_profile)
    {
        return;
    }
    
    if (p_profile->v_src_cfg)
    {
        onvif_free_VideoSourceConfigurations(&p_profile->v_src_cfg);
    }

    if (p_profile->v_enc_cfg)
    {
        onvif_free_VideoEncoderConfigurations(&p_profile->v_enc_cfg);
    }

    if (p_profile->a_src_cfg)
    {
        onvif_free_AudioSourceConfigurations(&p_profile->a_src_cfg);
    }

    if (p_profile->a_enc_cfg)
    {
        onvif_free_AudioEncoderConfigurations(&p_profile->a_enc_cfg);
    }

    if (p_profile->ptz_cfg)
    {
        onvif_free_PTZConfigurations(&p_profile->ptz_cfg);
    }

    if (p_profile->va_cfg)
    {
        onvif_free_VideoAnalyticsConfigurations(&p_profile->va_cfg);
    }

    if (p_profile->metadata_cfg)
    {
        onvif_free_MetadataConfigurations(&p_profile->metadata_cfg);
    }
}

HT_API void onvif_free_profiles(ONVIF_PROFILE ** p_head)
{
    ONVIF_PROFILE * p_next;
    ONVIF_PROFILE * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_profile(p_tmp);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API MediaProfileList * onvif_add_MediaProfile(MediaProfileList ** p_head)
{
    MediaProfileList * p_tmp;
    MediaProfileList * p_new = (MediaProfileList *) malloc(sizeof(MediaProfileList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(MediaProfileList));

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

HT_API MediaProfileList * onvif_find_MediaProfile(MediaProfileList * p_head, const char * token)
{
    MediaProfileList * p_tmp = p_head;
    
    while (p_tmp)
    {
        if (strcmp(p_tmp->MediaProfile.token, token) == 0)
        {
            break;
        }
        
        p_tmp = p_tmp->next;
    }

    return p_tmp;
}

HT_API void onvif_free_MediaProfiles(MediaProfileList ** p_head)
{
    MediaProfileList * p_next;
    MediaProfileList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->MediaProfile.Configurations.AnalyticsFlag)
        {            
            onvif_free_Configs(&p_tmp->MediaProfile.Configurations.Analytics.AnalyticsEngineConfiguration.AnalyticsModule);
            onvif_free_Configs(&p_tmp->MediaProfile.Configurations.Analytics.RuleEngineConfiguration.Rule);
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API VideoSourceList * onvif_add_VideoSource(VideoSourceList ** p_head)
{
    VideoSourceList * p_tmp;
    VideoSourceList * p_new = (VideoSourceList *) malloc(sizeof(VideoSourceList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoSourceList));

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

HT_API void onvif_free_VideoSources(VideoSourceList ** p_head)
{
    VideoSourceList * p_next;
    VideoSourceList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API VideoSourceList * onvif_get_cur_VideoSource(ONVIF_DEVICE * p_dev)
{
    VideoSourceList * p_v_src;
    ONVIF_PROFILE * p_profile = p_dev->curProfile;    
    if (NULL == p_profile)
    {
        p_profile = p_dev->profiles;
    }
    
    if (NULL == p_profile || NULL == p_profile->v_src_cfg)
    {
        return NULL;
    }

    p_v_src = onvif_find_VideoSource(p_dev->v_src, p_profile->v_src_cfg->Configuration.SourceToken);
    if (NULL == p_v_src)
    {
        return NULL;
    }

    return p_v_src;
}

HT_API VideoSourceModeList * onvif_add_VideoSourceMode(VideoSourceModeList ** p_head)
{
    VideoSourceModeList * p_tmp;
    VideoSourceModeList * p_new = (VideoSourceModeList *) malloc(sizeof(VideoSourceModeList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoSourceModeList));

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

HT_API void onvif_free_VideoSourceModes(VideoSourceModeList ** p_new)
{
    VideoSourceModeList * p_next;
    VideoSourceModeList * p_tmp = *p_new;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_new = NULL;
}

HT_API VideoSourceConfigurationList * onvif_add_VideoSourceConfiguration(VideoSourceConfigurationList ** p_head)
{
    VideoSourceConfigurationList * p_tmp;
    VideoSourceConfigurationList * p_new = (VideoSourceConfigurationList *) malloc(sizeof(VideoSourceConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoSourceConfigurationList));

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

HT_API VideoEncoder2ConfigurationList * onvif_add_VideoEncoder2Configuration(VideoEncoder2ConfigurationList ** p_head)
{
    VideoEncoder2ConfigurationList * p_tmp;
    VideoEncoder2ConfigurationList * p_new = (VideoEncoder2ConfigurationList *) malloc(sizeof(VideoEncoder2ConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoEncoder2ConfigurationList));

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

HT_API void onvif_free_VideoEncoder2Configurations(VideoEncoder2ConfigurationList ** p_head)
{
    VideoEncoder2ConfigurationList * p_next;
    VideoEncoder2ConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
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

HT_API Dot11AvailableNetworksList * onvif_add_Dot11AvailableNetworks(Dot11AvailableNetworksList ** p_head)
{
    Dot11AvailableNetworksList * p_tmp;
    Dot11AvailableNetworksList * p_new = (Dot11AvailableNetworksList *) malloc(sizeof(Dot11AvailableNetworksList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(Dot11AvailableNetworksList));

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

HT_API void onvif_free_Dot11AvailableNetworks(Dot11AvailableNetworksList ** p_head)
{
    Dot11AvailableNetworksList * p_next;
    Dot11AvailableNetworksList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
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

HT_API MetadataConfigurationList * onvif_add_MetadataConfiguration(MetadataConfigurationList ** p_head)
{
    MetadataConfigurationList * p_tmp;
    MetadataConfigurationList * p_new = (MetadataConfigurationList *) malloc(sizeof(MetadataConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(MetadataConfigurationList));

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

        onvif_free_Configs(&p_tmp->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API VideoEncoderConfigurationList * onvif_add_VideoEncoderConfiguration(VideoEncoderConfigurationList ** p_head)
{
    VideoEncoderConfigurationList * p_tmp;
    VideoEncoderConfigurationList * p_new = (VideoEncoderConfigurationList *) malloc(sizeof(VideoEncoderConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoEncoderConfigurationList));

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

HT_API VideoEncoderConfigurationList * onvif_find_VideoEncoderConfiguration(VideoEncoderConfigurationList * p_head, const char * token)
{
    VideoEncoderConfigurationList * p_tmp = p_head;
    
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

HT_API void onvif_free_VideoEncoderConfigurations(VideoEncoderConfigurationList ** p_head)
{
    VideoEncoderConfigurationList * p_next;
    VideoEncoderConfigurationList * p_tmp = *p_head;

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

    return p_new;
}

HT_API void onvif_free_NotificationMessage(NotificationMessageList * p_message)
{
    if (p_message)
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

/***
 * get notify list item numbers
 */
HT_API int onvif_get_NotificationMessages_nums(NotificationMessageList * p_head)
{
    int nums = 0;
    NotificationMessageList * p_tmp = p_head;

    while (p_tmp)
    {
        ++nums;
        p_tmp = p_tmp->next;
    }

    return nums;
}

/***
 * add notify list to onvif device event notify list at last
 */
HT_API void onvif_device_add_NotificationMessages(ONVIF_DEVICE * p_dev, NotificationMessageList * p_notify)
{
    if (NULL == p_dev->events.notify)
    {        
        p_dev->events.notify = p_notify;
    }
    else
    {
        NotificationMessageList * p_tmp = p_dev->events.notify;

        while (p_tmp && p_tmp->next)
        {
            p_tmp = p_tmp->next;
        }

        p_tmp->next = p_notify;
    }    
}

/***
 * free nums event notify item from onvif device event notify list at first
 *
 * return the freed notify item numbers
 */
HT_API int onvif_device_free_NotificationMessages(ONVIF_DEVICE * p_dev, int nums)
{
    int freed_nums = 0;

    NotificationMessageList * p_next;
    NotificationMessageList * p_notify = p_dev->events.notify;
    while (p_notify && freed_nums < nums)
    {
        p_next = p_notify->next;
        
        onvif_free_NotificationMessage(p_notify);

        ++freed_nums;
        p_notify = p_next;
    }

    p_dev->events.notify = p_notify;

    return freed_nums;
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

HT_API const char * onvif_format_SimpleItem(SimpleItemList * p_item)
{
    int offset = 0;
    int mlen = 1024;
    static char str[1024];
    SimpleItemList * p_tmp;

    memset(str, 0, sizeof(str));
    
    p_tmp = p_item;
    while (p_tmp)
    {
        offset += snprintf(str+offset, mlen-offset, "%s:%s\r\n", p_tmp->SimpleItem.Name, p_tmp->SimpleItem.Value);
        
        p_tmp = p_tmp->next;
    }

    return str;
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

HT_API ImagingPresetList * onvif_add_ImagingPreset(ImagingPresetList ** p_head)
{
    ImagingPresetList * p_tmp;
    ImagingPresetList * p_new = (ImagingPresetList *) malloc(sizeof(ImagingPresetList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ImagingPresetList));

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

HT_API AudioSourceList * onvif_add_AudioSource(AudioSourceList ** p_head)
{
    AudioSourceList * p_tmp;
    AudioSourceList * p_new = (AudioSourceList *) malloc(sizeof(AudioSourceList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioSourceList));

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

HT_API AudioSourceConfigurationList * onvif_add_AudioSourceConfiguration(AudioSourceConfigurationList ** p_head)
{
    AudioSourceConfigurationList * p_tmp;
    AudioSourceConfigurationList * p_new = (AudioSourceConfigurationList *) malloc(sizeof(AudioSourceConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioSourceConfigurationList));

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

HT_API AudioEncoderConfigurationList * onvif_add_AudioEncoderConfiguration(AudioEncoderConfigurationList ** p_head)
{
    AudioEncoderConfigurationList * p_tmp;
    AudioEncoderConfigurationList * p_new = (AudioEncoderConfigurationList *) malloc(sizeof(AudioEncoderConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioEncoderConfigurationList));

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

HT_API AudioEncoderConfigurationList * onvif_find_AudioEncoderConfiguration(AudioEncoderConfigurationList * p_head, const char * token)
{
    AudioEncoderConfigurationList * p_tmp = p_head;
    
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

HT_API void onvif_free_AudioEncoderConfigurations(AudioEncoderConfigurationList ** p_head)
{
    AudioEncoderConfigurationList * p_next;
    AudioEncoderConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API AudioEncoder2ConfigurationList * onvif_add_AudioEncoder2Configuration(AudioEncoder2ConfigurationList ** p_head)
{
    AudioEncoder2ConfigurationList * p_tmp;
    AudioEncoder2ConfigurationList * p_new = (AudioEncoder2ConfigurationList *) malloc(sizeof(AudioEncoder2ConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioEncoder2ConfigurationList));

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

HT_API void onvif_free_AudioEncoder2Configurations(AudioEncoder2ConfigurationList ** p_head)
{
    AudioEncoder2ConfigurationList * p_next;
    AudioEncoder2ConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

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

HT_API AudioDecoderConfigurationList * onvif_add_AudioDecoderConfiguration(AudioDecoderConfigurationList ** p_head)
{
    AudioDecoderConfigurationList * p_tmp;
    AudioDecoderConfigurationList * p_new = (AudioDecoderConfigurationList *) malloc(sizeof(AudioDecoderConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioDecoderConfigurationList));

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

HT_API void onvif_free_AudioDecoderConfigurations(AudioDecoderConfigurationList ** p_head)
{
    AudioDecoderConfigurationList * p_next;
    AudioDecoderConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
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

HT_API PTZNodeList * onvif_add_PTZNode(PTZNodeList ** p_head)
{
    PTZNodeList * p_tmp;
    PTZNodeList * p_new = (PTZNodeList *) malloc(sizeof(PTZNodeList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZNodeList));

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

HT_API PTZConfigurationList * onvif_add_PTZConfiguration(PTZConfigurationList ** p_head)
{
    PTZConfigurationList * p_tmp;
    PTZConfigurationList * p_new = (PTZConfigurationList *) malloc(sizeof(PTZConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZConfigurationList));

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

HT_API PTZPresetList * onvif_add_PTZPreset(PTZPresetList ** p_head)
{
    PTZPresetList * p_tmp;
    PTZPresetList * p_new = (PTZPresetList *) malloc(sizeof(PTZPresetList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PTZPresetList));

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

HT_API PresetTourList * onvif_add_PresetTour(PresetTourList ** p_head)
{
    PresetTourList * p_tmp;
    PresetTourList * p_new = (PresetTourList *) malloc(sizeof(PresetTourList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(PresetTourList));

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

HT_API void onvif_free_Config(ConfigList * p_head)
{
    onvif_free_SimpleItems(&p_head->Config.Parameters.SimpleItem);
    onvif_free_ElementItems(&p_head->Config.Parameters.ElementItem);
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

HT_API VideoAnalyticsConfigurationList * onvif_add_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList ** p_head)
{
    VideoAnalyticsConfigurationList * p_tmp;
    VideoAnalyticsConfigurationList * p_new = (VideoAnalyticsConfigurationList *) malloc(sizeof(VideoAnalyticsConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(VideoAnalyticsConfigurationList));

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

HT_API void onvif_free_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList * p_head)
{
    onvif_free_Configs(&p_head->rules);
    onvif_free_Configs(&p_head->modules);

    onvif_free_ConfigDescriptions(&p_head->SupportedRules.RuleDescription);
    
    onvif_free_Configs(&p_head->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
    onvif_free_Configs(&p_head->Configuration.RuleEngineConfiguration.Rule);
}

HT_API void onvif_free_VideoAnalyticsConfigurations(VideoAnalyticsConfigurationList ** p_head)
{
    VideoAnalyticsConfigurationList * p_next;
    VideoAnalyticsConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        onvif_free_VideoAnalyticsConfiguration(p_tmp);
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API AnalyticsModuleConfigOptionsList * onvif_add_AnalyticsModuleConfigOptions(AnalyticsModuleConfigOptionsList ** p_head)
{
    AnalyticsModuleConfigOptionsList * p_tmp;
    AnalyticsModuleConfigOptionsList * p_new = (AnalyticsModuleConfigOptionsList *) malloc(sizeof(AnalyticsModuleConfigOptionsList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AnalyticsModuleConfigOptionsList));

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

HT_API void onvif_free_AnalyticsModuleConfigOptions(AnalyticsModuleConfigOptionsList ** p_head)
{
    AnalyticsModuleConfigOptionsList * p_next;
    AnalyticsModuleConfigOptionsList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

HT_API MetadataInfoList * onvif_add_MetadataInfo(MetadataInfoList ** p_head)
{
    MetadataInfoList * p_tmp;
    MetadataInfoList * p_new = (MetadataInfoList *) malloc(sizeof(MetadataInfoList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(MetadataInfoList));

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

HT_API void onvif_free_MetadataInfo(MetadataInfoList ** p_head)
{
    MetadataInfoList * p_next;
    MetadataInfoList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;

        if (p_tmp->MetadataInfo.Frame)
        {
            free(p_tmp->MetadataInfo.Frame);
        }
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
}

#ifdef PROFILE_G_SUPPORT

HT_API RecordingList * onvif_add_Recording(RecordingList ** p_head)
{
    RecordingList * p_tmp;
    RecordingList * p_new = (RecordingList *) malloc(sizeof(RecordingList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RecordingList));

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

HT_API TrackList * onvif_add_Track(TrackList ** p_head)
{
    TrackList * p_tmp;
    TrackList * p_new = (TrackList *) malloc(sizeof(TrackList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(TrackList));

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

HT_API RecordingJobList * onvif_add_RecordingJob(RecordingJobList ** p_head)
{
    RecordingJobList * p_tmp;
    RecordingJobList * p_new = (RecordingJobList *) malloc(sizeof(RecordingJobList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RecordingJobList));

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

HT_API TrackAttributesList * onvif_add_TrackAttributes(TrackAttributesList ** p_head)
{
    TrackAttributesList * p_tmp;
    TrackAttributesList * p_new = (TrackAttributesList *) malloc(sizeof(TrackAttributesList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(TrackAttributesList));

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

HT_API void onvif_free_TrackAttributes(TrackAttributesList ** p_head)
{
    TrackAttributesList * p_next;
    TrackAttributesList * p_tmp = *p_head;

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

#endif    // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

HT_API AccessPointList * onvif_add_AccessPoint(AccessPointList ** p_head)
{
    AccessPointList * p_tmp;
    AccessPointList * p_new = (AccessPointList *) malloc(sizeof(AccessPointList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AccessPointList));

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

HT_API DoorList * onvif_add_Door(DoorList ** p_head)
{
    DoorList * p_tmp;
    DoorList * p_new = (DoorList *) malloc(sizeof(DoorList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(DoorList));

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

HT_API AreaList * onvif_add_Area(AreaList ** p_head)
{
    AreaList * p_tmp;
    AreaList * p_new = (AreaList *) malloc(sizeof(AreaList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AreaList));

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

#endif // end of PROFILE_C_SUPPORT

#ifdef DEVICEIO_SUPPORT

HT_API AudioOutputList * onvif_add_AudioOutput(AudioOutputList ** p_head)
{
    AudioOutputList * p_tmp;
    AudioOutputList * p_new = (AudioOutputList *) malloc(sizeof(AudioOutputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioOutputList));
    
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

HT_API AudioOutputConfigurationList * onvif_add_AudioOutputConfiguration(AudioOutputConfigurationList ** p_head)
{
    AudioOutputConfigurationList * p_tmp;
    AudioOutputConfigurationList * p_new = (AudioOutputConfigurationList *) malloc(sizeof(AudioOutputConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(AudioOutputConfigurationList));

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

HT_API RelayOutputOptionsList * onvif_add_RelayOutputOptions(RelayOutputOptionsList ** p_head)
{
    RelayOutputOptionsList * p_tmp;
    RelayOutputOptionsList * p_new = (RelayOutputOptionsList *) malloc(sizeof(RelayOutputOptionsList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(RelayOutputOptionsList));

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

HT_API void onvif_free_RelayOutputOptions(RelayOutputOptionsList ** p_head)
{
    RelayOutputOptionsList * p_next;
    RelayOutputOptionsList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
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

HT_API DigitalInputList * onvif_add_DigitalInput(DigitalInputList ** p_head)
{
    DigitalInputList * p_tmp;
    DigitalInputList * p_new = (DigitalInputList *) malloc(sizeof(DigitalInputList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(DigitalInputList));

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

HT_API SerialPortList * onvif_add_SerialPort(SerialPortList ** p_head)
{
    SerialPortList * p_tmp;
    SerialPortList * p_new = (SerialPortList *) malloc(sizeof(SerialPortList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(SerialPortList));

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

#endif // end of DEVICEIO_SUPPORT

#ifdef THERMAL_SUPPORT

HT_API ThermalConfigurationList * onvif_add_ThermalConfiguration(ThermalConfigurationList ** p_head)
{
    ThermalConfigurationList * p_tmp;
    ThermalConfigurationList * p_new = (ThermalConfigurationList *) malloc(sizeof(ThermalConfigurationList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ThermalConfigurationList));

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

HT_API void onvif_free_ThermalConfigurations(ThermalConfigurationList ** p_head)
{
    ThermalConfigurationList * p_next;
    ThermalConfigurationList * p_tmp = *p_head;

    while (p_tmp)
    {
        p_next = p_tmp->next;
        
        free(p_tmp);
        p_tmp = p_next;
    }

    *p_head = NULL;
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

HT_API NUCTableList * onvif_add_NUCTable(NUCTableList ** p_head)
{
    NUCTableList * p_tmp;
    NUCTableList * p_new = (NUCTableList *) malloc(sizeof(NUCTableList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(NUCTableList));

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

#endif // end of THERMAL_SUPPORT

#ifdef RECEIVER_SUPPORT

HT_API ReceiverList * onvif_add_Receiver(ReceiverList ** p_head)
{
    ReceiverList * p_tmp;
    ReceiverList * p_new = (ReceiverList *) malloc(sizeof(ReceiverList));
    if (NULL == p_new)
    {
        return NULL;
    }

    memset(p_new, 0, sizeof(ReceiverList));

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

#endif // end of RECEIVER_SUPPORT




