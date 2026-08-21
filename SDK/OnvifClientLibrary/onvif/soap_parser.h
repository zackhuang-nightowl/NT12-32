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

#ifndef SOAP_PARSER_H
#define SOAP_PARSER_H

#include "sys_inc.h"
#include "onvif.h"
#include "xml_node.h"
#include "onvif_res.h"

#ifdef __cplusplus
extern "C" {
#endif

HT_API BOOL parse_Bool(const char * pdata);
HT_API int  parse_DeviceType(const char * pdata);
HT_API int  parse_XAddr(const char * pdata, onvif_XAddr * p_xaddr);

int  parse_Time(const char * pdata);
BOOL parse_XSDDatetime(const char * s, time_t * p);
BOOL parse_XSDDuration(const char *s, int *a);
BOOL parse_Fault(XMLN * p_node, onvif_Fault * p_res);
BOOL parse_FloatRangeList(const char * p_buff, onvif_FloatRange * p_res);
BOOL parse_IntList(XMLN * p_node, onvif_IntList * p_res);
BOOL parse_FloatList(XMLN * p_node, onvif_FloatList * p_req);
BOOL parse_IntRange(XMLN * p_node, onvif_IntRange * p_res);
BOOL parse_FloatRange(XMLN * p_node, onvif_FloatRange * p_res);
BOOL parse_EncodingList(const char * p_encoding, onvif_RecordingCapabilities * p_res);
BOOL parse_Vector(XMLN * p_node, onvif_Vector * p_res);
BOOL parse_Vector1D(XMLN * p_node, onvif_Vector1D * p_res);
BOOL parse_PTZSpeed(XMLN * p_node, onvif_PTZSpeed * p_res);
BOOL parse_PTZVector(XMLN * p_node, onvif_PTZVector * p_res);
BOOL parse_AnalyticsCapabilities(XMLN * p_node, onvif_AnalyticsCapabilities * p_res);
BOOL parse_DeviceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res);
BOOL parse_EventsCapabilities(XMLN * p_node, onvif_EventCapabilities * p_res);
BOOL parse_ImageCapabilities(XMLN * p_node, onvif_ImagingCapabilities * p_res);
BOOL parse_MediaCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res);
BOOL parse_PTZCapabilities(XMLN * p_node, onvif_PTZCapabilities * p_res);
BOOL parse_DeviceIOCapabilities(XMLN * p_node, onvif_DeviceIOCapabilities * p_res);
BOOL parse_RecordingCapabilities(XMLN * p_node, onvif_RecordingCapabilities * p_res);
BOOL parse_SearchCapabilities(XMLN * p_node, onvif_SearchCapabilities * p_res);
BOOL parse_ReplayCapabilities(XMLN * p_node, onvif_ReplayCapabilities * p_res);
BOOL parse_Version(XMLN * p_node, onvif_Version * p_res);
BOOL parse_DeviceServiceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res);
BOOL parse_DeviceService(XMLN * p_node, onvif_DevicesCapabilities * p_res);
BOOL parse_MediaServiceCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res);
BOOL parse_MediaService(XMLN * p_node, onvif_MediaCapabilities * p_res);
BOOL parse_Media2ServiceCapabilities(XMLN * p_node, onvif_MediaCapabilities2 * p_res);
BOOL parse_Media2Service(XMLN * p_node, onvif_MediaCapabilities2 * p_res);
BOOL parse_EventServiceCapabilities(XMLN * p_node, onvif_EventCapabilities * p_res);
BOOL parse_EventsService(XMLN * p_node, onvif_EventCapabilities * p_res);
BOOL parse_PTZServiceCapabilities(XMLN * p_node, onvif_PTZCapabilities * p_res);
BOOL parse_PTZService(XMLN * p_node, onvif_PTZCapabilities * p_res);
BOOL parse_ImagingServiceCapabilities(XMLN * p_node, onvif_ImagingCapabilities * p_res);
BOOL parse_ImagingService(XMLN * p_node, onvif_ImagingCapabilities * p_res);
BOOL parse_AnalyticsServiceCapabilities(XMLN * p_node, onvif_AnalyticsCapabilities * p_res);
BOOL parse_AnalyticsService(XMLN * p_node, onvif_AnalyticsCapabilities * p_res);
BOOL parse_VideoSourceConfiguration(XMLN * p_node, onvif_VideoSourceConfiguration * p_res);
BOOL parse_AudioSourceConfiguration(XMLN * p_node, onvif_AudioSourceConfiguration * p_res);
BOOL parse_MulticastConfiguration(XMLN * p_node, onvif_MulticastConfiguration * p_res);
BOOL parse_VideoResolution(XMLN * p_node, onvif_VideoResolution * p_res);
BOOL parse_ImagingSettings(XMLN * p_node, onvif_ImagingSettings * p_res);
BOOL parse_VideoSource(XMLN * p_node, onvif_VideoSource * p_res);
BOOL parse_AudioSource(XMLN * p_node, onvif_AudioSource * p_res);
BOOL parse_SimpleItem(XMLN * p_node, onvif_SimpleItem * p_res);
BOOL parse_ElementItem(XMLN * p_node, onvif_ElementItem * p_res);
BOOL parse_ItemList(XMLN * p_node, onvif_ItemList * p_res);
BOOL parse_Config(XMLN * p_node, onvif_Config * p_res);
BOOL parse_VideoEncoderConfiguration(XMLN * p_node, onvif_VideoEncoderConfiguration * p_res);
BOOL parse_AudioEncoderConfiguration(XMLN * p_node, onvif_AudioEncoderConfiguration * p_res);
BOOL parse_PTZConfiguration(XMLN * p_node, onvif_PTZConfiguration * p_res);
BOOL parse_AnalyticsEngineConfiguration(XMLN * p_node, onvif_AnalyticsEngineConfiguration * p_res);
BOOL parse_RuleEngineConfiguration(XMLN * p_node, onvif_RuleEngineConfiguration * p_res);
BOOL parse_VideoAnalyticsConfiguration(XMLN * p_node, onvif_VideoAnalyticsConfiguration * p_res);
BOOL parse_Profile(XMLN * p_node, ONVIF_PROFILE * p_profile);
BOOL parse_Dot11Configuration(XMLN * p_node, onvif_Dot11Configuration * p_res);
BOOL parse_NetworkInterface(XMLN * p_node, onvif_NetworkInterface * p_res);
BOOL parse_User(XMLN * p_node, onvif_User * p_res);
BOOL parse_Datetime(XMLN * p_node, onvif_DateTime * p_datetime);
BOOL parse_Dot11AvailableNetworks(XMLN * p_node, onvif_Dot11AvailableNetworks * p_res);
BOOL parse_LocationEntity(XMLN * p_node, onvif_LocationEntity * p_res);
BOOL parse_PrefixedIPAddress(XMLN * p_node, onvif_PrefixedIPAddress * p_res);
BOOL parse_IPAddressFilter(XMLN * p_node, onvif_IPAddressFilter * p_res);
BOOL parse_UserCredential(XMLN * p_node, onvif_UserCredential * p_res);
BOOL parse_StorageConfigurationData(XMLN * p_node, onvif_StorageConfigurationData * p_res);
BOOL parse_StorageConfiguration(XMLN * p_node, onvif_StorageConfiguration * p_res);
BOOL parse_RelayOutputSettings(XMLN * p_node, onvif_RelayOutputSettings * p_res);

