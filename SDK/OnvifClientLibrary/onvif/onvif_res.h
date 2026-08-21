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

#ifndef ONVIF_RES_H
#define ONVIF_RES_H



/*************************************************************************
 *
 * onvif response structure
 *
**************************************************************************/
typedef struct
{
    onvif_Capabilities  Capabilities;                   // required, Capability information
} tds_GetCapabilities_RES;

typedef struct
{
    onvif_Capabilities  Capabilities;                   // required, Capability information
} tds_GetServices_RES;

typedef struct
{
    onvif_DevicesCapabilities Capabilities;             // required, he capabilities for the device service is returned in the Capabilities element
} tds_GetServiceCapabilities_RES;

typedef struct
{
    onvif_DeviceInformation DeviceInformation;          // required, Device information
} tds_GetDeviceInformation_RES;

typedef struct
{
    UserList * User;
} tds_GetUsers_RES;

typedef struct
{
    int     dummy;
} tds_CreateUsers_RES;

typedef struct
{
    int     dummy;
} tds_DeleteUsers_RES;

typedef struct
{
    int     dummy;
} tds_SetUser_RES;

typedef struct
{
    uint32  RemoteUserFlag  : 1;                        // Indicates whether the field RemoteUser is valid
    uint32  Reserved        : 31;
    
    onvif_RemoteUser RemoteUser;                        // optional
} tds_GetRemoteUser_RES;

typedef struct
{
    int     dummy;
} tds_SetRemoteUser_RES;

typedef struct
{
    NetworkInterfaceList * NetworkInterfaces;           // required, List of network interfaces
} tds_GetNetworkInterfaces_RES;

typedef struct
{
    BOOL    RebootNeeded;                               // required, Indicates whether or not a reboot is required after configuration updates, 
} tds_SetNetworkInterfaces_RES;

typedef struct
{
    onvif_NTPInformation    NTPInformation;             // required, NTP information
} tds_GetNTP_RES;

typedef struct 
{
    int     dummy;
} tds_SetNTP_RES;

typedef struct
{
    onvif_HostnameInformation   HostnameInformation;    // required, Host name information
} tds_GetHostname_RES;

typedef struct
{
    int     dummy;
} tds_SetHostname_RES;

typedef struct
{
    BOOL    RebootNeeded;                               // required, Indicates whether or not a reboot is required after configuration updates
} tds_SetHostnameFromDHCP_RES;

typedef struct
{
    onvif_DNSInformation    DNSInformation;             // required, DNS information
} tds_GetDNS_RES;

typedef struct 
{
    int     dummy;
} tds_SetDNS_RES;

typedef struct
{
    onvif_DynamicDNSInformation DynamicDNSInformation;  // Dynamic DNS information
} tds_GetDynamicDNS_RES;

typedef struct
{
    int     dummy;
} tds_SetDynamicDNS_RES;

typedef struct
{
    onvif_NetworkProtocol   NetworkProtocols;           // Contains defined protocols supported by the device
} tds_GetNetworkProtocols_RES;

typedef struct
{
    int     dummy;
} tds_SetNetworkProtocols_RES;

typedef struct
{
    onvif_DiscoveryMode DiscoveryMode;                  // required, Indicator of discovery mode: Discoverable, NonDiscoverable
} tds_GetDiscoveryMode_RES;

typedef struct
{
    int     dummy;
} tds_SetDiscoveryMode_RES;

typedef struct
{
    onvif_NetworkGateway NetworkGateway;                // required, Gets the default IPv4 and IPv6 gateway settings from the device
} tds_GetNetworkDefaultGateway_RES;

typedef struct
{
    int     dummy;
} tds_SetNetworkDefaultGateway_RES;

typedef struct
{
    onvif_NetworkZeroConfiguration ZeroConfiguration;   // Contains the zero-configuration
} tds_GetZeroConfiguration_RES;

typedef struct
{
    int     dummy;
} tds_SetZeroConfiguration_RES;

typedef struct
{
    char    GUID[100];
} tds_GetEndpointReference_RES;

typedef struct
{
    uint32  AuxiliaryCommandResponseFlag    : 1;        // Indicates whether the field AuxiliaryCommandResponse is valid
    uint32  Reserved                        : 31;
    
    char    AuxiliaryCommandResponse[1024];             // optional
} tds_SendAuxiliaryCommand_RES;

typedef struct
{
    RelayOutputList * RelayOutputs;
} tds_GetRelayOutputs_RES;

typedef struct
{
    int     dummy;
} tds_SetRelayOutputSettings_RES;

typedef struct
{
    int     dummy;
} tds_SetRelayOutputState_RES;

typedef struct
{
    uint32  UTCDateTimeFlag     : 1;                    // Indicates whether the field UTCDateTime is valid
    uint32  LocalDateTimeFlag   : 1;                    // Indicates whether the field LocalDateTime is valid
    uint32  Reserved            : 30;
    
    onvif_SystemDateTime    SystemDateTime;             // required,     
    onvif_DateTime          UTCDateTime;                // optional, Date and time in UTC. If time is obtained via NTP, UTCDateTime has no meaning
    onvif_DateTime          LocalDateTime;              // optional, 
} tds_GetSystemDateAndTime_RES;

typedef struct
{
    int     dummy;
} tds_SetSystemDateAndTime_RES;

typedef struct
{
    int     dummy;
} tds_SystemReboot_RES;

typedef struct
{
    int     dummy;
} tds_SetSystemFactoryDefault_RES;

typedef struct
{
    char  * String;                                     // optional, Contains the system log information
                                                        // The caller should call FreeBuff to free the memory
} tds_GetSystemLog_RES;

typedef struct
{
    uint32  sizeScopes;

    onvif_Scope Scopes[MAX_SCOPE_NUMS];
} tds_GetScopes_RES;

typedef struct
{
    int     dummy;
} tds_SetScopes_RES;

typedef struct
{
    int     dummy;
} tds_AddScopes_RES;

typedef struct
{
    int     dummy;
} tds_RemoveScopes_RES;

typedef struct
{
    char    UploadUri[256];                             // required 
    int     UploadDelay;                                // required 
    int     ExpectedDownTime;                           // required 
} tds_StartFirmwareUpgrade_RES;

typedef struct
{
    uint32  SystemLogUriFlag    : 1;                    // Indicates whether the field SystemLogUri is valid
    uint32  AccessLogUriFlag    : 1;                    // Indicates whether the field AccessLogUri is valid
    uint32  SupportInfoUriFlag  : 1;                    // Indicates whether the field SupportInfoUri is valid
    uint32  SystemBackupUriFlag : 1;                    // Indicates whether the field SystemBackupUri is valid
    uint32  Reserved            : 28;
    
    char    SystemLogUri[256];                          // optional
    char    AccessLogUri[256];                          // optional
    char    SupportInfoUri[256];                        // optional
    char    SystemBackupUri[256];                       // optional
} tds_GetSystemUris_RES;

typedef struct
{
    char    UploadUri[256];                             // required
    int     ExpectedDownTime;                           // required
} tds_StartSystemRestore_RES;

