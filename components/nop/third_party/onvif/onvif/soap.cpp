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

#include "soap.h"
#include "soap_parser.h"


BOOL onvif_tds_GetCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetCapabilities(p_node, p_res);
}

BOOL onvif_tds_GetServices_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetServices_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServicesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetServices_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetServices(p_node, p_res);
}

BOOL onvif_tds_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    return parse_tds_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tds_GetDeviceInformation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDeviceInformation_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDeviceInformationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDeviceInformation_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    return parse_tds_GetDeviceInformation(p_node, p_res);
}

BOOL onvif_tds_GetUsers_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetUsers_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetUsersResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetUsers_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tds_GetUsers_RES));

    return parse_tds_GetUsers(p_node, p_res);
}

BOOL onvif_tds_CreateUsers_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateUsersResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_DeleteUsers_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteUsersResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_SetUser_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetUserResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetRemoteUser_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetRemoteUser_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetRemoteUserResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tds_GetRemoteUser_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    return parse_tds_GetRemoteUser(p_node, p_res);
}

BOOL onvif_tds_SetRemoteUser_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRemoteUserResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetNetworkInterfaces_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetNetworkInterfaces_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetNetworkInterfacesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetNetworkInterfaces_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetNetworkInterfaces_RES));

    return parse_tds_GetNetworkInterfaces(p_node, p_res);
}

BOOL onvif_tds_SetNetworkInterfaces_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_SetNetworkInterfaces_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SetNetworkInterfacesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_SetNetworkInterfaces_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_SetNetworkInterfaces_RES));
    
    return parse_tds_SetNetworkInterfaces(p_node, p_res);
}

BOOL onvif_tds_GetNTP_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetNTP_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetNTPResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetNTP_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetNTP(p_node, p_res);
}

BOOL onvif_tds_SetNTP_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetNTPResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetHostname_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetHostname_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetHostnameResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetHostname_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetHostname(p_node, p_res);
}

BOOL onvif_tds_SetHostname_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetHostnameResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_SetHostnameFromDHCP_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_SetHostnameFromDHCP_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SetHostnameFromDHCPResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_SetHostnameFromDHCP_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    return parse_tds_SetHostnameFromDHCP(p_node, p_res);
}
    
BOOL onvif_tds_GetDNS_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDNS_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDNSResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDNS_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetDNS(p_node, p_res);
}
    
BOOL onvif_tds_SetDNS_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetDNSResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
    
BOOL onvif_tds_GetDynamicDNS_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDynamicDNS_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDynamicDNSResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDynamicDNS_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetDynamicDNS(p_node, p_res);
}
    
BOOL onvif_tds_SetDynamicDNS_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetDynamicDNSResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetNetworkProtocols_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetNetworkProtocols_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetNetworkProtocolsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetNetworkProtocols_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetNetworkProtocols(p_node, p_res);
}

BOOL onvif_tds_SetNetworkProtocols_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetNetworkProtocolsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
    
BOOL onvif_tds_GetDiscoveryMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDiscoveryMode_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDiscoveryModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDiscoveryMode_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetDiscoveryMode(p_node, p_res);
}

BOOL onvif_tds_SetDiscoveryMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetDiscoveryModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
    
BOOL onvif_tds_GetNetworkDefaultGateway_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetNetworkDefaultGateway_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetNetworkDefaultGatewayResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetNetworkDefaultGateway_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetNetworkDefaultGateway(p_node, p_res);
}

BOOL onvif_tds_SetNetworkDefaultGateway_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetNetworkDefaultGatewayResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetZeroConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetZeroConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetZeroConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetZeroConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetZeroConfiguration_RES));

    return parse_tds_GetZeroConfiguration(p_node, p_res);
}

BOOL onvif_tds_SetZeroConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetZeroConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetEndpointReference_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetEndpointReference_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetEndpointReferenceResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tds_GetEndpointReference_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetEndpointReference_RES));
    
    return parse_tds_GetEndpointReference(p_node, p_res);
}

BOOL onvif_tds_SendAuxiliaryCommand_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_SendAuxiliaryCommand_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SendAuxiliaryCommandResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tds_SendAuxiliaryCommand_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_SendAuxiliaryCommand_RES));
    
    return parse_tds_SendAuxiliaryCommand(p_node, p_res);
}

BOOL onvif_tds_GetRelayOutputs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetRelayOutputs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetRelayOutputsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tds_GetRelayOutputs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetRelayOutputs_RES));
    
    return parse_tds_GetRelayOutputs(p_node, p_res);
}

BOOL onvif_tds_SetRelayOutputSettings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRelayOutputSettingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_SetRelayOutputState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRelayOutputStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetSystemDateAndTime_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetSystemDateAndTime_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSystemDateAndTimeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetSystemDateAndTime_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    return parse_tds_GetSystemDateAndTime(p_node, p_res);
}

BOOL onvif_tds_SetSystemDateAndTime_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSystemDateAndTimeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_SystemReboot_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SystemRebootResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_SetSystemFactoryDefault_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSystemFactoryDefaultResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_GetSystemLog_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetSystemLog_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetSystemLogResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetSystemLog_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetSystemLog_RES));
    
    return parse_tds_GetSystemLog(p_node, p_res);
}
        
BOOL onvif_tds_GetScopes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetScopes_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetScopesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetScopes_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tds_GetScopes_RES));
    
    return parse_tds_GetScopes(p_node, p_res);
}
        
BOOL onvif_tds_SetScopes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetScopesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_tds_AddScopes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddScopesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_tds_RemoveScopes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveScopesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tds_StartFirmwareUpgrade_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_StartFirmwareUpgrade_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "StartFirmwareUpgradeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_StartFirmwareUpgrade_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_StartFirmwareUpgrade(p_node, p_res);
}

BOOL onvif_tds_GetSystemUris_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetSystemUris_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetSystemUrisResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetSystemUris_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetSystemUris(p_node, p_res);
}

BOOL onvif_tds_StartSystemRestore_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_StartSystemRestore_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "StartSystemRestoreResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_StartSystemRestore_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_StartSystemRestore(p_node, p_res);
}

BOOL onvif_tds_GetWsdlUrl_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetWsdlUrl_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetWsdlUrlResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetWsdlUrl_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetWsdlUrl(p_node, p_res);
}

BOOL onvif_tds_GetDot11Capabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDot11Capabilities_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetDot11CapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDot11Capabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetDot11Capabilities(p_node, p_res);
}

BOOL onvif_tds_GetDot11Status_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetDot11Status_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetDot11StatusResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetDot11Status_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetDot11Status(p_node, p_res);
}

BOOL onvif_tds_ScanAvailableDot11Networks_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_ScanAvailableDot11Networks_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "ScanAvailableDot11NetworksResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_ScanAvailableDot11Networks_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_ScanAvailableDot11Networks(p_node, p_res);
}

BOOL onvif_tds_GetGeoLocation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetGeoLocation_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetGeoLocationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetGeoLocation_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetGeoLocation(p_node, p_res);
}

BOOL onvif_tds_SetGeoLocation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetGeoLocationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_DeleteGeoLocation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteGeoLocationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_SetHashingAlgorithm_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetHashingAlgorithmResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

#ifdef IPFILTER_SUPPORT

BOOL onvif_tds_GetIPAddressFilter_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetIPAddressFilter_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetIPAddressFilterResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetIPAddressFilter_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetIPAddressFilter(p_node, p_res);
}

BOOL onvif_tds_SetIPAddressFilter_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetIPAddressFilterResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_AddIPAddressFilter_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddIPAddressFilterResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_RemoveIPAddressFilter_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveIPAddressFilterResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

#endif // end of IPFILTER_SUPPORT

BOOL onvif_tds_GetAccessPolicy_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetAccessPolicy_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetAccessPolicyResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetAccessPolicy_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetAccessPolicy(p_node, p_res);
}

BOOL onvif_tds_SetAccessPolicy_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAccessPolicyResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_GetStorageConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetStorageConfigurations_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetStorageConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetStorageConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetStorageConfigurations(p_node, p_res);
}

BOOL onvif_tds_CreateStorageConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_CreateStorageConfiguration_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "CreateStorageConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_CreateStorageConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_CreateStorageConfiguration(p_node, p_res);
}

BOOL onvif_tds_GetStorageConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tds_GetStorageConfiguration_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetStorageConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tds_GetStorageConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_tds_GetStorageConfiguration(p_node, p_res);
}

BOOL onvif_tds_SetStorageConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetStorageConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tds_DeleteStorageConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteStorageConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_trt_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_trt_GetVideoSources_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoSources_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourcesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoSources_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetVideoSources(p_node, p_res);
}
        
BOOL onvif_trt_GetAudioSources_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioSources_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioSourcesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetAudioSources_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetAudioSources(p_node, p_res);
}
        
BOOL onvif_trt_CreateProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_CreateProfile_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreateProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_CreateProfile_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_CreateProfile_RES));

    return parse_trt_CreateProfile(p_node, p_res);
}
        
BOOL onvif_trt_GetProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetProfile_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetProfile_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetProfile_RES));

    return parse_trt_GetProfile(p_node, p_res);
}

BOOL onvif_trt_GetProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetProfiles_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetProfiles_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetProfiles_RES));

    return parse_trt_GetProfiles(p_node, p_res);
}        

BOOL onvif_trt_AddVideoEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddVideoEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_AddVideoSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddVideoSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_AddAudioEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddAudioEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_AddAudioSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddAudioSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetVideoSourceModes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoSourceModes_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "GetVideoSourceModesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoSourceModes_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetVideoSourceModes_RES));

    return parse_trt_GetVideoSourceModes(p_node, p_res);
}

BOOL onvif_trt_SetVideoSourceMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_SetVideoSourceMode_RES * p_res;
    
    p_node = xml_node_soap_get(p_xml, "SetVideoSourceModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_SetVideoSourceMode_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_SetVideoSourceMode_RES));

    return parse_trt_SetVideoSourceMode(p_node, p_res);
}

BOOL onvif_trt_AddPTZConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddPTZConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_RemoveVideoEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveVideoEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_RemoveVideoSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveVideoSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_RemoveAudioEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveAudioEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_RemoveAudioSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveAudioSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_RemovePTZConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemovePTZConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_DeleteProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_GetVideoSourceConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoSourceConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoSourceConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetVideoSourceConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetVideoEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetVideoEncoderConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetAudioSourceConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioSourceConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioSourceConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetAudioSourceConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetAudioSourceConfigurations(p_node, p_res);
}
        
BOOL onvif_trt_GetAudioEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetAudioEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetAudioEncoderConfigurations(p_node, p_res);
}
        
BOOL onvif_trt_GetVideoSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoSourceConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoSourceConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetVideoSourceConfiguration(p_node, p_res);
}
        
BOOL onvif_trt_GetVideoEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoEncoderConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoEncoderConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetVideoEncoderConfiguration(p_node, p_res);
}
        
BOOL onvif_trt_GetAudioSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioSourceConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetAudioSourceConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetAudioSourceConfiguration(p_node, p_res);
}
        
BOOL onvif_trt_GetAudioEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioEncoderConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetAudioEncoderConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_trt_GetAudioEncoderConfiguration(p_node, p_res);
}
        
BOOL onvif_trt_SetVideoSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetVideoSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_SetVideoEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetVideoEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_SetAudioSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_SetAudioEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_GetVideoSourceConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoSourceConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoSourceConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trt_GetVideoSourceConfigurationOptions_RES));

    return parse_trt_GetVideoSourceConfigurationOptions(p_node, p_res);
}
        
BOOL onvif_trt_GetVideoEncoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoEncoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoEncoderConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trt_GetVideoEncoderConfigurationOptions_RES));

    return parse_trt_GetVideoEncoderConfigurationOptions(p_node, p_res);
}

BOOL onvif_trt_GetAudioSourceConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAudioSourceConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_GetAudioEncoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioEncoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioEncoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioEncoderConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trt_GetAudioEncoderConfigurationOptions_RES));

    return parse_trt_GetAudioEncoderConfigurationOptions(p_node, p_res);
}

BOOL onvif_trt_GetStreamUri_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetStreamUri_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetStreamUriResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetStreamUri_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetStreamUri_RES));
    
    return parse_trt_GetStreamUri(p_node, p_res);
}

BOOL onvif_trt_SetSynchronizationPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSynchronizationPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_trt_GetSnapshotUri_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetSnapshotUri_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSnapshotUriResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetSnapshotUri_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetSnapshotUri_RES));
    
    return parse_trt_GetSnapshotUri(p_node, p_res);
}

BOOL onvif_trt_GetGuaranteedNumberOfVideoEncoderInstances_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetGuaranteedNumberOfVideoEncoderInstances_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetGuaranteedNumberOfVideoEncoderInstancesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetGuaranteedNumberOfVideoEncoderInstances_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetGuaranteedNumberOfVideoEncoderInstances_RES));
    
    return parse_trt_GetGuaranteedNumberOfVideoEncoderInstances(p_node, p_res);
}