BOOL parse_tds_GetCapabilities(XMLN * p_node, tds_GetCapabilities_RES * p_res);
BOOL parse_tds_GetServices(XMLN * p_node, tds_GetServices_RES * p_res);
BOOL parse_tds_GetServiceCapabilities(XMLN * p_node, tds_GetServiceCapabilities_RES * p_res);
BOOL parse_tds_GetDeviceInformation(XMLN * p_node, tds_GetDeviceInformation_RES * p_res);
BOOL parse_tds_GetUsers(XMLN * p_node, tds_GetUsers_RES * p_res);
BOOL parse_tds_GetRemoteUser(XMLN * p_node, tds_GetRemoteUser_RES * p_res);
BOOL parse_tds_GetNetworkInterfaces(XMLN * p_node, tds_GetNetworkInterfaces_RES * p_res);
BOOL parse_tds_SetNetworkInterfaces(XMLN * p_node, tds_SetNetworkInterfaces_RES * p_res);
BOOL parse_tds_GetNTP(XMLN * p_node, tds_GetNTP_RES * p_res);
BOOL parse_tds_GetHostname(XMLN * p_node, tds_GetHostname_RES * p_res);
BOOL parse_tds_SetHostnameFromDHCP(XMLN * p_node, tds_SetHostnameFromDHCP_RES * p_res);
BOOL parse_tds_GetDNS(XMLN * p_node, tds_GetDNS_RES * p_res);
BOOL parse_tds_GetDynamicDNS(XMLN * p_node, tds_GetDynamicDNS_RES * p_res);
BOOL parse_tds_GetNetworkProtocols(XMLN * p_node, tds_GetNetworkProtocols_RES * p_res);
BOOL parse_tds_GetDiscoveryMode(XMLN * p_node, tds_GetDiscoveryMode_RES * p_res);
BOOL parse_tds_GetNetworkDefaultGateway(XMLN * p_node, tds_GetNetworkDefaultGateway_RES * p_res);
BOOL parse_tds_GetZeroConfiguration(XMLN * p_node, tds_GetZeroConfiguration_RES * p_res);
BOOL parse_tds_GetEndpointReference(XMLN * p_node, tds_GetEndpointReference_RES * p_res);
BOOL parse_tds_SendAuxiliaryCommand(XMLN * p_node, tds_SendAuxiliaryCommand_RES * p_res);
BOOL parse_tds_GetRelayOutputs(XMLN * p_node, tds_GetRelayOutputs_RES * p_res);
BOOL parse_tds_GetSystemDateAndTime(XMLN * p_node, tds_GetSystemDateAndTime_RES * p_res);
BOOL parse_tds_GetSystemLog(XMLN * p_node, tds_GetSystemLog_RES * p_res);
BOOL parse_tds_GetScopes(XMLN * p_node, tds_GetScopes_RES * p_res);
BOOL parse_tds_StartFirmwareUpgrade(XMLN * p_node, tds_StartFirmwareUpgrade_RES * p_res);
BOOL parse_tds_GetSystemUris(XMLN * p_node, tds_GetSystemUris_RES * p_res);
BOOL parse_tds_StartSystemRestore(XMLN * p_node, tds_StartSystemRestore_RES * p_res);
BOOL parse_tds_GetWsdlUrl(XMLN * p_node, tds_GetWsdlUrl_RES * p_res);
BOOL parse_tds_GetDot11Capabilities(XMLN * p_node, tds_GetDot11Capabilities_RES * p_res);
BOOL parse_tds_GetDot11Status(XMLN * p_node, tds_GetDot11Status_RES * p_res);
BOOL parse_tds_ScanAvailableDot11Networks(XMLN * p_node, tds_ScanAvailableDot11Networks_RES * p_res);
BOOL parse_tds_GetGeoLocation(XMLN * p_node, tds_GetGeoLocation_RES * p_res);
BOOL parse_tds_GetIPAddressFilter(XMLN * p_node, tds_GetIPAddressFilter_RES * p_res);
BOOL parse_tds_GetAccessPolicy(XMLN * p_node, tds_GetAccessPolicy_RES * p_res);
BOOL parse_tds_GetStorageConfigurations(XMLN * p_node, tds_GetStorageConfigurations_RES * p_res);
BOOL parse_tds_CreateStorageConfiguration(XMLN * p_node, tds_CreateStorageConfiguration_RES * p_res);
BOOL parse_tds_GetStorageConfiguration(XMLN * p_node, tds_GetStorageConfiguration_RES * p_res);

