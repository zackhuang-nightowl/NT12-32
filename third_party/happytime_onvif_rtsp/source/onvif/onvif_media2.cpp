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
#include "onvif_media2.h"
#include "onvif_media.h"
#include "onvif_event.h"

#ifdef MEDIA2_SUPPORT

/************************************************************************************/
extern ONVIF_CLS g_onvif_cls;
extern ONVIF_CFG g_onvif_cfg;
extern ONVIF_IDX g_onvif_idx;

/************************************************************************************
 *      
 * Whenever a profile is created, deleted or one or more of its configurations are 
 * added or removed the following event should be generated.
 *
*************************************************************************************/
void onvif_MediaProfileChangedNotify(ONVIF_PROFILE * p_profile)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:Media/ProfileChanged", PropertyOperation_Changed, 
        "Token", p_profile->token, NULL, NULL, 
        NULL, NULL, NULL, NULL);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a Configuration of a device changes the device should provide the 
 * following event. 
 * For the parameter Type pass the appropriate ConfigurationEnumeration value
 *
*************************************************************************************/
void onvif_MediaConfigurationChangedNotify(const char * token, const char * type)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:Media/ConfigurationChanged", PropertyOperation_Changed, 
        "Token", token, "Type", type, 
        NULL, NULL, NULL, NULL);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetVideoEncoderConfiguration(tr2_SetVideoEncoderConfiguration_REQ * p_req)
{
    uint32 i = 0;
    VideoEncoder2ConfigurationList * p_v_enc_cfg;
    VideoEncoder2ConfigurationOptionsList * p_option;
    
    p_v_enc_cfg = onvif_find_VideoEncoder2Configuration(g_onvif_cfg.v_enc_cfg, p_req->Configuration.token);
    if (NULL == p_v_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    p_option = onvif_find_VideoEncoder2ConfigurationOptions(p_v_enc_cfg->Options2, p_req->Configuration.Encoding);
    if (p_option)
    {
        if (p_req->Configuration.Quality < p_option->Options.QualityRange.Min || 
            p_req->Configuration.Quality > p_option->Options.QualityRange.Max )
        {
            return ONVIF_ERR_ConfigModify;
        }

        if (p_req->Configuration.GovLengthFlag)
        {
            // todo : check the govlength ...
        }

        for (i = 0; i < ARRAY_SIZE(p_option->Options.ResolutionsAvailable); i++)
        {
            if (p_option->Options.ResolutionsAvailable[i].Width == p_req->Configuration.Resolution.Width && 
                p_option->Options.ResolutionsAvailable[i].Height == p_req->Configuration.Resolution.Height)
            {
                break;
            }
        }

        if (i == ARRAY_SIZE(p_option->Options.ResolutionsAvailable))
        {
            return ONVIF_ERR_ConfigModify;
        }
    }

    p_v_enc_cfg->Configuration.Resolution.Width = p_req->Configuration.Resolution.Width;
    p_v_enc_cfg->Configuration.Resolution.Height = p_req->Configuration.Resolution.Height;
    p_v_enc_cfg->Configuration.Quality = p_req->Configuration.Quality;
    strcpy(p_v_enc_cfg->Configuration.Encoding, p_req->Configuration.Encoding);
    strcpy(p_v_enc_cfg->Configuration.Name, p_req->Configuration.Name);

    if (strcasecmp(p_req->Configuration.Encoding, "JPEG") == 0)
    {
        p_v_enc_cfg->Configuration.VideoEncoding = VideoEncoding_JPEG;
    }
    else if (strcasecmp(p_req->Configuration.Encoding, "H264") == 0)
    {
        p_v_enc_cfg->Configuration.VideoEncoding = VideoEncoding_H264;
    }
    else if (strcasecmp(p_req->Configuration.Encoding, "H265") == 0)
    {
        // The onvif media 1 service does not support H265, 
        // here set the VideoEncoding of the onvif media 1 service field to H264
        p_v_enc_cfg->Configuration.VideoEncoding = VideoEncoding_H264;
    }
    else if (strcasecmp(p_req->Configuration.Encoding, "MP4V-ES") == 0)
    {
        p_v_enc_cfg->Configuration.VideoEncoding = VideoEncoding_MPEG4;
    }
    
    if (p_req->Configuration.GovLengthFlag)
    {
        p_v_enc_cfg->Configuration.GovLength = p_req->Configuration.GovLength;
    }

    if (strcasecmp(p_req->Configuration.Encoding, "JPEG") == 0)
    {
        p_v_enc_cfg->Configuration.GovLengthFlag = 0;
    }
    else
    {
        p_v_enc_cfg->Configuration.GovLengthFlag = 1;
    }    
    
    if (p_req->Configuration.ProfileFlag)
    {
        strcpy(p_v_enc_cfg->Configuration.Profile, p_req->Configuration.Profile);
    }
    
    if (p_req->Configuration.RateControlFlag)
    {
        p_v_enc_cfg->Configuration.RateControl.FrameRateLimit = p_req->Configuration.RateControl.FrameRateLimit;        
        p_v_enc_cfg->Configuration.RateControl.BitrateLimit = p_req->Configuration.RateControl.BitrateLimit;

        if (p_req->Configuration.RateControl.ConstantBitRateFlag)
        {
            p_v_enc_cfg->Configuration.RateControl.ConstantBitRate = p_req->Configuration.RateControl.ConstantBitRate;
        }
    }

    if (p_req->Configuration.MulticastFlag)
    {
        memcpy(&p_v_enc_cfg->Configuration.Multicast, &p_req->Configuration.Multicast, sizeof(onvif_MulticastConfiguration));
    }

    onvif_MediaConfigurationChangedNotify(p_req->Configuration.token, "VideoEncoder");
    
    // todo : here add handler code ...
    
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Returns the available parameters and their valid ranges to the client. 
 *  Any combination of the parameters obtained using a given media profile 
 *  and configuration shall be a valid input for the corresponding set 
 *  configuration command.
 *
 *  If a configuration token is provided, the device shall return the options
 *  compatible with that configuration. If a media profile token is specified, 
 *  the device shall return the options compatible with that media profile. 
 *  If both a media profile token and a configuration token are specified, the 
 *  device shall return the options compatible with both that media profile and 
 *  that configuration. If no tokens are specified, the options shall be 
 *  considered generic for the device.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_IncompatibleConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_GetVideoEncoderConfigurationOptions(tr2_GetVideoEncoderConfigurationOptions_REQ * p_req, tr2_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    ONVIF_PROFILE * p_profile = NULL;
    VideoEncoder2ConfigurationList * p_v_enc_cfg = NULL;
    VideoEncoder2ConfigurationOptionsList * p_option;
    
    if (p_req->GetConfiguration.ConfigurationTokenFlag)
    {
        p_v_enc_cfg = onvif_find_VideoEncoder2Configuration(g_onvif_cfg.v_enc_cfg, p_req->GetConfiguration.ConfigurationToken);
        if (NULL == p_v_enc_cfg)
        {
            return ONVIF_ERR_NoConfig;
        }
    }

    if (p_req->GetConfiguration.ProfileTokenFlag)
    {
        p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->GetConfiguration.ProfileToken);
        if (NULL == p_profile)
        {
            return ONVIF_ERR_NoProfile;
        }

        p_v_enc_cfg = p_profile->v_enc_cfg;
    }

    if (NULL == p_v_enc_cfg)
    {
        p_v_enc_cfg = g_onvif_cfg.v_enc_cfg;
    }

    if (NULL == p_v_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    p_option = p_v_enc_cfg->Options2;

    while (p_option)
    {
        VideoEncoder2ConfigurationOptionsList * p_item = onvif_add_VideoEncoder2ConfigurationOptions(&p_res->Options);
        if (p_item)
        {
            memcpy(&p_item->Options, &p_option->Options, sizeof(onvif_VideoEncoder2ConfigurationOptions));
        }
        
        p_option = p_option->next;
    }

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Creates a new media profile. The media profile shall be created in the device.
 *
 *  A created profile shall be deletable and a device shall set the "fixed" 
 *  attribute to false in the returned Profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_MaxNVTProfiles
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_CreateProfile(tr2_CreateProfile_REQ * p_req, tr2_CreateProfile_RES * p_res)
{
    uint32 i;
    ONVIF_PROFILE * p_profile = NULL;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    p_profile = onvif_add_profile(&p_config->profiles, FALSE);
    if (p_profile)
    {
        strcpy(p_profile->name, p_req->Name);        
        strcpy(p_res->Token, p_profile->token);

        // add configuration to the new profile
        for (i = 0; i < p_req->sizeConfiguration; i++)
        {                                         
            if (strcasecmp(p_req->Configuration[i].Type, "all") == 0)
            {
                ONVIF_PROFILE * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_profile(p_config->profiles, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->profiles;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
            
                p_profile->v_src_cfg = p_refer->v_src_cfg;
                if (p_profile->v_src_cfg)
                {
                    p_profile->v_src_cfg->Configuration.UseCount++;
                }
                
                p_profile->v_enc_cfg = p_refer->v_enc_cfg;
                if (p_profile->v_enc_cfg)
                {
                    p_profile->v_enc_cfg->Configuration.UseCount++;
                }    
                
                p_profile->metadata_cfg = p_refer->metadata_cfg;
                if (p_profile->metadata_cfg)
                {
                    p_profile->metadata_cfg->Configuration.UseCount++;
                }    
                
#ifdef AUDIO_SUPPORT                
                p_profile->a_src_cfg = p_refer->a_src_cfg;
                if (p_profile->a_src_cfg)
                {
                    p_profile->a_src_cfg->Configuration.UseCount++;
                }
                
                p_profile->a_enc_cfg = p_refer->a_enc_cfg;
                if (p_profile->a_enc_cfg)
                {
                    p_profile->a_enc_cfg->Configuration.UseCount++;
                }

                p_profile->a_dec_cfg = p_refer->a_dec_cfg;
                if (p_profile->a_dec_cfg)
                {
                    p_profile->a_dec_cfg->Configuration.UseCount++;
                }
#endif        

#ifdef DEVICEIO_SUPPORT
                p_profile->a_output_cfg = p_refer->a_output_cfg;
                if (p_profile->a_output_cfg)
                {
                    p_profile->a_output_cfg->Configuration.UseCount++;
                }
#endif

#ifdef VIDEO_ANALYTICS
                p_profile->va_cfg = p_refer->va_cfg;
                if (p_profile->va_cfg)
                {
                    p_profile->va_cfg->Configuration.UseCount++;
                }
#endif

#ifdef PTZ_SUPPORT
                p_profile->ptz_cfg = p_refer->ptz_cfg;
                if (p_profile->ptz_cfg)
                {
                    p_profile->ptz_cfg->Configuration.UseCount++;
                }
#endif
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "VideoSource") == 0)
            {
                VideoSourceConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_VideoSourceConfiguration(p_config->v_src_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->v_src_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->v_src_cfg = p_refer;
                p_profile->v_src_cfg->Configuration.UseCount++;
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "VideoEncoder") == 0)
            {
                VideoEncoder2ConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_VideoEncoder2Configuration(p_config->v_enc_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->v_enc_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->v_enc_cfg = p_refer;
                p_profile->v_enc_cfg->Configuration.UseCount++;
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "AudioSource") == 0)
            {
#ifdef AUDIO_SUPPORT
                AudioSourceConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_AudioSourceConfiguration(p_config->a_src_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->a_src_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->a_src_cfg = p_refer;
                p_profile->a_src_cfg->Configuration.UseCount++;
#endif
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "AudioEncoder") == 0)
            {
#ifdef AUDIO_SUPPORT
                AudioEncoder2ConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_AudioEncoder2Configuration(p_config->a_enc_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->a_enc_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->a_enc_cfg = p_refer;
                p_profile->a_enc_cfg->Configuration.UseCount++;
#endif            
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "AudioOutput") == 0)
            {
#ifdef DEVICEIO_SUPPORT
                AudioOutputConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_AudioOutputConfiguration(p_config->a_output_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->a_output_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->a_output_cfg = p_refer;
                p_profile->a_output_cfg->Configuration.UseCount++;
#endif            
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "AudioDecoder") == 0)
            {
#ifdef AUDIO_SUPPORT        
                AudioDecoderConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_AudioDecoderConfiguration(p_config->a_dec_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->a_dec_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->a_dec_cfg = p_refer;
                p_profile->a_dec_cfg->Configuration.UseCount++;
#endif    
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "Metadata") == 0)
            {
                MetadataConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_MetadataConfiguration(p_config->metadata_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->metadata_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->metadata_cfg = p_refer;
                p_profile->metadata_cfg->Configuration.UseCount++;
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "Analytics") == 0)
            {
#ifdef VIDEO_ANALYTICS
                VideoAnalyticsConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_VideoAnalyticsConfiguration(p_config->va_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->va_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->va_cfg = p_refer;
                p_profile->va_cfg->Configuration.UseCount++;
#endif
            }
            else if (strcasecmp(p_req->Configuration[i].Type, "PTZ") == 0)
            {
#ifdef PTZ_SUPPORT
                PTZConfigurationList * p_refer = NULL;
                if (p_req->Configuration[i].TokenFlag)
                {
                    p_refer = onvif_find_PTZConfiguration(p_config->ptz_cfg, p_req->Configuration[i].Token);
                }
                else
                {
                    p_refer = p_config->ptz_cfg;
                }

                if (NULL == p_refer)
                {
                    continue;
                }
                
                p_profile->ptz_cfg = p_refer;
                p_profile->ptz_cfg->Configuration.UseCount++;
#endif            
            }
        }
        
        // setup the new profile stream uri    
        if (p_config->profiles)
        {
            strcpy(p_profile->stream_uri, p_config->profiles->stream_uri);
        }
    }
    else 
    {
        return ONVIF_ERR_MaxNVTProfiles;
    }
    
    onvif_MediaProfileChangedNotify(p_profile);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Deletes a profile.
 *
 *  A device signaling support for MultiTrackStreaming shall support deleting
 *  of virtual profiles via the command. Note that deleting a profile of a 
 *  virtual profile set may invalidate the virtual profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_DeletionOfFixedProfile
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_DeleteProfile(tr2_DeleteProfile_REQ * p_req)
{
    trt_DeleteProfile_REQ req;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->Token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    
    onvif_MediaProfileChangedNotify(p_profile);

    strcpy(req.ProfileToken, p_req->Token);
    
    return onvif_trt_DeleteProfile(&req);
}

/************************************************************************************
 *
 * @brief
 *  Adds one or more configurations to an existing media profile. 
 *  If one of the configuration already exists in the media profile, 
 *  it will be replaced.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_AddConfiguration(tr2_AddConfiguration_REQ * p_req)
{
    uint32 i;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(p_config->profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_req->NameFlag)
    {
        strcpy(p_profile->name, p_req->Name);
    }

    // add configuration to the new profile
    for (i = 0; i < p_req->sizeConfiguration; i++)
    {                                         
        if (strcasecmp(p_req->Configuration[i].Type, "all") == 0)
        {
            ONVIF_PROFILE * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_profile(p_config->profiles, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->profiles;
            }

            if (NULL == p_refer)
            {
                continue;
            }
        
            p_profile->v_src_cfg = p_refer->v_src_cfg;
            if (p_profile->v_src_cfg)
            {
                p_profile->v_src_cfg->Configuration.UseCount++;
            }
            
            p_profile->v_enc_cfg = p_refer->v_enc_cfg;
            if (p_profile->v_enc_cfg)
            {
                p_profile->v_enc_cfg->Configuration.UseCount++;
            }   
            
            p_profile->metadata_cfg = p_refer->metadata_cfg;
            if (p_profile->metadata_cfg)
            {
                p_profile->metadata_cfg->Configuration.UseCount++;
            }   
            
#ifdef AUDIO_SUPPORT
            p_profile->a_src_cfg = p_refer->a_src_cfg;
            if (p_profile->a_src_cfg)
            {
                p_profile->a_src_cfg->Configuration.UseCount++;
            }
            
            p_profile->a_enc_cfg = p_refer->a_enc_cfg;
            if (p_profile->a_enc_cfg)
            {
                p_profile->a_enc_cfg->Configuration.UseCount++;
            }

            p_profile->a_dec_cfg = p_refer->a_dec_cfg;
            if (p_profile->a_dec_cfg)
            {
                p_profile->a_dec_cfg->Configuration.UseCount++;
            }
#endif

#ifdef DEVICEIO_SUPPORT
            p_profile->a_output_cfg = p_refer->a_output_cfg;
            if (p_profile->a_output_cfg)
            {
                p_profile->a_output_cfg->Configuration.UseCount++;
            }
#endif

#ifdef VIDEO_ANALYTICS
            p_profile->va_cfg = p_refer->va_cfg;
            if (p_profile->va_cfg)
            {
                p_profile->va_cfg->Configuration.UseCount++;
            }
#endif

#ifdef PTZ_SUPPORT
            p_profile->ptz_cfg = p_refer->ptz_cfg;
            if (p_profile->ptz_cfg)
            {
                p_profile->ptz_cfg->Configuration.UseCount++;
            }
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "VideoSource") == 0)
        {
            VideoSourceConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_VideoSourceConfiguration(p_config->v_src_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->v_src_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->v_src_cfg = p_refer;
            p_profile->v_src_cfg->Configuration.UseCount++;
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "VideoEncoder") == 0)
        {
            VideoEncoder2ConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_VideoEncoder2Configuration(p_config->v_enc_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->v_enc_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->v_enc_cfg = p_refer;
            p_profile->v_enc_cfg->Configuration.UseCount++;
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioSource") == 0)
        {
#ifdef AUDIO_SUPPORT
            AudioSourceConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_AudioSourceConfiguration(p_config->a_src_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->a_src_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->a_src_cfg = p_refer;
            p_profile->a_src_cfg->Configuration.UseCount++;
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioEncoder") == 0)
        {
#ifdef AUDIO_SUPPORT
            AudioEncoder2ConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_AudioEncoder2Configuration(p_config->a_enc_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->a_enc_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->a_enc_cfg = p_refer;
            p_profile->a_enc_cfg->Configuration.UseCount++;
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioOutput") == 0)
        {
#ifdef DEVICEIO_SUPPORT
            AudioOutputConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_AudioOutputConfiguration(p_config->a_output_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->a_output_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->a_output_cfg = p_refer;
            p_profile->a_output_cfg->Configuration.UseCount++;
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioDecoder") == 0)
        {
#ifdef AUDIO_SUPPORT
            AudioDecoderConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_AudioDecoderConfiguration(p_config->a_dec_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->a_dec_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->a_dec_cfg = p_refer;
            p_profile->a_dec_cfg->Configuration.UseCount++;
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "Metadata") == 0)
        {
            MetadataConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_MetadataConfiguration(p_config->metadata_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->metadata_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }
            
            p_profile->metadata_cfg = p_refer;
            p_profile->metadata_cfg->Configuration.UseCount++;
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "Analytics") == 0)
        {
#ifdef VIDEO_ANALYTICS
            VideoAnalyticsConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_VideoAnalyticsConfiguration(p_config->va_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->va_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }

            p_profile->va_cfg = p_refer;
            p_profile->va_cfg->Configuration.UseCount++;
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "PTZ") == 0)
        {
#ifdef PTZ_SUPPORT
            PTZConfigurationList * p_refer = NULL;
            if (p_req->Configuration[i].TokenFlag)
            {
                p_refer = onvif_find_PTZConfiguration(p_config->ptz_cfg, p_req->Configuration[i].Token);
            }
            else
            {
                p_refer = p_config->ptz_cfg;
            }

            if (NULL == p_refer)
            {
                continue;
            }

            p_profile->ptz_cfg = p_refer;
            p_profile->ptz_cfg->Configuration.UseCount++;
#endif
        }
    }

    onvif_MediaProfileChangedNotify(p_profile);

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes one or more configurations from an existing media profile. 
 *  Tokens appearing in the configuration list shall be ignored. 
 *  Presence of the "All" type shall result in an empty profile. 
 *  Removing a non existing configuration shall be ignored and not 
 *  result in an error.
 *  
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_RemoveConfiguration(tr2_RemoveConfiguration_REQ * p_req)
{
    uint32 i;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    for (i = 0; i < p_req->sizeConfiguration; i++)
    {
        if (strcasecmp(p_req->Configuration[i].Type, "all") == 0)
        {
            if (p_profile->v_src_cfg && p_profile->v_src_cfg->Configuration.UseCount > 0)
            {
                p_profile->v_src_cfg->Configuration.UseCount--;
                p_profile->v_src_cfg = NULL;
            }

            if (p_profile->v_enc_cfg && p_profile->v_enc_cfg->Configuration.UseCount > 0)
            {
                p_profile->v_enc_cfg->Configuration.UseCount--;
                p_profile->v_enc_cfg = NULL;
            }

            if (p_profile->metadata_cfg && p_profile->metadata_cfg->Configuration.UseCount > 0)
            {
                p_profile->metadata_cfg->Configuration.UseCount--;
                p_profile->metadata_cfg = NULL;
            }
            
#ifdef AUDIO_SUPPORT
            if (p_profile->a_src_cfg && p_profile->a_src_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_src_cfg->Configuration.UseCount--;
                p_profile->a_src_cfg = NULL;
            }

            if (p_profile->a_enc_cfg && p_profile->a_enc_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_enc_cfg->Configuration.UseCount--;
                p_profile->a_enc_cfg = NULL;
            }

            if (p_profile->a_dec_cfg && p_profile->a_dec_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_dec_cfg->Configuration.UseCount--;
                p_profile->a_dec_cfg = NULL;
            }
#endif        

#ifdef DEVICEIO_SUPPORT
            if (p_profile->a_output_cfg && p_profile->a_output_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_output_cfg->Configuration.UseCount--;
                p_profile->a_output_cfg = NULL;
            }
#endif

#ifdef VIDEO_ANALYTICS
            if (p_profile->va_cfg && p_profile->va_cfg->Configuration.UseCount > 0)
            {
                p_profile->va_cfg->Configuration.UseCount--;
                p_profile->va_cfg = NULL;
            }
#endif

#ifdef PTZ_SUPPORT
            if (p_profile->ptz_cfg && p_profile->ptz_cfg->Configuration.UseCount > 0)
            {
                p_profile->ptz_cfg->Configuration.UseCount--;
                p_profile->ptz_cfg = NULL;
            }
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "VideoSource") == 0)
        {
            if (p_profile->v_src_cfg && p_profile->v_src_cfg->Configuration.UseCount > 0)
            {
                p_profile->v_src_cfg->Configuration.UseCount--;
                p_profile->v_src_cfg = NULL;
            }
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "VideoEncoder") == 0)
        {
            if (p_profile->v_enc_cfg && p_profile->v_enc_cfg->Configuration.UseCount > 0)
            {
                p_profile->v_enc_cfg->Configuration.UseCount--;
                p_profile->v_enc_cfg = NULL;
            }
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioSource") == 0)
        {
#ifdef AUDIO_SUPPORT                
            if (p_profile->a_src_cfg && p_profile->a_src_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_src_cfg->Configuration.UseCount--;
                p_profile->a_src_cfg = NULL;
            }
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioEncoder") == 0)
        {
#ifdef AUDIO_SUPPORT
            if (p_profile->a_enc_cfg && p_profile->a_enc_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_enc_cfg->Configuration.UseCount--;
                p_profile->a_enc_cfg = NULL;
            }
#endif            
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioOutput") == 0)
        {
#ifdef DEVICEIO_SUPPORT
            if (p_profile->a_output_cfg && p_profile->a_output_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_output_cfg->Configuration.UseCount--;
                p_profile->a_output_cfg = NULL;
            }
#endif            
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "AudioDecoder") == 0)
        {
#ifdef AUDIO_SUPPORT        
            if (p_profile->a_dec_cfg && p_profile->a_dec_cfg->Configuration.UseCount > 0)
            {
                p_profile->a_dec_cfg->Configuration.UseCount--;
                p_profile->a_dec_cfg = NULL;
            }
#endif        
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "Metadata") == 0)
        {
            if (p_profile->metadata_cfg && p_profile->metadata_cfg->Configuration.UseCount > 0)
            {
                p_profile->metadata_cfg->Configuration.UseCount--;
                p_profile->metadata_cfg = NULL;
            }
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "Analytics") == 0)
        {
#ifdef VIDEO_ANALYTICS
            if (p_profile->va_cfg && p_profile->va_cfg->Configuration.UseCount > 0)
            {
                p_profile->va_cfg->Configuration.UseCount--;
                p_profile->va_cfg = NULL;
            }
#endif
        }
        else if (strcasecmp(p_req->Configuration[i].Type, "PTZ") == 0)
        {
#ifdef PTZ_SUPPORT
            if (p_profile->ptz_cfg && p_profile->ptz_cfg->Configuration.UseCount > 0)
            {
                p_profile->ptz_cfg->Configuration.UseCount--;
                p_profile->ptz_cfg = NULL;
            }
#endif            
        }
    }

    onvif_MediaProfileChangedNotify(p_profile);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetVideoSourceConfiguration(tr2_SetVideoSourceConfiguration_REQ * p_req)
{
    ONVIF_RET ret;
    trt_SetVideoSourceConfiguration_REQ req;

    memcpy(&req.Configuration, &p_req->Configuration, sizeof(onvif_VideoSourceConfiguration));
    req.ForcePersistence = TRUE;
    
    ret = onvif_trt_SetVideoSourceConfiguration(&req);

    if (ONVIF_OK == ret)
    {
        onvif_MediaConfigurationChangedNotify(req.Configuration.token, "VideoSource");
    }

    // todo : here add handler code ...

    
    return ret;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetMetadataConfiguration(tr2_SetMetadataConfiguration_REQ * p_req)
{
    ONVIF_RET ret;
    trt_SetMetadataConfiguration_REQ req;

    memcpy(&req.Configuration, &p_req->Configuration, sizeof(onvif_MetadataConfiguration));
    req.ForcePersistence = TRUE;

    ret = onvif_trt_SetMetadataConfiguration(&req);

    if (ONVIF_OK == ret)
    {
        onvif_MediaConfigurationChangedNotify(p_req->Configuration.token, "Metadata");
    }

    // todo : here add handler code ...

    
    return ret;
}

/************************************************************************************
 *
 * @brief
 *  Provides information on how many video encoders a device can instantiate
 *  concurrently for a VideoSourceConfiguration.
 *
 *  The Info response contains the following information:
 *  Total Total number of encoder instances independent of the codec,
 *  Codec Number of encoder instances for each supported codec .
 *
 *  A device shall guarantee to instantiate the indicated number of instances
 *  concurrently. If a device limits the number of instances of each particular
 *  video encoding type, the response shall contain information per video codec.
 *  For each video source, there shall be at least one video source configuration
 *  for which the GetVideoEncoderInstances shall return a Total greater than 0.
 *  The total sum of video encoder instances over all video source configurations 
 *  of a device shall not exceed the value signaled via MaximumNumberOfProfiles.
 *
 *  For example, if a device has two VideoSourceConfigurations and if the first 
 *  allows a total of two concurrent instances and the second allows only one 
 *  instance, this device shall allow creation of at least three media profiles.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_GetVideoEncoderInstances(tr2_GetVideoEncoderInstances_REQ * p_req, tr2_GetVideoEncoderInstances_RES * p_res)
{
    int total = 0;
    ONVIF_PROFILE * p_profile;
    VideoSourceConfigurationList * p_v_src = onvif_find_VideoSourceConfiguration(g_onvif_cfg.v_src_cfg, p_req->ConfigurationToken);
    if (NULL == p_v_src)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : modify the p_res ...

    // get the stream nums
    p_profile = g_onvif_cfg.profiles;
    while (p_profile)
    {
        if (p_profile->v_src_cfg && strcmp(p_profile->v_src_cfg->Configuration.token, p_req->ConfigurationToken) == 0)
        {
            total++;
        }
        
        p_profile = p_profile->next;
    }
    
    p_res->Info.Total = total;
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Synchronization points allow clients to decode and correctly use all 
 *  data after the synchronization point.
 *  For example, if a video stream is configured with a large I-frame 
 *  distance and a client loses a single packet, the client does not 
 *  display video until the next I-frame is transmitted. In such cases, 
 *  the client can request a Synchronization Point which forces the device
 *  to add an I-frame as soon as possible. Clients can request Synchronization
 *  Points for profiles. The device shall add synchronization points for 
 *  all streams associated with this profile.
 *
 *  Similarly, a synchronization point is used to get an update on full PTZ
 *  or event status through the metadata stream.
 *
 *  If a video stream is associated with the profile, an I-frame shall be 
 *  added to this video stream. If an event stream is associated to the profile,
 *  the synchronization point request shall be handled as described in the 
 *  section Synchronization Point of the ONVIF Core Specification. If the 
 *  profile is configured for PTZ metadata, the PTZ position shall be repeated
 *  within the metadata stream.
 *
 *  A device shall support the request for an I-frame through the 
 *  SetSynchronizationPoint command if the RTSPStreaming capability is set.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetSynchronizationPoint(tr2_SetSynchronizationPoint_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // todo : here add handler code ...


    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Changes the media profile structure relating to video source for 
 *  the specified video source mode. A device that indicates a capability
 *  of VideoSourceMode shall support this command. The behavior after 
 *  changing the mode is not defined in this specification.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoVideoSource
 *  ONVIF_ERR_NoVideoSourceMode
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetVideoSourceMode(tr2_SetVideoSourceMode_REQ * p_req, tr2_SetVideoSourceMode_RES * p_res)
{
    VideoSourceList * p_v_src = onvif_find_VideoSource(g_onvif_cfg.v_src, p_req->VideoSourceToken);
    if (NULL == p_v_src)
    {
        return ONVIF_ERR_NoVideoSource;
    }

    if (strcmp(p_v_src->VideoSourceMode.token, p_req->VideoSourceModeToken))
    {
        return ONVIF_ERR_NoVideoSourceMode;
    }

    // todo : here add handler code ...
    

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  A Network client uses the GetSnapshotUri command to obtain a JPEG
 *  snapshot from the device. The returned URI shall remain valid indefinitely
 *  even if the profile parameters change. The URI can be used for acquiring
 *  one or more JPEG images through a HTTP GET operation.
 *
 *  The image encoding will always be JPEG regardless of the encoding setting 
 *  in the media profile. The JPEG settings (like resolution or quality) 
 *  should be taken from the profile if suitable. The provided image shall be
 *  updated automatically and independent from calls to GetSnapshotUri.
 *
 *  A device shall support this command when the SnapshotUri capability is 
 *  set to true.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_GetSnapshotUri(HTTPCLN * p_user, tr2_GetSnapshotUri_REQ * p_req, tr2_GetSnapshotUri_RES * p_res)
{
    uint16 port;
    char proto[16];
    char sip[64];
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // set the media uri

    onvif_get_service_ip_by_user(p_user, sip, sizeof(sip)-1);

#ifdef HTTPS
    if (p_user->https)
    {
        port = g_onvif_cls.https_port;
        strcpy(proto, "https");
    }
    else
#endif
    {
        port = g_onvif_cls.http_port;
        strcpy(proto, "http");
    }

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->Uri, sizeof(p_res->Uri), 
            "%s://[%s]:%u/snapshot/%s", 
            proto, sip, port, p_profile->token);
    }
    else
    {
        snprintf(p_res->Uri, sizeof(p_res->Uri), 
            "%s://%s:%u/snapshot/%s", 
            proto, sip, port, p_profile->token);
    }

    return ONVIF_OK;
}

int onvif_tr2_BuildStreamParams(tr2_GetStreamUri_REQ * p_req, ONVIF_PROFILE * p_profile, char * buff, int len)
{
    int offset = 0;
    
    if (strcasecmp(p_req->Protocol, "RtspUnicast") == 0)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "unicast");
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "udp");
    }
    else if (strcasecmp(p_req->Protocol, "RtspMulticast") == 0)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "multicast");
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "udp");
    }
    else if (strcasecmp(p_req->Protocol, "RTSP") == 0)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "unicast");
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "rtsp");
    }
    else if (strcasecmp(p_req->Protocol, "RtspOverHttp") == 0)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "unicast");
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "http");
    }

    /**
     * If the audio and video parameters have been set to the encoder 
     * in the onvif_tr2_SetVideoEncoderConfiguration function, 
     * then there is no need to pass the audio and video parameters 
     * to the rtsp server through the url.
     *
     **/

#if 1
    if (p_profile->v_enc_cfg)
    {            
        offset += snprintf(buff+offset, len-offset, "&amp;ve=%s&amp;w=%d&amp;h=%d", 
            p_profile->v_enc_cfg->Configuration.Encoding,
            p_profile->v_enc_cfg->Configuration.Resolution.Width,
            p_profile->v_enc_cfg->Configuration.Resolution.Height);
        
    }

#ifdef AUDIO_SUPPORT
    if (p_profile->a_enc_cfg)
    {            
        offset += snprintf(buff+offset, len-offset, "&amp;ae=%s&amp;sr=%d", 
            p_profile->a_enc_cfg->Configuration.Encoding,
            p_profile->a_enc_cfg->Configuration.SampleRate * 1000);
        
    }

    if (p_profile->a_dec_cfg && p_profile->a_dec_cfg->Options2)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;bce=%s", 
            p_profile->a_dec_cfg->Options2->Options.Encoding);
    }
#endif

#endif

    return offset;
}

/************************************************************************************
 *
 * @brief
 *  Requests a URI that can be used to initiate a live media stream using
 *  RTSP as the control protocol. The returned URI should remain valid 
 *  indefinitely even if the parameters of the profile are changed.
 *
 *  The following stream types are defined
 *  RtspUnicast RTSP streaming RTP via UDP Unicast.
 *  RtspMulticast RTSP streaming RTP via UDP Multicast.
 *  RTSP RTSP streaming RTP over TCP.
 *  RtspsUnicast Secure RTSP streaming SRTP via UDP Unicast.
 *  RtspsMulticast Secure RTSP streaming SRTP via UDP Multicast.
 *  RtspOverHttp Tunneling both the RTSP control channel and the RTP stream
 *  over HTTP or HTTPS.
 *
 *  For full compatibility with other ONVIF services a device shall not generate
 *  URIs longer than 128 octets.
 *
 *  A device that signals the RTSPStreaming capability shall support this command. 
 *  On a request for transport protocol RtspOverHttp a device shall return a URI
 *  that uses the same port as the web service. This enables seamless NAT traversal.
 *
 *  A device supporting MultiTrackStreaming shall support the retrieval of a 
 *  multitrack RTSP session URI by passing a virtual profile token.
 *  A device signaling support for SecureRTSPStreaming shall support streaming
 *  via SRTP.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_InvalidStreamSetup
 *  ONVIF_ERR_StreamConflict
 *  ONVIF_ERR_InvalidMulticastSettings
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_GetStreamUri(HTTPCLN * p_user, tr2_GetStreamUri_REQ * p_req, tr2_GetStreamUri_RES * p_res)
{
    int offset = 0;
    int len = sizeof(p_res->Uri);
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // set the media uri

    if (p_profile->stream_uri[0] == '\0')
    {
        /**
         * If the <stream_uri> node value under the <profile> node in the 
         * configuration file is not set, the default rtsp url is generated
         *
         */

        uint16 port;
        char sip[64];

        onvif_get_service_ip_by_user(p_user, sip, sizeof(sip)-1);

        if (strcasecmp(p_req->Protocol, "RtspOverHttp") == 0)
        {
            char proto[16] = {'\0'};
        
#ifdef HTTPS
            if (p_user->https)
            {
                port = g_onvif_cls.https_port;
                strcpy(proto, "https");
            }
            else
#endif
            {
                port = g_onvif_cls.http_port;
                strcpy(proto, "http");
            }

            if (p_user->sockaddr.ipv6_flag)
            {
                offset += snprintf(p_res->Uri, len, "%s://[%s]:%u/%s", proto, sip, port, RTSP_URL_SUFFIX);
            }
            else
            {
                offset += snprintf(p_res->Uri, len, "%s://%s:%u/%s", proto, sip, port, RTSP_URL_SUFFIX);
            }
        }
        else
        {
            port = g_onvif_cls.rtsp_port;
            
            if (p_user->sockaddr.ipv6_flag)
            {
                offset += snprintf(p_res->Uri, len, "rtsp://[%s]:%u/%s", sip, port, RTSP_URL_SUFFIX);
            }
            else
            {
                offset += snprintf(p_res->Uri, len, "rtsp://%s:%u/%s", sip, port, RTSP_URL_SUFFIX);
            }
        }

        onvif_tr2_BuildStreamParams(p_req, p_profile, p_res->Uri+offset, len-offset);
    }
    else
    {
        /**
         * If the <stream_uri> node value under the <profile> node in the 
         * configuration file is set, the rtsp url is used
         *
         */

        offset += snprintf(p_res->Uri, len, "%s", p_profile->stream_uri);

        if (p_profile->append_params)
        {
            onvif_tr2_BuildStreamParams(p_req, p_profile, p_res->Uri+offset, len-offset);
        }
    }

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Creates a new Mask for an existing VideoSourceConfiguration. 
 *  A device that signals support for Masks by the Mask capability shall
 *  support the creation of masks via this function as long as the number of
 *  existing masks does not exceed the value of MaxMasks for the given 
 *  VideoSourceConfiguration.
 *  
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_MaxMasks
 *  ONVIF_ERR_InvalidPolygon
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_CreateMask(tr2_CreateMask_REQ * p_req)
{
    MaskList * p_mask;
    VideoSourceConfigurationList * p_v_cfg = onvif_find_VideoSourceConfiguration(g_onvif_cfg.v_src_cfg, p_req->Mask.ConfigurationToken);
    if (NULL == p_v_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }
    
    p_mask = onvif_add_Mask(&g_onvif_cfg.mask);
    if (NULL == p_mask)
    {
        return ONVIF_ERR_MaxMasks;
    }

    if (p_req->Mask.Polygon.sizePoint <= 0)
    {
        return ONVIF_ERR_InvalidPolygon;
    }

    // return the token
    strcpy(p_req->Mask.token, p_mask->Mask.token);
    
    memcpy(&p_mask->Mask, &p_req->Mask, sizeof(onvif_Mask));    
    
    // todo : here add handler code ... 
    
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Deletes a mask configuration.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_DeleteMask(tr2_DeleteMask_REQ * p_req)
{
    MaskList * p_prev;
    MaskList * p_mask = onvif_find_Mask(g_onvif_cfg.mask, p_req->Token);
    if (NULL == p_mask)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : here add handler code ...


    p_prev = g_onvif_cfg.mask;
    if (p_mask == p_prev)
    {
        g_onvif_cfg.mask = p_mask->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_mask)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_mask->next;
    }

    free(p_mask);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a mask configuration. Running streams using this configuration
 *  may be immediately updated according to the new settings.
 *
 *  A device signaling support for Mask via its capabilities support this
 *  command. It shall accept any combination of parameters returned by 
 *  GetMaskOptions. If necessary the device may adapt parameter values 
 *  for the Color and Polygon element without returning an error.
 *
 *  Note that for devices signaling SingleColorOnly all masks of the 
 *  associated VideoSource will be updated.
 *  
 *  Note: A device signaling RectangleOnly shall accept any polygon with
 *  four points. In case the four vertices are not defining an exact 
 *  rectangle the device may adjust the vertices.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_InvalidPolygon
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetMask(tr2_SetMask_REQ * p_req)
{
    MaskList * p_mask = onvif_find_Mask(g_onvif_cfg.mask, p_req->Mask.token);
    if (NULL == p_mask)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_req->Mask.ConfigurationToken[0] == '\0' || p_req->Mask.Polygon.sizePoint <= 0)
    {
        return ONVIF_ERR_InvalidArgVal;
    }
    
    // todo : here add handler code ...


    memcpy(&p_mask->Mask, &p_req->Mask, sizeof(onvif_Mask));
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Starts multicast streaming using a specified media profile of a device. 
 *  Streaming continues until StopMulticastStreaming is called for the same
 *  Profile. The streaming shall be resumed after rebooting. It can be turned
 *  off using the StopMulticastStreaming method. The multicast address, port
 *  and TTL are configured in the VideoEncoderConfiguration, 
 *  AudioEncoderConfiguration and MetadataConfiguration respectively.
 *
 *  Multicast streaming may stop when the corresponding profile is deleted 
 *  or one of its Configurations is altered via one of the set configuration
 *  methods.
 *
 *  The implementation shall ensure that the RTP stream can be decoded without
 *  setting up an RTSP control connection. Especially in case of H.264 video, 
 *  the SPS/PPS header shall be sent inband.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_StartMulticastStreaming(tr2_StartMulticastStreaming_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // todo : start multicast streaming ...

    p_profile->multicasting = TRUE;

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Stops multicast streaming using a specified media profile of a device. 
 *  In case a device receives a StopMulticastStreaming request whose 
 *  corresponding multicast streaming is not started, the device should
 *  reply with successful StopMulticastStreamingResponse.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_StopMulticastStreaming(tr2_StopMulticastStreaming_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // todo : stop multicast streaming ...

    p_profile->multicasting = FALSE;

    return ONVIF_OK;
}

#ifdef AUDIO_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @returb
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetAudioEncoderConfiguration(tr2_SetAudioEncoderConfiguration_REQ * p_req)
{
    AudioEncoder2ConfigurationList * p_a_enc_cfg = onvif_find_AudioEncoder2Configuration(g_onvif_cfg.a_enc_cfg, p_req->Configuration.token);
    if (NULL == p_a_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_req->Configuration.SampleRate != 8  && 
        p_req->Configuration.SampleRate != 16 && 
        p_req->Configuration.SampleRate != 24 && 
        p_req->Configuration.SampleRate != 32 &&
        p_req->Configuration.SampleRate != 48)
    {
        return ONVIF_ERR_ConfigModify;
    }

    p_a_enc_cfg->Configuration.SessionTimeout = p_req->Configuration.SessionTimeout;
    p_a_enc_cfg->Configuration.Bitrate = p_req->Configuration.Bitrate;
    p_a_enc_cfg->Configuration.SampleRate = p_req->Configuration.SampleRate;
    strcpy(p_a_enc_cfg->Configuration.Name, p_req->Configuration.Name);
    strcpy(p_a_enc_cfg->Configuration.Encoding, p_req->Configuration.Encoding);

    if (strcasecmp(p_req->Configuration.Encoding, "PCMU") == 0)
    {
        p_req->Configuration.AudioEncoding = AudioEncoding_G711;
    }
    else if (strcasecmp(p_req->Configuration.Encoding, "G726") == 0)
    {
        p_req->Configuration.AudioEncoding = AudioEncoding_G726;
    }
    else if (strcasecmp(p_req->Configuration.Encoding, "MP4A-LATM") == 0)
    {
        p_req->Configuration.AudioEncoding = AudioEncoding_AAC;
    }
    
    memcpy(&p_a_enc_cfg->Configuration.Multicast, &p_req->Configuration.Multicast, sizeof(onvif_MulticastConfiguration));

    onvif_MediaConfigurationChangedNotify(p_req->Configuration.token, "AudioEncoder");
    
    // todo : here add handler code ...

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetAudioSourceConfiguration(tr2_SetAudioSourceConfiguration_REQ * p_req)
{
    ONVIF_RET ret;
    trt_SetAudioSourceConfiguration_REQ req;

    memcpy(&req.Configuration, &p_req->Configuration, sizeof(onvif_AudioSourceConfiguration));
    req.ForcePersistence = TRUE;

    ret = onvif_trt_SetAudioSourceConfiguration(&req);
    
    if (ONVIF_OK == ret)
    {
        onvif_MediaConfigurationChangedNotify(req.Configuration.token, "AudioSource");
    }

    // todo : here add handler code ...
    
    return ret;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetAudioDecoderConfiguration(tr2_SetAudioDecoderConfiguration_REQ * p_req)
{
    ONVIF_RET ret;
    trt_SetAudioDecoderConfiguration_REQ req;

    memcpy(&req.Configuration, &p_req->Configuration, sizeof(onvif_AudioDecoderConfiguration));
    req.ForcePersistence = TRUE;

    ret = onvif_trt_SetAudioDecoderConfiguration(&req);
    
    if (ONVIF_OK == ret)
    {
        onvif_MediaConfigurationChangedNotify(req.Configuration.token, "AudioDecoder");
    }

    // todo : here add handler code ...
    
    return ret;
}

#endif // end of AUDIO_SUPPORT

#ifdef DEVICEIO_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_tr2_SetAudioOutputConfiguration(tr2_SetAudioOutputConfiguration_REQ * p_req)
{
    AudioOutputList * p_output;
    AudioOutputConfigurationList * p_cfg = onvif_find_AudioOutputConfiguration(g_onvif_cfg.a_output_cfg, p_req->Configuration.token);
    if (NULL == p_cfg)
    {
        return ONVIF_ERR_NoAudioOutput;
    }

    p_output = onvif_find_AudioOutput(g_onvif_cfg.a_output, p_req->Configuration.OutputToken);
    if (NULL == p_output)
    {
        return ONVIF_ERR_NoAudioOutput;
    }

    if (p_req->Configuration.OutputLevel < p_cfg->Options.OutputLevelRange.Min || 
        p_req->Configuration.OutputLevel > p_cfg->Options.OutputLevelRange.Max)
    {
        return ONVIF_ERR_ConfigModify;
    }
    
    // todo : here add handler code ...

    
    strcpy(p_cfg->Configuration.Name, p_req->Configuration.Name);
    strcpy(p_cfg->Configuration.OutputToken, p_req->Configuration.OutputToken);
    if (p_req->Configuration.SendPrimacyFlag)
    {
        strcpy(p_cfg->Configuration.SendPrimacy, p_req->Configuration.SendPrimacy);
    }
    p_cfg->Configuration.OutputLevel = p_req->Configuration.OutputLevel;

    onvif_MediaConfigurationChangedNotify(p_req->Configuration.token, "AudioOutput");

    return ONVIF_OK;
}

#endif // end of DEVICEIO_SUPPORT

#endif // MEDIA2_SUPPORT