BOOL onvif_trt_GetAudioOutputs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioOutputs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioOutputs_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioOutputs_RES));
    
    return parse_trt_GetAudioOutputs(p_node, p_res);
}

BOOL onvif_trt_GetAudioOutputConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioOutputConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioOutputConfigurations_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioOutputConfigurations_RES));
    
    return parse_trt_GetAudioOutputConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetAudioOutputConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioOutputConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioOutputConfiguration_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioOutputConfiguration_RES));
    
    return parse_trt_GetAudioOutputConfiguration(p_node, p_res);
}

BOOL onvif_trt_GetAudioOutputConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioOutputConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioOutputConfigurationOptions_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioOutputConfigurationOptions_RES));
    
    return parse_trt_GetAudioOutputConfigurationOptions(p_node, p_res);
}

BOOL onvif_trt_SetAudioOutputConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioOutputConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetAudioDecoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioDecoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioDecoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioDecoderConfigurations_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioDecoderConfigurations_RES));
    
    return parse_trt_GetAudioDecoderConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetAudioDecoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioDecoderConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioDecoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioDecoderConfiguration_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioDecoderConfiguration_RES));
    
    return parse_trt_GetAudioDecoderConfiguration(p_node, p_res);
}

BOOL onvif_trt_GetAudioDecoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetAudioDecoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioDecoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetAudioDecoderConfigurationOptions_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetAudioDecoderConfigurationOptions_RES));
    
    return parse_trt_GetAudioDecoderConfigurationOptions(p_node, p_res);
}

BOOL onvif_trt_SetAudioDecoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioDecoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_AddAudioOutputConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddAudioOutputConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_AddAudioDecoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddAudioDecoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_RemoveAudioOutputConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveAudioOutputConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_RemoveAudioDecoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveAudioDecoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetOSDs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetOSDs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOSDsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetOSDs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetOSDs_RES));

    return parse_trt_GetOSDs(p_node, p_res);
}

BOOL onvif_trt_GetOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetOSD_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetOSD_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetOSD_RES));

    return parse_trt_GetOSD(p_node, p_res);
}

BOOL onvif_trt_SetOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetOSDOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetOSDOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOSDOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetOSDOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetOSDOptions_RES));
    
    return parse_trt_GetOSDOptions(p_node, p_res);
}

BOOL onvif_trt_CreateOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_CreateOSD_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreateOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_CreateOSD_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_CreateOSD_RES));
    
    return parse_trt_CreateOSD(p_node, p_res);
}

BOOL onvif_trt_DeleteOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetVideoAnalyticsConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoAnalyticsConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoAnalyticsConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetVideoAnalyticsConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetVideoAnalyticsConfigurations_RES));    
    
    return parse_trt_GetVideoAnalyticsConfigurations(p_node, p_res);
}

BOOL onvif_trt_AddVideoAnalyticsConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddVideoAnalyticsConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetVideoAnalyticsConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetVideoAnalyticsConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoAnalyticsConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trt_GetVideoAnalyticsConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetVideoAnalyticsConfiguration_RES));    
    
    return parse_trt_GetVideoAnalyticsConfiguration(p_node, p_res);
}

BOOL onvif_trt_RemoveVideoAnalyticsConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveVideoAnalyticsConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_SetVideoAnalyticsConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetVideoAnalyticsConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetMetadataConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetMetadataConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMetadataConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetMetadataConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetMetadataConfigurations_RES));    
    
    return parse_trt_GetMetadataConfigurations(p_node, p_res);
}

BOOL onvif_trt_AddMetadataConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddMetadataConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetMetadataConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetMetadataConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMetadataConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetMetadataConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetMetadataConfiguration_RES));    
    
    return parse_trt_GetMetadataConfiguration(p_node, p_res);
}

BOOL onvif_trt_RemoveMetadataConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveMetadataConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_SetMetadataConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetMetadataConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_trt_GetMetadataConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetMetadataConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMetadataConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetMetadataConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetMetadataConfigurationOptions_RES));    
    
    return parse_trt_GetMetadataConfigurationOptions(p_node, p_res);
}

BOOL onvif_trt_GetCompatibleVideoEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetCompatibleVideoEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCompatibleVideoEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetCompatibleVideoEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetCompatibleVideoEncoderConfigurations_RES));    
    
    return parse_trt_GetCompatibleVideoEncoderConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetCompatibleAudioEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetCompatibleAudioEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCompatibleAudioEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetCompatibleAudioEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetCompatibleAudioEncoderConfigurations_RES));    
    
    return parse_trt_GetCompatibleAudioEncoderConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetCompatibleVideoAnalyticsConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetCompatibleVideoAnalyticsConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCompatibleVideoAnalyticsConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetCompatibleVideoAnalyticsConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetCompatibleVideoAnalyticsConfigurations_RES));    
    
    return parse_trt_GetCompatibleVideoAnalyticsConfigurations(p_node, p_res);
}

BOOL onvif_trt_GetCompatibleMetadataConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trt_GetCompatibleMetadataConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCompatibleMetadataConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (trt_GetCompatibleMetadataConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trt_GetCompatibleMetadataConfigurations_RES));    
    
    return parse_trt_GetCompatibleMetadataConfigurations(p_node, p_res);
}

BOOL onvif_ptz_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(ptz_GetServiceCapabilities_RES));

    return parse_ptz_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_ptz_GetNodes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetNodes_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetNodesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetNodes_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(ptz_GetNodes_RES));

    return parse_ptz_GetNodes(p_node, p_res);
}
        
BOOL onvif_ptz_GetNode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetNode_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetNodeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetNode_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(ptz_GetNode_RES));

    return parse_ptz_GetNode(p_node, p_res);
}        

BOOL onvif_ptz_GetPresets_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetPresets_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetPresetsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetPresets_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_ptz_GetPresets(p_node, p_res);
}

BOOL onvif_ptz_SetPreset_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_SetPreset_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SetPresetResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_SetPreset_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(ptz_SetPreset_RES));
    
    return parse_ptz_SetPreset(p_node, p_res);
}

BOOL onvif_ptz_RemovePreset_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemovePresetResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_GotoPreset_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{    
    XMLN * p_node = xml_node_soap_get(p_xml, "GotoPresetResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_GotoHomePosition_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "GotoHomePositionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_SetHomePosition_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetHomePositionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_GetStatus_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetStatus_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetStatusResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetStatus_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(ptz_GetStatus_RES));
    
    return parse_ptz_GetStatus(p_node, p_res);
}

BOOL onvif_ptz_ContinuousMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ContinuousMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_RelativeMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RelativeMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
        
BOOL onvif_ptz_AbsoluteMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AbsoluteMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_Stop_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "StopResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_GetConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{    
    XMLN * p_node;
    ptz_GetConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_ptz_GetConfigurations(p_node, p_res);
} 

BOOL onvif_ptz_GetConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_GetConfiguration_RES *) argv;
    if (NULL == p_res) 
    {
        return TRUE;
    }

    return parse_ptz_GetConfiguration(p_node, p_res);
}
        
BOOL onvif_ptz_SetConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_ptz_GetConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_GetConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_GetConfigurationOptions_RES));
    
    return parse_ptz_GetConfigurationOptions(p_node, p_res);
}

BOOL onvif_ptz_GetPresetTours_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetPresetTours_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetPresetToursResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_GetPresetTours_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_GetPresetTours_RES));
    
    return parse_ptz_GetPresetTours(p_node, p_res);
}

BOOL onvif_ptz_GetPresetTour_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetPresetTour_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetPresetTourResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_GetPresetTour_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_GetPresetTour_RES));
    
    return parse_ptz_GetPresetTour(p_node, p_res);
}

BOOL onvif_ptz_GetPresetTourOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_GetPresetTourOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetPresetTourOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_GetPresetTourOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_GetPresetTourOptions_RES));
    
    return parse_ptz_GetPresetTourOptions(p_node, p_res);
}

BOOL onvif_ptz_CreatePresetTour_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_CreatePresetTour_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreatePresetTourResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (ptz_CreatePresetTour_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_CreatePresetTour_RES));
    
    return parse_ptz_CreatePresetTour(p_node, p_res);
}

BOOL onvif_ptz_ModifyPresetTour_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyPresetTourResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_ptz_OperatePresetTour_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "OperatePresetTourResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_ptz_RemovePresetTour_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemovePresetTourResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_ptz_SendAuxiliaryCommand_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    ptz_SendAuxiliaryCommand_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SendAuxiliaryCommandResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (ptz_SendAuxiliaryCommand_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(ptz_SendAuxiliaryCommand_RES));
    
    return parse_ptz_SendAuxiliaryCommand(p_node, p_res);
}

BOOL onvif_ptz_GeoMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "GeoMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tev_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_GetServiceCapabilities_RES));
    
    return parse_tev_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tev_GetEventProperties_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_GetEventProperties_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetEventPropertiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_GetEventProperties_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_GetEventProperties_RES));
    
    return parse_tev_GetEventProperties(p_node, p_res);
}
    
BOOL onvif_tev_Renew_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RenewResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}
    
BOOL onvif_tev_Unsubscribe_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "UnsubscribeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
    
BOOL onvif_tev_Subscribe_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_Subscribe_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SubscribeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_Subscribe_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_Subscribe_RES));
    
    return parse_tev_Subscribe(p_node, p_res);
}

BOOL onvif_tev_PauseSubscription_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "PauseSubscriptionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tev_ResumeSubscription_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ResumeSubscriptionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tev_CreatePullPointSubscription_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_CreatePullPointSubscription_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreatePullPointSubscriptionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_CreatePullPointSubscription_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_CreatePullPointSubscription_RES));
    
    return parse_tev_CreatePullPointSubscription(p_node, p_res);
}

BOOL onvif_tev_DestroyPullPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DestroyPullPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tev_PullMessages_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_PullMessages_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "PullMessagesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_PullMessages_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_PullMessages_RES));
    
    return parse_tev_PullMessages(p_node, p_res);
}

BOOL onvif_tev_GetMessages_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tev_GetMessages_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMessagesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tev_GetMessages_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tev_GetMessages_RES));
    
    return parse_tev_GetMessages(p_node, p_res);
}

BOOL onvif_tev_Seek_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SeekResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tev_SetSynchronizationPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSynchronizationPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_img_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (img_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetServiceCapabilities_RES));
    
    return parse_img_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_img_GetImagingSettings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetImagingSettings_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetImagingSettingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetImagingSettings_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    return parse_img_GetImagingSettings(p_node, p_res);
}
    
BOOL onvif_img_SetImagingSettings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetImagingSettingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_img_GetOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetOptions_RES));
    
    return parse_img_GetOptions(p_node, p_res);
}

BOOL onvif_img_Move_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "MoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
  
BOOL onvif_img_Stop_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "StopResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
     
BOOL onvif_img_GetStatus_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetStatus_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetStatusResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetStatus_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetStatus_RES));
    
    return parse_img_GetStatus(p_node, p_res);
}
 
BOOL onvif_img_GetMoveOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetMoveOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMoveOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetMoveOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetMoveOptions_RES));
    
    return parse_img_GetMoveOptions(p_node, p_res);
}

BOOL onvif_img_GetPresets_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetPresets_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetPresetsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetPresets_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetPresets_RES));
    
    return parse_img_GetPresets(p_node, p_res);
}

BOOL onvif_img_GetCurrentPreset_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    img_GetCurrentPreset_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetCurrentPresetResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (img_GetCurrentPreset_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(img_GetCurrentPreset_RES));
    
    return parse_img_GetCurrentPreset(p_node, p_res);
}

BOOL onvif_img_SetCurrentPreset_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetCurrentPresetResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tan_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tan_GetServiceCapabilities_RES));
    
    return parse_tan_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tan_GetSupportedRules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetSupportedRules_RES * p_res = (tan_GetSupportedRules_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetSupportedRulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetSupportedRules_RES));    
    
    return parse_tan_GetSupportedRules(p_node, p_res);
}

BOOL onvif_tan_CreateRules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateRulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_DeleteRules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteRulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_GetRules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetRules_RES * p_res = (tan_GetRules_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetRulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetRules_RES));    
    
    return parse_tan_GetRules(p_node, p_res);
}

BOOL onvif_tan_ModifyRules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyRulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_CreateAnalyticsModules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateAnalyticsModulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_DeleteAnalyticsModules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteAnalyticsModulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_GetAnalyticsModules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetAnalyticsModules_RES * p_res = (tan_GetAnalyticsModules_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetAnalyticsModulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetAnalyticsModules_RES));    
    
    return parse_tan_GetAnalyticsModules(p_node, p_res);
}

BOOL onvif_tan_ModifyAnalyticsModules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyAnalyticsModulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tan_GetSupportedAnalyticsModules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetSupportedAnalyticsModules_RES * p_res = (tan_GetSupportedAnalyticsModules_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetSupportedAnalyticsModulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetSupportedAnalyticsModules_RES));    
    
    return parse_tan_GetSupportedAnalyticsModules(p_node, p_res);
}

BOOL onvif_tan_GetRuleOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetRuleOptions_RES * p_res = (tan_GetRuleOptions_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetRuleOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetRuleOptions_RES));    
    
    return parse_tan_GetRuleOptions(p_node, p_res);
}