BOOL parse_JPEGOptions(XMLN * p_node, onvif_JpegOptions * p_res);
BOOL parse_MPEG4Options(XMLN * p_node, onvif_Mpeg4Options * p_res);
BOOL parse_H264Options(XMLN * p_node, onvif_H264Options * p_res);
BOOL parse_AudioOutputConfiguration(XMLN * p_node, onvif_AudioOutputConfiguration * p_res);
BOOL parse_AudioDecoderConfiguration(XMLN * p_node, onvif_AudioDecoderConfiguration * p_res);
BOOL parse_VideoSourceMode(XMLN * p_node, onvif_VideoSourceMode * p_res);
BOOL parse_Color(XMLN * p_node, onvif_Color * p_res);
BOOL parse_ColorOptions(XMLN * p_node, onvif_ColorOptions * p_res);
BOOL parse_OSDColorOptions(XMLN * p_node, onvif_OSDColorOptions * p_res);
BOOL parse_OSDOptions(XMLN * p_node, onvif_OSDConfigurationOptions * p_res);
BOOL parse_OSDColor(XMLN * p_node, onvif_OSDColor * p_res);
BOOL parse_OSDConfiguration(XMLN * p_node, onvif_OSDConfiguration * p_res);

BOOL parse_trt_GetServiceCapabilities(XMLN * p_node, trt_GetServiceCapabilities_RES * p_res);
BOOL parse_trt_GetVideoSources(XMLN * p_node, trt_GetVideoSources_RES * p_res);
BOOL parse_trt_GetAudioSources(XMLN * p_node, trt_GetAudioSources_RES * p_res);
BOOL parse_trt_CreateProfile(XMLN * p_node, trt_CreateProfile_RES * p_res);
BOOL parse_trt_GetProfile(XMLN * p_node, trt_GetProfile_RES * p_res);
BOOL parse_trt_GetProfiles(XMLN * p_node, trt_GetProfiles_RES * p_res);
BOOL parse_trt_GetVideoSourceModes(XMLN * p_node, trt_GetVideoSourceModes_RES * p_res);
BOOL parse_trt_SetVideoSourceMode(XMLN * p_node, trt_SetVideoSourceMode_RES * p_res);
BOOL parse_trt_GetVideoSourceConfigurations(XMLN * p_node, trt_GetVideoSourceConfigurations_RES * p_res);
BOOL parse_trt_GetVideoEncoderConfigurations(XMLN * p_node, trt_GetVideoEncoderConfigurations_RES * p_res);
BOOL parse_trt_GetAudioSourceConfigurations(XMLN * p_node, trt_GetAudioSourceConfigurations_RES * p_res);
BOOL parse_trt_GetAudioEncoderConfigurations(XMLN * p_node, trt_GetAudioEncoderConfigurations_RES * p_res);
BOOL parse_trt_GetVideoSourceConfiguration(XMLN * p_node, trt_GetVideoSourceConfiguration_RES * p_res);
BOOL parse_trt_GetVideoEncoderConfiguration(XMLN * p_node, trt_GetVideoEncoderConfiguration_RES * p_res);
BOOL parse_trt_GetAudioSourceConfiguration(XMLN * p_node, trt_GetAudioSourceConfiguration_RES * p_res);
BOOL parse_trt_GetAudioEncoderConfiguration(XMLN * p_node, trt_GetAudioEncoderConfiguration_RES * p_res);
BOOL parse_trt_GetVideoSourceConfigurationOptions(XMLN * p_node, trt_GetVideoSourceConfigurationOptions_RES * p_res);
BOOL parse_trt_GetVideoEncoderConfigurationOptions(XMLN * p_node, trt_GetVideoEncoderConfigurationOptions_RES * p_res);
BOOL parse_trt_GetAudioEncoderConfigurationOptions(XMLN * p_node, trt_GetAudioEncoderConfigurationOptions_RES * p_res);
BOOL parse_trt_GetStreamUri(XMLN * p_node, trt_GetStreamUri_RES * p_res);
BOOL parse_trt_GetSnapshotUri(XMLN * p_node, trt_GetSnapshotUri_RES * p_res);
BOOL parse_trt_GetGuaranteedNumberOfVideoEncoderInstances(XMLN * p_node, trt_GetGuaranteedNumberOfVideoEncoderInstances_RES * p_res);
BOOL parse_trt_GetAudioOutputs(XMLN * p_node, trt_GetAudioOutputs_RES * p_res);
BOOL parse_trt_GetAudioOutputConfigurations(XMLN * p_node, trt_GetAudioOutputConfigurations_RES * p_res);
BOOL parse_trt_GetAudioOutputConfiguration(XMLN * p_node, trt_GetAudioOutputConfiguration_RES * p_res);
BOOL parse_trt_GetAudioOutputConfigurationOptions(XMLN * p_node, trt_GetAudioOutputConfigurationOptions_RES * p_res);
BOOL parse_trt_GetAudioDecoderConfigurations(XMLN * p_node, trt_GetAudioDecoderConfigurations_RES * p_res);
BOOL parse_trt_GetAudioDecoderConfiguration(XMLN * p_node, trt_GetAudioDecoderConfiguration_RES * p_res);
BOOL parse_trt_GetAudioDecoderConfigurationOptions(XMLN * p_node, trt_GetAudioDecoderConfigurationOptions_RES * p_res);
BOOL parse_trt_GetOSDs(XMLN * p_node, trt_GetOSDs_RES * p_res);
BOOL parse_trt_GetOSD(XMLN * p_node, trt_GetOSD_RES * p_res);
BOOL parse_trt_GetOSDOptions(XMLN * p_node, trt_GetOSDOptions_RES * p_res);
BOOL parse_trt_CreateOSD(XMLN * p_node, trt_CreateOSD_RES * p_res);
BOOL parse_trt_GetVideoAnalyticsConfigurations(XMLN * p_node, trt_GetVideoAnalyticsConfigurations_RES * p_res);
BOOL parse_trt_GetVideoAnalyticsConfiguration(XMLN * p_node, trt_GetVideoAnalyticsConfiguration_RES * p_res);
BOOL parse_trt_GetMetadataConfigurations(XMLN * p_node, trt_GetMetadataConfigurations_RES * p_res);
BOOL parse_trt_GetMetadataConfiguration(XMLN * p_node, trt_GetMetadataConfiguration_RES * p_res);
BOOL parse_trt_GetMetadataConfigurationOptions(XMLN * p_node, trt_GetMetadataConfigurationOptions_RES * p_res);
BOOL parse_trt_GetCompatibleVideoEncoderConfigurations(XMLN * p_node, trt_GetCompatibleVideoEncoderConfigurations_RES * p_res);
BOOL parse_trt_GetCompatibleAudioEncoderConfigurations(XMLN * p_node, trt_GetCompatibleAudioEncoderConfigurations_RES * p_res);
BOOL parse_trt_GetCompatibleVideoAnalyticsConfigurations(XMLN * p_node, trt_GetCompatibleVideoAnalyticsConfigurations_RES * p_res);
BOOL parse_trt_GetCompatibleMetadataConfigurations(XMLN * p_node, trt_GetCompatibleMetadataConfigurations_RES * p_res);

