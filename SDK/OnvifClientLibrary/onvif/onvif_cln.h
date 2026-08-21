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

#ifndef ONVIF_CLIENT_H
#define ONVIF_CLIENT_H

#include "onvif.h"
#include "onvif_req.h"
#include "onvif_res.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief
 *  Set device host and port information.
 *  It only sets the host, port, and https fields, does not change any other fields.
 *
 * @param host device host address, it can be an IP or domain name
 * @param port onvif port
 * @param https is an HTTPS connection
 *
 **/
HT_API void onvif_initDevice(ONVIF_DEVICE * p_dev, const char * host, int port, int https);

/**
 * @brief
 *  Set authentication username and password
 *
 * @param user authentication username
 * @param pass authentication password
 *
 **/
HT_API void onvif_SetAuthInfo(ONVIF_DEVICE * p_dev, const char * user, const char * pass);

/**
 * @brief
 *  Set authentication method
 *  AuthMethod_HttpDigest - HTTP digest authentication
 *  AuthMethod_UsernameToken - Usernametoken authentication
 *
 * @param method authentication method
 *
 **/
HT_API void onvif_SetAuthMethod(ONVIF_DEVICE * p_dev, onvif_AuthMethod method);

/**
 * @brief
 *  Set request timeout time
 *
 * @param timeout request timeout value, unit is millisecond
 *
 **/
HT_API void onvif_SetReqTimeout(ONVIF_DEVICE * p_dev, int timeout);

/**
 * @brief
 *  If the request fails, get the error string
 *
 **/
HT_API char * onvif_GetErrString(ONVIF_DEVICE * p_dev);

/**
 * onvif device service interfaces 
 **/

/**
 * @brief
 *  This method provides a backward compatible interface for the base capabilities.
 *
 **/
HT_API BOOL onvif_tds_GetCapabilities(ONVIF_DEVICE * p_dev, tds_GetCapabilities_REQ * p_req, tds_GetCapabilities_RES * p_res);

/***
 * @brief
 *  Returns a collection of the devices services and possibly their available
 *  capabilities. The returned capability response message is untyped to allow
 *  future addition of services, service revisions and service capabilities.
 *  All returned service capabilities shall be structured by different namespaces
 *  which are supported by a device.
 *
 *  The version in GetServicesResponse shall contain the specification version 
 *  number of the corresponding service that is implemented by a device.
 *
 *  For the returned XAddr a device shall match the scheme and IP part of the 
 *  one used in the GetServices request.
 *  Note that if device is behind a NAT that device may return the local address
 *  and not the external address used by the client.
 *
 **/
HT_API BOOL onvif_tds_GetServices(ONVIF_DEVICE * p_dev, tds_GetServices_REQ * p_req, tds_GetServices_RES * p_res);

/**
 * @brief
 *  Returns the capabilities of the device service. The service shall 
 *  implement this method if the device supports the GetServices method.
 *
 **/
HT_API BOOL onvif_tds_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tds_GetServiceCapabilities_REQ * p_req, tds_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Gets device information, such as manufacturer, model and firmware
 *  version from a device.
 *
 **/
HT_API BOOL onvif_tds_GetDeviceInformation(ONVIF_DEVICE * p_dev, tds_GetDeviceInformation_REQ * p_req, tds_GetDeviceInformation_RES * p_res);

/**
 * @brief
 *  Lists the registered users and along with their user levels. 
 *  A device shall support this command unless support signalled via the
 *  UserConfigNotSupported capability is 'True'.
 *
 **/
HT_API BOOL onvif_tds_GetUsers(ONVIF_DEVICE * p_dev, tds_GetUsers_REQ * p_req, tds_GetUsers_RES * p_res);

/**
 * @brief
 *  Creates new device users and corresponding credentials on a device 
 *  for authentication.
 *
 *  A device shall support this command unless support signalled via the 
 *  UserConfigNotSupported capability is 'True'.
 *
 *  Either all users are created successfully or a fault message shall 
 *  be returned without creating any user.
 *
 **/
HT_API BOOL onvif_tds_CreateUsers(ONVIF_DEVICE * p_dev, tds_CreateUsers_REQ * p_req, tds_CreateUsers_RES * p_res);

/**
 * @brief
 *  Deletes users on a device.
 *
 *  A device shall support this command unless support signalled via
 *  the UserConfigNotSupported capability is 'True'.
 *
 *  A device may have one or more fixed users that cannot be deleted 
 *  to ensure access to the unit. Either all users are deleted 
 *  successfully or a fault message shall be returned and no users be 
 *  deleted.
 *
 **/
HT_API BOOL onvif_tds_DeleteUsers(ONVIF_DEVICE * p_dev, tds_DeleteUsers_REQ * p_req, tds_DeleteUsers_RES * p_res);

/**
 * @brief
 *  Updates the settings for one or several users on a device for authentication.
 *
 *  A device shall support this command unless support signalled via the 
 *  UserConfigNotSupported capability is 'True'. Either all change requests are 
 *  processed successfully or a fault message shall be returned and no change 
 *  requests be processed.
 *
 **/
HT_API BOOL onvif_tds_SetUser(ONVIF_DEVICE * p_dev, tds_SetUser_REQ * p_req, tds_SetUser_RES * p_res);

/**
 * @brief
 *  Returns the configured remote user (if any). 
 *  A device that signals support for remote user handling via the Security 
 *  Capability RemoteUserHandling shall support this operation. The user is 
 *  only valid for the WSUserToken profile or as a HTTP / RTSP user.
 *
 *  Password derivation is outside of the scope of this specification.
 *  
 **/
HT_API BOOL onvif_tds_GetRemoteUser(ONVIF_DEVICE * p_dev, tds_GetRemoteUser_REQ * p_req, tds_GetRemoteUser_RES * p_res);

/**
 * @brief
 *  Sets the remote user. 
 *  A device that signals support for remote user handling via the Security
 *  Capability RemoteUserHandling shall support this operation. Password 
 *  derivation is outside of the scope of this specification.
 *
 *  To remove the remote user SetRemoteUser should be called without the 
 *  RemoteUser parameter.
 *
 **/
HT_API BOOL onvif_tds_SetRemoteUser(ONVIF_DEVICE * p_dev, tds_SetRemoteUser_REQ * p_req, tds_SetRemoteUser_RES * p_res);

/**
 * @brief
 *  Gets the network interface configuration from a device.
 *
 **/
HT_API BOOL onvif_tds_GetNetworkInterfaces(ONVIF_DEVICE * p_dev, tds_GetNetworkInterfaces_REQ * p_req, tds_GetNetworkInterfaces_RES * p_res);

/**
 * @brief
 *  Sets the network interface configuration on a device.
 *
 *  If a device responds with RebootNeeded set to false, the device can be 
 *  reached via the new IP address without further action. A client should 
 *  be aware that a device may not be responsive for a short period of time 
 *  until it signals availability at the new address via the discovery Hello 
 *  messages
 *
 *  If a device responds with RebootNeeded set to true, it will be further 
 *  available under its previous IP address. The settings will only be activated 
 *  when the device is rebooted via the SystemReboot command.
 *
 *  For interoperability with a client unaware of the IEEE 802.11 extension 
 *  a device shall retain its IEEE 802.11 configuration if the IEEE 802.11 
 *  configuration element isn't present in the request.
 *
 **/
HT_API BOOL onvif_tds_SetNetworkInterfaces(ONVIF_DEVICE * p_dev, tds_SetNetworkInterfaces_REQ * p_req, tds_SetNetworkInterfaces_RES * p_res);

/**
 * @brief
 *  Gets the NTP settings from a device.
 *
 **/
HT_API BOOL onvif_tds_GetNTP(ONVIF_DEVICE * p_dev, tds_GetNTP_REQ * p_req, tds_GetNTP_RES * p_res);

/**
 * @brief
 *  Sets the NTP settings on a device.
 *
 *  A device shall accept string formated according to [RFC 1123] section 2.1, 
 *  other string shall be considered as invalid strings. It is valid to set the 
 *  FromDHCP flag while the device is not using DHCP to retrieve its IPv4 address.
 *
 *  Changes to the NTP server list shall not affect the clock mode DateTimeType. 
 *  Use SetSystemDateAndTime to activate NTP operation.
 *
 **/
HT_API BOOL onvif_tds_SetNTP(ONVIF_DEVICE * p_dev, tds_SetNTP_REQ * p_req, tds_SetNTP_RES * p_res);

/**
 * @brief
 *  Get the hostname from a device.
 *  
 *  The device shall return an empty string if no hostname has been assigned.
 *
 **/
HT_API BOOL onvif_tds_GetHostname(ONVIF_DEVICE * p_dev, tds_GetHostname_REQ * p_req, tds_GetHostname_RES * p_res);

/**
 * @brief
 *  Sets the hostname on a device.
 *
 *  Attention: a call to SetDNS may result in overriding a previously set hostname.
 *  
 *  A device shall accept strings formated according to [RFC 1123] section 2.1 or 
 *  alternatively to [RFC 952], other string shall be considered as invalid strings.
 *
 *  A device shall try to retrieve the name via DHCP when the HostnameFromDHCP 
 *  capability is set and an empty name string is provided.
 *
 **/
HT_API BOOL onvif_tds_SetHostname(ONVIF_DEVICE * p_dev, tds_SetHostname_REQ * p_req, tds_SetHostname_RES * p_res);

/**
 * @brief
 *  Controls whether the hostname shall be retrieved from DHCP.
 *
 *  A device shall support this command if support is signalled via the 
 *  HostnameFromDHCP capability. Depending on the device implementation 
 *  the change may only become effective after a device reboot. A device 
 *  shall accept the command independent whether it is currently using 
 *  DHCP to retrieve its IPv4 address or not.Note that the device is not 
 *  required to retrieve its hostname via DHCP while the device is not 
 *  using DHCP for retrieving its IP address. In the latter case the device 
 *  may fall back to the statically
 *
 **/
HT_API BOOL onvif_tds_SetHostnameFromDHCP(ONVIF_DEVICE * p_dev, tds_SetHostnameFromDHCP_REQ * p_req, tds_SetHostnameFromDHCP_RES * p_res);

/**
 * @brief
 *  Gets the DNS settings from a device.
 *
 **/
HT_API BOOL onvif_tds_GetDNS(ONVIF_DEVICE * p_dev, tds_GetDNS_REQ * p_req, tds_GetDNS_RES * p_res);

/**
 * @brief
 *  Sets the DNS settings on a device.
 *
 *  It is valid to set the FromDHCP flag while the device is not using 
 *  DHCP to retrieve its IPv4 address.
 *
 **/
HT_API BOOL onvif_tds_SetDNS(ONVIF_DEVICE * p_dev, tds_SetDNS_REQ * p_req, tds_SetDNS_RES * p_res);

/**
 * @brief
 *  Gets the dynamic DNS settings from a device.
 *
 **/
HT_API BOOL onvif_tds_GetDynamicDNS(ONVIF_DEVICE * p_dev, tds_GetDynamicDNS_REQ * p_req, tds_GetDynamicDNS_RES * p_res);

/**
 * @brief
 *  Sets the dynamic DNS settings on a device.
 *
 **/
HT_API BOOL onvif_tds_SetDynamicDNS(ONVIF_DEVICE * p_dev, tds_SetDynamicDNS_REQ * p_req, tds_SetDynamicDNS_RES * p_res);

/**
 * @brief
 *  Gets defined network protocols from a device.
 *
 *  This message returns an array of defined protocols supported by the device. 
 *  There are three protocols defined, HTTP, HTTPS and RTSP. For each protocol
 *  the parameters Port and Enable/Disable can be retrieved.
 *
 **/
HT_API BOOL onvif_tds_GetNetworkProtocols(ONVIF_DEVICE * p_dev, tds_GetNetworkProtocols_REQ * p_req, tds_GetNetworkProtocols_RES * p_res);

/**
 * @brief
 *  Configures defined network protocols on a device.
 *
 *  This message configures one or more defined network protocols supported 
 *  by the device. There are currently three protocols defined, HTTP, HTTPS 
 *  and RTSP. For each protocol the parameters Port and Enable/Disable can 
 *  be configured.
 *
 **/
HT_API BOOL onvif_tds_SetNetworkProtocols(ONVIF_DEVICE * p_dev, tds_SetNetworkProtocols_REQ * p_req, tds_SetNetworkProtocols_RES * p_res);

/**
 * @brief
 *  Gets the discovery mode of a device.
 *
 **/
HT_API BOOL onvif_tds_GetDiscoveryMode(ONVIF_DEVICE * p_dev, tds_GetDiscoveryMode_REQ * p_req, tds_GetDiscoveryMode_RES * p_res);

/**
 * @brief
 *  Sets the discovery mode operation of a device.
 *
 **/
HT_API BOOL onvif_tds_SetDiscoveryMode(ONVIF_DEVICE * p_dev, tds_SetDiscoveryMode_REQ * p_req, tds_SetDiscoveryMode_RES * p_res);

/**
 * @brief
 *  Gets the default gateway settings from a device.
 *
 **/
HT_API BOOL onvif_tds_GetNetworkDefaultGateway(ONVIF_DEVICE * p_dev, tds_GetNetworkDefaultGateway_REQ * p_req, tds_GetNetworkDefaultGateway_RES * p_res);

/**
 * @brief
 *  Sets the default gateway settings on a device.
 *
 **/
HT_API BOOL onvif_tds_SetNetworkDefaultGateway(ONVIF_DEVICE * p_dev, tds_SetNetworkDefaultGateway_REQ * p_req, tds_SetNetworkDefaultGateway_RES * p_res);

/**
 * @brief
 *  Gets the zero-configuration from a device.
 *
 **/
HT_API BOOL onvif_tds_GetZeroConfiguration(ONVIF_DEVICE * p_dev, tds_GetZeroConfiguration_REQ * p_req, tds_GetZeroConfiguration_RES * p_res);

/**
 * @brief
 *  Sets the zero-configuration on the device.
 *
 **/
HT_API BOOL onvif_tds_SetZeroConfiguration(ONVIF_DEVICE * p_dev, tds_SetZeroConfiguration_REQ * p_req, tds_SetZeroConfiguration_RES * p_res);

/**
 * @brief
 *  A client can ask for the device service endpoint reference address property 
 *  that can be used to derive the password equivalent for remote user operation.
 *
 **/
HT_API BOOL onvif_tds_GetEndpointReference(ONVIF_DEVICE * p_dev, tds_GetEndpointReference_REQ * p_req, tds_GetEndpointReference_RES * p_res);

/**
 * @brief
 *  The commands supported by the device is reported in the AuxiliaryCommands 
 *  attribute returned by the capabilities commands.
 *
 *  The command transmitted by using this command should match one of the 
 *  commands supported by the device.
 *  If for example the capability command response lists only irlampon command, 
 *  then the SendAuxiliaryCommand argument will be irlampon, which may indicate
 *  turning the connected IR lamp on.
 *
 *  Although the name of the auxiliary commands can be freely defined, commands
 *  starting with the prefix tt: are reserved to define frequently used commands
 *  and these reserved commands shall all share the "tt:command|parameter" syntax.
 *
 *  tt:Wiper|On - Request to start the wiper.
 *  tt:Wiper|Off -  Request to stop the wiper.
 *  tt:Washer|On - Request to start the washer.
 *  tt:Washer|Off - Request to stop the washer.
 *  tt:WashingProcedure|On - Request to start the washing procedure.
 *  tt:WashingProcedure|Off - Request to stop the washing procedure.
 *  tt:IRLamp|On - Request to turn ON an IR illuminator attached to the unit.
 *  tt:IRLamp|Off - Request to turn OFF an IR illuminator attached to the unit.
 *  tt:IRLamp|Auto - Request to configure an IR illuminator attached to the unit
 *  so that it automatically turns ON and OFF.
 *
 *  A device that indicates auxiliary service capability shall support this command.
 *
 **/
HT_API BOOL onvif_tds_SendAuxiliaryCommand(ONVIF_DEVICE * p_dev, tds_SendAuxiliaryCommand_REQ * p_req, tds_SendAuxiliaryCommand_RES * p_res);

/**
 * @brief
 *  Gets a list of all available relay outputs and their settings.
 *
 **/
HT_API BOOL onvif_tds_GetRelayOutputs(ONVIF_DEVICE * p_dev, tds_GetRelayOutputs_REQ * p_req, tds_GetRelayOutputs_RES * p_res);

/**
 * @brief
 *  Gets the settings of a relay output.
 *
 *  The relay can work in two relay modes:
 *  Bistable - After setting the state, the relay remains in this state.
 *  Monostable - After setting the state, the relay returns to its idle 
 *  state after the specified time.
 *
 *  The physical idle state of a relay output can be configured by setting
 *  the IdleState to 'open' or 'closed' (inversion of the relay behaviour).
 *
 *  Idle State 'open' means that the relay is open when the relay state is 
 *  set to 'inactive' through the trigger command and closed when the state
 *  is set to 'active' through the same command.
 *
 *  Idle State 'closed' means that the relay is closed when the relay state
 *  is set to 'inactive' through the trigger command and open when the state 
 *  is set to 'active' through the same command.
 *
 *  The Duration parameter of the Properties field "DelayTime" describes the
 *  time after which the relay returns to its idle state if it is in 
 *  monostable mode. If the relay is set to bistable mode the value of the
 *  parameter shall be ignored.
 *
 **/
HT_API BOOL onvif_tds_SetRelayOutputSettings(ONVIF_DEVICE * p_dev, tds_SetRelayOutputSettings_REQ * p_req, tds_SetRelayOutputSettings_RES * p_res);

/**
 * @brief
 *  Triggers a relay output.
 *
 **/
HT_API BOOL onvif_tds_SetRelayOutputState(ONVIF_DEVICE * p_dev, tds_SetRelayOutputState_REQ * p_req, tds_SetRelayOutputState_RES * p_res);

/**
 * @brief
 *  Gets the device system date and time.
 *
 *  A device shall provide the UTCDateTime information although the item is
 *  marked as optional to ensure backward compatibility.
 *
 **/
HT_API BOOL onvif_tds_GetSystemDateAndTime(ONVIF_DEVICE * p_dev, tds_GetSystemDateAndTime_REQ * p_req, tds_GetSystemDateAndTime_RES * p_res);

/**
 * @brief
 *  Sets the device system date and time.
 *  The device shall support the configuration of the daylight saving setting 
 *  and of the manual system date and time (if applicable) or indication of 
 *  NTP time (if applicable) through the SetSystemDateAndTime command. A device 
 *  shall consider a Timezone which is not formed according to the rules of 
 *  [IEEE 1003.1] section 8.3 as invalid.
 *
 *  The DayLightSavings flag should be set to true to activate any DST settings 
 *  of the TimeZone string. Clear the DayLightSavings flag if the DST portion of 
 *  the TimeZone settings should be ignored.
 *
 **/
HT_API BOOL onvif_tds_SetSystemDateAndTime(ONVIF_DEVICE * p_dev, tds_SetSystemDateAndTime_REQ * p_req, tds_SetSystemDateAndTime_RES * p_res);

/**
 * @brief
 *  Reboots a device. 
 *  Before the device reboots the response message shall be sent.
 *
 **/
HT_API BOOL onvif_tds_SystemReboot(ONVIF_DEVICE * p_dev, tds_SystemReboot_REQ * p_req, tds_SystemReboot_RES * p_res);

/**
 * @brief
 *  Reloads parameters of a device to their factory default values.
 *  The device shall support hard and soft factory default through the 
 *  SetSystemFactoryDefault command.
 *
 *  Hard All parameters are set to their factory default value.
 *
 *  Soft The meaning of soft factory default is device product-specific and 
 *       vendor-specific. The effect of a soft factory default operation is 
 *       not fully defined. However, it shall be guaranteed that after a soft 
 *       reset the device is reachable on the same IP address as used before 
 *       the reset. This means that basic network settings like IP address, 
 *       subnet and gateway or DHCP settings are kept unchanged by the soft reset.
 *  
 **/
HT_API BOOL onvif_tds_SetSystemFactoryDefault(ONVIF_DEVICE * p_dev, tds_SetSystemFactoryDefault_REQ * p_req, tds_SetSystemFactoryDefault_RES * p_res);

/**
 * @brief
 *  Gets a system log from a device.
 *
 *  The exact format of the system logs is outside the scope of this standard.
 *
 *  The system log information is transmitted through MTOM [MTOM] or as a string.
 *
 **/
HT_API BOOL onvif_tds_GetSystemLog(ONVIF_DEVICE * p_dev, tds_GetSystemLog_REQ * p_req, tds_GetSystemLog_RES * p_res);

/**
 * @brief
 *  Requests the scope parameters of a device. 
 *  The scope parameters are used in the device discovery to match a probe message. 
 *  The Scope parameters are of two different types:
 *
 *  Fixed
 *  Configurable
 *
 *  Fixed scope parameters are permanent device characteristics and cannot be 
 *  removed through the device management interface. The scope type is indicated
 *  in the scope list returned in the get scope parameters response. 
 *  
 *  As some scope parameters are mandatory, the device shall return a non-empty 
 *  scope list in the response.
 *
 **/
HT_API BOOL onvif_tds_GetScopes(ONVIF_DEVICE * p_dev, tds_GetScopes_REQ * p_req, tds_GetScopes_RES * p_res);

/**
 * @brief
 *  Sets the scope parameters of a device. 
 *  The scope parameters are used in the device discovery to match a probe message
 *
 *  This operation replaces all existing configurable scope parameters 
 *  (not fixed parameters). If this shall be avoided, one should use the 
 *  scope add command instead.
 *
 **/
