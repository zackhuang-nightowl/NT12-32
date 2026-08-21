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
#ifndef ONVIF_API_H
#define ONVIF_API_H

#include "sys_inc.h"
#include "onvif.h"
#include "onvif_cln.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 *  ONVIF 1.0 compatible interface.
 *  Call GetCapabilities interface to obtain device capability set.
 *  The device capability set is saved in p_dev->Capabilities.
 *
 *  Merge the obtained capability set with the existing capability set
 *
 *  This method provides a backward compatible interface for the base capabilities.
 *
 **/
HT_API BOOL GetCapabilities(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Call GetServices interface to obtain device capability set.
 *  The device capability set is saved in p_dev->Capabilities.
 *
 *  Merge the obtained capability set with the existing capability set
 *
 **/
HT_API BOOL GetServices(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Get device date and time.
 *  The device date and time are saved in p_dev->devTime.
 *
 *  If p_dev->timeType is 1, p_dev->devTime is local time, 
 *  If p_dev->timeType is 2, p_dev->devTime is UTC time.
 *
 **/
HT_API BOOL GetSystemDateAndTime(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Set the device date and time using the current system time.
 *
 **/
HT_API BOOL SetSystemDateAndTime(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve device information and save it in p_dev->DeviceInformation.
 *
 **/
HT_API BOOL GetDeviceInformation(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve device EndpointReference and save it in p_dev->binfo.EndpointReference.
 *
 **/
HT_API BOOL GetEndpointReference(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Call the GetProfiles interface to retrieve a list of all device profiles
 *  and save them in p_dev->profiles, existing profiles will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetProfiles(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve the RTSP stream addresses of all device profiles.
 *  Before calling this interface, it is necessary to first call GetProfiles
 *  to obtain all the profiles of the device.
 *
 *  The obtained stream address is saved in ONVIF_PROFILE.stream_uri.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 * @param 
 *  proto - Specify the RTSP stream protocol to be obtained.
 *
 **/
HT_API BOOL GetStreamUris(ONVIF_DEVICE * p_dev, onvif_TransportProtocol proto);

/**
 * @brief
 *  Retrieve a list of all device video source configuration.
 *  and save them in p_dev->v_src_cfg, existing video source configuration
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device audio source configuration.
 *  and save them in p_dev->a_src_cfg, existing audio source configuration
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device video encoder configuration.
 *  and save them in p_dev->v_enc_cfg, existing video encoder configuration
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device audio encoder configuration.
 *  and save them in p_dev->a_enc_cfg, existing audio encoder configuration
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device video source.
 *  and save them in p_dev->v_src, existing video source
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetVideoSources(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device audio source.
 *  and save them in p_dev->a_src, existing audio source
 *  will be released.
 *
 *  This is the ONVIF Media 1 service interface.
 *
 **/
HT_API BOOL GetAudioSources(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device PTZ node.
 *  and save them in p_dev->ptz_node, existing PTZ node
 *  will be released.
 *
 *  The device needs to support PTZ, and the capability set 
 *  p_dev->Capabilities.ptz.support needs to be 1.
 *  
 **/
HT_API BOOL GetNodes(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device PTZ configuration.
 *  and save them in p_dev->ptz_cfg, existing PTZ configuration
 *  will be released.
 *
 *  The device needs to support PTZ, and the capability set 
 *  p_dev->Capabilities.ptz.support needs to be 1.
 *
 **/
HT_API BOOL GetConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Get image settings for all video sources on the device.
 *  Image settings saved in VideoSourceList.VideoSource.ImagingSettings.
 *  
 *  Before calling this interface, GetVideoSources needs to be called first.
 *
 *
 *  The device needs to support image service, and the capability set 
 *  p_dev->Capabilities.image.support needs to be 1.
 *
 **/
HT_API BOOL GetImagingSettings(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Subscription event notifications.
 *  The internal timer will send a renew request before TerminationTime expires.
 *  
 *  Before calling this interface, you need to first call onvif_event_init.
 *  Call onvif_set_event_notify_cb to set event notification callback.
 *  Call onvif_set_subscribe_disconnect_cb to set event subscribe discnnnect callback.
 *
 * @param 
 *  index - The index will be added to the subscription reference addr.
 *
 **/
HT_API BOOL Subscribe(ONVIF_DEVICE * p_dev, int index);

/**
 * @brief
 *  Cancel event notification subscription.
 *
 **/
HT_API BOOL Unsubscribe(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Create event pulling point.
 *
 **/
HT_API BOOL CreatePullPointSubscription(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Pulling event messages.
 *  Before calling this interface, you need to first call 
 *  CreatePullPointSubscription.
 *
 *  After CreatePullPointSubscription, you need to call PullMessages to get 
 *  messages before timeout. If PullMessages is not called after timeout, 
 *  the device should delete the event pulling point.
 *
 * @param timeout - The timeout for obtaining messages
 * @param message_limit - Maximum number of event messages
 * @param p_res - event messages list
 *  
 **/
HT_API BOOL PullMessages(ONVIF_DEVICE * p_dev, int timeout, int message_limit, tev_PullMessages_RES * p_res);

/**
 * @brief
 *  Get the device snapshot
 *  
 *  If success, the caller should call FreeBuff to free the snapshot buffer.
 *
 * @param profile_token, profile token
 * @param p_buf - [out] return the snapshot buffer
 * @param buflen [out] return the snapshot buffer length
 *
 **/
HT_API BOOL GetSnapshot(ONVIF_DEVICE * p_dev, const char * profile_token, unsigned char ** p_buf, int * buflen);

/**
 * @brief
 *  Free the buffer
 *
 * @param p_buf the buffer to be freed.
 *
 **/
HT_API void FreeBuff(void * p_buf);

/**
 * @brief
 *  Firmware upgrade
 *
 * @param filename the upgrade filename
 *
 **/
HT_API BOOL FirmwareUpgrade(ONVIF_DEVICE * p_dev, const char * filename);

/**
 * @brief
 *  backup system settings
 *
 * @param filename save as system backup
 *
 **/
HT_API BOOL SystemBackup(ONVIF_DEVICE * p_dev, const char * filename);

/**
 * @brief
 *  restore system settings
 *
 * @param filename the system backup filename
 *
 **/
HT_API BOOL SystemRestore(ONVIF_DEVICE * p_dev, const char * filename);

/**
 * @brief
 *  Call the GetProfiles interface to retrieve a list of all device profiles
 *  and save them in p_dev->media_profiles, existing profiles will be released.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 **/
HT_API BOOL tr2_GetProfiles(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve the RTSP stream addresses of all device profiles.
 *  Before calling this interface, it is necessary to first call tr2_GetProfiles
 *  to obtain all the profiles of the device.
 *
 *  The obtained stream address is saved in MediaProfile.stream_uri.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 * @param proto
 *  RtspUnicast -- RTSP streaming RTP as UDP Unicast
 *  RtspMulticast -- RTSP streaming RTP as UDP Multicast
 *  RTSP -- RTSP streaming RTP over TCP
 *  RtspOverHttp -- Tunneling both the RTSP control channel and the RTP stream 
 *  over HTTP or HTTPS
 *
 **/
HT_API BOOL tr2_GetStreamUris(ONVIF_DEVICE * p_dev, const char * proto);

/**
 * @brief
 *  Retrieve a list of all device video source configuration.
 *  and save them in p_dev->v_src_cfg, existing video source configuration
 *  will be released.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 **/
HT_API BOOL tr2_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device audio source configuration.
 *  and save them in p_dev->a_src_cfg, existing audio source configuration
 *  will be released.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 **/
HT_API BOOL tr2_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device video encoder configuration.
 *  and save them in p_dev->v_enc_cfg2, existing video encoder configuration
 *  will be released.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 **/
HT_API BOOL tr2_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev);

/**
 * @brief
 *  Retrieve a list of all device audio encoder configuration.
 *  and save them in p_dev->a_enc_cfg2, existing audio encoder configuration
 *  will be released.
 *
 *  This is the ONVIF Media 2 service interface.
 *
 **/
HT_API BOOL tr2_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev);

#ifdef __cplusplus
}
#endif

#endif