BOOL parse_Space2DDescription(XMLN * p_node, onvif_Space2DDescription * p_res);
BOOL parse_Space1DDescription(XMLN * p_node, onvif_Space1DDescription * p_res);
BOOL parse_PTZNode(XMLN * p_node, onvif_PTZNode * p_res);
BOOL parse_Preset(XMLN * p_node, onvif_PTZPreset * p_res);
BOOL parse_PTZPresetTourSpot(XMLN * p_node, onvif_PTZPresetTourSpot * p_res);
BOOL parse_PTZPresetTourStatus(XMLN * p_node, onvif_PTZPresetTourStatus * p_res);
BOOL parse_PTZPresetTourStartingCondition(XMLN * p_node, onvif_PTZPresetTourStartingCondition * p_res);
BOOL parse_PresetTour(XMLN * p_node, onvif_PresetTour * p_res);
BOOL parse_DurationRange(XMLN * p_node, onvif_DurationRange * p_res);

BOOL parse_ptz_GetServiceCapabilities(XMLN * p_node, ptz_GetServiceCapabilities_RES * p_res);
BOOL parse_ptz_GetNodes(XMLN * p_node, ptz_GetNodes_RES * p_res);
BOOL parse_ptz_GetNode(XMLN * p_node, ptz_GetNode_RES * p_res);
BOOL parse_ptz_GetPresets(XMLN * p_node, ptz_GetPresets_RES * p_res);
BOOL parse_ptz_SetPreset(XMLN * p_node, ptz_SetPreset_RES * p_res);
BOOL parse_ptz_GetStatus(XMLN * p_node, ptz_GetStatus_RES * p_res);
BOOL parse_ptz_GetConfigurations(XMLN * p_node, ptz_GetConfigurations_RES * p_res);
BOOL parse_ptz_GetConfiguration(XMLN * p_node, ptz_GetConfiguration_RES * p_res);
BOOL parse_ptz_GetConfigurationOptions(XMLN * p_node, ptz_GetConfigurationOptions_RES * p_res);
BOOL parse_ptz_GetPresetTours(XMLN * p_node, ptz_GetPresetTours_RES * p_res);
BOOL parse_ptz_GetPresetTour(XMLN * p_node, ptz_GetPresetTour_RES * p_res);
BOOL parse_ptz_GetPresetTourOptions(XMLN * p_node, ptz_GetPresetTourOptions_RES * p_res);
BOOL parse_ptz_CreatePresetTour(XMLN * p_node, ptz_CreatePresetTour_RES * p_res);
BOOL parse_ptz_SendAuxiliaryCommand(XMLN * p_node, ptz_SendAuxiliaryCommand_RES * p_res);

BOOL parse_NotificationMessage(XMLN * p_node, onvif_NotificationMessage * p_res);
BOOL parse_NotifyMessage(XMLN * p_node, NotificationMessageList ** p_res);
BOOL parse_tev_GetServiceCapabilities(XMLN * p_node, tev_GetServiceCapabilities_RES * p_res);
BOOL parse_tev_GetEventProperties(XMLN * p_node, tev_GetEventProperties_RES * p_res);
BOOL parse_tev_Subscribe(XMLN * p_node, tev_Subscribe_RES * p_res);
BOOL parse_tev_CreatePullPointSubscription(XMLN * p_node, tev_CreatePullPointSubscription_RES * p_res);
BOOL parse_tev_PullMessages(XMLN * p_node, tev_PullMessages_RES * p_res);
BOOL parse_tev_GetMessages(XMLN * p_node, tev_GetMessages_RES * p_res);

