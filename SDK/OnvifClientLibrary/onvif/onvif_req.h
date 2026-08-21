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

#ifndef ONVIF_REQ_H
#define ONVIF_REQ_H


/*************************************************************************
 *
 * onvif request structure
 *
**************************************************************************/ 

typedef struct
{
    onvif_CapabilityCategory    Category;               // optional, List of categories to retrieve capability information on
} tds_GetCapabilities_REQ;

typedef struct
{
    BOOL    IncludeCapability;                          // required, Indicates if the service capabilities (untyped) should be included in the response.
} tds_GetServices_REQ;

typedef struct
{
    int     dummy;
} tds_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} tds_GetDeviceInformation_REQ;

typedef struct
{
    int     dummy;
} tds_GetUsers_REQ;

typedef struct
{
    onvif_User  User;                                   // required
} tds_CreateUsers_REQ;

typedef struct
{
    char    Username[ONVIF_NAME_LEN];                   // required
} tds_DeleteUsers_REQ;

typedef struct
{
    onvif_User  User;                                   // required
} tds_SetUser_REQ;

typedef struct
{
    int     dummy;
} tds_GetRemoteUser_REQ;

typedef struct
{
    uint32  RemoteUserFlag  : 1;                        // Indicates whether the field RemoteUser is valid
    uint32  Reserved        : 31;
    
    onvif_RemoteUser RemoteUser;                        // optional     
} tds_SetRemoteUser_REQ;

typedef struct
{
    int     dummy;
} tds_GetNetworkInterfaces_REQ;

typedef struct
{
    onvif_NetworkInterface  NetworkInterface;
} tds_SetNetworkInterfaces_REQ;

typedef struct
{
    int     dummy;
} tds_GetNTP_REQ;

typedef struct 
{
    onvif_NTPInformation    NTPInformation;
} tds_SetNTP_REQ;

typedef struct
{
    int     dummy;
} tds_GetHostname_REQ;

typedef struct
{
    char    Name[ONVIF_NAME_LEN];                       // required 
} tds_SetHostname_REQ;

typedef struct
{
    BOOL    FromDHCP;                                   //  required, True if the hostname shall be obtained via DHCP
} tds_SetHostnameFromDHCP_REQ;

typedef struct
{
    int     dummy;
} tds_GetDNS_REQ;

typedef struct 
{
    onvif_DNSInformation DNSInformation;
} tds_SetDNS_REQ;

typedef struct
{
    int     dummy;
} tds_GetDynamicDNS_REQ;

typedef struct
{
    onvif_DynamicDNSInformation DynamicDNSInformation;
} tds_SetDynamicDNS_REQ;

typedef struct
{
    int     dummy;
} tds_GetNetworkProtocols_REQ;

typedef struct
{
    onvif_NetworkProtocol   NetworkProtocol;
} tds_SetNetworkProtocols_REQ;

typedef struct
{
    int     dummy;
} tds_GetDiscoveryMode_REQ;

typedef struct
{
    onvif_DiscoveryMode DiscoveryMode;                  // required, Indicator of discovery mode: Discoverable, NonDiscoverable
} tds_SetDiscoveryMode_REQ;

typedef struct
{
    int     dummy;
} tds_GetNetworkDefaultGateway_REQ;

typedef struct
{
    char    IPv4Address[MAX_GATEWAY][32];               // optional, Sets IPv4 gateway address used as default setting
    char    IPv6Address[MAX_GATEWAY][64];               // optional, Sets IPv6 gateway address used as default setting
} tds_SetNetworkDefaultGateway_REQ;

typedef struct
{
    int     dummy;
} tds_GetZeroConfiguration_REQ;

typedef struct
{
    char    InterfaceToken[ONVIF_TOKEN_LEN];            // requied, Unique identifier referencing the physical interface
    BOOL    Enabled;                                    // requied, Specifies if the zero-configuration should be enabled or not
} tds_SetZeroConfiguration_REQ;

typedef struct
{
    int     dummy;
} tds_GetEndpointReference_REQ;

typedef struct
{
    char    AuxiliaryCommand[1024];                     // required 
} tds_SendAuxiliaryCommand_REQ;

typedef struct
{
    int     dummy;
} tds_GetRelayOutputs_REQ;

typedef struct
{
    char    RelayOutputToken[ONVIF_TOKEN_LEN];          // required
    
    onvif_RelayOutputSettings   Properties;             // required
} tds_SetRelayOutputSettings_REQ;

typedef struct
{
    char    RelayOutputToken[ONVIF_TOKEN_LEN];          // required
    
    onvif_RelayLogicalState     LogicalState;           // required
} tds_SetRelayOutputState_REQ;

typedef struct
{
    int     dummy;
} tds_GetSystemDateAndTime_REQ;

typedef struct
{
    uint32  UTCDateTimeFlag : 1;                        // Indicates whether the field UTCDateTime is valid
    uint32  Reserved        : 31;
    
    onvif_SystemDateTime    SystemDateTime;             // required,     
    onvif_DateTime          UTCDateTime;                // optional, Date and time in UTC. If time is obtained via NTP, UTCDateTime has no meaning
} tds_SetSystemDateAndTime_REQ;

typedef struct
{
    int     dummy;
} tds_SystemReboot_REQ;

typedef struct
{
    onvif_FactoryDefaultType    FactoryDefault;         // required
} tds_SetSystemFactoryDefault_REQ;

typedef struct
{
    onvif_SystemLogType LogType;                        // required, Specifies the type of system log to get
} tds_GetSystemLog_REQ;

typedef struct
{
    int     dummy;
} tds_GetScopes_REQ;

typedef struct
{
    int     sizeScopes;
    char    Scopes[MAX_SCOPE_NUMS][100];
} tds_SetScopes_REQ;

typedef struct
{
    int     sizeScopeItem;
    char    ScopeItem[MAX_SCOPE_NUMS][100];             // required
} tds_AddScopes_REQ;

typedef struct
{
    int     sizeScopeItem;
    char    ScopeItem[MAX_SCOPE_NUMS][100];             // required
} tds_RemoveScopes_REQ;

typedef struct
{
    int     dummy;
} tds_StartFirmwareUpgrade_REQ;

typedef struct
{
    int     dummy;
} tds_GetSystemUris_REQ;

typedef struct
{
    int     dummy;
} tds_StartSystemRestore_REQ;

typedef struct
{
    int     dummy;
} tds_GetWsdlUrl_REQ;

typedef struct
{
    int     dummy;
} tds_GetDot11Capabilities_REQ;

typedef struct
{
    char    InterfaceToken[ONVIF_TOKEN_LEN];            // required
} tds_GetDot11Status_REQ;

typedef struct
{
    char    InterfaceToken[ONVIF_TOKEN_LEN];            // required
} tds_ScanAvailableDot11Networks_REQ;

typedef struct
{
    int     dummy;
} tds_GetGeoLocation_REQ;

typedef struct
{
    LocationEntityList * Location;                      // required
} tds_SetGeoLocation_REQ;

typedef struct
{
    LocationEntityList * Location;                      // required
} tds_DeleteGeoLocation_REQ;

typedef struct
{
    char    Algorithm[64];                              // required, Hashing algorithm(s) used in HTTP and RTSP Digest Authentication
} tds_SetHashingAlgorithm_REQ;

typedef struct
{
    int     dummy;
} tds_GetIPAddressFilter_REQ;

typedef struct 
{
    onvif_IPAddressFilter   IPAddressFilter;            // required
} tds_SetIPAddressFilter_REQ;

typedef struct
{
    onvif_IPAddressFilter   IPAddressFilter;            // required
} tds_AddIPAddressFilter_REQ;

typedef struct
{
    onvif_IPAddressFilter   IPAddressFilter;            // required
} tds_RemoveIPAddressFilter_REQ;

typedef struct
{
    int     dummy;
} tds_GetAccessPolicy_REQ;

typedef struct
{
    onvif_BinaryData    PolicyFile;                     // required
} tds_SetAccessPolicy_REQ;

typedef struct
{
    int     dummy;
} tds_GetStorageConfigurations_REQ;

typedef struct
{
    onvif_StorageConfigurationData StorageConfiguration;    // required
} tds_CreateStorageConfiguration_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required
} tds_GetStorageConfiguration_REQ;