typedef struct
{
    char    WsdlUrl[256];                               // required
} tds_GetWsdlUrl_RES;

typedef struct
{
    onvif_Dot11Capabilities Capabilities;               // required
} tds_GetDot11Capabilities_RES;

typedef struct
{
    onvif_Dot11Status   Status;                         // required
} tds_GetDot11Status_RES;

typedef struct
{
    Dot11AvailableNetworksList * Networks;              // optional
} tds_ScanAvailableDot11Networks_RES;

typedef struct
{
    LocationEntityList * Location;                      // optional
} tds_GetGeoLocation_RES;

typedef struct
{
    int     dummy;
} tds_SetGeoLocation_RES;

typedef struct
{
    int     dummy;
} tds_DeleteGeoLocation_RES;

typedef struct
{
    int     dummy;
} tds_SetHashingAlgorithm_RES;

typedef struct
{
    onvif_IPAddressFilter   IPAddressFilter;            // required
} tds_GetIPAddressFilter_RES;

typedef struct 
{
    int     dummy;
} tds_SetIPAddressFilter_RES;

typedef struct
{
    int     dummy;
} tds_AddIPAddressFilter_RES;

typedef struct
{
    int     dummy;
} tds_RemoveIPAddressFilter_RES;

typedef struct
{
    onvif_BinaryData    PolicyFile;                     // required, need call free function to free PolicyFile.Data.ptr buffer
} tds_GetAccessPolicy_RES;

typedef struct
{
    int     dummy;
} tds_SetAccessPolicy_RES;

typedef struct
{
    StorageConfigurationList * StorageConfigurations;   // optional
} tds_GetStorageConfigurations_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required
} tds_CreateStorageConfiguration_RES;

typedef struct
{
    onvif_StorageConfiguration  StorageConfiguration;   // required
} tds_GetStorageConfiguration_RES;

typedef struct
{
    int     dummy;
} tds_SetStorageConfiguration_RES;

typedef struct
{
    int     dummy;
} tds_DeleteStorageConfiguration_RES;

typedef struct
{
    onvif_MediaCapabilities Capabilities;               // required, the capabilities for the media service is returned
} trt_GetServiceCapabilities_RES;

typedef struct
{
    VideoSourceList * VideoSources;                     // List of existing Video Sources
} trt_GetVideoSources_RES;

typedef struct
{
    AudioSourceList * AudioSources;                     // List of existing Audio Sources
} trt_GetAudioSources_RES;

typedef struct
{
    ONVIF_PROFILE Profile;                              // required, returns the new created profile                        
} trt_CreateProfile_RES;

typedef struct
{
    ONVIF_PROFILE Profile;                              // required, returns the requested media profile
} trt_GetProfile_RES;

typedef struct
{
    ONVIF_PROFILE * Profiles;                           // lists all profiles that exist in the media service
} trt_GetProfiles_RES;

typedef struct
{
    int     dummy;
} trt_AddVideoEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_AddVideoSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_AddAudioEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_AddAudioSourceConfiguration_RES;

typedef struct
{
    VideoSourceModeList * VideoSourceModes;             // Return the information for specified video source mode            
} trt_GetVideoSourceModes_RES;

typedef struct
{
    BOOL    Reboot;                                     // The response contains information about rebooting after returning response. When Reboot is set true, a device will reboot automatically after setting mode
} trt_SetVideoSourceMode_RES;

typedef struct
{
    int     dummy;
} trt_AddPTZConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveVideoEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveVideoSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveAudioEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveAudioSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemovePTZConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_DeleteProfile_RES;

typedef struct
{
    VideoSourceConfigurationList * Configurations;      // This element contains a list of video source configurations
} trt_GetVideoSourceConfigurations_RES;

typedef struct
{
    VideoEncoderConfigurationList * Configurations;     // This element contains a list of video encoder configurations
} trt_GetVideoEncoderConfigurations_RES;

typedef struct
{
    AudioSourceConfigurationList * Configurations;      // This element contains a list of audio source configurations
} trt_GetAudioSourceConfigurations_RES;

typedef struct
{
    AudioEncoderConfigurationList * Configurations;     // This element contains a list of audio encoder configurations
} trt_GetAudioEncoderConfigurations_RES;

typedef struct
{
    onvif_VideoSourceConfiguration  Configuration;      // required, The requested video source configuration
} trt_GetVideoSourceConfiguration_RES;

typedef struct
{
    onvif_VideoEncoderConfiguration Configuration;      // required, The requested video encoder configuration
} trt_GetVideoEncoderConfiguration_RES;

typedef struct
{
    onvif_AudioSourceConfiguration  Configuration;      // required, The requested audio source configuration
} trt_GetAudioSourceConfiguration_RES;

typedef struct
{
    onvif_AudioEncoderConfiguration Configuration;      // required, The requested audio encorder configuration
} trt_GetAudioEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetVideoSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetVideoEncoderConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetAudioSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetAudioEncoderConfiguration_RES;

typedef struct
{
    onvif_VideoSourceConfigurationOptions   Options;
} trt_GetVideoSourceConfigurationOptions_RES;

typedef struct
{
    onvif_VideoEncoderConfigurationOptions  Options;    // required
} trt_GetVideoEncoderConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} trt_GetAudioSourceConfigurationOptions_RES;


typedef struct
{
    onvif_AudioEncoderConfigurationOptions  Options;    // required
} trt_GetAudioEncoderConfigurationOptions_RES;

typedef struct
{
    char    Uri[ONVIF_URI_LEN];                         // required, Stable Uri to be used for requesting the media stream

    int     Timeout;                                    // required, Duration how long the Uri is valid. This parameter shall be set to PT0S to indicate that this stream URI is indefinitely valid even if the profile changes
    
    BOOL    InvalidAfterConnect;                        // required, Indicates if the Uri is only valid until the connection is established. The value shall be set to "false"
    BOOL    InvalidAfterReboot;                         // required, Indicates if the Uri is invalid after a reboot of the device. The value shall be set to "false"
} trt_GetStreamUri_RES;

typedef struct
{
    int     dummy;
} trt_SetSynchronizationPoint_RES;

typedef struct
{
    char    Uri[ONVIF_URI_LEN];                         // required, Stable Uri to be used for requesting the media stream

    int     Timeout;                                    // required, Duration how long the Uri is valid. This parameter shall be set to PT0S to indicate that this stream URI is indefinitely valid even if the profile changes
    
    BOOL    InvalidAfterConnect;                        // required, Indicates if the Uri is only valid until the connection is established. The value shall be set to "false"
    BOOL    InvalidAfterReboot;                         // required, Indicates if the Uri is invalid after a reboot of the device. The value shall be set to "false"
} trt_GetSnapshotUri_RES;