BOOL parse_ImagingPreset(XMLN * p_node, onvif_ImagingPreset * p_res);
BOOL parse_img_GetServiceCapabilities(XMLN * p_node, img_GetServiceCapabilities_RES * p_res);
BOOL parse_img_GetImagingSettings(XMLN * p_node, img_GetImagingSettings_RES * p_res);
BOOL parse_img_GetOptions(XMLN * p_node, img_GetOptions_RES * p_res);
BOOL parse_img_GetStatus(XMLN * p_node, img_GetStatus_RES * p_res);
BOOL parse_img_GetMoveOptions(XMLN * p_node, img_GetMoveOptions_RES * p_res);
BOOL parse_img_GetPresets(XMLN * p_node, img_GetPresets_RES * p_res);
BOOL parse_img_GetCurrentPreset(XMLN * p_node, img_GetCurrentPreset_RES * p_res);

int  parse_SimpleItemDescriptions(XMLN * p_node, const char * name, SimpleItemDescriptionList * p_res);
BOOL parse_ItemListDescription(XMLN * p_node, onvif_ItemListDescription * p_res);
BOOL parse_ConfigDescription_Messages(XMLN * p_node, onvif_ConfigDescription_Messages * p_res);
BOOL parse_RuleDescription(XMLN * p_node, onvif_ConfigDescription * p_res);
BOOL parse_ConfigDescription(XMLN * p_node, onvif_ConfigDescription * p_res);
BOOL parse_ConfigOptions(XMLN * p_node, onvif_ConfigOptions * p_res);
BOOL parse_AnalyticsModuleConfigOptions(XMLN * p_node, onvif_AnalyticsModuleConfigOptions * p_res);
BOOL parse_tan_GetServiceCapabilities(XMLN * p_node, tan_GetServiceCapabilities_RES * p_res);
BOOL parse_tan_GetSupportedRules(XMLN * p_node, tan_GetSupportedRules_RES * p_res);
BOOL parse_tan_GetRules(XMLN * p_node, tan_GetRules_RES * p_res);
BOOL parse_tan_GetAnalyticsModules(XMLN * p_node, tan_GetAnalyticsModules_RES * p_res);
BOOL parse_tan_GetSupportedAnalyticsModules(XMLN * p_node, tan_GetSupportedAnalyticsModules_RES * p_res);
BOOL parse_tan_GetRuleOptions(XMLN * p_node, tan_GetRuleOptions_RES * p_res);
BOOL parse_tan_GetAnalyticsModuleOptions(XMLN * p_node, tan_GetAnalyticsModuleOptions_RES * p_res);
BOOL parse_tan_GetSupportedMetadata(XMLN * p_node, tan_GetSupportedMetadata_RES * p_res);

BOOL parse_VideoEncoder2Configuration(XMLN * p_node, onvif_VideoEncoder2Configuration * p_res);
BOOL parse_VideoEncoder2ConfigurationOptions(XMLN * p_node, onvif_VideoEncoder2ConfigurationOptions * p_res);
BOOL parse_MetadataConfiguration(XMLN * p_node, onvif_MetadataConfiguration * p_res);
BOOL parse_AudioEncoder2Configuration(XMLN * p_node, onvif_AudioEncoder2Configuration * p_res);
BOOL parse_AudioEncoder2ConfigurationOptions(XMLN * p_node, onvif_AudioEncoder2ConfigurationOptions * p_res);
BOOL parse_MediaProfile(XMLN * p_node, onvif_MediaProfile * p_res);
BOOL parse_Polygon(XMLN * p_node, onvif_Polygon * p_res);
BOOL parse_Mask(XMLN * p_node, onvif_Mask * p_res);
BOOL parse_MaskOptions(XMLN * p_node, onvif_MaskOptions * p_res);

BOOL parse_tr2_GetServiceCapabilities(XMLN * p_node, tr2_GetServiceCapabilities_RES * p_res);
BOOL parse_tr2_GetVideoEncoderConfigurations(XMLN * p_node, tr2_GetVideoEncoderConfigurations_RES * p_res);
BOOL parse_tr2_GetVideoEncoderConfigurationOptions(XMLN * p_node, tr2_GetVideoEncoderConfigurationOptions_RES * p_res);
BOOL parse_tr2_GetProfiles(XMLN * p_node, tr2_GetProfiles_RES * p_res);
BOOL parse_tr2_CreateProfile(XMLN * p_node, tr2_CreateProfile_RES * p_res);
BOOL parse_tr2_GetStreamUri(XMLN * p_node, tr2_GetStreamUri_RES * p_res);
BOOL parse_tr2_GetVideoSourceConfigurations(XMLN * p_node, tr2_GetVideoSourceConfigurations_RES * p_res);
BOOL parse_tr2_GetMetadataConfigurations(XMLN * p_node, tr2_GetMetadataConfigurations_RES * p_res);
BOOL parse_tr2_GetMetadataConfigurationOptions(XMLN * p_node, tr2_GetMetadataConfigurationOptions_RES * p_res);
BOOL parse_tr2_GetAudioEncoderConfigurations(XMLN * p_node, tr2_GetAudioEncoderConfigurations_RES * p_res);
BOOL parse_tr2_GetAudioSourceConfigurations(XMLN * p_node, tr2_GetAudioSourceConfigurations_RES * p_res);
BOOL parse_tr2_GetAudioEncoderConfigurationOptions(XMLN * p_node, tr2_GetAudioEncoderConfigurationOptions_RES * p_res);
BOOL parse_tr2_GetVideoEncoderInstances(XMLN * p_node, tr2_GetVideoEncoderInstances_RES * p_res);
BOOL parse_tr2_GetAudioOutputConfigurations(XMLN * p_node, tr2_GetAudioOutputConfigurations_RES * p_res);
BOOL parse_tr2_GetAudioOutputConfigurationOptions(XMLN * p_node, tr2_GetAudioOutputConfigurationOptions_RES * p_res);
BOOL parse_tr2_GetAudioDecoderConfigurations(XMLN * p_node, tr2_GetAudioDecoderConfigurations_RES * p_res);
BOOL parse_tr2_GetAudioDecoderConfigurationOptions(XMLN * p_node, tr2_GetAudioDecoderConfigurationOptions_RES * p_res);
BOOL parse_tr2_GetSnapshotUri(XMLN * p_node, tr2_GetSnapshotUri_RES * p_res);
BOOL parse_tr2_GetVideoSourceModes(XMLN * p_node, tr2_GetVideoSourceModes_RES * p_res);
BOOL parse_tr2_SetVideoSourceMode(XMLN * p_node, tr2_SetVideoSourceMode_RES * p_res);
BOOL parse_tr2_CreateOSD(XMLN * p_node, tr2_CreateOSD_RES * p_res);
BOOL parse_tr2_GetOSDs(XMLN * p_node, tr2_GetOSDs_RES * p_res);
BOOL parse_tr2_GetOSDOptions(XMLN * p_node, tr2_GetOSDOptions_RES * p_res);
BOOL parse_tr2_GetAnalyticsConfigurations(XMLN * p_node, tr2_GetAnalyticsConfigurations_RES * p_res);
BOOL parse_tr2_GetMasks(XMLN * p_node, tr2_GetMasks_RES * p_res);
BOOL parse_tr2_CreateMask(XMLN * p_node, tr2_CreateMask_RES * p_res);
BOOL parse_tr2_GetMaskOptions(XMLN * p_node, tr2_GetMaskOptions_RES * p_res);