HT_API BOOL onvif_tds_SetScopes(ONVIF_DEVICE * p_dev, tds_SetScopes_REQ * p_req, tds_SetScopes_RES * p_res);

/**
 * @brief
 *  Adds new configurable scope parameters to a device. 
 *  The scope parameters are used in the device discovery to match a probe message
 *
 **/
HT_API BOOL onvif_tds_AddScopes(ONVIF_DEVICE * p_dev, tds_AddScopes_REQ * p_req, tds_AddScopes_RES * p_res);

/**
 * @brief
 *  Deletes scope-configurable scope parameters from a device.
 *
 **/
HT_API BOOL onvif_tds_RemoveScopes(ONVIF_DEVICE * p_dev, tds_RemoveScopes_REQ * p_req, tds_RemoveScopes_RES * p_res);

/***
 * @brief
 *  Initiates a firmware upgrade using the HTTP POST mechanism.
 *  The response to the command includes an HTTP URL to which the upgrade 
 *  file may be uploaded. The actual upgrade takes place as soon as the 
 *  HTTP POST operation has completed. The device should support firmware 
 *  upgrade through the StartFirmwareUpgrade command. The exact format of 
 *  the firmware data is outside the scope of this specification.
 *
 *  Firmware upgrade over HTTP may be achieved using the following steps:
 *  1. Client calls StartFirmwareUpgrade.
 *  2. Device service responds with upload URI and optional delay value.
 *  3. Client waits for delay duration if specified by server.
 *  4. Client transmits the firmware image to the upload URI using HTTP POST.
 *  5. Server reprograms itself using the uploaded image, then reboots
 *
 *  If the firmware upgrade fails because the upgrade file was invalid, 
 *  the HTTP POST response shall be "415 Unsupported Media Type". If the 
 *  firmware upgrade fails due to an error at the device, the HTTP POST 
 *  response shall be "500 Internal Server Error".
 *
 *  The value of the Content-Type header in the HTTP POST request shall be 
 *  "application/octet-stream".
 *
 *  After applying a firmware upgrade the device shall keep the basic network 
 *  configuration like IP address, subnet mask and gateway or DHCP settings 
 *  unchanged. Additionally a firmware upgrade shall not change user credentials.
 *
 **/
HT_API BOOL onvif_tds_StartFirmwareUpgrade(ONVIF_DEVICE * p_dev, tds_StartFirmwareUpgrade_REQ * p_req, tds_StartFirmwareUpgrade_RES * p_res);

/**
 * @brief
 *  Retrieve URIs from which system information may be downloaded using HTTP. 
 *  URIs may be returned for the following system information:
 *
 *  System Logs. Multiple system logs may be returned, of different types. 
 *  The exact format of the system logs is outside the scope of this specification.
 *
 *  Support Information. This consists of arbitrary device diagnostics information 
 *  from a device. The exact format of the diagnostic information is outside the 
 *  scope of this specification.
 *
 *  System Backup. The received file is a backup file that can be used to restore 
 *  the current device configuration at a later date. The exact format of the backup 
 *  configuration file is outside the scope of this specification.
 *
 *  If the device allows retrieval of system logs, support information or system 
 *  backup data, it should make them available via HTTP GET.
 *
 **/
HT_API BOOL onvif_tds_GetSystemUris(ONVIF_DEVICE * p_dev, tds_GetSystemUris_REQ * p_req, tds_GetSystemUris_RES * p_res);

/***
 * @brief
 *  Initiates a system restore from backed up configuration data using 
 *  the HTTP POST mechanism.
 *
 *  The response to the command includes an HTTP URL to which the backup 
 *  file may be uploaded. The actual restore takes place as soon as the 
 *  HTTP POST operation has completed. The exact format of the backup 
 *  configuration data is outside the scope of this specification. 
 *  
 *  System restore over HTTP may be achieved using the following steps:
 *  1. Client calls StartSystemRestore.
 *  2. Device service responds with upload URI.
 *  3. Client transmits the configuration data to the upload URI using HTTP POST.
 *  4. Server applies the uploaded configuration, then reboots if necessary
 *
 *  If the system restore fails because the uploaded file was invalid, the HTTP POST 
 *  response shall be "415 Unsupported Media Type". If the system restore fails due 
 *  to an error at the device, the HTTP POST response shall be "500 Internal Server Error".
 *  The value of the Content-Type header in the HTTP POST request shall be 
 *  "Application/octet-stream".
 *
 **/
HT_API BOOL onvif_tds_StartSystemRestore(ONVIF_DEVICE * p_dev, tds_StartSystemRestore_REQ * p_req, tds_StartSystemRestore_RES * p_res);

/***
 * @brief
 *  This method allows to provide a URL where product specific WSDL and schema 
 *  definitions can be retrieved.
 *  This method has been deprecated with version 20.12.
 **/
HT_API BOOL onvif_tds_GetWsdlUrl(ONVIF_DEVICE * p_dev, tds_GetWsdlUrl_REQ * p_req, tds_GetWsdlUrl_RES * p_res);

/**
 * @brief
 *  Returns the IEEE802.11 capabilities
 *
 **/
HT_API BOOL onvif_tds_GetDot11Capabilities(ONVIF_DEVICE * p_dev, tds_GetDot11Capabilities_REQ * p_req, tds_GetDot11Capabilities_RES * p_res);

/**
 * @brief
 *  Returns the status of a wireless network interface.
 *
 *  The following status can be returned:
 *  SSID (shall)
 *  BSSID (should)
 *  Pair cipher (should)
 *  Group cipher (should)
 *  Signal strength (should)
 *  Alias of active wireless configuration (shall)
 *
 **/
HT_API BOOL onvif_tds_GetDot11Status(ONVIF_DEVICE * p_dev, tds_GetDot11Status_REQ * p_req, tds_GetDot11Status_RES * p_res);

/**
 * @brief
 *  Returns a lists of the wireless networks in range of the device.
 *
 *  The following status can be returned for each network:
 *  SSID (shall)
 *  BSSID (should)
 *  Authentication and key management suite(s) (should)
 *  Pair cipher(s) (should)
 *  Group cipher(s) (should)
 *  Signal strength (should)
 *
 **/
HT_API BOOL onvif_tds_ScanAvailableDot11Networks(ONVIF_DEVICE * p_dev, tds_ScanAvailableDot11Networks_REQ * p_req, tds_ScanAvailableDot11Networks_RES * p_res);

/**
 * @brief
 *  Gets the geo location information of a device. 
 *  A device that signals support for GeoLocation via the capability
 *  GeoLocationEntities shall support the retrieval of geo location 
 *  information via this command.
 *
 **/
HT_API BOOL onvif_tds_GetGeoLocation(ONVIF_DEVICE * p_dev, tds_GetGeoLocation_REQ * p_req, tds_GetGeoLocation_RES * p_res);

/**
 * @brief
 *  Modify one or more geo location entries.
 *  A device that signals support for GeoLocation via the GeoLocationEntities 
 *  capabiliy shall support modifying geo location information via this command.
 *
 *  The method allows to update one or more entries at once. The method shall 
 *  modify only those entries that are referenced by the request arguments. 
 *  A device shall create a new entry in case the combination of type
 *  and token does not yet exist. A device shall remove any of the location and 
 *  orientations components in case they are not passed in the request.
 *
 **/
HT_API BOOL onvif_tds_SetGeoLocation(ONVIF_DEVICE * p_dev, tds_SetGeoLocation_REQ * p_req, tds_SetGeoLocation_RES * p_res);

/**
 * @brief
 *  Remove one or more geo location entries.
 *
 *  A device shall delete an entity based on the passed fields type and token.
 *
 **/
HT_API BOOL onvif_tds_DeleteGeoLocation(ONVIF_DEVICE * p_dev, tds_DeleteGeoLocation_REQ * p_req, tds_DeleteGeoLocation_RES * p_res);

/**
 * @brief
 *  How to set HashingAlgorithm for an ONVIF Device/Client:
 *  ONVIF client should use SetHashingAlgorithm API to modify the current 
 *  hashing algorithm of a device.
 *  SetHashingAlgorithm API sets the hashing algorithm(s) to be used in 
 *  HTTP and RTSP Digest Authentication.
 *
 *  After changing the hashing algorithm of an ONVIF device, the device 
 *  should use the new hashing algorithm in the digest challenge for the 
 *  upcoming HTTP and RTSP request.
 *  
 **/
HT_API BOOL onvif_tds_SetHashingAlgorithm(ONVIF_DEVICE * p_dev, tds_SetHashingAlgorithm_REQ * p_req, tds_SetHashingAlgorithm_RES * p_res);

/**
 * @brief
 *  Gets the IP address filter settings from a device. 
 *  If the device supports device access control based on IP filtering rules
 *  (denied or accepted ranges of IP addresses).
 *
 **/
HT_API BOOL onvif_tds_GetIPAddressFilter(ONVIF_DEVICE * p_dev, tds_GetIPAddressFilter_REQ * p_req, tds_GetIPAddressFilter_RES * p_res);

/**
 * @brief
 *  Sets the IP address filter settings on a device.
 *
 **/
HT_API BOOL onvif_tds_SetIPAddressFilter(ONVIF_DEVICE * p_dev, tds_SetIPAddressFilter_REQ * p_req, tds_SetIPAddressFilter_RES * p_res);

/**
 * @brief
 *  Adds an IP filter address to a device.
 *
 *  The value of the Type field shall be ignored by the device. 
 *  Use SetIPAddressFilter to set the type.
 *
 **/
HT_API BOOL onvif_tds_AddIPAddressFilter(ONVIF_DEVICE * p_dev, tds_AddIPAddressFilter_REQ * p_req, tds_AddIPAddressFilter_RES * p_res);

/**
 * @brief
 *  Deletes an IP filter address from a device.
 *
 *  The value of the Type field shall be ignored by the device.
 *
 **/
HT_API BOOL onvif_tds_RemoveIPAddressFilter(ONVIF_DEVICE * p_dev, tds_RemoveIPAddressFilter_REQ * p_req, tds_RemoveIPAddressFilter_RES * p_res);

/**
 * @brief
 *  Get access policy.
 *  
 * If call succesful, should call free to free p_res->PolicyFile.Data.ptr.
 *
 **/
HT_API BOOL onvif_tds_GetAccessPolicy(ONVIF_DEVICE * p_dev, tds_GetAccessPolicy_REQ * p_req, tds_GetAccessPolicy_RES * p_res);

/**
 * @brief
 *  Set access policy.
 *
 **/
HT_API BOOL onvif_tds_SetAccessPolicy(ONVIF_DEVICE * p_dev, tds_SetAccessPolicy_REQ * p_req, tds_SetAccessPolicy_RES * p_res);

/**
 * @brief
 *  Lists all existing storage configurations. 
 *  A device indicating storage configuration capability shall
 *  support the listing of existing storage configurations
 *
 **/
HT_API BOOL onvif_tds_GetStorageConfigurations(ONVIF_DEVICE * p_dev, tds_GetStorageConfigurations_REQ * p_req, tds_GetStorageConfigurations_RES * p_res);

/**
 * @brief
 *  Creates a new storage configuration. The configuration data shall be created
 *  in the device and shall be persistent (remains after a device reboots). 
 *  A device indicating storage configuration capability shall support the 
 *  creation of storage configurations as long as the number of existing storage
 *  configurations does not exceed the value of MaxStorageConfigurations capability.
 *
 **/
HT_API BOOL onvif_tds_CreateStorageConfiguration(ONVIF_DEVICE * p_dev, tds_CreateStorageConfiguration_REQ * p_req, tds_CreateStorageConfiguration_RES * p_res);

/**
 * @brief
 *  Retrieves the Storage configuration when the storage configuration token
 *  is known. A device indicating storage configuration capability shall support
 *  retrieval of specific storage configuration
 *
 **/
HT_API BOOL onvif_tds_GetStorageConfiguration(ONVIF_DEVICE * p_dev, tds_GetStorageConfiguration_REQ * p_req, tds_GetStorageConfiguration_RES * p_res);

/**
 * @brief
 *  Modifies an existing storage configuration.
 *
 **/
HT_API BOOL onvif_tds_SetStorageConfiguration(ONVIF_DEVICE * p_dev, tds_SetStorageConfiguration_REQ * p_req, tds_SetStorageConfiguration_RES * p_res);

/**
 * @brief
 *  Deletes a storage configuration.
 *  
 **/
HT_API BOOL onvif_tds_DeleteStorageConfiguration(ONVIF_DEVICE * p_dev, tds_DeleteStorageConfiguration_REQ * p_req, tds_DeleteStorageConfiguration_RES * p_res);

/**
 * onvif media service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_trt_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trt_GetServiceCapabilities_REQ * p_req, trt_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Lists all available video sources for the device.
 *
 **/
HT_API BOOL onvif_trt_GetVideoSources(ONVIF_DEVICE * p_dev, trt_GetVideoSources_REQ * p_req, trt_GetVideoSources_RES * p_res);

/**
 * @brief
 *  Lists all available audio sources of the device. 
 *  A device that supports audio streaming from device to client shall 
 *  support listing of available audio sources
 *
 **/
HT_API BOOL onvif_trt_GetAudioSources(ONVIF_DEVICE * p_dev, trt_GetAudioSources_REQ * p_req, trt_GetAudioSources_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_CreateProfile(ONVIF_DEVICE * p_dev, trt_CreateProfile_REQ * p_req, trt_CreateProfile_RES * p_res);

/**
 * @brief
 *  If the profile token is already known, a profile can be fetched through
 *  the GetProfile command.
 *
 **/
HT_API BOOL onvif_trt_GetProfile(ONVIF_DEVICE * p_dev, trt_GetProfile_REQ * p_req, trt_GetProfile_RES * p_res);

/**
 * @brief
 *  Any endpoint can ask for the existing media profiles of a device using the 
 *  GetProfiles command. Pre-configured or dynamically configured profiles can 
 *  be retrieved using this command. This command lists all configured profiles
 *  in a device. The client does not need to know the media profile in order to 
 *  use the command.
 *
 *  A device shall include the "fixed" attribute in all the returned Profile
 *  elements.
 *
 **/
HT_API BOOL onvif_trt_GetProfiles(ONVIF_DEVICE * p_dev, trt_GetProfiles_REQ * p_req, trt_GetProfiles_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_AddVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoEncoderConfiguration_REQ * p_req, trt_AddVideoEncoderConfiguration_RES * p_res);

/**
 * 
 * @brief
 *  Adds a VideoSourceConfiguration to an existing media profile. 
 *  If such a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_AddVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoSourceConfiguration_REQ * p_req, trt_AddVideoSourceConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Adds an AudioEncoderConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  A device shall support adding a compatible AudioEncoderConfiguration 
 *  to a Profile containing an AudioSourceConfiguration and shall support 
 *  streaming audio data of such a Profile.
 *
 **/
HT_API BOOL onvif_trt_AddAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioEncoderConfiguration_REQ * p_req, trt_AddAudioEncoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Adds an AudioSourceConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_AddAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioSourceConfiguration_REQ * p_req, trt_AddAudioSourceConfiguration_RES * p_res);

/**
 * @brief
 *  A device returns the information for current video source mode and
 *  settable video source modes of specified video source. 
 *  A device that indicates a capability of VideoSourceMode shall support
 *  this command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoSourceModes(ONVIF_DEVICE * p_dev, trt_GetVideoSourceModes_REQ * p_req, trt_GetVideoSourceModes_RES * p_res);

/**
 *
 * @brief
 *  Changes the media profile structure relating to video source for the
 *  specified video source mode. A device that indicates a capability of 
 *  VideoSourceMode shall support this command. The behavior after changing 
 *  the mode is not defined in this specification.
 *
 **/
HT_API BOOL onvif_trt_SetVideoSourceMode(ONVIF_DEVICE * p_dev, trt_SetVideoSourceMode_REQ * p_req, trt_SetVideoSourceMode_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_AddPTZConfiguration(ONVIF_DEVICE * p_dev, trt_AddPTZConfiguration_REQ * p_req, trt_AddPTZConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes a VideoEncoderConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoEncoderConfiguration, 
 *  the operation has no effect.The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoEncoderConfiguration_REQ * p_req, trt_RemoveVideoEncoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes a VideoSourceConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoSourceConfiguration, 
 *  the operation has no effect.The removal shall be persistent.
 *
 *  Video source configurations should only be removed after removing a 
 *  VideoEncoderConfiguration from the media profile.
 *
 **/
HT_API BOOL onvif_trt_RemoveVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoSourceConfiguration_REQ * p_req, trt_RemoveVideoSourceConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes an AudioEncoderConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioEncoderConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioEncoderConfiguration_REQ * p_req, trt_RemoveAudioEncoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes an AudioSourceConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioSourceConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 *  Audio source configurations should only be removed after removing an 
 *  AudioEncoderConfiguration from the media profile.
 *
 **/
HT_API BOOL onvif_trt_RemoveAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioSourceConfiguration_REQ * p_req, trt_RemoveAudioSourceConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes a PTZConfiguration from an existing media profile. 
 *  If the media profile does not contain a PTZConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemovePTZConfiguration(ONVIF_DEVICE * p_dev, trt_RemovePTZConfiguration_REQ * p_req, trt_RemovePTZConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Deletes a profile. This change shall always be persistent.
 *
 **/
HT_API BOOL onvif_trt_DeleteProfile(ONVIF_DEVICE * p_dev, trt_DeleteProfile_REQ * p_req, trt_DeleteProfile_RES * p_res);

/**
 * @brief
 *  Lists all existing video source configurations for a device. 
 *  This command lists all video source configurations in a device. 
 *  The client need not know anything about the video source configurations
 *  in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfigurations_REQ * p_req, trt_GetVideoSourceConfigurations_RES * p_res);

/**
 * @brief
 *  Lists all existing video encoder configurations of a device. 
 *  This command lists all configured video encoder configurations in a device.
 *  The client does not need to know anything apriori about the video encoder 
 *  configurations in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfigurations_REQ * p_req, trt_GetVideoEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  Lists all existing audio source configurations of a device.
 *  This command lists all audio source configurations in a device. 
 *  The client does not need to know anything apriori about the audio source
 *  configurations in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfigurations_REQ * p_req, trt_GetAudioSourceConfigurations_RES * p_res);

/**
 * @brief
 *  Lists all existing device audio encoder configurations. 
 *  The client does not need to know anything apriori about the audio encoder
 *  configurations in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfigurations_REQ * p_req, trt_GetAudioEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  If the video source configuration token is already known, the video source
 *  configuration can be fetched through the GetVideoSourceConfiguration command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfiguration_REQ * p_req, trt_GetVideoSourceConfiguration_RES * p_res);

/**
 * @brief
 *  If the video encoder configuration token is already known, the encoder 
 *  configuration can be fetched through the GetVideoEncoderConfiguration command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfiguration_REQ * p_req, trt_GetVideoEncoderConfiguration_RES * p_res);

/**
 * @brief
 *  Fetches the audio source configurations if the audio source configuration 
 *  token is already known.
 *
 **/
HT_API BOOL onvif_trt_GetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfiguration_REQ * p_req, trt_GetAudioSourceConfiguration_RES * p_res);

/**
 * @brief
 *  Fetches the encoder configuration if the audio encoder configuration token
 *  is known.
 *
 **/
HT_API BOOL onvif_trt_GetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfiguration_REQ * p_req, trt_GetAudioEncoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Modifies a video source configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. Running streams using this configuration may be 
 *  immediately updated according to the new settings. The changes are not 
 *  guaranteed to take effect unless the client requests a new stream URI 
 *  and restarts any affected stream.
 *
 **/
HT_API BOOL onvif_trt_SetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoSourceConfiguration_REQ * p_req, trt_SetVideoSourceConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoEncoderConfiguration_REQ * p_req, trt_SetVideoEncoderConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioSourceConfiguration_REQ * p_req, trt_SetAudioSourceConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioEncoderConfiguration_REQ * p_req, trt_SetAudioEncoderConfiguration_RES * p_res);

/**
 * @brief
 *  Returns the available parameters and their valid ranges to the client.
 *
 *  If a video source configuration token is provided, the device shall return
 *  the options compatible with that configuration. If a media profile token
 *  is specified, the device shall return the options compatible with that media
 *  profile. If both a media profile token and a video source configuration 
 *  token are specified, the device shall return the options compatible with 
 *  both that media profile and that configuration. If no tokens are specified, 
 *  the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetVideoSourceConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfigurationOptions_REQ * p_req, trt_GetVideoSourceConfigurationOptions_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_GetVideoEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfigurationOptions_REQ * p_req, trt_GetVideoEncoderConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Returns the available parameters and their valid ranges to the client.
 *  Any combination of the parameters obtained using a given media profile 
 *  and audio source configuration shall be a valid input for the
 *  SetAudioSourceConfiguration command.
 *
 *  If an audio source configuration token is provided, the device shall 
 *  return the options compatible with that configuration. If a media profile
 *  token is specified, the device shall return the options compatible with
 *  that media profile. If both a media profile token and an audio source 
 *  configuration token are specified, the device shall return the options 
 *  compatible with both that media profile and that configuration. If no 
 *  tokens are specified, the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetAudioSourceConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfigurationOptions_REQ * p_req, trt_GetAudioSourceConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Returns the available parameters and their valid ranges to the client. 
 *  Any combination of the parameters obtained using a given media profile 
 *  and audio encoder configuration shall be a valid input for the
 *  SetAudioEncoderConfiguration command.
 *
 *  If an audio encoder configuration token is provided, the device shall 
 *  return the options compatible with that configuration. If a media profile
 *  token is specified, the device shall return the options compatible with 
 *  that media profile. If both a media profile token and an audio encoder
 *  configuration token are specified, the device shall return the options 
 *  compatible with both that media profile and that configuration. If no 
 *  tokens are specified, the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetAudioEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfigurationOptions_REQ * p_req, trt_GetAudioEncoderConfigurationOptions_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_GetStreamUri(ONVIF_DEVICE * p_dev, trt_GetStreamUri_REQ * p_req, trt_GetStreamUri_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, trt_SetSynchronizationPoint_REQ * p_req, trt_SetSynchronizationPoint_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_GetSnapshotUri(ONVIF_DEVICE * p_dev, trt_GetSnapshotUri_REQ * p_req, trt_GetSnapshotUri_RES * p_res);

/**
 * @brief
 *  Request the minimum number of guaranteed video encoder instances 
 *  (applications) per Video Source Configuration. 
 *  A device SHALLsupport this command. This command was added in ONVIF 1.02.
 *
 **/
HT_API BOOL onvif_trt_GetGuaranteedNumberOfVideoEncoderInstances(ONVIF_DEVICE * p_dev, trt_GetGuaranteedNumberOfVideoEncoderInstances_REQ * p_req, trt_GetGuaranteedNumberOfVideoEncoderInstances_RES * p_res);

/**
 * @brief
 *  Lists all available audio outputs of a device.
 *  An device that signals support for Audio outputs via its Device IO 
 *  AudioOutputs capability shall support listing of available audio outputs
 *
 **/
HT_API BOOL onvif_trt_GetAudioOutputs(ONVIF_DEVICE * p_dev, trt_GetAudioOutputs_REQ * p_req, trt_GetAudioOutputs_RES * p_res);

/**
 * @brief
 *  Lists all existing AudioOutputConfigurations of a device. 
 *  The client does not need to know anything apriori about the audio 
 *  configurations to use this command.
 *
 **/
HT_API BOOL onvif_trt_GetAudioOutputConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfigurations_REQ * p_req, trt_GetAudioOutputConfigurations_RES * p_res);