typedef struct
{
    onvif_StorageConfiguration StorageConfiguration;    // required
} tds_SetStorageConfiguration_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required
} tds_DeleteStorageConfiguration_REQ;

typedef struct
{
    int     dummy;
} trt_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} trt_GetVideoSources_REQ;

typedef struct
{
    int     dummy;
} trt_GetAudioSources_REQ;

typedef struct
{
    uint32  TokenFlag : 1;                              // Indicates whether the field Token is valid
    uint32  Reserved  : 31;
    
    char    Name[ONVIF_NAME_LEN];                       // required, friendly name of the profile to be created
    char    Token[ONVIF_TOKEN_LEN];                     // optional, Optional token, specifying the unique identifier of the new profile
} trt_CreateProfile_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, this command requests a specific profile
} trt_GetProfile_REQ;

typedef struct
{
    int     dummy;
} trt_GetProfiles_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoEncoderConfiguration to add
} trt_AddVideoEncoderConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoEncoderConfiguration to add
} trt_AddVideoSourceConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoEncoderConfiguration to add
} trt_AddAudioEncoderConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoEncoderConfiguration to add
} trt_AddAudioSourceConfiguration_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Contains a video source reference for which a video source mode is requested
} trt_GetVideoSourceModes_REQ;

typedef struct 
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Contains a video source reference for which a video source mode is requested
    char    VideoSourceModeToken[ONVIF_TOKEN_LEN];      // required, Indicate video source mode
} trt_SetVideoSourceMode_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoEncoderConfiguration to add
} trt_AddPTZConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the VideoEncoderConfiguration shall be removed
} trt_RemoveVideoEncoderConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the VideoSourceConfiguration shall be removed
} trt_RemoveVideoSourceConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the AudioEncoderConfiguration shall be removed
} trt_RemoveAudioEncoderConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the AudioSourceConfiguration shall be removed
} trt_RemoveAudioSourceConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the PTZConfiguration shall be removed
} trt_RemovePTZConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, This element contains a  reference to the profile that should be deleted
} trt_DeleteProfile_REQ;

typedef struct
{
    int     dummy;
} trt_GetVideoSourceConfigurations_REQ;

typedef struct
{
    int     dummy;
} trt_GetVideoEncoderConfigurations_REQ;

typedef struct
{
    int     dummy;
} trt_GetAudioSourceConfigurations_REQ;

typedef struct
{
    int     dummy;
} trt_GetAudioEncoderConfigurations_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested video source configuration
} trt_GetVideoSourceConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested video encoder configuration
} trt_GetVideoEncoderConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested audio source configuration
} trt_GetAudioSourceConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested audio encoder configuration
} trt_GetAudioEncoderConfiguration_REQ;

typedef struct
{
    onvif_VideoSourceConfiguration  VideoSourceConfiguration;

    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetVideoSourceConfiguration_REQ;

typedef struct
{
    onvif_VideoEncoderConfiguration VideoEncoderConfiguration;
    
    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetVideoEncoderConfiguration_REQ;

typedef struct
{
    onvif_AudioSourceConfiguration  AudioSourceConfiguration;

    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetAudioSourceConfiguration_REQ;

typedef struct
{
    onvif_AudioEncoderConfiguration AudioEncoderConfiguration;

    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetAudioEncoderConfiguration_REQ;

typedef struct
{
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  Reserved                : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional video source configurationToken that specifies an existing configuration that the options are intended for
} trt_GetVideoSourceConfigurationOptions_REQ;

typedef struct
{
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  Reserved                : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional video encoder configuration token that specifies an existing configuration that the options are intended for    
} trt_GetVideoEncoderConfigurationOptions_REQ;

typedef struct
{
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  Reserved                : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional audio source configuration token that specifies an existing configuration that the options are intended for
} trt_GetAudioSourceConfigurationOptions_REQ;

typedef struct
{
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  Reserved                : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional audio encoder configuration token that specifies an existing configuration that the options are intended for
} trt_GetAudioEncoderConfigurationOptions_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, The ProfileToken element indicates the media profile to use and will define the configuration of the content of the stream

    onvif_StreamSetup   StreamSetup;                    // required, Stream Setup that should be used with the uri
} trt_GetStreamUri_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a Profile reference for which a Synchronization Point is requested
} trt_SetSynchronizationPoint_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, The ProfileToken element indicates the media profile to use and will define the source and dimensions of the snapshot
} trt_GetSnapshotUri_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the video source configuration
} trt_GetGuaranteedNumberOfVideoEncoderInstances_REQ;

typedef struct 
{
    int     dummy;
} trt_GetAudioOutputs_REQ;

typedef struct 
{
    int     dummy;
} trt_GetAudioOutputConfigurations_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested audio output configuration
} trt_GetAudioOutputConfiguration_REQ;

typedef struct 
{
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  Reserved                : 30;
    
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional audio output configuration token that specifies an existing configuration that the options are intended for
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
} trt_GetAudioOutputConfigurationOptions_REQ;

typedef struct 
{
    onvif_AudioOutputConfiguration  Configuration;      // required, Contains the modified audio output configuration. The configuration shall exist in the device
    
    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetAudioOutputConfiguration_REQ;

typedef struct 
{
    int     dummy;
} trt_GetAudioDecoderConfigurations_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested audio decoder configuration
} trt_GetAudioDecoderConfiguration_REQ;

typedef struct 
{
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid
    uint32  Reserved                : 30;
    
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional audio decoder configuration token that specifies an existing configuration that the options are intended for
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
} trt_GetAudioDecoderConfigurationOptions_REQ;

typedef struct 
{
    onvif_AudioDecoderConfiguration Configuration;      // required, Contains the modified audio decoder configuration. The configuration shall exist in the device

    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetAudioDecoderConfiguration_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the AudioOutputConfiguration to add
} trt_AddAudioOutputConfiguration_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, This element contains a reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, This element contains a reference to the AudioDecoderConfiguration to add
} trt_AddAudioDecoderConfiguration_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the AudioOutputConfiguration shall be removed
} trt_RemoveAudioOutputConfiguration_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, This element contains a  reference to the media profile from which the AudioDecoderConfiguration shall be removed
} trt_RemoveAudioDecoderConfiguration_REQ;

typedef struct
{
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid    
    uint32  Reserved                : 31;
    
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Token of the Video Source Configuration, which has OSDs associated with are requested. If token not exist, request all available OSDs
} trt_GetOSDs_REQ;

typedef struct
{
    char    OSDToken[ONVIF_TOKEN_LEN];                  // required, The GetOSD command fetches the OSD configuration if the OSD token is known
} trt_GetOSD_REQ;

typedef struct
{
    onvif_OSDConfiguration  OSD;                        // required, Contains the modified OSD configuration
} trt_SetOSD_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Video Source Configuration Token that specifies an existing video source configuration that the options shall be compatible with
} trt_GetOSDOptions_REQ;

typedef struct
{
    onvif_OSDConfiguration  OSD;                        // required, Contain the initial OSD configuration for create
} trt_CreateOSD_REQ;

typedef struct
{
    char    OSDToken[ONVIF_TOKEN_LEN];                  // required, This element contains a reference to the OSD configuration that should be deleted
} trt_DeleteOSD_REQ;


typedef struct
{
    int     dummy;
} trt_GetVideoAnalyticsConfigurations_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Contains a reference to the VideoAnalyticsConfiguration to add
} trt_AddVideoAnalyticsConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, The requested video analytics configuration
} trt_GetVideoAnalyticsConfiguration_REQ;

typedef struct
{
    onvif_VideoAnalyticsConfiguration   Configuration;  // required, Contains the modified video analytics configuration. The configuration shall exist in the device

    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetVideoAnalyticsConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the VideoAnalyticsConfiguration shall be removed
} trt_RemoveVideoAnalyticsConfiguration_REQ;

typedef struct
{
    int     dummy;
} trt_GetMetadataConfigurations_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required,  Reference to the profile where the configuration should be added
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required,  Contains a reference to the MetadataConfiguration to add    
} trt_AddMetadataConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, The requested metadata configuration
} trt_GetMetadataConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a reference to the media profile from which the MetadataConfiguration shall be removed
} trt_RemoveMetadataConfiguration_REQ;

