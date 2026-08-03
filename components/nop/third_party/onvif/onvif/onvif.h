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

#ifndef __H_ONVIF_H__
#define __H_ONVIF_H__

#include "sys_inc.h"
#include "http.h"
#include "onvif_cm.h"
#include "onvif_act.h"


/***************************************************************************************/

// device type
#define ODT_UNKNOWN         0                   // Unknow device type
#define ODT_NVT             1                   // ONVIF Network Video Transmitter
#define ODT_NVD             2                   // ONVIF Network Video Display
#define ODT_NVS             3                   // ONVIF Network video Storage
#define ODT_NVA             4                   // ONVIF Network video Analytics

// device flag
#define FLAG_MANUAL         (1 << 0)            // manual added device, other auto discovery devices


/***************************************************************************************/
typedef struct
{
    int         type;                           // device type
    char        EndpointReference[100];         // endpoint reference
    int         MetadataVersion;                // metadata version

    uint32      sizeScopes;                     // sopes numbers
    onvif_Scope scopes[MAX_SCOPE_NUMS];         // scopes

    onvif_XAddr XAddr;                          // xaddr, include port host, url
} DEVICE_BINFO;

/***************************************************************************************/

// video source list
typedef struct _VideoSourceList
{
    struct _VideoSourceList * next;
    
    onvif_VideoSource VideoSource; 
} VideoSourceList;

// video source mode list
typedef struct _VideoSourceModeList
{
    struct _VideoSourceModeList * next;

    onvif_VideoSourceMode   VideoSourceMode;
} VideoSourceModeList;

// video source configuration list
typedef struct _VideoSourceConfigurationList
{
    struct _VideoSourceConfigurationList * next;

    onvif_VideoSourceConfiguration Configuration;
} VideoSourceConfigurationList;

// video encoder configuration list
typedef struct _VideoEncoderConfigurationList
{    
    struct _VideoEncoderConfigurationList * next;

    onvif_VideoEncoderConfiguration Configuration;
} VideoEncoderConfigurationList;

// audio source list
typedef struct _AudioSourceList
{    
    struct _AudioSourceList * next;
    
    onvif_AudioSource AudioSource;
} AudioSourceList;

// audio source configuration list
typedef struct _AudioSourceConfigurationList
{    
    struct _AudioSourceConfigurationList * next;
    
    onvif_AudioSourceConfiguration  Configuration;
} AudioSourceConfigurationList;

// audio encoder configuration list
typedef struct _AudioEncoderConfigurationList
{
    struct _AudioEncoderConfigurationList * next;
    
    onvif_AudioEncoderConfiguration Configuration;
} AudioEncoderConfigurationList;

typedef struct _MetadataConfigurationList
{
    struct _MetadataConfigurationList * next;
    
    onvif_MetadataConfiguration Configuration;
} MetadataConfigurationList;

// ptz preset list
typedef struct _PTZPresetList
{
    struct _PTZPresetList * next;
    
    onvif_PTZPreset PTZPreset;
} PTZPresetList;

// ptz configuration list
typedef struct _PTZConfigurationList
{
    struct _PTZConfigurationList * next;
    
    onvif_PTZConfiguration  Configuration;
} PTZConfigurationList;

// ptz node list
typedef struct _PTZNodeList
{
    struct _PTZNodeList * next;

    onvif_PTZNode   PTZNode;
} PTZNodeList;

// preset tour list
typedef struct _PresetTourList
{
    struct _PresetTourList * next;

    onvif_PresetTour    PresetTour;
} PresetTourList;

// video analytics configuration list
typedef struct _VideoAnalyticsConfigurationList
{
    struct _VideoAnalyticsConfigurationList * next;

    ConfigList      * rules;                    // video analytics rule configuration
    ConfigList      * modules;                  // video analytics module configuration

    onvif_SupportedRules  SupportedRules;       // supported rules
    
    onvif_VideoAnalyticsConfiguration   Configuration;
} VideoAnalyticsConfigurationList;