BOOL onvif_tan_GetAnalyticsModuleOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetAnalyticsModuleOptions_RES * p_res = (tan_GetAnalyticsModuleOptions_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetAnalyticsModuleOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetAnalyticsModuleOptions_RES));    
    
    return parse_tan_GetAnalyticsModuleOptions(p_node, p_res);
}

BOOL onvif_tan_GetSupportedMetadata_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tan_GetSupportedMetadata_RES * p_res = (tan_GetSupportedMetadata_RES *)argv;

    p_node = xml_node_soap_get(p_xml, "GetSupportedMetadataResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tan_GetSupportedMetadata_RES));
    
    return parse_tan_GetSupportedMetadata(p_node, p_res);
}

BOOL onvif_tr2_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tr2_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetServiceCapabilities_RES));

    return parse_tr2_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tr2_GetVideoEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoEncoderConfigurationOptions_RES));

    return parse_tr2_GetVideoEncoderConfigurations(p_node, p_res);
}

BOOL onvif_tr2_GetVideoEncoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoEncoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoEncoderConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoEncoderConfigurationOptions_RES));

    return parse_tr2_GetVideoEncoderConfigurationOptions(p_node, p_res);
}

BOOL onvif_tr2_SetVideoEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetVideoEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tr2_GetProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetProfiles_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetProfiles_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tr2_GetProfiles_RES));

    return parse_tr2_GetProfiles(p_node, p_res);
}
 
BOOL onvif_tr2_CreateProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_CreateProfile_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreateProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_CreateProfile_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_CreateProfile_RES));

    return parse_tr2_CreateProfile(p_node, p_res);
}
 
BOOL onvif_tr2_DeleteProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_GetStreamUri_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetStreamUri_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetStreamUriResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetStreamUri_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetStreamUri_RES));

    return parse_tr2_GetStreamUri(p_node, p_res);
}
 
BOOL onvif_tr2_GetVideoSourceConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoSourceConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoSourceConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoSourceConfigurations_RES));

    return parse_tr2_GetVideoSourceConfigurations(p_node, p_res);
}
 
BOOL onvif_tr2_GetVideoSourceConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoSourceConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoSourceConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoSourceConfigurationOptions_RES));

    return parse_trt_GetVideoSourceConfigurationOptions(p_node, (trt_GetVideoSourceConfigurationOptions_RES*)p_res);
}
 
BOOL onvif_tr2_SetVideoSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetVideoSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_SetSynchronizationPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSynchronizationPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_GetMetadataConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetMetadataConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMetadataConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetMetadataConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetMetadataConfigurations_RES));

    return parse_tr2_GetMetadataConfigurations(p_node, p_res);
}
 
BOOL onvif_tr2_GetMetadataConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetMetadataConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMetadataConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tr2_GetMetadataConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetMetadataConfigurationOptions_RES));

    return parse_tr2_GetMetadataConfigurationOptions(p_node, p_res);
}
 
BOOL onvif_tr2_SetMetadataConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetMetadataConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_GetAudioEncoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioEncoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioEncoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioEncoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioEncoderConfigurations_RES));

    return parse_tr2_GetAudioEncoderConfigurations(p_node, p_res);
}
 
BOOL onvif_tr2_GetAudioSourceConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioSourceConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioSourceConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioSourceConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioSourceConfigurations_RES));

    return parse_tr2_GetAudioSourceConfigurations(p_node, p_res);
}
 
BOOL onvif_tr2_GetAudioSourceConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAudioSourceConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}
 
BOOL onvif_tr2_SetAudioSourceConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioSourceConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_SetAudioEncoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioEncoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_GetAudioEncoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioEncoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioEncoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioEncoderConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioEncoderConfigurationOptions_RES));

    return parse_tr2_GetAudioEncoderConfigurationOptions(p_node, p_res);
}
 
BOOL onvif_tr2_AddConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AddConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_RemoveConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RemoveConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tr2_GetVideoEncoderInstances_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoEncoderInstances_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoEncoderInstancesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoEncoderInstances_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoEncoderInstances_RES));

    return parse_tr2_GetVideoEncoderInstances(p_node, p_res);
} 

BOOL onvif_tr2_GetAudioOutputConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioOutputConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioOutputConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioOutputConfigurations_RES));

    return parse_tr2_GetAudioOutputConfigurations(p_node, p_res);
}

BOOL onvif_tr2_GetAudioOutputConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioOutputConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioOutputConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioOutputConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioOutputConfigurationOptions_RES));

    return parse_tr2_GetAudioOutputConfigurationOptions(p_node, p_res);
}

BOOL onvif_tr2_SetAudioOutputConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioOutputConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tr2_GetAudioDecoderConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioDecoderConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioDecoderConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioDecoderConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioDecoderConfigurations_RES));

    return parse_tr2_GetAudioDecoderConfigurations(p_node, p_res);
}

BOOL onvif_tr2_GetAudioDecoderConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAudioDecoderConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAudioDecoderConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAudioDecoderConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAudioDecoderConfigurationOptions_RES));

    return parse_tr2_GetAudioDecoderConfigurationOptions(p_node, p_res);
}

BOOL onvif_tr2_SetAudioDecoderConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAudioDecoderConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tr2_GetSnapshotUri_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetSnapshotUri_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSnapshotUriResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetSnapshotUri_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetSnapshotUri_RES));

    return parse_tr2_GetSnapshotUri(p_node, p_res);
}

BOOL onvif_tr2_StartMulticastStreaming_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "StartMulticastStreamingResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tr2_StopMulticastStreaming_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "StopMulticastStreamingResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tr2_GetVideoSourceModes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetVideoSourceModes_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetVideoSourceModesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetVideoSourceModes_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetVideoSourceModes_RES));

    return parse_tr2_GetVideoSourceModes(p_node, p_res);
}

BOOL onvif_tr2_SetVideoSourceMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_SetVideoSourceMode_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SetVideoSourceModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_SetVideoSourceMode_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_SetVideoSourceMode_RES));

    return parse_tr2_SetVideoSourceMode(p_node, p_res);
}

BOOL onvif_tr2_CreateOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_CreateOSD_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreateOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_CreateOSD_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_CreateOSD_RES));

    return parse_tr2_CreateOSD(p_node, p_res);
}

BOOL onvif_tr2_DeleteOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tr2_GetOSDs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetOSDs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOSDsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetOSDs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetOSDs_RES));

    return parse_tr2_GetOSDs(p_node, p_res);
}

BOOL onvif_tr2_SetOSD_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetOSDResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tr2_GetOSDOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetOSDOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetOSDOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetOSDOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetOSDOptions_RES));

    return parse_tr2_GetOSDOptions(p_node, p_res);
}

BOOL onvif_tr2_GetAnalyticsConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetAnalyticsConfigurations_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetAnalyticsConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetAnalyticsConfigurations_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetAnalyticsConfigurations_RES));

    return parse_tr2_GetAnalyticsConfigurations(p_node, p_res);
}

BOOL onvif_tr2_GetMasks_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetMasks_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMasksResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetMasks_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetMasks_RES));

    return parse_tr2_GetMasks(p_node, p_res);
}

BOOL onvif_tr2_SetMask_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetMaskResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tr2_CreateMask_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_CreateMask_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "CreateMaskResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_CreateMask_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_CreateMask_RES));

    return parse_tr2_CreateMask(p_node, p_res);
}

BOOL onvif_tr2_DeleteMask_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteMaskResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tr2_GetMaskOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tr2_GetMaskOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetMaskOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tr2_GetMaskOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tr2_GetMaskOptions_RES));

    return parse_tr2_GetMaskOptions(p_node, p_res);
}

#ifdef DEVICEIO_SUPPORT

BOOL onvif_tmd_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetServiceCapabilities_RES));

    return parse_tmd_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tmd_GetRelayOutputs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetRelayOutputs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetRelayOutputsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetRelayOutputs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetRelayOutputs_RES));

    return parse_tmd_GetRelayOutputs(p_node, p_res);
}

BOOL onvif_tmd_GetRelayOutputOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetRelayOutputOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetRelayOutputOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetRelayOutputOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetRelayOutputOptions_RES));

    return parse_tmd_GetRelayOutputOptions(p_node, p_res);
}

BOOL onvif_tmd_SetRelayOutputSettings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRelayOutputSettingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tmd_SetRelayOutputState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRelayOutputStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tmd_GetDigitalInputs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetDigitalInputs_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDigitalInputsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetDigitalInputs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetDigitalInputs_RES));

    return parse_tmd_GetDigitalInputs(p_node, p_res);
}

BOOL onvif_tmd_GetDigitalInputConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetDigitalInputConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetDigitalInputConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetDigitalInputConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetDigitalInputConfigurationOptions_RES));

    return parse_tmd_GetDigitalInputConfigurationOptions(p_node, p_res);
}

BOOL onvif_tmd_SetDigitalInputConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetDigitalInputConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tmd_GetSerialPorts_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetSerialPorts_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSerialPortsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetSerialPorts_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetSerialPorts_RES));

    return parse_tmd_GetSerialPorts(p_node, p_res);
}

BOOL onvif_tmd_GetSerialPortConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetSerialPortConfiguration_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSerialPortConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetSerialPortConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetSerialPortConfiguration_RES));

    return parse_tmd_GetSerialPortConfiguration(p_node, p_res);
}

BOOL onvif_tmd_SetSerialPortConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetSerialPortConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tmd_GetSerialPortConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_GetSerialPortConfigurationOptions_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetSerialPortConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_GetSerialPortConfigurationOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_GetSerialPortConfigurationOptions_RES));

    return parse_tmd_GetSerialPortConfigurationOptions(p_node, p_res);
}

BOOL onvif_tmd_SendReceiveSerialCommand_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tmd_SendReceiveSerialCommand_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "SendReceiveSerialCommandResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tmd_SendReceiveSerialCommand_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tmd_SendReceiveSerialCommand_RES));

    return parse_tmd_SendReceiveSerialCommand(p_node, p_res);
}

#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT

BOOL onvif_trc_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trc_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetServiceCapabilities_RES));

    return parse_trc_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_trc_CreateRecording_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_CreateRecording_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateRecordingResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_CreateRecording_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_CreateRecording_RES));

    return parse_trc_CreateRecording(p_node, p_res);
}
  
BOOL onvif_trc_DeleteRecording_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteRecordingResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
    
BOOL onvif_trc_GetRecordings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordings_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordings_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordings_RES));

    return parse_trc_GetRecordings(p_node, p_res);
}
 
BOOL onvif_trc_SetRecordingConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRecordingConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_trc_GetRecordingConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordingConfiguration_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordingConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordingConfiguration_RES));

    return parse_trc_GetRecordingConfiguration(p_node, p_res);
}
 
BOOL onvif_trc_GetRecordingOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordingOptions_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordingOptions_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordingOptions_RES));

    return parse_trc_GetRecordingOptions(p_node, p_res);
}
 
BOOL onvif_trc_CreateTrack_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_CreateTrack_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateTrackResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_CreateTrack_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_CreateTrack_RES));

    return parse_trc_CreateTrack(p_node, p_res);
}
 
BOOL onvif_trc_DeleteTrack_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteTrackResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_trc_GetTrackConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetTrackConfiguration_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetTrackConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetTrackConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetTrackConfiguration_RES));

    return parse_trc_GetTrackConfiguration(p_node, p_res);
}
 
BOOL onvif_trc_SetTrackConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetTrackConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_trc_CreateRecordingJob_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_CreateRecordingJob_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateRecordingJobResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_CreateRecordingJob_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_CreateRecordingJob_RES));

    return parse_trc_CreateRecordingJob(p_node, p_res);
}
 
BOOL onvif_trc_DeleteRecordingJob_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteRecordingJobResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_trc_GetRecordingJobs_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordingJobs_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingJobsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordingJobs_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordingJobs_RES));

    return parse_trc_GetRecordingJobs(p_node, p_res);
}
 
BOOL onvif_trc_SetRecordingJobConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_SetRecordingJobConfiguration_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRecordingJobConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_SetRecordingJobConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_SetRecordingJobConfiguration_RES));

    return parse_trc_SetRecordingJobConfiguration(p_node, p_res);
}
 
BOOL onvif_trc_GetRecordingJobConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordingJobConfiguration_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingJobConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordingJobConfiguration_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordingJobConfiguration_RES));

    return parse_trc_GetRecordingJobConfiguration(p_node, p_res);
}
 
BOOL onvif_trc_SetRecordingJobMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRecordingJobModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_trc_GetRecordingJobState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetRecordingJobState_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingJobStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetRecordingJobState_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetRecordingJobState_RES));

    return parse_trc_GetRecordingJobState(p_node, p_res);
}

BOOL onvif_trc_ExportRecordedData_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_ExportRecordedData_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "ExportRecordedDataResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_ExportRecordedData_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_ExportRecordedData_RES));

    return parse_trc_ExportRecordedData(p_node, p_res);
}