typedef struct
{
    onvif_MetadataConfiguration Configuration;          // required,  Contains the modified metadata configuration. The configuration shall exist in the device
    BOOL    ForcePersistence;                           // required, The ForcePersistence element is obsolete and should always be assumed to be true
} trt_SetMetadataConfiguration_REQ;

typedef struct
{
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid    
    uint32  ProfileTokenFlag        : 1;                // Indicates whether the field ProfileToken is valid    
    uint32  Reserved                : 30;
    
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional,  Optional metadata configuration token that specifies an existing configuration that the options are intended for
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional,  Optional ProfileToken that specifies an existing media profile that the options shall be compatible with
} trt_GetMetadataConfigurationOptions_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required 
} trt_GetCompatibleVideoEncoderConfigurations_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
} trt_GetCompatibleAudioEncoderConfigurations_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
} trt_GetCompatibleVideoAnalyticsConfigurations_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
} trt_GetCompatibleMetadataConfigurations_REQ;

typedef struct
{
    int     dummy;
} ptz_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} ptz_GetNodes_REQ;

typedef struct
{
    char    NodeToken[ONVIF_TOKEN_LEN];                 // required, Token of the requested PTZNode
} ptz_GetNode_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place
} ptz_GetPresets_REQ;

typedef struct
{
    uint32  PresetTokenFlag : 1;                        // Indicates whether the field PresetToken is valid
    uint32  PresetNameFlag  : 1;                        // Indicates whether the field PresetName is valid
    uint32  Reserved        : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place
    char    PresetToken[ONVIF_TOKEN_LEN];               // optional, A requested preset token
    char    PresetName[ONVIF_NAME_LEN];                 // optional, A requested preset name
} ptz_SetPreset_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place
    char    PresetToken[ONVIF_TOKEN_LEN];               // required, A requested preset token
} ptz_RemovePreset_REQ;

typedef struct
{
    uint32  SpeedFlag       : 1;                        // Indicates whether the field Speed is valid
    uint32  Reserved        : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place
    char    PresetToken[ONVIF_TOKEN_LEN];               // required, A requested preset token

    onvif_PTZSpeed  Speed;                              // optional, A requested speed.The speed parameter can only be specified when Speed Spaces are available for the PTZ Node
} ptz_GotoPreset_REQ;

typedef struct
{
    uint32  SpeedFlag       : 1;                        // Indicates whether the field Speed is valid
    uint32  Reserved        : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place

    onvif_PTZSpeed  Speed;                              // optional, A requested speed.The speed parameter can only be specified when Speed Spaces are available for the PTZ Node
} ptz_GotoHomePosition_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the home position should be set
} ptz_SetHomePosition_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the PTZStatus should be requested
} ptz_GetStatus_REQ;

typedef struct
{
    uint32  TimeoutFlag     : 1;                        // Indicates whether the field Timeout is valid
    uint32  Reserved        : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile
    
    onvif_PTZSpeed  Velocity;                           // required, A Velocity vector specifying the velocity of pan, tilt and zoom
    
    int     Timeout;                                    // optional, An optional Timeout parameter, unit is second
} ptz_ContinuousMove_REQ;

typedef struct
{
    uint32  SpeedFlag       : 1;                        // Indicates whether the field Speed is valid
    uint32  Reserved        : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile
    
    onvif_PTZVector Translation;                        // required, A positional Translation relative to the current position
    onvif_PTZSpeed  Speed;                              // optional, An optional Speed parameter
} ptz_RelativeMove_REQ;

typedef struct
{
    uint32  SpeedFlag       : 1;                        // Indicates whether the field Speed is valid
    uint32  Reserved        : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile

    onvif_PTZVector Position;                           // required, A Position vector specifying the absolute target position
    onvif_PTZSpeed  Speed;                              // optional, An optional Speed 
} ptz_AbsoluteMove_REQ;

typedef struct
{
    uint32  PanTiltFlag     : 1;                        // Indicates whether the field PanTilt is valid
    uint32  ZoomFlag        : 1;                        // Indicates whether the field Zoom is valid
    uint32  Reserved        : 30;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile that indicate what should be stopped

    BOOL    PanTilt;                                    // optional, Set true when we want to stop ongoing pan and tilt movements.If PanTilt arguments are not present, this command stops these movements
    BOOL    Zoom;                                       // optional, Set true when we want to stop ongoing zoom movement.If Zoom arguments are not present, this command stops ongoing zoom movement
} ptz_Stop_REQ;

typedef struct
{
    int     dummy;
} ptz_GetConfigurations_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the requested PTZConfiguration
} ptz_GetConfiguration_REQ;

typedef struct
{
    onvif_PTZConfiguration  PTZConfiguration;           // required
    
    BOOL    ForcePersistence;                           // required, Flag that makes configuration persistent. Example: User wants the configuration to exist after reboot
} ptz_SetConfiguration_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of an existing configuration that the options are intended for
} ptz_GetConfigurationOptions_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
} ptz_GetPresetTours_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
    char    PresetTourToken[ONVIF_TOKEN_LEN];           // required
} ptz_GetPresetTour_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
    char    PresetTourToken[ONVIF_TOKEN_LEN];           // optional
} ptz_GetPresetTourOptions_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
} ptz_CreatePresetTour_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
    
    onvif_PresetTour    PresetTour;                     // required
} ptz_ModifyPresetTour_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
    char    PresetTourToken[ONVIF_TOKEN_LEN];           // required
    
    onvif_PTZPresetTourOperation Operation;             // required
} ptz_OperatePresetTour_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required
    char    PresetTourToken[ONVIF_TOKEN_LEN];           // required
} ptz_RemovePresetTour_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile where the operation should take place
    char    AuxiliaryData[1024];                        // required, The Auxiliary request data
} ptz_SendAuxiliaryCommand_REQ;

typedef struct
{
    uint32  SpeedFlag       : 1;                        // Indicates whether the field Speed is valid
    uint32  AreaHeightFlag  : 1;                        // Indicates whether the field AreaHeight is valid
    uint32  AreaWidthFlag   : 1;                        // Indicates whether the field AreaHeight is valid
    uint32  Reserved        : 29;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, A reference to the MediaProfile

    onvif_GeoLocation   Target;                         // required, The geolocation of the target position
    onvif_PTZSpeed      Speed;                          // optional, An optional Speed

    float   AreaHeight;                                 // optional, An optional indication of the height of the target/area
    float   AreaWidth;                                  // optional, An optional indication of the width of the target/area
} ptz_GeoMove_REQ;

typedef struct
{
    int     dummy;
} tev_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} tev_GetEventProperties_REQ;

typedef struct
{
    int     TerminationTime;
} tev_Renew_REQ;

typedef struct
{
    int     dummy;
} tev_Unsubscribe_REQ;

typedef struct
{
    uint32  FiltersFlag : 1;
    uint32  Reserved    : 31;
    
    char    ConsumerReference[256];
    int     InitialTerminationTime;                     // InitialTerminationTime, unit is second

    onvif_EventFilter   Filter;
} tev_Subscribe_REQ;

typedef struct
{
    int     dummy;
} tev_PauseSubscription_REQ;

typedef struct
{
    int     dummy;
} tev_ResumeSubscription_REQ;

typedef struct
{
    uint32  FiltersFlag : 1;
    uint32  Reserved    : 31;
    
    int     InitialTerminationTime;                     // InitialTerminationTime, unit is second

    onvif_EventFilter   Filter;
} tev_CreatePullPointSubscription_REQ;

typedef struct
{
    int     dummy;
} tev_DestroyPullPoint_REQ;

typedef struct
{
    int     Timeout;                                    // required, Maximum time to block until this method returns, unit is second
    int     MessageLimit;                               // required, Upper limit for the number of messages to return at once. A server implementation may decide to return less messages
} tev_PullMessages_REQ;

typedef struct
{
    int     MaximumNumber;                              // required, Upper limit for the number of messages to return at once. A server implementation may decide to return less messages
} tev_GetMessages_REQ;

typedef struct
{
    time_t  UtcTime;                                    // required, The date and time to match against stored messages.
                                                        //  When Seek is used in the forward mode a device shall position the pull pointer to include all NotificationMessages in the persistent storage with a UtcTime attribute greater than or equal to the Seek argument. 
                                                        //  When Seek is used in reverse mode a device shall position the pull pointer to include all NotificationMessages in the in the persistent storage with a UtcTime attribute less than or equal to the Seek argument. 
    BOOL    Reverse;                                    // optional, Reverse the pull direction of PullMessages
} tev_Seek_REQ;