BOOL parse_DeviceIOServiceCapabilities(XMLN * p_node, onvif_DeviceIOCapabilities * p_res);
BOOL parse_DeviceIOService(XMLN * p_node, onvif_DeviceIOCapabilities * p_res);
BOOL parse_RelayOutput(XMLN * p_node, onvif_RelayOutput * p_res);
BOOL parse_RelayOutputOptions(XMLN * p_node, onvif_RelayOutputOptions * p_res);
BOOL parse_DigitalInput(XMLN * p_node, onvif_DigitalInput * p_res);
BOOL parse_tmd_GetServiceCapabilities(XMLN * p_node, tmd_GetServiceCapabilities_RES * p_res);
BOOL parse_tmd_GetRelayOutputs(XMLN * p_node, tmd_GetRelayOutputs_RES * p_res);
BOOL parse_tmd_GetRelayOutputOptions(XMLN * p_node, tmd_GetRelayOutputOptions_RES * p_res);
BOOL parse_tmd_GetDigitalInputs(XMLN * p_node, tmd_GetDigitalInputs_RES * p_res);
BOOL parse_tmd_GetDigitalInputConfigurationOptions(XMLN * p_node, tmd_GetDigitalInputConfigurationOptions_RES * p_res);
BOOL parse_tmd_GetSerialPorts(XMLN * p_node, tmd_GetSerialPorts_RES * p_res);
BOOL parse_tmd_GetSerialPortConfiguration(XMLN * p_node, tmd_GetSerialPortConfiguration_RES * p_res);
BOOL parse_tmd_GetSerialPortConfigurationOptions(XMLN * p_node, tmd_GetSerialPortConfigurationOptions_RES * p_res);
BOOL parse_tmd_SendReceiveSerialCommand(XMLN * p_node, tmd_SendReceiveSerialCommand_RES * p_res);

BOOL parse_RecordingServiceCapabilities(XMLN * p_node, onvif_RecordingCapabilities * p_res);
BOOL parse_RecordingService(XMLN * p_node, onvif_RecordingCapabilities * p_res);
BOOL parse_SearchServiceCapabilities(XMLN * p_node, onvif_SearchCapabilities * p_res);
BOOL parse_SearchService(XMLN * p_node, onvif_SearchCapabilities * p_res);
BOOL parse_ReplayServiceCapabilities(XMLN * p_node, onvif_ReplayCapabilities * p_res);
BOOL parse_ReplayService(XMLN * p_node, onvif_ReplayCapabilities * p_res);
BOOL parse_trc_GetServiceCapabilities(XMLN * p_node, trc_GetServiceCapabilities_RES * p_res);
BOOL parse_trc_CreateRecording(XMLN * p_node, trc_CreateRecording_RES * p_res);
BOOL parse_trc_GetRecordings(XMLN * p_node, trc_GetRecordings_RES * p_res);
BOOL parse_trc_GetRecordingConfiguration(XMLN * p_node, trc_GetRecordingConfiguration_RES * p_res);
BOOL parse_trc_GetRecordingOptions(XMLN * p_node, trc_GetRecordingOptions_RES * p_res);
BOOL parse_trc_CreateTrack(XMLN * p_node, trc_CreateTrack_RES * p_res);
BOOL parse_trc_GetTrackConfiguration(XMLN * p_node, trc_GetTrackConfiguration_RES * p_res);
BOOL parse_trc_CreateRecordingJob(XMLN * p_node, trc_CreateRecordingJob_RES * p_res);
BOOL parse_trc_GetRecordingJobs(XMLN * p_node, trc_GetRecordingJobs_RES * p_res);
BOOL parse_trc_SetRecordingJobConfiguration(XMLN * p_node, trc_SetRecordingJobConfiguration_RES * p_res);
BOOL parse_trc_GetRecordingJobConfiguration(XMLN * p_node, trc_GetRecordingJobConfiguration_RES * p_res);
BOOL parse_trc_GetRecordingJobState(XMLN * p_node, trc_GetRecordingJobState_RES * p_res);
BOOL parse_trc_ExportRecordedData(XMLN * p_node, trc_ExportRecordedData_RES * p_res);
BOOL parse_trc_StopExportRecordedData(XMLN * p_node, trc_StopExportRecordedData_RES * p_res);
BOOL parse_trc_GetExportRecordedDataState(XMLN * p_node, trc_GetExportRecordedDataState_RES * p_res);
BOOL parse_trp_GetServiceCapabilities(XMLN * p_node, trp_GetServiceCapabilities_RES * p_res);
BOOL parse_trp_GetReplayUri(XMLN * p_node, trp_GetReplayUri_RES * p_res);
BOOL parse_trp_GetReplayConfiguration(XMLN * p_node, trp_GetReplayConfiguration_RES * p_res);
BOOL parse_tse_GetServiceCapabilities(XMLN * p_node, tse_GetServiceCapabilities_RES * p_res);
BOOL parse_tse_GetRecordingSummary(XMLN * p_node, tse_GetRecordingSummary_RES * p_res);
BOOL parse_tse_GetRecordingInformation(XMLN * p_node, tse_GetRecordingInformation_RES * p_res);
BOOL parse_tse_GetMediaAttributes(XMLN * p_node, tse_GetMediaAttributes_RES * p_res);
BOOL parse_tse_FindRecordings(XMLN * p_node, tse_FindRecordings_RES * p_res);
BOOL parse_tse_GetRecordingSearchResults(XMLN * p_node, tse_GetRecordingSearchResults_RES * p_res);
BOOL parse_tse_FindEvents(XMLN * p_node, tse_FindEvents_RES * p_res);
BOOL parse_tse_GetEventSearchResults(XMLN * p_node, tse_GetEventSearchResults_RES * p_res);
BOOL parse_tse_FindMetadata(XMLN * p_node, tse_FindMetadata_RES * p_res);
BOOL parse_tse_GetMetadataSearchResults(XMLN * p_node, tse_GetMetadataSearchResults_RES * p_res);
BOOL parse_tse_FindPTZPosition(XMLN * p_node, tse_FindPTZPosition_RES * p_res);
BOOL parse_tse_GetPTZPositionSearchResults(XMLN * p_node, tse_GetPTZPositionSearchResults_RES * p_res);
BOOL parse_tse_GetSearchState(XMLN * p_node, tse_GetSearchState_RES * p_res);
BOOL parse_tse_EndSearch(XMLN * p_node, tse_EndSearch_RES * p_res);