BOOL onvif_trc_StopExportRecordedData_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_StopExportRecordedData_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "StopExportRecordedDataResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_StopExportRecordedData_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_StopExportRecordedData_RES));

    return parse_trc_StopExportRecordedData(p_node, p_res);
}

BOOL onvif_trc_GetExportRecordedDataState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trc_GetExportRecordedDataState_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetExportRecordedDataStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trc_GetExportRecordedDataState_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trc_GetExportRecordedDataState_RES));

    return parse_trc_GetExportRecordedDataState(p_node, p_res);
}

BOOL onvif_trp_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trp_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trp_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trp_GetServiceCapabilities_RES));

    return parse_trp_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_trp_GetReplayUri_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trp_GetReplayUri_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetReplayUriResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trp_GetReplayUri_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trp_GetReplayUri_RES));
    
    return parse_trp_GetReplayUri(p_node, p_res);
}

BOOL onvif_trp_GetReplayConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trp_GetReplayConfiguration_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetReplayConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trp_GetReplayConfiguration_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(trp_GetReplayConfiguration_RES));
    
    return parse_trp_GetReplayConfiguration(p_node, p_res);
}

BOOL onvif_trp_SetReplayConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetReplayConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tse_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tse_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tse_GetServiceCapabilities_RES));

    return parse_tse_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tse_GetRecordingSummary_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetRecordingSummary_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingSummaryResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetRecordingSummary_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetRecordingSummary_RES));
    
    return parse_tse_GetRecordingSummary(p_node, p_res);
}

BOOL onvif_tse_GetRecordingInformation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetRecordingInformation_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingInformationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetRecordingInformation_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetRecordingInformation_RES));
    
    return parse_tse_GetRecordingInformation(p_node, p_res);
}

BOOL onvif_tse_GetMediaAttributes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetMediaAttributes_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetMediaAttributesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetMediaAttributes_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetMediaAttributes_RES));
    
    return parse_tse_GetMediaAttributes(p_node, p_res);
}

BOOL onvif_tse_FindRecordings_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_FindRecordings_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "FindRecordingsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_FindRecordings_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_FindRecordings_RES));
    
    return parse_tse_FindRecordings(p_node, p_res);
}

BOOL onvif_tse_GetRecordingSearchResults_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetRecordingSearchResults_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRecordingSearchResultsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetRecordingSearchResults_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetRecordingSearchResults_RES));
    
    return parse_tse_GetRecordingSearchResults(p_node, p_res);
}

BOOL onvif_tse_FindEvents_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_FindEvents_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "FindEventsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_FindEvents_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_FindEvents_RES));
    
    return parse_tse_FindEvents(p_node, p_res);
}

BOOL onvif_tse_GetEventSearchResults_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetEventSearchResults_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetEventSearchResultsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetEventSearchResults_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetEventSearchResults_RES));
    
    return parse_tse_GetEventSearchResults(p_node, p_res);
}

BOOL onvif_tse_FindMetadata_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_FindMetadata_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "FindMetadataResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_FindMetadata_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_FindMetadata_RES));
    
    return parse_tse_FindMetadata(p_node, p_res);
}

BOOL onvif_tse_GetMetadataSearchResults_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetMetadataSearchResults_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetMetadataSearchResultsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetMetadataSearchResults_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetMetadataSearchResults_RES));
    
    return parse_tse_GetMetadataSearchResults(p_node, p_res);
}

BOOL onvif_tse_FindPTZPosition_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_FindPTZPosition_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "FindPTZPositionResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_FindPTZPosition_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_FindPTZPosition_RES));
    
    return parse_tse_FindPTZPosition(p_node, p_res);
}

BOOL onvif_tse_GetPTZPositionSearchResults_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetPTZPositionSearchResults_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetPTZPositionSearchResultsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetPTZPositionSearchResults_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetPTZPositionSearchResults_RES));
    
    return parse_tse_GetPTZPositionSearchResults(p_node, p_res);
}

BOOL onvif_tse_GetSearchState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_GetSearchState_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSearchStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_GetSearchState_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_GetSearchState_RES));
    
    return parse_tse_GetSearchState(p_node, p_res);
}

BOOL onvif_tse_EndSearch_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tse_EndSearch_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "EndSearchResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tse_EndSearch_RES *)argv;
    if (NULL == p_res)
    {
        return TRUE;
    }
    
    memset(p_res, 0, sizeof(tse_EndSearch_RES));
    
    return parse_tse_EndSearch(p_node, p_res);
}


#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

BOOL onvif_tac_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tac_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tac_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetServiceCapabilities_RES));

    return parse_tac_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tac_GetAccessPointInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAccessPointInfoList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessPointInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAccessPointInfoList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAccessPointInfoList_RES));

    return parse_tac_GetAccessPointInfoList(p_node, p_res);
}
   
BOOL onvif_tac_GetAccessPointInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAccessPointInfo_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessPointInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAccessPointInfo_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAccessPointInfo_RES));

    return parse_tac_GetAccessPointInfo(p_node, p_res);
}

BOOL onvif_tac_GetAccessPointList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAccessPointList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessPointListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAccessPointList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAccessPointList_RES));

    return parse_tac_GetAccessPointList(p_node, p_res);
}

BOOL onvif_tac_GetAccessPoints_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAccessPoints_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessPointsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAccessPoints_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAccessPoints_RES));

    return parse_tac_GetAccessPoints(p_node, p_res);
}

BOOL onvif_tac_CreateAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_CreateAccessPoint_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_CreateAccessPoint_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_CreateAccessPoint_RES));

    return parse_tac_CreateAccessPoint(p_node, p_res);
}

BOOL onvif_tac_SetAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_ModifyAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_DeleteAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_GetAreaInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAreaInfoList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAreaInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAreaInfoList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAreaInfoList_RES));

    return parse_tac_GetAreaInfoList(p_node, p_res);
}
 
BOOL onvif_tac_GetAreaInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAreaInfo_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAreaInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAreaInfo_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAreaInfo_RES));

    return parse_tac_GetAreaInfo(p_node, p_res);
}

BOOL onvif_tac_GetAreaList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAreaList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAreaListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAreaList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAreaList_RES));

    return parse_tac_GetAreaList(p_node, p_res);
}

BOOL onvif_tac_GetAreas_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAreas_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAreasResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAreas_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAreas_RES));

    return parse_tac_GetAreas(p_node, p_res);
}

BOOL onvif_tac_CreateArea_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_CreateArea_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateAreaResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_CreateArea_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_CreateArea_RES));

    return parse_tac_CreateArea(p_node, p_res);
}

BOOL onvif_tac_SetArea_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetAreaResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_ModifyArea_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyAreaResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_DeleteArea_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteAreaResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tac_GetAccessPointState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tac_GetAccessPointState_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessPointStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tac_GetAccessPointState_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tac_GetAccessPointState_RES));

    return parse_tac_GetAccessPointState(p_node, p_res);
}

BOOL onvif_tac_EnableAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "EnableAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tac_DisableAccessPoint_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DisableAccessPointResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tdc_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tdc_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tdc_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetServiceCapabilities_RES));

    return parse_tdc_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tdc_GetDoorInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_GetDoorInfoList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetDoorInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_GetDoorInfoList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetDoorInfoList_RES));

    return parse_tdc_GetDoorInfoList(p_node, p_res);
}

BOOL onvif_tdc_GetDoorInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_GetDoorInfo_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetDoorInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_GetDoorInfo_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetDoorInfo_RES));

    return parse_tdc_GetDoorInfo(p_node, p_res);
}
 
BOOL onvif_tdc_GetDoorState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_GetDoorState_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetDoorStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_GetDoorState_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetDoorState_RES));

    return parse_tdc_GetDoorState(p_node, p_res);
}
 
BOOL onvif_tdc_AccessDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "AccessDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_LockDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "LockDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_UnlockDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "UnlockDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_DoubleLockDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DoubleLockDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_BlockDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "BlockDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_LockDownDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "LockDownDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_LockDownReleaseDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "LockDownReleaseDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_LockOpenDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "LockOpenDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}
 
BOOL onvif_tdc_LockOpenReleaseDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "LockOpenReleaseDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tdc_GetDoors_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_GetDoors_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetDoorsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_GetDoors_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetDoors_RES));

    return parse_tdc_GetDoors(p_node, p_res);
}

BOOL onvif_tdc_GetDoorList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_GetDoorList_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetDoorListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_GetDoorList_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_GetDoorList_RES));

    return parse_tdc_GetDoorList(p_node, p_res);
}

BOOL onvif_tdc_CreateDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tdc_CreateDoor_RES * p_res;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    p_res = (tdc_CreateDoor_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tdc_CreateDoor_RES));

    return parse_tdc_CreateDoor(p_node, p_res);
}

BOOL onvif_tdc_SetDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tdc_ModifyDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tdc_DeleteDoor_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteDoorResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT

BOOL onvif_tth_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tth_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tth_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetServiceCapabilities_RES));

    return parse_tth_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tth_GetConfigurations_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tth_GetConfigurations_RES * p_res = (tth_GetConfigurations_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetConfigurationsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetConfigurations_RES));

    return parse_tth_GetConfigurations(p_node, p_res);
}

BOOL onvif_tth_GetConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tth_GetConfiguration_RES * p_res = (tth_GetConfiguration_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetConfiguration_RES));

    return parse_tth_GetConfiguration(p_node, p_res);
}

BOOL onvif_tth_SetConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tth_GetConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tth_GetConfigurationOptions_RES * p_res = (tth_GetConfigurationOptions_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetConfigurationOptions_RES));

    return parse_tth_GetConfigurationOptions(p_node, p_res);
}

BOOL onvif_tth_GetRadiometryConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tth_GetRadiometryConfiguration_RES * p_res = (tth_GetRadiometryConfiguration_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRadiometryConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetRadiometryConfiguration_RES));

    return parse_tth_GetRadiometryConfiguration(p_node, p_res);
}

BOOL onvif_tth_SetRadiometryConfiguration_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetRadiometryConfigurationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL onvif_tth_GetRadiometryConfigurationOptions_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{    
    tth_GetRadiometryConfigurationOptions_RES * p_res = (tth_GetRadiometryConfigurationOptions_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetRadiometryConfigurationOptionsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tth_GetRadiometryConfigurationOptions_RES));

    return parse_tth_GetRadiometryConfigurationOptions(p_node, p_res);
}

#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

BOOL onvif_tcr_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tcr_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tcr_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetServiceCapabilities_RES));

    return parse_tcr_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tcr_GetCredentialInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialInfo_RES * p_res = (tcr_GetCredentialInfo_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialInfo_RES));

    return parse_tcr_GetCredentialInfo(p_node, p_res);
}

BOOL onvif_tcr_GetCredentialInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialInfoList_RES * p_res = (tcr_GetCredentialInfoList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialInfoList_RES));

    return parse_tcr_GetCredentialInfoList(p_node, p_res);
}

BOOL onvif_tcr_GetCredentials_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentials_RES * p_res = (tcr_GetCredentials_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentials_RES));

    return parse_tcr_GetCredentials(p_node, p_res);
}

BOOL onvif_tcr_GetCredentialList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialList_RES * p_res = (tcr_GetCredentialList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialList_RES));

    return parse_tcr_GetCredentialList(p_node, p_res);
}

BOOL onvif_tcr_CreateCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_CreateCredential_RES * p_res = (tcr_CreateCredential_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_CreateCredential_RES));

    return parse_tcr_CreateCredential(p_node, p_res);
}

BOOL onvif_tcr_ModifyCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_DeleteCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_GetCredentialState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialState_RES * p_res = (tcr_GetCredentialState_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialState_RES));

    return parse_tcr_GetCredentialState(p_node, p_res);
}

BOOL onvif_tcr_EnableCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "EnableCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_DisableCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DisableCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_SetCredential_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetCredentialResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_ResetAntipassbackViolation_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ResetAntipassbackViolationResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_GetSupportedFormatTypes_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetSupportedFormatTypes_RES * p_res = (tcr_GetSupportedFormatTypes_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSupportedFormatTypesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetSupportedFormatTypes_RES));

    return parse_tcr_GetSupportedFormatTypes(p_node, p_res);
}

BOOL onvif_tcr_GetCredentialIdentifiers_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialIdentifiers_RES * p_res = (tcr_GetCredentialIdentifiers_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialIdentifiersResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialIdentifiers_RES));

    return parse_tcr_GetCredentialIdentifiers(p_node, p_res);
}

BOOL onvif_tcr_SetCredentialIdentifier_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetCredentialIdentifierResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_DeleteCredentialIdentifier_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteCredentialIdentifierResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_GetCredentialAccessProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tcr_GetCredentialAccessProfiles_RES * p_res = (tcr_GetCredentialAccessProfiles_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetCredentialAccessProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tcr_GetCredentialAccessProfiles_RES));

    return parse_tcr_GetCredentialAccessProfiles(p_node, p_res);
}

BOOL onvif_tcr_SetCredentialAccessProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetCredentialAccessProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tcr_DeleteCredentialAccessProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteCredentialAccessProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