typedef struct
{
    int     dummy;
} tev_SetSynchronizationPoint_REQ;

typedef struct
{
    char    PostUrl[200];
    
    NotificationMessageList * notify;
} Notify_REQ;

// imaging service

typedef struct 
{
    int     dummy;
} img_GetServiceCapabilities_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required
} img_GetImagingSettings_REQ;

typedef struct
{
    uint32  ForcePersistenceFlag : 1;                   // Indicates whether the field ForcePersistence is valid
    uint32  Reserved             : 31;
    
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required
    BOOL    ForcePersistence;                           // optional

    onvif_ImagingSettings   ImagingSettings;            // required
} img_SetImagingSettings_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference token to the VideoSource for which the imaging parameter options are requested
} img_GetOptions_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource for the requested move (focus) operation
    
    onvif_FocusMove Focus;                              // required, Content of the requested move (focus) operation
} img_Move_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource
} img_Stop_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource
} img_GetStatus_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource
} img_GetMoveOptions_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource
} img_GetPresets_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference to the VideoSource
} img_GetCurrentPreset_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, , Reference to the VideoSource
    char    PresetToken[ONVIF_TOKEN_LEN];               // required, Reference token to the Imaging Preset to be applied to the specified Video Source
} img_SetCurrentPreset_REQ;

// device IO

typedef struct
{
    int     dummy;
} tmd_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} tmd_GetRelayOutputs_REQ;

typedef struct 
{
    uint32  RelayOutputTokenFlag : 1;
    uint32  Reserved             : 31;
    
    char    RelayOutputToken[ONVIF_TOKEN_LEN];          // optional, Optional reference token to the relay for which the options are requested
} tmd_GetRelayOutputOptions_REQ;

typedef struct 
{
    onvif_RelayOutput   RelayOutput;                    // required
} tmd_SetRelayOutputSettings_REQ;

typedef struct 
{
    char    RelayOutputToken[ONVIF_TOKEN_LEN];          // required
    
    onvif_RelayLogicalState LogicalState;               // required 
} tmd_SetRelayOutputState_REQ;

typedef struct
{
    int     dummy;
} tmd_GetDigitalInputs_REQ;

typedef struct 
{
    uint32  TokenFlag   : 1;
    uint32  Reserved    : 31;
    
    char    Token[ONVIF_TOKEN_LEN];                     // optional, 
} tmd_GetDigitalInputConfigurationOptions_REQ;

typedef struct
{
    DigitalInputList * DigitalInputs;
} tmd_SetDigitalInputConfigurations_REQ;

typedef struct
{
    int     dummy;
} tmd_GetSerialPorts_REQ;

typedef struct
{
    char    SerialPortToken[ONVIF_TOKEN_LEN];           // required 
} tmd_GetSerialPortConfiguration_REQ;

typedef struct
{
    onvif_SerialPortConfiguration   SerialPortConfiguration;    // required
    BOOL    ForcePersistance;                           // required 
} tmd_SetSerialPortConfiguration_REQ;

typedef struct
{
    char    SerialPortToken[ONVIF_TOKEN_LEN];           // required 
} tmd_GetSerialPortConfigurationOptions_REQ;

typedef struct
{
    char    token[ONVIF_TOKEN_LEN];                     // required, The physical serial port reference to be used when this request is invoked
    
    onvif_SendReceiveSerialCommand  Command;
} tmd_SendReceiveSerialCommand_REQ;

// recording

typedef struct
{
    int     dummy;
} trc_GetServiceCapabilities_REQ;

typedef struct
{
    onvif_RecordingConfiguration RecordingConfiguration;// required, Initial configuration for the recording
} trc_CreateRecording_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, 
} trc_DeleteRecording_REQ;

typedef struct
{
    int        dummy;
} trc_GetRecordings_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, Token of the recording that shall be changed
    
    onvif_RecordingConfiguration RecordingConfiguration;// required, The new configuration
} trc_SetRecordingConfiguration_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, 
} trc_GetRecordingConfiguration_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, 
} trc_GetRecordingOptions_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, Identifies the recording to which a track shall be added
    
    onvif_TrackConfiguration TrackConfiguration;        // required, The configuration of the new track
} trc_CreateTrack_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, Token of the recording the track belongs to
    char    TrackToken[ONVIF_TOKEN_LEN];                // required, Token of the track to be deleted
} trc_DeleteTrack_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, Token of the recording the track belongs to
    char    TrackToken[ONVIF_TOKEN_LEN];                // required, Token of the track
} trc_GetTrackConfiguration_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required, Token of the recording the track belongs to
    char    TrackToken[ONVIF_TOKEN_LEN];                // required, Token of the track to be modified
    
    onvif_TrackConfiguration    TrackConfiguration;     // required, New configuration for the track
} trc_SetTrackConfiguration_REQ;

typedef struct
{
    onvif_RecordingJobConfiguration JobConfiguration;   // required, The initial configuration of the new recording job
} trc_CreateRecordingJob_REQ;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required
} trc_DeleteRecordingJob_REQ;

typedef struct
{
    int     dummy;
} trc_GetRecordingJobs_REQ;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required, Token of the job to be modified
    
    onvif_RecordingJobConfiguration JobConfiguration;   // required, New configuration of the recording job
} trc_SetRecordingJobConfiguration_REQ;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required
} trc_GetRecordingJobConfiguration_REQ;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required, Token of the recording job
    char    Mode[16];                                   // required, The new mode for the recording job, The only valid values for Mode shall be "Idle" or "Active"
} trc_SetRecordingJobMode_REQ;

typedef struct
{
    char    JobToken[ONVIF_TOKEN_LEN];                  // required
} trc_GetRecordingJobState_REQ;

typedef struct
{
    time_t  StartPoint;                                 // optional, Optional parameter that specifies start time for the exporting
    time_t  EndPoint;                                   // optional, Optional parameter that specifies end time for the exporting
    onvif_SearchScope   SearchScope;                    // required, Indicates the selection criterion on the existing recordings
    char    FileFormat[32];                             // required, Indicates which export file format to be used
    onvif_StorageReferencePath  StorageDestination;     // required, Indicates the target storage and relative directory path
} trc_ExportRecordedData_REQ;

typedef struct 
{
    char    OperationToken[ONVIF_TOKEN_LEN];            // required, Unique ExportRecordedData operation token
} trc_StopExportRecordedData_REQ;

typedef struct 
{
    char    OperationToken[ONVIF_TOKEN_LEN];            // required, Unique ExportRecordedData operation token
} trc_GetExportRecordedDataState_REQ;

typedef struct
{
    int     dummy;
} trp_GetServiceCapabilities_REQ;

typedef struct
{
    onvif_StreamSetup   StreamSetup;
    
    char    RecordingToken[ONVIF_TOKEN_LEN];            // required. The identifier of the recording to be streamed
} trp_GetReplayUri_REQ;

typedef struct
{
    int     dummy;
} trp_GetReplayConfiguration_REQ;

typedef struct
{
    onvif_ReplayConfiguration   Configuration;          // required, Description of the new replay configuration parameters
} trp_SetReplayConfiguration_REQ;

typedef struct
{
    int     dummy;
} tse_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} tse_GetRecordingSummary_REQ;

typedef struct
{
    char    RecordingToken[ONVIF_TOKEN_LEN];            // Required
} tse_GetRecordingInformation_REQ;

typedef struct
{
    char    RecordingTokens[10][ONVIF_TOKEN_LEN];       // optional
    time_t  Time;                                       // required
} tse_GetMediaAttributes_REQ;

typedef struct
{
    uint32  MaxMatchesFlag  : 1;                        // Indicates whether the field MaxMatches is valid
    uint32  Reserved        : 31;
    
    onvif_SearchScope   Scope;                          // required
    
    int     MaxMatches;                                 // optional, The search will be completed after this many matches. If not specified, the search will continue until reaching the endpoint or until the session expires
    int     KeepAliveTime;                              // required, The time the search session will be kept alive after responding to this and subsequent requests. A device shall support at least values up to ten seconds
} tse_FindRecordings_REQ;