BOOL parse_AccessControlServiceCapabilities(XMLN * p_node, onvif_AccessControlCapabilities * p_res);
BOOL parse_AccessControlService(XMLN * p_node, onvif_AccessControlCapabilities * p_res);
BOOL parse_DoorControlServiceCapabilities(XMLN * p_node, onvif_DoorControlCapabilities * p_res);
BOOL parse_DoorControlService(XMLN * p_node, onvif_DoorControlCapabilities * p_res);
BOOL parse_tac_GetServiceCapabilities(XMLN * p_node, tac_GetServiceCapabilities_RES * p_res);
BOOL parse_tac_GetAccessPointInfoList(XMLN * p_node, tac_GetAccessPointInfoList_RES * p_res);
BOOL parse_tac_GetAccessPointInfo(XMLN * p_node, tac_GetAccessPointInfo_RES * p_res);
BOOL parse_tac_GetAccessPointList(XMLN * p_node, tac_GetAccessPointList_RES * p_res);
BOOL parse_tac_GetAccessPoints(XMLN * p_node, tac_GetAccessPoints_RES * p_res);
BOOL parse_tac_CreateAccessPoint(XMLN * p_node, tac_CreateAccessPoint_RES * p_res);
BOOL parse_tac_GetAreaInfoList(XMLN * p_node, tac_GetAreaInfoList_RES * p_res);
BOOL parse_tac_GetAreaInfo(XMLN * p_node, tac_GetAreaInfo_RES * p_res);
BOOL parse_tac_GetAreaList(XMLN * p_node, tac_GetAreaList_RES * p_res);
BOOL parse_tac_GetAreas(XMLN * p_node, tac_GetAreas_RES * p_res);
BOOL parse_tac_CreateArea(XMLN * p_node, tac_CreateArea_RES * p_res);
BOOL parse_tac_GetAccessPointState(XMLN * p_node, tac_GetAccessPointState_RES * p_res);
BOOL parse_tdc_GetServiceCapabilities(XMLN * p_node, tdc_GetServiceCapabilities_RES * p_res);
BOOL parse_tdc_GetDoorInfoList(XMLN * p_node, tdc_GetDoorInfoList_RES * p_res);
BOOL parse_tdc_GetDoorInfo(XMLN * p_node, tdc_GetDoorInfo_RES * p_res);
BOOL parse_tdc_GetDoorState(XMLN * p_node, tdc_GetDoorState_RES * p_res);
BOOL parse_tdc_GetDoors(XMLN * p_node, tdc_GetDoors_RES * p_res);
BOOL parse_tdc_GetDoorList(XMLN * p_node, tdc_GetDoorList_RES * p_res);
BOOL parse_tdc_CreateDoor(XMLN * p_node, tdc_CreateDoor_RES * p_res);

BOOL parse_ThermalServiceCapabilities(XMLN * p_node, onvif_ThermalCapabilities * p_res);
BOOL parse_ThermalService(XMLN * p_node, onvif_ThermalCapabilities * p_res);
BOOL parse_tth_GetServiceCapabilities(XMLN * p_node, tth_GetServiceCapabilities_RES * p_res);
BOOL parse_tth_GetConfigurations(XMLN * p_node, tth_GetConfigurations_RES * p_req);
BOOL parse_tth_GetConfiguration(XMLN * p_node, tth_GetConfiguration_RES * p_req);
BOOL parse_tth_GetConfigurationOptions(XMLN * p_node, tth_GetConfigurationOptions_RES * p_req);
BOOL parse_tth_GetRadiometryConfiguration(XMLN * p_node, tth_GetRadiometryConfiguration_RES * p_req);
BOOL parse_tth_GetRadiometryConfigurationOptions(XMLN * p_node, tth_GetRadiometryConfigurationOptions_RES * p_req);

