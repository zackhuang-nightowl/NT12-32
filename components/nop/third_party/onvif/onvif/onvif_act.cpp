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
#include "onvif_act.h"


/***************************************************************************************/
OVFACTS g_onvif_acts[] = 
{
    // device
    {etdsGetCapabilities, "http://www.onvif.org/ver10/device/wsdl/GetCapabilities"},
    {etdsGetServices, "http://www.onvif.org/ver10/device/wsdl/GetServices"},
    {etdsGetServiceCapabilities, "http://www.onvif.org/ver10/device/wsdl/GetServiceCapabilities"},
    {etdsGetDeviceInformation, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation"},
    {etdsGetUsers, "http://www.onvif.org/ver10/device/wsdl/GetUsers"},
    {etdsCreateUsers, "http://www.onvif.org/ver10/device/wsdl/CreateUsers"},
    {etdsDeleteUsers, "http://www.onvif.org/ver10/device/wsdl/DeleteUsers"},
    {etdsSetUser, "http://www.onvif.org/ver10/device/wsdl/SetUser"},
    {etdsGetRemoteUser, "http://www.onvif.org/ver10/device/wsdl/GetRemoteUser"},
    {etdsSetRemoteUser, "http://www.onvif.org/ver10/device/wsdl/SetRemoteUser"},
    {etdsGetNetworkInterfaces, "http://www.onvif.org/ver10/device/wsdl/GetNetworkInterfaces"},
    {etdsSetNetworkInterfaces, "http://www.onvif.org/ver10/device/wsdl/SetNetworkInterfaces"},
    {etdsGetNTP, "http://www.onvif.org/ver10/device/wsdl/GetNTP"},
    {etdsSetNTP, "http://www.onvif.org/ver10/device/wsdl/SetNTP"},
    {etdsGetHostname, "http://www.onvif.org/ver10/device/wsdl/GetHostname"},
    {etdsSetHostname, "http://www.onvif.org/ver10/device/wsdl/SetHostname"},
    {etdsSetHostnameFromDHCP, "http://www.onvif.org/ver10/device/wsdl/SetHostnameFromDHCP"},
    {etdsGetDNS, "http://www.onvif.org/ver10/device/wsdl/GetDNS"},
    {etdsSetDNS, "http://www.onvif.org/ver10/device/wsdl/SetDNS"},
    {etdsGetDynamicDNS, "http://www.onvif.org/ver10/device/wsdl/GetDynamicDNS"},
    {etdsSetDynamicDNS, "http://www.onvif.org/ver10/device/wsdl/SetDynamicDNS"},
    {etdsGetNetworkProtocols, "http://www.onvif.org/ver10/device/wsdl/GetNetworkProtocols"},
    {etdsSetNetworkProtocols, "http://www.onvif.org/ver10/device/wsdl/SetNetworkProtocols"},
    {etdsGetDiscoveryMode, "http://www.onvif.org/ver10/device/wsdl/GetDiscoveryMode"},
    {etdsSetDiscoveryMode, "http://www.onvif.org/ver10/device/wsdl/SetDiscoveryMode"},
    {etdsGetNetworkDefaultGateway, "http://www.onvif.org/ver10/device/wsdl/GetNetworkDefaultGateway"},
    {etdsSetNetworkDefaultGateway, "http://www.onvif.org/ver10/device/wsdl/SetNetworkDefaultGateway"},
    {etdsGetZeroConfiguration, "http://www.onvif.org/ver10/device/wsdl/GetZeroConfiguration"},
    {etdsSetZeroConfiguration, "http://www.onvif.org/ver10/device/wsdl/SetZeroConfiguration"},
    {etdsGetEndpointReference, "http://www.onvif.org/ver10/device/wsdl/GetEndpointReference"},
    {etdsSendAuxiliaryCommand, "http://www.onvif.org/ver10/device/wsdl/SendAuxiliaryCommand"},
    {etdsGetRelayOutputs, "http://www.onvif.org/ver10/device/wsdl/GetRelayOutputs"},
    {etdsSetRelayOutputSettings, "http://www.onvif.org/ver10/device/wsdl/SetRelayOutputSettings"},
    {etdsSetRelayOutputState, "http://www.onvif.org/ver10/device/wsdl/SetRelayOutputState"},
    {etdsGetSystemDateAndTime, "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime"},
    {etdsSetSystemDateAndTime, "http://www.onvif.org/ver10/device/wsdl/SetSystemDateAndTime"},
    {etdsSystemReboot, "http://www.onvif.org/ver10/device/wsdl/SystemReboot"},
    {etdsSetSystemFactoryDefault, "http://www.onvif.org/ver10/device/wsdl/SetSystemFactoryDefault"},
    {etdsGetSystemLog, "http://www.onvif.org/ver10/device/wsdl/GetSystemLog"},
    {etdsGetScopes, "http://www.onvif.org/ver10/device/wsdl/GetScopes"},
    {etdsSetScopes, "http://www.onvif.org/ver10/device/wsdl/SetScopes"},
    {etdsAddScopes, "http://www.onvif.org/ver10/device/wsdl/AddScopes"},
    {etdsRemoveScopes, "http://www.onvif.org/ver10/device/wsdl/RemoveScopes"},
    {etdsStartFirmwareUpgrade, "http://www.onvif.org/ver10/device/wsdl/StartFirmwareUpgrade"},
    {etdsGetSystemUris, "http://www.onvif.org/ver10/device/wsdl/GetSystemUris"},
    {etdsStartSystemRestore, "http://www.onvif.org/ver10/device/wsdl/StartSystemRestore"},
    {etdsGetWsdlUrl, "http://www.onvif.org/ver10/device/wsdl/GetWsdlUrl"},
    {etdsGetDot11Capabilities, "http://www.onvif.org/ver10/device/wsdl/GetDot11Capabilities"},
    {etdsGetDot11Status, "http://www.onvif.org/ver10/device/wsdl/GetDot11Status"},
    {etdsScanAvailableDot11Networks, "http://www.onvif.org/ver10/device/wsdl/ScanAvailableDot11Networks"},
    {etdsGetGeoLocation, "http://www.onvif.org/ver10/device/wsdl/GetGeoLocation"},
    {etdsSetGeoLocation, "http://www.onvif.org/ver10/device/wsdl/SetGeoLocation"},
    {etdsDeleteGeoLocation, "http://www.onvif.org/ver10/device/wsdl/DeleteGeoLocation"},
    {etdsSetHashingAlgorithm, "http://www.onvif.org/ver10/device/wsdl/SetHashingAlgorithm"},

#ifdef IPFILTER_SUPPORT
    {etdsGetIPAddressFilter, "http://www.onvif.org/ver10/device/wsdl/GetIPAddressFilter"},
    {etdsSetIPAddressFilter, "http://www.onvif.org/ver10/device/wsdl/SetIPAddressFilter"},
    {etdsAddIPAddressFilter, "http://www.onvif.org/ver10/device/wsdl/AddIPAddressFilter"},
    {etdsRemoveIPAddressFilter, "http://www.onvif.org/ver10/device/wsdl/RemoveIPAddressFilter"},
#endif

    {etdsGetAccessPolicy, "http://www.onvif.org/ver10/device/wsdl/GetAccessPolicy"},
    {etdsSetAccessPolicy, "http://www.onvif.org/ver10/device/wsdl/SetAccessPolicy"},
    {etdsGetStorageConfigurations, "http://www.onvif.org/ver10/device/wsdl/GetStorageConfigurations"},
    {etdsCreateStorageConfiguration, "http://www.onvif.org/ver10/device/wsdl/CreateStorageConfiguration"},
    {etdsGetStorageConfiguration, "http://www.onvif.org/ver10/device/wsdl/GetStorageConfiguration"},
    {etdsSetStorageConfiguration, "http://www.onvif.org/ver10/device/wsdl/SetStorageConfiguration"},
    {etdsDeleteStorageConfiguration, "http://www.onvif.org/ver10/device/wsdl/DeleteStorageConfiguration"},
    
    // end of device

    // media
    {etrtGetServiceCapabilities, "http://www.onvif.org/ver10/media/wsdl/GetServiceCapabilities"},
    {etrtGetVideoSources, "http://www.onvif.org/ver10/media/wsdl/GetVideoSources"},
    {etrtGetAudioSources, "http://www.onvif.org/ver10/media/wsdl/GetAudioSources"},
    {etrtCreateProfile, "http://www.onvif.org/ver10/media/wsdl/CreateProfile"},
    {etrtGetProfile, "http://www.onvif.org/ver10/media/wsdl/GetProfile"},
    {etrtGetProfiles, "http://www.onvif.org/ver10/media/wsdl/GetProfiles"},
    {etrtAddVideoEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddVideoEncoderConfiguration"},
    {etrtAddVideoSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddVideoSourceConfiguration"},
    {etrtAddAudioEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddAudioEncoderConfiguration"},
    {etrtAddAudioSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddAudioSourceConfiguration"},
    {etrtGetVideoSourceModes, "http://www.onvif.org/ver10/media/wsdl/GetVideoSourceModes"},
    {etrtSetVideoSourceMode, "http://www.onvif.org/ver10/media/wsdl/SetVideoSourceMode"},
    {etrtAddPTZConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddPTZConfiguration"},
    {etrtRemoveVideoEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveVideoEncoderConfiguration"},
    {etrtRemoveVideoSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveVideoSourceConfiguration"},
    {etrtRemoveAudioEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveAudioEncoderConfiguration"},
    {etrtRemoveAudioSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveAudioSourceConfiguration"},
    {etrtRemovePTZConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemovePTZConfiguration"},
    {etrtDeleteProfile, "http://www.onvif.org/ver10/media/wsdl/DeleteProfile"},
    {etrtGetVideoSourceConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetVideoSourceConfigurations"},
    {etrtGetVideoEncoderConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfigurations"},
    {etrtGetAudioSourceConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetAudioSourceConfigurations"},
    {etrtGetAudioEncoderConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetAudioEncoderConfigurations"},
    {etrtGetVideoSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetVideoSourceConfiguration"},
    {etrtGetVideoEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfiguration"},
    {etrtGetAudioSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetAudioSourceConfiguration"},
    {etrtGetAudioEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetAudioEncoderConfiguration"},
    {etrtSetVideoSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetVideoSourceConfiguration"},
    {etrtSetVideoEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetVideoEncoderConfiguration"},
    {etrtSetAudioSourceConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetAudioSourceConfiguration"},
    {etrtSetAudioEncoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetAudioEncoderConfiguration"},
    {etrtGetVideoSourceConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetVideoSourceConfigurationOptions"},
    {etrtGetVideoEncoderConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfigurationOptions"},
    {etrtGetAudioSourceConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetAudioSourceConfigurationOptions"},
    {etrtGetAudioEncoderConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetAudioEncoderConfigurationOptions"},
    {etrtGetStreamUri,    "http://www.onvif.org/ver10/media/wsdl/GetStreamUri"},
    {etrtSetSynchronizationPoint, "http://www.onvif.org/ver10/media/wsdl/SetSynchronizationPoint"},
    {etrtGetSnapshotUri, "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri"},
    {etrtGetGuaranteedNumberOfVideoEncoderInstances, "http://www.onvif.org/ver10/media/wsdl/GetGuaranteedNumberOfVideoEncoderInstances"},
    {etrtGetAudioOutputs, "http://www.onvif.org/ver10/media/wsdl/GetAudioOutputs"},
    {etrtGetAudioOutputConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetAudioOutputConfigurations"},
    {etrtGetAudioOutputConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetAudioOutputConfiguration"},
    {etrtGetAudioOutputConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetAudioOutputConfigurationOptions"},
    {etrtSetAudioOutputConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetAudioOutputConfiguration"},
    {etrtGetAudioDecoderConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetAudioDecoderConfigurations"},
    {etrtGetAudioDecoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetAudioDecoderConfiguration"},
    {etrtGetAudioDecoderConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetAudioDecoderConfigurationOptions"},
    {etrtSetAudioDecoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetAudioDecoderConfiguration"},
    {etrtAddAudioOutputConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddAudioOutputConfiguration"},
    {etrtAddAudioDecoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddAudioDecoderConfiguration"},
    {etrtRemoveAudioOutputConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveAudioOutputConfiguration"},
    {etrtRemoveAudioDecoderConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveAudioDecoderConfiguration"},
    {etrtGetOSDs, "http://www.onvif.org/ver10/media/wsdl/GetOSDs"},
    {etrtGetOSD, "http://www.onvif.org/ver10/media/wsdl/GetOSD"},
    {etrtSetOSD, "http://www.onvif.org/ver10/media/wsdl/SetOSD"},
    {etrtGetOSDOptions, "http://www.onvif.org/ver10/media/wsdl/GetOSDOptions"},
    {etrtCreateOSD, "http://www.onvif.org/ver10/media/wsdl/CreateOSD"},
    {etrtDeleteOSD, "http://www.onvif.org/ver10/media/wsdl/DeleteOSD"},
    {etrtGetVideoAnalyticsConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetVideoAnalyticsConfigurations"},
    {etrtAddVideoAnalyticsConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddVideoAnalyticsConfiguration"},
    {etrtGetVideoAnalyticsConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetVideoAnalyticsConfigurations"},
    {etrtRemoveVideoAnalyticsConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveVideoAnalyticsConfiguration"},
    {etrtSetVideoAnalyticsConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetVideoAnalyticsConfiguration"},
    {etrtGetMetadataConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetMetadataConfigurations"},
    {etrtAddMetadataConfiguration, "http://www.onvif.org/ver10/media/wsdl/AddMetadataConfiguration"},
    {etrtGetMetadataConfiguration, "http://www.onvif.org/ver10/media/wsdl/GetMetadataConfiguration"},
    {etrtRemoveMetadataConfiguration, "http://www.onvif.org/ver10/media/wsdl/RemoveMetadataConfiguration"},
    {etrtSetMetadataConfiguration, "http://www.onvif.org/ver10/media/wsdl/SetMetadataConfiguration"},
    {etrtGetMetadataConfigurationOptions, "http://www.onvif.org/ver10/media/wsdl/GetMetadataConfigurationOptions"},
    {etrtGetCompatibleVideoEncoderConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetCompatibleVideoEncoderConfigurations"},
    {etrtGetCompatibleAudioEncoderConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetCompatibleAudioEncoderConfigurations"},
    {etrtGetCompatibleVideoAnalyticsConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetCompatibleVideoAnalyticsConfigurations"},
    {etrtGetCompatibleMetadataConfigurations, "http://www.onvif.org/ver10/media/wsdl/GetCompatibleMetadataConfigurations"},
    // end of media

    // media 2
    {etr2GetServiceCapabilities, "http://www.onvif.org/ver20/media/wsdl/GetServiceCapabilities"},
    {etr2GetVideoEncoderConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetVideoEncoderConfigurations"},
    {etr2SetVideoEncoderConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetVideoEncoderConfiguration"},
    {etr2GetVideoEncoderConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetVideoEncoderConfigurationOptions"},
    {etr2GetProfiles, "http://www.onvif.org/ver20/media/wsdl/GetProfiles"},
    {etr2CreateProfile, "http://www.onvif.org/ver20/media/wsdl/CreateProfile"},
    {etr2DeleteProfile, "http://www.onvif.org/ver20/media/wsdl/DeleteProfile"},
    {etr2GetStreamUri, "http://www.onvif.org/ver20/media/wsdl/GetStreamUri"},
    {etr2GetVideoSourceConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetVideoSourceConfigurations"},
    {etr2GetVideoSourceConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetVideoSourceConfigurationOptions"},
    {etr2SetVideoSourceConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetVideoSourceConfiguration"},
    {etr2SetSynchronizationPoint, "http://www.onvif.org/ver20/media/wsdl/SetSynchronizationPoint"},
    {etr2GetMetadataConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetMetadataConfigurations"},
    {etr2GetMetadataConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetMetadataConfigurationOptions"},
    {etr2SetMetadataConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetMetadataConfiguration"},
    {etr2GetAudioEncoderConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetAudioEncoderConfigurations"},
    {etr2GetAudioSourceConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetAudioSourceConfigurations"},
    {etr2GetAudioSourceConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetAudioSourceConfigurationOptions"},
    {etr2SetAudioSourceConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetAudioSourceConfiguration"},
    {etr2SetAudioEncoderConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetAudioEncoderConfiguration"},
    {etr2GetAudioEncoderConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetAudioEncoderConfigurationOptions"},
    {etr2AddConfiguration, "http://www.onvif.org/ver20/media/wsdl/AddConfiguration"},
    {etr2RemoveConfiguration, "http://www.onvif.org/ver20/media/wsdl/RemoveConfiguration"},
    {etr2GetVideoEncoderInstances, "http://www.onvif.org/ver20/media/wsdl/GetVideoEncoderInstances"},
    {etr2GetAudioOutputConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetAudioOutputConfigurations"},
    {etr2GetAudioOutputConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetAudioOutputConfigurationOptions"},
    {etr2SetAudioOutputConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetAudioOutputConfiguration"},
    {etr2GetAudioDecoderConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetAudioDecoderConfigurations"},
    {etr2GetAudioDecoderConfigurationOptions, "http://www.onvif.org/ver20/media/wsdl/GetAudioDecoderConfigurationOptions"},
    {etr2SetAudioDecoderConfiguration, "http://www.onvif.org/ver20/media/wsdl/SetAudioDecoderConfiguration"},
    {etr2GetSnapshotUri, "http://www.onvif.org/ver20/media/wsdl/GetSnapshotUri"},
    {etr2StartMulticastStreaming, "http://www.onvif.org/ver20/media/wsdl/StartMulticastStreaming"},
    {etr2StopMulticastStreaming, "http://www.onvif.org/ver20/media/wsdl/StopMulticastStreaming"},
    {etr2GetVideoSourceModes, "http://www.onvif.org/ver20/media/wsdl/GetVideoSourceModes"},
    {etr2SetVideoSourceMode, "http://www.onvif.org/ver20/media/wsdl/SetVideoSourceMode"},
    {etr2CreateOSD, "http://www.onvif.org/ver20/media/wsdl/CreateOSD"},
    {etr2DeleteOSD, "http://www.onvif.org/ver20/media/wsdl/DeleteOSD"},
    {etr2GetOSDs, "http://www.onvif.org/ver20/media/wsdl/GetOSDs"},
    {etr2SetOSD, "http://www.onvif.org/ver20/media/wsdl/SetOSD"},
    {etr2GetOSDOptions, "http://www.onvif.org/ver20/media/wsdl/GetOSDOptions"},
    {etr2GetAnalyticsConfigurations, "http://www.onvif.org/ver20/media/wsdl/GetAnalyticsConfigurations"},
    {etr2GetMasks, "http://www.onvif.org/ver20/media/wsdl/GetMasks"},
    {etr2SetMask, "http://www.onvif.org/ver20/media/wsdl/SetMask"},
    {etr2CreateMask, "http://www.onvif.org/ver20/media/wsdl/CreateMask"},
    {etr2DeleteMask, "http://www.onvif.org/ver20/media/wsdl/DeleteMask"},
    {etr2GetMaskOptions, "http://www.onvif.org/ver20/media/wsdl/GetMaskOptions"},
    // end of media 2
    
    // PTZ
    {eptzGetServiceCapabilities, "http://www.onvif.org/ver20/ptz/wsdl/GetServiceCapabilities"},
    {eptzGetNodes, "http://www.onvif.org/ver20/ptz/wsdl/GetNodes"},
    {eptzGetNode, "http://www.onvif.org/ver20/ptz/wsdl/GetNode"},
    {eptzGetPresets, "http://www.onvif.org/ver20/ptz/wsdl/GetPresets"},
    {eptzSetPreset, "http://www.onvif.org/ver20/ptz/wsdl/SetPreset"},
    {eptzRemovePreset, "http://www.onvif.org/ver20/ptz/wsdl/RemovePreset"},
    {eptzGotoPreset, "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset"},
    {eptzGotoHomePosition, "http://www.onvif.org/ver20/ptz/wsdl/GotoHomePosition"},
    {eptzSetHomePosition, "http://www.onvif.org/ver20/ptz/wsdl/SetHomePosition"},
    {eptzGetStatus, "http://www.onvif.org/ver20/ptz/wsdl/GetStatus"},
    {eptzContinuousMove, "http://www.onvif.org/ver20/ptz/wsdl/ContinuousMove"},
    {eptzRelativeMove, "http://www.onvif.org/ver20/ptz/wsdl/RelativeMove"},
    {eptzAbsoluteMove, "http://www.onvif.org/ver20/ptz/wsdl/AbsoluteMove"},
    {eptzStop, "http://www.onvif.org/ver20/ptz/wsdl/Stop"},
    {eptzGetConfigurations, "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurations"},
    {eptzGetConfiguration, "http://www.onvif.org/ver20/ptz/wsdl/GetConfiguration"},
    {eptzSetConfiguration, "http://www.onvif.org/ver20/ptz/wsdl/SetConfiguration"},
    {eptzGetConfigurationOptions, "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurationOptions"},
    {eptzGetPresetTours, "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTours"},
    {eptzGetPresetTour, "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTour"},
    {eptzGetPresetTourOptions, "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTourOptions"},
    {eptzCreatePresetTour, "http://www.onvif.org/ver20/ptz/wsdl/CreatePresetTour"},
    {eptzModifyPresetTour, "http://www.onvif.org/ver20/ptz/wsdl/ModifyPresetTour"},
    {eptzOperatePresetTour, "http://www.onvif.org/ver20/ptz/wsdl/OperatePresetTour"},
    {eptzRemovePresetTour, "http://www.onvif.org/ver20/ptz/wsdl/RemovePresetTour"},
    {eptzSendAuxiliaryCommand, "http://www.onvif.org/ver20/ptz/wsdl/SendAuxiliaryCommand"},
    {eptzGeoMove, "http://www.onvif.org/ver20/ptz/wsdl/GeoMove"},
    // end of PTZ

    // event
    {etevGetServiceCapabilities, "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetServiceCapabilitiesRequest"},
    {etevGetEventProperties, "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest"},
    {etevRenew, "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest"},
    {etevUnsubscribe, "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest"},
    {etevSubscribe, "http://docs.oasis-open.org/wsn/bw-2/NotificationProducer/SubscribeRequest"},
    {etevPauseSubscription, "http://docs.oasis-open.org/wsn/bw-2/PausableSubscriptionManager/PauseSubscriptionRequest"},
    {etevResumeSubscription, "http://docs.oasis-open.org/wsn/bw-2/PausableSubscriptionManager/ResumeSubscriptionRequest"},
    {etevCreatePullPointSubscription, "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionRequest"},
    {etevDestroyPullPoint, "http://docs.oasis-open.org/wsn/bw-2/PullPoint/DestroyPullPointRequest"},
    {etevPullMessages, "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest"},
    {etevGetMessages, "http://docs.oasis-open.org/wsn/bw-2/PullPoint/GetMessagesRequest"},
    {etevSeek, "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SeekRequest"},
    {etevSetSynchronizationPoint, "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SetSynchronizationPointRequest"},
    // end of event

    // image
    {eimgGetServiceCapabilities, "http://www.onvif.org/ver20/imaging/wsdl/GetServiceCapabilities"},
    {eimgGetImagingSettings, "http://www.onvif.org/ver20/imaging/wsdl/GetImagingSettings"},
    {eimgSetImagingSettings, "http://www.onvif.org/ver20/imaging/wsdl/SetImagingSettings"},
    {eimgGetOptions, "http://www.onvif.org/ver20/imaging/wsdl/GetOptions"},    
    {eimgMove, "http://www.onvif.org/ver20/imaging/wsdl/Move"},
    {eimgStop, "http://www.onvif.org/ver20/imaging/wsdl/FocusStop"},
    {eimgGetStatus, "http://www.onvif.org/ver20/imaging/wsdl/GetStatus"},
    {eimgGetMoveOptions, "http://www.onvif.org/ver20/imaging/wsdl/GetMoveOptions"},
    {eimgGetPresets, "http://www.onvif.org/ver20/imaging/wsdl/GetPresets"},
    {eimgGetCurrentPreset, "http://www.onvif.org/ver20/imaging/wsdl/GetCurrentPreset"},
    {eimgSetCurrentPreset, "http://www.onvif.org/ver20/imaging/wsdl/SetCurrentPreset"},
    // end of image

    {etanGetServiceCapabilities, "http://www.onvif.org/ver20/analytics/wsdl/GetServiceCapabilities"},
    {etanGetSupportedRules, "http://www.onvif.org/ver20/analytics/wsdl/GetSupportedRules"},
    {etanCreateRules, "http://www.onvif.org/ver20/analytics/wsdl/CreateRules"},
    {etanDeleteRules, "http://www.onvif.org/ver20/analytics/wsdl/DeleteRules"},
    {etanGetRules, "http://www.onvif.org/ver20/analytics/wsdl/GetRules"},
    {etanModifyRules, "http://www.onvif.org/ver20/analytics/wsdl/ModifyRules"},
    {etanCreateAnalyticsModules, "http://www.onvif.org/ver20/analytics/wsdl/CreateAnalyticsModules"},
    {etanDeleteAnalyticsModules, "http://www.onvif.org/ver20/analytics/wsdl/DeleteAnalyticsModules"},
    {etanGetAnalyticsModules, "http://www.onvif.org/ver20/analytics/wsdl/GetAnalyticsModules"},
    {etanModifyAnalyticsModules, "http://www.onvif.org/ver20/analytics/wsdl/ModifyAnalyticsModules"},
    {etanGetSupportedAnalyticsModules, "http://www.onvif.org/ver20/analytics/wsdl/GetSupportedAnalyticsModules"},
    {etanGetRuleOptions, "http://www.onvif.org/ver20/analytics/wsdl/GetRuleOptions"},
    {etanGetAnalyticsModuleOptions, "http://www.onvif.org/ver20/analytics/wsdl/GetAnalyticsModuleOptions"},
    {etanGetSupportedMetadata, "http://www.onvif.org/ver20/analytics/wsdl/GetSupportedMetadata"},

#ifdef DEVICEIO_SUPPORT
    {etmdGetServiceCapabilities, "http://www.onvif.org/ver10/deviceio/wsdl/GetServiceCapabilities"},
    {etmdGetRelayOutputs, "http://www.onvif.org/ver10/deviceio/wsdl/GetRelayOutputs"},
    {etmdGetRelayOutputOptions, "http://www.onvif.org/ver10/deviceio/wsdl/GetRelayOutputOptions"},
    {etmdSetRelayOutputSettings, "http://www.onvif.org/ver10/deviceio/wsdl/SetRelayOutputSettings"},
    {etmdSetRelayOutputState, "http://www.onvif.org/ver10/deviceio/wsdl/SetRelayOutputState"},
    {etmdGetDigitalInputs, "http://www.onvif.org/ver10/deviceio/wsdl/GetDigitalInputs"},
    {etmdGetDigitalInputConfigurationOptions, "http://www.onvif.org/ver10/deviceio/wsdl/GetDigitalInputConfigurationOptions"},
    {etmdSetDigitalInputConfigurations, "http://www.onvif.org/ver10/deviceio/wsdl/SetDigitalInputConfigurations"},
    {etmdGetSerialPorts, "http://www.onvif.org/ver10/deviceio/wsdl/GetSerialPorts"},
    {etmdGetSerialPortConfiguration, "http://www.onvif.org/ver10/deviceio/wsdl/GetSerialPortConfiguration"},
    {etmdSetSerialPortConfiguration, "http://www.onvif.org/ver10/deviceio/wsdl/SetSerialPortConfiguration"},
    {etmdGetSerialPortConfigurationOptions, "http://www.onvif.org/ver10/deviceio/wsdl/GetSerialPortConfigurationOptions"},
    {etmdSendReceiveSerialCommand, "http://www.onvif.org/ver10/deviceio/wsdl/SendReceiveSerialCommand"},
#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT
    // recording service interfaces
    {etrcGetServiceCapabilities, "http://www.onvif.org/ver10/recording/wsdl/GetServiceCapabilities"},
    {etrcCreateRecording, "http://www.onvif.org/ver10/recording/wsdl/CreateRecording"},
    {etrcDeleteRecording, "http://www.onvif.org/ver10/recording/wsdl/DeleteRecording"},
    {etrcGetRecordings, "http://www.onvif.org/ver10/recording/wsdl/GetRecordings"},
    {etrcSetRecordingConfiguration, "http://www.onvif.org/ver10/recording/wsdl/SetRecordingConfiguration"},
    {etrcGetRecordingConfiguration, "http://www.onvif.org/ver10/recording/wsdl/GetRecordingConfiguration"},
    {etrcGetRecordingOptions, "http://www.onvif.org/ver10/recording/wsdl/GetRecordingOptions"},
    {etrcCreateTrack, "http://www.onvif.org/ver10/recording/wsdl/CreateTrack"},
    {etrcDeleteTrack, "http://www.onvif.org/ver10/recording/wsdl/DeleteTrack"},
    {etrcGetTrackConfiguration, "http://www.onvif.org/ver10/recording/wsdl/GetTrackConfiguration"},
    {etrcSetTrackConfiguration, "http://www.onvif.org/ver10/recording/wsdl/SetTrackConfiguration"},
    {etrcCreateRecordingJob, "http://www.onvif.org/ver10/recording/wsdl/CreateRecordingJob"},
    {etrcDeleteRecordingJob, "http://www.onvif.org/ver10/recording/wsdl/DeleteRecordingJob"},
    {etrcGetRecordingJobs, "http://www.onvif.org/ver10/recording/wsdl/GetRecordingJobs"},
    {etrcSetRecordingJobConfiguration, "http://www.onvif.org/ver10/recording/wsdl/SetRecordingJobConfiguration"},
    {etrcGetRecordingJobConfiguration, "http://www.onvif.org/ver10/recording/wsdl/GetRecordingJobConfiguration"},
    {etrcSetRecordingJobMode, "http://www.onvif.org/ver10/recording/wsdl/SetRecordingJobMode"},
    {etrcGetRecordingJobState, "http://www.onvif.org/ver10/recording/wsdl/GetRecordingJobState"},
    {etrcExportRecordedData, "http://www.onvif.org/ver10/recording/wsdl/ExportRecordedData"},
    {etrcStopExportRecordedData, "http://www.onvif.org/ver10/recording/wsdl/StopExportRecordedData"},
    {etrcGetExportRecordedDataState, "http://www.onvif.org/ver10/recording/wsdl/GetExportRecordedDataState"},

    {etrpGetServiceCapabilities, "http://www.onvif.org/ver10/replay/wsdl/GetServiceCapabilities"},
    {etrpGetReplayUri, "http://www.onvif.org/ver10/replay/wsdl/GetReplayUri"},
    {etrpGetReplayConfiguration, "http://www.onvif.org/ver10/replay/wsdl/GetReplayConfiguration"},
    {etrpSetReplayConfiguration, "http://www.onvif.org/ver10/replay/wsdl/SetReplayConfiguration"},
    
    {etseGetServiceCapabilities, "http://www.onvif.org/ver10/search/wsdl/GetServiceCapabilities"},
    {etseGetRecordingSummary, "http://www.onvif.org/ver10/search/wsdl/GetRecordingSummary"},
    {etseGetRecordingInformation, "http://www.onvif.org/ver10/search/wsdl/GetRecordingInformation"},
    {etseGetMediaAttributes, "http://www.onvif.org/ver10/search/wsdl/GetMediaAttributes"},
    {etseFindRecordings, "http://www.onvif.org/ver10/search/wsdl/FindRecordings"},
    {etseGetRecordingSearchResults, "http://www.onvif.org/ver10/search/wsdl/GetRecordingSearchResults"},
    {etseFindEvents, "http://www.onvif.org/ver10/search/wsdl/FindEvents"},
    {etseGetEventSearchResults, "http://www.onvif.org/ver10/search/wsdl/GetEventSearchResults"},
    {etseFindMetadata, "http://www.onvif.org/ver10/search/wsdl/FindMetadata"},
    {etseGetMetadataSearchResults, "http://www.onvif.org/ver10/search/wsdl/GetMetadataSearchResults"},
    {etseFindPTZPosition, "http://www.onvif.org/ver10/search/wsdl/FindPTZPosition"},
    {etseGetPTZPositionSearchResults, "http://www.onvif.org/ver10/search/wsdl/GetPTZPositionSearchResults"},
    {etseGetSearchState, "http://www.onvif.org/ver10/search/wsdl/GetSearchState"},
    {etseEndSearch, "http://www.onvif.org/ver10/search/wsdl/EndSearch"},
#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT
    {etacGetServiceCapabilities, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetServiceCapabilities"},
    {etacGetAccessPointInfoList, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAccessPointInfoList"},
    {etacGetAccessPointInfo, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAccessPointInfo"},
    {etacGetAccessPointList, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAccessPointList"},
    {etacGetAccessPoints, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAccessPoints"},
    {etacCreateAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/CreateAccessPoint"},
    {etacSetAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/SetAccessPoint"},
    {etacModifyAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/ModifyAccessPoint"},
    {etacDeleteAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/DeleteAccessPoint"},
    {etacGetAreaInfoList, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAreaInfoList"},
    {etacGetAreaInfo, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAreaInfo"},
    {etacGetAreaList, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAreaList"},
    {etacGetAreas, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAreas"},
    {etacCreateArea, "http://www.onvif.org/ver10/accesscontrol/wsdl/CreateArea"},
    {etacSetArea, "http://www.onvif.org/ver10/accesscontrol/wsdl/SetArea"},
    {etacModifyArea, "http://www.onvif.org/ver10/accesscontrol/wsdl/ModifyArea"},
    {etacDeleteArea, "http://www.onvif.org/ver10/accesscontrol/wsdl/DeleteArea"},
    {etacGetAccessPointState, "http://www.onvif.org/ver10/accesscontrol/wsdl/GetAccessPointState"},
    {etacEnableAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/EnableAccessPoint"},
    {etacDisableAccessPoint, "http://www.onvif.org/ver10/accesscontrol/wsdl/DisableAccessPoint"},

    {etdcGetServiceCapabilities, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetServiceCapabilities"},
    {etdcGetDoorInfoList, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetDoorInfoList"},
    {etdcGetDoorInfo, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetDoorInfo"},
    {etdcGetDoorState, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetDoorState"},
    {etdcAccessDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/AccessDoor"},
    {etdcLockDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/LockDoor"},
    {etdcUnlockDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/UnlockDoor"},
    {etdcDoubleLockDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/DoubleLockDoor"},
    {etdcBlockDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/BlockDoor"},
    {etdcLockDownDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/LockDownDoor"},
    {etdcLockDownReleaseDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/LockDownReleaseDoor"},
    {etdcLockOpenDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/LockOpenDoor"},
    {etdcLockOpenReleaseDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/LockOpenReleaseDoor"},
    {etdcGetDoors, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetDoors"},
    {etdcGetDoorList, "http://www.onvif.org/ver10/doorcontrol/wsdl/GetDoorList"},
    {etdcCreateDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/CreateDoor"},
    {etdcSetDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/SetDoor"},
    {etdcModifyDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/ModifyDoor"},
    {etdcDeleteDoor, "http://www.onvif.org/ver10/doorcontrol/wsdl/DeleteDoor"},
#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT
    {etthGetServiceCapabilities, "http://www.onvif.org/ver10/thermal/wsdl/GetServiceCapabilities"},
    {etthGetConfigurations, "http://www.onvif.org/ver10/thermal/wsdl/GetConfigurations"},
    {etthGetConfiguration, "http://www.onvif.org/ver10/thermal/wsdl/GetConfiguration"},
    {etthSetConfiguration, "http://www.onvif.org/ver10/thermal/wsdl/SetConfiguration"},
    {etthGetConfigurationOptions, "http://www.onvif.org/ver10/thermal/wsdl/GetConfigurationOptions"},
    {etthGetRadiometryConfiguration, "http://www.onvif.org/ver10/thermal/wsdl/GetRadiometryConfiguration"},
    {etthSetRadiometryConfiguration, "http://www.onvif.org/ver10/thermal/wsdl/SetRadiometryConfiguration"},
    {etthGetRadiometryConfigurationOptions, "http://www.onvif.org/ver10/thermal/wsdl/GetRadiometryConfigurationOptions"},
#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT
    {etcrGetServiceCapabilities, "http://www.onvif.org/ver10/credential/wsdl/GetServiceCapabilities"},
    {etcrGetCredentialInfo, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialInfo"},
    {etcrGetCredentialInfoList, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialInfoList"},
    {etcrGetCredentials, "http://www.onvif.org/ver10/credential/wsdl/GetCredentials"},
    {etcrGetCredentialList, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialList"},
    {etcrCreateCredential, "http://www.onvif.org/ver10/credential/wsdl/CreateCredential"},
    {etcrModifyCredential, "http://www.onvif.org/ver10/credential/wsdl/ModifyCredential"},
    {etcrDeleteCredential, "http://www.onvif.org/ver10/credential/wsdl/DeleteCredential"},
    {etcrGetCredentialState, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialState"},
    {etcrEnableCredential, "http://www.onvif.org/ver10/credential/wsdl/EnableCredential"},
    {etcrDisableCredential, "http://www.onvif.org/ver10/credential/wsdl/DisableCredential"},
    {etcrSetCredential, "http://www.onvif.org/ver10/credential/wsdl/SetCredential"},
    {etcrResetAntipassbackViolation, "http://www.onvif.org/ver10/credential/wsdl/ResetAntipassbackViolation"},
    {etcrGetSupportedFormatTypes, "http://www.onvif.org/ver10/credential/wsdl/GetSupportedFormatTypes"},
    {etcrGetCredentialIdentifiers, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialIdentifiers"},
    {etcrSetCredentialIdentifier, "http://www.onvif.org/ver10/credential/wsdl/SetCredentialIdentifier"},
    {etcrDeleteCredentialIdentifier, "http://www.onvif.org/ver10/credential/wsdl/DeleteCredentialIdentifier"},
    {etcrGetCredentialAccessProfiles, "http://www.onvif.org/ver10/credential/wsdl/GetCredentialAccessProfiles"},
    {etcrSetCredentialAccessProfiles, "http://www.onvif.org/ver10/credential/wsdl/SetCredentialAccessProfiles"},
    {etcrDeleteCredentialAccessProfiles, "http://www.onvif.org/ver10/credential/wsdl/DeleteCredentialAccessProfiles"},
#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES
    {etarGetServiceCapabilities, "http://www.onvif.org/ver10/accessrules/wsdl/GetServiceCapabilities"},
    {etarGetAccessProfileInfo, "http://www.onvif.org/ver10/accessrules/wsdl/GetAccessProfileInfo"},
    {etarGetAccessProfileInfoList, "http://www.onvif.org/ver10/accessrules/wsdl/GetAccessProfileInfoList"},
    {etarGetAccessProfiles, "http://www.onvif.org/ver10/accessrules/wsdl/GetAccessProfiles"},
    {etarGetAccessProfileList, "http://www.onvif.org/ver10/accessrules/wsdl/GetAccessProfileList"},
    {etarCreateAccessProfile, "http://www.onvif.org/ver10/accessrules/wsdl/CreateAccessProfile"},
    {etarModifyAccessProfile, "http://www.onvif.org/ver10/accessrules/wsdl/ModifyAccessProfile"},
    {etarDeleteAccessProfile, "http://www.onvif.org/ver10/accessrules/wsdl/DeleteAccessProfile"},
#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT
    {etscGetServiceCapabilities, "http://www.onvif.org/ver10/schedule/wsdl/GetServiceCapabilities"},
    {etscGetScheduleInfo, "http://www.onvif.org/ver10/schedule/wsdl/GetScheduleInfo"},
    {etscGetScheduleInfoList, "http://www.onvif.org/ver10/schedule/wsdl/GetScheduleInfoList"},
    {etscGetSchedules, "http://www.onvif.org/ver10/schedule/wsdl/GetSchedules"},
    {etscGetScheduleList, "http://www.onvif.org/ver10/schedule/wsdl/GetScheduleList"},
    {etscCreateSchedule, "http://www.onvif.org/ver10/schedule/wsdl/CreateSchedule"},
    {etscModifySchedule, "http://www.onvif.org/ver10/schedule/wsdl/ModifySchedule"},
    {etscDeleteSchedule, "http://www.onvif.org/ver10/schedule/wsdl/DeleteSchedule"},
    {etscGetSpecialDayGroupInfo, "http://www.onvif.org/ver10/schedule/wsdl/GetSpecialDayGroupInfo"},
    {etscGetSpecialDayGroupInfoList, "http://www.onvif.org/ver10/schedule/wsdl/GetSpecialDayGroupInfoList"},
    {etscGetSpecialDayGroups, "http://www.onvif.org/ver10/schedule/wsdl/GetSpecialDayGroups"},
    {etscGetSpecialDayGroupList, "http://www.onvif.org/ver10/schedule/wsdl/GetSpecialDayGroupList"},
    {etscCreateSpecialDayGroup, "http://www.onvif.org/ver10/schedule/wsdl/CreateSpecialDayGroup"},
    {etscModifySpecialDayGroup, "http://www.onvif.org/ver10/schedule/wsdl/ModifySpecialDayGroup"},
    {etscDeleteSpecialDayGroup, "http://www.onvif.org/ver10/schedule/wsdl/DeleteSpecialDayGroup"},
    {etscGetScheduleState, "http://www.onvif.org/ver10/schedule/wsdl/GetScheduleState"},
#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT
    {etrvGetServiceCapabilities, "http://www.onvif.org/ver10/receiver/wsdl/GetServiceCapabilities"},
    {etrvGetReceivers, "http://www.onvif.org/ver10/receiver/wsdl/GetReceivers"},
    {etrvGetReceiver, "http://www.onvif.org/ver10/receiver/wsdl/GetReceiver"},
    {etrvCreateReceiver, "http://www.onvif.org/ver10/receiver/wsdl/CreateReceiver"},
    {etrvDeleteReceiver, "http://www.onvif.org/ver10/receiver/wsdl/DeleteReceiver"},
    {etrvConfigureReceiver, "http://www.onvif.org/ver10/receiver/wsdl/ConfigureReceiver"},
    {etrvSetReceiverMode, "http://www.onvif.org/ver10/receiver/wsdl/SetReceiverMode"},
    {etrvGetReceiverState, "http://www.onvif.org/ver10/receiver/wsdl/GetReceiverState"},
#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT
    {etpvGetServiceCapabilities, "http://www.onvif.org/ver10/provisioning/wsdl/GetServiceCapabilities"},
    {etpvPanMove, "http://www.onvif.org/ver10/provisioning/wsdl/PanMove"},
    {etpvTiltMove, "http://www.onvif.org/ver10/provisioning/wsdl/TiltMove"},
    {etpvZoomMove, "http://www.onvif.org/ver10/provisioning/wsdl/ZoomMove"},
    {etpvRollMove, "http://www.onvif.org/ver10/provisioning/wsdl/RollMove"},
    {etpvFocusMove, "http://www.onvif.org/ver10/provisioning/wsdl/FocusMove"},
    {etpvStop, "http://www.onvif.org/ver10/provisioning/wsdl/Stop"},
    {etpvGetUsage, "http://www.onvif.org/ver10/provisioning/wsdl/GetUsage"},
#endif // end of PROVISIONING_SUPPORT    
};


/***************************************************************************************/
HT_API OVFACTS * onvif_find_action_by_type(eOnvifAction type)
{
    uint32 i;
    
    if (type < eActionNull || type >= eActionMax)
    {
        return NULL;
    }
    
    for (i=0; i<(sizeof(g_onvif_acts)/sizeof(OVFACTS)); i++)
    {
        if (g_onvif_acts[i].type == type)
        {
            return &g_onvif_acts[i];
        }    
    }

    return NULL;
}