typedef struct
{    
    uint32  MinResultsFlag  : 1;                        // Indicates whether the field MinResults is valid
    uint32  MaxResultsFlag  : 1;                        // Indicates whether the field MaxResults is valid
    uint32  WaitTimeFlag    : 1;                        // Indicates whether the field WaitTime is valid
    uint32  Reserved        : 29;
    
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to get results from
    int     MinResults;                                 // optional, The minimum number of results to return in one response
    int     MaxResults;                                 // optional, The maximum number of results to return in one response
    int     WaitTime;                                   // optional, The maximum time before responding to the request, even if the MinResults parameter is not fulfilled
} tse_GetRecordingSearchResults_REQ;

typedef struct
{
    uint32  EndPointFlag        : 1;                    // Indicates whether the field EndPoint is valid
    uint32  MaxMatchesFlag      : 1;                    // Indicates whether the field MaxMatches is valid
    uint32  Reserved            : 30;
    
    time_t  StartPoint;                                 // required, The point of time where the search will start
    time_t  EndPoint;                                   // optional, The point of time where the search will stop. This can be a time before the StartPoint, in which case the search is performed backwards in time

    onvif_SearchScope   Scope;                          // required, 
    onvif_EventFilter   SearchFilter;                   // required, 

    BOOL    IncludeStartState;                          // required, Setting IncludeStartState to true means that the server should return virtual events representing the start state for any recording included in the scope. Start state events are limited to the topics defined in the SearchFilter that have the IsProperty flag set to true
    int     MaxMatches;                                 // optional, The search will be completed after this many matches. If not specified, the search will continue until reaching the endpoint or until the session expires
    int     KeepAliveTime;                              // required, The time the search session will be kept alive after responding to this and subsequent requests. A device shall support at least values up to ten seconds
} tse_FindEvents_REQ;

typedef struct
{
    uint32  MinResultsFlag  : 1;                        // Indicates whether the field MinResults is valid
    uint32  MaxResultsFlag  : 1;                        // Indicates whether the field MaxResults is valid
    uint32  WaitTimeFlag    : 1;                        // Indicates whether the field WaitTime is valid
    uint32  Reserved        : 29;
    
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to get results from
    int     MinResults;                                 // optional, The minimum number of results to return in one response
    int     MaxResults;                                 // optional, The maximum number of results to return in one response
    int     WaitTime;                                   // optional, The maximum time before responding to the request, even if the MinResults parameter is not fulfilled
} tse_GetEventSearchResults_REQ;

typedef struct
{
    uint32  EndPointFlag        : 1;                    // Indicates whether the field EndPoint is valid
    uint32  MaxMatchesFlag      : 1;                    // Indicates whether the field MaxMatches is valid
    uint32  Reserved            : 30;
    
    time_t  StartPoint;                                 // required, The point of time where the search will start
    time_t  EndPoint;                                   // optional, The point of time where the search will stop. This can be a time before the StartPoint, in which case the search is performed backwards in time

    onvif_SearchScope       Scope;                      // required, 
    onvif_MetadataFilter    MetadataFilter;             // required, 

    int     MaxMatches;                                 // optional, The search will be completed after this many matches. If not specified, the search will continue until reaching the endpoint or until the session expires
    int     KeepAliveTime;                              // required, The time the search session will be kept alive after responding to this and subsequent requests. A device shall support at least values up to ten seconds
} tse_FindMetadata_REQ;

typedef struct
{
    uint32  MinResultsFlag  : 1;                        // Indicates whether the field MinResults is valid
    uint32  MaxResultsFlag  : 1;                        // Indicates whether the field MaxResults is valid
    uint32  WaitTimeFlag    : 1;                        // Indicates whether the field WaitTime is valid
    uint32  Reserved        : 29;
    
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to get results from
    int     MinResults;                                 // optional, The minimum number of results to return in one response
    int     MaxResults;                                 // optional, The maximum number of results to return in one response
    int     WaitTime;                                   // optional, The maximum time before responding to the request, even if the MinResults parameter is not fulfilled
} tse_GetMetadataSearchResults_REQ;

typedef struct
{
    uint32  EndPointFlag        : 1;                    // Indicates whether the field EndPoint is valid
    uint32  MaxMatchesFlag      : 1;                    // Indicates whether the field MaxMatches is valid
    uint32  Reserved            : 30;
    
    time_t  StartPoint;                                 // required, The point of time where the search will start
    time_t  EndPoint;                                   // optional, The point of time where the search will stop. This can be a time before the StartPoint, in which case the search is performed backwards in time

    onvif_SearchScope       Scope;                      // required, 
    onvif_PTZPositionFilter SearchFilter;               // required, 

    int     MaxMatches;                                 // optional, The search will be completed after this many matches. If not specified, the search will continue until reaching the endpoint or until the session expires
    int     KeepAliveTime;                              // required, The time the search session will be kept alive after responding to this and subsequent requests. A device shall support at least values up to ten seconds
} tse_FindPTZPosition_REQ;

typedef struct
{
    uint32  MinResultsFlag  : 1;                        // Indicates whether the field MinResults is valid
    uint32  MaxResultsFlag  : 1;                        // Indicates whether the field MaxResults is valid
    uint32  WaitTimeFlag    : 1;                        // Indicates whether the field WaitTime is valid
    uint32  Reserved        : 29;
    
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to get results from
    int     MinResults;                                 // optional, The minimum number of results to return in one response
    int     MaxResults;                                 // optional, The maximum number of results to return in one response
    int     WaitTime;                                   // optional, The maximum time before responding to the request, even if the MinResults parameter is not fulfilled
} tse_GetPTZPositionSearchResults_REQ;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to get the state from
} tse_GetSearchState_REQ;

typedef struct
{
    char    SearchToken[ONVIF_TOKEN_LEN];               // required, The search session to end
} tse_EndSearch_REQ;

typedef struct
{
    int     dummy;
} tan_GetServiceCapabilities_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, References an existing Video Analytics configuration. The list of available tokens can be obtained
                                                        //  via the Media service GetVideoAnalyticsConfigurations method
} tan_GetSupportedRules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration

    ConfigList * Rule;                                  // required
} tan_CreateRules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration

    int     sizeRuleName;
    char    RuleName[10][ONVIF_NAME_LEN];               // required, References the specific rule to be deleted (e.g. "MyLineDetector"). 
} tan_DeleteRules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration
} tan_GetRules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration

    ConfigList * Rule;                                  // required 
} tan_ModifyRules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration

    ConfigList * AnalyticsModule;                       // required 
} tan_CreateAnalyticsModules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing Video Analytics configuration

    int     sizeAnalyticsModuleName;
    char    AnalyticsModuleName[10][ONVIF_NAME_LEN];    //required, name of the AnalyticsModule to be deleted
} tan_DeleteAnalyticsModules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration
} tan_GetAnalyticsModules_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration
    
    ConfigList * AnalyticsModule;                       // required 
} tan_ModifyAnalyticsModules_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing VideoAnalyticsConfiguration
} tan_GetSupportedAnalyticsModules_REQ;

typedef struct
{
    uint32  RuleTypeFlag    : 1;                        // Indicates whether the field RuleType is valid    
    uint32  Reserved        : 31;
    
    char    RuleType[128];                              // optional, Reference to an SupportedRule Type returned from GetSupportedRules
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, 
} tan_GetRuleOptions_REQ;

typedef struct
{
    char    Type[128];                                  // optional, Reference to an SupportedAnalyticsModule Type returned from GetSupportedAnalyticsModules
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Reference to an existing AnalyticsConfiguration
} tan_GetAnalyticsModuleOptions_REQ;

typedef struct
{
    char    Type[128];                                  // optional, reference to an AnalyticsModule Type returned from GetSupportedAnalyticsModules
} tan_GetSupportedMetadata_REQ;

// onvif media service 2 interfaces

#define TR2_MAX_TYPE            10
#define TR2_MAX_CONFIGURATION   10

typedef struct 
{
    int     dummy;
} tr2_GetServiceCapabilities_REQ;