BOOL onvif_tar_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tar_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tar_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_GetServiceCapabilities_RES));

    return parse_tar_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tar_GetAccessProfileInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tar_GetAccessProfileInfo_RES * p_res = (tar_GetAccessProfileInfo_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessProfileInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_GetAccessProfileInfo_RES));

    return parse_tar_GetAccessProfileInfo(p_node, p_res);
}

BOOL onvif_tar_GetAccessProfileInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tar_GetAccessProfileInfoList_RES * p_res = (tar_GetAccessProfileInfoList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessProfileInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_GetAccessProfileInfoList_RES));

    return parse_tar_GetAccessProfileInfoList(p_node, p_res);
}

BOOL onvif_tar_GetAccessProfiles_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tar_GetAccessProfiles_RES * p_res = (tar_GetAccessProfiles_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessProfilesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_GetAccessProfiles_RES));

    return parse_tar_GetAccessProfiles(p_node, p_res);
}

BOOL onvif_tar_GetAccessProfileList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tar_GetAccessProfileList_RES * p_res = (tar_GetAccessProfileList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetAccessProfileListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_GetAccessProfileList_RES));

    return parse_tar_GetAccessProfileList(p_node, p_res);
}

BOOL onvif_tar_CreateAccessProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tar_CreateAccessProfile_RES * p_res = (tar_CreateAccessProfile_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateAccessProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tar_CreateAccessProfile_RES));

    return parse_tar_CreateAccessProfile(p_node, p_res);
}

BOOL onvif_tar_ModifyAccessProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyAccessProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tar_DeleteAccessProfile_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteAccessProfileResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

BOOL onvif_tsc_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    tsc_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (tsc_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetServiceCapabilities_RES));

    return parse_tsc_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tsc_GetScheduleInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetScheduleInfo_RES * p_res = (tsc_GetScheduleInfo_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetScheduleInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetScheduleInfo_RES));

    return parse_tsc_GetScheduleInfo(p_node, p_res);
}

BOOL onvif_tsc_GetScheduleInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetScheduleInfoList_RES * p_res = (tsc_GetScheduleInfoList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetScheduleInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetScheduleInfoList_RES));

    return parse_tsc_GetScheduleInfoList(p_node, p_res);
}

BOOL onvif_tsc_GetSchedules_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetSchedules_RES * p_res = (tsc_GetSchedules_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSchedulesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetSchedules_RES));

    return parse_tsc_GetSchedules(p_node, p_res);
}

BOOL onvif_tsc_GetScheduleList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetScheduleList_RES * p_res = (tsc_GetScheduleList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetScheduleListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetScheduleList_RES));

    return parse_tsc_GetScheduleList(p_node, p_res);
}

BOOL onvif_tsc_CreateSchedule_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_CreateSchedule_RES * p_res = (tsc_CreateSchedule_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateScheduleResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_CreateSchedule_RES));

    return parse_tsc_CreateSchedule(p_node, p_res);
}

BOOL onvif_tsc_ModifySchedule_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifyScheduleResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tsc_DeleteSchedule_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteScheduleResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tsc_GetSpecialDayGroupInfo_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetSpecialDayGroupInfo_RES * p_res = (tsc_GetSpecialDayGroupInfo_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSpecialDayGroupInfoResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetSpecialDayGroupInfo_RES));

    return parse_tsc_GetSpecialDayGroupInfo(p_node, p_res);
}

BOOL onvif_tsc_GetSpecialDayGroupInfoList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetSpecialDayGroupInfoList_RES * p_res = (tsc_GetSpecialDayGroupInfoList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSpecialDayGroupInfoListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetSpecialDayGroupInfoList_RES));

    return parse_tsc_GetSpecialDayGroupInfoList(p_node, p_res);
}

BOOL onvif_tsc_GetSpecialDayGroups_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetSpecialDayGroups_RES * p_res = (tsc_GetSpecialDayGroups_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSpecialDayGroupsResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetSpecialDayGroups_RES));

    return parse_tsc_GetSpecialDayGroups(p_node, p_res);
}

BOOL onvif_tsc_GetSpecialDayGroupList_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetSpecialDayGroupList_RES * p_res = (tsc_GetSpecialDayGroupList_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetSpecialDayGroupListResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetSpecialDayGroupList_RES));

    return parse_tsc_GetSpecialDayGroupList(p_node, p_res);
}

BOOL onvif_tsc_CreateSpecialDayGroup_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_CreateSpecialDayGroup_RES * p_res = (tsc_CreateSpecialDayGroup_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateSpecialDayGroupResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_CreateSpecialDayGroup_RES));

    return parse_tsc_CreateSpecialDayGroup(p_node, p_res);
}

BOOL onvif_tsc_ModifySpecialDayGroup_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ModifySpecialDayGroupResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tsc_DeleteSpecialDayGroup_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteSpecialDayGroupResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tsc_GetScheduleState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tsc_GetScheduleState_RES * p_res = (tsc_GetScheduleState_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetScheduleStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tsc_GetScheduleState_RES));

    return parse_tsc_GetScheduleState(p_node, p_res);
}

#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

BOOL onvif_trv_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node;
    trv_GetServiceCapabilities_RES * p_res;

    p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }
    
    p_res = (trv_GetServiceCapabilities_RES *) argv;
    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trv_GetServiceCapabilities_RES));

    return parse_trv_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_trv_GetReceivers_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trv_GetReceivers_RES * p_res = (trv_GetReceivers_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetReceiversResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trv_GetReceivers_RES));

    return parse_trv_GetReceivers(p_node, p_res);
}

BOOL onvif_trv_GetReceiver_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trv_GetReceiver_RES * p_res = (trv_GetReceiver_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetReceiverResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trv_GetReceiver_RES));

    return parse_trv_GetReceiver(p_node, p_res);
}

BOOL onvif_trv_CreateReceiver_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trv_CreateReceiver_RES * p_res = (trv_CreateReceiver_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "CreateReceiverResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trv_CreateReceiver_RES));

    return parse_trv_CreateReceiver(p_node, p_res);
}

BOOL onvif_trv_DeleteReceiver_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "DeleteReceiverResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_trv_ConfigureReceiver_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ConfigureReceiverResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_trv_SetReceiverMode_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "SetReceiverModeResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_trv_GetReceiverState_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    trv_GetReceiverState_RES * p_res = (trv_GetReceiverState_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetReceiverStateResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(trv_GetReceiverState_RES));

    return parse_trv_GetReceiverState(p_node, p_res);
}

#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

BOOL onvif_tpv_GetServiceCapabilities_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tpv_GetServiceCapabilities_RES * p_res = (tpv_GetServiceCapabilities_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetServiceCapabilitiesResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tpv_GetServiceCapabilities_RES));

    return parse_tpv_GetServiceCapabilities(p_node, p_res);
}

BOOL onvif_tpv_PanMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "PanMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_TiltMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "TiltMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_ZoomMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "ZoomMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_RollMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "RollMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_FocusMove_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "FocusMoveResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_Stop_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    XMLN * p_node = xml_node_soap_get(p_xml, "StopResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL onvif_tpv_GetUsage_rly(XMLN * p_xml, ONVIF_DEVICE * p_dev, void * argv)
{
    tpv_GetUsage_RES * p_res = (tpv_GetUsage_RES *) argv;
    XMLN * p_node = xml_node_soap_get(p_xml, "GetUsageResponse");
    if (NULL == p_node)
    {
        return FALSE;
    }

    if (NULL == p_res)
    {
        return TRUE;
    }

    memset(p_res, 0, sizeof(tpv_GetUsage_RES));

    return parse_tpv_GetUsage(p_node, p_res);
}

#endif // end of PROVISIONING_SUPPORT