/**
 * @brief
 *  If the audio output configuration token is already known, the output
 *  configuration can be fetched through the GetAudioOutputConfiguration 
 *  command.
 *
 **/
HT_API BOOL onvif_trt_GetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfiguration_REQ * p_req, trt_GetAudioOutputConfiguration_RES * p_res);

/**
 * @brief
 *  Returns the available parameters and their valid ranges to the client.
 *  Any combination of the parameters obtained using a given media profile 
 *  and audio output configuration shall be a valid input for the
 *  SetAudioOutputConfiguration command.
 *
 *  If an audio output configuration token is provided, the device shall 
 *  return the options compatible with that configuration. If a media profile 
 *  token is specified, the device shall return the options compatible with
 *  that media profile. If both a media profile token and an audio output 
 *  configuration token are specified, the device shall return the options
 *  compatible with both that media profile and that configuration. If no 
 *  tokens are specified, the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetAudioOutputConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfigurationOptions_REQ * p_req, trt_GetAudioOutputConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies an audio output configuration.
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device. An device that signals support for Audio outputs
 *  via its Device IO AudioOutputs capability shall support the modification
 *  of audio output parameters
 *
 **/
HT_API BOOL onvif_trt_SetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioOutputConfiguration_REQ * p_req, trt_SetAudioOutputConfiguration_RES * p_res);

/**
 * @brief
 *  Lists all existing AudioDecoderConfigurations of a device.
 *
 *  The client does not need to know anything apriori about the audio decoder
 *  configurations in order to use this command. An device that signals 
 *  support for Audio outputs via its Device IO AudioOutputs capability shall
 *  support the listing of AudioOutputConfigurations through this command.
 *
 **/
HT_API BOOL onvif_trt_GetAudioDecoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfigurations_REQ * p_req, trt_GetAudioDecoderConfigurations_RES * p_res);

/**
 * @brief
 *  If the audio decoder configuration token is already known, the decoder 
 *  configuration can be fetched through the GetAudioDecoderConfiguration
 *  command. An device that signals support for Audio outputs via its Device
 *  IO AudioOutputs capability shall support the retrieval of a specific 
 *  audio decoder configuration.
 *
 **/
HT_API BOOL onvif_trt_GetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfiguration_REQ * p_req, trt_GetAudioDecoderConfiguration_RES * p_res);

/**
 * @brief
 *  Returns the available parameters and their valid ranges to the client.
 *  Any combination of the parameters obtained using a given media profile
 *  and audio decoder configuration shall be a valid input for the
 *  SetAudioDecoderConfiguration command.
 *
 *  If an audio decoder configuration token is provided, the device shall 
 *  return the options compatible with that configuration. If a media profile
 *  token is specified, the device shall return the options compatible with
 *  that media profile. If both a media profile token and an audio decoder
 *  configuration token are specified, the device shall return the options 
 *  compatible with both that media profile and that configuration. If no 
 *  tokens are specified, the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetAudioDecoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfigurationOptions_REQ * p_req, trt_GetAudioDecoderConfigurationOptions_RES * p_res);

/**
 *
 * @brief
 *  Modifies an audio decoder configuration. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device.
 *
 **/
HT_API BOOL onvif_trt_SetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioDecoderConfiguration_REQ * p_req, trt_SetAudioDecoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Adds an AudioOutputConfiguration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent. 
 *
 *  An device that signals support for Audio outputs via its Device IO 
 *  AudioOutputs capability shall support the addition of an audio output
 *  configuration to a profile.
 *
 **/
HT_API BOOL onvif_trt_AddAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioOutputConfiguration_REQ * p_req, trt_AddAudioOutputConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_AddAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioDecoderConfiguration_REQ * p_req, trt_AddAudioDecoderConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes an AudioOutputConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioOutputConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioOutputConfiguration_REQ * p_req, trt_RemoveAudioOutputConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes an AudioDecoderConfiguration from an existing media profile. 
 *  If the media profile does not contain an AudioDecoderConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioDecoderConfiguration_REQ * p_req, trt_RemoveAudioDecoderConfiguration_RES * p_res);

/**
 * @brief
 *  Lists all existing OSD configurations for the device.
 *
 **/
HT_API BOOL onvif_trt_GetOSDs(ONVIF_DEVICE * p_dev, trt_GetOSDs_REQ * p_req, trt_GetOSDs_RES * p_res);

/**
 * @brief
 *  If the OSD configuration token is already known, the OSD configuration 
 *  can be fetched through the GetOSD command.
 *
 **/
HT_API BOOL onvif_trt_GetOSD(ONVIF_DEVICE * p_dev, trt_GetOSD_REQ * p_req, trt_GetOSD_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetOSD(ONVIF_DEVICE * p_dev, trt_SetOSD_REQ * p_req, trt_SetOSD_RES * p_res);

/**
 * @brief
 *  Returns the available options when the OSD parameters are reconfigured.
 *  The device shall support the listing of available OSD parameter options 
 *  (for a given video source configuration) through the GetOSDOptions command.
 *  Any combination of the parameters obtained using a given video source 
 *  configuration shall be a valid input for the corresponding SetOSD command.
 *
 **/
HT_API BOOL onvif_trt_GetOSDOptions(ONVIF_DEVICE * p_dev, trt_GetOSDOptions_REQ * p_req, trt_GetOSDOptions_RES * p_res);

/**
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
 *  is missing, device shall assume IsPersistentText attribute as true.
 *
 *  A created OSD shall be deletable.
 *
 **/
HT_API BOOL onvif_trt_CreateOSD(ONVIF_DEVICE * p_dev, trt_CreateOSD_REQ * p_req, trt_CreateOSD_RES * p_res);

/**
 *
 * @brief
 *  Deletes an OSD. This change shall always be persistent.
 *
 **/
HT_API BOOL onvif_trt_DeleteOSD(ONVIF_DEVICE * p_dev, trt_DeleteOSD_REQ * p_req, trt_DeleteOSD_RES * p_res);

/**
 * @brief
 *  Lists all video analytics configurations of a device. 
 *  This command lists all configured video analytics in a device. 
 *  The client does not need to know anything apriori about the video 
 *  analytics in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetVideoAnalyticsConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoAnalyticsConfigurations_REQ * p_req, trt_GetVideoAnalyticsConfigurations_RES * p_res);

/**
 *
 * @brief
 *  Adds a VideoAnalytics configuration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  Adding a VideoAnalyticsConfiguration to a media profile means that 
 *  streams using that media profile can contain video analytics data 
 *  (in the metadata) as defined by the submitted configuration reference.
 *
 *  A profile containing only a video analytics configuration but no video 
 *  source configuration is incomplete. Therefore, a client should first 
 *  add a video source configuration to a profile before adding a video 
 *  analytics configuration. The device can deny adding of a video analytics 
 *  configuration before a video source configuration. 
 *
 **/
HT_API BOOL onvif_trt_AddVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoAnalyticsConfiguration_REQ * p_req, trt_AddVideoAnalyticsConfiguration_RES * p_res);

/**
 * @brief
 *  Fetches the video analytics configuration if the video analytics
 *  token is known.
 *
 **/
HT_API BOOL onvif_trt_GetVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoAnalyticsConfiguration_REQ * p_req, trt_GetVideoAnalyticsConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes a VideoAnalyticsConfiguration from an existing media profile. 
 *  If the media profile does not contain a VideoAnalyticsConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoAnalyticsConfiguration_REQ * p_req, trt_RemoveVideoAnalyticsConfiguration_RES * p_res);

/**
 *
 * @brief
 *  A video analytics configuration is modified using this command. 
 *  The ForcePersistence flag indicates if the changes shall remain after 
 *  reboot of the device or not. Running streams using this configuration
 *  shall be immediately updated according to the new settings. Otherwise
 *  inconsistencies can occur between the scene description processed by 
 *  the rule engine and the notifications produced by analytics engine and
 *  rule engine which reference the very same video analytics configuration
 *  token.
 *
 **/
HT_API BOOL onvif_trt_SetVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoAnalyticsConfiguration_REQ * p_req, trt_SetVideoAnalyticsConfiguration_RES * p_res);

/**
 * @brief
 *  Lists all existing metadata configurations. 
 *  The client does not need to know anything apriori about the metadata 
 *  in order to use the command.
 *
 **/
HT_API BOOL onvif_trt_GetMetadataConfigurations(ONVIF_DEVICE * p_dev, trt_GetMetadataConfigurations_REQ * p_req, trt_GetMetadataConfigurations_RES * p_res);

/**
 *
 * @brief
 *  Adds a Metadata configuration to an existing media profile. 
 *  If a configuration exists in the media profile, it will be replaced. 
 *  The change shall be persistent.
 *
 *  Adding a MetadataConfiguration to a Profile means that streams using 
 *  that profile contain metadata. Metadata can consist of events, PTZ status, 
 *  and/or video analytics data.
 *
 **/
HT_API BOOL onvif_trt_AddMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_AddMetadataConfiguration_REQ * p_req, trt_AddMetadataConfiguration_RES * p_res);

/**
 * @brief
 *  Fetches the metadata configuration if the metadata token is known.
 *
 **/
HT_API BOOL onvif_trt_GetMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_GetMetadataConfiguration_REQ * p_req, trt_GetMetadataConfiguration_RES * p_res);

/**
 *
 * @brief
 *  Removes a MetadataConfiguration from an existing media profile. 
 *  If the media profile does not contain a MetadataConfiguration, 
 *  the operation has no effect. The removal shall be persistent.
 *
 **/
HT_API BOOL onvif_trt_RemoveMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveMetadataConfiguration_REQ * p_req, trt_RemoveMetadataConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_trt_SetMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_SetMetadataConfiguration_REQ * p_req, trt_SetMetadataConfiguration_RES * p_res);

/**
 * @brief
 *  This operation returns the available parameters and their valid ranges 
 *  to the client. Any combination of the parameters obtained using a given
 *  media profile and metadata configuration shall be a valid input for the
 *  SetMetadataConfiguration command.
 *
 *  If a metadata configuration token is provided, the device shall return
 *  the options compatible with that configuration. If a media profile token
 *  is specified, the device shall return the options compatible with that
 *  media profile. If both a media profile token and a metadata configuration
 *  token are specified, the device shall return the options compatible with 
 *  both that media profile and that configuration. If no tokens are specified, 
 *  the options shall be considered generic for the device.
 *
 **/
HT_API BOOL onvif_trt_GetMetadataConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetMetadataConfigurationOptions_REQ * p_req, trt_GetMetadataConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Lists all the video encoder configurations of the device that are 
 *  compatible with a certain media profile. 
 *  Each of the returned configurations shall be a valid input parameter 
 *  for the AddVideoEncoderConfiguration command on the media profile.
 *  The result will vary depending on the capabilities, configurations and
 *  settings in the device.
 *
 **/
HT_API BOOL onvif_trt_GetCompatibleVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleVideoEncoderConfigurations_REQ * p_req, trt_GetCompatibleVideoEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  Requests all audio encoder configurations of the device that are 
 *  compatible with a certain media profile. 
 *  Each of the returned configurations shall be a valid input parameter 
 *  for the AddAudioEncoderConfiguration command on the media profile.
 *  The result varies depending on the capabilities, configurations and 
 *  settings in the device.
 *
 **/
HT_API BOOL onvif_trt_GetCompatibleAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleAudioEncoderConfigurations_REQ * p_req, trt_GetCompatibleAudioEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  Requests all video analytic configurations of the device that are 
 *  compatible with a certain media profile. 
 *  Each of the returned configurations shall be a valid input parameter 
 *  for the AddVideoAnalyticsConfiguration command on the media profile. 
 *  The result varies depending on the capabilities, configurations and
 *  settings in the device.
 *
 **/
HT_API BOOL onvif_trt_GetCompatibleVideoAnalyticsConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleVideoAnalyticsConfigurations_REQ * p_req, trt_GetCompatibleVideoAnalyticsConfigurations_RES * p_res);

/**
 * @brief
 *  Requests all the metadata configurations of the device that are 
 *  compatible with a certain media profile.
 *  Each of the returned configurations shall be a valid input parameter 
 *  for the AddMetadataConfiguration command on the media profile.
 *  The result varies depending on the capabilities, configurations and
 *  settings in the device.
 *
 **/
HT_API BOOL onvif_trt_GetCompatibleMetadataConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleMetadataConfigurations_REQ * p_req, trt_GetCompatibleMetadataConfigurations_RES * p_res);

/**
 * onvif ptz service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_ptz_GetServiceCapabilities(ONVIF_DEVICE * p_dev, ptz_GetServiceCapabilities_REQ * p_req, ptz_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall implement this operation and return all PTZ nodes
 *  available on the device.
 *
 **/