typedef struct 
{
    uint32  ConfigurationTokenFlag  : 1;
    uint32  ProfileTokenFlag        : 1;
    uint32  Reserved                : 30;
    
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Token of the requested configuration
    char    ProfileToken[ONVIF_TOKEN_LEN];              // optional, Contains the token of an existing media profile the configurations shall be compatible with
} tr2_GetConfiguration;

typedef struct 
{
    char    Name[ONVIF_NAME_LEN];                       // required, friendly name of the profile to be created
    
    int     sizeConfiguration;
    onvif_ConfigurationRef  Configuration[TR2_MAX_CONFIGURATION];   // optional, Optional set of configurations to be assigned to the profile
} tr2_CreateProfile_REQ;

typedef struct 
{
    uint32  TokenFlag   : 1;
    uint32  Reserved    : 31;
    
    char    Token[ONVIF_TOKEN_LEN];                     // optional, Optional token of the requested profile

    int     sizeType;
    char    Type[TR2_MAX_TYPE][32];                     // optional, The types shall be provided as defined by tr2:ConfigurationEnumeration
                                                        //  All, VideoSource, VideoEncoder, AudioSource, AudioEncoder, AudioOutput,
                                                        //  AudioDecoder, Metadata, Analytics, PTZ
} tr2_GetProfiles_REQ;

typedef struct 
{
    uint32  NameFlag    : 1;
    uint32  Reserved    : 31;
    
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Reference to the profile where the configuration should be added
    char    Name[ONVIF_NAME_LEN];                       // optional, Optional item. If present updates the Name property of the profile

    int     sizeConfiguration;    
    onvif_ConfigurationRef  Configuration[TR2_MAX_CONFIGURATION];   // optional, List of configurations to be added
} tr2_AddConfiguration_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, This element contains a  reference to the media profile

    int     sizeConfiguration;
    onvif_ConfigurationRef  Configuration[TR2_MAX_CONFIGURATION];    // required, List of configurations to be removed
} tr2_RemoveConfiguration_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, This element contains a  reference to the profile that should be deleted
} tr2_DeleteProfile_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetVideoEncoderConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetVideoSourceConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioEncoderConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioSourceConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetMetadataConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioOutputConfigurations_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioDecoderConfigurations_REQ;

typedef struct
{
    onvif_VideoEncoder2Configuration    Configuration;  // Contains the modified video encoder configuration. The configuration shall exist in the device
} tr2_SetVideoEncoderConfiguration_REQ;

typedef struct 
{
    onvif_VideoSourceConfiguration      Configuration;  // required, Contains the modified video source configuration. The configuration shall exist in the device
} tr2_SetVideoSourceConfiguration_REQ;

typedef struct 
{
    onvif_AudioEncoder2Configuration    Configuration;  // required, Contains the modified audio encoder configuration. The configuration shall exist in the device
} tr2_SetAudioEncoderConfiguration_REQ;

typedef struct 
{
    onvif_AudioSourceConfiguration      Configuration;  // required, Contains the modified audio source configuration. The configuration shall exist in the device
} tr2_SetAudioSourceConfiguration_REQ;

typedef struct 
{
    onvif_MetadataConfiguration Configuration;          // required, Contains the modified metadata configuration. The configuration shall exist in the device
} tr2_SetMetadataConfiguration_REQ;

typedef struct 
{
    onvif_AudioOutputConfiguration  Configuration;      // required, Contains the modified audio output configuration. The configuration shall exist in the device
} tr2_SetAudioOutputConfiguration_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetVideoSourceConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetVideoEncoderConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioSourceConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioEncoderConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetMetadataConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioOutputConfigurationOptions_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;
} tr2_GetAudioDecoderConfigurationOptions_REQ;

typedef struct
{
    onvif_AudioDecoderConfiguration     Configuration;
} tr2_SetAudioDecoderConfiguration_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, 
} tr2_GetSnapshotUri_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, 
} tr2_StartMulticastStreaming_REQ;

typedef struct
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, 
} tr2_StopMulticastStreaming_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required,  
} tr2_GetVideoSourceModes_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, 
    char    VideoSourceModeToken[ONVIF_TOKEN_LEN];      // required, 
} tr2_SetVideoSourceMode_REQ;

typedef struct
{
    onvif_OSDConfiguration  OSD;                        // required, 
} tr2_CreateOSD_REQ;

typedef struct
{
    char    OSDToken[ONVIF_TOKEN_LEN];                  // required, 
} tr2_DeleteOSD_REQ;

typedef struct
{
    uint32  OSDTokenFlag            : 1;
    uint32  ConfigurationTokenFlag  : 1;
    uint32  Reserved                : 30;
    
    char    OSDToken[ONVIF_TOKEN_LEN];                  // optional, 
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, 
} tr2_GetOSDs_REQ;

typedef struct
{
    onvif_OSDConfiguration  OSD;                        // required
} tr2_SetOSD_REQ;

typedef struct
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Video Source Configuration Token that specifies an existing video source configuration that the options shall be compatible with
} tr2_GetOSDOptions_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Token of the video source configuration
} tr2_GetVideoEncoderInstances_REQ;

typedef struct 
{
    char    Protocol[32];                               // required, Defines the network protocol
                                                        //  RtspUnicast -- RTSP streaming RTP as UDP Unicast
                                                        //  RtspMulticast -- RTSP streaming RTP as UDP Multicast
                                                        //  RTSP -- RTSP streaming RTP over TCP
                                                        //  RtspOverHttp -- Tunneling both the RTSP control channel and the RTP stream over HTTP or HTTPS
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, The ProfileToken element indicates the media profile 
                                                        // to use and will define the configuration of the content of the stream
} tr2_GetStreamUri_REQ;

typedef struct 
{
    char    ProfileToken[ONVIF_TOKEN_LEN];              // required, Contains a Profile reference for which a Synchronization Point is requested
} tr2_SetSynchronizationPoint_REQ;

typedef struct 
{
    tr2_GetConfiguration    GetConfiguration;           // optional, 
} tr2_GetAnalyticsConfigurations_REQ;

typedef struct
{
    onvif_Mask  Mask;                                   // required, Contain the initial mask configuration for create
} tr2_CreateMask_REQ;

typedef struct
{
    uint32  TokenFlag               : 1;                // Indicates whether the field Token is valid
    uint32  ConfigurationTokenFlag  : 1;                // Indicates whether the field ConfigurationToken is valid
    uint32  Reserved                : 30;
    
    char    Token[ONVIF_TOKEN_LEN];                     // optional, Optional mask token of an existing mask
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // optional, Optional token of a Video Source Configuration
} tr2_GetMasks_REQ;

typedef struct
{
    onvif_Mask  Mask;                                   // required, Mask to be updated
} tr2_SetMask_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, This element contains a reference to the Mask configuration that should be deleted
} tr2_DeleteMask_REQ;

typedef struct 
{
    char    ConfigurationToken[ONVIF_TOKEN_LEN];        // required, Video Source Configuration Token that specifies an existing video source configuration that the options shall be compatible with
} tr2_GetMaskOptions_REQ;

// end of onvif media 2 interfaces

typedef struct
{
    int     dummy;
} tac_GetServiceCapabilities_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher than what the device supports, the number of items is determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified, entries shall start from the beginning of the dataset
} tac_GetAccessPointInfoList_REQ;

typedef struct
{
    char    token[ACCESS_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];  // Tokens of AccessPointInfo items to get    
} tac_GetAccessPointInfo_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher than what the device supports, the number of items is determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified, entries shall start from the beginning of the dataset
} tac_GetAccessPointList_REQ;

typedef struct
{
    char    token[ACCESS_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];  // Tokens of AccessPoint items to get
} tac_GetAccessPoints_REQ;

typedef struct
{
    uint32  AuthenticationProfileTokenFlag  : 1;
    uint32  Reserved                        : 31;
    
    onvif_AccessPointInfo   AccessPoint;                // required
    
    char    AuthenticationProfileToken[ONVIF_TOKEN_LEN];// optional
} tac_CreateAccessPoint_REQ;

typedef struct
{
    uint32  AuthenticationProfileTokenFlag  : 1;
    uint32  Reserved                        : 31;
    
    onvif_AccessPointInfo   AccessPoint;                // required
    
    char    AuthenticationProfileToken[ONVIF_TOKEN_LEN];// optional
} tac_SetAccessPoint_REQ;