typedef struct 
{
    uint32  JPEGFlag    : 1;                            // Indicates whether the field JPEG is valid
    uint32  H264Flag    : 1;                            // Indicates whether the field H264 is valid
    uint32  MPEG4Flag   : 1;                            // Indicates whether the field SupportInfoUri is valid
    uint32  Reserved    : 29;
    
    int     TotalNumber;                                // required, The minimum guaranteed total number of encoder instances (applications) per VideoSourceConfiguration. The device is able to deliver the TotalNumber of streams
    int     JPEG;                                       // optional, If a device limits the number of instances for respective Video Codecs the response contains the information how many Jpeg streams can be set up at the same time per VideoSource
    int     H264;                                       // optional, If a device limits the number of instances for respective Video Codecs the response contains the information how many H264 streams can be set up at the same time per VideoSource
    int     MPEG4;                                      // optional, If a device limits the number of instances for respective Video Codecs the response contains the information how many Mpeg4 streams can be set up at the same time per VideoSource
} trt_GetGuaranteedNumberOfVideoEncoderInstances_RES;

typedef struct 
{
    AudioOutputList * AudioOutputs;                     // optional, List of existing Audio Outputs
} trt_GetAudioOutputs_RES;

typedef struct 
{
    AudioOutputConfigurationList * Configurations;      // optional, This element contains a list of audio output configurations
} trt_GetAudioOutputConfigurations_RES;

typedef struct 
{
    onvif_AudioOutputConfiguration  Configuration;      // The requested audio output configuration
} trt_GetAudioOutputConfiguration_RES;