// network interface list
typedef struct _NetworkInterfaceList
{
    struct _NetworkInterfaceList * next;
    
    onvif_NetworkInterface  NetworkInterface;
} NetworkInterfaceList;

typedef struct 
{
    onvif_NetworkProtocol           NetworkProtocol;
    onvif_DNSInformation            DNSInformation;
    onvif_NTPInformation            NTPInformation;
    onvif_HostnameInformation       HostnameInformation;
    onvif_NetworkGateway            NetworkGateway;
    onvif_DiscoveryMode             DiscoveryMode;
    onvif_NetworkZeroConfiguration  ZeroConfiguration;

    NetworkInterfaceList          * interfaces;
} ONVIF_NET;

// user list
typedef struct _UserList
{
    struct _UserList * next;

    onvif_User  User;
} UserList;

// osd configuration list
typedef struct _OSDConfigurationList
{
    struct _OSDConfigurationList * next;
    
    onvif_OSDConfiguration OSD;
} OSDConfigurationList;

typedef struct _RecordingList
{
    struct _RecordingList * next;

    onvif_Recording Recording;
} RecordingList;

typedef struct _RecordingJobList
{
    struct _RecordingJobList * next;

    onvif_RecordingJob  RecordingJob;
} RecordingJobList;

typedef struct _NotificationMessageList
{
    struct _NotificationMessageList * next;

    onvif_NotificationMessage   NotificationMessage;
} NotificationMessageList;

typedef struct
{
    BOOL        subscribe;                              // event subscribed flag
    BOOL        pullpoint;                              // create pull point flag
    
    char        reference_addr[256];                    // event comsumer address
    char        producter_addr[256];                    // event producter address
    char        producter_parameters[512];              // event producter reference parameters

    int         init_term_time;                         // termination time, unit is second
    time_t      renew_time;                             // the previous renew timestamp

    uint32      notify_nums;                            // event notify numbers
    
    NotificationMessageList  * notify;                  // event notify messages
} ONVIF_EVENT;

typedef struct _ONVIF_PROFILE
{
    struct _ONVIF_PROFILE * next;

    VideoSourceConfigurationList    * v_src_cfg;        // video source configuration
    VideoEncoderConfigurationList   * v_enc_cfg;        // video encoder configuration
    AudioSourceConfigurationList    * a_src_cfg;        // audio source configuration
    AudioEncoderConfigurationList   * a_enc_cfg;        // audio encoder configuration
    PTZConfigurationList            * ptz_cfg;          // ptz configuration
    VideoAnalyticsConfigurationList * va_cfg;           // video analytics configuration
    MetadataConfigurationList       * metadata_cfg;     // metadata configuration

    char        name[ONVIF_NAME_LEN];                   // profile name
    char        token[ONVIF_TOKEN_LEN];                 // profile token
    char        stream_uri[ONVIF_URI_LEN];              // rtsp stream address
    BOOL        fixed;                                  // fixed profile flag
} ONVIF_PROFILE;

typedef enum
{
    TIME_TYPE_INVALID = 0,
    TIME_TYPE_LOCAL,
    TIME_TYPE_UTC
} DEV_TIME_TYPE;

