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

#include "onvif_device.h"
#include "sys_inc.h"
#include "onvif.h"
#include "xml_node.h"
#include "onvif_utils.h"
#include "onvif_cfg.h"
#include "onvif_probe.h"
#include "onvif_event.h"
#include "onvif_timer.h"
#include "http_srv.h"

/***************************************************************************************/
extern ONVIF_CFG g_onvif_cfg;
extern ONVIF_CLS g_onvif_cls;

/***************************************************************************************/

/**
 * @brief
 *  When the device clock has been synchronized the last time either via an NTP
 *  message or via a SetSystemDateAndTime call.
 *
 **/
void onvif_LastClockSynchronizationNotify()
{
    char str[100] = {'\0'};
    NotificationMessageList * p_message;

    onvif_format_datetime_str(time(NULL), 1, "%Y-%m-%dT%H:%M:%SZ", str, sizeof(str));
    
    p_message = onvif_init_NotificationMessage3(
        "tns1:Monitoring/OperatingTime/LastClockSynchronization", 
        PropertyOperation_Changed, NULL, NULL, NULL, NULL, 
        "Status", str, NULL, NULL);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

#ifdef PROFILE_Q_SUPPORT

void onvif_switchDeviceState(int state)
{
    // 0 - Factory Default state, 1 - Operational State

    if (state != g_onvif_cfg.device_state)
    {
        g_onvif_cfg.device_state = state;
    }
    
    // When switching from Factory Default State to Operational State, 
    //  the device may reboot if necessary.
    
}

#endif

/**
 * @brief
 *  Gets a system log from a device.
 *
 *  The exact format of the system logs is outside the scope of this standard.
 *
 *  The system log information is transmitted through MTOM [MTOM] or as a string.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_AccesslogUnavailable
 *  ONVIF_ERR_SystemlogUnavailable
 **/
ONVIF_RET onvif_tds_GetSystemLog(tds_GetSystemLog_REQ * p_req, tds_GetSystemLog_RES * p_res)
{
    if (SystemLogType_Access == p_req->LogType)
    {
        strcpy(p_res->String, "test access log");
    }
    else
    {
        strcpy(p_res->String, "test system log");
    }
    
    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidTimeZone
 *  ONVIF_ERR_InvalidDateTime
 *  ONVIF_ERR_NtpServerUndefined
 **/
ONVIF_RET onvif_tds_SetSystemDateAndTime(tds_SetSystemDateAndTime_REQ * p_req)
{
    // check datetime
    if (p_req->SystemDateTime.DateTimeType == SetDateTimeType_Manual)
    {
        if (p_req->UTCDateTime.Date.Month < 1 || p_req->UTCDateTime.Date.Month > 12 ||
            p_req->UTCDateTime.Date.Day < 1 || p_req->UTCDateTime.Date.Day > 31 ||
            p_req->UTCDateTime.Time.Hour < 0 || p_req->UTCDateTime.Time.Hour > 23 ||
            p_req->UTCDateTime.Time.Minute < 0 || p_req->UTCDateTime.Time.Minute > 59 ||
            p_req->UTCDateTime.Time.Second < 0 || p_req->UTCDateTime.Time.Second > 61)
        {
            return ONVIF_ERR_InvalidDateTime;
        }
    }

    // check timezone
    if (p_req->SystemDateTime.TimeZoneFlag && 
        p_req->SystemDateTime.TimeZone.TZ[0] != '\0' && 
        onvif_is_valid_timezone(p_req->SystemDateTime.TimeZone.TZ) == FALSE)
    {
        return ONVIF_ERR_InvalidTimeZone;
    }

    // todo : here add handler code ...
    

    g_onvif_cfg.SystemDateTime.DateTimeType = p_req->SystemDateTime.DateTimeType;
    g_onvif_cfg.SystemDateTime.DaylightSavings = p_req->SystemDateTime.DaylightSavings;
    
    if (p_req->SystemDateTime.TimeZoneFlag && 
        p_req->SystemDateTime.TimeZone.TZ[0] != '\0')
    {
        strcpy(g_onvif_cfg.SystemDateTime.TimeZone.TZ, p_req->SystemDateTime.TimeZone.TZ);
    }

    // send notify message ...
    onvif_LastClockSynchronizationNotify();
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Reboots a device. 
 *  Before the device reboots the response message shall be sent.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_SystemReboot()
{
    // todo : here add handler code ...

    // send onvif bye message    
    sleep(3);
    onvif_bye();

    // please comment the code below
    // send onvif hello message, just for test
    sleep(3);
    onvif_hello();
    
    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_SetSystemFactoryDefault(tds_SetSystemFactoryDefault_REQ * p_req)
{
    // todo : here add handler code ...
    

#ifdef PROFILE_Q_SUPPORT
    onvif_switchDeviceState(0); // Devices conformant to Profile Q shall be in Factory Default State 
                                // out-of-the-box and after hard factory reset
#endif

    // todo : please comment the code below, just for test
    // send onvif hello message
    sleep(3);
    onvif_hello();

    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidHostname
 **/
ONVIF_RET onvif_tds_SetHostname(tds_SetHostname_REQ * p_req)
{
    if (p_req->Name[0] == '\0')
    {
        // A device shall try to retrieve the name via DHCP 
        //  when the HostnameFromDHCP capability is 
        //  set and an empty name string is provided

        if (g_onvif_cfg.Capabilities.device.HostnameFromDHCP)
        {
            // todo : retrieve the name via DHCP

            g_onvif_cfg.network.HostnameInformation.FromDHCP = TRUE;
        }
        else
        {
            return ONVIF_ERR_InvalidHostname;
        }
    }
    else if (onvif_is_valid_hostname(p_req->Name) == FALSE)
    {
        return ONVIF_ERR_InvalidHostname;
    }
    else
    {
        g_onvif_cfg.network.HostnameInformation.FromDHCP = FALSE;
    }
    
    // todo : here add handler code ...
    

    strncpy(g_onvif_cfg.network.HostnameInformation.Name, p_req->Name, sizeof(g_onvif_cfg.network.HostnameInformation.Name)-1);

    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_SetHostnameFromDHCP(tds_SetHostnameFromDHCP_REQ * p_req)
{
    // todo : here add handler code ...
    

    g_onvif_cfg.network.HostnameInformation.FromDHCP = p_req->FromDHCP;

    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the DNS settings on a device.
 *
 *  It is valid to set the FromDHCP flag while the device is not using 
 *  DHCP to retrieve its IPv4 address.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidIPv4Address
 **/
ONVIF_RET onvif_tds_SetDNS(tds_SetDNS_REQ * p_req)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    // todo : here add handler code ...

    p_config->network.DNSInformation.FromDHCP = p_req->DNSInformation.FromDHCP;
    p_config->network.DNSInformation.SearchDomainFlag = p_req->DNSInformation.SearchDomainFlag;
    
    if (p_config->network.DNSInformation.FromDHCP == FALSE)
    {
        if (p_req->DNSInformation.SearchDomainFlag)
        {
            memcpy(p_config->network.DNSInformation.SearchDomain, 
                p_req->DNSInformation.SearchDomain, 
                sizeof(p_config->network.DNSInformation.SearchDomain));
        }

        memcpy(p_config->network.DNSInformation.DNSServer, 
            p_req->DNSInformation.DNSServer, 
            sizeof(p_config->network.DNSInformation.DNSServer));
    }
    else
    {
        // todo : get dns server from dhcp ...
        
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the dynamic DNS settings on a device.
 *
 * The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_SetDynamicDNS(tds_SetDynamicDNS_REQ * p_req)
{
    // todo : here add handler code ...

    memcpy(&g_onvif_cfg.network.DynamicDNSInformation, &p_req->DynamicDNSInformation, sizeof(onvif_DynamicDNSInformation));

    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidIPv4Address
 *  ONVIF_ERR_InvalidDnsName
 **/
ONVIF_RET onvif_tds_SetNTP(tds_SetNTP_REQ * p_req)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    // todo : here add handler code ...

    p_config->network.NTPInformation.FromDHCP = p_req->NTPInformation.FromDHCP;

    if (p_config->network.NTPInformation.FromDHCP == FALSE)
    {
        memcpy(p_config->network.NTPInformation.NTPServer, 
            p_req->NTPInformation.NTPServer, 
            sizeof(p_config->network.NTPInformation.NTPServer));
    }
    else
    {
        // todo : get ntp server from dhcp ...
        
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the zero-configuration on the device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidNetworkInterface
 **/
ONVIF_RET onvif_tds_SetZeroConfiguration(tds_SetZeroConfiguration_REQ * p_req)
{
    if (strcmp(p_req->InterfaceToken, g_onvif_cfg.network.ZeroConfiguration.InterfaceToken))
    {
        return ONVIF_ERR_InvalidNetworkInterface;
    }

    // todo : here add handler code ...

    g_onvif_cfg.network.ZeroConfiguration.Enabled = p_req->Enabled;

    onvif_init_ZeroConfiguration();

    return ONVIF_OK;
}

/**
 * @brief
 *  Configures defined network protocols on a device.
 *
 *  This message configures one or more defined network protocols supported 
 *  by the device. There are currently three protocols defined, HTTP, HTTPS 
 *  and RTSP. For each protocol the parameters Port and Enable/Disable can 
 *  be configured.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_ServiceNotSupported
 *  ONVIF_ERR_PortAlreadyInUse
 **/
ONVIF_RET onvif_tds_SetNetworkProtocols(tds_SetNetworkProtocols_REQ * p_req)
{
    ONVIF_CLS * p_class = &g_onvif_cls;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
#ifndef HTTPS
    if (p_req->NetworkProtocol.HTTPSFlag && p_req->NetworkProtocol.HTTPSEnabled)
    {
        return ONVIF_ERR_ServiceNotSupported;
    }
#endif

    // todo : here add handler code ...


    if (p_req->NetworkProtocol.HTTPFlag)
    {
        p_config->network.NetworkProtocol.HTTPEnabled = p_req->NetworkProtocol.HTTPEnabled;
        memcpy(p_config->network.NetworkProtocol.HTTPPort, 
            p_req->NetworkProtocol.HTTPPort, 
            sizeof(p_config->network.NetworkProtocol.HTTPPort));

        http_set_enable(&p_class->http_srv, p_config->network.NetworkProtocol.HTTPEnabled);
    }

    if (p_req->NetworkProtocol.HTTPSFlag)
    {
        p_config->network.NetworkProtocol.HTTPSEnabled = p_req->NetworkProtocol.HTTPSEnabled;
        memcpy(p_config->network.NetworkProtocol.HTTPSPort, 
            p_req->NetworkProtocol.HTTPSPort, 
            sizeof(p_config->network.NetworkProtocol.HTTPSPort));

#ifdef HTTPS
        http_set_enable(&p_class->https_srv, p_config->network.NetworkProtocol.HTTPSEnabled);
#endif
    }

    if (p_req->NetworkProtocol.RTSPFlag)
    {
        p_config->network.NetworkProtocol.RTSPEnabled = p_req->NetworkProtocol.RTSPEnabled;
        memcpy(p_config->network.NetworkProtocol.RTSPPort, 
            p_req->NetworkProtocol.RTSPPort, 
            sizeof(p_config->network.NetworkProtocol.RTSPPort));
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the default gateway settings on a device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidGatewayAddress
 *  ONVIF_ERR_InvalidIPv4Address
 **/
ONVIF_RET onvif_tds_SetNetworkDefaultGateway(tds_SetNetworkDefaultGateway_REQ * p_req)
{
    // todo : here add handler code ...

    memcpy(&g_onvif_cfg.network.NetworkGateway, &p_req->NetworkGateway, sizeof(onvif_NetworkGateway));

    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_GetSystemUris(HTTPCLN * p_user, tds_GetSystemUris_RES * p_res)
{
    uint16 port;
    char proto[16];
    char sip[64];
    ONVIF_CLS * p_class = &g_onvif_cls;

    onvif_get_service_ip_by_user(p_user, sip, sizeof(sip)-1);
    
    p_res->AccessLogUriFlag = 1;
    p_res->SystemLogUriFlag = 1;
    p_res->SupportInfoUriFlag = 1;
    p_res->SystemBackupUriFlag = 1;

#ifdef HTTPS
    if (p_user->https)
    {
        port = p_class->https_port;
        strcpy(proto, "https");
    }
    else
#endif
    {
        port = p_class->http_port;
        strcpy(proto, "http");
    }

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->SystemLogUri, sizeof(p_res->SystemLogUri), "%s://[%s]:%u/SystemLog", proto, sip, port);
    }
    else
    {
        snprintf(p_res->SystemLogUri, sizeof(p_res->SystemLogUri), "%s://%s:%u/SystemLog", proto, sip, port);
    }

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->AccessLogUri, sizeof(p_res->AccessLogUri), "%s://[%s]:%u/AccessLog", proto, sip, port);
    }
    else
    {
        snprintf(p_res->AccessLogUri, sizeof(p_res->AccessLogUri), "%s://%s:%u/AccessLog", proto, sip, port);
    }

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->SupportInfoUri, sizeof(p_res->SupportInfoUri), "%s://[%s]:%u/SupportInfo", proto, sip, port);
    }
    else
    {
        snprintf(p_res->SupportInfoUri, sizeof(p_res->SupportInfoUri), "%s://%s:%u/SupportInfo", proto, sip, port);
    }

    if (p_user->sockaddr.ipv6_flag)
    {
        snprintf(p_res->SystemBackupUri, sizeof(p_res->SystemBackupUri), "%s://[%s]:%u/SystemBackup", proto, sip, port);
    }
    else
    {
        snprintf(p_res->SystemBackupUri, sizeof(p_res->SystemBackupUri), "%s://%s:%u/SystemBackup", proto, sip, port);
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  This function is called when the network interface 
 *  is set without restarting the device
 **/
void onvif_tds_SetNetworkInterfacesToDevice(void * argv)
{
    NetworkInterfaceList * p_net_inf = (NetworkInterfaceList *)argv;

    // todo : Set network interface parameters
    //  Network interface parameters are saved in p_net_inf->NetworkInterface

    if (p_net_inf->NetworkInterface.Enabled)
    {
        if (p_net_inf->NetworkInterface.IPv4Flag && 
            p_net_inf->NetworkInterface.IPv4.Enabled)
        {
            if (p_net_inf->NetworkInterface.IPv4.Config.DHCP)
            {
                // todo : Get IP address from DHCP
            }
            else
            {
                // todo : Set IP address and mask
            }
        }
    }

    // The onvif discovery service needs to be restarted when the network interface changed
    onvif_stop_discovery();
    onvif_start_discovery();

    onvif_hello();
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidNetworkInterface
 *  ONVIF_ERR_InvalidMtuValue
 *  ONVIF_ERR_InvalidInterfaceSpeed
 *  ONVIF_ERR_InvalidInterfaceType
 *  ONVIF_ERR_InvalidIPv4Address
 **/
ONVIF_RET onvif_tds_SetNetworkInterfaces(tds_SetNetworkInterfaces_REQ * p_req, tds_SetNetworkInterfaces_RES * p_res)
{
    uint32 i;
    NetworkInterfaceList * p_net_inf = onvif_find_NetworkInterface(g_onvif_cfg.network.interfaces, p_req->NetworkInterface.token);
    if (NULL == p_net_inf)
    {
        return ONVIF_ERR_InvalidNetworkInterface;
    }

    // Check network interface parameters
    
    if (p_req->NetworkInterface.InfoFlag && 
        p_req->NetworkInterface.Info.MTUFlag && 
        (p_req->NetworkInterface.Info.MTU < 0 || p_req->NetworkInterface.Info.MTU > 1530))
    {
        return ONVIF_ERR_InvalidMtuValue;
    }

    if (p_req->NetworkInterface.Enabled)
    {
        if (p_req->NetworkInterface.IPv4Flag && 
            p_req->NetworkInterface.IPv4.Enabled && 
            p_req->NetworkInterface.IPv4.Config.DHCP == FALSE)
        {
            for (i = 0; i < p_req->NetworkInterface.IPv4.Config.sizeAddress; i ++)
            {
                if (is_ipv4_address(p_req->NetworkInterface.IPv4.Config.Address[i].Address) == FALSE)
                {
                    return ONVIF_ERR_InvalidIPv4Address;
                }
            }
        }

        if (p_req->NetworkInterface.IPv6Flag && p_req->NetworkInterface.IPv6.Enabled)
        {
            for (i = 0; i < p_req->NetworkInterface.IPv6.Config.sizeAddress; i++)
            {
                if (is_ipv6_address(p_req->NetworkInterface.IPv6.Config.Address[i].Address) == FALSE)
                {
                    return ONVIF_ERR_InvalidIPv6Address;
                }
            }
        }
    }

    // Save network interface parameters

    p_net_inf->NetworkInterface.Enabled = p_req->NetworkInterface.Enabled;

    if (p_req->NetworkInterface.InfoFlag && p_req->NetworkInterface.Info.MTUFlag)
    {
        p_net_inf->NetworkInterface.Info.MTU = p_req->NetworkInterface.Info.MTU;
    }
        
    if (p_req->NetworkInterface.IPv4Flag)
    {
        p_net_inf->NetworkInterface.IPv4.Enabled = p_req->NetworkInterface.IPv4.Enabled;
        p_net_inf->NetworkInterface.IPv4.Config.DHCP = p_req->NetworkInterface.IPv4.Config.DHCP;
        
        if (p_net_inf->NetworkInterface.IPv4.Config.DHCP == FALSE)
        {
            p_net_inf->NetworkInterface.IPv4.Config.sizeAddress = p_req->NetworkInterface.IPv4.Config.sizeAddress;
            memcpy(p_net_inf->NetworkInterface.IPv4.Config.Address, p_req->NetworkInterface.IPv4.Config.Address,
                ARRAY_SIZE(p_net_inf->NetworkInterface.IPv4.Config.Address) * sizeof(onvif_PrefixedIPv4Address));
        }
    }

    if (p_req->NetworkInterface.IPv6Flag)
    {
        p_net_inf->NetworkInterface.IPv6.Enabled = p_req->NetworkInterface.IPv6.Enabled;
        p_net_inf->NetworkInterface.IPv6.Config.AcceptRouterAdvert = p_req->NetworkInterface.IPv6.Config.AcceptRouterAdvert;
        p_net_inf->NetworkInterface.IPv6.Config.DHCP = p_req->NetworkInterface.IPv6.Config.DHCP;
        
        if (p_req->NetworkInterface.IPv6.Config.sizeAddress > 0)
        {
            p_net_inf->NetworkInterface.IPv6.Config.sizeAddress = p_req->NetworkInterface.IPv6.Config.sizeAddress;
            memcpy(p_net_inf->NetworkInterface.IPv6.Config.Address, p_req->NetworkInterface.IPv6.Config.Address,
                ARRAY_SIZE(p_net_inf->NetworkInterface.IPv6.Config.Address) * sizeof(onvif_PrefixedIPv6Address));
        }
    }

    /**
     * Set RebootNeeded to TRUE, the onvif client should 
     * call SystemReboot to reboot the device,
     *
     * Set RebootNeeded to FALSE, which means the device 
     * does not need to be restarted after set the network interface
     */
     
#if 1
    // todo : here add handler code ...
         
    p_res->RebootNeeded = TRUE;
#else
    p_res->RebootNeeded = FALSE;

    timer_add(1, onvif_tds_SetNetworkInterfacesToDevice, (void *)p_net_inf, TIMER_MODE_SINGLE);
#endif

    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the discovery mode operation of a device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_SetDiscoveryMode(tds_SetDiscoveryMode_REQ * p_req)
{
    g_onvif_cfg.network.DiscoveryMode = p_req->DiscoveryMode;

    return ONVIF_OK;
}

/**
 * @brief
 *  Creates new device users and corresponding credentials on a device 
 *  for authentication.
 *
 *  A device shall support this command unless support signalled via the 
 *  UserConfigNotSupported capability is 'True'.
 *
 *  Either all users are created successfully or a fault message shall 
 *  be returned without creating any user.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_UsernameClash
 *  ONVIF_ERR_PasswordTooLong
 *  ONVIF_ERR_UsernameTooLong
 *  ONVIF_ERR_Password
 *  ONVIF_ERR_TooManyUsers
 *  ONVIF_ERR_AnonymousNotAllowed
 *  ONVIF_ERR_UsernameTooShort
 **/
ONVIF_RET onvif_tds_CreateUsers(tds_CreateUsers_REQ * p_req)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(p_req->User); i++)
    {
        uint32 len;
        onvif_User * p_idle_user;
        
        if (p_req->User[i].Username[0] == '\0')
        {
            break;
        }

        len = strlen(p_req->User[i].Username);
        if (len <= 3)
        {
            return ONVIF_ERR_UsernameTooShort;
        }

        if (onvif_is_user_exist(p_req->User[i].Username))
        {
            return ONVIF_ERR_UsernameClash;
        }
        
        p_idle_user = onvif_get_idle_user();
        if (p_idle_user)
        {
            memcpy(p_idle_user, &p_req->User[i], sizeof(onvif_User));

#ifdef PROFILE_Q_SUPPORT
            if (UserLevel_Administrator == p_req->User[i].UserLevel && 
                p_req->User[i].Password[0] != '\0')
            {
                onvif_switchDeviceState(1); // creates a new admin user, switch to Operational State
            }
#endif            
        }
        else
        {
            return ONVIF_ERR_TooManyUsers;
        }
    }
    
    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_UsernameMissing
 *  ONVIF_ERR_FixedUser
 **/
ONVIF_RET onvif_tds_DeleteUsers(tds_DeleteUsers_REQ * p_req)
{
    uint32 i;
    onvif_User * p_item = NULL;
    
    for (i = 0; i < ARRAY_SIZE(p_req->Username); i++)
    {
        if (p_req->Username[i][0] == '\0')
        {
            break;
        }

        p_item = onvif_find_user(p_req->Username[i]);
        if (NULL == p_item)
        {
            return ONVIF_ERR_UsernameMissing;
        }
        else if (p_item->fixed)
        {
            return ONVIF_ERR_FixedUser;
        }
    }    

    for (i = 0; i < ARRAY_SIZE(p_req->Username); i++)
    {
        if (p_req->Username[i][0] == '\0')
        {
            break;
        }

        p_item = onvif_find_user(p_req->Username[i]);
        if (NULL != p_item && p_item->fixed == FALSE)
        {
            memset(p_item, 0, sizeof(onvif_User));
        }
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  Updates the settings for one or several users on a device for authentication.
 *
 *  A device shall support this command unless support signalled via the 
 *  UserConfigNotSupported capability is 'True'. Either all change requests are 
 *  processed successfully or a fault message shall be returned and no change 
 *  requests be processed.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_UsernameMissing
 *  ONVIF_ERR_FixedUser
 *  ONVIF_ERR_PasswordTooLong
 *  ONVIF_ERR_Password
 *  ONVIF_ERR_AnonymousNotAllowed
 **/
ONVIF_RET onvif_tds_SetUser(tds_SetUser_REQ * p_req)
{
    uint32 i;
    onvif_User * p_item = NULL;
    
    for (i = 0; i < ARRAY_SIZE(p_req->User); i++)
    {
        if (p_req->User[i].Username[0] == '\0')
        {
            break;
        }
        
        p_item = onvif_find_user(p_req->User[i].Username);
        if (NULL == p_item)
        {
            return ONVIF_ERR_UsernameMissing;
        }
        else if (p_item->fixed)
        {
            return ONVIF_ERR_FixedUser;
        }
    }
    
    for (i = 0; i < ARRAY_SIZE(p_req->User); i++)
    {
        if (p_req->User[i].Username[0] == '\0')
        {
            break;
        }
        
        p_item = onvif_find_user(p_req->User[i].Username);
        if (p_item && FALSE == p_item->fixed)
        {
            strcpy(p_item->Password, p_req->User[i].Password);
            p_item->UserLevel = p_req->User[i].UserLevel;

#ifdef PROFILE_Q_SUPPORT
            if (UserLevel_Administrator == p_item->UserLevel && 
                p_item->Password[0] != '\0')
            {
                onvif_switchDeviceState(1); // modifies the password of an existing admin user, switch to Operational State
            }
#endif
        }
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  Returns the configured remote user (if any). 
 *  A device that signals support for remote user handling via the Security 
 *  Capability RemoteUserHandling shall support this operation. The user is 
 *  only valid for the WSUserToken profile or as a HTTP / RTSP user.
 *
 *  Password derivation is outside of the scope of this specification.
 *  
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NotRemoteUser
 **/
ONVIF_RET onvif_tds_GetRemoteUser(tds_GetRemoteUser_RES * p_res)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    // todo : here add handler code ...


    if (p_config->RemoteUser.Username[0] == '\0')
    {
        p_res->RemoteUserFlag = 0;
    }
    else
    {
        p_res->RemoteUserFlag = 1;
        strcpy(p_res->RemoteUser.Username, p_config->RemoteUser.Username);
    }
    
    p_res->RemoteUser.UseDerivedPassword = p_config->RemoteUser.UseDerivedPassword;

    return ONVIF_OK; 
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NotRemoteUser
 **/
ONVIF_RET onvif_tds_SetRemoteUser(tds_SetRemoteUser_REQ * p_req)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    // todo : here add handler code ...

    // To remove the remote user SetRemoteUser should be called without the RemoteUser parameter

    if (p_req->RemoteUserFlag)
    {
        p_config->RemoteUser.UseDerivedPassword = p_req->RemoteUser.UseDerivedPassword;
        strcpy(p_config->RemoteUser.Username, p_req->RemoteUser.Username);

        if (p_req->RemoteUser.PasswordFlag)
        {
            strcpy(p_config->RemoteUser.Password, p_req->RemoteUser.Password);
        }
    }
    else
    {
        p_config->RemoteUser.UseDerivedPassword = FALSE;
        strcpy(p_config->RemoteUser.Username, "");
        strcpy(p_config->RemoteUser.Password, "");
    }
    
    return ONVIF_OK; 
}

/**
 * @brief
 *  Adds new configurable scope parameters to a device. 
 *  The scope parameters are used in the device discovery to match a probe message
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_TooManyScopes
 **/
ONVIF_RET onvif_tds_AddScopes(tds_AddScopes_REQ * p_req)
{
    uint32 i;
    for (i = 0; i < ARRAY_SIZE(p_req->ScopeItem); i++)
    {
        onvif_Scope * p_item;
        
        if (p_req->ScopeItem[i][0] == '\0')
        {
            break;
        }

        if (onvif_is_scope_exist(p_req->ScopeItem[i]))
        {
            continue;
        }
        
        p_item = onvif_get_idle_scope();
        if (p_item)
        {
            p_item->ScopeDef = ScopeDefinition_Configurable;
            strcpy(p_item->ScopeItem, p_req->ScopeItem[i]);
        }
        else
        {
            return ONVIF_ERR_TooManyScopes;
        }
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Sets the scope parameters of a device. 
 *  The scope parameters are used in the device discovery to match a probe message
 *
 *  This operation replaces all existing configurable scope parameters 
 *  (not fixed parameters). If this shall be avoided, one should use the 
 *  scope add command instead.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_TooManyScopes
 *  ONVIF_ERR_ScopeOverwrite
 **/
ONVIF_RET onvif_tds_SetScopes(tds_SetScopes_REQ * p_req)
{
    uint32 i;
    onvif_Scope * p_item = NULL;
    ONVIF_CFG * p_config = &g_onvif_cfg;

    for (i = 0; i < ARRAY_SIZE(p_config->scopes); i++)
    {
        if (p_config->scopes[i].ScopeDef == ScopeDefinition_Configurable)
        {
            memset(p_config->scopes[i].ScopeItem, 0, sizeof(p_config->scopes[i].ScopeItem));
        }
    }
    
    for (i = 0; i < ARRAY_SIZE(p_req->Scopes); i++)
    {
        if (p_req->Scopes[i][0] == '\0')
        {
            break;
        }

        p_item = onvif_find_scope(p_req->Scopes[i]);
        if (NULL == p_item)
        {
            p_item = onvif_get_idle_scope();
            if (p_item)
            {
                p_item->ScopeDef = ScopeDefinition_Configurable;
                strcpy(p_item->ScopeItem, p_req->Scopes[i]);
            }
            else
            {
                return ONVIF_ERR_TooManyScopes;
            }
        }
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes scope-configurable scope parameters from a device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_FixedScope
 *  ONVIF_ERR_NoScope
 **/
ONVIF_RET onvif_tds_RemoveScopes(tds_RemoveScopes_REQ * p_req)
{
    uint32 i;
    onvif_Scope * p_item = NULL;
    
    for (i = 0; i < ARRAY_SIZE(p_req->ScopeItem); i++)
    {
        if (p_req->ScopeItem[i][0] == '\0')
        {
            break;
        }

        p_item = onvif_find_scope(p_req->ScopeItem[i]);
        if (NULL == p_item)
        {
            return ONVIF_ERR_NoScope;
        }
        else if (ScopeDefinition_Fixed == p_item->ScopeDef)
        {
            return ONVIF_ERR_FixedScope;
        }
    }    

    for (i = 0; i < ARRAY_SIZE(p_req->ScopeItem); i++)
    {
        if (p_req->ScopeItem[i][0] == '\0')
        {
            break;
        }

        p_item = onvif_find_scope(p_req->ScopeItem[i]);
        if (NULL != p_item && ScopeDefinition_Configurable == p_item->ScopeDef)
        {
            memset(p_item, 0, sizeof(onvif_Scope));
        }
    }    

    return ONVIF_OK;
}

/***
 * @brief
 *  Initiates a firmware upgrade using the HTTP POST mechanism.
 *  The response to the command includes an HTTP URL to which the upgrade 
 *  file may be uploaded. The actual upgrade takes place as soon as the 
 *  HTTP POST operation has completed. The device should support firmware 
 *  upgrade through the Start FirmwareUpgrade command. The exact format of 
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
 *  configuration like IP address, subnet mask and gateway or DHCP settings 
 *  unchanged. Additionally a firmware upgrade shall not change user credentials.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *
 **/
ONVIF_RET onvif_tds_StartFirmwareUpgrade(HTTPCLN * p_user, tds_StartFirmwareUpgrade_RES * p_res)
{
    uint16 port;
    char proto[16];
    char sip[64];
    
    // todo : do some file upload prepare ...

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
        snprintf(p_res->UploadUri, sizeof(p_res->UploadUri), 
            "%s://[%s]:%u/FirmwareUpgrade", proto, sip, port);
    }
    else
    {
        snprintf(p_res->UploadUri, sizeof(p_res->UploadUri), 
            "%s://%s:%u/FirmwareUpgrade", proto, sip, port);
    }

    p_res->UploadDelay = 5;             // 5 seconds
    p_res->ExpectedDownTime = 5 * 60;   // 5 minutes

    return ONVIF_OK;
}

/***
 * @brief
 *  Do some check before the upgrade.
 *
 * @param buff : pointer the upload content
 * @param len  : the upload content length
 *
 **/
BOOL onvif_tds_FirmwareUpgradeCheck(const char * buff, int len)
{
    // todo : add the check code ...

    // the firmware length is too short
    if (len < 100)
    {
        return FALSE;
    }
    
    return TRUE;
}

/***
 * @brief
 *  Begin firmware upgrade
 *
 * @param buff : pointer the upload content
 * @param len  : the upload content length
 **/
BOOL onvif_tds_FirmwareUpgrade(const char * buff, int len)
{
    // todo : add the upgrade code ...

    
    return TRUE;
}

/***
 * @brief
 *  After the upgrade is complete do some works, such as reboot device ...
 *  
 **/
void onvif_tds_FirmwareUpgradePost()
{
    
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tds_StartSystemRestore(HTTPCLN * p_user, tds_StartSystemRestore_RES * p_res)
{
    uint16 port;
    char proto[16];
    char sip[64];
    
    // todo : do some file upload prepare ...

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
        snprintf(p_res->UploadUri, sizeof(p_res->UploadUri), 
            "%s://[%s]:%u/SystemRestore", proto, sip, port);
    }
    else
    {
        snprintf(p_res->UploadUri, sizeof(p_res->UploadUri), 
            "%s://%s:%u/SystemRestore", proto, sip, port);
    }

    p_res->ExpectedDownTime = 5 * 60;     // 5 minutes

    return ONVIF_OK;
}

/***
 * @brief
 * Do some check before the restore.
 *
 * @param buff : pointer the upload content
 * @param len  : the upload content length
 *
 **/
BOOL onvif_tds_SystemRestoreCheck(const char * buff, int len)
{
    // todo : add the check code ...
    
    if (NULL == buff || len < 10)
    {
        return FALSE;
    }
    
    return TRUE;
}

/***
 * @brief
 *  Begin system restore.
 *
 * @param buff : pointer the upload content
 * @param len  : the upload content length
 *
 **/
BOOL onvif_tds_SystemRestore(const char * buff, int len)
{
    // todo : add the system restore code ...

    
    return TRUE;
}

/***
 * @brief
 *  After the system restore is complete do some works, such as reboot device ...
 *  
 **/
void onvif_tds_SystemRestorePost()
{
    // todo : please comment the code below
    // send onvif hello message, just for test
    sleep(3);
    onvif_hello();
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidArgVal
 **/
ONVIF_RET onvif_tds_SetHashingAlgorithm(tds_SetHashingAlgorithm_REQ * p_req)
{
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    if (strstr(p_req->Algorithm, "MD5"))
    {
        p_config->md5_hashing = 1;
    }
    else
    {
        p_config->md5_hashing = 0;
    }
    
    if (strstr(p_req->Algorithm, "SHA-256"))
    {
        p_config->sha256_hashing = 1;
    }
    else
    {
        p_config->sha256_hashing = 0;
    }

    if (!p_config->md5_hashing && !p_config->sha256_hashing)
    {
        p_config->md5_hashing = 1;
    }
    
    strncpy(p_config->Capabilities.device.HashingAlgorithms, p_req->Algorithm, 
        sizeof(p_config->Capabilities.device.HashingAlgorithms)-1);
    
    return ONVIF_OK;
}

#ifdef IPFILTER_SUPPORT

/**
 * @brief
 *  Sets the IP address filter settings on a device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidIPv4Address
 **/
ONVIF_RET onvif_tds_SetIPAddressFilter(tds_SetIPAddressFilter_REQ * p_req)
{
    memcpy(&g_onvif_cfg.ipaddr_filter, &p_req->IPAddressFilter, sizeof(onvif_IPAddressFilter));

    // todo : here add handler code ...
    
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Adds an IP filter address to a device.
 *
 *  The value of the Type field shall be ignored by the device. 
 *  Use SetIPAddressFilter to set the type.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidIPv4Address
 *  ONVIF_ERR_IPFilterListIsFull
 **/
ONVIF_RET onvif_tds_AddIPAddressFilter(tds_AddIPAddressFilter_REQ * p_req)
{
    uint32 i;
    onvif_PrefixedIPAddress * p_item;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv4Address); i++)
    {
        if (p_req->IPAddressFilter.IPv4Address[i].Address[0] == '\0')
        {
            break;
        }

        if (onvif_is_ipaddr_filter_exist(p_config->ipaddr_filter.IPv4Address, 
                ARRAY_SIZE(p_config->ipaddr_filter.IPv4Address), 
                &p_req->IPAddressFilter.IPv4Address[i]))
        {
            continue;
        }
        
        p_item = onvif_get_idle_ipaddr_filter(p_config->ipaddr_filter.IPv4Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv4Address));
        if (p_item)
        {
            p_item->PrefixLength = p_req->IPAddressFilter.IPv4Address[i].PrefixLength;
            strcpy(p_item->Address, p_req->IPAddressFilter.IPv4Address[i].Address);
        }
        else
        {
            return ONVIF_ERR_IPFilterListIsFull;
        }
    }

    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv6Address); i++)
    {        
        if (p_req->IPAddressFilter.IPv6Address[i].Address[0] == '\0')
        {
            break;
        }

        if (onvif_is_ipaddr_filter_exist(p_config->ipaddr_filter.IPv6Address, 
                ARRAY_SIZE(p_config->ipaddr_filter.IPv6Address), 
                &p_req->IPAddressFilter.IPv6Address[i]))
        {
            continue;
        }
        
        p_item = onvif_get_idle_ipaddr_filter(p_config->ipaddr_filter.IPv6Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv6Address));
        if (p_item)
        {
            p_item->PrefixLength = p_req->IPAddressFilter.IPv6Address[i].PrefixLength;
            strcpy(p_item->Address, p_req->IPAddressFilter.IPv6Address[i].Address);
        }
        else
        {
            return ONVIF_ERR_IPFilterListIsFull;
        }
    }

    // todo : here add handler code ...

    
    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes an IP filter address from a device.
 *
 *  The value of the Type field shall be ignored by the device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidIPv4Address
 *  ONVIF_ERR_NoIPv4Address
 **/
ONVIF_RET onvif_tds_RemoveIPAddressFilter(tds_RemoveIPAddressFilter_REQ * p_req)
{
    uint32 i;
    onvif_PrefixedIPAddress * p_item = NULL;
    ONVIF_CFG * p_config = &g_onvif_cfg;
    
    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv4Address); i++)
    {
        if (p_req->IPAddressFilter.IPv4Address[i].Address[0] == '\0')
        {
            break;
        }

        p_item = onvif_find_ipaddr_filter(p_config->ipaddr_filter.IPv4Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv4Address), 
                    &p_req->IPAddressFilter.IPv4Address[i]);
        if (NULL == p_item)
        {
            return ONVIF_ERR_NoIPv4Address;
        }
    }

    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv6Address); i++)
    {
        if (p_req->IPAddressFilter.IPv6Address[i].Address[0] == '\0')
        {
            break;
        }

        p_item = onvif_find_ipaddr_filter(p_config->ipaddr_filter.IPv6Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv6Address), 
                    &p_req->IPAddressFilter.IPv6Address[i]);
        if (NULL == p_item)
        {
            return ONVIF_ERR_NoIPv6Address;
        }
    }

    // todo : here add handler code ...
    

    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv4Address); i++)
    {
        if (p_req->IPAddressFilter.IPv4Address[i].Address[0] == '\0')
        {
            break;
        }

        p_item = onvif_find_ipaddr_filter(p_config->ipaddr_filter.IPv4Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv4Address), 
                    &p_req->IPAddressFilter.IPv4Address[i]);
        if (NULL != p_item)
        {
            memset(p_item, 0, sizeof(onvif_PrefixedIPAddress));
        }
    }

    for (i = 0; i < ARRAY_SIZE(p_req->IPAddressFilter.IPv6Address); i++)
    {
        if (p_req->IPAddressFilter.IPv6Address[i].Address[0] == '\0')
        {
            break;
        }

        p_item = onvif_find_ipaddr_filter(p_config->ipaddr_filter.IPv6Address, 
                    ARRAY_SIZE(p_config->ipaddr_filter.IPv6Address), 
                    &p_req->IPAddressFilter.IPv6Address[i]);
        if (NULL != p_item)
        {
            memset(p_item, 0, sizeof(onvif_PrefixedIPAddress));
        }
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  New HTTP connection callback
 *
 * @param p_srv http server pointer (HTTPSRV *)
 * @param addr the connection remote addr
 * @param port the connection remote port
 * @parma userdata user data
 *
 * @return
 *  TRUE, accept new connection
 *  FALSE, deny new connection
 *
 **/
BOOL onvif_http_conn_cb(void * p_srv, struct sockaddr * addr, void * userdata)
{
    uint32 i;
    ONVIF_CFG * p_config = &g_onvif_cfg;

    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in * addr_ipv4 = (struct sockaddr_in *) addr;
        
        for (i = 0; i < ARRAY_SIZE(p_config->ipaddr_filter.IPv4Address); i++)
        {
            BOOL ret;
            HT_SOCKADDR sockaddr;
            
            if (p_config->ipaddr_filter.IPv4Address[i].Address[0] == '\0')
            {
                continue;
            }

            ret = get_address_by_name(p_config->ipaddr_filter.IPv4Address[i].Address, HT_PROTOCOL_TCP, &sockaddr);
            if (!ret || !sockaddr.ipv4_flag)
            {
                continue;
            }

            if (is_same_ipv4_network(&sockaddr.ipv4_addr.sin_addr, &addr_ipv4->sin_addr, p_config->ipaddr_filter.IPv4Address[i].PrefixLength))
            {
                if (IPAddressFilterType_Deny == p_config->ipaddr_filter.Type)
                {
                    return FALSE;
                }
                else
                {
                    return TRUE;
                }
            }
        }
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * addr_ipv6 = (struct sockaddr_in6 *) addr;
        
        for (i = 0; i < ARRAY_SIZE(p_config->ipaddr_filter.IPv6Address); i++)
        {
            BOOL ret;
            HT_SOCKADDR sockaddr;
            
            if (p_config->ipaddr_filter.IPv6Address[i].Address[0] == '\0')
            {
                continue;
            }

            ret = get_address_by_name(p_config->ipaddr_filter.IPv6Address[i].Address, HT_PROTOCOL_TCP, &sockaddr);
            if (!ret || !sockaddr.ipv6_flag)
            {
                continue;
            }

            if (is_same_ipv6_network(&sockaddr.ipv6_addr.sin6_addr, &addr_ipv6->sin6_addr, p_config->ipaddr_filter.IPv6Address[i].PrefixLength))
            {
                if (IPAddressFilterType_Deny == p_config->ipaddr_filter.Type)
                {
                    return FALSE;
                }
                else
                {
                    return TRUE;
                }
            }
        }
    }

    return TRUE;
}

#endif // end of IPFILTER_SUPPORT

#ifdef STORAGE_SUPPORT

/**
 * @brief
 *  Creates a new storage configuration. The configuration data shall be created
 *  in the device and shall be persistent (remains after a device reboots). 
 *  A device indicating storage configuration capability shall support the 
 *  creation of storage configurations as long as the number of existing storage
 *  configurations does not exceed the value of MaxStorageConfigurations capability.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaxStorageConfigurations
 **/
ONVIF_RET onvif_tds_CreateStorageConfiguration(tds_CreateStorageConfiguration_REQ * p_req, tds_CreateStorageConfiguration_RES * p_res)
{
    StorageConfigurationList * p_storage = onvif_add_StorageConfiguration(&g_onvif_cfg.storage);
    if (p_storage)
    {
        memcpy(&p_storage->Configuration.Data, &p_req->StorageConfiguration, sizeof(onvif_StorageConfigurationData));

        strcpy(p_res->Token, p_storage->Configuration.token);
    }

    // todo : here add handler code ...
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Modifies an existing storage configuration.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_ConfigModify
 **/
ONVIF_RET onvif_tds_SetStorageConfiguration(tds_SetStorageConfiguration_REQ * p_req)
{
    StorageConfigurationList * p_storage = onvif_find_StorageConfiguration(g_onvif_cfg.storage, p_req->StorageConfiguration.token);
    if (NULL == p_storage)
    {
        return ONVIF_ERR_NoConfig;
    }

    memcpy(&p_storage->Configuration.Data, &p_req->StorageConfiguration.Data, sizeof(onvif_StorageConfigurationData));

    // todo : here add handler code ...
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes a storage configuration.
 *  
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoConfig
 **/
ONVIF_RET onvif_tds_DeleteStorageConfiguration(tds_DeleteStorageConfiguration_REQ * p_req)
{
    StorageConfigurationList * p_storage = onvif_find_StorageConfiguration(g_onvif_cfg.storage, p_req->Token);
    if (NULL == p_storage)
    {
        return ONVIF_ERR_NoConfig;
    }

    // todo : here add handler code ...
    
    onvif_free_StorageConfiguration(&g_onvif_cfg.storage, p_storage);

    return ONVIF_OK;
}

#endif // STORAGE_SUPPORT

#ifdef GEOLOCATION_SUPPORT

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_TooManyEntries
 *  ONVIF_ERR_NoAutoGeo
 **/
ONVIF_RET onvif_tds_SetGeoLocation(tds_SetGeoLocation_REQ * p_req)
{
    int count = 0;
    
    count = onvif_get_LocationEntity_nums(p_req->Location);
    if (count > g_onvif_cfg.Capabilities.device.GeoLocationEntries)
    {
        return ONVIF_ERR_TooManyEntries;
    }
    
    onvif_free_LocationEntitis(&g_onvif_cfg.location);

    g_onvif_cfg.location = p_req->Location;

    p_req->Location = NULL;

    return ONVIF_OK;
}

/**
 * @brief
 *  Remove one or more geo location entries.
 *
 *  A device shall delete an entity based on the passed fields type and token.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoConfig
 *  ONVIF_ERR_Fixed
 **/
ONVIF_RET onvif_tds_DeleteGeoLocation(tds_DeleteGeoLocation_REQ * p_req)
{
    LocationEntityList * p_location;
    LocationEntityList * p_item = p_req->Location;

    while (p_item)
    {
        p_location = onvif_find_LocationEntity(g_onvif_cfg.location, p_item->Location.Entity, p_item->Location.Token);
        if (p_location)
        {
            if (p_location->Location.Fixed)
            {
                return ONVIF_ERR_Fixed;
            }
        }
        else
        {
            return ONVIF_ERR_NoConfig;
        }
        
        p_item = p_item->next;
    }

    p_item = p_req->Location;
    
    while (p_item)
    {
        p_location = onvif_find_LocationEntity(g_onvif_cfg.location, p_item->Location.Entity, p_item->Location.Token);
        if (p_location)
        {
            onvif_free_LocationEntity(&g_onvif_cfg.location, p_location);
        }
        
        p_item = p_item->next;
    }
    
    return ONVIF_OK;
}

#endif // GEOLOCATION_SUPPORT

#ifdef DOT11_SUPPORT

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidNetworkInterface
 *  ONVIF_ERR_InvalidDot11
 *  ONVIF_ERR_NotDot11
 *  ONVIF_ERR_NotConnectedDot11
 **/
ONVIF_RET onvif_tds_GetDot11Status(tds_GetDot11Status_REQ * p_req, tds_GetDot11Status_RES * p_res)
{    
    NetworkInterfaceList * p_net_inf = onvif_find_NetworkInterface(g_onvif_cfg.network.interfaces, p_req->InterfaceToken);
    if (NULL == p_net_inf)
    {
        return ONVIF_ERR_InvalidNetworkInterface;
    }
    
    // todo : here add handler code ...

    
    return ONVIF_OK;
}

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
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_InvalidNetworkInterface
 *  ONVIF_ERR_InvalidDot11
 *  ONVIF_ERR_NotDot11
 *  ONVIF_ERR_NotScanAvailable
 **/
ONVIF_RET onvif_tds_ScanAvailableDot11Networks(tds_ScanAvailableDot11Networks_REQ * p_req, tds_ScanAvailableDot11Networks_RES * p_res)
{
    NetworkInterfaceList * p_net_inf = onvif_find_NetworkInterface(g_onvif_cfg.network.interfaces, p_req->InterfaceToken);
    if (NULL == p_net_inf)
    {
        return ONVIF_ERR_InvalidNetworkInterface;
    }

    if (g_onvif_cfg.Capabilities.dot11.ScanAvailableNetworks == 0)
    {
        return ONVIF_ERR_NotScanAvailable;
    }

    // todo : here add handler code ...

    
    return ONVIF_OK;
}

#endif // DOT11_SUPPORT