typedef struct 
{
    onvif_AudioOutputConfigurationOptions   Options;    // required, This message contains the audio output configuration options. 
                                                        //  If a audio output configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} trt_GetAudioOutputConfigurationOptions_RES;

typedef struct 
{
    int     dummy;
} trt_SetAudioOutputConfiguration_RES;

typedef struct 
{
    AudioDecoderConfigurationList * Configurations;     // optional, This element contains a list of audio decoder configurations
} trt_GetAudioDecoderConfigurations_RES;

typedef struct 
{
    onvif_AudioDecoderConfiguration Configuration;      // The requested audio decoder configuration
} trt_GetAudioDecoderConfiguration_RES;

typedef struct 
{
    onvif_AudioDecoderConfigurationOptions  Options;    // required, This message contains the audio decoder configuration options. 
                                                        //  If a audio decoder configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} trt_GetAudioDecoderConfigurationOptions_RES;

typedef struct 
{
    int     dummy;
} trt_SetAudioDecoderConfiguration_RES;

typedef struct 
{
    int     dummy;
} trt_AddAudioOutputConfiguration_RES;

typedef struct 
{
    int     dummy;
} trt_AddAudioDecoderConfiguration_RES;

typedef struct 
{
    int     dummy;
} trt_RemoveAudioOutputConfiguration_RES;

typedef struct 
{
    int     dummy;
} trt_RemoveAudioDecoderConfiguration_RES;

typedef struct
{
    OSDConfigurationList * OSDs;                        // This element contains a list of requested OSDs
} trt_GetOSDs_RES;

typedef struct
{
    onvif_OSDConfiguration  OSD;                        // required, The requested OSD configuration
} trt_GetOSD_RES;

typedef struct
{
    int     dummy;
} trt_SetOSD_RES;

typedef struct
{
    onvif_OSDConfigurationOptions   OSDOptions;         // required, 
} trt_GetOSDOptions_RES;

typedef struct
{
    char    OSDToken[ONVIF_TOKEN_LEN];                  // required, Returns Token of the newly created OSD
} trt_CreateOSD_RES;

typedef struct
{
    int     dummy;
} trt_DeleteOSD_RES;

typedef struct
{
    VideoAnalyticsConfigurationList * Configurations;   // list of video analytics configurations
} trt_GetVideoAnalyticsConfigurations_RES;

typedef struct
{
    int     dummy;
} trt_AddVideoAnalyticsConfiguration_RES;

typedef struct
{
    onvif_VideoAnalyticsConfiguration   Configuration;  // The requested video analytics configuration
} trt_GetVideoAnalyticsConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveVideoAnalyticsConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetVideoAnalyticsConfiguration_RES;

typedef struct
{
    MetadataConfigurationList * Configurations;         // list of metadata configurations
} trt_GetMetadataConfigurations_RES;

typedef struct
{
    int     dummy;
} trt_AddMetadataConfiguration_RES;

typedef struct
{
    onvif_MetadataConfiguration Configuration;          // The requested metadata configuration
} trt_GetMetadataConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_RemoveMetadataConfiguration_RES;

typedef struct
{
    int     dummy;
} trt_SetMetadataConfiguration_RES;

typedef struct
{
    onvif_MetadataConfigurationOptions Options;         // If a metadata configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} trt_GetMetadataConfigurationOptions_RES;

typedef struct
{
    VideoEncoderConfigurationList * Configurations;     // list of video encoder configurations
} trt_GetCompatibleVideoEncoderConfigurations_RES;

typedef struct
{
    AudioEncoderConfigurationList * Configurations;     // list of audio encoder configurations
} trt_GetCompatibleAudioEncoderConfigurations_RES;

typedef struct
{
    VideoAnalyticsConfigurationList * Configurations;   // list of video analytics configurations
} trt_GetCompatibleVideoAnalyticsConfigurations_RES;

typedef struct
{
    MetadataConfigurationList * Configurations;         // list of metadata configurations 
} trt_GetCompatibleMetadataConfigurations_RES;

typedef struct
{
    onvif_PTZCapabilities Capabilities;                 // required, the capabilities for the PTZ service is returned
} ptz_GetServiceCapabilities_RES;

typedef struct
{
    PTZNodeList * PTZNode;                              // A list of the existing PTZ Nodes on the device
} ptz_GetNodes_RES;

typedef struct
{
    onvif_PTZNode   PTZNode;                            // required, A requested PTZNode
} ptz_GetNode_RES;

typedef struct
{
    PTZPresetList * PTZPresets;                         // A list of presets which are available for the requested MediaProfile
} ptz_GetPresets_RES;

typedef struct
{
    char    PresetToken[ONVIF_TOKEN_LEN];               // required, A token to the Preset which has been set
} ptz_SetPreset_RES;

typedef struct
{
    int     dummy;
} ptz_RemovePreset_RES;

typedef struct
{
    int     dummy;
} ptz_GotoPreset_RES;

typedef struct
{
    int     dummy;
} ptz_GotoHomePosition_RES;

typedef struct
{
    int     dummy;
} ptz_SetHomePosition_RES;

typedef struct
{
    onvif_PTZStatus PTZStatus;                          // required, The PTZStatus for the requested MediaProfile
} ptz_GetStatus_RES;

typedef struct
{
    int     dummy;
} ptz_ContinuousMove_RES;

typedef struct
{
    int     dummy;
} ptz_RelativeMove_RES;

typedef struct
{
    int     dummy;
} ptz_AbsoluteMove_RES;

typedef struct
{
    int     dummy;
} ptz_Stop_RES;

typedef struct
{
    PTZConfigurationList * PTZConfiguration;            // A list of all existing PTZConfigurations on the device
} ptz_GetConfigurations_RES;

typedef struct
{
    onvif_PTZConfiguration  PTZConfiguration;           // required, A requested PTZConfiguration
} ptz_GetConfiguration_RES;

typedef struct
{
    int     dummy;
} ptz_SetConfiguration_RES;

typedef struct
{
    onvif_PTZConfigurationOptions   PTZConfigurationOptions;    // required, The requested PTZ configuration options
} ptz_GetConfigurationOptions_RES;

typedef struct
{
    PresetTourList *  PresetTour;
} ptz_GetPresetTours_RES;

typedef struct
{
    onvif_PresetTour    PresetTour;
} ptz_GetPresetTour_RES;

typedef struct
{
    onvif_PTZPresetTourOptions  Options;
} ptz_GetPresetTourOptions_RES;

typedef struct
{
    char    PresetTourToken[ONVIF_TOKEN_LEN];           // required, 
} ptz_CreatePresetTour_RES;

typedef struct
{
    int     dummy;
} ptz_ModifyPresetTour_RES;

typedef struct
{
    int     dummy;
} ptz_OperatePresetTour_RES;

typedef struct
{
    int     dummy;
} ptz_RemovePresetTour_RES;

typedef struct
{
    char    AuxiliaryResponse[1024];                    // required, The response contains the auxiliary response
} ptz_SendAuxiliaryCommand_RES;

typedef struct
{
    int     dummy;
} ptz_GeoMove_RES;

typedef struct
{
    onvif_EventCapabilities Capabilities;               // required, The capabilities for the event service
} tev_GetServiceCapabilities_RES;

typedef struct
{
    uint32  sizeTopicNamespaceLocation;
    char    TopicNamespaceLocation[10][256];            // required element of type xsd:anyURI
    BOOL    FixedTopicSet;                              // required element of type xsd:anyURI

    char    * TopicSet;                                 // required, the caller should free the memory

    uint32  sizeTopicExpressionDialect;
    char    TopicExpressionDialect[10][256];            // required element of type xsd:anyURI
    uint32  sizeMessageContentFilterDialect;
    char    MessageContentFilterDialect[10][256];       // required element of type xsd:anyURI
    uint32  sizeProducerPropertiesFilterDialect;
    char    ProducerPropertiesFilterDialect[10][256];   // optional element of type xsd:anyURI
    uint32  sizeMessageContentSchemaLocation;
    char    MessageContentSchemaLocation[10][256];      // required element of type xsd:anyURI
} tev_GetEventProperties_RES;

typedef struct
{
    int     dummy;
} tev_Renew_RES;

typedef struct
{
    int     dummy;
} tev_Unsubscribe_RES;

typedef struct
{
    char    producter_addr[256];                        // required
    char    ReferenceParameters[512];                   // optional
} tev_Subscribe_RES;

typedef struct
{
    int     dummy;
} tev_PauseSubscription_RES;

typedef struct
{
    int     dummy;
} tev_ResumeSubscription_RES;

typedef struct
{
    char    producter_addr[256];                        // required, Endpoint reference of the subscription to be used for pulling the messages
    char    ReferenceParameters[512];                   // optional
    
    time_t  CurrentTime;                                // required, Current time of the server for synchronization purposes
    time_t  TerminationTime;                            // required, Date time when the PullPoint will be shut down without further pull requests
} tev_CreatePullPointSubscription_RES;

typedef struct
{
    int     dummy;
} tev_DestroyPullPoint_RES;

typedef struct
{
    time_t  CurrentTime;                                // required, The date and time when the messages have been delivered by the web server to the client
    time_t  TerminationTime;                            // required, Date time when the PullPoint will be shut down without further pull requests

    NotificationMessageList * NotifyMessages;
} tev_PullMessages_RES;

typedef struct
{
    NotificationMessageList * NotifyMessages;
} tev_GetMessages_RES;

typedef struct
{
    int     dummy;
} tev_Seek_RES;

typedef struct
{
    int     dummy;
} tev_SetSynchronizationPoint_RES;

// imaging service

typedef struct 
{
    onvif_ImagingCapabilities   Capabilities;           // required, The capabilities for the imaging service is returned
} img_GetServiceCapabilities_RES;

typedef struct
{
    onvif_ImagingSettings   ImagingSettings;            // required, ImagingSettings for the VideoSource that was requested
} img_GetImagingSettings_RES;

typedef struct
{
    int     dummy;
} img_SetImagingSettings_RES;

typedef struct
{
    onvif_ImagingOptions    ImagingOptions;             // required, Valid ranges for the imaging parameters that are categorized as device specific
} img_GetOptions_RES;

typedef struct
{
    int     dummy;
} img_Move_RES;

typedef struct
{
    int     dummy;
} img_Stop_RES;

typedef struct
{
    onvif_ImagingStatus Status;                         // required, Requested imaging status
} img_GetStatus_RES;

typedef struct
{
    onvif_MoveOptions20 MoveOptions;                    // required, Valid ranges for the focus lens move options
} img_GetMoveOptions_RES;

typedef struct
{
    ImagingPresetList * Preset;                         // required, List of Imaging Presets which are available for the requested VideoSource.
} img_GetPresets_RES;

typedef struct
{
    uint32  PresetFlag  : 1;                            // Indicates whether the field Preset is valid
    uint32  Reserved    : 31;
    
    onvif_ImagingPreset Preset;                         // optional, Current Imaging Preset in use for the specified Video Source
} img_GetCurrentPreset_RES;

typedef struct
{
    int     dummy;
} img_SetCurrentPreset_RES;

// device IO

typedef struct
{
    onvif_DeviceIOCapabilities Capabilities;            // required, the capabilities for the deviceIO service is returned
} tmd_GetServiceCapabilities_RES;

typedef struct
{
    RelayOutputList * RelayOutputs;
} tmd_GetRelayOutputs_RES;

typedef struct
{
    RelayOutputOptionsList * RelayOutputOptions;
} tmd_GetRelayOutputOptions_RES;

typedef struct
{
    int     dummy;   
} tmd_SetRelayOutputSettings_RES;

typedef struct
{
    int     dummy;
} tmd_SetRelayOutputState_RES;

typedef struct
{
    DigitalInputList * DigitalInputs;
} tmd_GetDigitalInputs_RES;

typedef struct
{
    onvif_DigitalInputConfigurationOptions  DigitalInputOptions;
} tmd_GetDigitalInputConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tmd_SetDigitalInputConfigurations_RES;

typedef struct
{
    SerialPortList * serial_port;
} tmd_GetSerialPorts_RES;

typedef struct
{
    onvif_SerialPortConfiguration   SerialPortConfiguration;    // required 
} tmd_GetSerialPortConfiguration_RES;

typedef struct
{
    int     dummy;
} tmd_SetSerialPortConfiguration_RES;

typedef struct
{
    onvif_SerialPortConfigurationOptions    SerialPortOptions;   // required 
} tmd_GetSerialPortConfigurationOptions_RES;

typedef struct
{
    uint32  SerialDataFlag  : 1;
    uint32  Reserved        : 31;
    
    onvif_SerialData    SerialData;                     // optional, The serial port data
} tmd_SendReceiveSerialCommand_RES;

// recording

typedef struct
{
    onvif_RecordingCapabilities Capabilities;           // required, The capabilities for the recording service
} trc_GetServiceCapabilities_RES;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required
} trc_CreateRecording_RES;

typedef struct
{
    int     dummy;
} trc_DeleteRecording_RES;

typedef struct
{
    RecordingList * Recordings;
} trc_GetRecordings_RES;

typedef struct
{
    int     dummy;
} trc_SetRecordingConfiguration_RES;

typedef struct
{
    onvif_RecordingConfiguration    RecordingConfiguration;
} trc_GetRecordingConfiguration_RES;

typedef struct
{
    onvif_RecordingOptions  Options;
} trc_GetRecordingOptions_RES;

typedef struct
{
    char    TrackToken[ONVIF_TOKEN_LEN];                // required
} trc_CreateTrack_RES;

typedef struct
{
    int     dummy;
} trc_DeleteTrack_RES;

typedef struct
{
    onvif_TrackConfiguration    TrackConfiguration;     // required
} trc_GetTrackConfiguration_RES;

typedef struct
{
    int     dummy;
} trc_SetTrackConfiguration_RES;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required
    
    onvif_RecordingJobConfiguration JobConfiguration;   // required
} trc_CreateRecordingJob_RES;

typedef struct
{
    int     dummy;
} trc_DeleteRecordingJob_RES;

typedef struct
{
    RecordingJobList * RecordingJobs;
} trc_GetRecordingJobs_RES;

typedef struct
{
    onvif_RecordingJobConfiguration JobConfiguration;   // required
} trc_SetRecordingJobConfiguration_RES;

typedef struct
{
    onvif_RecordingJobConfiguration JobConfiguration;   // required
} trc_GetRecordingJobConfiguration_RES;

typedef struct
{
    int     dummy;
} trc_SetRecordingJobMode_RES;

typedef struct
{
    onvif_RecordingJobStateInformation  State;          // required
} trc_GetRecordingJobState_RES;

typedef struct 
{
    char    OperationToken[ONVIF_TOKEN_LEN];            // required, Unique operation token for client to associate the relevant events
    uint32  sizeFileNames;                              // sequence of elements <FileNames> 
    char    FileNames[100][256];                        // optional, List of exported file names. The device can also use AsyncronousOperationStatus event to publish this list
} trc_ExportRecordedData_RES;

typedef struct 
{
    float   Progress;                                   // required, Progress percentage of ExportRecordedData operation
    onvif_ArrayOfFileProgress   FileProgressStatus;     // required, 
} trc_StopExportRecordedData_RES;

typedef struct 
{
    float   Progress;                                   // required, Progress percentage of ExportRecordedData operation
    onvif_ArrayOfFileProgress   FileProgressStatus;     // required, 
} trc_GetExportRecordedDataState_RES;

typedef struct
{
    onvif_ReplayCapabilities Capabilities;              // required, The capabilities for the replay service
} trp_GetServiceCapabilities_RES;

typedef struct
{
    char    Uri[ONVIF_URI_LEN];                         // required. The URI to which the client should connect in order to stream the recording
} trp_GetReplayUri_RES;

typedef struct
{
    onvif_ReplayConfiguration   Configuration;          // required, The current replay configuration parameters
} trp_GetReplayConfiguration_RES;

typedef struct
{
    int     dummy;
} trp_SetReplayConfiguration_RES;

typedef struct
{
    onvif_SearchCapabilities Capabilities;              // required, The capabilities for the search service
} tse_GetServiceCapabilities_RES;

typedef struct
{
    time_t  DataFrom;                                   // Required. The earliest point in time where there is recorded data on the device.
    time_t  DataUntil;                                  // Required. The most recent point in time where there is recorded data on the device
    int     NumberRecordings;                           // Required. The device contains this many recordings
} tse_GetRecordingSummary_RES;

typedef struct
{
    onvif_RecordingInformation  RecordingInformation;   // required
} tse_GetRecordingInformation_RES;

typedef struct
{
    onvif_MediaAttributes   MediaAttributes;
} tse_GetMediaAttributes_RES;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // Required, A unique reference to the search session created by this request
} tse_FindRecordings_RES;

