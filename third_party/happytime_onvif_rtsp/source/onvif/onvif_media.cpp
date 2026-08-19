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
#include "onvif_media.h"
#include "onvif_utils.h"
#include "onvif_pkt.h"
#include "onvif_event.h"

#if defined(MEDIA_SUPPORT) || defined(MEDIA2_SUPPORT)

/***************************************************************************************/
extern ONVIF_CFG g_onvif_cfg;
extern ONVIF_CLS g_onvif_cls;
extern ONVIF_IDX g_onvif_idx;

/***************************************************************************************/

#ifdef MEDIA_SUPPORT

/************************************************************************************
 *      
 * Whenever a change in the profiles of a device supporting the media service occurs 
 * the device should provide the following event. The Profile change could be caused 
 * by Creation or Deletion of a Profile or by Adding or Removing a Configuration to 
 * or from a Profile 
 *
*************************************************************************************/
void onvif_ProfileChangedNotify(ONVIF_PROFILE * p_req, onvif_PropertyOperation op)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/Profile");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = op;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*10;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:Profile token=\"%s\" fixed=\"%s\">",
                    p_req->token, 
                    p_req->fixed ? "true" : "false");
                offset += build_Profile_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:Profile>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a VideoEncoderConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_VideoEncoderConfigurationChangedNotify(onvif_VideoEncoder2Configuration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/VideoEncoderConfiguration");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:VideoEncoderConfiguration token=\"%s\">", 
                    p_req->token);
                offset += build_VideoEncoderConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:VideoEncoderConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a VideoSourceConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_VideoSourceConfigurationChangedNotify(onvif_VideoSourceConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/VideoSourceConfiguration/MediaService");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:VideoSourceConfiguration token=\"%s\">", 
                    p_req->token);
                offset += build_VideoSourceConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:VideoSourceConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a MetadataConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_MetadataConfigurationChangedNotify(onvif_MetadataConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/MetadataConfiguration");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:MetadataConfiguration token=\"%s\" "
                        "CompressionType=\"%s\" GeoLocation=\"%s\" ShapePolygon=\"%s\">\r\n", 
                    p_req->token,
                    p_req->CompressionType,
                    p_req->GeoLocation ? "true" : "false",
                    p_req->ShapePolygon ? "true" : "false");
                offset += build_MetadataConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:MetadataConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

#ifdef DEVICEIO_SUPPORT