HT_API BOOL onvif_ptz_GetNodes(ONVIF_DEVICE * p_dev, ptz_GetNodes_REQ * p_req, ptz_GetNodes_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall implement the GetNode operation and return the 
 *  properties of the requested PTZ node, if it exists. 
 *  Otherwise, the device shall respond with an appropriate fault message.
 *
 **/
HT_API BOOL onvif_ptz_GetNode(ONVIF_DEVICE * p_dev, ptz_GetNode_REQ * p_req, ptz_GetNode_RES * p_res);

/**
 * @brief
 *  The GetPresets operation returns the saved presets consisting of the 
 *  following elements:
 *
 *  Token - A unique identifier to reference the preset.
 *  Name - An optional mnemonic name.
 *  PTZ Position - An optional absolute position. 
 *  If the PTZ node supports absolute pan/tilt position spaces, the pan/tilt 
 *  position shall be specified. If the PTZ node supports absolute zoom 
 *  position spaces, the zoom position shall be specified.
 *
 **/
HT_API BOOL onvif_ptz_GetPresets(ONVIF_DEVICE * p_dev, ptz_GetPresets_REQ * p_req, ptz_GetPresets_RES * p_res);

/**
 * @brief
 *  The SetPreset command saves the current device position parameters so 
 *  that the device can move to the saved preset position through the 
 *  GotoPreset operation.
 *
 *  If the PresetToken parameter is absent, the device shall create a new
 *  preset. Otherwise it shall update the stored position and optionally 
 *  the name of the given preset. If creation is successful, the response
 *  contains the PresetToken which uniquely identifies the preset. 
 *  An existing preset can be overwritten by specifying the PresetToken of
 *  the corresponding preset. In both cases (overwriting or creation) an 
 *  optional PresetName can be specified. The operation fails if the PTZ 
 *  device is moving during the SetPreset operation.
 *
 *  The device may internally save additional states such as imaging 
 *  properties in the PTZ preset which then should be recalled
 *  in the GotoPreset operation. A device shall accept a valid 
 *  SetPresetRequest that does not include the optional element
 *  PresetName.
 *
 *  Devices may require unique preset names and reject a request that 
 *  contains an already existing PresetName by responding with the error
 *  message ter:PresetExist.
 *
 **/
HT_API BOOL onvif_ptz_SetPreset(ONVIF_DEVICE * p_dev, ptz_SetPreset_REQ * p_req, ptz_SetPreset_RES * p_res);

/**
 * @brief
 *  The RemovePreset operation removes a previously set preset.
 *
 **/
HT_API BOOL onvif_ptz_RemovePreset(ONVIF_DEVICE * p_dev, ptz_RemovePreset_REQ * p_req, ptz_RemovePreset_RES * p_res);

/**
 * @brief
 *  The GotoPreset operation recalls a previously set preset. If the speed 
 *  parameter is omitted, the default speed of the corresponding PTZ 
 *  configuration shall be used. The speed parameter can only be specified
 *  when speed spaces are available for the PTZ node. The GotoPreset command
 *  is a non-blocking operation and can be interrupted by other move commands.
 *
 **/
HT_API BOOL onvif_ptz_GotoPreset(ONVIF_DEVICE * p_dev, ptz_GotoPreset_REQ * p_req, ptz_GotoPreset_RES * p_res);

/**
 * @brief
 *  This operation moves the PTZ unit to its home position. If the speed 
 *  parameter is omitted, the default speed of the corresponding PTZ 
 *  configuration shall be used. The speed parameter can only be specified 
 *  when speed spaces are available for the PTZ node.The command is 
 *  non-blocking and can be interrupted by other move commands.
 *
 **/
HT_API BOOL onvif_ptz_GotoHomePosition(ONVIF_DEVICE * p_dev, ptz_GotoHomePosition_REQ * p_req, ptz_GotoHomePosition_RES * p_res);

/**
 * @brief
 *  The SetHome operation saves the current position parameters as the home
 *  position, so that the GotoHome operation can request that the device
 *  move to the home position.
 *
 *  The SetHomePosition command shall return with a failure if the "home" 
 *  position is fixed and cannot be overwritten. If the SetHomePosition is
 *  successful, it shall be possible to recall the home position with the
 *  GotoHomePosition command.
 *
 **/
HT_API BOOL onvif_ptz_SetHomePosition(ONVIF_DEVICE * p_dev, ptz_SetHomePosition_REQ * p_req, ptz_SetHomePosition_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall be able to report its PTZ status through 
 *  the GetStatus command.
 *
 *  The PTZ status contains the following information:
 *
 *  Position (optional) - Specifies the absolute position of the PTZ unit
 *  together with the space references. The default absolute spaces of the
 *  corresponding PTZ configuration shall be referenced within the position
 *  element. This information shall be present if the device signals support
 *  via the capability StatusPosition.
 *
 *  MoveStatus (optional) - Indicates if the pan/tilt/zoom device unit is 
 *  currently moving, idle or in an unknown state. This information shall be
 *  present if the device signals support via the capability MoveStatus. 
 *  The state Unknown shall not be used during normal operation, but is 
 *  reserved to initialization or error conditions.
 *
 *  Error (optional) - States a current PTZ error condition. This field
 *  shall be present if the MoveStatus signals Unkown.
 *
 *  UTC Time - Specifies the UTC time when this status was generated.
 *  
 **/
HT_API BOOL onvif_ptz_GetStatus(ONVIF_DEVICE * p_dev, ptz_GetStatus_REQ * p_req, ptz_GetStatus_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall support continuous movements. The velocity 
 *  argument of this command specifies a signed speed value for the pan, 
 *  tilt and zoom. The combined pan/tilt element is optional and the Zoom
 *  element itself is optional. If the pan/tilt element is omitted, the 
 *  current pan/tilt movement shall not be affected by this command. The 
 *  same holds for the zoom element. The spaces referenced within the 
 *  velocity element shall be velocity spaces supported by the PTZ node.
 *  If the space information is omitted for the velocity argument, the 
 *  corresponding default spaces of the PTZ configuration belonging to 
 *  the specified media profile is used. A device may support continuous 
 *  pan/tilt movements and/or continuous zoom movements by providing only
 *  velocity spaces for the supported cases.
 *
 *  An existing timeout argument overrides the DefaultPTZTimeout parameter
 *  of the corresponding PTZ configuration for this Move operation. 
 *  The timeout parameter specifies how long the PTZ node continues to move.
 *
 *  A device shall stop movement in a particular axis (Pan, Tilt, or Zoom) 
 *  when zero is sent as the ContinuousMove parameter for that axis. Stopping
 *  shall have the same effect independent of the velocity space referenced.
 *
 *  If the requested velocity leads to absolute positions which cannot be 
 *  reached, the PTZ node shall move to a reachable position along the border
 *  of its range. A typical application of the continuous move operation is
 *  controlling PTZ via joystick.
 *
 **/
HT_API BOOL onvif_ptz_ContinuousMove(ONVIF_DEVICE * p_dev, ptz_ContinuousMove_REQ * p_req, ptz_ContinuousMove_RES * p_res);

/**
 * @brief
 *  If a PTZ node supports relative pan/tilt or relative zoom movements, 
 *  then it shall support the RelativeMove operation. The translation 
 *  argument of this operation specifies the difference from the current
 *  position to the position to which the PTZ device is instructed to move. 
 *  The operation is split into an optional pan/tilt element and an optional
 *  zoom element. If the pan/tilt element is omitted, the current pan/tilt 
 *  movement shall NOT be affected by this command. The same holds for the 
 *  zoom element.
 *
 *  The spaces referenced within the translation element shall be translation
 *  spaces supported by the PTZ node. If the space information is omitted
 *  for the translation argument, the corresponding default spaces of the 
 *  PTZ configuration, which is part of the specified media profile, is used.
 *  A device may support relative pan/tilt movements, relative Zoom movements
 *  or no relative movements by providing only translation spaces for the 
 *  supported cases.
 *
 *  An existing speed argument overrides DefaultSpeed of the corresponding 
 *  PTZ configuration during movement by the requested translation. 
 *  If spaces are referenced within the speed argument, they shall be speed 
 *  spaces supported by the PTZ node.
 *  
 *  The command can be used to stop the PTZ unit at its current position by
 *  sending zero values for pan/tilt and zoom. Stopping shall have the very
 *  same effect independent of the relative space referenced.
 *
 *  If the requested translation leads to an absolute position which cannot
 *  be reached, the PTZ node shall move to a reachable position along the 
 *  border of valid positions.
 *
 **/
HT_API BOOL onvif_ptz_RelativeMove(ONVIF_DEVICE * p_dev, ptz_RelativeMove_REQ * p_req, ptz_RelativeMove_RES * p_res);

/**
 * @brief
 *  If a PTZ node supports absolute pan/tilt or absolute zoom movements, 
 *  it shall support the AbsoluteMove operation. Theposition argument of
 *  this command specifies the absolute position to which the PTZ unit 
 *  moves. It splits into an optional pan/tilt element and an optional 
 *  zoom element. If the pan/tilt position is omitted, the current 
 *  pan/tilt movement shall not be affected by this command. The same 
 *  holds for the zoom position.
 *
 *  The spaces referenced within the position shall be absolute position 
 *  spaces supported by the PTZ node. If the space information is omitted, 
 *  the corresponding default spaces of the PTZ configuration, a part of 
 *  the specified media profile, is used. A device may support absolute 
 *  pan/tilt movements, absolute zoom movements or no absolute movements by
 *  providing only absolute position spaces for the supported cases.
 *
 *  An existing Speed argument overrides DefaultSpeed of the corresponding 
 *  PTZ configuration during movement to the requested position. If spaces 
 *  are referenced within the Speed argument, they shall be speed spaces 
 *  supported by the PTZ node.
 *
 *  The operation shall fail if the requested absolute position is not reachable.
 *
 **/
HT_API BOOL onvif_ptz_AbsoluteMove(ONVIF_DEVICE * p_dev, ptz_AbsoluteMove_REQ * p_req, ptz_AbsoluteMove_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall support the Stop operation. If no stop filter 
 *  arguments are present, this command stops all ongoing pan, tilt and zoom
 *  movements. The Stop operation can be filtered to stop a specific movement
 *  by setting the corresponding stop argument.
 *
 **/
HT_API BOOL onvif_ptz_Stop(ONVIF_DEVICE * p_dev, ptz_Stop_REQ * p_req, ptz_Stop_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall return all available PTZConfigurations through
 *  the GetConfigurations operation.
 *
 **/
HT_API BOOL onvif_ptz_GetConfigurations(ONVIF_DEVICE * p_dev, ptz_GetConfigurations_REQ * p_req, ptz_GetConfigurations_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall return the requested PTZ configuration,
 *  if it exists, through the GetConfiguration operation.
 *
 **/
HT_API BOOL onvif_ptz_GetConfiguration(ONVIF_DEVICE * p_dev, ptz_GetConfiguration_REQ * p_req, ptz_GetConfiguration_RES * p_res);

/**
 * @brief
 *  A PTZ-capable device shall implement the SetConfiguration operation. 
 *  The ForcePersistence flag indicates if the changes remain after reboot
 *  of the device.
 *
 **/
HT_API BOOL onvif_ptz_SetConfiguration(ONVIF_DEVICE * p_dev, ptz_SetConfiguration_REQ * p_req, ptz_SetConfiguration_RES * p_res);

/**
 * @brief
 *  Returns the list of supported coordinate systems including their range
 *  limitations. Therefore, the options MAY differ depending on whether the
 *  PTZ configuration is assigned to a profile containing a 
 *  VideoSourceConfiguration. In this case, the options may contain additional
 *  coordinate systems referring to the image coordinate system described by
 *  the VideoSourceConfiguration. Each listed coordinate system belongs to one
 *  of the groups. If the PTZ node supports continuous movements, it shall 
 *  return a timeout range within which timeouts are accepted by the PTZ node.
 *
 **/
HT_API BOOL onvif_ptz_GetConfigurationOptions(ONVIF_DEVICE * p_dev, ptz_GetConfigurationOptions_REQ * p_req, ptz_GetConfigurationOptions_RES * p_res);

/**
 * @brief
 *  A device supporting Preset Tour feature shall return all available preset
 *  tours through GetPresetTours.
 *
 **/
HT_API BOOL onvif_ptz_GetPresetTours(ONVIF_DEVICE * p_dev, ptz_GetPresetTours_REQ * p_req, ptz_GetPresetTours_RES * p_res);

/**
 * @brief
 *  A device supporting preset tours shall return the requested preset tour 
 *  through GetPresetTour.
 *
 **/
HT_API BOOL onvif_ptz_GetPresetTour(ONVIF_DEVICE * p_dev, ptz_GetPresetTour_REQ * p_req, ptz_GetPresetTour_RES * p_res);

/**
 * @brief
 *  A device supporting preset tours shall provide options for how preset 
 *  tours can be configured through GetPresetTourOptions.
 *
 **/
HT_API BOOL onvif_ptz_GetPresetTourOptions(ONVIF_DEVICE * p_dev, ptz_GetPresetTourOptions_REQ * p_req, ptz_GetPresetTourOptions_RES * p_res);

/**
 * @brief
 *  A device supporting Preset Tour feature shall allow creating a new 
 *  Preset Tour through the CreatePresetTour.
 *
 **/
HT_API BOOL onvif_ptz_CreatePresetTour(ONVIF_DEVICE * p_dev, ptz_CreatePresetTour_REQ * p_req, ptz_CreatePresetTour_RES * p_res);

/**
 * @brief
 *  A device supporting preset tours shall allow modifying a preset tour 
 *  through ModifyPresetTour.
 *
 **/
HT_API BOOL onvif_ptz_ModifyPresetTour(ONVIF_DEVICE * p_dev, ptz_ModifyPresetTour_REQ * p_req, ptz_ModifyPresetTour_RES * p_res);

/**
 * @brief
 *  A device supporting preset tours shall allow starting, stopping, or 
 *  pausing a preset tour through OperatePresetTour.
 *
 *  Preset tour can be operated with the PresetTourOperation parameter
 *  of OperatePresetTour command.
 *  Start: indicates starting the preset tour or re-starting the paused preset tour.
 *  Stop: indicates stopping the preset tour.
 *  Pause:iIndicates pausing the preset tour.
 *
 *  When receiving another OperatePresetTour command of Start operation 
 *  for a preset tour which has already been started, the preset tour 
 *  shall be restarted with the newly requested parameter.
 *
 **/
HT_API BOOL onvif_ptz_OperatePresetTour(ONVIF_DEVICE * p_dev, ptz_OperatePresetTour_REQ * p_req, ptz_OperatePresetTour_RES * p_res);

/**
 * @brief
 *  A device supporting preset tours shall support removing preset tours
 *  through RemoevPresetTour.
 *
 **/
HT_API BOOL onvif_ptz_RemovePresetTour(ONVIF_DEVICE * p_dev, ptz_RemovePresetTour_REQ * p_req, ptz_RemovePresetTour_RES * p_res);

/**
 * @brief
 *  This operation is used to call an auxiliary operation on the device. 
 *  The supported commands can be retrieved via the PTZ node properties.
 *  The auxiliary command should match the supported command listed in the
 *  PTZ node; no other syntax is supported. If the PTZ node lists the 
 *  tt:IRLamp command, then the parameter of AuxiliaryCommand command shall
 *  conform to the syntax specified in Section 8.6 Auxiliary operation of 
 *  ONVIF Core Specification. The SendAuxiliaryCommand shall be implemented
 *  when the PTZ node supports auxiliary commands.
 *
 **/
HT_API BOOL onvif_ptz_SendAuxiliaryCommand(ONVIF_DEVICE * p_dev, ptz_SendAuxiliaryCommand_REQ * p_req, ptz_SendAuxiliaryCommand_RES * p_res);

/**
 * @brief
 *  A device signaling GeoMove in one of its PTZ nodes shall support this command.
 *
 *  The optional AreaHeight and AreaWidth parameters can be added to the 
 *  request, so that the PTZ-capable device can internally determine the 
 *  zoom factor. In case both AreaHeight and AreaWidth are not provided, 
 *  the unit will not change the zoom. AreaHeight and AreaWidth are 
 *  expressed in meters.
 *
 *  An existing speed argument overrides the DefaultSpeed of the corresponding
 *  PTZ configuration during movement by the requested translation. If spaces
 *  are referenced within the speed argument, they shall be speed spaces 
 *  supported by the PTZ node.
 *
 *  If the PTZ-capable device does not support automatic retrieval of the 
 *  geolocation, it shall be configured by using SetGeoLocation before it 
 *  can perform geo-referenced commands. If the client requests a GeoMove 
 *  command before the geolocation of the device is configured, the device 
 *  shall return an error.
 *
 *  Depending on the kinematics of the PTZ-capable device, the requested 
 *  position may not be reachable. In this situation the device shall return
 *  an error, signalling that it cannot perform the requested action due 
 *  to physical limitations.
 *
 **/
HT_API BOOL onvif_ptz_GeoMove(ONVIF_DEVICE * p_dev, ptz_GeoMove_REQ * p_req, ptz_GeoMove_RES * p_res);

/**
 * onvif event service interfaces
 **/

/**
 * @brief
 *  Get the event service capabilities.
 *
 **/
HT_API BOOL onvif_tev_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tev_GetServiceCapabilities_REQ * p_req, tev_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  An ONVIF compliant device shall implement this method to report supported
 *  event topics and provide information about the FilterDialects and schema 
 *  files.
 *
 *  Note, that configuration dependent events may not always be reported via 
 *  this interface, e.g. refer to GetSupportedRules specification regarding 
 *  analytics event definitions.
 *
 **/
HT_API BOOL onvif_tev_GetEventProperties(ONVIF_DEVICE * p_dev, tev_GetEventProperties_REQ * p_req, tev_GetEventProperties_RES * p_res);

/**
 * @brief
 *  An ONVIF compliant device shall support this command if it signals support
 *  for [WS-Base Notification] via the MaxNotificationProducers capability.
 *  The command shall at least support a Timeout of one minute. A device shall
 *  respond both parameters CurrentTime and TerminationTime as utc using 
 *  the 'Z' indicator.
 *
 **/
HT_API BOOL onvif_tev_Renew(ONVIF_DEVICE * p_dev, tev_Renew_REQ * p_req, tev_Renew_RES * p_res);

/**
 * @brief
 *  The device shall provide the following Unsubscribe command for all
 *  SubscriptionManager endpoints returned by the CreatePullPointSubscription
 *  command.
 *
 **/
HT_API BOOL onvif_tev_Unsubscribe(ONVIF_DEVICE * p_dev, tev_Unsubscribe_REQ * p_req, tev_Unsubscribe_RES * p_res);

/**
 * @brief
 *  Subscribe Event Notification.
 *
 **/
HT_API BOOL onvif_tev_Subscribe(ONVIF_DEVICE * p_dev, tev_Subscribe_REQ * p_req, tev_Subscribe_RES * p_res);

/**
 * @brief
 *  Pause Subscribe Event Notification.
 *
 **/
HT_API BOOL onvif_tev_PauseSubscription(ONVIF_DEVICE * p_dev, tev_PauseSubscription_REQ * p_req, tev_PauseSubscription_RES * p_res);

/**
 * @brief
 *  Resume Subscribe Event Notification.
 *
 **/
HT_API BOOL onvif_tev_ResumeSubscription(ONVIF_DEVICE * p_dev, tev_ResumeSubscription_REQ * p_req, tev_ResumeSubscription_RES * p_res);

/**
 * @brief
 *  An ONVIF compliant device shall provide the CreatePullPointSubscription command. 
 *  If no Filter element is specified the pullpoint shall notify all occurring 
 *  events to the client.
 *  
 *  By default the pull point keep alive is controlled via the PullMessages operation. 
 *  In this case, after a PullMessages response is returned, the subscription should
 *  be active for at least the timeout specified in the PullMessages request.
 *  
 *  A device shall support an absolute time value specified in utc as well as a 
 *  relative time value for the InitialTerminationTime parameter. A device shall 
 *  respond both parameters CurrentTime and TerminationTime as utc using the 'Z' indicator.
 *
 **/
HT_API BOOL onvif_tev_CreatePullPointSubscription(ONVIF_DEVICE * p_dev, tev_CreatePullPointSubscription_REQ * p_req, tev_CreatePullPointSubscription_RES * p_res);

/**
 * @brief
 *  Destroy pullpoint.
 *
 **/
HT_API BOOL onvif_tev_DestroyPullPoint(ONVIF_DEVICE * p_dev, tev_DestroyPullPoint_REQ * p_req, tev_DestroyPullPoint_RES * p_res);

/**
 * @brief
 *  The device shall provide the following PullMessages command for all 
 *  SubscriptionManager endpoints returned by the CreatePullPointSubscription 
 *  command.
 *
 *  The device shall support a Timeout of at least one minute. 
 *  The device shall not respond with a PullMessagesFaultResponse when the 
 *  MessageLimit is greater than the device supports. Instead, the device shall
 *  return up to the supported messages in the response.
 *
 *  The response behavior shall be one of three types:
 *  
 *  If there are one or more messages waiting (i.e., aggregated) when the request
 *  arrives, the device shall immediately respond with the waiting messages, 
 *  up to the MessageLimit. The device shall not discard unsent messages, but 
 *  shall await the next PullMessages request to send remaining messages.
 *
 *  If there are no messages waiting, and the device generates a message 
 *  (or multiple simultaneous messages) prior to reaching the Timeout, the device 
 *  shall immediately respond with the generated messages, up to the MessageLimit. 
 *  The device shall not wait for additional messages before returning the response.
 *
 *  If there are no messages waiting, and the device does not generate any message
 *  prior to reaching the Timeout, the device shall respond with zero messages. 
 *  The device shall not return a response with zero messages prior to reaching the 
 *  Timeout.
 *
 *  A device shall respond both parameters CurrentTime and TerminationTime as utc 
 *  using the 'Z' indicator.
 *
 *  After a seek operation the device shall return the messages in strict message 
 *  utc time order. Note that this requirement is not applicable to standard realtime
 *  message delivery where the delivery order may be affected by device internal 
 *  computations.
 *  
 *  A device should return an error (UnableToGetMessagesFault) when receiving a 
 *  PullMessages request for a subscription where a blocking PullMessage request 
 *  already exists.
 *
 **/
HT_API BOOL onvif_tev_PullMessages(ONVIF_DEVICE * p_dev, tev_PullMessages_REQ * p_req, tev_PullMessages_RES * p_res);

/**
 * @brief
 *  Get messages.
 *
 **/
HT_API BOOL onvif_tev_GetMessages(ONVIF_DEVICE * p_dev, tev_GetMessages_REQ * p_req, tev_GetMessages_RES * p_res);

/**
 * @brief
 *  A device supporting persistent notification storage shall provide the 
 *  following Seek command for all SubscriptionManager endpoints returned
 *  by the CreatePullPointSubscription command.
 *
 *  On a Seek a pullpoint shall abort any event delivery including any 
 *  initial states of properties. Furthermore the pullpoint should flush 
 *  events not already queued for transmission from the transmit queue.
 *
 **/
HT_API BOOL onvif_tev_Seek(ONVIF_DEVICE * p_dev, tev_Seek_REQ * p_req, tev_Seek_RES * p_res);

/**
 * @brief
 *  When a client wants to synchronize its properties with the properties of 
 *  the device, it can request a synchronization point which repeats the 
 *  current status of all properties to which a client has subscribed.
 *
 *  The Synchronization Point is requested directly from the SubscriptionManager
 *  which was returned in either the SubscriptionResponse or in the 
 *  CreatePullPointSubscriptionResponse. The property update is transmitted via
 *  the notification transportation of the notification interface.
 *
 **/
HT_API BOOL onvif_tev_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, tev_SetSynchronizationPoint_REQ * p_req, tev_SetSynchronizationPoint_RES * p_res);

/**
 * onvif imaging service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_img_GetServiceCapabilities(ONVIF_DEVICE * p_dev, img_GetServiceCapabilities_REQ * p_req, img_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests the imaging setting for a video source on the device.
 *
 **/
HT_API BOOL onvif_img_GetImagingSettings(ONVIF_DEVICE * p_dev, img_GetImagingSettings_REQ * p_req, img_GetImagingSettings_RES * p_res);

/**
 * @brief
 *  Sets the imaging settings for a video source on a device.
 *  A device indicating support for the AdaptablePreset capability shall 
 *  apply the same settings to the current active preset
 *
 **/
HT_API BOOL onvif_img_SetImagingSettings(ONVIF_DEVICE * p_dev, img_SetImagingSettings_REQ * p_req, img_SetImagingSettings_RES * p_res);

/**
 * @brief
 *  Gets the valid ranges for the imaging parameters that have device 
 *  specific ranges.
 *
 *  The command shall return all supported parameters and their ranges
 *  such that these can be applied to the SetImagingSettings command.
 *
 *  For read-only parameters which cannot be modified via the 
 *  SetImagingSettings command only a single option or identical Min and
 *  Max values shall be provided.
 *
 **/
HT_API BOOL onvif_img_GetOptions(ONVIF_DEVICE * p_dev, img_GetOptions_REQ * p_req, img_GetOptions_RES * p_res);

/**
 * @brief
 *  Moves the focus lens in an absolute, a relative or in a continuous manner 
 *  from its current position. The speed argument is optional for absolute 
 *  and relative control, but required for continuous. If no speed argument 
 *  is used, the default speed is used. Focus adjustments through this operation 
 *  will turn off the autofocus. A device with support for remote focus control
 *  should support absolute, relative or continuous control through the Move operation.
 *
 *  At least one focus control capability is required for this operation to be functional.
 *
 *  The move operation contains the following commands:
 *  Absolute - Requires position parameter and optionally takes a speed argument. 
 *  A unitless type is used by default for focus positioning and speed. 
 *  Relative - Requires distance parameter and optionally takes a speed argument. 
 *  Negative distance means negative direction.
 *  Continuous - Requires a speed argument. Negative speed argument means negative 
 *  direction.
 *
 **/
HT_API BOOL onvif_img_Move(ONVIF_DEVICE * p_dev, img_Move_REQ * p_req, img_Move_RES * p_res);

/**
 * @brief
 *  Stops all ongoing focus movements of the lense.
 *  The operation will not affect ongoing autofocus operation.
 *
 **/
HT_API BOOL onvif_img_Stop(ONVIF_DEVICE * p_dev, img_Stop_REQ * p_req, img_Stop_RES * p_res);

/**
 * @brief
 *  Requests the current imaging status from the device.
 *
 **/
HT_API BOOL onvif_img_GetStatus(ONVIF_DEVICE * p_dev, img_GetStatus_REQ * p_req, img_GetStatus_RES * p_res);

/**
 * @brief
 *  Retrieves the focus lens move options to be used in the move command.
 *
 *  The response to the command shall include all supported Move Operations. 
 *  If focus move is not supported at all, the reponse shall be empty.
 *
 **/
HT_API BOOL onvif_img_GetMoveOptions(ONVIF_DEVICE * p_dev, img_GetMoveOptions_REQ * p_req, img_GetMoveOptions_RES * p_res);

/**
 * @brief
 *  Requests the current predefined list of Imaging Settings (Presets) 
 *  offered by the manufacturer for a given video source.
 *  The output is a list containing the available Imaging Presets.
 *  A device indicating the ImagingPresets capability shall support this
 *  command.
 *
 **/
HT_API BOOL onvif_img_GetPresets(ONVIF_DEVICE * p_dev, img_GetPresets_REQ * p_req, img_GetPresets_RES * p_res);

/**
 * @brief
 *  Request the Imaging Preset which is currently applied to the specified
 *  video source, i.e. it shall request which of the predefined set of 
 *  Imaging Settings (and/or Thermal Settings) was last applied to the video
 *  source. The output is the current Imaging Preset.
 *
 *  If the video source configuration does not match any of the existing
 *  Imaging Presets, the output of GetCurrentPreset shall be Empty.
 *
 *  A device indicating the ImagingPresets capability shall support this
 *  command.
 *
 **/
HT_API BOOL onvif_img_GetCurrentPreset(ONVIF_DEVICE * p_dev, img_GetCurrentPreset_REQ * p_req, img_GetCurrentPreset_RES * p_res);

/**
 * @brief
 *  Request a given Imaging Preset to be applied to the specified video source.
 *
 *  A device indicating the ImagingPresets capability shall support this command.
 *
 *  Imaging Presets are defined by the Manufacturer, and offered as a tool to 
 *  simplify Imaging Settings adjustments for specific scene patterns. When the 
 *  new Imaging Preset is applied by SetCurrentPreset, as a response, the device 
 *  shall adjust the video source settings to match those values defined by the 
 *  specified Imaging Preset.
 *
 **/
HT_API BOOL onvif_img_SetCurrentPreset(ONVIF_DEVICE * p_dev, img_SetCurrentPreset_REQ * p_req, img_SetCurrentPreset_RES * p_res);

/**
 * onvif device IO service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_tmd_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tmd_GetServiceCapabilities_REQ * p_req, tmd_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Gets a list of all available relay outputs and their settings.
 *
 **/
HT_API BOOL onvif_tmd_GetRelayOutputs(ONVIF_DEVICE * p_dev, tmd_GetRelayOutputs_REQ * p_req, tmd_GetRelayOutputs_RES * p_res);

/**
 * @brief
 *  Request the available settings and ranges for one or all relay outputs. 
 *  The method shall return the information for exactly one output when a 
 *  RelayOutputToken is provided as request parameter. Otherwise the method 
 *  shall return the information for all relay outputs.
 *
 *  A device that has one or more RelayOutputs should support this command.
 *
 **/
HT_API BOOL onvif_tmd_GetRelayOutputOptions(ONVIF_DEVICE * p_dev, tmd_GetRelayOutputOptions_REQ * p_req, tmd_GetRelayOutputOptions_RES * p_res);

/**
 * @brief
 *  Sets the settings of a relay output.
 *
 *  The relay can work in two relay modes:
 *  Bistable - After setting the state, the relay remains in this state.
 *  Monostable - After setting the state, the relay returns to its idle 
 *  state after the specified time.
 *
 *  The physical idle state of a relay output can be configured by setting
 *  the IdleState to 'open' or 'closed' (inversion of the relay behaviour).
 *
 *  Idle State 'open' means that the relay is open when the relay state is 
 *  set to 'inactive' through the trigger command and closed when the state
 *  is set to 'active' through the same command.
 *
 *  Idle State 'closed' means, that the relay is closed when the relay state
 *  is set to 'inactive' through the trigger command and open when the state 
 *  is set to 'active' through the same command.
 *
 *  The Duration parameter of the Properties field "DelayTime" describes the
 *  time after which the relay returns to its idle state if it is in monostable
 *  mode. If the relay is set to bistable mode the value of the parameter shall
 *  be ignored.
 *
 **/
HT_API BOOL onvif_tmd_SetRelayOutputSettings(ONVIF_DEVICE * p_dev, tmd_SetRelayOutputSettings_REQ * p_req, tmd_SetRelayOutputSettings_RES * p_res);

/**
 * @brief
 *  Triggers a relay output.
 *
 **/
HT_API BOOL onvif_tmd_SetRelayOutputState(ONVIF_DEVICE * p_dev, tmd_SetRelayOutputState_REQ * p_req, tmd_SetRelayOutputState_RES * p_res);

/**
 * @brief
 *  Lists all available digital inputs of a device. 
 *  A device that signals support for digital inputs via its capabilities
 *  shall support listing of available inputs through the GetDigitalInputs 
 *  command. 
 *
 **/
HT_API BOOL onvif_tmd_GetDigitalInputs(ONVIF_DEVICE * p_dev, tmd_GetDigitalInputs_REQ * p_req, tmd_GetDigitalInputs_RES * p_res);

/**
 * @brief
 *  Retrieves the digital input configuration options when the digital input
 *  configuration token is known. If a specific digital input is specified, 
 *  the options shall concern that particular configuration. If a token is
 *  not specified, the options shall be considered generic for the device. 
 *  
 *  A device shall support the GetDigitalInputConfigurationOptions command 
 *  if the device signals capability of digital input configuration via 
 *  DigitalInputOptions capability.
 *
 **/
HT_API BOOL onvif_tmd_GetDigitalInputConfigurationOptions(ONVIF_DEVICE * p_dev, tmd_GetDigitalInputConfigurationOptions_REQ * p_req, tmd_GetDigitalInputConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies existing digital input configurations. 
 *  When applying multiple configuration settings, the expected behaviour
 *  is to configure all or none. If one of the provided configurations is
 *  invalid, the expected behaviour of the device is to apply none of the
 *  configurations and an indication in the return fault which digital
 *  input configuration has not been accepted.
 *
 **/
HT_API BOOL onvif_tmd_SetDigitalInputConfigurations(ONVIF_DEVICE * p_dev, tmd_SetDigitalInputConfigurations_REQ * p_req, tmd_SetDigitalInputConfigurations_RES * p_res);

/**
 * @brief
 *  Lists all available serial ports of a device.
 *
 **/
HT_API BOOL onvif_tmd_GetSerialPorts(ONVIF_DEVICE * p_dev, tmd_GetSerialPorts_REQ * p_req, tmd_GetSerialPorts_RES * p_res);

/**
 * @brief
 *  Gets the configuration of a serial port.
 *
 **/
HT_API BOOL onvif_tmd_GetSerialPortConfiguration(ONVIF_DEVICE * p_dev, tmd_GetSerialPortConfiguration_REQ * p_req, tmd_GetSerialPortConfiguration_RES * p_res);

/**
 * @brief
 *  Sets the setting of serial port.
 *
 **/
HT_API BOOL onvif_tmd_SetSerialPortConfiguration(ONVIF_DEVICE * p_dev, tmd_SetSerialPortConfiguration_REQ * p_req, tmd_SetSerialPortConfiguration_RES * p_res);

/**
 * @brief
 *  Requests the SerialPortConfigurationOptions of a SerialPort.
 *
 **/
HT_API BOOL onvif_tmd_GetSerialPortConfigurationOptions(ONVIF_DEVICE * p_dev, tmd_GetSerialPortConfigurationOptions_REQ * p_req, tmd_GetSerialPortConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Transmit/receive generic controlling data to/from a serial device that is
 *  connected to the serial port of the device.
 *
 *  This operation can be used for the following purposes.
 *  Transmitting arbitrary data to the connected serial device
 *  Receiving data from the connected serial device
 *  Transmitting arbitrary data to the connected serial device and then receiving
 *  its response data
 *
 **/
HT_API BOOL onvif_tmd_SendReceiveSerialCommand(ONVIF_DEVICE * p_dev, tmd_SendReceiveSerialCommand_REQ * p_req, tmd_SendReceiveSerialCommand_RES * p_res);

/**
 * onvif recording service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_trc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trc_GetServiceCapabilities_REQ * p_req, trc_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Create a new recording
 *
 *  This method is optional. It shall be available if the 
 *  Recording/DynamicRecordings capability is TRUE.
 *
 **/
HT_API BOOL onvif_trc_CreateRecording(ONVIF_DEVICE * p_dev, trc_CreateRecording_REQ * p_req, trc_CreateRecording_RES * p_res);

/**
 * @brief
 *  delete a recording object. Whenever a recording is deleted, 
 *  the device shall delete all the tracks that are part of the 
 *  recording, and it shall delete all the Recording Jobs that 
 *  record into the recording. For each deleted recording job, 
 *  the device shall also delete all the receiver objects associated 
 *  with the recording jobthat are automatically created using 
 *  the AutoCreateReceiver field of the recording job configuration 
 *  structure and are not used in any other recording job.
 *
 *  This method is optional. It shall be available if the 
 *  Recording/DynamicRecordings capability is TRUE.
 *
 **/
HT_API BOOL onvif_trc_DeleteRecording(ONVIF_DEVICE * p_dev, trc_DeleteRecording_REQ * p_req, trc_DeleteRecording_RES * p_res);

/**
 * @brief
 *  Return a description of all the recordings in the device. 
 *  This description shall include a list of all the tracks for each recording.
 *
 **/
HT_API BOOL onvif_trc_GetRecordings(ONVIF_DEVICE * p_dev, trc_GetRecordings_REQ * p_req, trc_GetRecordings_RES * p_res);

/**
 * @brief
 *  Change the configuration of a recording
 *
 **/
HT_API BOOL onvif_trc_SetRecordingConfiguration(ONVIF_DEVICE * p_dev, trc_SetRecordingConfiguration_REQ * p_req, trc_SetRecordingConfiguration_RES * p_res);

/**
 * @brief
 *  Retrieve the recording configuration for a recording.
 *
 **/
HT_API BOOL onvif_trc_GetRecordingConfiguration(ONVIF_DEVICE * p_dev, trc_GetRecordingConfiguration_REQ * p_req, trc_GetRecordingConfiguration_RES * p_res);

/**
 * @brief
 *  Returns information for a recording identified by the RecordingToken. 
 *  The information includes the number of additional tracks as well as 
 *  recording jobs that can be configured.
 *  This method shall be supported if the Options support is signaled 
 *  via the capabilities.
 *  Note that this information is not static and is only guaranteed to 
 *  be valid until the next modification of any recording jobs or tracks.
 *  The track options shall be supported if the device signals support 
 *  for dynamic tracks.
 *
 **/
HT_API BOOL onvif_trc_GetRecordingOptions(ONVIF_DEVICE * p_dev, trc_GetRecordingOptions_REQ * p_req, trc_GetRecordingOptions_RES * p_res);

/**
 * @brief
 *  Create a new track within a recording if the method GetRecordingOptions 
 *  signals spare tracks for the recording. For a track to be created the 
 *  SpareXXX (where XXX is the track type) needs to be set.
 *
 *  This method is optional. It shall be available if the 
 *  Recording/DynamicTracks capability is TRUE.
 *
 **/
HT_API BOOL onvif_trc_CreateTrack(ONVIF_DEVICE * p_dev, trc_CreateTrack_REQ * p_req, trc_CreateTrack_RES * p_res);

/**
 * @brief
 *  Remove a track from a recording. All the data in the track shall be deleted.
 *
 *  This method is optional. It shall be available if the 
 *  Recording/DynamicTracks capability is TRUE.
 *
 **/
HT_API BOOL onvif_trc_DeleteTrack(ONVIF_DEVICE * p_dev, trc_DeleteTrack_REQ * p_req, trc_DeleteTrack_RES * p_res);

/**
 * @brief
 *  Retrieve the configuration for a specific track.
 *
 **/
HT_API BOOL onvif_trc_GetTrackConfiguration(ONVIF_DEVICE * p_dev, trc_GetTrackConfiguration_REQ * p_req, trc_GetTrackConfiguration_RES * p_res);

/**
 * @brief 
 *  Change the configuration of a track. TrackType shall be 
 *  ignored by the device as it can't be changed
 *
 **/
HT_API BOOL onvif_trc_SetTrackConfiguration(ONVIF_DEVICE * p_dev, trc_SetTrackConfiguration_REQ * p_req, trc_SetTrackConfiguration_RES * p_res);

/**
 * @brief
 *  Create a new recording job. A device shall support adding a 
 *  RecordingJob to a recording for which it signals Spare jobs 
 *  via GetRecordingOptions.
 *
 *  A device should reject a configuration that neither includes 
 *  a source with a source token nor AutoCreateReceiver set to true.
 *
 *  If the configuration doesn not include any tracks a device should 
 *  assign all tracks of the corresponding recording.
 *
 **/
HT_API BOOL onvif_trc_CreateRecordingJob(ONVIF_DEVICE * p_dev, trc_CreateRecordingJob_REQ * p_req, trc_CreateRecordingJob_RES * p_res);

/**
 * @brief
 *  Removes a recording job. It shall also implicitly delete all 
 *  the receiver objects associated with the recording job that 
 *  are automatically created using the AutoCreateReceiver field 
 *  of the recording job configuration structure and are not used 
 *  in any other recording job.
 *
 **/
HT_API BOOL onvif_trc_DeleteRecordingJob(ONVIF_DEVICE * p_dev, trc_DeleteRecordingJob_REQ * p_req, trc_DeleteRecordingJob_RES * p_res);

/**
 * @brief
 *  Return a list of all the recording jobs in the device.
 *
 **/
HT_API BOOL onvif_trc_GetRecordingJobs(ONVIF_DEVICE * p_dev, trc_GetRecordingJobs_REQ * p_req, trc_GetRecordingJobs_RES * p_res);

/**
 * @brief
 *  Change the configuration for a recording job. A device shall 
 *  reject a request that tries to modify the RecordingToken.
 *
 **/
HT_API BOOL onvif_trc_SetRecordingJobConfiguration(ONVIF_DEVICE * p_dev, trc_SetRecordingJobConfiguration_REQ * p_req, trc_SetRecordingJobConfiguration_RES * p_res);

/**
 * @brief
 *  Return the current configuration for a recording job.
 *
 **/
HT_API BOOL onvif_trc_GetRecordingJobConfiguration(ONVIF_DEVICE * p_dev, trc_GetRecordingJobConfiguration_REQ * p_req, trc_GetRecordingJobConfiguration_RES * p_res);

/**
 * @brief
 *  Change the mode of the recording job. Using this method shall be 
 *  equivalent to retrieving the recording job configuration, 
 *  and writing it back with a different mode.
 *
 *  Note that the state of a recording job will only become active 
 *  if the recording job has the highest priority of all active jobs 
 *  of a recording.
 *
 **/
HT_API BOOL onvif_trc_SetRecordingJobMode(ONVIF_DEVICE * p_dev, trc_SetRecordingJobMode_REQ * p_req, trc_SetRecordingJobMode_RES * p_res);

/**
 * @brief
 *  Returns the state of a recording job. It includes an aggregated state, 
 *  and state for each track of the recording job. The RecordingJogState 
 *  may change due to
 *  calls that effect the RecordingJobMode, e.g. SetRecordingJobMode,
 *  internal recording engine state changes,
 *  changes in the recorded local media profile or
 *  changes to the RTSP connection defined by the associated Receiver.
 *
 **/
HT_API BOOL onvif_trc_GetRecordingJobState(ONVIF_DEVICE * p_dev, trc_GetRecordingJobState_REQ * p_req, trc_GetRecordingJobState_RES * p_res);

/**
 * @brief 
 *  Exports the selected recordings to the given storage target
 *
 **/
HT_API BOOL onvif_trc_ExportRecordedData(ONVIF_DEVICE * p_dev, trc_ExportRecordedData_REQ * p_req, trc_ExportRecordedData_RES * p_res);

/**
 * @brief 
 *  Stops the ExportRecordedData operation that is started before. 
 *  The response message lists the status of the exported files.
 *
 **/
HT_API BOOL onvif_trc_StopExportRecordedData(ONVIF_DEVICE * p_dev, trc_StopExportRecordedData_REQ * p_req, trc_StopExportRecordedData_RES * p_res);

/**
 * @brief
 *  Returns the status of export operations. This interface allows 
 *  client to poll the status information from the device.
 *
 **/
HT_API BOOL onvif_trc_GetExportRecordedDataState(ONVIF_DEVICE * p_dev, trc_GetExportRecordedDataState_REQ * p_req, trc_GetExportRecordedDataState_RES * p_res);

/**
 * onvif replay service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_trp_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trp_GetServiceCapabilities_REQ * p_req, trp_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a URI that can be used to initiate playback of a recorded 
 *  stream using RTSP as the control protocol
 *
 **/
HT_API BOOL onvif_trp_GetReplayUri(ONVIF_DEVICE * p_dev, trp_GetReplayUri_REQ * p_req, trp_GetReplayUri_RES * p_res);

/**
 * @brief
 *   Returns the current configuration of the replay service
 *
 **/
HT_API BOOL onvif_trp_GetReplayConfiguration(ONVIF_DEVICE * p_dev, trp_GetReplayConfiguration_REQ * p_req, trp_GetReplayConfiguration_RES * p_res);

/**
 * @brief
 *  Changes the configuration of the replay service
 *
 **/
HT_API BOOL onvif_trp_SetReplayConfiguration(ONVIF_DEVICE * p_dev, trp_SetReplayConfiguration_REQ * p_req, trp_SetReplayConfiguration_RES * p_res);

/**
 * onvif search service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_tse_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tse_GetServiceCapabilities_REQ * p_req, tse_GetServiceCapabilities_RES * p_res);

/**
 * @brief 
 *  Get a summary description of all recorded data
 *
 * @return 
 *  The possible return values:
 *  ONVIF_OK
 **/
HT_API BOOL onvif_tse_GetRecordingSummary(ONVIF_DEVICE * p_dev, tse_GetRecordingSummary_REQ * p_req, tse_GetRecordingSummary_RES * p_res);

/**
 * @brief
 *  Returns information about a single Recording specified by a RecordingToken
 *
 **/
HT_API BOOL onvif_tse_GetRecordingInformation(ONVIF_DEVICE * p_dev, tse_GetRecordingInformation_REQ * p_req, tse_GetRecordingInformation_RES * p_res);

/**
 * @brief 
 *  Returns a set of media attributes for all tracks of the 
 *  specified recordings at a specified point in time
 *
 **/
HT_API BOOL onvif_tse_GetMediaAttributes(ONVIF_DEVICE * p_dev, tse_GetMediaAttributes_REQ * p_req, tse_GetMediaAttributes_RES * p_res);

/**
 * @brief 
 *  Starts a search session, looking for recordings that match the 
 *  scope (See 5.2.4) defined in the request. Results from the search 
 *  are acquired using the GetRecordingSearchResults request, 
 *  specifying the search token returned from this request
 *  
 *  The device shall continue searching until one of the following occurs:
 *  The total number of matches has been found, defined by the MaxMatches parameter
 *  The session has been ended by a client EndSearch request
 *  The session has been ended because KeepAliveTime since the last 
 *  request related to this session has expired.
 *
 *  The order of the results is undefined, to allow the device to return 
 *  results in any order they are found. This operation is mandatory to 
 *  support for a device implementing the recording search service.
 *  For the KeepAliveTime a device shall support at least values up to 
 *  ten seconds. A device may adapt larger values.
 *
 **/
HT_API BOOL onvif_tse_FindRecordings(ONVIF_DEVICE * p_dev, tse_FindRecordings_REQ * p_req, tse_FindRecordings_RES * p_res);

/**
 * @brief
 *  Acquires the results from a recording search session previously 
 *  initiated by a FindRecordings operation. The response shall not 
 *  include results already returned in previous requests for the
 *  same session. If MaxResults is specified, the response shall not 
 *  contain more than MaxResults results. The number of results relates 
 *  to the number of recordings. For viewing individual recorded data 
 *  for a signal track use the FindEvents method.
 *
 **/
HT_API BOOL onvif_tse_GetRecordingSearchResults(ONVIF_DEVICE * p_dev, tse_GetRecordingSearchResults_REQ * p_req, tse_GetRecordingSearchResults_RES * p_res);

/**
 * @brief
 *  Starts a search session, looking for events in the scope (See 5.2.4) 
 *  that match the search filter defined in the request. Events are recording 
 *  events (see 5.2.2) and other events that are available in the track.
 *  Results from the search are acquired using the GetEventSearchResults request, 
 *  specifying the search token returned from this request.
 *
 *  The device shall continue searching until one of the following occurs:
 *  The entire time range from StartPoint to EndPoint has been searched through.
 *  The total number of matches has been found, defined by the MaxMatches parameter.
 *  The session has been ended by a client EndSearch request.
 *  The session has been ended because KeepAliveTime since the last request 
 *  related to this session has expired.
 *
 *  Results shall be ordered by time, ascending in case of forward search, 
 *  or descending in case of backward search. This operation is mandatory 
 *  to support for a device implementing the recording search service. 
 *  Although the values of property events refer to the forward direction, 
 *  they shall be reported identically in reverse search mode.
 *
 *  For the KeepAliveTime a device shall support at least values up to 
 *  ten seconds. A device may adapt larger values.
 *
 **/
HT_API BOOL onvif_tse_FindEvents(ONVIF_DEVICE * p_dev, tse_FindEvents_REQ * p_req, tse_FindEvents_RES * p_res);

/**
 * @brief
 *  acquires the results from a recording event search session 
 *  previously initiated by a FindEvents operation. The response 
 *  shall not include results already returned in previous requests 
 *  for the same session. If MaxResults is specified, the response 
 *  shall not contain more than MaxResults results.
 *
 **/
HT_API BOOL onvif_tse_GetEventSearchResults(ONVIF_DEVICE * p_dev, tse_GetEventSearchResults_REQ * p_req, tse_GetEventSearchResults_RES * p_res);

/**
 * @brief
 *  Starts a search session, looking for metadata in the scope (See 5.2.4) 
 *  that matches the search filter defined in the request. Results 
 *  from the search are acquired using the GetMetadataSearchResults request, 
 *  specifying the search token returned from this request.
 *
 *  The device shall continue searching until one of the following occurs:
 *  The entire time range from StartPoint to EndPoint has been searched through.
 *  The total number of matches has been found, defined by the MaxMatches parameter.
 *  The session has been ended by a client EndSearch request.
 *  The session has been ended because KeepAliveTime since the last request 
 *  related to this session has expired.
 *
 *  This operation is mandatory to support if the MetaDataSearch capability 
 *  is set to true in the SearchCapabilities structure return by the 
 *  GetCapabilities command in the Device service.
 *
 *  For the KeepAliveTime a device shall support at least values up to ten seconds. 
 *  A device may adapt larger values.
 *
 **/
HT_API BOOL onvif_tse_FindMetadata(ONVIF_DEVICE * p_dev, tse_FindMetadata_REQ * p_req, tse_FindMetadata_RES * p_res);

/**
 * @brief
 *  Acquires the results from a recording search session previously 
 *  initiated by a FindMetadata operation. The response shall not 
 *  include results already returned in previous requests for the same
 *  session. If MaxResults is specified, the response shall not contain 
 *  more than MaxResults results.
 *
 **/
HT_API BOOL onvif_tse_GetMetadataSearchResults(ONVIF_DEVICE * p_dev, tse_GetMetadataSearchResults_REQ * p_req, tse_GetMetadataSearchResults_RES * p_res);

/**
 * @brief 
 *  Starts a search session, looking for ptz positions in the scope (See 5.2.4) 
 *  that matches the search filter defined in the request. Results from the 
 *  search are acquired using the GetPTZPositionSearchResults request, 
 *  specifying the search token returned from this request.
 *
 *  The device shall continue searching until one of the following occurs:
 *  The entire time range from StartPoint to EndPoint has been searched through.
 *  The total number of matches has been found, defined by the MaxMatches parameter.
 *  The session has been ended by a client EndSearch request.
 *  The session has been ended because KeepAliveTime since the last request
 *  related to this session has expired.
 *
 *  This operation is mandatory to support whenever CanContainPTZ is true
 *  for any metadata track in any recording on the device.
 *
 *  For the KeepAliveTime a device shall support at least values up to 
 *  ten seconds. A device may adapt larger values.
 *
 *  A device shall only match the search criteria against PTZ status updates 
 *  available between the time interval given in the search, i.e. the device 
 *  shall not locate the PTZ position at the start of the search interval.
 *
 **/
HT_API BOOL onvif_tse_FindPTZPosition(ONVIF_DEVICE * p_dev, tse_FindPTZPosition_REQ * p_req, tse_FindPTZPosition_RES * p_res);

/**
 * @brief 
 *  Acquires the results from a PTZ position search session previously 
 *  initiated by a FindPTZPosition operation. The response shall not 
 *  include results already returned in previous requests for the same 
 *  session. If MaxResults is specified, the response shall not 
 *  contain more than MaxResults results.
 *
 **/
HT_API BOOL onvif_tse_GetPTZPositionSearchResults(ONVIF_DEVICE * p_dev, tse_GetPTZPositionSearchResults_REQ * p_req, tse_GetPTZPositionSearchResults_RES * p_res);

/**
 * @return
 *  Get the search state.
 *
 **/
HT_API BOOL onvif_tse_GetSearchState(ONVIF_DEVICE * p_dev, tse_GetSearchState_REQ * p_req, tse_GetSearchState_RES * p_res);

/**
 * @brief 
 *  Stops an ongoing search session, causing any blocking result 
 *  request to return and the SearchToken to become invalid.
 *  If the search was interrupted before completion, the point 
 *  in time that the search had reached shall be returned. 
 *  If the search had not yet begun, the StartPoint shall be returned. 
 *  Note that an error  message will occur if the search session 
 *  has been already completed before this request. If the search was
 *  completed the original EndPoint supplied by the Find operation 
 *  shall be returned. When issuing EndSearch on a FindRecordings 
 *  request the EndPoint is undefined and shall not be used since 
 *  the FindRecordings request doesn't have StartPoint/EndPoint.
 *
 **/
HT_API BOOL onvif_tse_EndSearch(ONVIF_DEVICE * p_dev, tse_EndSearch_REQ * p_req, tse_EndSearch_RES * p_res);

/**
 * onvif analytics service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_tan_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tan_GetServiceCapabilities_REQ * p_req, tan_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  A device signaling support for rules via the RuleSupport capability shall 
 *  support this operation. It returns a list of rule descriptions according 
 *  to the Rule Description Language.Additionally, it contains a list of URLs 
 *  that provide the location of the schema files. These schema files describe 
 *  the types and elements used in the rule descriptions. Rule descriptions 
 *  that reference types or elements imported from any ONVIF defined schema 
 *  files need not explicitly list those schema files. The device shall indicate
 *  its limit for maximum number of rules through the maxInstances attribute.
 *
 **/
HT_API BOOL onvif_tan_GetSupportedRules(ONVIF_DEVICE * p_dev, tan_GetSupportedRules_REQ * p_req, tan_GetSupportedRules_RES * p_res);

/**
 * @brief
 *  A device signaling support for rules via the RuleSupport capability shall 
 *  support this operation to add rules to an AnalyticsConfiguration.
 *
 *  If all rules can not be created as requested, the device responds with
 *  a fault message.
 *
 *  The device shall accept adding of analytics rules with an empty Parameter
 *  definition. Note that the resulted configuration may include a set of 
 *  default parameter values.
 *
 **/
HT_API BOOL onvif_tan_CreateRules(ONVIF_DEVICE * p_dev, tan_CreateRules_REQ * p_req, tan_CreateRules_RES * p_res);

/**
 * @brief
 *  A device signaling support for rules via the RuleSupport capability shall
 *  support this operation to delete one or more rules. If all rules can not
 *  be deleted as requested, the device responds with a fault message.
 *
 **/
HT_API BOOL onvif_tan_DeleteRules(ONVIF_DEVICE * p_dev, tan_DeleteRules_REQ * p_req, tan_DeleteRules_RES * p_res);

/**
 * @brief
 *  A device signaling support for rules via the RuleSupport capability shall
 *  support this operation to retrieve the currently associated rules with a 
 *  video analytics configuration.
 *
 **/
HT_API BOOL onvif_tan_GetRules(ONVIF_DEVICE * p_dev, tan_GetRules_REQ * p_req, tan_GetRules_RES * p_res);

/**
 * @brief
 *  A device signaling support for modifying rules via the RuleOptionsSupported
 *  capability shall support this operation. If all rules can not be modified 
 *  as requested, the device responds with a fault message. A device may reject
 *  a request to change the rule type.
 *
 *  A device should interpret parameters not present in the payload as unchanged. 
 *  Note that this does not always result in unchanged values of such parameters, 
 *  since some parameters may change due to dependency on others.
 *
 **/
HT_API BOOL onvif_tan_ModifyRules(ONVIF_DEVICE * p_dev, tan_ModifyRules_REQ * p_req, tan_ModifyRules_RES * p_res);

/**
 * @brief
 *  A device signaling support for analytics modules via the 
 *  AnalyticsModuleSupport capability shall support this method to add 
 *  analytics modules to an analytics configuration.
 *
 *  The device shall accept adding of analytics modules with an empty 
 *  Parameter definition. Note that the resulted configuration may include
 *  a set of default parameter values.
 *
 **/
HT_API BOOL onvif_tan_CreateAnalyticsModules(ONVIF_DEVICE * p_dev, tan_CreateAnalyticsModules_REQ * p_req, tan_CreateAnalyticsModules_RES * p_res);

/**
 * @breif
 *  A device signaling support for analytics modules via the 
 *  AnalyticsModuleSupport capability shall support this method for removing
 *  analytics module from an analytics configuration.
 *
 **/
HT_API BOOL onvif_tan_DeleteAnalyticsModules(ONVIF_DEVICE * p_dev, tan_DeleteAnalyticsModules_REQ * p_req, tan_DeleteAnalyticsModules_RES * p_res);

/**
 * @brief
 *  A device signaling support for analytics modules via the 
 *  AnalyticsModuleSupport capability shall support this method to retrieve
 *  currently associated analytics modules for an analytics configuration.
 *
 **/
HT_API BOOL onvif_tan_GetAnalyticsModules(ONVIF_DEVICE * p_dev, tan_GetAnalyticsModules_REQ * p_req, tan_GetAnalyticsModules_RES * p_res);

/**
 * @brief
 *  A device signaling support for analytics modules via the 
 *  AnalyticsModuleOptionsSupported capability shall support this method
 *  to modify analytics module configurations. A device may reject a 
 *  request to change the module type.
 *
 **/
HT_API BOOL onvif_tan_ModifyAnalyticsModules(ONVIF_DEVICE * p_dev, tan_ModifyAnalyticsModules_REQ * p_req, tan_ModifyAnalyticsModules_RES * p_res);

/**
 * @brief
 *  A device signaling support for analytics modules via the 
 *  AnalyticsModuleSupport capability shall support retrieving a list of 
 *  analytics modules for an analytics configuration. The description shall
 *  conform to the configuration description language.
 *  
 *  The optional AnalyticsModuleContentSchemaLocation parameter allows to
 *  list schema file locations that provide reference to types or elements
 *  which are not defined by ONVIF.
 *
 *  The device shall indicate its limit for maximum number of analytics 
 *  modules through the maxInstances attribute.
 *
 **/
HT_API BOOL onvif_tan_GetSupportedAnalyticsModules(ONVIF_DEVICE * p_dev, tan_GetSupportedAnalyticsModules_REQ * p_req, tan_GetSupportedAnalyticsModules_RES * p_res);

/**
 * @brief
 *  A device signaling support for modifying rules via the 
 *  RuleOptionsSupported capability shall support this operation to 
 *  retrieve the options for the supported rules that specify an 
 *  Option attribute.
 *
 **/
HT_API BOOL onvif_tan_GetRuleOptions(ONVIF_DEVICE * p_dev, tan_GetRuleOptions_REQ * p_req, tan_GetRuleOptions_RES * p_res);

/**
 * @brief
 *  Returns the options for the supported analytics modules that 
 *  specify an Option attribute.
 *  A device signaling support for the AnalyticsModuleOptionsSupported
 *  capability shall support this method.
 *
 **/
HT_API BOOL onvif_tan_GetAnalyticsModuleOptions(ONVIF_DEVICE * p_dev, tan_GetAnalyticsModuleOptions_REQ * p_req, tan_GetAnalyticsModuleOptions_RES * p_res);

/**
 * @brief
 *  This operation allows to query what metadata a device can generate. 
 *  It shall be supported if the SupportedMetadata capability is set to true.
 *
 *  The response contains the following information:
 *  SampleFrame [tt:Frame]
 *  An example Frame instance. The instance shall include all elements that 
 *  the analytics module outputs to the frame of a metadata stream. A device 
 *  implementation should strive for including all supported enumeration values.
 *
 *  If the sample frame includes object bounding boxes or shapes, these shall
 *  be located in the top left quarter of the image.
 *
 **/
HT_API BOOL onvif_tan_GetSupportedMetadata(ONVIF_DEVICE * p_dev, tan_GetSupportedMetadata_REQ * p_req, tan_GetSupportedMetadata_RES * p_res);

/**
 * onvif media 2 service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/
HT_API BOOL onvif_tr2_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tr2_GetServiceCapabilities_REQ * p_req, tr2_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Get video encoder configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderConfigurations_REQ * p_req, tr2_GetVideoEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetVideoEncoderConfiguration_REQ * p_req, tr2_SetVideoEncoderConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_tr2_GetVideoEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderConfigurationOptions_REQ * p_req, tr2_GetVideoEncoderConfigurationOptions_RES * p_res);

/**
 * @brief
 *  An endpoint can ask for the existing media profiles of a device using the
 *  GetProfiles command. Both preconfigured and dynamically created profiles
 *  can be retrieved using this command.
 *  
 *  The token parameter controls which profiles are returned:
 *  If no Token is provided this command lists all configured profiles of a device.
 *  If a Token is provided the command either lists the referenced profile or
 *  responds with an error.
 *
 *  The Type parameter controls which configurations are returned and has no 
 *  effect on the number of profiles returned:
 *  If no Type is provided the returned profiles shall contain no configuration
 *  information.
 *  If a single Type with value 'All' is provided the returned profiles shall
 *  include all associated configurations.
 *  Otherwise the requested list of configurations shall for each profile 
 *  include the configurations present as Type.
 *
 **/
HT_API BOOL onvif_tr2_GetProfiles(ONVIF_DEVICE * p_dev, tr2_GetProfiles_REQ * p_req, tr2_GetProfiles_RES * p_res);

/**
 * @brief
 *  Creates a new media profile. The media profile shall be created in the device.
 *
 *  A created profile shall be deletable and a device shall set the "fixed" 
 *  attribute to false in the returned Profile.
 *
 **/
HT_API BOOL onvif_tr2_CreateProfile(ONVIF_DEVICE * p_dev, tr2_CreateProfile_REQ * p_req, tr2_CreateProfile_RES * p_res);

/**
 * @brief
 *  Deletes a profile.
 *
 *  A device signaling support for MultiTrackStreaming shall support deleting
 *  of virtual profiles via the command. Note that deleting a profile of a 
 *  virtual profile set may invalidate the virtual profile.
 *
 **/
HT_API BOOL onvif_tr2_DeleteProfile(ONVIF_DEVICE * p_dev, tr2_DeleteProfile_REQ * p_req, tr2_DeleteProfile_RES * p_res);

/**
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
 *  multitrack RTSP session URI by passing a virtual profile token.
 *  A device signaling support for SecureRTSPStreaming shall support streaming
 *  via SRTP.
 *
 **/
HT_API BOOL onvif_tr2_GetStreamUri(ONVIF_DEVICE * p_dev, tr2_GetStreamUri_REQ * p_req, tr2_GetStreamUri_RES * p_res);

/**
 * @brief
 *  Get video cource configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceConfigurations_REQ * p_req, tr2_GetVideoSourceConfigurations_RES * p_res);

/**
 * @brief
 *  Get video cource configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetVideoSourceConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceConfigurationOptions_REQ * p_req, tr2_GetVideoSourceConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, tr2_SetVideoSourceConfiguration_REQ * p_req, tr2_SetVideoSourceConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_tr2_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, tr2_SetSynchronizationPoint_REQ * p_req, tr2_SetSynchronizationPoint_RES * p_res);

/**
 * @brief
 *  Get metadata configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetMetadataConfigurations(ONVIF_DEVICE * p_dev, tr2_GetMetadataConfigurations_REQ * p_req, tr2_GetMetadataConfigurations_RES * p_res);

/**
 * @brief
 *  Get metadata configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetMetadataConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetMetadataConfigurationOptions_REQ * p_req, tr2_GetMetadataConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetMetadataConfiguration(ONVIF_DEVICE * p_dev, tr2_SetMetadataConfiguration_REQ * p_req, tr2_SetMetadataConfiguration_RES * p_res);

/**
 * @brief
 *  Get audio encoder configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioEncoderConfigurations_REQ * p_req, tr2_GetAudioEncoderConfigurations_RES * p_res);

/**
 * @brief
 *  Get audio source configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioSourceConfigurations_REQ * p_req, tr2_GetAudioSourceConfigurations_RES * p_res);

/**
 * @brief
 *  Get audio source configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioSourceConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioSourceConfigurationOptions_REQ * p_req, tr2_GetAudioSourceConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioSourceConfiguration_REQ * p_req, tr2_SetAudioSourceConfiguration_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioEncoderConfiguration_REQ * p_req, tr2_SetAudioEncoderConfiguration_RES * p_res);

/**
 * @brief
 *  Get audio encoder configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioEncoderConfigurationOptions_REQ * p_req, tr2_GetAudioEncoderConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Adds one or more configurations to an existing media profile. 
 *  If one of the configuration already exists in the media profile, 
 *  it will be replaced.
 *
 **/
HT_API BOOL onvif_tr2_AddConfiguration(ONVIF_DEVICE * p_dev, tr2_AddConfiguration_REQ * p_req, tr2_AddConfiguration_RES * p_res);

/**
 * @brief
 *  Removes one or more configurations from an existing media profile. 
 *  Tokens appearing in the configuration list shall be ignored. 
 *  Presence of the "All" type shall result in an empty profile. 
 *  Removing a non existing configuration shall be ignored and not 
 *  result in an error.
 *  
 **/
HT_API BOOL onvif_tr2_RemoveConfiguration(ONVIF_DEVICE * p_dev, tr2_RemoveConfiguration_REQ * p_req, tr2_RemoveConfiguration_RES * p_res);

/**
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
 *  for which the GetVideoEncoderInstances shall return a Total greater than 0.
 *  The total sum of video encoder instances over all video source configurations 
 *  of a device shall not exceed the value signaled via MaximumNumberOfProfiles.
 *
 *  For example, if a device has two VideoSourceConfigurations and if the first 
 *  allows a total of two concurrent instances and the second allows only one 
 *  instance, this device shall allow creation of at least three media profiles.
 *
 **/
HT_API BOOL onvif_tr2_GetVideoEncoderInstances(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderInstances_REQ * p_req, tr2_GetVideoEncoderInstances_RES * p_res);

/**
 * @brief
 *  Get audio output configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioOutputConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioOutputConfigurations_REQ * p_req, tr2_GetAudioOutputConfigurations_RES * p_res);

/**
 * @brief
 *  Get audio output configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioOutputConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioOutputConfigurationOptions_REQ * p_req, tr2_GetAudioOutputConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioOutputConfiguration_REQ * p_req, tr2_SetAudioOutputConfiguration_RES * p_res);

/**
 * @brief
 *  Get audio decoder configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioDecoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioDecoderConfigurations_REQ * p_req, tr2_GetAudioDecoderConfigurations_RES * p_res);

/**
 * @brief
 *  Get audio decoder configuration options.
 *
 **/
HT_API BOOL onvif_tr2_GetAudioDecoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioDecoderConfigurationOptions_REQ * p_req, tr2_GetAudioDecoderConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Modifies a configuration. 
 *  The change may have immediate effect to running streams but the changes
 *  are not guaranteed to take effect unless the client restarts any affected
 *  stream.
 *
 **/
HT_API BOOL onvif_tr2_SetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioDecoderConfiguration_REQ * p_req, tr2_SetAudioDecoderConfiguration_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_tr2_GetSnapshotUri(ONVIF_DEVICE * p_dev, tr2_GetSnapshotUri_REQ * p_req, tr2_GetSnapshotUri_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_tr2_StartMulticastStreaming(ONVIF_DEVICE * p_dev, tr2_StartMulticastStreaming_REQ * p_req, tr2_StartMulticastStreaming_RES * p_res);

/**
 * @brief
 *  Stops multicast streaming using a specified media profile of a device. 
 *  In case a device receives a StopMulticastStreaming request whose 
 *  corresponding multicast streaming is not started, the device should
 *  reply with successful StopMulticastStreamingResponse.
 *
 **/
HT_API BOOL onvif_tr2_StopMulticastStreaming(ONVIF_DEVICE * p_dev, tr2_StopMulticastStreaming_REQ * p_req, tr2_StopMulticastStreaming_RES * p_res);

/**
 * @brief
 *  Returns the information for current video source mode and settable 
 *  video source modes of specified video source. 
 *  A device that indicates a capability of VideoSourceMode shall support
 *  this command.
 *
 **/
HT_API BOOL onvif_tr2_GetVideoSourceModes(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceModes_REQ * p_req, tr2_GetVideoSourceModes_RES * p_res);

/**
 * @brief
 *  Changes the media profile structure relating to video source for 
 *  the specified video source mode. A device that indicates a capability
 *  of VideoSourceMode shall support this command. The behavior after 
 *  changing the mode is not defined in this specification.
 *
 **/
HT_API BOOL onvif_tr2_SetVideoSourceMode(ONVIF_DEVICE * p_dev, tr2_SetVideoSourceMode_REQ * p_req, tr2_SetVideoSourceMode_RES * p_res);

/**
 * @brief
 *  Creates a new OSD configuration with specified values and also makes
 *  the association between the new OSD and an existing VideoSourceConfiguration
 *  identified by the VideoSourceConfigurationToken.
 *  Any value required by a device for a new OSD configuration that is optional
 *  and not present in the CreateOSD message may be adapted to the appropriate
 *  value by the device. A device that indicates OSD capability shall support 
 *  the creation of OSD as long as the number of existing OSDs does not exceed
 *  the value of MaximumNumberOfOSDs in GetOSDOptions. 
 *  A created OSD configuration shall be deletable.
 *
 **/
HT_API BOOL onvif_tr2_CreateOSD(ONVIF_DEVICE * p_dev, tr2_CreateOSD_REQ * p_req, tr2_CreateOSD_RES * p_res);

/**
 * @brief
 *  Deletes an OSD configuration.
 *
 **/
HT_API BOOL onvif_tr2_DeleteOSD(ONVIF_DEVICE * p_dev, tr2_DeleteOSD_REQ * p_req, tr2_DeleteOSD_RES * p_res);

/**
 * @brief
 *  Lists existing OSD configurations for the device. 
 *  If neither an OSD token nor a video source configuration token is provided
 *  the device shall respond with all available OSD configurations.
 *
 **/
HT_API BOOL onvif_tr2_GetOSDs(ONVIF_DEVICE * p_dev, tr2_GetOSDs_REQ * p_req, tr2_GetOSDs_RES * p_res);

/**
 * @brief
 *  Modifies an OSD configuration. Running streams using this configuration 
 *  may be immediately updated according to the new settings.
 *
 *  A device shall accept any combination of parameters returned by GetOSDOptions.
 *  If necessary the device may adapt parameter values for FontColor, FontSize, 
 *  and BackgroundColor elements without returning an error.
 *  Modifying the configuration token is not supported.
 *
 **/
HT_API BOOL onvif_tr2_SetOSD(ONVIF_DEVICE * p_dev, tr2_SetOSD_REQ * p_req, tr2_SetOSD_RES * p_res);

/**
 * @brief
 *  Returns the available options when the OSD parameters are reconfigured.
 *  The device shall support the listing of available OSD parameter options 
 *  (for a given video source configuration) through the GetOSDOptions command. 
 *  Any combination of the parameters obtained using a given video source 
 *  configuration shall be a valid input for the corresponding SetOSD command.
 *
 **/
HT_API BOOL onvif_tr2_GetOSDOptions(ONVIF_DEVICE * p_dev, tr2_GetOSDOptions_REQ * p_req, tr2_GetOSDOptions_RES * p_res);

/**
 * @brief
 *  Get analytics configurations list.
 *
 **/
HT_API BOOL onvif_tr2_GetAnalyticsConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAnalyticsConfigurations_REQ * p_req, tr2_GetAnalyticsConfigurations_RES * p_res);

/**
 * @brief
 *  Lists existing Mask configurations for the device. 
 *  A device signaling support for the Mask capability shall support the 
 *  listing of existing Mask configurations through this command. 
 *  In case neither a mask nor configuration tokens is provided the device
 *  shall respond with all available Mask configurations in the device.
 *
 **/
HT_API BOOL onvif_tr2_GetMasks(ONVIF_DEVICE * p_dev, tr2_GetMasks_REQ * p_req, tr2_GetMasks_RES * p_res);

/**
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
 **/
HT_API BOOL onvif_tr2_SetMask(ONVIF_DEVICE * p_dev, tr2_SetMask_REQ * p_req, tr2_SetMask_RES * p_res);

/**
 * @brief
 *  Creates a new Mask for an existing VideoSourceConfiguration. 
 *  A device that signals support for Masks by the Mask capability shall
 *  support the creation of masks via this function as long as the number of
 *  existing masks does not exceed the value of MaxMasks for the given 
 *  VideoSourceConfiguration.
 *  
 **/
HT_API BOOL onvif_tr2_CreateMask(ONVIF_DEVICE * p_dev, tr2_CreateMask_REQ * p_req, tr2_CreateMask_RES * p_res);

/**
 * @brief
 *  Deletes a mask configuration.
 *
 **/
HT_API BOOL onvif_tr2_DeleteMask(ONVIF_DEVICE * p_dev, tr2_DeleteMask_REQ * p_req, tr2_DeleteMask_RES * p_res);

/**
 * @brief
 *  Returns the available options when the Mask parameters are reconfigured. 
 *  A device signaling support for Mask via its capabilities shall support
 *  the listing of available Mask parameter options (for a given video source
 *  configuration) via this command. 
 *  Any combination of the parameters obtained using a given video source
 *  configuration shall be a valid input for the corresponding SetMask command.
 *
 **/
HT_API BOOL onvif_tr2_GetMaskOptions(ONVIF_DEVICE * p_dev, tr2_GetMaskOptions_REQ * p_req, tr2_GetMaskOptions_RES * p_res);

/**
 * access control service interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the access control service.
 *
 **/
HT_API BOOL onvif_tac_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tac_GetServiceCapabilities_REQ * p_req, tac_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a list of all AccessPointInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available.
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tac_GetAccessPointInfoList(ONVIF_DEVICE * p_dev, tac_GetAccessPointInfoList_REQ * p_req, tac_GetAccessPointInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of AccessPointInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty
 *  list if there are no items matching the specified tokens. The device shall
 *  not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be returned.
 *
 **/
HT_API BOOL onvif_tac_GetAccessPointInfo(ONVIF_DEVICE * p_dev, tac_GetAccessPointInfo_REQ * p_req, tac_GetAccessPointInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all AccessPoint items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more data is available.
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 *  A device that signals support for the AccessPointManagementSupported 
 *  capability shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_GetAccessPointList(ONVIF_DEVICE * p_dev, tac_GetAccessPointList_REQ * p_req, tac_GetAccessPointList_RES * p_res);

/**
 * @brief
 *  Requests a list of AccessPoint items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty
 *  list if there are no items matching the specified tokens. The device shall
 *  not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be returned.
 *
 **/
HT_API BOOL onvif_tac_GetAccessPoints(ONVIF_DEVICE * p_dev, tac_GetAccessPoints_REQ * p_req, tac_GetAccessPoints_RES * p_res);

/**
 * @brief
 *  Creates the specified access point in the device.
 *
 *  The token field of the AccessPoint structure shall be empty and the 
 *  device shall allocate a token for the access point. The allocated 
 *  token shall be returned in the response.
 *
 *  If the client sends any value in the token field, the device shall
 *  return InvalidArgVal as a generic fault code.
 *
 **/
HT_API BOOL onvif_tac_CreateAccessPoint(ONVIF_DEVICE * p_dev, tac_CreateAccessPoint_REQ * p_req, tac_CreateAccessPoint_RES * p_res);

/**
 * @brief
 *  Synchronize an access point in a client with the device.
 *
 *  If an access point with the specified token does not exist in the device, 
 *  the access point is created. If an access point with the specified token
 *  exists, then the access point is modified.
 *
 *  A call to this method takes an AccessPoint structure as input parameter. 
 *  The token field of the AccessPoint structure shall not be empty.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tac_SetAccessPoint(ONVIF_DEVICE * p_dev, tac_SetAccessPoint_REQ * p_req, tac_SetAccessPoint_RES * p_res);

/**
 * @brief
 *  Modifies the specified access point.
 *
 *  The token of the access point to modify is specified in the token field
 *  of the AccessPoint structure and shall not be empty. All other fields 
 *  in the structure shall overwrite the fields in the specified access point.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tac_ModifyAccessPoint(ONVIF_DEVICE * p_dev, tac_ModifyAccessPoint_REQ * p_req, tac_ModifyAccessPoint_RES * p_res);

/**
 * @brief
 *  Deletes the specified access point.
 *
 *  If it is associated with one or more entities some devices may not be
 *  able to delete the access point, and consequently a ReferenceInUse fault
 *  shall be generated.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tac_DeleteAccessPoint(ONVIF_DEVICE * p_dev, tac_DeleteAccessPoint_REQ * p_req, tac_DeleteAccessPoint_RES * p_res);

/**
 * @brief
 *  Requests a list of all AreaInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more data is available.
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tac_GetAreaInfoList(ONVIF_DEVICE * p_dev, tac_GetAreaInfoList_REQ * p_req, tac_GetAreaInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of AreaInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty
 *  list if there are no items matching the specified tokens. The device shall
 *  not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be returned.
 *
 **/
HT_API BOOL onvif_tac_GetAreaInfo(ONVIF_DEVICE * p_dev, tac_GetAreaInfo_REQ * p_req, tac_GetAreaInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all Area items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available.
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter
 *
 *  A device that signals support for the AreaManagementSupported capability
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_GetAreaList(ONVIF_DEVICE * p_dev, tac_GetAreaList_REQ * p_req, tac_GetAreaList_RES * p_res);

/**
 * @brief
 *  Requests a list of Area items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty 
 *  list if there are no items matching the specified tokens. The device shall 
 *  not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be returned.
 *
 *  A device that signals support for the AreaManagementSupported capability 
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_GetAreas(ONVIF_DEVICE * p_dev, tac_GetAreas_REQ * p_req, tac_GetAreas_RES * p_res);

/**
 * @brief
 *  Creates the specified area in the device.
 *
 *  The token field of the Area structure shall be empty and the device 
 *  shall allocate a token for the area. The allocated token shall be
 *  returned in the response.
 *
 *  If the client sends any value in the token field, the device shall
 *  return InvalidArgVal as a generic fault code.
 *
 *  A device that signals support for the AreaManagementSupported 
 *  capability shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_CreateArea(ONVIF_DEVICE * p_dev, tac_CreateArea_REQ * p_req, tac_CreateArea_RES * p_res);

/**
 * @brief
 *  Synchronize an area in a client with the device.
 *
 *  If an area with the specified token does not exist in the device, 
 *  the area is created. If an area with the specified token exists, 
 *  then the area is modified.
 *
 *  A call to this method takes an Area structure as input parameter. 
 *  The token field of the Area structure shall not be empty.
 *
 *  A device that signals support for the ClientSuppliedTokenSupported 
 *  capability shall implement this command.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tac_SetArea(ONVIF_DEVICE * p_dev, tac_SetArea_REQ * p_req, tac_SetArea_RES * p_res);

/**
 * @brief
 *  Modifies the specified area.
 *
 *  The token of the area to modify is specified in the token field of the 
 *  Area structure and shall not be empty. All other fields in the 
 *  structure shall overwrite the fields in the specified area.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 *  A device that signals support for the AreaManagementSupported capability
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_ModifyArea(ONVIF_DEVICE * p_dev, tac_ModifyArea_REQ * p_req, tac_ModifyArea_RES * p_res);

/**
 * @brief
 *  Deletes the specified area.
 *
 *  If it is associated with one or more entities some devices may not be 
 *  able to delete the area, and consequently a ReferenceInUse fault shall
 *  be generated.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 *  A device that signals support for the AreaManagementSupported capability
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_DeleteArea(ONVIF_DEVICE * p_dev, tac_DeleteArea_REQ * p_req, tac_DeleteArea_RES * p_res);

/**
 * @brief
 *  Requests the AccessPointState for the access point instance specified
 *  by the token.
 *
 **/
HT_API BOOL onvif_tac_GetAccessPointState(ONVIF_DEVICE * p_dev, tac_GetAccessPointState_REQ * p_req, tac_GetAccessPointState_RES * p_res);

/**
 * @brief
 *  Enabling an access point.
 *
 *  A device that signals support for DisableAccessPoint capability for
 *  a particular access point instance shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_EnableAccessPoint(ONVIF_DEVICE * p_dev, tac_EnableAccessPoint_REQ * p_req, tac_EnableAccessPoint_RES * p_res);

/**
 * @brief
 *  Disabling an access point.
 *
 *  A device that signals support for the DisableAccessPoint capability for 
 *  a particular access point instance shall implement this command.
 *
 **/
HT_API BOOL onvif_tac_DisableAccessPoint(ONVIF_DEVICE * p_dev, tac_DisableAccessPoint_REQ * p_req, tac_DisableAccessPoint_RES * p_res);

/**
 * door control service interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the door control service.
 *
 **/
HT_API BOOL onvif_tdc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tdc_GetServiceCapabilities_REQ * p_req, tdc_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a list of all DoorInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more data is available. The reference shall be valid 
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tdc_GetDoorInfoList(ONVIF_DEVICE * p_dev, tdc_GetDoorInfoList_REQ * p_req, tdc_GetDoorInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of DoorInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty
 *  list if there are no items matching the specified tokens. The device shall
 *  not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems 
 *  fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_GetDoorInfo(ONVIF_DEVICE * p_dev, tdc_GetDoorInfo_REQ * p_req, tdc_GetDoorInfo_RES * p_res);

/**
 * @brief
 *  Requests the state of a door specified by the Token.
 *
 **/
HT_API BOOL onvif_tdc_GetDoorState(ONVIF_DEVICE * p_dev, tdc_GetDoorState_REQ * p_req, tdc_GetDoorState_RES * p_res);

/**
 * @brief
 *  This operation allows momentarily accessing a door. It invokes the
 *  functionality typically used when a card holder presents a card to 
 *  a card reader at the door and is granted access.
 *
 *  The DoorMode shall change to Accessed state.
 *
 *  The door shall remain accessible for the defined time. When the time
 *  span elapses, the DoorMode shall change back to its previous state.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 *  A device that signals support for Access capability for a particular 
 *  Door instance shall support this method.
 *
 *  The device shall take the best effort approach for parameters not 
 *  supported, it must fallback to preconfigured time or limit the time to 
 *  the closest supported time if the specified time is out of range.
 *
 **/
HT_API BOOL onvif_tdc_AccessDoor(ONVIF_DEVICE * p_dev, tdc_AccessDoor_REQ * p_req, tdc_AccessDoor_RES * p_res);

/**
 * @brief
 *  This operation allows locking a door. The door mode shall change to 
 *  Locked state.
 *
 *  A device that signals support for Lock capability for a particular 
 *  Door instance shall support this command.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_LockDoor(ONVIF_DEVICE * p_dev, tdc_LockDoor_REQ * p_req, tdc_LockDoor_RES * p_res);

/**
 * @brief
 *  This operation allows unlocking a door. The door mode shall change to 
 *  Unlocked state.
 *
 *  A device that signals support for Unlock capability for a particular 
 *  Door instance support this command.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_UnlockDoor(ONVIF_DEVICE * p_dev, tdc_UnlockDoor_REQ * p_req, tdc_UnlockDoor_RES * p_res);

/**
 * @brief
 *  This operation is used for securely locking a door. 
 *  A call to this method shall change door mode state to DoubleLocked.
 *
 *  A device that signals support for DoubleLock capability for a 
 *  particular Door instance shall support this command. Otherwise this 
 *  method can be performed as a standard Lock operation.
 *
 *  If the door has an extra lock that shall be locked as well.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_DoubleLockDoor(ONVIF_DEVICE * p_dev, tdc_DoubleLockDoor_REQ * p_req, tdc_DoubleLockDoor_RES * p_res);

/**
 * @brief
 *  This operation allows blocking a door and preventing momentary access.
 *  The door mode shall change to Blocked state.
 *
 *  A device that signals support for Block capability for a particular 
 *  Door instance shall support this command.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_BlockDoor(ONVIF_DEVICE * p_dev, tdc_BlockDoor_REQ * p_req, tdc_BlockDoor_RES * p_res);

/**
 * @brief
 *  This operation allows locking and preventing other actions until a 
 *  LockDownRelease command is invoked. 
 *  The DoorMode shall change to LockedDown state.
 *  
 *  The device shall ignore other door control commands until a 
 *  LockDownRelease command is performed.
 *
 *  A device that signals support for LockDown capability for a 
 *  particular Door instance shall support this command.
 *
 *  If a device supports DoubleLock capability for a particular Door 
 *  instance, that operation may be engaged as well.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_LockDownDoor(ONVIF_DEVICE * p_dev, tdc_LockDownDoor_REQ * p_req, tdc_LockDownDoor_RES * p_res);

/**
 * @brief
 *  This operation allows releasing the LockedDown state of a door. 
 *  The door mode shall change back to its previous/next state. 
 *  It is not defined what the previous/next state shall be, but 
 *  typically the Locked state. A device that signals support for 
 *  LockDown capability for a particular Door instance shall support
 *  this command.
 *
 *  This method shall only succeed if the current door mode is LockedDown.
 *
 **/
HT_API BOOL onvif_tdc_LockDownReleaseDoor(ONVIF_DEVICE * p_dev, tdc_LockDownReleaseDoor_REQ * p_req, tdc_LockDownReleaseDoor_RES * p_res);

/**
 * @brief
 *  This operation allows unlocking a door and preventing other actions
 *  until LockOpenRelease method is invoked. 
 *  The door mode shall change to LockedOpen state. 
 *
 *  The device shall ignore other door control commands until a 
 *  LockOpenRelease command is performed.
 *
 *  A device that signals support for LockOpen capability for a particular
 *  Door instance shall support this command.
 *
 *  If the request cannot be fulfilled, a Failure fault shall be returned.
 *
 **/
HT_API BOOL onvif_tdc_LockOpenDoor(ONVIF_DEVICE * p_dev, tdc_LockOpenDoor_REQ * p_req, tdc_LockOpenDoor_RES * p_res);

/**
 * @brief
 *  This operation allows releasing the LockedOpen state of a door. 
 *  The door mode shall change state from the LockedOpen state back 
 *  to its previous/next state. It is not defined what the previous/next
 *  state shall be, but typically the Unlocked state. A device that 
 *  signals support for LockOpen capability for a particular Door 
 *  instance shall support this command.
 *
 *  This method shall only succeed if the current DoorMode is LockedOpen.
 *
 **/
HT_API BOOL onvif_tdc_LockOpenReleaseDoor(ONVIF_DEVICE * p_dev, tdc_LockOpenReleaseDoor_REQ * p_req, tdc_LockOpenReleaseDoor_RES * p_res);

/**
 * @brief
 *  Requests a list of Door items matching the given tokens. 
 *
 *  The device shall ignore tokens it cannot resolve and shall return an empty
 *  list if there are no items matching specified tokens. The device shall not
 *  return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be returned.
 *
 *  A device that signals support for the DoorManagementSupported capability 
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tdc_GetDoors(ONVIF_DEVICE * p_dev, tdc_GetDoors_REQ * p_req, tdc_GetDoors_RES * p_res);

/**
 * @brief
 *  Requests a list of all Door items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available. The reference shall be valid 
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *  
 *  A device that signals support for the DoorManagementSupported capability 
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tdc_GetDoorList(ONVIF_DEVICE * p_dev, tdc_GetDoorList_REQ * p_req, tdc_GetDoorList_RES * p_res);

/**
 * @brief
 *  Creates the specified door in the device.
 *
 *  The token field of the Door structure shall be empty and the device 
 *  shall allocate a token for the door. The allocated token shall be 
 *  returned in the response.
 *
 *  If the client sends any value in the token field, the device shall
 *  return InvalidArgVal as a generic fault code.
 *
 *  A device that signals support for the DoorManagementSupported 
 *  capability shall implement this command.
 *
 **/
HT_API BOOL onvif_tdc_CreateDoor(ONVIF_DEVICE * p_dev, tdc_CreateDoor_REQ * p_req, tdc_CreateDoor_RES * p_res);

/**
 * @brief
 *  Synchronize a door in a client with the device.
 *
 *  If a door with the specified token does not exist in the device, 
 *  the door is created. If a door with the specified token exists, 
 *  then the door is modified.
 *
 *  A call to this method takes a door structure as input parameter.
 *  The token field of the Door structure shall not be empty.
 *
 *  A device that signals support for the ClientSuppliedTokenSupported 
 *  capability shall implement this command.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tdc_SetDoor(ONVIF_DEVICE * p_dev, tdc_SetDoor_REQ * p_req, tdc_SetDoor_RES * p_res);

/**
 * @brief
 *  Modifies the specified door.
 *
 *  The token of the door to modify is specified in the token field of the
 *  Door structure and shall not be empty. All other fields in the structure
 *  shall overwrite the fields in the specified door.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 *  A device that signals support for the DoorManagementSupported 
 *  capability shall implement this command.
 *
 **/
HT_API BOOL onvif_tdc_ModifyDoor(ONVIF_DEVICE * p_dev, tdc_ModifyDoor_REQ * p_req, tdc_ModifyDoor_RES * p_res);

/**
 * @brief
 *  Deletes the specified door.
 *
 *  If it is associated with one or more entities some devices may not be
 *  able to delete the door, and consequently a ReferenceInUse fault shall
 *  be generated.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 *  A device that signals support for the DoorManagementSupported capability
 *  shall implement this command.
 *
 **/
HT_API BOOL onvif_tdc_DeleteDoor(ONVIF_DEVICE * p_dev, tdc_DeleteDoor_REQ * p_req, tdc_DeleteDoor_RES * p_res);

/**
 * thermal service interfaces
 **/

/**
 * @brief
 *  The capabilities reflect optional functions and functionality of a service. 
 *  The information is static and does not change during device operation.
 *
 **/ 
HT_API BOOL onvif_tth_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tth_GetServiceCapabilities_REQ * p_req, tth_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests the thermal imaging settings for all thermal video sources on 
 *  the device.
 *
 **/
HT_API BOOL onvif_tth_GetConfigurations(ONVIF_DEVICE * p_dev, tth_GetConfigurations_REQ * p_req, tth_GetConfigurations_RES * p_res);

/**
 * @brief
 *  Requests the thermal imaging settings for a thermal video source on the
 *  device.
 *
 **/
HT_API BOOL onvif_tth_GetConfiguration(ONVIF_DEVICE * p_dev, tth_GetConfiguration_REQ * p_req, tth_GetConfiguration_RES * p_res);

/**
 * @brief
 *  Sets the thermal configuration for a thermal video source on a device.
 *
 **/ 
HT_API BOOL onvif_tth_SetConfiguration(ONVIF_DEVICE * p_dev, tth_SetConfiguration_REQ * p_req, tth_SetConfiguration_RES * p_res);

/**
 * @brief
 *  Gets the valid ranges for the thermal configurtion parameters that 
 *  have device specific ranges. 
 *
 *  The command shall return all supported parameters and their ranges such
 *  that these can be applied to the SetConfigurationSettings command.
 *
 **/
HT_API BOOL onvif_tth_GetConfigurationOptions(ONVIF_DEVICE * p_dev, tth_GetConfigurationOptions_REQ * p_req, tth_GetConfigurationOptions_RES * p_res);

/**
 * @brief
 *  Requests the global radiometry settings for a thermal video source on the device.
 *
 **/
HT_API BOOL onvif_tth_GetRadiometryConfiguration(ONVIF_DEVICE * p_dev, tth_GetRadiometryConfiguration_REQ * p_req, tth_GetRadiometryConfiguration_RES * p_res);

/**
 * @brief
 *  Sets the radiometry configuration for a thermal video source on a device.
 *
 **/ 
HT_API BOOL onvif_tth_SetRadiometryConfiguration(ONVIF_DEVICE * p_dev, tth_SetRadiometryConfiguration_REQ * p_req, tth_SetRadiometryConfiguration_RES * p_res);

/**
 * @brief
 *  Gets the valid ranges for the radiometry configuration parameters that 
 *  have device specific ranges. 
 *
 *  The command shall return all supported parameters and their ranges such
 *  that these can be applied to the SetRadiometryConfiguration command.
 *
 **/
HT_API BOOL onvif_tth_GetRadiometryConfigurationOptions(ONVIF_DEVICE * p_dev, tth_GetRadiometryConfigurationOptions_REQ * p_req, tth_GetRadiometryConfigurationOptions_RES * p_res);

/**
 * credential service interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the credential service.
 *
 **/
HT_API BOOL onvif_tcr_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tcr_GetServiceCapabilities_REQ * p_req, tcr_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a list of CredentialInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return 
 *  an empty list if there are no items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialInfo(ONVIF_DEVICE * p_dev, tcr_GetCredentialInfo_REQ * p_req, tcr_GetCredentialInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all CredentialInfo items provided by the device.
 *  
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available. The reference shall be valid 
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialInfoList(ONVIF_DEVICE * p_dev, tcr_GetCredentialInfoList_REQ * p_req, tcr_GetCredentialInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of Credential items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an 
 *  empty list if there are no items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentials(ONVIF_DEVICE * p_dev, tcr_GetCredentials_REQ * p_req, tcr_GetCredentials_RES * p_res);

/**
 * @brief
 *  Requests a list of all Credential items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more data is available. The reference shall be valid 
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialList(ONVIF_DEVICE * p_dev, tcr_GetCredentialList_REQ * p_req, tcr_GetCredentialList_RES * p_res);

/**
 * @brief
 *  Creates the specified credential in the device.
 *
 *  A call to this method takes a credential structure and a credential
 *  state structure as input parameters. The credential state can be created
 *  in disabled or enabled state.
 *
 *  The token field of the Credential structure shall be empty and the device
 *  shall allocate a token for the credential. The allocated token shall be 
 *  returned in the response.
 *
 *  If the client sends any value in the token field, the device shall return
 *  InvalidArgVal as a generic fault code.
 *
 **/
HT_API BOOL onvif_tcr_CreateCredential(ONVIF_DEVICE * p_dev, tcr_CreateCredential_REQ * p_req, tcr_CreateCredential_RES * p_res);

/**
 * @brief
 *  Modifies the specified credential.
 *
 *  The token of the credential to modify is specified in the token field of 
 *  the Credential structure and shall not be empty. All other fields in the
 *  structure shall overwrite the fields in the specified credential.
 *
 *  When an existing credential is modified, the state is not modified explicitly. 
 *  The only way for a client to change the state of a credential is to 
 *  explicitly call the EnableCredential, DisableCredential or ResetAntipassback
 *  command.
 *
 *  All existing credential identifiers and credential access profiles are 
 *  removed and replaced with the specified entities.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tcr_ModifyCredential(ONVIF_DEVICE * p_dev, tcr_ModifyCredential_REQ * p_req, tcr_ModifyCredential_RES * p_res);

/**
 * @brief
 *  Deletes the specified credential.
 *
 *  If it is associated with one or more entities some devices may not be 
 *  able to delete the credential, and consequently a ReferenceInUse fault
 *  shall be generated.
 *
 *  If no token was specified in the request, the device shall return
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tcr_DeleteCredential(ONVIF_DEVICE * p_dev, tcr_DeleteCredential_REQ * p_req, tcr_DeleteCredential_RES * p_res);

/**
 * @brief
 *  Returns the state for the specified credential.
 *
 *  If the capability ResetAntipassbackSupported is set to true, then the 
 *  device shall supply the anti-passback state in the returned credential
 *  state structure.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialState(ONVIF_DEVICE * p_dev, tcr_GetCredentialState_REQ * p_req, tcr_GetCredentialState_RES * p_res);

/**
 * @brief
 *  Enable a credential.
 * 
 **/
HT_API BOOL onvif_tcr_EnableCredential(ONVIF_DEVICE * p_dev, tcr_EnableCredential_REQ * p_req, tcr_EnableCredential_RES * p_res);

/**
 * @brief
 *  Disable a credential.
 *
 **/
HT_API BOOL onvif_tcr_DisableCredential(ONVIF_DEVICE * p_dev, tcr_DisableCredential_REQ * p_req, tcr_DisableCredential_RES * p_res);

/**
 * @brief
 *  Synchronize a credential in a client with the device.
 *
 *  A call to this method takes a credential structure and a credential 
 *  state structure as input parameters. The token field of the credential 
 *  must not be empty
 *
 *  If a credential with the specified token does not exist in the device, 
 *  the credential is created. The credential state can be created in 
 *  disabled or enabled state.
 *
 *  If a credential with the specified token exists in the device, then the
 *  credential is modified. The credential state will be set according to 
 *  the specified state (this behavior differs from the ModifyCredential 
 *  command). All existing credential identifiers and credential access 
 *  profiles are removed and replaced with the specified entities.
 *
 *  A device that signals support for the ClientSuppliedTokenSupported 
 *  capability shall implement this command.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tcr_SetCredential(ONVIF_DEVICE * p_dev, tcr_SetCredential_REQ * p_req, tcr_SetCredential_RES * p_res);

/**
 * @brief
 *  Reset anti-passback violations for a specified credential.
 *
 **/
HT_API BOOL onvif_tcr_ResetAntipassbackViolation(ONVIF_DEVICE * p_dev, tcr_ResetAntipassbackViolation_REQ * p_req, tcr_ResetAntipassbackViolation_RES * p_res);

/**
 * @brief
 *  Returns all the supported format types of a specified identifier type
 *  that is supported by the device.
 *
 **/
HT_API BOOL onvif_tcr_GetSupportedFormatTypes(ONVIF_DEVICE * p_dev, tcr_GetSupportedFormatTypes_REQ * p_req, tcr_GetSupportedFormatTypes_RES * p_res);

/**
 * @brief
 *  Returns all the credential identifiers for a credential.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialIdentifiers(ONVIF_DEVICE * p_dev, tcr_GetCredentialIdentifiers_REQ * p_req, tcr_GetCredentialIdentifiers_RES * p_res);

/**
 * @brief
 *  Creates or updates a credential identifier for a credential.
 *
 *  If the type of specified credential identifier already exists, the current
 *  credential identifier of that type is replaced. Otherwise the credential 
 *  identifier is added.
 *
 **/
HT_API BOOL onvif_tcr_SetCredentialIdentifier(ONVIF_DEVICE * p_dev, tcr_SetCredentialIdentifier_REQ * p_req, tcr_SetCredentialIdentifier_RES * p_res);

/**
 * @brief
 *  Deletes all the identifier values for the specified type. However, 
 *  if the identifier type name doesn't exist in the device, it will be 
 *  silently ignored without any response.
 *
 *  Note that each credential needs at least one identifier and an attempt
 *  to delete the last identifier will result in a fault.
 *
 **/
HT_API BOOL onvif_tcr_DeleteCredentialIdentifier(ONVIF_DEVICE * p_dev, tcr_DeleteCredentialIdentifier_REQ * p_req, tcr_DeleteCredentialIdentifier_RES * p_res);

/**
 * @brief
 *  Returns all the credential access profiles for a credential.
 *
 **/
HT_API BOOL onvif_tcr_GetCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_GetCredentialAccessProfiles_REQ * p_req, tcr_GetCredentialAccessProfiles_RES * p_res);

/**
 * @brief
 *  Adds or updates the credential access profiles for a credential.
 *
 *  The device shall update the credential access profile if the access
 *  profile token in the specified credential access profile matches. 
 *  Otherwise the credential access profile is added.
 *
 **/
HT_API BOOL onvif_tcr_SetCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_SetCredentialAccessProfiles_REQ * p_req, tcr_SetCredentialAccessProfiles_RES * p_res);

/**
 * @brief
 *  Deletes credential access profiles for the specified credential token.
 *
 *  However, if no matching credential access profiles are found, the 
 *  corresponding access profile tokens are silently ignored without any 
 *  response.
 *
 **/
HT_API BOOL onvif_tcr_DeleteCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_DeleteCredentialAccessProfiles_REQ * p_req, tcr_DeleteCredentialAccessProfiles_RES * p_res);

/**
 * access rules service interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the accessrules service.
 *
 **/
HT_API BOOL onvif_tar_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tar_GetServiceCapabilities_REQ * p_req, tar_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a list of AccessProfileInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an 
 *  empty list if there are no  items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a TooManyItems
 *  fault shall be  returned.
 *
 **/
HT_API BOOL onvif_tar_GetAccessProfileInfo(ONVIF_DEVICE * p_dev, tar_GetAccessProfileInfo_REQ * p_req, tar_GetAccessProfileInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all AccessProfileInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more  data is available. The reference shall be valid
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tar_GetAccessProfileInfoList(ONVIF_DEVICE * p_dev, tar_GetAccessProfileInfoList_REQ * p_req, tar_GetAccessProfileInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of AccessProfile items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return
 *  an empty list if there are no items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tar_GetAccessProfiles(ONVIF_DEVICE * p_dev, tar_GetAccessProfiles_REQ * p_req, tar_GetAccessProfiles_RES * p_res);

/**
 * @brief
 *  Requests a list of all AccessProfile items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more  data is available. The reference shall be valid
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tar_GetAccessProfileList(ONVIF_DEVICE * p_dev, tar_GetAccessProfileList_REQ * p_req, tar_GetAccessProfileList_RES * p_res);

/**
 * @brief
 *  Creates the specified access profile in the device.
 *
 *  The token field of the AccessProfile structure shall be empty and the 
 *  device shall allocate a  token for the access profile. The allocated 
 *  token shall be returned in the response.
 *
 *  If the client sends any value in the token field, the device shall 
 *  return InvalidArgVal as a generic fault code.
 *
 *  If several access policies in one access profile are specifying different
 *  schedules for the same access point, then it will result in a union of
 *  the schedules.
 *
 **/
HT_API BOOL onvif_tar_CreateAccessProfile(ONVIF_DEVICE * p_dev, tar_CreateAccessProfile_REQ * p_req, tar_CreateAccessProfile_RES * p_res);

/**
 * @brief
 *  Modifies the specified access profile.
 *  
 *  The token of the access profile to modify is specified in the token 
 *  field of the AccessProfile structure and shall not be empty. 
 *  All other fields in the structure shall overwrite the fields in 
 *  the specified access profile.
 *
 *  If several access policies specifying different schedules for the same 
 *  access point will result in a union of the schedules.
 *
 *  If no token was specified in the request, the device shall return
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tar_ModifyAccessProfile(ONVIF_DEVICE * p_dev, tar_ModifyAccessProfile_REQ * p_req, tar_ModifyAccessProfile_RES * p_res);

/**
 * @brief
 *  Delete the specified access profile.
 *
 *  If the access profile is deleted, all access policies associated with
 *  the access profile will also be deleted.
 *
 *  If it is associated with one or more entities some devices may not be 
 *  able to delete the access profile, and consequently a ReferenceInUse 
 *  fault shall be generated.
 *  
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tar_DeleteAccessProfile(ONVIF_DEVICE * p_dev, tar_DeleteAccessProfile_REQ * p_req, tar_DeleteAccessProfile_RES * p_res);

/**
 * schedule service interface
 **/

/**
 * @brief
 *  Returns the capabilities of the schedule service.
 *
 **/
HT_API BOOL onvif_tsc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tsc_GetServiceCapabilities_REQ * p_req, tsc_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Requests a list of ScheduleInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an
 *  empty list if there are no items matching the specified tokens.
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tsc_GetScheduleInfo(ONVIF_DEVICE * p_dev, tsc_GetScheduleInfo_REQ * p_req, tsc_GetScheduleInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all ScheduleInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data
 *  is returned and more data is available.
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tsc_GetScheduleInfoList(ONVIF_DEVICE * p_dev, tsc_GetScheduleInfoList_REQ * p_req, tsc_GetScheduleInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of Schedule items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an 
 *  empty list if there are no items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tsc_GetSchedules(ONVIF_DEVICE * p_dev, tsc_GetSchedules_REQ * p_req, tsc_GetSchedules_RES * p_res);

/**
 * @brief
 *  Requests a list of all Schedule items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available. The reference shall be valid
 *  for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tsc_GetScheduleList(ONVIF_DEVICE * p_dev, tsc_GetScheduleList_REQ * p_req, tsc_GetScheduleList_RES * p_res);

/**
 * @brief
 *  Creates the specified schedule in the device.
 *
 *  The token field of the Schedule structure shall be empty and the device 
 *  shall allocate a token for the schedule. 
 *  The allocated token shall be returned in the response.
 *
 *  If the client sends any value in the token field, the device shall return
 *  InvalidArgVal as a generic fault code.
 *
 **/
HT_API BOOL onvif_tsc_CreateSchedule(ONVIF_DEVICE * p_dev, tsc_CreateSchedule_REQ * p_req, tsc_CreateSchedule_RES * p_res);

/**
 * @brief
 *  Modifies the specified schedule.
 *
 *  The token of the schedule to modify is specified in the token field of
 *  the Schedule structure and shall not be empty. All other fields in the
 *  structure shall overwrite the fields in the specified schedule.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tsc_ModifySchedule(ONVIF_DEVICE * p_dev, tsc_ModifySchedule_REQ * p_req, tsc_ModifySchedule_RES * p_res);

/**
 * @brief
 *  Delete the specified schedule.
 *
 *  If it is associated with one or more entities some devices may not be 
 *  able to delete the schedule, and consequently a ReferenceInUse fault
 *  shall be generated.
 *
 *  If no token was specified in the request, the device shall return 
 *  InvalidArgs as a generic fault code.
 *
 **/
HT_API BOOL onvif_tsc_DeleteSchedule(ONVIF_DEVICE * p_dev, tsc_DeleteSchedule_REQ * p_req, tsc_DeleteSchedule_RES * p_res);

/**
 * @brief
 *  Requests a list of SpecialDayGroupInfo items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an 
 *  empty list if there are no items matching specified tokens. The device
 *  shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, a 
 *  TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tsc_GetSpecialDayGroupInfo(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupInfo_REQ * p_req, tsc_GetSpecialDayGroupInfo_RES * p_res);

/**
 * @brief
 *  Requests a list of all SpecialDayGroupInfo items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data is 
 *  returned and more data is available. 
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tsc_GetSpecialDayGroupInfoList(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupInfoList_REQ * p_req, tsc_GetSpecialDayGroupInfoList_RES * p_res);

/**
 * @brief
 *  Requests a list of SpecialDayGroup items matching the given tokens.
 *
 *  The device shall ignore tokens it cannot resolve and shall return an
 *  empty list if there are no items matching the specified tokens. 
 *  The device shall not return a fault in this case.
 *
 *  If the number of requested items is greater than MaxLimit, 
 *  a TooManyItems fault shall be returned.
 *
 **/
HT_API BOOL onvif_tsc_GetSpecialDayGroups(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroups_REQ * p_req, tsc_GetSpecialDayGroups_RES * p_res);

/**
 * @brief
 *  Requests a list of all SpecialDayGroupList items provided by the device.
 *
 *  A call to this method shall return a StartReference when not all data 
 *  is returned and more data is available. 
 *  The reference shall be valid for retrieving the next set of data.
 *
 *  The number of items returned shall not be greater than the Limit parameter.
 *
 **/
HT_API BOOL onvif_tsc_GetSpecialDayGroupList(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupList_REQ * p_req, tsc_GetSpecialDayGroupList_RES * p_res);

/**
 * @brief
 *  Creates the specified special day group in the device.
 *
 *  The token field of the SpecialDayGroup structure shall be empty and 
 *  the device shall allocate a token for the special day group. 
 *  The allocated token shall be returned in the response.
 *
 *  If the client sends any value in the token field, the device shall 
 *  return InvalidArgVal as a generic fault code.
 *
 **/
HT_API BOOL onvif_tsc_CreateSpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_CreateSpecialDayGroup_REQ * p_req, tsc_CreateSpecialDayGroup_RES * p_res);

/**
 * @brief
 *  Modifies the specified special day group.
 *
 *  The token of the special day group to modify is specified in the token
 *  field of the SpecialDayGroup structure and shall not be empty. 
 *  All other fields in the structure shall overwrite the fields in the
 *  specified special day group.
 *
 **/
HT_API BOOL onvif_tsc_ModifySpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_ModifySpecialDayGroup_REQ * p_req, tsc_ModifySpecialDayGroup_RES * p_res);

/**
 * @brief
 *  Deletes the specified special day group.
 *
 *  If it is associated with one or more schedules some devices may not be
 *  able to delete the special day group, and consequently a ReferenceInUse 
 *  fault must be generated.
 *
 **/
HT_API BOOL onvif_tsc_DeleteSpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_DeleteSpecialDayGroup_REQ * p_req, tsc_DeleteSpecialDayGroup_RES * p_res);

/**
 * @brief
 *  Requests the ScheduleState for the schedule instance specified
 *  by the given token.
 *
 **/
HT_API BOOL onvif_tsc_GetScheduleState(ONVIF_DEVICE * p_dev, tsc_GetScheduleState_REQ * p_req, tsc_GetScheduleState_RES * p_res);

/**
 * receiver service interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the receiver service.
 *
 **/
HT_API BOOL onvif_trv_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trv_GetServiceCapabilities_REQ * p_req, trv_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Lists all receivers that currently exist on the device.
 *
 **/
HT_API BOOL onvif_trv_GetReceivers(ONVIF_DEVICE * p_dev, trv_GetReceivers_REQ * p_req, trv_GetReceivers_RES * p_res);

/**
 * @brief
 *  Retrieves the details of a specific receiver whose token is known to
 *  the client.
 *
 **/
HT_API BOOL onvif_trv_GetReceiver(ONVIF_DEVICE * p_dev, trv_GetReceiver_REQ * p_req, trv_GetReceiver_RES * p_res);

/**
 * @brief
 *  Creates a new receiver.
 *
 **/
HT_API BOOL onvif_trv_CreateReceiver(ONVIF_DEVICE * p_dev, trv_CreateReceiver_REQ * p_req, trv_CreateReceiver_RES * p_res);

/**
 * @brief
 *  Deletes an existing receiver.
 *  A device may reject deletion of a receiver which is in use.
 *
 **/
HT_API BOOL onvif_trv_DeleteReceiver(ONVIF_DEVICE * p_dev, trv_DeleteReceiver_REQ * p_req, trv_DeleteReceiver_RES * p_res);

/**
 * @brief
 *  Configures a receiver.
 *
 **/
HT_API BOOL onvif_trv_ConfigureReceiver(ONVIF_DEVICE * p_dev, trv_ConfigureReceiver_REQ * p_req, trv_ConfigureReceiver_RES * p_res);

/**
 * @brief
 *  Set the mode of the receiver independently of the rest of its configuration.
 *
 **/
HT_API BOOL onvif_trv_SetReceiverMode(ONVIF_DEVICE * p_dev, trv_SetReceiverMode_REQ * p_req, trv_SetReceiverMode_RES * p_res);

/**
 * @brief
 *  determines whether the receiver is currently disconnected, 
 *  connected or attempting to connect.
 *
 **/
HT_API BOOL onvif_trv_GetReceiverState(ONVIF_DEVICE * p_dev, trv_GetReceiverState_REQ * p_req, trv_GetReceiverState_RES * p_res);

/**
 * provisioning interfaces
 **/

/**
 * @brief
 *  Returns the capabilities of the provisioning service.
 *
 **/
HT_API BOOL onvif_tpv_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tpv_GetServiceCapabilities_REQ * p_req, tpv_GetServiceCapabilities_RES * p_res);

/**
 * @brief
 *  Continuously moves the camera left or right.
 *
 *  A device indicating MaximumPanMoves capability greater than zero shall
 *  support the provisional pan through the PanMove command.
 *
 **/
HT_API BOOL onvif_tpv_PanMove(ONVIF_DEVICE * p_dev, tpv_PanMove_REQ * p_req, tpv_PanMove_RES * p_res);

/**
 * @brief
 *  Continuously moves the camera up or down.
 *
 *  A device indicating MaximumTiltMoves capability greater than zero shall
 *  support the provisional tilt through the TiltMove command.
 *
 **/
HT_API BOOL onvif_tpv_TiltMove(ONVIF_DEVICE * p_dev, tpv_TiltMove_REQ * p_req, tpv_TiltMove_RES * p_res);

/**
 * @brief
 *  Continuously moves the lens focal point in or out.
 *
 *  A device indicating MaximumZoomMoves capability greater than zero shall
 *  support the provisional zoom through the ZoomMove command.
 *
 **/
HT_API BOOL onvif_tpv_ZoomMove(ONVIF_DEVICE * p_dev, tpv_ZoomMove_REQ * p_req, tpv_ZoomMove_RES * p_res);

/**
 * @brief
 *  Continuously moves the camera clockwise or counterclockwise.
 *
 *  A device indicating MaximumRollMoves capability greater than zero shall 
 *  support the provisional roll through the RollMove command.
 *
 *  If a device supports the AutoLevel capability, the direction may be Auto.
 *  
 **/
HT_API BOOL onvif_tpv_RollMove(ONVIF_DEVICE * p_dev, tpv_RollMove_REQ * p_req, tpv_RollMove_RES * p_res);

/**
 * @brief
 *  Continuously moves the camera lens in or out.
 *
 *  A device indicating MaximumFocusMoves capability greater than zero 
 *  shall support the provisional focus through the FocusMove command.
 *
 *  If a device supports the AutoFocus capability, the direction may be Auto.
 *
 **/
HT_API BOOL onvif_tpv_FocusMove(ONVIF_DEVICE * p_dev, tpv_FocusMove_REQ * p_req, tpv_FocusMove_RES * p_res);

/**
 * @brief
 *  Immediately stops movement on all axes.
 *
 **/
HT_API BOOL onvif_tpv_Stop(ONVIF_DEVICE * p_dev, tpv_Stop_REQ * p_req, tpv_Stop_RES * p_res);

/**
 * @brief
 *  Returns information about how many provisioning operations have been 
 *  performed on each axis.
 *
 *  These values can be compared to the lifetime limits from 
 *  GetServiceCapabilities to determine how close to (or past) vendor defined
 *  life limits the device is. 
 *  The values shall survive a SetSystemFactoryDefault operation.
 *
 *  A single provisioning operation may increase the corresponding usage number
 *  by more than 1. 
 *  For example, pan usage may increment by the number of steps the stepper 
 *  motor has moved, which can be multiple steps per operation.
 *
 *  If a particular provisioning axis is not supported, the corresponding 
 *  usage value may be omitted from the response.
 *
 **/
HT_API BOOL onvif_tpv_GetUsage(ONVIF_DEVICE * p_dev, tpv_GetUsage_REQ * p_req, tpv_GetUsage_RES * p_res);


#ifdef __cplusplus
}
#endif

#endif