typedef struct
{
    onvif_FindRecordingResultList   ResultList;
} tse_GetRecordingSearchResults_RES;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // Required, A unique reference to the search session created by this request
} tse_FindEvents_RES;

typedef struct
{
    onvif_FindEventResultList   ResultList;             // Required, 
} tse_GetEventSearchResults_RES;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // Required, A unique reference to the search session created by this request
} tse_FindMetadata_RES;

typedef struct
{
    onvif_FindMetadataResultList    ResultList;         // Required,          
} tse_GetMetadataSearchResults_RES;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // Required, A unique reference to the search session created by this request
} tse_FindPTZPosition_RES;

typedef struct
{
    onvif_FindPTZPositionResultList ResultList;         // Required, 
} tse_GetPTZPositionSearchResults_RES;

typedef struct
{
    onvif_SearchState   State;
} tse_GetSearchState_RES;

typedef struct
{
    time_t      Endpoint;                               // Required, The point of time the search had reached when it was ended. It is equal to the EndPoint specified in Find-operation if the search was completed
} tse_EndSearch_RES;

typedef struct
{
    onvif_AnalyticsCapabilities Capabilities;           // required, The capabilities for the Analytics service
} tan_GetServiceCapabilities_RES;

typedef struct
{
    onvif_SupportedRules    SupportedRules;
} tan_GetSupportedRules_RES;

typedef struct
{
    int     dummy;
} tan_CreateRules_RES;

typedef struct
{
    int     dummy;
} tan_DeleteRules_RES;

typedef struct
{
    ConfigList * Rule;                                  // optional
} tan_GetRules_RES;

typedef struct
{
    int     dummy;
} tan_ModifyRules_RES;

typedef struct
{
    int     dummy;
} tan_CreateAnalyticsModules_RES;

typedef struct
{
    int     dummy;
} tan_DeleteAnalyticsModules_RES;

typedef struct
{
    ConfigList * AnalyticsModule;                       // optional
} tan_GetAnalyticsModules_RES;

typedef struct
{
    int     dummy;
} tan_ModifyAnalyticsModules_RES;

typedef struct
{
    onvif_SupportedAnalyticsModules SupportedAnalyticsModules;  // required element of type ns2:SupportedAnalyticsModules
} tan_GetSupportedAnalyticsModules_RES;

typedef struct
{
    ConfigOptionsList * RuleOptions;                    // optional, A device shall provide respective ConfigOptions.RuleType for each RuleOption 
                                                        //  if the request does not specify RuleType
} tan_GetRuleOptions_RES;

typedef struct
{
    AnalyticsModuleConfigOptionsList * Options;         // optional, 
} tan_GetAnalyticsModuleOptions_RES;

typedef struct
{
    MetadataInfoList * AnalyticsModule;                 // optional, 
} tan_GetSupportedMetadata_RES;

// onvif media service 2 interfaces

typedef struct
{
    onvif_MediaCapabilities2 Capabilities;              // required, The capabilities for the medai 2 service
} tr2_GetServiceCapabilities_RES;