typedef struct
{
    uint32  AuthenticationProfileTokenFlag  : 1;
    uint32  Reserved                        : 31;
    
    onvif_AccessPointInfo   AccessPoint;                // required
    
    char    AuthenticationProfileToken[ONVIF_TOKEN_LEN];// optional
} tac_ModifyAccessPoint_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of AccessPoint item to delete
} tac_DeleteAccessPoint_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, or higher than what the device supports, the number of items shall be determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified, entries shall start from the beginning of the dataset
} tac_GetAreaInfoList_REQ;

typedef struct
{
    char    token[ACCESS_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];  // Tokens of DoorInfo items to get 
} tac_GetAreaInfo_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher than what the device supports, the number of items is determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified, entries shall start from the beginning of the dataset
} tac_GetAreaList_REQ;

typedef struct
{
    char    token[ACCESS_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];  // Tokens of Area items to get
} tac_GetAreas_REQ;

typedef struct
{
    onvif_AreaInfo   Area;                              // required
} tac_CreateArea_REQ;

typedef struct
{
    onvif_AreaInfo   Area;                              // required
} tac_SetArea_REQ;

typedef struct
{
    onvif_AreaInfo   Area;                              // required
} tac_ModifyArea_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of Area item to delete
} tac_DeleteArea_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of AccessPoint instance to get AccessPointState for
} tac_GetAccessPointState_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the AccessPoint instance to enable
} tac_EnableAccessPoint_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the AccessPoint instance to disable
} tac_DisableAccessPoint_REQ;

typedef struct
{
    int     dummy;
} tdc_GetServiceCapabilities_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, 
                                                        //  or higher than what the device supports, 
                                                        //  the number of items shall be determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified, 
                                                        //  entries shall start from the beginning of the dataset
} tdc_GetDoorInfoList_REQ;

typedef struct
{
    char    token[DOOR_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];    // Tokens of DoorInfo items to get 
} tdc_GetDoorInfo_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to get the state for
} tdc_GetDoorState_REQ;

typedef struct
{
    uint32  UseExtendedTimeFlag : 1;
    uint32  AccessTimeFlag      : 1;
    uint32  OpenTooLongTimeFlag : 1;
    uint32  PreAlarmTimeFlag    : 1;
    uint32  Reserved            : 28;
    
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
    BOOL    UseExtendedTime;                            // optional, Indicates that the configured extended time should be used
    int     AccessTime;                                 // optional, overrides AccessTime if specified
    int     OpenTooLongTime;                            // optional, overrides OpenTooLongTime if specified (DOTL)
    int     PreAlarmTime;                               // optional, overrides PreAlarmTime if specified
} tdc_AccessDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_LockDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_UnlockDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_DoubleLockDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_BlockDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_LockDownDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_LockDownReleaseDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_LockOpenDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of the Door instance to control
} tdc_LockOpenReleaseDoor_REQ;

typedef struct
{
    char    token[DOOR_CTRL_MAX_LIMIT][ONVIF_TOKEN_LEN];    // Tokens of Door items to get 
} tdc_GetDoors_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;
    uint32  StartReferenceFlag  : 1;
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, aximum number of entries to return. If not specified, less than one
                                                        //  or higher than what the device supports, 
                                                        //  the number of items is determined by the device
    char    StartReference[256];                        // optional, Start returning entries from this start reference. If not specified,
                                                        //  entries shall start from the beginning of the dataset
} tdc_GetDoorList_REQ;

typedef struct
{
    onvif_Door  Door;                                   // required, Door item to create
} tdc_CreateDoor_REQ;

typedef struct
{
    onvif_Door  Door;                                   // required, The Door item to create or modify
} tdc_SetDoor_REQ;

typedef struct
{
    onvif_Door  Door;                                   // required, The details of the door
} tdc_ModifyDoor_REQ;

typedef struct
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The Token of the door to delete
} tdc_DeleteDoor_REQ;

typedef struct
{
    int     dummy;
} tth_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} tth_GetConfigurations_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // requied, Reference token to the VideoSource for which the Thermal Settings are requested
} tth_GetConfiguration_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // requied, Reference token to the VideoSource for which the Thermal Settings are configured

    onvif_ThermalConfiguration Configuration;           // requied, Thermal Settings to be configured
} tth_SetConfiguration_REQ;

typedef struct
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // requied, Reference token to the VideoSource for which the Thermal Configuration Options are requested
} tth_GetConfigurationOptions_REQ;

typedef struct 
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference token to the VideoSource for which the Radiometry Configuration is requested
} tth_GetRadiometryConfiguration_REQ;

typedef struct 
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference token to the VideoSource for which the Radiometry settings are configured

    onvif_RadiometryConfiguration   Configuration;      // required, Radiometry settings to be configured
} tth_SetRadiometryConfiguration_REQ;

typedef struct 
{
    char    VideoSourceToken[ONVIF_TOKEN_LEN];          // required, Reference token to the VideoSource for which the Thermal Radiometry Options are requested
} tth_GetRadiometryConfigurationOptions_REQ;

typedef struct 
{
    int     dummy;
} tcr_GetServiceCapabilities_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[CREDENTIAL_MAX_LIMIT][ONVIF_TOKEN_LEN];   // required, Tokens of CredentialInfo items to get
} tcr_GetCredentialInfo_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher 
                                                        //  than what the device supports, the number of items is determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference. If not specified,
                                                        //  entries shall start from the beginning of the dataset
} tcr_GetCredentialInfoList_REQ;
 
typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token> 
    char    Token[CREDENTIAL_MAX_LIMIT][ONVIF_TOKEN_LEN];   // required, Token of Credentials to get
} tcr_GetCredentials_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher 
                                                        //  than what the device supports, the number of items is determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference. If not specified,
                                                        //  entries shall start from the beginning of the dataset
} tcr_GetCredentialList_REQ;

typedef struct 
{
    onvif_Credential        Credential;                 // required, The credential to create
    onvif_CredentialState   State;                      // required, The state of the credential
} tcr_CreateCredential_REQ;

typedef struct 
{
    onvif_Credential    Credential;                     // required, Details of the credential
} tcr_ModifyCredential_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the credential to delete
} tcr_DeleteCredential_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of Credential
} tcr_GetCredentialState_REQ;

typedef struct 
{
    uint32  ReasonFlag  : 1;                            // Indicates whether the field ReasonFlag is valid
    uint32  Reserved    : 31;
    
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the credential
    char    Reason[200];                                // optional, Reason for enabling the credential
} tcr_EnableCredential_REQ;

typedef struct 
{
    uint32  ReasonFlag  : 1;                            // Indicates whether the field ReasonFlag is valid
    uint32  Reserved    : 31;
    
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the credential
    char    Reason[200];                                // optional, Reason for disabling the credential
} tcr_DisableCredential_REQ;

typedef struct 
{
    onvif_CredentialData    CredentialData;             // required, Details of the credential
} tcr_SetCredential_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
} tcr_ResetAntipassbackViolation_REQ;

typedef struct 
{
    char    CredentialIdentifierTypeName[100];          // required, Name of the credential identifier type
} tcr_GetSupportedFormatTypes_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
} tcr_GetCredentialIdentifiers_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
    
    onvif_CredentialIdentifier CredentialIdentifier;    // required, Identifier of the credential
} tcr_SetCredentialIdentifier_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
    char    CredentialIdentifierTypeName[100];          // required, Identifier type name of a credential
} tcr_DeleteCredentialIdentifier_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
} tcr_GetCredentialAccessProfiles_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
    int     sizeCredentialAccessProfile;                // sequence of elements <CredentialAccessProfile>

    onvif_CredentialAccessProfile CredentialAccessProfile[CREDENTIAL_MAX_LIMIT];    // required, Access Profiles of the credential
} tcr_SetCredentialAccessProfiles_REQ;

typedef struct 
{
    char    CredentialToken[ONVIF_TOKEN_LEN];           // required, Token of the Credential
    int     sizeAccessProfileToken;                     // sequence of elements <CredentialAccessProfile>

    char    AccessProfileToken[CREDENTIAL_MAX_LIMIT][ONVIF_TOKEN_LEN];    // required, Tokens of Access Profiles
} tcr_DeleteCredentialAccessProfiles_REQ;