/************************************************************************************
 *      
 * Whenever a VideoOutputConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_VideoOutputConfigurationChangedNotify(onvif_VideoOutputConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/VideoOutputConfiguration/MediaService");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:VideoOutputConfiguration token=\"%s\">", 
                    p_req->token);
                offset += build_VideoOutputConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:VideoOutputConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a AudioOutputConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_AudioOutputConfigurationChangedNotify(onvif_AudioOutputConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/AudioOutputConfiguration/MediaService");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:AudioOutputConfiguration token=\"%s\">",
                    p_req->token);
                offset += build_AudioOutputConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:AudioOutputConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

#endif // DEVICEIO_SUPPORT

#ifdef AUDIO_SUPPORT

/************************************************************************************
 *      
 * Whenever an AudioEncoderConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_AudioEncoderConfigurationChangedNotify(onvif_AudioEncoder2Configuration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/AudioEncoderConfiguration");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:AudioEncoderConfiguration token=\"%s\">", 
                    p_req->token);
                offset += build_AudioEncoderConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:AudioEncoderConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

/************************************************************************************
 *      
 * Whenever a AudioSourceConfiguration of a device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_AudioSourceConfigurationChangedNotify(onvif_AudioSourceConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/AudioSourceConfiguration/MediaService");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:AudioSourceConfiguration token=\"%s\">",
                    p_req->token);
                offset += build_AudioSourceConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:AudioSourceConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

#endif // AUDIO_SUPPORT

#ifdef PTZ_SUPPORT

/************************************************************************************
 *      
 * Whenever a PTZConfiguration of a PTZ capable device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_PTZConfigurationChangedNotify(onvif_PTZConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/PTZConfiguration");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:PTZConfiguration token=\"%s\" "
                        "MoveRamp=\"%d\" PresetRamp=\"%d\" PresetTourRamp=\"%d\">", 
                    p_req->token, 
                    p_req->MoveRamp, 
                    p_req->PresetRamp, 
                    p_req->PresetTourRamp);
                offset += build_PTZConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:PTZConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

#endif // PTZ_SUPPORT

#ifdef VIDEO_ANALYTICS

/************************************************************************************
 *      
 * Whenever a VideoAnalyticsConfiguration of device changes the device should 
 * provide the following event
 *
*************************************************************************************/
void onvif_VideoAnalyticsConfigurationChangedNotify(onvif_VideoAnalyticsConfiguration * p_req)
{
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;
    NotificationMessageList * p_message;
    onvif_NotificationMessage * p_msg;
    
    p_message = onvif_add_NotificationMessage(NULL);
    if (p_message)
    {
        p_msg = &p_message->NotificationMessage;
        
        strcpy(p_msg->Dialect, "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
        strcpy(p_msg->Topic, "tns1:Configuration/VideoAnalyticsConfiguration");
        p_msg->Message.PropertyOperationFlag = 1;
        p_msg->Message.PropertyOperation = PropertyOperation_Changed;
        p_msg->Message.UtcTime = time(NULL)+1;

        p_msg->Message.SourceFlag = 1;
        
        p_simpleitem = onvif_add_SimpleItem(&p_msg->Message.Source.SimpleItem);
        if (p_simpleitem)
        {
            strcpy(p_simpleitem->SimpleItem.Name, "Token");
            strcpy(p_simpleitem->SimpleItem.Value, p_req->token);
        }

        p_msg->Message.DataFlag = 1;
        
        p_elementitem = onvif_add_ElementItem(&p_msg->Message.Data.ElementItem);
        if (p_elementitem)
        {
            int buflen = 1024*4;
            
            strcpy(p_elementitem->ElementItem.Name, "Configuration");
            
            p_elementitem->ElementItem.Any = (char *)malloc(buflen);
            if (p_elementitem->ElementItem.Any)
            {
                int offset = 0;
                char * buff = p_elementitem->ElementItem.Any;
                
                memset(buff, 0, buflen);
                
                p_elementitem->ElementItem.AnyFlag = 1;

                offset += snprintf(buff+offset, buflen-offset, 
                    "<tt:VideoAnalyticsConfiguration token=\"%s\">",
                    p_req->token);
                offset += build_VideoAnalyticsConfiguration_xml(buff+offset, buflen-offset, p_req);
                offset += snprintf(buff+offset, buflen-offset, 
                    "</tt:VideoAnalyticsConfiguration>");
            }
        }

        onvif_put_NotificationMessage(p_message);
    }
}

#endif // VIDEO_ANALYTICS

#endif // MEDIA_SUPPORT

/************************************************************************************
 *      
 * @brief
 *  Get snapshot JPEG image data.
 *
 * @param buff data buffer
 * @param rlen [in, out], [in] the buff size, [out] the image data size
 * @param profile_token profile token
 * 
 * @return
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_ServiceNotSupported
 *
*************************************************************************************/
ONVIF_RET onvif_trt_GetSnapshot(char * buff, int * rlen, char * profile_token)
{
    int len;
    FILE * fp;
    ONVIF_PROFILE * p_profile;
    
    onvif_print("onvif_GetSnapshot\r\n");

    p_profile = onvif_find_profile(g_onvif_cfg.profiles, profile_token);
    if (NULL == p_profile || NULL == p_profile->v_src_cfg)
    {
        return ONVIF_ERR_NoProfile;
    }
    
    // todo : here is the test code, just read the image data from file ...

    fp = fopen(g_onvif_cfg.snapshot, "rb");
    if (NULL == fp)
    {
        return ONVIF_ERR_ServiceNotSupported;
    }
    
    fseek(fp, 0, SEEK_END);
    
    len = ftell(fp);
    if (len <= 0)
    {
        fclose(fp);
        return ONVIF_ERR_ServiceNotSupported;
    }
    fseek(fp, 0, SEEK_SET);
    
    if (len > *rlen)
    {
        fclose(fp);
        return ONVIF_ERR_ServiceNotSupported;
    }

    len = (int)fread(buff, 1, len, fp);
    
    fclose(fp);

    *rlen = len;

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brif
 *  Modifies an OSD configuration. 
 *  Running streams using this configuration may be immediately updated 
 *  according to the new settings.
 *
 *  A device shall accept any combination of parameters returned by 
 *  GetOSDOptions. If necessary the device may adapt parameter values 
 *  for FontColor, FontSize, and BackgroundColor elements without 
 *  returning an error.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetOSD(trt_SetOSD_REQ * p_req)
{
    OSDConfigurationList * p_osd = onvif_find_OSDConfiguration(g_onvif_cfg.OSDs, p_req->OSD.token);
    if (NULL == p_osd)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : here add handler code ...


    memcpy(&p_osd->OSD, &p_req->OSD, sizeof(onvif_OSDConfiguration));
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Creates a new OSD configuration with specified values and also make 
 *  the association between the new OSD and an existing VideoSourceConfiguration
 *  identified by the VideoSourceConfigurationToken. Any value required by 
 *  a device for a new OSD configuration that is optional and not present in
 *  the CreateOSD message may be adapted to the appropriate value by the device.
 *  The OSD shall be created in the device and shall be persistent 
 *  (remain after reboot). A device that indicates OSD capability shall support
 *  the creation of OSD as long as the number of existing OSDs does not exceed
 *  the value of MaximumNumberOfOSDs in GetOSDOptions.
 *
 *  When creating a OSDTextConfiguration, if the IsPersistentText attribute
 *  is missing, device shall assume IsPersistentText attribute as true.
 *
 *  A created OSD shall be deletable.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_MaxOSDs
 *
*************************************************************************************/
ONVIF_RET onvif_trt_CreateOSD(trt_CreateOSD_REQ * p_req)
{
    OSDConfigurationList * p_osd = onvif_add_OSDConfiguration(&g_onvif_cfg.OSDs);
    if (NULL == p_osd)
    {
        return ONVIF_ERR_MaxOSDs;
    }

    // return the token
    strcpy(p_req->OSD.token, p_osd->OSD.token);
    
    memcpy(&p_osd->OSD, &p_req->OSD, sizeof(onvif_OSDConfiguration));

    // todo : here add handler code ... 
    
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Deletes an OSD. This change shall always be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *
*************************************************************************************/
ONVIF_RET onvif_trt_DeleteOSD(trt_DeleteOSD_REQ * p_req)
{
    OSDConfigurationList * p_prev;
    OSDConfigurationList * p_osd = onvif_find_OSDConfiguration(g_onvif_cfg.OSDs, p_req->OSDToken);
    if (NULL == p_osd)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : here add handler code ...


    p_prev = g_onvif_cfg.OSDs;
    if (p_osd == p_prev)
    {
        g_onvif_cfg.OSDs = p_osd->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_osd)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_osd->next;
    }

    free(p_osd);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Deletes a profile. This change shall always be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_DeletionOfFixedProfile
 *
*************************************************************************************/
ONVIF_RET onvif_trt_DeleteProfile(trt_DeleteProfile_REQ * p_req)
{
    ONVIF_PROFILE * p_prev;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->fixed)
    {
        return ONVIF_ERR_DeletionOfFixedProfile;
    }

    p_prev = g_onvif_cfg.profiles;
    if (p_profile == p_prev)
    {
        g_onvif_cfg.profiles = p_profile->next;
    }
    else
    {
        while (p_prev->next)
        {
            if (p_prev->next == p_profile)
            {
                break;
            }

            p_prev = p_prev->next;
        }

        p_prev->next = p_profile->next;
    }

#ifdef MEDIA_SUPPORT
    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Deleted);
#endif

    if (p_profile->v_src_cfg && p_profile->v_src_cfg->Configuration.UseCount > 0)
    {
        p_profile->v_src_cfg->Configuration.UseCount--;
    }
    
    if (p_profile->v_enc_cfg && p_profile->v_enc_cfg->Configuration.UseCount > 0)
    {
        p_profile->v_enc_cfg->Configuration.UseCount--;
    }

#ifdef AUDIO_SUPPORT
    if (p_profile->a_src_cfg && p_profile->a_src_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_src_cfg->Configuration.UseCount--;
    }

    if (p_profile->a_enc_cfg && p_profile->a_enc_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_enc_cfg->Configuration.UseCount--;
    }
#endif

#ifdef PTZ_SUPPORT
    if (p_profile->ptz_cfg && p_profile->ptz_cfg->Configuration.UseCount > 0)
    {
        p_profile->ptz_cfg->Configuration.UseCount--;
    }
#endif

    if (p_profile->multicasting)
    {
        // todo : stop multicast streaming ...
    }

    free(p_profile);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a video source configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. Running streams using this configuration may be 
 *  immediately updated according to the new settings. The changes are not 
 *  guaranteed to take effect unless the client requests a new stream URI 
 *  and restarts any affected stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetVideoSourceConfiguration(trt_SetVideoSourceConfiguration_REQ * p_req)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    VideoSourceList * p_v_src;
    VideoSourceConfigurationList * p_v_src_cfg = onvif_find_VideoSourceConfiguration(p_config->v_src_cfg, p_req->Configuration.token);
    if (NULL == p_v_src_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }
    
    p_v_src = onvif_find_VideoSource(p_config->v_src, p_req->Configuration.SourceToken);
    if (NULL == p_v_src)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (/*p_req->Configuration.Bounds.x < p_config->VideoSourceConfigurationOptions.BoundsRange.XRange.Min || */
        p_req->Configuration.Bounds.x > p_v_src_cfg->Options.BoundsRange.XRange.Max ||
        /*p_req->Configuration.Bounds.y < p_config->VideoSourceConfigurationOptions.BoundsRange.YRange.Min || */
        p_req->Configuration.Bounds.y > p_v_src_cfg->Options.BoundsRange.YRange.Max ||
        /*p_req->Configuration.Bounds.width < p_config->VideoSourceConfigurationOptions.BoundsRange.WidthRange.Min || */
        p_req->Configuration.Bounds.width > p_v_src_cfg->Options.BoundsRange.WidthRange.Max ||
        /*p_req->Configuration.Bounds.height < p_config->VideoSourceConfigurationOptions.BoundsRange.HeightRange.Min || */
        p_req->Configuration.Bounds.height > p_v_src_cfg->Options.BoundsRange.HeightRange.Max)
    {
        return ONVIF_ERR_ConfigModify;
    }

    p_v_src_cfg->Configuration.Bounds.x = p_req->Configuration.Bounds.x;
    p_v_src_cfg->Configuration.Bounds.y = p_req->Configuration.Bounds.y;
    p_v_src_cfg->Configuration.Bounds.width = p_req->Configuration.Bounds.width;
    p_v_src_cfg->Configuration.Bounds.height = p_req->Configuration.Bounds.height;

    strcpy(p_v_src_cfg->Configuration.Name, p_req->Configuration.Name);
    strcpy(p_v_src_cfg->Configuration.SourceToken, p_req->Configuration.SourceToken);

    if (p_req->Configuration.ExtensionFlag)
    {
        memcpy(&p_v_src_cfg->Configuration.Extension, &p_req->Configuration.Extension, sizeof(onvif_VideoSourceConfigurationExtension));
    }

#ifdef MEDIA_SUPPORT
    onvif_VideoSourceConfigurationChangedNotify(&p_v_src_cfg->Configuration);
#endif

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a metadata configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. Changes in the Multicast settings shall always be 
 *  persistent. Running streams using this configuration may be updated
 *  immediately according to the new settings. The changes are not guaranteed 
 *  to take effect unless the client requests a new stream URI and restarts
 *  any affected streams.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetMetadataConfiguration(trt_SetMetadataConfiguration_REQ * p_req)
{
    MetadataConfigurationList * p_cfg = onvif_find_MetadataConfiguration(g_onvif_cfg.metadata_cfg, p_req->Configuration.token);
    if (NULL == p_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_req->Configuration.SessionTimeout <= 0)
    {
        return ONVIF_ERR_ConfigModify;
    }

    strcpy(p_cfg->Configuration.Name, p_req->Configuration.Name);
    p_cfg->Configuration.SessionTimeout = p_req->Configuration.SessionTimeout;

    p_cfg->Configuration.AnalyticsFlag = p_req->Configuration.AnalyticsFlag;
    p_cfg->Configuration.Analytics = p_req->Configuration.Analytics;

    p_cfg->Configuration.PTZStatusFlag = p_req->Configuration.PTZStatusFlag;
    memcpy(&p_cfg->Configuration.PTZStatus, &p_req->Configuration.PTZStatus, sizeof(onvif_PTZFilter));

    p_cfg->Configuration.EventsFlag = p_req->Configuration.EventsFlag;
    memcpy(&p_cfg->Configuration.Events, &p_req->Configuration.Events, sizeof(onvif_EventSubscription));

    memcpy(&p_cfg->Configuration.Multicast, &p_req->Configuration.Multicast, sizeof(onvif_MulticastConfiguration));

#ifdef MEDIA_SUPPORT
    onvif_MetadataConfigurationChangedNotify(&p_cfg->Configuration);
#endif

    return ONVIF_OK;
}

#ifdef AUDIO_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Modifies an audio source configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after
 *  reboot of the device. Running streams using this configuration may be 
 *  immediately updated according to the new settings, but the changes are 
 *  not guaranteed to take effect unless the client requests a new stream 
 *  URI and restarts any affected stream. If the new settings invalidate 
 *  any parameters already negotiated using RTSP, for example by changing 
 *  codec type, the device must not apply these settings to existing streams.
 *  Instead it must either continue to stream using the old settings or stop 
 *  sending data on the affected streams.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetAudioSourceConfiguration(trt_SetAudioSourceConfiguration_REQ * p_req)
{
    AudioSourceList * p_a_src;
    AudioSourceConfigurationList * p_a_src_cfg = onvif_find_AudioSourceConfiguration(g_onvif_cfg.a_src_cfg, p_req->Configuration.token);
    if (NULL == p_a_src_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }
    
    p_a_src = onvif_find_AudioSource(g_onvif_cfg.a_src, p_req->Configuration.SourceToken);
    if (NULL == p_a_src)
    {
        return ONVIF_ERR_NoConfig;
    }

    strcpy(p_a_src_cfg->Configuration.Name, p_req->Configuration.Name);

#ifdef MEDIA_SUPPORT
    onvif_AudioSourceConfigurationChangedNotify(&p_a_src_cfg->Configuration);
#endif

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies an audio decoder configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetAudioDecoderConfiguration(trt_SetAudioDecoderConfiguration_REQ * p_req)
{
    AudioDecoderConfigurationList * p_a_dec_cfg = onvif_find_AudioDecoderConfiguration(g_onvif_cfg.a_dec_cfg, p_req->Configuration.token);
    if (NULL == p_a_dec_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : add set audio decoder code ...

    strcpy(p_a_dec_cfg->Configuration.Name, p_req->Configuration.Name);
    
    return ONVIF_OK;
}

#endif // end of AUDIO_SUPPORT

#endif // end of defined(MEDIA_SUPPORT) || defined(MEDIA2_SUPPORT)

#ifdef MEDIA_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Creates a new empty media profile.
 *  The media profile shall be created in the device and shall be persistent
 *  (remain after reboot). A device shall support the creation of media profiles
 *  as long as the number of existing profiles does not exceed the capability 
 *  value MaximumNumberOfProfiles.
 *
 *  A created profile shall be deletable and a device shall set the "fixed" 
 *  attribute to false in the returned Profile.
 *
 *  Optionally the token identifier can be defined by the client. In this case 
 *  a device shall support at least a token length of 12 characters and characters
 *  "A-Z" | "a-z" | "0-9" | "-.".
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_ProfileExists
 *  ONVIF_ERR_MaxNVTProfiles
 *
*************************************************************************************/
ONVIF_RET onvif_trt_CreateProfile(trt_CreateProfile_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = NULL;

    if (p_req->TokenFlag && p_req->Token[0] != '\0')
    {
        p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->Token);
        if (p_profile)
        {
            return ONVIF_ERR_ProfileExists;
        }
    }
    
    p_profile = onvif_add_profile(&g_onvif_cfg.profiles, FALSE);
    if (p_profile)
    {
        strcpy(p_profile->name, p_req->Name);
        
        if (p_req->TokenFlag && p_req->Token[0] != '\0')
        {
            strcpy(p_profile->token, p_req->Token);
        }
        else
        {
            strcpy(p_req->Token, p_profile->token);
        }
    }
    else 
    {
        return ONVIF_ERR_MaxNVTProfiles;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Initialized);
    
    return ONVIF_OK;
}

/************************************************************************************
 * 
 * @brief
 *  Adds a VideoSourceConfiguration to an existing media profile. 
 *  If such a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddVideoSourceConfiguration(trt_AddVideoSourceConfiguration_REQ * p_req)
{
    VideoSourceConfigurationList * p_v_src_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_v_src_cfg = onvif_find_VideoSourceConfiguration(g_onvif_cfg.v_src_cfg, p_req->ConfigurationToken);
    if (NULL == p_v_src_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->v_src_cfg != p_v_src_cfg)
    {
        if (p_profile->v_src_cfg && p_profile->v_src_cfg->Configuration.UseCount > 0)
        {
            p_profile->v_src_cfg->Configuration.UseCount--;
        }
        
        p_v_src_cfg->Configuration.UseCount++;
        
        p_profile->v_src_cfg = p_v_src_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Adds a VideoEncoderConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  A device shall support adding a compatible VideoEncoderconfiguration 
 *  to a Profile containing a VideoSourceConfiguration and shall support 
 *  streaming video data of such a Profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddVideoEncoderConfiguration(trt_AddVideoEncoderConfiguration_REQ * p_req)
{
    VideoEncoder2ConfigurationList * p_v_enc_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_v_enc_cfg = onvif_find_VideoEncoder2Configuration(g_onvif_cfg.v_enc_cfg, p_req->ConfigurationToken);
    if (NULL == p_v_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->v_enc_cfg != p_v_enc_cfg)
    {
        if (p_profile->v_enc_cfg && p_profile->v_enc_cfg->Configuration.UseCount > 0)
        {
            p_profile->v_enc_cfg->Configuration.UseCount--;
        }
        
        p_v_enc_cfg->Configuration.UseCount++;
        
        p_profile->v_enc_cfg = p_v_enc_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes a VideoEncoderConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoEncoderConfiguration, 
 *  the operation has no effect.The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveVideoEncoderConfiguration(trt_RemoveVideoEncoderConfiguration_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->v_enc_cfg && p_profile->v_enc_cfg->Configuration.UseCount > 0)
    {
        p_profile->v_enc_cfg->Configuration.UseCount--;
    }
    
    p_profile->v_enc_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes a VideoSourceConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoSourceConfiguration, 
 *  the operation has no effect.The removal shall be persistent.
 *
 *  Video source configurations should only be removed after removing a 
 *  VideoEncoderConfiguration from the media profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveVideoSourceConfiguration(trt_RemoveVideoSourceConfiguration_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->v_enc_cfg)
    {
        return ONVIF_ERR_ConfigurationConflict;
    }

    if (p_profile->v_src_cfg && p_profile->v_src_cfg->Configuration.UseCount > 0)
    {
        p_profile->v_src_cfg->Configuration.UseCount--;
    }
    
    p_profile->v_src_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies a video encoder configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. Changes in the Multicast settings shall always 
 *  be persistent. Running streams using this configuration may be immediately
 *  updated according to the new settings, but the changes are not guaranteed 
 *  to take effect unless the client requests a new stream URI and restarts 
 *  any affected stream. If the new settings invalidate any parameters already 
 *  negotiated using RTSP, for example by changing codec type, the device must
 *  not apply these settings to existing streams. Instead it must either 
 *  continue to stream using the old settings or stop sending data on the 
 *  affected streams.
 *
 *  A device shall accept any combination of parameters that it returned in the 
 *  GetVideoEncoderConfigurationOptionsResponse. If necessary the device may 
 *  adapt parameter values for Quality and RateControl elements without returning
 *  an error. A device shall adapt an out of range BitrateLimit instead of 
 *  returning a fault
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetVideoEncoderConfiguration(trt_SetVideoEncoderConfiguration_REQ * p_req)
{
    int i = 0;
    onvif_VideoResolution * p_VideoResolution;
    VideoEncoder2ConfigurationList * p_v_enc_cfg;

    p_v_enc_cfg = onvif_find_VideoEncoder2Configuration(g_onvif_cfg.v_enc_cfg, p_req->Configuration.token);
    if (NULL == p_v_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_req->Configuration.Quality < p_v_enc_cfg->Options.QualityRange.Min || 
        p_req->Configuration.Quality > p_v_enc_cfg->Options.QualityRange.Max )
    {
        return ONVIF_ERR_ConfigModify;
    }

    if (VideoEncoding_MPEG4 == p_req->Configuration.Encoding)
    {
        if (p_req->Configuration.MPEG4.GovLength < p_v_enc_cfg->Options.MPEG4.GovLengthRange.Min ||
            p_req->Configuration.MPEG4.GovLength > p_v_enc_cfg->Options.MPEG4.GovLengthRange.Max)
        {
            return ONVIF_ERR_ConfigModify;
        }

        p_VideoResolution = p_v_enc_cfg->Options.MPEG4.ResolutionsAvailable;
    }
    else if (VideoEncoding_H264 == p_req->Configuration.Encoding)
    {
        if (p_req->Configuration.H264.GovLength < p_v_enc_cfg->Options.H264.GovLengthRange.Min ||
            p_req->Configuration.H264.GovLength > p_v_enc_cfg->Options.H264.GovLengthRange.Max)
        {
            return ONVIF_ERR_ConfigModify;
        }

        p_VideoResolution = p_v_enc_cfg->Options.H264.ResolutionsAvailable;
    }
    else
    {
        p_VideoResolution = p_v_enc_cfg->Options.JPEG.ResolutionsAvailable;
    }

    for (i = 0; i < MAX_RES_NUMS; i++)
    {
        if (p_VideoResolution[i].Width == p_req->Configuration.Resolution.Width && 
            p_VideoResolution[i].Height == p_req->Configuration.Resolution.Height)
        {
            break;
        }
    }

    if (i == MAX_RES_NUMS)
    {
        return ONVIF_ERR_ConfigModify;
    }

    p_v_enc_cfg->Configuration.Resolution.Width = p_req->Configuration.Resolution.Width;
    p_v_enc_cfg->Configuration.Resolution.Height = p_req->Configuration.Resolution.Height;
    p_v_enc_cfg->Configuration.Quality = (float)p_req->Configuration.Quality;
    p_v_enc_cfg->Configuration.SessionTimeout = p_req->Configuration.SessionTimeout;
    p_v_enc_cfg->Configuration.VideoEncoding = p_req->Configuration.Encoding;

    if (VideoEncoding_MPEG4 == p_req->Configuration.Encoding)
    {
        strcpy(p_v_enc_cfg->Configuration.Encoding, "MP4V-ES");
        p_v_enc_cfg->Configuration.GovLength = p_req->Configuration.MPEG4.GovLength;
        strcpy(p_v_enc_cfg->Configuration.Profile, onvif_Mpeg4ProfileToString(p_req->Configuration.MPEG4.Mpeg4Profile));
    }
    else if (VideoEncoding_H264 == p_req->Configuration.Encoding)
    {
        strcpy(p_v_enc_cfg->Configuration.Encoding, "H264");
        p_v_enc_cfg->Configuration.GovLength = p_req->Configuration.H264.GovLength;
        strcpy(p_v_enc_cfg->Configuration.Profile, onvif_H264ProfileToString(p_req->Configuration.H264.H264Profile));
    }    
    else if (VideoEncoding_JPEG == p_req->Configuration.Encoding)
    {
        strcpy(p_v_enc_cfg->Configuration.Encoding, "JPEG");
    }

    if (p_req->Configuration.RateControlFlag)
    {
        p_v_enc_cfg->Configuration.RateControl.FrameRateLimit = (float)p_req->Configuration.RateControl.FrameRateLimit;
        p_v_enc_cfg->Configuration.RateControl.EncodingInterval = p_req->Configuration.RateControl.EncodingInterval;
        p_v_enc_cfg->Configuration.RateControl.BitrateLimit = p_req->Configuration.RateControl.BitrateLimit;
    }

    memcpy(&p_v_enc_cfg->Configuration.Multicast, &p_req->Configuration.Multicast, sizeof(onvif_MulticastConfiguration));

    onvif_VideoEncoderConfigurationChangedNotify(&p_v_enc_cfg->Configuration);
    
    // todo : here add handler code ...
    
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Returns the available parameters and their valid ranges to the client. 
 *  Any combination of the parameters obtained using a given media profile
 *  and video encoder configuration shall be a valid input for the 
 *  SetVideoEncoderConfiguration command.
 *
 *  If a video encoder configuration token is provided, the device shall 
 *  return the options compatible with that configuration. If a media profile
 *  token is specified, the device shall return the options compatible with
 *  that media profile. If both a media profile token and a video encoder 
 *  configuration token are specified, the device shall return the options 
 *  compatible with both that media profile and that configuration. 
 *  If no tokens are specified, the options shall be considered generic for 
 *  the device.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *
*************************************************************************************/
ONVIF_RET onvif_trt_GetVideoEncoderConfigurationOptions(trt_GetVideoEncoderConfigurationOptions_REQ * p_req, trt_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    ONVIF_PROFILE * p_profile = NULL;
    VideoEncoder2ConfigurationList * p_v_enc_cfg = NULL;
    
    if (p_req->ProfileTokenFlag && p_req->ProfileToken[0] != '\0')
    {
        p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
        if (NULL == p_profile)
        {
            return ONVIF_ERR_NoProfile;
        }

        p_v_enc_cfg = p_profile->v_enc_cfg;
    }

    if (p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        p_v_enc_cfg = onvif_find_VideoEncoder2Configuration(g_onvif_cfg.v_enc_cfg, p_req->ConfigurationToken);
        if (NULL == p_v_enc_cfg)
        {
            return ONVIF_ERR_NoConfig;
        }
    }

    if (NULL == p_v_enc_cfg)
    {
        p_v_enc_cfg = g_onvif_cfg.v_enc_cfg;
    }
    
    if (NULL == p_v_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : Fill p_res ...

    memcpy(&p_res->Options, &p_v_enc_cfg->Options, sizeof(onvif_VideoEncoderConfigurationOptions));

    return ONVIF_OK;
}

int onvif_trt_BuildStreamParams(trt_GetStreamUri_REQ * p_req, ONVIF_PROFILE * p_profile, char * buff, int len)
{
    int offset = 0;
    
    if (StreamType_RTP_Unicast == p_req->StreamSetup.Stream)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "unicast");
    }
    else if (StreamType_RTP_Multicast == p_req->StreamSetup.Stream)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;t=%s", "multicast");
    }

    if (TransportProtocol_UDP == p_req->StreamSetup.Transport.Protocol)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "udp");
    }
    else if (TransportProtocol_TCP == p_req->StreamSetup.Transport.Protocol)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "tcp");
    }
    else if (TransportProtocol_RTSP == p_req->StreamSetup.Transport.Protocol)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "rtsp");
    }
    else if (TransportProtocol_HTTP == p_req->StreamSetup.Transport.Protocol)
    {
        offset += snprintf(buff+offset, len-offset, "&amp;p=%s", "http");
    }

    /**
     * If the audio and video parameters have been set to the encoder 
     * in the onvif_trt_SetVideoEncoderConfiguration function, 
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

    if (p_profile->a_dec_cfg)
    {
        char encoding[8];
        
        if (p_profile->a_dec_cfg->Options.AACDecOptionsFlag)
        {
            strcpy(encoding, "AAC");
        }
        else if (p_profile->a_dec_cfg->Options.G726DecOptionsFlag)
        {
            strcpy(encoding, "G726");
        }
        else
        {
            strcpy(encoding, "G711");
        }
        
        offset += snprintf(buff+offset, len-offset, "&amp;bce=%s", encoding);
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
 *  indefinitely even if the profile is changed. The InvalidAfterConnect,
 *  InvalidAfterReboot and Timeout Parameter should be set accordingly 
 *  (InvalidAfterConnect=false, InvalidAfterReboot=false, timeout=PT0S).
 *
 *  If a multicast stream is requested at least one of VideoEncoderConfiguration, 
 *  AudioEncoderConfiguration and MetadataConfiguration shall have a valid 
 *  multicast setting.
 *  
 *  For full compatibility with other ONVIF services a device should not 
 *  generate Uris longer than 128 octets.
 *
 *  On a request for transport protocol http a device shall return a url that 
 *  uses the same port as the web service. This enables seamless NAT traversal.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_InvalidStreamSetup
 *  ONVIF_ERR_StreamConflict
 *  ONVIF_ERR_IncompleteConfiguration
 *  ONVIF_ERR_InvalidMulticastSettings
 *
*************************************************************************************/
ONVIF_RET onvif_trt_GetStreamUri(HTTPCLN * p_user, trt_GetStreamUri_REQ * p_req, trt_GetStreamUri_RES * p_res)
{
    int offset = 0;
    int len = sizeof(p_res->MediaUri.Uri);
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
        
        if (p_req->StreamSetup.Transport.Protocol == TransportProtocol_HTTP)
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
                offset += snprintf(p_res->MediaUri.Uri, len, "%s://[%s]:%u/%s", proto, sip, port, RTSP_URL_SUFFIX);
            }
            else
            {
                offset += snprintf(p_res->MediaUri.Uri, len, "%s://%s:%u/%s", proto, sip, port, RTSP_URL_SUFFIX);
            }
        }
        else
        {
            port = g_onvif_cls.rtsp_port;
            
            if (p_user->sockaddr.ipv6_flag)
            {
                offset += snprintf(p_res->MediaUri.Uri, len, "rtsp://[%s]:%u/%s", sip, port, RTSP_URL_SUFFIX);
            }
            else
            {
                offset += snprintf(p_res->MediaUri.Uri, len, "rtsp://%s:%u/%s", sip, port, RTSP_URL_SUFFIX);
            }
        }

        onvif_trt_BuildStreamParams(p_req, p_profile, p_res->MediaUri.Uri+offset, len-offset);
    }
    else
    {
        /**
         * If the <stream_uri> node value under the <profile> node in the 
         * configuration file is set, the rtsp url is used
         *
         */
         
        offset += snprintf(p_res->MediaUri.Uri, len, "%s", p_profile->stream_uri);

        if (p_profile->append_params)
        {
            onvif_trt_BuildStreamParams(p_req, p_profile, p_res->MediaUri.Uri+offset, len-offset);
        }
    }

    p_res->MediaUri.InvalidAfterConnect = FALSE;
    p_res->MediaUri.InvalidAfterReboot = FALSE;
    p_res->MediaUri.Timeout = 60;

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Starts multicast streaming using a specified media profile of a device. 
 *  Streaming continues until StopMulticastStreaming is called for the same Profile. 
 *  The streaming shall continue after a reboot of the device until a 
 *  StopMulticastStreaming request is received. The multicast address, port and TTL 
 *  are configured in the VideoEncoderConfiguration, AudioEncoderConfiguration
 *  and MetadataConfiguration respectively.
 *
 *  Multicast streaming may stop when the corresponding profile is deleted 
 *  or one of its Configurations is altered via one of the set configuration 
 *  methods.
 *
 *  The implementation shall ensure that the RTP stream can be decoded without
 *  setting up an RTSP control connection. 
 *  Especially in case of H.264 video, the SPS/PPS header shall be sent inband.
 *
 * @returnn
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_trt_StartMulticastStreaming(const char * token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
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
 *  Stop multicast streaming using a specified media profile of a device. 
 *  In case that a device receives the StopMulticastStreaming request whose 
 *  corresponding multicast streaming is not started, the device should 
 *  reply with successful StopMulticastStreamingResponse.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_trt_StopMulticastStreaming(const char * token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    // todo : stop multicast streaming ...

    p_profile->multicasting = FALSE;
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  adds a Metadata configuration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  Adding a MetadataConfiguration to a Profile means that streams using 
 *  that profile contain metadata. Metadata can consist of events, PTZ status, 
 *  and/or video analytics data.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddMetadataConfiguration(trt_AddMetadataConfiguration_REQ * p_req)
{
    MetadataConfigurationList * p_metadata_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_metadata_cfg = onvif_find_MetadataConfiguration(g_onvif_cfg.metadata_cfg, p_req->ConfigurationToken);
    if (NULL == p_metadata_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->metadata_cfg != p_metadata_cfg)
    {
        if (p_profile->metadata_cfg && p_profile->metadata_cfg->Configuration.UseCount > 0)
        {
            p_profile->metadata_cfg->Configuration.UseCount--;
        }
        
        p_metadata_cfg->Configuration.UseCount++;
        
        p_profile->metadata_cfg = p_metadata_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes a MetadataConfiguration from an existing media profile. 
 *  If the media profile does not contain a MetadataConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveMetadataConfiguration(const char * profile_token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, profile_token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->metadata_cfg && p_profile->metadata_cfg->Configuration.UseCount > 0)
    {
        p_profile->metadata_cfg->Configuration.UseCount--;
    }
    
    p_profile->metadata_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Changes the media profile structure relating to video source for the
 *  specified video source mode. A device that indicates a capability of 
 *  VideoSourceMode shall support this command. The behavior after changing 
 *  the mode is not defined in this specification.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoVideoSource
 *  ONVIF_ERR_NoVideoSourceMode
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetVideoSourceMode(trt_SetVideoSourceMode_REQ * p_req, trt_SetVideoSourceMode_RES * p_res)
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

    // todo : handler set video source mode ...
    

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Obtain a JPEG snhapshot from the device. 
 *  The returned URI shall remain valid indefinitely even if the profile 
 *  is changed. The ValidUntilConnect, ValidUntilReboot and Timeout 
 *  Parameter shall be set accordingly 
 *  (ValidUntilConnect=false, ValidUntilReboot=false, timeout=PT0S).
 *  The URI can be used for acquiring a JPEG image through a HTTP GET operation.
 *
 *  The image encoding will always be JPEG regardless of the encoding setting 
 *  in the media profile. The JPEG settings (like resolution or quality) should
 *  be taken from the profile if suitable. The provided image shall be updated 
 *  automatically and independent from calls to GetSnapshotUri.
 *
 *  A device supporting the media service should support this command. 
 *  A device shall support this command when the SnapshotUri capability is 
 *  set to true.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_IncompleteConfiguration
 *
*************************************************************************************/
ONVIF_RET onvif_trt_GetSnapshotUri(HTTPCLN * p_user, trt_GetSnapshotUri_REQ * p_req, trt_GetSnapshotUri_RES * p_res)
{
    uint16 port;
    char proto[16];
    char sip[64];
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

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

    // set the media uri

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->MediaUri.Uri, sizeof(p_res->MediaUri.Uri), 
            "%s://[%s]:%u/snapshot/%s", 
            proto, sip, port, p_profile->token);
    }
    else
    {
        snprintf(p_res->MediaUri.Uri, sizeof(p_res->MediaUri.Uri), 
            "%s://%s:%u/snapshot/%s", 
            proto, sip, port, p_profile->token);
    }

    p_res->MediaUri.InvalidAfterConnect = FALSE;
    p_res->MediaUri.InvalidAfterReboot = FALSE;
    p_res->MediaUri.Timeout = 60;

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Synchronization points allow clients to decode and correctly use all data 
 *  after the synchronization point.
 *
 *  For example, if a video stream is configured with a large I-frame distance 
 *  and a client loses a single packet, the client does not display video until 
 *  the next I-frame is transmitted. In such cases, the client can request a
 *  Synchronization Point which enforces the device to add an I-frame as soon as 
 *  possible. Clients can request Synchronization Points for profiles.The device 
 *  shall add synchronization points for all streams associated with this profile.
 *
 *  Similarly, a synchronization point is used to get an update on full PTZ or 
 *  event status through the metadata stream.
 *
 *  If a video stream is associated with the profile, an I-frame shall be added 
 *  to this video stream.
 *  If a PTZ metadata stream is associated to the profile, the PTZ position 
 *  shall be repeated within the metadata stream.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetSynchronizationPoint(trt_SetSynchronizationPoint_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    
    // todo : here add handler code ...
    
    return ONVIF_OK;
}

#ifdef VIDEO_ANALYTICS

/************************************************************************************
 *
 * @brief
 *  adds a VideoAnalytics configuration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  Adding a VideoAnalyticsConfiguration to a media profile means that 
 *  streams using that media profile can contain video analytics data 
 *  (in the metadata) as defined by the submitted configuration reference.
 *
 *  A profile containing only a video analytics configuration but no video 
 *  source configuration is incomplete. Therefore, a client should first 
 *  add a video source configuration to a profile before adding a video 
 *  analytics configuration. The device can deny adding of a video analytics 
 *  configuration before a video source configuration. 
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddVideoAnalyticsConfiguration(trt_AddVideoAnalyticsConfiguration_REQ * p_req)
{
    VideoAnalyticsConfigurationList * p_va_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_va_cfg = onvif_find_VideoAnalyticsConfiguration(g_onvif_cfg.va_cfg, p_req->ConfigurationToken);
    if (NULL == p_va_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->va_cfg != p_va_cfg)
    {
        if (p_profile->va_cfg && p_profile->va_cfg->Configuration.UseCount > 0)
        {
            p_profile->va_cfg->Configuration.UseCount--;
        }
        
        p_va_cfg->Configuration.UseCount++;
        
        p_profile->va_cfg = p_va_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    // todo : add video analytics configuration code ...
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes a VideoAnalyticsConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoAnalyticsConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveVideoAnalyticsConfiguration(trt_RemoveVideoAnalyticsConfiguration_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->va_cfg && p_profile->va_cfg->Configuration.UseCount > 0)
    {
        p_profile->va_cfg->Configuration.UseCount--;
    }
    
    p_profile->va_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    // todo : remove video analytics configuration code ...
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  A video analytics configuration is modified using this command. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device or not. Running streams using this configuration
 *  shall be immediately updated according to the new settings. Otherwise
 *  inconsistencies can occur between the scene description processed by 
 *  the rule engine and the notifications produced by analytics engine and
 *  rule engine which reference the very same video analytics configuration
 *  token.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict 
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetVideoAnalyticsConfiguration(trt_SetVideoAnalyticsConfiguration_REQ * p_req)
{
    VideoAnalyticsConfigurationList * p_va_cfg;

    p_va_cfg = onvif_find_VideoAnalyticsConfiguration(g_onvif_cfg.va_cfg, p_req->Configuration.token);
    if (NULL == p_va_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    // check the configuration parameters ...

    // save the video analytics configuration 
    strcpy(p_va_cfg->Configuration.Name, p_req->Configuration.Name);
    
    onvif_free_Configs(&p_va_cfg->Configuration.AnalyticsEngineConfiguration.AnalyticsModule);
    onvif_free_Configs(&p_va_cfg->Configuration.RuleEngineConfiguration.Rule);

    p_va_cfg->Configuration.AnalyticsEngineConfiguration.AnalyticsModule = p_req->Configuration.AnalyticsEngineConfiguration.AnalyticsModule;
    p_va_cfg->Configuration.RuleEngineConfiguration.Rule = p_req->Configuration.RuleEngineConfiguration.Rule;

    onvif_VideoAnalyticsConfigurationChangedNotify(&p_va_cfg->Configuration);
    
    // todo : set video analytics configuration code ...

    
    return ONVIF_OK;
}

#endif // VIDEO_ANALYTICS

#ifdef AUDIO_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Adds an AudioSourceConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddAudioSourceConfiguration(trt_AddAudioSourceConfiguration_REQ * p_req)
{
    AudioSourceConfigurationList * p_a_src_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_a_src_cfg = onvif_find_AudioSourceConfiguration(g_onvif_cfg.a_src_cfg, p_req->ConfigurationToken);
    if (NULL == p_a_src_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->a_src_cfg != p_a_src_cfg)
    {
        if (p_profile->a_src_cfg && p_profile->a_src_cfg->Configuration.UseCount > 0)
        {
            p_profile->a_src_cfg->Configuration.UseCount--;
        }
        
        p_a_src_cfg->Configuration.UseCount++;
        
        p_profile->a_src_cfg = p_a_src_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Adds an AudioEncoderConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  A device shall support adding a compatible AudioEncoderConfiguration 
 *  to a Profile containing an AudioSourceConfiguration and shall support 
 *  streaming audio data of such a Profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddAudioEncoderConfiguration(trt_AddAudioEncoderConfiguration_REQ * p_req)
{
    AudioEncoder2ConfigurationList * p_a_enc_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_a_enc_cfg = onvif_find_AudioEncoder2Configuration(g_onvif_cfg.a_enc_cfg, p_req->ConfigurationToken);
    if (NULL == p_a_enc_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->a_enc_cfg != p_a_enc_cfg)
    {
        if (p_profile->a_enc_cfg && p_profile->a_enc_cfg->Configuration.UseCount > 0)
        {
            p_profile->a_enc_cfg->Configuration.UseCount--;
        }
        
        p_a_enc_cfg->Configuration.UseCount++;
        
        p_profile->a_enc_cfg = p_a_enc_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes an AudioEncoderConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioEncoderConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveAudioEncoderConfiguration(const char * token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->a_enc_cfg && p_profile->a_enc_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_enc_cfg->Configuration.UseCount--;
    }
    
    p_profile->a_enc_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes an AudioSourceConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioSourceConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 *  Audio source configurations should only be removed after removing an 
 *  AudioEncoderConfiguration from the media profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveAudioSourceConfiguration(const char * token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->a_enc_cfg)
    {
        return ONVIF_ERR_ConfigurationConflict;
    }
    
    if (p_profile->a_src_cfg && p_profile->a_src_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_src_cfg->Configuration.UseCount--;
    }
    
    p_profile->a_src_cfg= NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Modifies an audio encoder configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. Changes in the Multicast settings shall always be 
 *  persistent. Running streams using this configuration may be immediately 
 *  updated according to the new settings. The changes are not guaranteed to
 *  take effect unless the client requests a new stream URI and restarts any
 *  affected streams.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_SetAudioEncoderConfiguration(trt_SetAudioEncoderConfiguration_REQ * p_req)
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
    p_a_enc_cfg->Configuration.AudioEncoding = p_req->Configuration.Encoding;

    if (AudioEncoding_G711 == p_req->Configuration.Encoding)
    {
        strcpy(p_a_enc_cfg->Configuration.Encoding, "PCMU");
    }
    else if (AudioEncoding_G726 == p_req->Configuration.Encoding)
    {
        strcpy(p_a_enc_cfg->Configuration.Encoding, "G726");
    }
    else if (AudioEncoding_AAC == p_req->Configuration.Encoding)
    {
        strcpy(p_a_enc_cfg->Configuration.Encoding, "MP4A-LATM");
    }

    memcpy(&p_a_enc_cfg->Configuration.Multicast, &p_req->Configuration.Multicast, sizeof(onvif_MulticastConfiguration));

    onvif_AudioEncoderConfigurationChangedNotify(&p_a_enc_cfg->Configuration);
    
    // todo : add set audio encoder code ...

    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Adds an AudioDecoderConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it shall be replaced. 
 *  The change shall be persistent. 
 *
 *  An device that signals support for Audio outputs via its Device IO 
 *  AudioOutputs capability shall support the addition of an audio decoder 
 *  configuration to a profile
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddAudioDecoderConfiguration(trt_AddAudioDecoderConfiguration_REQ * p_req)
{
    AudioDecoderConfigurationList * p_a_dec_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_a_dec_cfg = onvif_find_AudioDecoderConfiguration(g_onvif_cfg.a_dec_cfg, p_req->ConfigurationToken);
    if (NULL == p_a_dec_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->a_dec_cfg != p_a_dec_cfg)
    {
        if (p_profile->a_dec_cfg && p_profile->a_dec_cfg->Configuration.UseCount > 0)
        {
            p_profile->a_dec_cfg->Configuration.UseCount--;
        }
        
        p_a_dec_cfg->Configuration.UseCount++;
        
        p_profile->a_dec_cfg = p_a_dec_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes an AudioDecoderConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioDecoderConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveAudioDecoderConfiguration(trt_RemoveAudioDecoderConfiguration_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->a_dec_cfg && p_profile->a_dec_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_dec_cfg->Configuration.UseCount--;
    }
    
    p_profile->a_dec_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

#endif // end of AUDIO_SUPPORT

#ifdef PTZ_SUPPORT

/************************************************************************************
 *
 * @brief
 *  Adds a PTZConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  Adding a PTZConfiguration to a media profile means that streams using 
 *  that media profile can contain PTZ status (in the metadata), and that 
 *  the media profile can be used for controlling PTZ movement.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddPTZConfiguration(trt_AddPTZConfiguration_REQ * p_req)
{
    PTZConfigurationList * p_ptz_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_ptz_cfg = onvif_find_PTZConfiguration(g_onvif_cfg.ptz_cfg, p_req->ConfigurationToken);
    if (NULL == p_ptz_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->ptz_cfg != p_ptz_cfg)
    {
        if (p_profile->ptz_cfg && p_profile->ptz_cfg->Configuration.UseCount > 0)
        {
            p_profile->ptz_cfg->Configuration.UseCount--;
        }
        
        p_ptz_cfg->Configuration.UseCount++;
        
        p_profile->ptz_cfg = p_ptz_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  removes a PTZConfiguration from an existing media profile. 
 *  If the media profile does not contain a PTZConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemovePTZConfiguration(const char * token)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->ptz_cfg && p_profile->ptz_cfg->Configuration.UseCount > 0)
    {
        p_profile->ptz_cfg->Configuration.UseCount--;
    }
    
    p_profile->ptz_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

#endif // PTZ_SUPPORT

#ifdef DEVICEIO_SUPPORT

/************************************************************************************
 *
 * @brief
 *  adds an AudioOutputConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent. 
 *
 *  An device that signals support for Audio outputs via its Device IO 
 *  AudioOutputs capability shall support the addition of an audio output
 *  configuration to a profile.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_AddAudioOutputConfiguration(trt_AddAudioOutputConfiguration_REQ * p_req)
{
    AudioOutputConfigurationList * p_a_output_cfg;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    p_a_output_cfg = onvif_find_AudioOutputConfiguration(g_onvif_cfg.a_output_cfg, p_req->ConfigurationToken);
    if (NULL == p_a_output_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }

    if (p_profile->a_output_cfg != p_a_output_cfg)
    {
        if (p_profile->a_output_cfg && p_profile->a_output_cfg->Configuration.UseCount > 0)
        {
            p_profile->a_output_cfg->Configuration.UseCount--;
        }
        
        p_a_output_cfg->Configuration.UseCount++;
        
        p_profile->a_output_cfg = p_a_output_cfg;
    }

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

/************************************************************************************
 *
 * @brief
 *  Removes an AudioOutputConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioOutputConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 * @return
 *  Possible error:
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigurationConflict
 *
*************************************************************************************/
ONVIF_RET onvif_trt_RemoveAudioOutputConfiguration(trt_RemoveAudioOutputConfiguration_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }

    if (p_profile->a_output_cfg && p_profile->a_output_cfg->Configuration.UseCount > 0)
    {
        p_profile->a_output_cfg->Configuration.UseCount--;
    }
    
    p_profile->a_output_cfg = NULL;

    onvif_ProfileChangedNotify(p_profile, PropertyOperation_Changed);
    
    return ONVIF_OK;
}

#endif // end of DEVICEIO_SUPPORT

#endif // end of MEDIA_SUPPORT