typedef struct
{
    VideoEncoder2ConfigurationList * Configurations;    // This element contains a list of video encoder configurations
} tr2_GetVideoEncoderConfigurations_RES;

typedef struct
{
    VideoEncoder2ConfigurationOptionsList * Options;    // required
} tr2_GetVideoEncoderConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetVideoEncoderConfiguration_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token assigned by the device for the newly created profile
} tr2_CreateProfile_RES;

typedef struct
{
    MediaProfileList * Profiles;                        // Lists all profiles that exist in the media service. 
                                                        //  The response provides the requested types of Configurations as far as available. 
                                                        //  If a profile doesn't contain a type no error shall be provided
} tr2_GetProfiles_RES;

typedef struct
{
    int     dummy;
} tr2_DeleteProfile_RES;

typedef struct
{
    char    Uri[256];                                   // required, Stable Uri to be used for requesting the media stream
} tr2_GetStreamUri_RES;

typedef struct
{
    VideoSourceConfigurationList * Configurations;      // This element contains a list of video source configurations
} tr2_GetVideoSourceConfigurations_RES;

typedef struct
{
    onvif_VideoSourceConfigurationOptions   Options;    // This message contains the video source configuration options.
                                                        //  If a video source configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} tr2_GetVideoSourceConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetVideoSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} tr2_SetSynchronizationPoint_RES;

typedef struct
{
    MetadataConfigurationList * Configurations;         // This element contains a list of metadata configurations
} tr2_GetMetadataConfigurations_RES;

typedef struct
{
    onvif_MetadataConfigurationOptions  Options;        // This message contains the metadata configuration options. 
                                                        //  If a metadata configuration is specified, the options shall concern that particular configuration.
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} tr2_GetMetadataConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetMetadataConfiguration_RES;

typedef struct
{
    AudioEncoder2ConfigurationList * Configurations;    // This element contains a list of audio encoder configurations
} tr2_GetAudioEncoderConfigurations_RES;

typedef struct
{
    AudioSourceConfigurationList * Configurations;      // This element contains a list of audio source configurations
} tr2_GetAudioSourceConfigurations_RES;

typedef struct
{
    int     dummy;
} tr2_GetAudioSourceConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetAudioSourceConfiguration_RES;

typedef struct
{
    int     dummy;
} tr2_SetAudioEncoderConfiguration_RES;

typedef struct
{
    AudioEncoder2ConfigurationOptionsList * Options;    // This message contains the audio encoder configuration options. 
                                                        //  If a audio encoder configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} tr2_GetAudioEncoderConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_AddConfiguration_RES;

typedef struct
{
    int     dummy;
} tr2_RemoveConfiguration_RES;

typedef struct
{
    onvif_EncoderInstanceInfo   Info;                   // required, The minimum guaranteed total number of encoder instances (applications) per VideoSourceConfiguration
} tr2_GetVideoEncoderInstances_RES;

typedef struct
{
    AudioOutputConfigurationList * Configurations;      // optional, This element contains a list of audio output configurations
} tr2_GetAudioOutputConfigurations_RES;

typedef struct
{
    onvif_AudioOutputConfigurationOptions   Options;    // required, This message contains the audio output configuration options.
                                                        //  If a audio output configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} tr2_GetAudioOutputConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetAudioOutputConfiguration_RES;

typedef struct
{
    AudioDecoderConfigurationList * Configurations;     // optional, This element contains a list of audio decoder configurations
} tr2_GetAudioDecoderConfigurations_RES;

typedef struct
{
    AudioEncoder2ConfigurationOptionsList * Options;    // optional, This message contains the audio decoder configuration options.
                                                        //  If a audio decoder configuration is specified, the options shall concern that particular configuration. 
                                                        //  If a media profile is specified, the options shall be compatible with that media profile. 
                                                        //  If no tokens are specified, the options shall be considered generic for the device
} tr2_GetAudioDecoderConfigurationOptions_RES;

typedef struct
{
    int     dummy;
} tr2_SetAudioDecoderConfiguration_RES;

typedef struct
{
    char    Uri[512];                                   // required, Stable Uri to be used for requesting snapshot images
} tr2_GetSnapshotUri_RES;

typedef struct
{
    int     dummy;
} tr2_StartMulticastStreaming_RES;

typedef struct
{
    int     dummy;
} tr2_StopMulticastStreaming_RES;

typedef struct
{
    VideoSourceModeList * VideoSourceModes;             //required, Return the information for specified video source mode
} tr2_GetVideoSourceModes_RES;

typedef struct
{
    BOOL    Reboot;                                     // required, The response contains information about rebooting after returning response. 
                                                        //  When Reboot is set true, a device will reboot automatically after setting mode
} tr2_SetVideoSourceMode_RES;

typedef struct
{
    char    OSDToken[ONVIF_TOKEN_LEN];                  // required, Returns Token of the newly created OSD
} tr2_CreateOSD_RES;

typedef struct
{
    int     dummy;
} tr2_DeleteOSD_RES;

typedef struct
{
    OSDConfigurationList * OSDs;                        // optional, This element contains a list of requested OSDs
} tr2_GetOSDs_RES;

typedef struct
{
    int     dummy;
} tr2_SetOSD_RES;

typedef struct
{
    onvif_OSDConfigurationOptions   OSDOptions;         // required
} tr2_GetOSDOptions_RES;

typedef struct
{
    VideoAnalyticsConfigurationList * Configurations;   // optional, This element contains a list of Analytics configurations
} tr2_GetAnalyticsConfigurations_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Returns Token of the newly created Mask
} tr2_CreateMask_RES;

typedef struct
{
    MaskList * Masks;                                   // optional, List of Mask configurations
} tr2_GetMasks_RES;

typedef struct
{
    int     dummy;
} tr2_SetMask_RES;

typedef struct 
{
    int     dummy;
} tr2_DeleteMask_RES;

typedef struct 
{
    onvif_MaskOptions   Options;                        // required, 
} tr2_GetMaskOptions_RES;

// end of onvif media 2 interfaces

typedef struct
{
    onvif_AccessControlCapabilities Capabilities;       // required, The capabilities for the access control service
} tac_GetServiceCapabilities_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    AccessPointList * AccessPointInfo;                  // optional, List of AccessPointInfo items
} tac_GetAccessPointInfoList_RES;

typedef struct
{
    AccessPointList * AccessPointInfo;                  // List of AccessPointInfo items
} tac_GetAccessPointInfo_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    AccessPointList * AccessPoint;                      // optional, List of AccessPoint items
} tac_GetAccessPointList_RES;

typedef struct
{
    AccessPointList * AccessPoint;                      // optional, List of AccessPoint items
} tac_GetAccessPoints_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required
} tac_CreateAccessPoint_RES;

typedef struct
{
    int     dummy;
} tac_SetAccessPoint_RES;

typedef struct
{
    int     dummy;
} tac_ModifyAccessPoint_RES;

typedef struct
{
    int     dummy;
} tac_DeleteAccessPoint_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    AreaList * AreaInfo;                                // optional, List of AreaInfo items
} tac_GetAreaInfoList_RES;