typedef struct 
{
    int     dummy;
} tar_GetServiceCapabilities_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[ACCESSRULES_MAX_LIMIT][ONVIF_TOKEN_LEN];  // required, Tokens of CredentialInfo items to get
} tar_GetAccessProfileInfo_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher 
                                                        //  than what the device supports, the number of items is determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference. If not specified,
                                                        //  entries shall start from the beginning of the dataset
} tar_GetAccessProfileInfoList_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[ACCESSRULES_MAX_LIMIT][ONVIF_TOKEN_LEN];  // required, Tokens of CredentialInfo items to get
} tar_GetAccessProfiles_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one or higher 
                                                        //  than what the device supports, the number of items is determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference. If not specified,
                                                        //  entries shall start from the beginning of the dataset
} tar_GetAccessProfileList_REQ;

typedef struct 
{
    onvif_AccessProfile AccessProfile;                  // required, The AccessProfile to create
} tar_CreateAccessProfile_REQ;

typedef struct 
{
    onvif_AccessProfile AccessProfile;                  // required, The details of Access Profile
} tar_ModifyAccessProfile_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the access profile to delete
} tar_DeleteAccessProfile_REQ;

typedef struct
{
    int     dummy;
} tsc_GetServiceCapabilities_REQ;

typedef struct
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[SCHEDULE_MAX_LIMIT][ONVIF_TOKEN_LEN]; // required, Tokens of ScheduleInfo items to get
} tsc_GetScheduleInfo_REQ;

typedef struct
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one
                                                        //  or higher than what the device supports, the number of items is
                                                        //  determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference.
                                                        //  If not specified, entries shall start from the beginning of the dataset
} tsc_GetScheduleInfoList_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[SCHEDULE_MAX_LIMIT][ONVIF_TOKEN_LEN]; // required, Tokens of Schedule items to get
} tsc_GetSchedules_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one
                                                        //  or higher than what the device supports, the number of items is
                                                        //  determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference.
                                                        //  If not specified, entries shall start from the beginning of the dataset
} tsc_GetScheduleList_REQ;

typedef struct 
{
    onvif_Schedule Schedule;                            // required, The Schedule to create
} tsc_CreateSchedule_REQ;

typedef struct 
{
    onvif_Schedule Schedule;                            // required, The Schedule to modify/update
} tsc_ModifySchedule_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the schedule to delete
} tsc_DeleteSchedule_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[SCHEDULE_MAX_LIMIT][ONVIF_TOKEN_LEN]; // required, Tokens of SpecialDayGroupInfo items to get
} tsc_GetSpecialDayGroupInfo_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one
                                                        //  or higher than what the device supports, the number of items is
                                                        //  determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference.
                                                        //  If not specified, entries shall start from the beginning of the dataset
} tsc_GetSpecialDayGroupInfoList_REQ;

typedef struct 
{
    int     sizeToken;                                  // sequence of elements <Token>
    char    Token[SCHEDULE_MAX_LIMIT][ONVIF_TOKEN_LEN]; // required, Tokens of the SpecialDayGroup items to get
} tsc_GetSpecialDayGroups_REQ;

typedef struct 
{
    uint32  LimitFlag           : 1;                    // Indicates whether the field Limit is valid
    uint32  StartReferenceFlag  : 1;                    // Indicates whether the field StartReference is valid
    uint32  Reserved            : 30;
    
    int     Limit;                                      // optional, Maximum number of entries to return. If not specified, less than one
                                                        //  or higher than what the device supports, the number of items is
                                                        //  determined by the device
    char    StartReference[ONVIF_TOKEN_LEN];            // optional, Start returning entries from this start reference.
                                                        //  If not specified, entries shall start from the beginning of the dataset
} tsc_GetSpecialDayGroupList_REQ;

typedef struct 
{
    onvif_SpecialDayGroup SpecialDayGroup;              // required, The special day group to create
} tsc_CreateSpecialDayGroup_REQ;

typedef struct 
{
    onvif_SpecialDayGroup SpecialDayGroup;              // required, The special day group to modify/update
} tsc_ModifySpecialDayGroup_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, The token of the special day group item to delete
} tsc_DeleteSpecialDayGroup_REQ;

typedef struct 
{
    char    Token[ONVIF_TOKEN_LEN];                     // required, Token of schedule instance to get ScheduleState
} tsc_GetScheduleState_REQ;

typedef struct
{
    int     dummy;
} trv_GetServiceCapabilities_REQ;

typedef struct
{
    int     dummy;
} trv_GetReceivers_REQ;

typedef struct
{
    char    ReceiverToken[ONVIF_TOKEN_LEN];             // required, The token of the receiver to be retrieved
} trv_GetReceiver_REQ;

typedef struct 
{
    onvif_ReceiverConfiguration Configuration;          // required, The initial configuration for the new receiver
} trv_CreateReceiver_REQ;

typedef struct 
{
    char    ReceiverToken[ONVIF_TOKEN_LEN];             // required, The token of the receiver to be deleted
} trv_DeleteReceiver_REQ;

typedef struct 
{
    char    ReceiverToken[ONVIF_TOKEN_LEN];             // required, The token of the receiver to be configured
    
    onvif_ReceiverConfiguration Configuration;          // required, The new configuration for the receiver
} trv_ConfigureReceiver_REQ;

typedef struct 
{
    char    ReceiverToken[ONVIF_TOKEN_LEN];             // required, The token of the receiver to be changed
    
    onvif_ReceiverMode Mode;                            // required, The new receiver mode
} trv_SetReceiverMode_REQ;

typedef struct 
{
    char    ReceiverToken[ONVIF_TOKEN_LEN];             // required, The token of the receiver to be queried
} trv_GetReceiverState_REQ;

typedef struct 
{
    int     dummy;
} tpv_GetServiceCapabilities_REQ;

typedef struct 
{
    uint32  TimeoutFlag : 1;                            // Indicates whether the field Timeout is valid
    uint32  Reserved    : 31;
    
    char    VideoSource[100];                           // required, The video source associated with the provisioning.
    
    onvif_PanDirection Direction;                       // required, The direction for PanMove to move the device, "left" or "right"
    
    int     Timeout;                                    // optional, Operation timeout, if less than default timeout, unit is second
} tpv_PanMove_REQ;

typedef struct 
{
    uint32  TimeoutFlag : 1;                            // Indicates whether the field Timeout is valid
    uint32  Reserved    : 31;
    
    char    VideoSource[100];                           // required, The video source associated with the provisioning
    
    onvif_TiltDirection Direction;                      // required, "up" or "down"

    int     Timeout;                                    // optional, Operation timeout, if less than default timeout, unit is second
} tpv_TiltMove_REQ;

typedef struct 
{
    uint32  TimeoutFlag : 1;                            // Indicates whether the field Timeout is valid
    uint32  Reserved    : 31;
    
    char    VideoSource[100];                           // required, The video source associated with the provisioning
    
    onvif_ZoomDirection Direction;                      // required, "wide" or "telephoto"
    
    int     Timeout;                                    // optional, Operation timeout, if less than default timeout, unit is second
} tpv_ZoomMove_REQ;

typedef struct 
{
    uint32  TimeoutFlag : 1;                            // Indicates whether the field Timeout is valid
    uint32  Reserved    : 31;
    
    char    VideoSource[100];                           // required, The video source associated with the provisioning
    
    onvif_RollDirection Direction;                      // required, "clockwise", "counterclockwise", or "auto"
    
    int     Timeout;                                    // optional, Operation timeout, if less than default timeout, unit is second
} tpv_RollMove_REQ;

typedef struct 
{
    uint32  TimeoutFlag : 1;                            // Indicates whether the field Timeout is valid
    uint32  Reserved    : 31;
    
    char    VideoSource[100];                           // required, The video source associated with the provisioning
    
    onvif_FocusDirection Direction;                     // required, "near", "far", or "auto"
    
    int     Timeout;                                    // optional, Operation timeout, if less than default timeout, unit is second
} tpv_FocusMove_REQ;

typedef struct 
{
    char    VideoSource[100];                           // required, The video source associated with the provisioning
} tpv_Stop_REQ;

typedef struct 
{
    char    VideoSource[100];                           // required, The video source associated with the provisioning
} tpv_GetUsage_REQ;


#endif // end of ONVIF_REQ_H