BOOL parse_CredentialServiceCapabilities(XMLN * p_node, onvif_CredentialCapabilities * p_res);
BOOL parse_CredentialService(XMLN * p_node, onvif_CredentialCapabilities * p_res);
BOOL parse_tcr_GetServiceCapabilities(XMLN * p_node, tcr_GetServiceCapabilities_RES * p_res);
BOOL parse_tcr_GetCredentialInfo(XMLN * p_node, tcr_GetCredentialInfo_RES * p_res);
BOOL parse_tcr_GetCredentialInfoList(XMLN * p_node, tcr_GetCredentialInfoList_RES * p_res);
BOOL parse_tcr_GetCredentials(XMLN * p_node, tcr_GetCredentials_RES * p_res);
BOOL parse_tcr_GetCredentialList(XMLN * p_node, tcr_GetCredentialList_RES * p_res);
BOOL parse_tcr_CreateCredential(XMLN * p_node, tcr_CreateCredential_RES * p_res);
BOOL parse_tcr_GetCredentialState(XMLN * p_node, tcr_GetCredentialState_RES * p_res);
BOOL parse_tcr_GetSupportedFormatTypes(XMLN * p_node, tcr_GetSupportedFormatTypes_RES * p_res);
BOOL parse_tcr_GetCredentialIdentifiers(XMLN * p_node, tcr_GetCredentialIdentifiers_RES * p_res);
BOOL parse_tcr_GetCredentialAccessProfiles(XMLN * p_node, tcr_GetCredentialAccessProfiles_RES * p_res);

BOOL parse_AccessRulesServiceCapabilities(XMLN * p_node, onvif_AccessRulesCapabilities * p_res);
BOOL parse_AccessRulesService(XMLN * p_node, onvif_AccessRulesCapabilities * p_res);
BOOL parse_tar_GetServiceCapabilities(XMLN * p_node, tar_GetServiceCapabilities_RES * p_res);
BOOL parse_tar_GetAccessProfileInfo(XMLN * p_node, tar_GetAccessProfileInfo_RES * p_res);
BOOL parse_tar_GetAccessProfileInfoList(XMLN * p_node, tar_GetAccessProfileInfoList_RES * p_res);
BOOL parse_tar_GetAccessProfiles(XMLN * p_node, tar_GetAccessProfiles_RES * p_res);
BOOL parse_tar_GetAccessProfileList(XMLN * p_node, tar_GetAccessProfileList_RES * p_res);
BOOL parse_tar_CreateAccessProfile(XMLN * p_node, tar_CreateAccessProfile_RES * p_res);

BOOL parse_ScheduleServiceCapabilities(XMLN * p_node, onvif_ScheduleCapabilities * p_res);
BOOL parse_ScheduleService(XMLN * p_node, onvif_ScheduleCapabilities * p_res);
BOOL parse_tsc_GetServiceCapabilities(XMLN * p_node, tsc_GetServiceCapabilities_RES * p_res);
BOOL parse_tsc_GetScheduleInfo(XMLN * p_node, tsc_GetScheduleInfo_RES * p_res);
BOOL parse_tsc_GetScheduleInfoList(XMLN * p_node, tsc_GetScheduleInfoList_RES * p_res);
BOOL parse_tsc_GetSchedules(XMLN * p_node, tsc_GetSchedules_RES * p_res);
BOOL parse_tsc_GetScheduleList(XMLN * p_node, tsc_GetScheduleList_RES * p_res);
BOOL parse_tsc_CreateSchedule(XMLN * p_node, tsc_CreateSchedule_RES * p_res);
BOOL parse_tsc_GetSpecialDayGroupInfo(XMLN * p_node, tsc_GetSpecialDayGroupInfo_RES * p_res);
BOOL parse_tsc_GetSpecialDayGroupInfoList(XMLN * p_node, tsc_GetSpecialDayGroupInfoList_RES * p_res);
BOOL parse_tsc_GetSpecialDayGroups(XMLN * p_node, tsc_GetSpecialDayGroups_RES * p_res);
BOOL parse_tsc_GetSpecialDayGroupList(XMLN * p_node, tsc_GetSpecialDayGroupList_RES * p_res);
BOOL parse_tsc_CreateSpecialDayGroup(XMLN * p_node, tsc_CreateSpecialDayGroup_RES * p_res);
BOOL parse_tsc_GetScheduleState(XMLN * p_node, tsc_GetScheduleState_RES * p_res);

BOOL parse_ReceiverServiceCapabilities(XMLN * p_node, onvif_ReceiverCapabilities * p_res);
BOOL parse_ReceiverService(XMLN * p_node, onvif_ReceiverCapabilities * p_res);
BOOL parse_trv_GetServiceCapabilities(XMLN * p_node, trv_GetServiceCapabilities_RES * p_res);
BOOL parse_trv_GetReceivers(XMLN * p_node, trv_GetReceivers_RES * p_res);
BOOL parse_trv_GetReceiver(XMLN * p_node, trv_GetReceiver_RES * p_res);
BOOL parse_trv_CreateReceiver(XMLN * p_node, trv_CreateReceiver_RES * p_res);
BOOL parse_trv_GetReceiverState(XMLN * p_node, trv_GetReceiverState_RES * p_res);

BOOL parse_ProvisioningServiceCapabilities(XMLN * p_node, onvif_ProvisioningCapabilities * p_res);
BOOL parse_ProvisioningService(XMLN * p_node, onvif_ProvisioningCapabilities * p_res);
BOOL parse_tpv_GetServiceCapabilities(XMLN * p_node, tpv_GetServiceCapabilities_RES * p_res);
BOOL parse_tpv_GetUsage(XMLN * p_node, tpv_GetUsage_RES * p_res);


#ifdef __cplusplus
}
#endif

#endif