typedef struct
{
    AreaList * AreaInfo;                                // List of AreaInfo items
} tac_GetAreaInfo_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    AreaList * Area;                                    // optional, List of Area items
} tac_GetAreaList_RES;

typedef struct
{
    AreaList * Area;                                    // optional, List of Area items
} tac_GetAreas_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required
} tac_CreateArea_RES;

typedef struct
{
    int     dummy;
} tac_SetArea_RES;

typedef struct
{
    int     dummy;
} tac_ModifyArea_RES;

typedef struct
{
    int     dummy;
} tac_DeleteArea_RES;

typedef struct
{
    BOOL    Enabled;                                    // required, Indicates that the AccessPoint is enabled. 
                                                        //  By default this field value shall be True, if the DisableAccessPoint capabilities is not supported
} tac_GetAccessPointState_RES;

typedef struct
{
    int     dummy;
} tac_EnableAccessPoint_RES;

typedef struct
{
    int     dummy;
} tac_DisableAccessPoint_RES;

typedef struct
{
    onvif_DoorControlCapabilities Capabilities;         // required, The capabilities for the door control service
} tdc_GetServiceCapabilities_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    DoorInfoList * DoorInfo;                            // optional, List of DoorInfo items
} tdc_GetDoorInfoList_RES;

typedef struct
{
    DoorInfoList * DoorInfo;                            // List of DoorInfo items
} tdc_GetDoorInfo_RES;

typedef struct
{
    onvif_DoorState DoorState;
} tdc_GetDoorState_RES;

typedef struct
{
    int     dummy;
} tdc_AccessDoor_RES;

typedef struct
{
    int     dummy;
} tdc_LockDoor_RES;

typedef struct
{
    int     dummy;
} tdc_UnlockDoor_RES;

typedef struct
{
    int     dummy;
} tdc_DoubleLockDoor_RES;

typedef struct
{
    int     dummy;
} tdc_BlockDoor_RES;

typedef struct
{
    int     dummy;
} tdc_LockDownDoor_RES;

typedef struct
{
    int     dummy;
} tdc_LockDownReleaseDoor_RES;

typedef struct
{
    int     dummy;
} tdc_LockOpenDoor_RES;

typedef struct
{
    int     dummy;
} tdc_LockOpenReleaseDoor_RES;

typedef struct
{
    DoorList * Door;                                    // optional, List of Door items
} tdc_GetDoors_RES;

typedef struct
{
    uint32  NextStartReferenceFlag : 1;
    uint32  Reserved               : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If absent, no more items to get
    
    DoorList * Door;                                    // optional, List of Door items
} tdc_GetDoorList_RES;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of created Door item
} tdc_CreateDoor_RES;

typedef struct
{
    int     dummy;
} tdc_SetDoor_RES;

typedef struct
{
    int     dummy;
} tdc_ModifyDoor_RES;

typedef struct
{
    int     dummy;
} tdc_DeleteDoor_RES;

// thermal service

typedef struct
{
    onvif_ThermalCapabilities Capabilities;             // required, The capabilities for the thermal service
} tth_GetServiceCapabilities_RES;

typedef struct
{
    ThermalConfigurationList * Configurations;          // This element contains a list of thermal VideoSource configurations
} tth_GetConfigurations_RES;

typedef struct
{
    onvif_ThermalConfiguration  Configuration;          // required, thermal Settings for the VideoSource that was requested
} tth_GetConfiguration_RES;

typedef struct
{
    int     dummy;
} tth_SetConfiguration_RES;

typedef struct
{
    onvif_ThermalConfigurationOptions   Options;        // required, Valid ranges for the Thermal configuration parameters that are categorized as device specific
} tth_GetConfigurationOptions_RES;

typedef struct
{
    onvif_RadiometryConfiguration   Configuration;      // required, Radiometry Configuration for the VideoSource that was requested
} tth_GetRadiometryConfiguration_RES;

typedef struct
{
    int     dummy;
} tth_SetRadiometryConfiguration_RES;

typedef struct
{
    onvif_RadiometryConfigurationOptions    Options;    // required, Valid ranges for the Thermal Radiometry parameters that are categorized as device specific
} tth_GetRadiometryConfigurationOptions_RES;

typedef struct 
{
    onvif_CredentialCapabilities Capabilities;          // required, The capabilities for the credential service
} tcr_GetServiceCapabilities_RES;

typedef struct 
{
    uint32  sizeCredentialInfo;                         // sequence of elements <CredentialInfo>

    onvif_CredentialInfo CredentialInfo[CREDENTIAL_MAX_LIMIT];  // optional, List of CredentialInfo items
} tcr_GetCredentialInfo_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeCredentialInfo;                         // sequence of elements <CredentialInfo>

    onvif_CredentialInfo CredentialInfo[CREDENTIAL_MAX_LIMIT];  // optional, List of CredentialInfo items
} tcr_GetCredentialInfoList_RES;

typedef struct 
{
    uint32  sizeCredential;                             // sequence of elements <Credential>
    
    onvif_Credential    Credential[CREDENTIAL_MAX_LIMIT];   // optional, List of Credential items
} tcr_GetCredentials_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeCredential;                             // sequence of elements <Credential>
    onvif_Credential Credential[CREDENTIAL_MAX_LIMIT];  // optional, List of Credential items
} tcr_GetCredentialList_RES;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the created credential
} tcr_CreateCredential_RES;

typedef struct 
{
    int     dummy;
} tcr_ModifyCredential_RES;

typedef struct 
{
    int     dummy;
} tcr_DeleteCredential_RES;

typedef struct 
{
    onvif_CredentialState   State;                      // required, State of the credential
} tcr_GetCredentialState_RES;

typedef struct 
{
    int     dummy;
} tcr_EnableCredential_RES;

typedef struct 
{
    int     dummy;
} tcr_DisableCredential_RES;

typedef struct 
{
    int     dummy;
} tcr_SetCredential_RES;

typedef struct 
{
    int     dummy;
} tcr_ResetAntipassbackViolation_RES;

typedef struct 
{
    uint32  sizeFormatTypeInfo;                         // sequence of elements <FormatTypeInfo>

    onvif_CredentialIdentifierFormatTypeInfo FormatTypeInfo[CREDENTIAL_MAX_LIMIT];  // required, Identifier format types
} tcr_GetSupportedFormatTypes_RES;

typedef struct 
{
    uint32  sizeCredentialIdentifier;                   // sequence of elements <CredentialIdentifier> 

    onvif_CredentialIdentifier CredentialIdentifier[CREDENTIAL_MAX_LIMIT];  // optional, Identifiers of the credential
} tcr_GetCredentialIdentifiers_RES;

typedef struct 
{
    int     dummy;
} tcr_SetCredentialIdentifier_RES;

typedef struct 
{
    int     dummy;
} tcr_DeleteCredentialIdentifier_RES;

typedef struct 
{
    uint32  sizeCredentialAccessProfile;                // sequence of elements <CredentialAccessProfile>
    
    onvif_CredentialAccessProfile CredentialAccessProfile[CREDENTIAL_MAX_LIMIT];    // optional, Access Profiles of the credential
} tcr_GetCredentialAccessProfiles_RES;