typedef struct
{
    DEVICE_BINFO    binfo;                              // device basic information

    DEV_TIME_TYPE   timeType;                           // the device datatime type
    onvif_DateTime  devTime;                            // the device datatime
    time_t          fetchTime;                          // fetch time

    // request
    char            username[32];                       // login user name, set by user
    char            password[32];                       // login password, set by user
    int             timeout;                            // request timeout, uinit is millisecond

    BOOL            needAuth;                           // whether need auth, If TRUE, it indicates that the ws-username-token authentication method is used for authentication
    BOOL            authFailed;                         // when login auth failed, set by onvif stack
    ONVIF_RET       errCode;                            // error code, set by onvif stack
    onvif_Fault     fault;                              // the onvif fault, set by onvif stack, valid when errCode is ONVIF_ERR_HttpResponseError, or please skip it
    int             retryCount;                         // retry count
    
    ONVIF_PROFILE * curProfile;                         // current profile pointer, the default pointer the first profile, user can set it (for media service)

    /********************************************************/
    
    VideoSourceList                 * v_src;            // the list of video source
    AudioSourceList                 * a_src;            // the list of audio source
    ONVIF_PROFILE                   * profiles;         // the list of profile (for media service 1)
    MediaProfileList                * media_profiles;   // the list of media profile (for media service 2)
    VideoSourceConfigurationList    * v_src_cfg;        // the list of video source configuration
    AudioSourceConfigurationList    * a_src_cfg;        // the list of audio source configuration
    VideoEncoderConfigurationList   * v_enc_cfg;        // the list of video encoder configuration
    AudioEncoderConfigurationList   * a_enc_cfg;        // the list of audio encoder configuration
    VideoEncoder2ConfigurationList  * v_enc_cfg2;       // the list of video encoder configuration (for media service 2)
    AudioEncoder2ConfigurationList  * a_enc_cfg2;       // the list of audio encoder configuration (for media service 2)
    PTZNodeList                     * ptz_node;         // the list of ptz node
    PTZConfigurationList            * ptz_cfg;          // the list of ptz configuration
    
    /********************************************************/
    ONVIF_EVENT                       events;           // event information
    onvif_DeviceInformation           DeviceInformation;// device information
    onvif_Capabilities                Capabilities;     // device capabilities
} ONVIF_DEVICE;