BOOL onvif_rly_handler(XMLN * p_xml, eOnvifAction act, ONVIF_DEVICE * p_dev, void * argv)
{
    BOOL ret = FALSE;
    
    switch (act)
    {
    // onvif device service interfaces
    
    case etdsGetCapabilities:
        ret = onvif_tds_GetCapabilities_rly(p_xml, p_dev, argv);
        break;

    case etdsGetServices:
        ret = onvif_tds_GetServices_rly(p_xml, p_dev, argv);
        break;

    case etdsGetServiceCapabilities:
        ret = onvif_tds_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetDeviceInformation:
        ret = onvif_tds_GetDeviceInformation_rly(p_xml, p_dev, argv);
        break;

    case etdsGetUsers:
        ret = onvif_tds_GetUsers_rly(p_xml, p_dev, argv);
        break;

    case etdsCreateUsers:
        ret = onvif_tds_CreateUsers_rly(p_xml, p_dev, argv);
        break;

    case etdsDeleteUsers:
        ret = onvif_tds_DeleteUsers_rly(p_xml, p_dev, argv);
        break;

    case etdsSetUser:
        ret = onvif_tds_SetUser_rly(p_xml, p_dev, argv);
        break;

    case etdsGetRemoteUser:
        ret = onvif_tds_GetRemoteUser_rly(p_xml, p_dev, argv);
        break;

    case etdsSetRemoteUser:
        ret = onvif_tds_SetRemoteUser_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetNetworkInterfaces:
        ret = onvif_tds_GetNetworkInterfaces_rly(p_xml, p_dev, argv);
        break;

    case etdsSetNetworkInterfaces:
        ret = onvif_tds_SetNetworkInterfaces_rly(p_xml, p_dev, argv);
        break;

    case etdsGetNTP:
        ret = onvif_tds_GetNTP_rly(p_xml, p_dev, argv);
        break;

    case etdsSetNTP:
        ret = onvif_tds_SetNTP_rly(p_xml, p_dev, argv);
        break;

    case etdsGetHostname:
        ret = onvif_tds_GetHostname_rly(p_xml, p_dev, argv);
        break;

    case etdsSetHostname:
        ret = onvif_tds_SetHostname_rly(p_xml, p_dev, argv);
        break;

    case etdsSetHostnameFromDHCP:
        ret = onvif_tds_SetHostnameFromDHCP_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetDNS:
        ret = onvif_tds_GetDNS_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetDNS:
        ret = onvif_tds_SetDNS_rly(p_xml, p_dev, argv);
        break;    
        
    case etdsGetDynamicDNS:
        ret = onvif_tds_GetDynamicDNS_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetDynamicDNS:
        ret = onvif_tds_SetDynamicDNS_rly(p_xml, p_dev, argv);
        break;

    case etdsGetNetworkProtocols:
        ret = onvif_tds_GetNetworkProtocols_rly(p_xml, p_dev, argv);
        break;

    case etdsSetNetworkProtocols:
        ret = onvif_tds_SetNetworkProtocols_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetDiscoveryMode:
        ret = onvif_tds_GetDiscoveryMode_rly(p_xml, p_dev, argv);
        break;

    case etdsSetDiscoveryMode:
        ret = onvif_tds_SetDiscoveryMode_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetNetworkDefaultGateway:
        ret = onvif_tds_GetNetworkDefaultGateway_rly(p_xml, p_dev, argv);
        break;

    case etdsSetNetworkDefaultGateway:
        ret = onvif_tds_SetNetworkDefaultGateway_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetZeroConfiguration:
        ret = onvif_tds_GetZeroConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetZeroConfiguration:
        ret = onvif_tds_SetZeroConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etdsGetEndpointReference:
        ret = onvif_tds_GetEndpointReference_rly(p_xml, p_dev, argv);
        break;

    case etdsSendAuxiliaryCommand:
        ret = onvif_tds_SendAuxiliaryCommand_rly(p_xml, p_dev, argv);
        break;

    case etdsGetRelayOutputs:
        ret = onvif_tds_GetRelayOutputs_rly(p_xml, p_dev, argv);
        break;

    case etdsSetRelayOutputSettings:
        ret = onvif_tds_SetRelayOutputSettings_rly(p_xml, p_dev, argv);
        break;

    case etdsSetRelayOutputState:
        ret = onvif_tds_SetRelayOutputState_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetSystemDateAndTime:
        ret = onvif_tds_GetSystemDateAndTime_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetSystemDateAndTime:
        ret = onvif_tds_SetSystemDateAndTime_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSystemReboot:
        ret = onvif_tds_SystemReboot_rly(p_xml, p_dev, argv);
        break;

    case etdsSetSystemFactoryDefault:
        ret = onvif_tds_SetSystemFactoryDefault_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetSystemLog:
        ret = onvif_tds_GetSystemLog_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetScopes:
        ret = onvif_tds_GetScopes_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetScopes:
        ret = onvif_tds_SetScopes_rly(p_xml, p_dev, argv);
        break;
        
    case etdsAddScopes:
        ret = onvif_tds_AddScopes_rly(p_xml, p_dev, argv);
        break;
        
    case etdsRemoveScopes:
        ret = onvif_tds_RemoveScopes_rly(p_xml, p_dev, argv);
        break;

    case etdsStartFirmwareUpgrade:
        ret = onvif_tds_StartFirmwareUpgrade_rly(p_xml, p_dev, argv);
        break;

    case etdsGetSystemUris:
        ret = onvif_tds_GetSystemUris_rly(p_xml, p_dev, argv);
        break;
        
    case etdsStartSystemRestore:
        ret = onvif_tds_StartSystemRestore_rly(p_xml, p_dev, argv);
        break;

    case etdsGetWsdlUrl:
        ret = onvif_tds_GetWsdlUrl_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetDot11Capabilities:
        ret = onvif_tds_GetDot11Capabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetDot11Status:
        ret = onvif_tds_GetDot11Status_rly(p_xml, p_dev, argv);
        break;
        
    case etdsScanAvailableDot11Networks:
        ret = onvif_tds_ScanAvailableDot11Networks_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetGeoLocation:
        ret = onvif_tds_GetGeoLocation_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetGeoLocation:
        ret = onvif_tds_SetGeoLocation_rly(p_xml, p_dev, argv);
        break;
        
    case etdsDeleteGeoLocation:
        ret = onvif_tds_DeleteGeoLocation_rly(p_xml, p_dev, argv);
        break;

    case etdsSetHashingAlgorithm:
        ret = onvif_tds_SetHashingAlgorithm_rly(p_xml, p_dev, argv);
        break;
        
#ifdef IPFILTER_SUPPORT
    case etdsGetIPAddressFilter:
        ret = onvif_tds_GetIPAddressFilter_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetIPAddressFilter:
        ret = onvif_tds_SetIPAddressFilter_rly(p_xml, p_dev, argv);
        break;
        
    case etdsAddIPAddressFilter:
        ret = onvif_tds_AddIPAddressFilter_rly(p_xml, p_dev, argv);
        break;
        
    case etdsRemoveIPAddressFilter:
        ret = onvif_tds_RemoveIPAddressFilter_rly(p_xml, p_dev, argv);
        break;
#endif // end of IPFILTER_SUPPORT

    case etdsGetAccessPolicy:
        ret = onvif_tds_GetAccessPolicy_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetAccessPolicy:
        ret = onvif_tds_SetAccessPolicy_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetStorageConfigurations:
        ret = onvif_tds_GetStorageConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etdsCreateStorageConfiguration:
        ret = onvif_tds_CreateStorageConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etdsGetStorageConfiguration:
        ret = onvif_tds_GetStorageConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etdsSetStorageConfiguration:
        ret = onvif_tds_SetStorageConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etdsDeleteStorageConfiguration:
        ret = onvif_tds_DeleteStorageConfiguration_rly(p_xml, p_dev, argv);
        break;

    // onvif media service interfaces

    case etrtGetServiceCapabilities:
        ret = onvif_trt_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoSources:
        ret = onvif_trt_GetVideoSources_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioSources:
        ret = onvif_trt_GetAudioSources_rly(p_xml, p_dev, argv);
        break;
        
    case etrtCreateProfile:
        ret = onvif_trt_CreateProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetProfile:
        ret = onvif_trt_GetProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetProfiles:
        ret = onvif_trt_GetProfiles_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddVideoEncoderConfiguration:
        ret = onvif_trt_AddVideoEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddVideoSourceConfiguration:
        ret = onvif_trt_AddVideoSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddAudioEncoderConfiguration:
        ret = onvif_trt_AddAudioEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddAudioSourceConfiguration:
        ret = onvif_trt_AddAudioSourceConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etrtGetVideoSourceModes:
        ret = onvif_trt_GetVideoSourceModes_rly(p_xml, p_dev, argv);
        break;

    case etrtSetVideoSourceMode:
        ret = onvif_trt_SetVideoSourceMode_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddPTZConfiguration:
        ret = onvif_trt_AddPTZConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveVideoEncoderConfiguration:
        ret = onvif_trt_RemoveVideoEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveVideoSourceConfiguration:
        ret = onvif_trt_RemoveVideoSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveAudioEncoderConfiguration:
        ret = onvif_trt_RemoveAudioEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveAudioSourceConfiguration:
        ret = onvif_trt_RemoveAudioSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemovePTZConfiguration:
        ret = onvif_trt_RemovePTZConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtDeleteProfile:
        ret = onvif_trt_DeleteProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoSourceConfigurations:
        ret = onvif_trt_GetVideoSourceConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoEncoderConfigurations:    
        ret = onvif_trt_GetVideoEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioSourceConfigurations:
        ret = onvif_trt_GetAudioSourceConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioEncoderConfigurations:
        ret = onvif_trt_GetAudioEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoSourceConfiguration:
        ret = onvif_trt_GetVideoSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoEncoderConfiguration:
        ret = onvif_trt_GetVideoEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioSourceConfiguration:
        ret = onvif_trt_GetAudioSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioEncoderConfiguration:
        ret = onvif_trt_GetAudioEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetVideoSourceConfiguration:
        ret = onvif_trt_SetVideoSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetVideoEncoderConfiguration:
        ret = onvif_trt_SetVideoEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetAudioSourceConfiguration:
        ret = onvif_trt_SetAudioSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetAudioEncoderConfiguration:
        ret = onvif_trt_SetAudioEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoSourceConfigurationOptions:
        ret = onvif_trt_GetVideoSourceConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoEncoderConfigurationOptions:
        ret = onvif_trt_GetVideoEncoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioSourceConfigurationOptions:
        ret = onvif_trt_GetAudioSourceConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioEncoderConfigurationOptions:
        ret = onvif_trt_GetAudioEncoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetStreamUri:
        ret = onvif_trt_GetStreamUri_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetSynchronizationPoint:
        ret = onvif_trt_SetSynchronizationPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetSnapshotUri:
        ret = onvif_trt_GetSnapshotUri_rly(p_xml, p_dev, argv);
        break;

    case etrtGetGuaranteedNumberOfVideoEncoderInstances:
        ret = onvif_trt_GetGuaranteedNumberOfVideoEncoderInstances_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioOutputs:
        ret = onvif_trt_GetAudioOutputs_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioOutputConfigurations:
        ret = onvif_trt_GetAudioOutputConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioOutputConfiguration:
        ret = onvif_trt_GetAudioOutputConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioOutputConfigurationOptions:
        ret = onvif_trt_GetAudioOutputConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetAudioOutputConfiguration:
        ret = onvif_trt_SetAudioOutputConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioDecoderConfigurations:
        ret = onvif_trt_GetAudioDecoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioDecoderConfiguration:
        ret = onvif_trt_GetAudioDecoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetAudioDecoderConfigurationOptions:
        ret = onvif_trt_GetAudioDecoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetAudioDecoderConfiguration:
        ret = onvif_trt_SetAudioDecoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddAudioOutputConfiguration:
        ret = onvif_trt_AddAudioOutputConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddAudioDecoderConfiguration:
        ret = onvif_trt_AddAudioDecoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveAudioOutputConfiguration:
        ret = onvif_trt_RemoveAudioOutputConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveAudioDecoderConfiguration:
        ret = onvif_trt_RemoveAudioDecoderConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etrtGetOSDs:
        ret = onvif_trt_GetOSDs_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetOSD:
        ret = onvif_trt_GetOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetOSD:
        ret = onvif_trt_SetOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetOSDOptions:
        ret = onvif_trt_GetOSDOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrtCreateOSD:
        ret = onvif_trt_CreateOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etrtDeleteOSD:
        ret = onvif_trt_DeleteOSD_rly(p_xml, p_dev, argv);
        break;
    
    case etrtGetVideoAnalyticsConfigurations:
        ret = onvif_trt_GetVideoAnalyticsConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddVideoAnalyticsConfiguration:
        ret = onvif_trt_AddVideoAnalyticsConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetVideoAnalyticsConfiguration:
        ret = onvif_trt_GetVideoAnalyticsConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveVideoAnalyticsConfiguration:
        ret = onvif_trt_RemoveVideoAnalyticsConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetVideoAnalyticsConfiguration:
        ret = onvif_trt_SetVideoAnalyticsConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etrtGetMetadataConfigurations:
        ret = onvif_trt_GetMetadataConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtAddMetadataConfiguration:
        ret = onvif_trt_AddMetadataConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetMetadataConfiguration:
        ret = onvif_trt_GetMetadataConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtRemoveMetadataConfiguration:
        ret = onvif_trt_RemoveMetadataConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrtSetMetadataConfiguration:
        ret = onvif_trt_SetMetadataConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etrtGetMetadataConfigurationOptions:
        ret = onvif_trt_GetMetadataConfigurationOptions_rly(p_xml, p_dev, argv);
        break;

    case etrtGetCompatibleVideoEncoderConfigurations:
        ret = onvif_trt_GetCompatibleVideoEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetCompatibleAudioEncoderConfigurations:
        ret = onvif_trt_GetCompatibleAudioEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetCompatibleVideoAnalyticsConfigurations:
        ret = onvif_trt_GetCompatibleVideoAnalyticsConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etrtGetCompatibleMetadataConfigurations:
        ret = onvif_trt_GetCompatibleMetadataConfigurations_rly(p_xml, p_dev, argv);
        break;
    
    // onvif ptz service interfaces

    case eptzGetServiceCapabilities:
        ret = onvif_ptz_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetNodes:
        ret = onvif_ptz_GetNodes_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetNode:
        ret = onvif_ptz_GetNode_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetPresets:
        ret = onvif_ptz_GetPresets_rly(p_xml, p_dev, argv);
        break;
        
    case eptzSetPreset:
        ret = onvif_ptz_SetPreset_rly(p_xml, p_dev, argv);
        break;
        
    case eptzRemovePreset:
        ret = onvif_ptz_RemovePreset_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGotoPreset:
        ret = onvif_ptz_GotoPreset_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGotoHomePosition:
        ret = onvif_ptz_GotoHomePosition_rly(p_xml, p_dev, argv);
        break;
        
    case eptzSetHomePosition:
        ret = onvif_ptz_SetHomePosition_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetStatus:
        ret = onvif_ptz_GetStatus_rly(p_xml, p_dev, argv);
        break;
        
    case eptzContinuousMove:
        ret = onvif_ptz_ContinuousMove_rly(p_xml, p_dev, argv);
        break;
        
    case eptzRelativeMove:
        ret = onvif_ptz_RelativeMove_rly(p_xml, p_dev, argv);
        break;
        
    case eptzAbsoluteMove:
        ret = onvif_ptz_AbsoluteMove_rly(p_xml, p_dev, argv);
        break;
        
    case eptzStop:
        ret = onvif_ptz_Stop_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetConfigurations:
        ret = onvif_ptz_GetConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetConfiguration:
        ret = onvif_ptz_GetConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case eptzSetConfiguration:
        ret = onvif_ptz_SetConfiguration_rly(p_xml, p_dev, argv);
        break;
            
    case eptzGetConfigurationOptions:
        ret = onvif_ptz_GetConfigurationOptions_rly(p_xml, p_dev, argv);
        break;

    case eptzGetPresetTours:
        ret = onvif_ptz_GetPresetTours_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetPresetTour:
        ret = onvif_ptz_GetPresetTour_rly(p_xml, p_dev, argv);
        break;
        
    case eptzGetPresetTourOptions:
        ret = onvif_ptz_GetPresetTourOptions_rly(p_xml, p_dev, argv);
        break;
        
    case eptzCreatePresetTour:
        ret = onvif_ptz_CreatePresetTour_rly(p_xml, p_dev, argv);
        break;
        
    case eptzModifyPresetTour:
        ret = onvif_ptz_ModifyPresetTour_rly(p_xml, p_dev, argv);
        break;
        
    case eptzOperatePresetTour:
        ret = onvif_ptz_OperatePresetTour_rly(p_xml, p_dev, argv);
        break;
        
    case eptzRemovePresetTour:
        ret = onvif_ptz_RemovePresetTour_rly(p_xml, p_dev, argv);
        break;

    case eptzSendAuxiliaryCommand:
        ret = onvif_ptz_SendAuxiliaryCommand_rly(p_xml, p_dev, argv);
        break;

    case eptzGeoMove:
        ret = onvif_ptz_GeoMove_rly(p_xml, p_dev, argv);
        break;
        
    // onvif event service interfaces

    case etevGetServiceCapabilities:
        ret = onvif_tev_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
    
    case etevGetEventProperties:
        ret = onvif_tev_GetEventProperties_rly(p_xml, p_dev, argv);
        break;
    
    case etevRenew:
        ret = onvif_tev_Renew_rly(p_xml, p_dev, argv);
        break;
    
    case etevUnsubscribe:
        ret = onvif_tev_Unsubscribe_rly(p_xml, p_dev, argv);
        break;
    
    case etevSubscribe:
        ret = onvif_tev_Subscribe_rly(p_xml, p_dev, argv);
        break;

    case etevPauseSubscription:
        ret = onvif_tev_PauseSubscription_rly(p_xml, p_dev, argv);
        break;
        
    case etevResumeSubscription:
        ret = onvif_tev_ResumeSubscription_rly(p_xml, p_dev, argv);
        break;
    
    case etevCreatePullPointSubscription:
        ret = onvif_tev_CreatePullPointSubscription_rly(p_xml, p_dev, argv);
        break;

    case etevDestroyPullPoint:
        ret = onvif_tev_DestroyPullPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etevPullMessages:
        ret = onvif_tev_PullMessages_rly(p_xml, p_dev, argv);
        break;

    case etevGetMessages:
        ret = onvif_tev_GetMessages_rly(p_xml, p_dev, argv);
        break;
        
    case etevSeek:
        ret = onvif_tev_Seek_rly(p_xml, p_dev, argv);
        break;

    case etevSetSynchronizationPoint:
        ret = onvif_tev_SetSynchronizationPoint_rly(p_xml, p_dev, argv);
        break;
        
    // onvif imaging service interfaces
    
    case eimgGetServiceCapabilities:
        ret = onvif_img_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case eimgGetImagingSettings:
        ret = onvif_img_GetImagingSettings_rly(p_xml, p_dev, argv);
        break;
    
    case eimgSetImagingSettings:
        ret = onvif_img_SetImagingSettings_rly(p_xml, p_dev, argv);
        break; 

    case eimgGetOptions:
        ret = onvif_img_GetOptions_rly(p_xml, p_dev, argv);
        break;

    case eimgMove:
        ret = onvif_img_Move_rly(p_xml, p_dev, argv);
        break;
        
    case eimgStop:
        ret = onvif_img_Stop_rly(p_xml, p_dev, argv);
        break;
        
    case eimgGetStatus:
        ret = onvif_img_GetStatus_rly(p_xml, p_dev, argv);
        break;
        
    case eimgGetMoveOptions:
        ret = onvif_img_GetMoveOptions_rly(p_xml, p_dev, argv);
        break;

    case eimgGetPresets:
        ret = onvif_img_GetPresets_rly(p_xml, p_dev, argv);
        break;
        
    case eimgGetCurrentPreset:
        ret = onvif_img_GetCurrentPreset_rly(p_xml, p_dev, argv);
        break;
        
    case eimgSetCurrentPreset:
        ret = onvif_img_SetCurrentPreset_rly(p_xml, p_dev, argv);
        break;

    // onvif analytics service interfaces

    case etanGetServiceCapabilities:
        ret = onvif_tan_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etanGetSupportedRules:
        ret = onvif_tan_GetSupportedRules_rly(p_xml, p_dev, argv);
        break;
        
    case etanCreateRules:
        ret = onvif_tan_CreateRules_rly(p_xml, p_dev, argv);
        break;
        
    case etanDeleteRules:
        ret = onvif_tan_DeleteRules_rly(p_xml, p_dev, argv);
        break;
        
    case etanGetRules:
        ret = onvif_tan_GetRules_rly(p_xml, p_dev, argv);
        break;
        
    case etanModifyRules:
        ret = onvif_tan_ModifyRules_rly(p_xml, p_dev, argv);
        break;
        
    case etanCreateAnalyticsModules:
        ret = onvif_tan_CreateAnalyticsModules_rly(p_xml, p_dev, argv);
        break;
        
    case etanDeleteAnalyticsModules:
        ret = onvif_tan_DeleteAnalyticsModules_rly(p_xml, p_dev, argv);
        break;
        
    case etanGetAnalyticsModules:
        ret = onvif_tan_GetAnalyticsModules_rly(p_xml, p_dev, argv);
        break;
        
    case etanModifyAnalyticsModules:
        ret = onvif_tan_ModifyAnalyticsModules_rly(p_xml, p_dev, argv);
        break;

    case etanGetSupportedAnalyticsModules:
        ret = onvif_tan_GetSupportedAnalyticsModules_rly(p_xml, p_dev, argv);
        break;
        
    case etanGetRuleOptions:
        ret = onvif_tan_GetRuleOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etanGetAnalyticsModuleOptions:
        ret = onvif_tan_GetAnalyticsModuleOptions_rly(p_xml, p_dev, argv);
        break;

    case etanGetSupportedMetadata:
        ret = onvif_tan_GetSupportedMetadata_rly(p_xml, p_dev, argv);
        break;
        
    // onvif media 2 service interfaces

    case etr2GetServiceCapabilities:
        ret = onvif_tr2_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetVideoEncoderConfigurations:
        ret = onvif_tr2_GetVideoEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetVideoEncoderConfiguration:
        ret = onvif_tr2_SetVideoEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetVideoEncoderConfigurationOptions:
        ret = onvif_tr2_GetVideoEncoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;

    case etr2GetProfiles:
        ret = onvif_tr2_GetProfiles_rly(p_xml, p_dev, argv);
        break;
        
    case etr2CreateProfile:
        ret = onvif_tr2_CreateProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etr2DeleteProfile:
        ret = onvif_tr2_DeleteProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetStreamUri:
        ret = onvif_tr2_GetStreamUri_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetVideoSourceConfigurations:
        ret = onvif_tr2_GetVideoSourceConfigurations_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetVideoSourceConfigurationOptions:
        ret = onvif_tr2_GetVideoSourceConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
         
    case etr2SetVideoSourceConfiguration:
        ret = onvif_tr2_SetVideoSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2SetSynchronizationPoint:
        ret = onvif_tr2_SetSynchronizationPoint_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetMetadataConfigurations:
        ret = onvif_tr2_GetMetadataConfigurations_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetMetadataConfigurationOptions:
        ret = onvif_tr2_GetMetadataConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
         
    case etr2SetMetadataConfiguration:
        ret = onvif_tr2_SetMetadataConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetAudioEncoderConfigurations:
        ret = onvif_tr2_GetAudioEncoderConfigurations_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetAudioSourceConfigurations:
        ret = onvif_tr2_GetAudioSourceConfigurations_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetAudioSourceConfigurationOptions:
        ret = onvif_tr2_GetAudioSourceConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
         
    case etr2SetAudioSourceConfiguration:
        ret = onvif_tr2_SetAudioSourceConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2SetAudioEncoderConfiguration:
        ret = onvif_tr2_SetAudioEncoderConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetAudioEncoderConfigurationOptions:
        ret = onvif_tr2_GetAudioEncoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
         
    case etr2AddConfiguration:
        ret = onvif_tr2_AddConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2RemoveConfiguration:
        ret = onvif_tr2_RemoveConfiguration_rly(p_xml, p_dev, argv);
        break;
         
    case etr2GetVideoEncoderInstances:
        ret = onvif_tr2_GetVideoEncoderInstances_rly(p_xml, p_dev, argv);
        break;         

    case etr2GetAudioOutputConfigurations:
        ret = onvif_tr2_GetAudioOutputConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetAudioOutputConfigurationOptions:
        ret = onvif_tr2_GetAudioOutputConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetAudioOutputConfiguration:
        ret = onvif_tr2_SetAudioOutputConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetAudioDecoderConfigurations:
        ret = onvif_tr2_GetAudioDecoderConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetAudioDecoderConfigurationOptions:
        ret = onvif_tr2_GetAudioDecoderConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetAudioDecoderConfiguration:
        ret = onvif_tr2_SetAudioDecoderConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetSnapshotUri:
        ret = onvif_tr2_GetSnapshotUri_rly(p_xml, p_dev, argv);
        break;
        
    case etr2StartMulticastStreaming:
        ret = onvif_tr2_StartMulticastStreaming_rly(p_xml, p_dev, argv);
        break;
        
    case etr2StopMulticastStreaming:
        ret = onvif_tr2_StopMulticastStreaming_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetVideoSourceModes:
        ret = onvif_tr2_GetVideoSourceModes_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetVideoSourceMode:
        ret = onvif_tr2_SetVideoSourceMode_rly(p_xml, p_dev, argv);
        break;
        
    case etr2CreateOSD:
        ret = onvif_tr2_CreateOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etr2DeleteOSD:
        ret = onvif_tr2_DeleteOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetOSDs:
        ret = onvif_tr2_GetOSDs_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetOSD:
        ret = onvif_tr2_SetOSD_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetOSDOptions:
        ret = onvif_tr2_GetOSDOptions_rly(p_xml, p_dev, argv);
        break;

    case etr2GetAnalyticsConfigurations:
        ret = onvif_tr2_GetAnalyticsConfigurations_rly(p_xml, p_dev, argv);
        break;

    case etr2GetMasks:
        ret = onvif_tr2_GetMasks_rly(p_xml, p_dev, argv);
        break;
        
    case etr2SetMask:
        ret = onvif_tr2_SetMask_rly(p_xml, p_dev, argv);
        break;
        
    case etr2CreateMask:
        ret = onvif_tr2_CreateMask_rly(p_xml, p_dev, argv);
        break;
        
    case etr2DeleteMask:
        ret = onvif_tr2_DeleteMask_rly(p_xml, p_dev, argv);
        break;
        
    case etr2GetMaskOptions:
        ret = onvif_tr2_GetMaskOptions_rly(p_xml, p_dev, argv);
        break;

#ifdef DEVICEIO_SUPPORT

    // onvif device IO service interfaces

    case etmdGetServiceCapabilities:
        ret = onvif_tmd_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetRelayOutputs:
        ret = onvif_tmd_GetRelayOutputs_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetRelayOutputOptions:
        ret = onvif_tmd_GetRelayOutputOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etmdSetRelayOutputSettings:
        ret = onvif_tmd_SetRelayOutputSettings_rly(p_xml, p_dev, argv);
        break;
        
    case etmdSetRelayOutputState:
        ret = onvif_tmd_SetRelayOutputState_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetDigitalInputs:
        ret = onvif_tmd_GetDigitalInputs_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetDigitalInputConfigurationOptions:
        ret = onvif_tmd_GetDigitalInputConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etmdSetDigitalInputConfigurations:
        ret = onvif_tmd_SetDigitalInputConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetSerialPorts:
        ret = onvif_tmd_GetSerialPorts_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetSerialPortConfiguration:
        ret = onvif_tmd_GetSerialPortConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etmdSetSerialPortConfiguration:
        ret = onvif_tmd_SetSerialPortConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etmdGetSerialPortConfigurationOptions:
        ret = onvif_tmd_GetSerialPortConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etmdSendReceiveSerialCommand:
        ret = onvif_tmd_SendReceiveSerialCommand_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT

    // onvif recording service interfaces

    case etrcGetServiceCapabilities:
        ret = onvif_trc_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etrcCreateRecording:
        ret = onvif_trc_CreateRecording_rly(p_xml, p_dev, argv);
        break;
          
    case etrcDeleteRecording:
        ret = onvif_trc_DeleteRecording_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordings:
        ret = onvif_trc_GetRecordings_rly(p_xml, p_dev, argv);
        break;
        
    case etrcSetRecordingConfiguration:
        ret = onvif_trc_SetRecordingConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordingConfiguration:
        ret = onvif_trc_GetRecordingConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordingOptions:
        ret = onvif_trc_GetRecordingOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etrcCreateTrack:
        ret = onvif_trc_CreateTrack_rly(p_xml, p_dev, argv);
        break;
        
    case etrcDeleteTrack:
        ret = onvif_trc_DeleteTrack_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetTrackConfiguration:
        ret = onvif_trc_GetTrackConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcSetTrackConfiguration:
        ret = onvif_trc_SetTrackConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcCreateRecordingJob:
        ret = onvif_trc_CreateRecordingJob_rly(p_xml, p_dev, argv);
        break;
        
    case etrcDeleteRecordingJob:
        ret = onvif_trc_DeleteRecordingJob_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordingJobs:
        ret = onvif_trc_GetRecordingJobs_rly(p_xml, p_dev, argv);
        break;
        
    case etrcSetRecordingJobConfiguration:
        ret = onvif_trc_SetRecordingJobConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordingJobConfiguration:
        ret = onvif_trc_GetRecordingJobConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etrcSetRecordingJobMode:
        ret = onvif_trc_SetRecordingJobMode_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetRecordingJobState:
        ret = onvif_trc_GetRecordingJobState_rly(p_xml, p_dev, argv);
        break;        

    case etrcExportRecordedData:
        ret = onvif_trc_ExportRecordedData_rly(p_xml, p_dev, argv);
        break;
        
    case etrcStopExportRecordedData:
        ret = onvif_trc_StopExportRecordedData_rly(p_xml, p_dev, argv);
        break;
        
    case etrcGetExportRecordedDataState:
        ret = onvif_trc_GetExportRecordedDataState_rly(p_xml, p_dev, argv);
        break;
    
    // onvif replay service interfaces

    case etrpGetServiceCapabilities:
        ret = onvif_trp_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etrpGetReplayUri:
        ret = onvif_trp_GetReplayUri_rly(p_xml, p_dev, argv);
        break;

    case etrpGetReplayConfiguration:
        ret = onvif_trp_GetReplayConfiguration_rly(p_xml, p_dev, argv);
        break;

    case etrpSetReplayConfiguration:
        ret = onvif_trp_SetReplayConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    // onvif search service interfaces

    case etseGetServiceCapabilities:
        ret = onvif_tse_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetRecordingSummary:
        ret = onvif_tse_GetRecordingSummary_rly(p_xml, p_dev, argv);
        break;

    case etseGetRecordingInformation:
        ret = onvif_tse_GetRecordingInformation_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetMediaAttributes:
        ret = onvif_tse_GetMediaAttributes_rly(p_xml, p_dev, argv);
        break;
        
    case etseFindRecordings:
        ret = onvif_tse_FindRecordings_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetRecordingSearchResults:
        ret = onvif_tse_GetRecordingSearchResults_rly(p_xml, p_dev, argv);
        break;
        
    case etseFindEvents:
        ret = onvif_tse_FindEvents_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetEventSearchResults:
        ret = onvif_tse_GetEventSearchResults_rly(p_xml, p_dev, argv);
        break;

    case etseFindMetadata:
        ret = onvif_tse_FindMetadata_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetMetadataSearchResults:
        ret = onvif_tse_GetMetadataSearchResults_rly(p_xml, p_dev, argv);
        break;
        
    case etseFindPTZPosition:
        ret = onvif_tse_FindPTZPosition_rly(p_xml, p_dev, argv);
        break;
        
    case etseGetPTZPositionSearchResults:
        ret = onvif_tse_GetPTZPositionSearchResults_rly(p_xml, p_dev, argv);
        break;
    
    case etseGetSearchState:
        ret = onvif_tse_GetSearchState_rly(p_xml, p_dev, argv);
        break;
        
    case etseEndSearch:
        ret = onvif_tse_EndSearch_rly(p_xml, p_dev, argv);
        break;   

#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

    // onvif access control interfaces

    case etacGetServiceCapabilities:
        ret = onvif_tac_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etacGetAccessPointInfoList:
        ret = onvif_tac_GetAccessPointInfoList_rly(p_xml, p_dev, argv);
        break;
          
    case etacGetAccessPointInfo:
        ret = onvif_tac_GetAccessPointInfo_rly(p_xml, p_dev, argv);
        break;

    case etacGetAccessPointList:
        ret = onvif_tac_GetAccessPointList_rly(p_xml, p_dev, argv);
        break;
        
    case etacGetAccessPoints:
        ret = onvif_tac_GetAccessPoints_rly(p_xml, p_dev, argv);
        break;
        
    case etacCreateAccessPoint:
        ret = onvif_tac_CreateAccessPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etacSetAccessPoint:
        ret = onvif_tac_SetAccessPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etacModifyAccessPoint:
        ret = onvif_tac_ModifyAccessPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etacDeleteAccessPoint:
        ret = onvif_tac_DeleteAccessPoint_rly(p_xml, p_dev, argv);
        break;
    
    case etacGetAreaInfoList:
        ret = onvif_tac_GetAreaInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etacGetAreaInfo:
        ret = onvif_tac_GetAreaInfo_rly(p_xml, p_dev, argv);
        break;

    case etacGetAreaList:
        ret = onvif_tac_GetAreaList_rly(p_xml, p_dev, argv);
        break;
        
    case etacGetAreas:
        ret = onvif_tac_GetAreas_rly(p_xml, p_dev, argv);
        break;
        
    case etacCreateArea:
        ret = onvif_tac_CreateArea_rly(p_xml, p_dev, argv);
        break;
        
    case etacSetArea:
        ret = onvif_tac_SetArea_rly(p_xml, p_dev, argv);
        break;
        
    case etacModifyArea:
        ret = onvif_tac_ModifyArea_rly(p_xml, p_dev, argv);
        break;
        
    case etacDeleteArea:
        ret = onvif_tac_DeleteArea_rly(p_xml, p_dev, argv);
        break;
    
    case etacGetAccessPointState:
        ret = onvif_tac_GetAccessPointState_rly(p_xml, p_dev, argv);
        break;

    case etacEnableAccessPoint:
        ret = onvif_tac_EnableAccessPoint_rly(p_xml, p_dev, argv);
        break;
        
    case etacDisableAccessPoint:
        ret = onvif_tac_DisableAccessPoint_rly(p_xml, p_dev, argv);
        break;

    // onvif door control service interfaces

    case etdcGetServiceCapabilities:
        ret = onvif_tdc_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etdcGetDoorInfoList:
        ret = onvif_tdc_GetDoorInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etdcGetDoorInfo:
        ret = onvif_tdc_GetDoorInfo_rly(p_xml, p_dev, argv);
        break;
        
    case etdcGetDoorState:
        ret = onvif_tdc_GetDoorState_rly(p_xml, p_dev, argv);
        break;
        
    case etdcAccessDoor:
        ret = onvif_tdc_AccessDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcLockDoor:
        ret = onvif_tdc_LockDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcUnlockDoor:
        ret = onvif_tdc_UnlockDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcDoubleLockDoor:
        ret = onvif_tdc_DoubleLockDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcBlockDoor:
        ret = onvif_tdc_BlockDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcLockDownDoor:
        ret = onvif_tdc_LockDownDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcLockDownReleaseDoor:
        ret = onvif_tdc_LockDownReleaseDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcLockOpenDoor:
        ret = onvif_tdc_LockOpenDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcLockOpenReleaseDoor:
        ret = onvif_tdc_LockOpenReleaseDoor_rly(p_xml, p_dev, argv);
        break;

    case etdcGetDoors:
        ret = onvif_tdc_GetDoors_rly(p_xml, p_dev, argv);
        break;
        
    case etdcGetDoorList:
        ret = onvif_tdc_GetDoorList_rly(p_xml, p_dev, argv);
        break;
    
    case etdcCreateDoor:
        ret = onvif_tdc_CreateDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcSetDoor:
        ret = onvif_tdc_SetDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcModifyDoor:
        ret = onvif_tdc_ModifyDoor_rly(p_xml, p_dev, argv);
        break;
        
    case etdcDeleteDoor:
        ret = onvif_tdc_DeleteDoor_rly(p_xml, p_dev, argv);
        break;
    
#endif // end of PROFILE_C_SUPPORT
    
#ifdef THERMAL_SUPPORT

    // onvif thermal service interfaces

    case etthGetServiceCapabilities:
        ret = onvif_tth_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etthGetConfigurations:
        ret = onvif_tth_GetConfigurations_rly(p_xml, p_dev, argv);
        break;
        
    case etthGetConfiguration:
        ret = onvif_tth_GetConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etthSetConfiguration:
        ret = onvif_tth_SetConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etthGetConfigurationOptions:
        ret = onvif_tth_GetConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
    case etthGetRadiometryConfiguration:
        ret = onvif_tth_GetRadiometryConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etthSetRadiometryConfiguration:
        ret = onvif_tth_SetRadiometryConfiguration_rly(p_xml, p_dev, argv);
        break;
        
    case etthGetRadiometryConfigurationOptions:
        ret = onvif_tth_GetRadiometryConfigurationOptions_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

    // onvif credential service interfaces

    case etcrGetServiceCapabilities:
        ret = onvif_tcr_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialInfo:
        ret = onvif_tcr_GetCredentialInfo_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialInfoList:
        ret = onvif_tcr_GetCredentialInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentials:
        ret = onvif_tcr_GetCredentials_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialList:
        ret = onvif_tcr_GetCredentialList_rly(p_xml, p_dev, argv);
        break;
        
    case etcrCreateCredential:
        ret = onvif_tcr_CreateCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrModifyCredential:
        ret = onvif_tcr_ModifyCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrDeleteCredential:
        ret = onvif_tcr_DeleteCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialState:
        ret = onvif_tcr_GetCredentialState_rly(p_xml, p_dev, argv);
        break;
        
    case etcrEnableCredential:
        ret = onvif_tcr_EnableCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrDisableCredential:
        ret = onvif_tcr_DisableCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrSetCredential:
        ret = onvif_tcr_SetCredential_rly(p_xml, p_dev, argv);
        break;
        
    case etcrResetAntipassbackViolation:
        ret = onvif_tcr_ResetAntipassbackViolation_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetSupportedFormatTypes:
        ret = onvif_tcr_GetSupportedFormatTypes_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialIdentifiers:
        ret = onvif_tcr_GetCredentialIdentifiers_rly(p_xml, p_dev, argv);
        break;
        
    case etcrSetCredentialIdentifier:
        ret = onvif_tcr_SetCredentialIdentifier_rly(p_xml, p_dev, argv);
        break;
        
    case etcrDeleteCredentialIdentifier:
        ret = onvif_tcr_DeleteCredentialIdentifier_rly(p_xml, p_dev, argv);
        break;
        
    case etcrGetCredentialAccessProfiles:
        ret = onvif_tcr_GetCredentialAccessProfiles_rly(p_xml, p_dev, argv);
        break;
        
    case etcrSetCredentialAccessProfiles:
        ret = onvif_tcr_SetCredentialAccessProfiles_rly(p_xml, p_dev, argv);
        break;
        
    case etcrDeleteCredentialAccessProfiles:
        ret = onvif_tcr_DeleteCredentialAccessProfiles_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

    // onvif access rules service interfaces

    case etarGetServiceCapabilities:
        ret = onvif_tar_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etarGetAccessProfileInfo:
        ret = onvif_tar_GetAccessProfileInfo_rly(p_xml, p_dev, argv);
        break;
        
    case etarGetAccessProfileInfoList:
        ret = onvif_tar_GetAccessProfileInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etarGetAccessProfiles:
        ret = onvif_tar_GetAccessProfiles_rly(p_xml, p_dev, argv);
        break;
        
    case etarGetAccessProfileList:
        ret = onvif_tar_GetAccessProfileList_rly(p_xml, p_dev, argv);
        break;
        
    case etarCreateAccessProfile:
        ret = onvif_tar_CreateAccessProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etarModifyAccessProfile:
        ret = onvif_tar_ModifyAccessProfile_rly(p_xml, p_dev, argv);
        break;
        
    case etarDeleteAccessProfile:
        ret = onvif_tar_DeleteAccessProfile_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

    // onvif schedule service interfaces

    case etscGetServiceCapabilities:
        ret = onvif_tsc_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetScheduleInfo:
        ret = onvif_tsc_GetScheduleInfo_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetScheduleInfoList:
        ret = onvif_tsc_GetScheduleInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetSchedules:
        ret = onvif_tsc_GetSchedules_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetScheduleList:
        ret = onvif_tsc_GetScheduleList_rly(p_xml, p_dev, argv);
        break;
        
    case etscCreateSchedule:
        ret = onvif_tsc_CreateSchedule_rly(p_xml, p_dev, argv);
        break;
        
    case etscModifySchedule:
        ret = onvif_tsc_ModifySchedule_rly(p_xml, p_dev, argv);
        break;
        
    case etscDeleteSchedule:
        ret = onvif_tsc_DeleteSchedule_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetSpecialDayGroupInfo:
        ret = onvif_tsc_GetSpecialDayGroupInfo_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetSpecialDayGroupInfoList:
        ret = onvif_tsc_GetSpecialDayGroupInfoList_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetSpecialDayGroups:
        ret = onvif_tsc_GetSpecialDayGroups_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetSpecialDayGroupList:
        ret = onvif_tsc_GetSpecialDayGroupList_rly(p_xml, p_dev, argv);
        break;
    
    case etscCreateSpecialDayGroup:
        ret = onvif_tsc_CreateSpecialDayGroup_rly(p_xml, p_dev, argv);
        break;
        
    case etscModifySpecialDayGroup:
        ret = onvif_tsc_ModifySpecialDayGroup_rly(p_xml, p_dev, argv);
        break;
        
    case etscDeleteSpecialDayGroup:
        ret = onvif_tsc_DeleteSpecialDayGroup_rly(p_xml, p_dev, argv);
        break;
        
    case etscGetScheduleState:
        ret = onvif_tsc_GetScheduleState_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

    // onvif receiver service interfaces

    case etrvGetServiceCapabilities:
        ret = onvif_trv_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etrvGetReceivers:
        ret = onvif_trv_GetReceivers_rly(p_xml, p_dev, argv);
        break;
        
    case etrvGetReceiver:
        ret = onvif_trv_GetReceiver_rly(p_xml, p_dev, argv);
        break;
        
    case etrvCreateReceiver:
        ret = onvif_trv_CreateReceiver_rly(p_xml, p_dev, argv);
        break;
        
    case etrvDeleteReceiver:
        ret = onvif_trv_DeleteReceiver_rly(p_xml, p_dev, argv);
        break;
        
    case etrvConfigureReceiver:
        ret = onvif_trv_ConfigureReceiver_rly(p_xml, p_dev, argv);
        break;
        
    case etrvSetReceiverMode:
        ret = onvif_trv_SetReceiverMode_rly(p_xml, p_dev, argv);
        break;
        
    case etrvGetReceiverState:
        ret = onvif_trv_GetReceiverState_rly(p_xml, p_dev, argv);
        break;
        
#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

    // onvif provisioning service interfaces
    
    case etpvGetServiceCapabilities:
        ret = onvif_tpv_GetServiceCapabilities_rly(p_xml, p_dev, argv);
        break;
        
    case etpvPanMove:
        ret = onvif_tpv_PanMove_rly(p_xml, p_dev, argv);
        break;
        
    case etpvTiltMove:
        ret = onvif_tpv_TiltMove_rly(p_xml, p_dev, argv);
        break;
        
    case etpvZoomMove:
        ret = onvif_tpv_ZoomMove_rly(p_xml, p_dev, argv);
        break;
        
    case etpvRollMove:
        ret = onvif_tpv_RollMove_rly(p_xml, p_dev, argv);
        break;
        
    case etpvFocusMove:
        ret = onvif_tpv_FocusMove_rly(p_xml, p_dev, argv);
        break;
        
    case etpvStop:
        ret = onvif_tpv_Stop_rly(p_xml, p_dev, argv);
        break;
        
    case etpvGetUsage:
        ret = onvif_tpv_GetUsage_rly(p_xml, p_dev, argv);
        break;

#endif // end of PROVISIONING_SUPPORT

    default:
        break;
    }

    return ret;
}