typedef struct 
{
    int     dummy;
} tcr_SetCredentialAccessProfiles_RES;

typedef struct 
{
    int     dummy;
} tcr_DeleteCredentialAccessProfiles_RES;

typedef struct 
{
    onvif_AccessRulesCapabilities Capabilities;         // required, The capabilities for the access rules service
} tar_GetServiceCapabilities_RES;

typedef struct 
{
    uint32  sizeAccessProfileInfo;                      // sequence of elements <AccessProfileInfo>

    onvif_AccessProfileInfo AccessProfileInfo[ACCESSRULES_MAX_LIMIT];   // optional, List of AccessProfileInfo items
} tar_GetAccessProfileInfo_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field NextStartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, 
    uint32  sizeAccessProfileInfo;                      // sequence of elements <AccessProfileInfo>

    onvif_AccessProfileInfo     AccessProfileInfo[ACCESSRULES_MAX_LIMIT];   // optional, List of AccessProfileInfo items
} tar_GetAccessProfileInfoList_RES;

typedef struct 
{
    uint32  sizeAccessProfile;                          // sequence of elements <AccessProfile>

    onvif_AccessProfile AccessProfile[ACCESSRULES_MAX_LIMIT];   // optional, List of Access Profile items
} tar_GetAccessProfiles_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field NextStartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, 
    uint32  sizeAccessProfile;                          // sequence of elements <AccessProfile>

    onvif_AccessProfile AccessProfile[ACCESSRULES_MAX_LIMIT];   // optional, List of Access Profile items
} tar_GetAccessProfileList_RES;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The Token of created AccessProfile
} tar_CreateAccessProfile_RES;

typedef struct 
{
    int     dummy;
} tar_ModifyAccessProfile_RES;

typedef struct 
{
    int     dummy;
} tar_DeleteAccessProfile_RES;

typedef struct
{
    onvif_ScheduleCapabilities Capabilities;            // required, The capabilities for the Schedule service
} tsc_GetServiceCapabilities_RES;

typedef struct
{
    uint32  sizeScheduleInfo;                           // sequence of elements <ScheduleInfo>
    onvif_ScheduleInfo  ScheduleInfo[SCHEDULE_MAX_LIMIT];   // optional, List of ScheduleInfo items
} tsc_GetScheduleInfo_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeScheduleInfo;                           // sequence of elements <ScheduleInfo>

    onvif_ScheduleInfo ScheduleInfo[SCHEDULE_MAX_LIMIT];    // optional, List of ScheduleInfo items
} tsc_GetScheduleInfoList_RES;

typedef struct 
{
    uint32  sizeSchedule;                               // sequence of elements <Schedule>
    onvif_Schedule  Schedule[SCHEDULE_MAX_LIMIT];       // optional, List of schedule items
} tsc_GetSchedules_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeSchedule;                               // sequence of elements <Schedule>

    onvif_Schedule Schedule[SCHEDULE_MAX_LIMIT];        // optional, List of Schedule items
} tsc_GetScheduleList_RES;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of created Schedule
} tsc_CreateSchedule_RES;

typedef struct 
{
    int     dummy;
} tsc_ModifySchedule_RES;

typedef struct 
{
    int     dummy;
} tsc_DeleteSchedule_RES;

typedef struct 
{
    uint32  sizeSpecialDayGroupInfo;                    // sequence of elements <SpecialDayGroupInfo>
    onvif_SpecialDayGroupInfo  SpecialDayGroupInfo[SCHEDULE_MAX_LIMIT];   // optional, List of SpecialDayGroupInfo items
} tsc_GetSpecialDayGroupInfo_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeSpecialDayGroupInfo;                    // sequence of elements <SpecialDayGroupInfo>

    onvif_SpecialDayGroupInfo SpecialDayGroupInfo[SCHEDULE_MAX_LIMIT];    // optional, List of SpecialDayGroupInfo items
} tsc_GetSpecialDayGroupInfoList_RES;

typedef struct 
{
    uint32  sizeSpecialDayGroup;                        // sequence of elements <SpecialDayGroup>
    onvif_SpecialDayGroup  SpecialDayGroup[SCHEDULE_MAX_LIMIT];   // optional, List of SpecialDayGroup items
} tsc_GetSpecialDayGroups_RES;

typedef struct 
{
    uint32  NextStartReferenceFlag  : 1;                // Indicates whether the field StartReference is valid
    uint32  Reserved                : 31;
    
    char    NextStartReference[ONVIF_TOKEN_LEN];        // optional, StartReference to use in next call to get the following items. If
                                                        //  absent, no more items to get
    uint32  sizeSpecialDayGroup;                        // sequence of elements <SpecialDayGroup>

    onvif_SpecialDayGroup SpecialDayGroup[SCHEDULE_MAX_LIMIT];    // optional, List of SpecialDayGroup items
} tsc_GetSpecialDayGroupList_RES;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of created special day group
} tsc_CreateSpecialDayGroup_RES;

typedef struct 
{
    int     dummy;
} tsc_ModifySpecialDayGroup_RES;

typedef struct 
{
    int     dummy;
} tsc_DeleteSpecialDayGroup_RES;

typedef struct 
{
    onvif_ScheduleState ScheduleState;                  // required, ScheduleState item
} tsc_GetScheduleState_RES;

typedef struct
{
    onvif_ReceiverCapabilities Capabilities;            // required, The capabilities for the Receiver service
} trv_GetServiceCapabilities_RES;

typedef struct
{
    ReceiverList * Receivers;                           // required, A list of all receivers that currently exist on the device
} trv_GetReceivers_RES;

typedef struct
{
    onvif_Receiver  Receiver;                           // required, The details of the receiver
} trv_GetReceiver_RES;

typedef struct 
{
    onvif_Receiver  Receiver;                           // required, The details of the receiver that was created
} trv_CreateReceiver_RES;

typedef struct 
{
    int     dummy;
} trv_DeleteReceiver_RES;

typedef struct 
{
    int     dummy;
} trv_ConfigureReceiver_RES;

typedef struct 
{
    int     dummy;
} trv_SetReceiverMode_RES;

typedef struct 
{
    onvif_ReceiverStateInformation  ReceiverState;      // required, Description of the current receiver state
} trv_GetReceiverState_RES;

typedef struct 
{
    onvif_ProvisioningCapabilities  Capabilities;       // required, The capabilities for the Receiver service
} tpv_GetServiceCapabilities_RES;

typedef struct 
{
    int     dummy;
} tpv_PanMove_RES;

typedef struct 
{
    int     dummy;
} tpv_TiltMove_RES;

typedef struct 
{
    int     dummy;
} tpv_ZoomMove_RES;

typedef struct 
{
} tpv_RollMove_RES;

typedef struct 
{
    int     dummy;
} tpv_FocusMove_RES;

typedef struct 
{
    int     dummy;
} tpv_Stop_RES;

typedef struct 
{
    onvif_Usage Usage;                                  // required, The set of lifetime usage values for the provisioning associated with the video source
} tpv_GetUsage_RES;


#endif // end of ONVIF_RES_H