#ifdef __cplusplus
extern "C" {
#endif

HT_API void                                      onvif_free_device(ONVIF_DEVICE * p_dev);

HT_API UserList *                                onvif_add_User(UserList ** p_head);
HT_API void                                      onvif_free_Users(UserList ** p_head);

HT_API LocationEntityList *                      onvif_add_LocationEntity(LocationEntityList ** p_head);
HT_API void                                      onvif_free_LocationEntitis(LocationEntityList ** p_head);

HT_API StorageConfigurationList *                onvif_add_StorageConfiguration(StorageConfigurationList ** p_head);
HT_API void                                      onvif_free_StorageConfigurations(StorageConfigurationList ** p_head);

HT_API ONVIF_PROFILE *                           onvif_add_profile(ONVIF_PROFILE ** p_head);
HT_API ONVIF_PROFILE *                           onvif_find_profile(ONVIF_PROFILE * p_head, const char * token);
HT_API void                                      onvif_free_profile(ONVIF_PROFILE * p_profile);
HT_API void                                      onvif_free_profiles(ONVIF_PROFILE ** p_head);

HT_API MediaProfileList *                        onvif_add_MediaProfile(MediaProfileList ** p_head);
HT_API MediaProfileList *                        onvif_find_MediaProfile(MediaProfileList * p_head, const char * token);
HT_API void                                      onvif_free_MediaProfiles(MediaProfileList ** p_head);

HT_API VideoSourceList *                         onvif_add_VideoSource(VideoSourceList ** p_head);
HT_API VideoSourceList *                         onvif_find_VideoSource(VideoSourceList * p_head, const char * token);
HT_API void                                      onvif_free_VideoSources(VideoSourceList ** p_head);
HT_API VideoSourceList *                         onvif_get_cur_VideoSource(ONVIF_DEVICE * p_dev);

HT_API VideoSourceModeList *                     onvif_add_VideoSourceMode(VideoSourceModeList ** p_head);
HT_API void                                      onvif_free_VideoSourceModes(VideoSourceModeList ** p_head);

HT_API VideoSourceConfigurationList *            onvif_add_VideoSourceConfiguration(VideoSourceConfigurationList ** p_head);
HT_API VideoSourceConfigurationList *            onvif_find_VideoSourceConfiguration(VideoSourceConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_VideoSourceConfigurations(VideoSourceConfigurationList ** p_head);

HT_API VideoEncoder2ConfigurationList *          onvif_add_VideoEncoder2Configuration(VideoEncoder2ConfigurationList ** p_head);
HT_API VideoEncoder2ConfigurationList *          onvif_find_VideoEncoder2Configuration(VideoEncoder2ConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_VideoEncoder2Configurations(VideoEncoder2ConfigurationList ** p_head);

HT_API NetworkInterfaceList *                    onvif_add_NetworkInterface(NetworkInterfaceList ** p_head);
HT_API NetworkInterfaceList *                    onvif_find_NetworkInterface(NetworkInterfaceList * p_head, const char * token);
HT_API void                                      onvif_free_NetworkInterfaces(NetworkInterfaceList ** p_head);

HT_API Dot11AvailableNetworksList *              onvif_add_Dot11AvailableNetworks(Dot11AvailableNetworksList ** p_head);
HT_API void                                      onvif_free_Dot11AvailableNetworks(Dot11AvailableNetworksList ** p_head);

HT_API OSDConfigurationList *                    onvif_add_OSDConfiguration(OSDConfigurationList ** p_head);
HT_API OSDConfigurationList *                    onvif_find_OSDConfiguration(OSDConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_OSDConfigurations(OSDConfigurationList ** p_head);

HT_API MetadataConfigurationList *               onvif_add_MetadataConfiguration(MetadataConfigurationList ** p_head);
HT_API MetadataConfigurationList *               onvif_find_MetadataConfiguration(MetadataConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_MetadataConfigurations(MetadataConfigurationList ** p_head);

HT_API VideoEncoderConfigurationList *           onvif_add_VideoEncoderConfiguration(VideoEncoderConfigurationList ** p_head);
HT_API VideoEncoderConfigurationList *           onvif_find_VideoEncoderConfiguration(VideoEncoderConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_VideoEncoderConfigurations(VideoEncoderConfigurationList ** p_head);

HT_API VideoEncoder2ConfigurationOptionsList *   onvif_add_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList ** p_head);
HT_API VideoEncoder2ConfigurationOptionsList *   onvif_find_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList * p_head, const char * encoding);
HT_API void                                      onvif_free_VideoEncoder2ConfigurationOptions(VideoEncoder2ConfigurationOptionsList ** p_head);

HT_API NotificationMessageList *                 onvif_add_NotificationMessage(NotificationMessageList ** p_head);
HT_API void                                      onvif_free_NotificationMessage(NotificationMessageList * p_message);
HT_API void                                      onvif_free_NotificationMessages(NotificationMessageList ** p_head);
HT_API int                                       onvif_get_NotificationMessages_nums(NotificationMessageList * p_head);

HT_API void                                      onvif_device_add_NotificationMessages(ONVIF_DEVICE * p_dev, NotificationMessageList * p_notify);
HT_API int                                       onvif_device_free_NotificationMessages(ONVIF_DEVICE * p_dev, int nums);

HT_API SimpleItemList *                          onvif_add_SimpleItem(SimpleItemList ** p_head);
HT_API void                                      onvif_free_SimpleItems(SimpleItemList ** p_head);
HT_API const char *                              onvif_format_SimpleItem(SimpleItemList * p_item);

HT_API ElementItemList *                         onvif_add_ElementItem(ElementItemList ** p_head);
HT_API void                                      onvif_free_ElementItems(ElementItemList ** p_head);

HT_API ImagingPresetList *                       onvif_add_ImagingPreset(ImagingPresetList ** p_head);
HT_API ImagingPresetList *                       onvif_find_ImagingPreset(ImagingPresetList * p_head, const char * token);
HT_API void                                      onvif_free_ImagingPresets(ImagingPresetList ** p_head);

HT_API AudioSourceList *                         onvif_add_AudioSource(AudioSourceList ** p_head);
HT_API AudioSourceList *                         onvif_find_AudioSource(AudioSourceList * p_head, const char * token);
HT_API void                                      onvif_free_AudioSources(AudioSourceList ** p_head);

HT_API AudioSourceConfigurationList *            onvif_add_AudioSourceConfiguration(AudioSourceConfigurationList ** p_head);
HT_API AudioSourceConfigurationList *            onvif_find_AudioSourceConfiguration(AudioSourceConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_AudioSourceConfigurations(AudioSourceConfigurationList ** p_head);

HT_API AudioEncoderConfigurationList *           onvif_add_AudioEncoderConfiguration(AudioEncoderConfigurationList ** p_head);
HT_API AudioEncoderConfigurationList *           onvif_find_AudioEncoderConfiguration(AudioEncoderConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_AudioEncoderConfigurations(AudioEncoderConfigurationList ** p_head);

HT_API AudioEncoder2ConfigurationList *          onvif_add_AudioEncoder2Configuration(AudioEncoder2ConfigurationList ** p_head);
HT_API AudioEncoder2ConfigurationList *          onvif_find_AudioEncoder2Configuration(AudioEncoder2ConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_AudioEncoder2Configurations(AudioEncoder2ConfigurationList ** p_head);

HT_API AudioEncoder2ConfigurationOptionsList *   onvif_add_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList ** p_head);
HT_API AudioEncoder2ConfigurationOptionsList *   onvif_find_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList * p_head, const char * encoding);
HT_API void                                      onvif_free_AudioEncoder2ConfigurationOptions(AudioEncoder2ConfigurationOptionsList ** p_head);

HT_API AudioDecoderConfigurationList *           onvif_add_AudioDecoderConfiguration(AudioDecoderConfigurationList ** p_head);
HT_API void                                      onvif_free_AudioDecoderConfigurations(AudioDecoderConfigurationList ** p_head);

HT_API MaskList *                                onvif_add_Mask(MaskList ** p_head);
HT_API MaskList *                                onvif_find_Mask(MaskList * p_head, const char * token);
HT_API void                                      onvif_free_Masks(MaskList ** p_head);

HT_API PTZNodeList *                             onvif_add_PTZNode(PTZNodeList ** p_head);
HT_API PTZNodeList *                             onvif_find_PTZNode(PTZNodeList * p_head, const char * token);
HT_API void                                      onvif_free_PTZNodes(PTZNodeList ** p_head);

HT_API PTZConfigurationList *                    onvif_add_PTZConfiguration(PTZConfigurationList ** p_head);
HT_API PTZConfigurationList *                    onvif_find_PTZConfiguration(PTZConfigurationList * p_head, const char * token);
HT_API void                                      onvif_free_PTZConfigurations(PTZConfigurationList ** p_head);

HT_API PTZPresetList *                           onvif_add_PTZPreset(PTZPresetList ** p_head);
HT_API void                                      onvif_free_PTZPresets(PTZPresetList ** p_head);

HT_API PTZPresetTourSpotList *                   onvif_add_PTZPresetTourSpot(PTZPresetTourSpotList ** p_head);
HT_API void                                      onvif_free_PTZPresetTourSpots(PTZPresetTourSpotList ** p_head);

HT_API PresetTourList *                          onvif_add_PresetTour(PresetTourList ** p_head);
HT_API void                                      onvif_free_PresetTours(PresetTourList ** p_head);

HT_API ConfigList *                              onvif_add_Config(ConfigList ** p_head);
HT_API void                                      onvif_free_Config(ConfigList * p_head);
HT_API void                                      onvif_free_Configs(ConfigList ** p_head);
HT_API ConfigList *                              onvif_find_Config(ConfigList * p_head, const char * name);
HT_API void                                      onvif_remove_Config(ConfigList ** p_head, ConfigList * p_remove);
HT_API ConfigList *                              onvif_get_prev_Config(ConfigList * p_head, ConfigList * p_found);

HT_API ConfigDescriptionList *                   onvif_add_ConfigDescription(ConfigDescriptionList ** p_head);
HT_API void                                      onvif_free_ConfigDescriptions(ConfigDescriptionList ** p_head);

HT_API ConfigDescription_MessagesList *          onvif_add_ConfigDescription_Message(ConfigDescription_MessagesList ** p_head);
HT_API void                                      onvif_free_ConfigDescription_Message(ConfigDescription_MessagesList * p_item);
HT_API void                                      onvif_free_ConfigDescription_Messages(ConfigDescription_MessagesList ** p_head);

HT_API ConfigOptionsList *                       onvif_add_ConfigOptions(ConfigOptionsList ** p_head);
HT_API void                                      onvif_free_ConfigOptions(ConfigOptionsList ** p_head);

HT_API SimpleItemDescriptionList *               onvif_add_SimpleItemDescription(SimpleItemDescriptionList ** p_head);
HT_API void                                      onvif_free_SimpleItemDescriptions(SimpleItemDescriptionList ** p_head);

HT_API VideoAnalyticsConfigurationList *         onvif_add_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList ** p_head);
HT_API void                                      onvif_free_VideoAnalyticsConfiguration(VideoAnalyticsConfigurationList * p_head);
HT_API void                                      onvif_free_VideoAnalyticsConfigurations(VideoAnalyticsConfigurationList ** p_head);

HT_API AnalyticsModuleConfigOptionsList *        onvif_add_AnalyticsModuleConfigOptions(AnalyticsModuleConfigOptionsList ** p_head);
HT_API void                                      onvif_free_AnalyticsModuleConfigOptions(AnalyticsModuleConfigOptionsList ** p_head);

HT_API MetadataInfoList *                        onvif_add_MetadataInfo(MetadataInfoList ** p_head);
HT_API void                                      onvif_free_MetadataInfo(MetadataInfoList ** p_head);

HT_API RecordingList *                           onvif_add_Recording(RecordingList ** p_head);
HT_API RecordingList *                           onvif_find_Recording(RecordingList * p_head, const char * token);
HT_API void                                      onvif_free_Recordings(RecordingList ** p_head);

HT_API TrackList *                               onvif_add_Track(TrackList ** p_head);
HT_API void                                      onvif_free_Tracks(TrackList ** p_head);
HT_API TrackList *                               onvif_find_Track(TrackList * p_head, const char * token);

HT_API RecordingJobList *                        onvif_add_RecordingJob(RecordingJobList ** p_head);
HT_API RecordingJobList *                        onvif_find_RecordingJob(RecordingJobList * p_head, const char * token);
HT_API void                                      onvif_free_RecordingJobs(RecordingJobList ** p_head);

HT_API TrackAttributesList *                     onvif_add_TrackAttributes(TrackAttributesList ** p_head);
HT_API void                                      onvif_free_TrackAttributes(TrackAttributesList ** p_head);

HT_API RecordingInformationList *                onvif_add_RecordingInformation(RecordingInformationList ** p_head);
HT_API void                                      onvif_free_RecordingInformations(RecordingInformationList ** p_head);

HT_API FindEventResultList *                     onvif_add_FindEventResult(FindEventResultList ** p_head);
HT_API void                                      onvif_free_FindEventResults(FindEventResultList ** p_head);

HT_API FindMetadataResultList *                  onvif_add_FindMetadataResult(FindMetadataResultList ** p_head);
HT_API void                                      onvif_free_FindMetadataResults(FindMetadataResultList ** p_head);

HT_API FindPTZPositionResultList *               onvif_add_FindPTZPositionResult(FindPTZPositionResultList ** p_head);
HT_API void                                      onvif_free_FindPTZPositionResults(FindPTZPositionResultList ** p_head);

HT_API AccessPointList *                         onvif_add_AccessPoint(AccessPointList ** p_head);
HT_API AccessPointList *                         onvif_find_AccessPoint(AccessPointList * p_head, const char * token);
HT_API void                                      onvif_free_AccessPoints(AccessPointList ** p_head);

HT_API DoorInfoList *                            onvif_add_DoorInfo(DoorInfoList ** p_head);
HT_API DoorInfoList *                            onvif_find_DoorInfo(DoorInfoList * p_head, const char * token);
HT_API void                                      onvif_free_DoorInfos(DoorInfoList ** p_head);

HT_API DoorList *                                onvif_add_Door(DoorList ** p_head);
HT_API DoorList *                                onvif_find_Door(DoorList * p_head, const char * token);
HT_API void                                      onvif_free_Doors(DoorList ** p_head);

HT_API AreaList *                                onvif_add_Area(AreaList ** p_head);
HT_API AreaList *                                onvif_find_Area(AreaList * p_head, const char * token);
HT_API void                                      onvif_free_Areas(AreaList ** p_head);

HT_API AudioOutputList *                         onvif_add_AudioOutput(AudioOutputList ** p_head);
HT_API void                                      onvif_free_AudioOutputs(AudioOutputList ** p_head);

HT_API AudioOutputConfigurationList *            onvif_add_AudioOutputConfiguration(AudioOutputConfigurationList ** p_head);
HT_API void                                      onvif_free_AudioOutputConfigurations(AudioOutputConfigurationList ** p_head);

HT_API RelayOutputOptionsList *                  onvif_add_RelayOutputOptions(RelayOutputOptionsList ** p_head);
HT_API void                                      onvif_free_RelayOutputOptions(RelayOutputOptionsList ** p_head);

HT_API RelayOutputList *                         onvif_add_RelayOutput(RelayOutputList ** p_head);
HT_API RelayOutputList *                         onvif_find_RelayOutput(RelayOutputList * p_head, const char * token);
HT_API void                                      onvif_free_RelayOutputs(RelayOutputList ** p_head);

HT_API DigitalInputList *                        onvif_add_DigitalInput(DigitalInputList ** p_head);
HT_API DigitalInputList *                        onvif_find_DigitalInput(DigitalInputList * p_head, const char * token);
HT_API void                                      onvif_free_DigitalInputs(DigitalInputList ** p_head);

HT_API SerialPortList *                          onvif_add_SerialPort(SerialPortList ** p_head);
HT_API SerialPortList *                          onvif_find_SerialPort(SerialPortList * p_head, const char * token);
HT_API void                                      onvif_free_SerialPorts(SerialPortList ** p_head);
HT_API void                                      onvif_malloc_SerialData(onvif_SerialData * p_data, int union_SerialData, int size);
HT_API void                                      onvif_free_SerialData(onvif_SerialData * p_data);

HT_API ThermalConfigurationList *                onvif_add_ThermalConfiguration(ThermalConfigurationList ** p_head);
HT_API void                                      onvif_free_ThermalConfigurations(ThermalConfigurationList ** p_head);
HT_API ColorPaletteList *                        onvif_add_ColorPalette(ColorPaletteList ** p_head);
HT_API void                                      onvif_free_ColorPalettes(ColorPaletteList ** p_head);
HT_API NUCTableList *                            onvif_add_NUCTable(NUCTableList ** p_head);
HT_API void                                      onvif_free_NUCTables(NUCTableList ** p_head);

HT_API ReceiverList *                            onvif_add_Receiver(ReceiverList ** p_head);
HT_API ReceiverList *                            onvif_find_Receiver(ReceiverList * p_head, const char * token);
HT_API void                                      onvif_free_Receivers(ReceiverList ** p_head);

#ifdef __cplusplus
}
#endif

#endif


