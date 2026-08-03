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
#include "http.h"
#include "http_cln.h"
#include "onvif_cln.h" 
#include "soap_parser.h"
#include "onvif_http.h"


/***************************************************************************************/
void onvif_init_req(HTTPREQ * p_req, onvif_XAddr * p_xaddr)
{
    memset(p_req, 0, sizeof(HTTPREQ));
    
    strcpy(p_req->host, p_xaddr->host);
    p_req->port = p_xaddr->port;
    p_req->https = p_xaddr->https;
    
    if (p_xaddr->url[0] != '\0')
    {
        strcpy(p_req->url, p_xaddr->url);
    }
    else
    {
        strcpy(p_req->url, "/onvif/device_service");
    }
}

HT_API void onvif_initDevice(ONVIF_DEVICE * p_dev, const char * host, int port, int https)
{
    p_dev->binfo.XAddr.https = https;
    p_dev->binfo.XAddr.port = port;
    strcpy(p_dev->binfo.XAddr.host, host);
    strcpy(p_dev->binfo.XAddr.url, "/onvif/device_service");
}

HT_API void onvif_SetAuthInfo(ONVIF_DEVICE * p_dev, const char * user, const char * pass)
{
    strncpy(p_dev->username, user, sizeof(p_dev->username)-1);
    strncpy(p_dev->password, pass, sizeof(p_dev->password)-1);
}

HT_API void onvif_SetAuthMethod(ONVIF_DEVICE * p_dev, onvif_AuthMethod method)
{
    if (method == AuthMethod_UsernameToken)
    {
        p_dev->needAuth = 1;
    }
    else
    {
        p_dev->needAuth = 0;
    }
}

HT_API void onvif_SetReqTimeout(ONVIF_DEVICE * p_dev, int timeout)
{
    p_dev->timeout = timeout;
}

HT_API char * onvif_GetErrString(ONVIF_DEVICE * p_dev)
{
    static char error[256] = {'\0'};

    if (NULL == p_dev)
    {
        error[0] = '\0';
        return error;
    }

    if (p_dev->authFailed)
    {
        strcpy(error, "Authentication failed");
        return error;
    }
    
    switch (p_dev->errCode)
    {
    case ONVIF_OK:
        strcpy(error, "OK");
        break;

    case ONVIF_ERR_ConnFailure:
        strcpy(error, "Connect failure");
        break;

    case ONVIF_ERR_MallocFailure:
        strcpy(error, "Memory malloc failure");
        break; 

    case ONVIF_ERR_NotSupportHttps:
        strcpy(error, "Not support https");
        break;

    case ONVIF_ERR_RecvTimeout:
        strcpy(error, "Receive timeout");
        break;

    case ONVIF_ERR_InvalidContentType:
        strcpy(error, "Invalid content type");
        break;

    case ONVIF_ERR_NullContent:
        strcpy(error, "Null content");
        break;

    case ONVIF_ERR_ParseFailed:
        strcpy(error, "Parse failed");
        break;

    case ONVIF_ERR_HandleFailed:
        strcpy(error, "Handle failed");
        break;

    case ONVIF_ERR_HttpResponseError:
        strcpy(error, p_dev->fault.Reason);
        break;    
    }

    return error;
}

/***************************************************************************************/

// onvif device service interfaces

HT_API BOOL onvif_tds_GetCapabilities(ONVIF_DEVICE * p_dev, tds_GetCapabilities_REQ * p_req, tds_GetCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetServices(ONVIF_DEVICE * p_dev, tds_GetServices_REQ * p_req, tds_GetServices_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetServices, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tds_GetServiceCapabilities_REQ * p_req, tds_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDeviceInformation(ONVIF_DEVICE * p_dev, tds_GetDeviceInformation_REQ * p_req, tds_GetDeviceInformation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDeviceInformation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetUsers(ONVIF_DEVICE * p_dev, tds_GetUsers_REQ * p_req, tds_GetUsers_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetUsers, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_CreateUsers(ONVIF_DEVICE * p_dev, tds_CreateUsers_REQ * p_req, tds_CreateUsers_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsCreateUsers, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_DeleteUsers(ONVIF_DEVICE * p_dev, tds_DeleteUsers_REQ * p_req, tds_DeleteUsers_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsDeleteUsers, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetUser(ONVIF_DEVICE * p_dev, tds_SetUser_REQ * p_req, tds_SetUser_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetUser, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetRemoteUser(ONVIF_DEVICE * p_dev, tds_GetRemoteUser_REQ * p_req, tds_GetRemoteUser_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetRemoteUser, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetRemoteUser(ONVIF_DEVICE * p_dev, tds_SetRemoteUser_REQ * p_req, tds_SetRemoteUser_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetRemoteUser, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetNetworkInterfaces(ONVIF_DEVICE * p_dev, tds_GetNetworkInterfaces_REQ * p_req, tds_GetNetworkInterfaces_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetNetworkInterfaces, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetNetworkInterfaces(ONVIF_DEVICE * p_dev, tds_SetNetworkInterfaces_REQ * p_req, tds_SetNetworkInterfaces_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetNetworkInterfaces, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetNTP(ONVIF_DEVICE * p_dev, tds_GetNTP_REQ * p_req, tds_GetNTP_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetNTP, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetNTP(ONVIF_DEVICE * p_dev, tds_SetNTP_REQ * p_req, tds_SetNTP_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetNTP, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetHostname(ONVIF_DEVICE * p_dev, tds_GetHostname_REQ * p_req, tds_GetHostname_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetHostname, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetHostname(ONVIF_DEVICE * p_dev, tds_SetHostname_REQ * p_req, tds_SetHostname_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetHostname, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetHostnameFromDHCP(ONVIF_DEVICE * p_dev, tds_SetHostnameFromDHCP_REQ * p_req, tds_SetHostnameFromDHCP_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetHostnameFromDHCP, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDNS(ONVIF_DEVICE * p_dev, tds_GetDNS_REQ * p_req, tds_GetDNS_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDNS, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetDNS(ONVIF_DEVICE * p_dev, tds_SetDNS_REQ * p_req, tds_SetDNS_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetDNS, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDynamicDNS(ONVIF_DEVICE * p_dev, tds_GetDynamicDNS_REQ * p_req, tds_GetDynamicDNS_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDynamicDNS, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetDynamicDNS(ONVIF_DEVICE * p_dev, tds_SetDynamicDNS_REQ * p_req, tds_SetDynamicDNS_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetDynamicDNS, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetNetworkProtocols(ONVIF_DEVICE * p_dev, tds_GetNetworkProtocols_REQ * p_req, tds_GetNetworkProtocols_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetNetworkProtocols, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetNetworkProtocols(ONVIF_DEVICE * p_dev, tds_SetNetworkProtocols_REQ * p_req, tds_SetNetworkProtocols_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetNetworkProtocols, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDiscoveryMode(ONVIF_DEVICE * p_dev, tds_GetDiscoveryMode_REQ * p_req, tds_GetDiscoveryMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDiscoveryMode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetDiscoveryMode(ONVIF_DEVICE * p_dev, tds_SetDiscoveryMode_REQ * p_req, tds_SetDiscoveryMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetDiscoveryMode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetNetworkDefaultGateway(ONVIF_DEVICE * p_dev, tds_GetNetworkDefaultGateway_REQ * p_req, tds_GetNetworkDefaultGateway_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetNetworkDefaultGateway, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetNetworkDefaultGateway(ONVIF_DEVICE * p_dev, tds_SetNetworkDefaultGateway_REQ * p_req, tds_SetNetworkDefaultGateway_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetNetworkDefaultGateway, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetZeroConfiguration(ONVIF_DEVICE * p_dev, tds_GetZeroConfiguration_REQ * p_req, tds_GetZeroConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetZeroConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetZeroConfiguration(ONVIF_DEVICE * p_dev, tds_SetZeroConfiguration_REQ * p_req, tds_SetZeroConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetZeroConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetEndpointReference(ONVIF_DEVICE * p_dev, tds_GetEndpointReference_REQ * p_req, tds_GetEndpointReference_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetEndpointReference, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SendAuxiliaryCommand(ONVIF_DEVICE * p_dev, tds_SendAuxiliaryCommand_REQ * p_req, tds_SendAuxiliaryCommand_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSendAuxiliaryCommand, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetRelayOutputs(ONVIF_DEVICE * p_dev, tds_GetRelayOutputs_REQ * p_req, tds_GetRelayOutputs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetRelayOutputs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetRelayOutputSettings(ONVIF_DEVICE * p_dev, tds_SetRelayOutputSettings_REQ * p_req, tds_SetRelayOutputSettings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetRelayOutputSettings, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetRelayOutputState(ONVIF_DEVICE * p_dev, tds_SetRelayOutputState_REQ * p_req, tds_SetRelayOutputState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetRelayOutputState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetSystemDateAndTime(ONVIF_DEVICE * p_dev, tds_GetSystemDateAndTime_REQ * p_req, tds_GetSystemDateAndTime_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetSystemDateAndTime, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetSystemDateAndTime(ONVIF_DEVICE * p_dev, tds_SetSystemDateAndTime_REQ * p_req, tds_SetSystemDateAndTime_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetSystemDateAndTime, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SystemReboot(ONVIF_DEVICE * p_dev, tds_SystemReboot_REQ * p_req, tds_SystemReboot_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSystemReboot, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetSystemFactoryDefault(ONVIF_DEVICE * p_dev, tds_SetSystemFactoryDefault_REQ * p_req, tds_SetSystemFactoryDefault_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetSystemFactoryDefault, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetSystemLog(ONVIF_DEVICE * p_dev, tds_GetSystemLog_REQ * p_req, tds_GetSystemLog_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetSystemLog, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetScopes(ONVIF_DEVICE * p_dev, tds_GetScopes_REQ * p_req, tds_GetScopes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetScopes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetScopes(ONVIF_DEVICE * p_dev, tds_SetScopes_REQ * p_req, tds_SetScopes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetScopes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_AddScopes(ONVIF_DEVICE * p_dev, tds_AddScopes_REQ * p_req, tds_AddScopes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsAddScopes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_RemoveScopes(ONVIF_DEVICE * p_dev, tds_RemoveScopes_REQ * p_req, tds_RemoveScopes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsRemoveScopes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_StartFirmwareUpgrade(ONVIF_DEVICE * p_dev, tds_StartFirmwareUpgrade_REQ * p_req, tds_StartFirmwareUpgrade_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsStartFirmwareUpgrade, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetSystemUris(ONVIF_DEVICE * p_dev, tds_GetSystemUris_REQ * p_req, tds_GetSystemUris_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetSystemUris, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_StartSystemRestore(ONVIF_DEVICE * p_dev, tds_StartSystemRestore_REQ * p_req, tds_StartSystemRestore_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsStartSystemRestore, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetWsdlUrl(ONVIF_DEVICE * p_dev, tds_GetWsdlUrl_REQ * p_req, tds_GetWsdlUrl_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetWsdlUrl, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDot11Capabilities(ONVIF_DEVICE * p_dev, tds_GetDot11Capabilities_REQ * p_req, tds_GetDot11Capabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDot11Capabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetDot11Status(ONVIF_DEVICE * p_dev, tds_GetDot11Status_REQ * p_req, tds_GetDot11Status_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetDot11Status, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_ScanAvailableDot11Networks(ONVIF_DEVICE * p_dev, tds_ScanAvailableDot11Networks_REQ * p_req, tds_ScanAvailableDot11Networks_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsScanAvailableDot11Networks, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetGeoLocation(ONVIF_DEVICE * p_dev, tds_GetGeoLocation_REQ * p_req, tds_GetGeoLocation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetGeoLocation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetGeoLocation(ONVIF_DEVICE * p_dev, tds_SetGeoLocation_REQ * p_req, tds_SetGeoLocation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetGeoLocation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_DeleteGeoLocation(ONVIF_DEVICE * p_dev, tds_DeleteGeoLocation_REQ * p_req, tds_DeleteGeoLocation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsDeleteGeoLocation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetHashingAlgorithm(ONVIF_DEVICE * p_dev, tds_SetHashingAlgorithm_REQ * p_req, tds_SetHashingAlgorithm_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetHashingAlgorithm, p_dev, p_req, p_res);
}

#ifdef IPFILTER_SUPPORT

HT_API BOOL onvif_tds_GetIPAddressFilter(ONVIF_DEVICE * p_dev, tds_GetIPAddressFilter_REQ * p_req, tds_GetIPAddressFilter_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetIPAddressFilter, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetIPAddressFilter(ONVIF_DEVICE * p_dev, tds_SetIPAddressFilter_REQ * p_req, tds_SetIPAddressFilter_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetIPAddressFilter, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_AddIPAddressFilter(ONVIF_DEVICE * p_dev, tds_AddIPAddressFilter_REQ * p_req, tds_AddIPAddressFilter_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsAddIPAddressFilter, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_RemoveIPAddressFilter(ONVIF_DEVICE * p_dev, tds_RemoveIPAddressFilter_REQ * p_req, tds_RemoveIPAddressFilter_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsRemoveIPAddressFilter, p_dev, p_req, p_res);
}

#endif // end of IPFILTER_SUPPORT

HT_API BOOL onvif_tds_GetAccessPolicy(ONVIF_DEVICE * p_dev, tds_GetAccessPolicy_REQ * p_req, tds_GetAccessPolicy_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetAccessPolicy, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetAccessPolicy(ONVIF_DEVICE * p_dev, tds_SetAccessPolicy_REQ * p_req, tds_SetAccessPolicy_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetAccessPolicy, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetStorageConfigurations(ONVIF_DEVICE * p_dev, tds_GetStorageConfigurations_REQ * p_req, tds_GetStorageConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetStorageConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_CreateStorageConfiguration(ONVIF_DEVICE * p_dev, tds_CreateStorageConfiguration_REQ * p_req, tds_CreateStorageConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsCreateStorageConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_GetStorageConfiguration(ONVIF_DEVICE * p_dev, tds_GetStorageConfiguration_REQ * p_req, tds_GetStorageConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsGetStorageConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_SetStorageConfiguration(ONVIF_DEVICE * p_dev, tds_SetStorageConfiguration_REQ * p_req, tds_SetStorageConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsSetStorageConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tds_DeleteStorageConfiguration(ONVIF_DEVICE * p_dev, tds_DeleteStorageConfiguration_REQ * p_req, tds_DeleteStorageConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    return http_onvif_trans(&http_req, p_dev->timeout, etdsDeleteStorageConfiguration, p_dev, p_req, p_res);
}

// onvif media service interfaces

HT_API BOOL onvif_trt_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trt_GetServiceCapabilities_REQ * p_req, trt_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoSources(ONVIF_DEVICE * p_dev, trt_GetVideoSources_REQ * p_req, trt_GetVideoSources_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoSources, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioSources(ONVIF_DEVICE * p_dev, trt_GetAudioSources_REQ * p_req, trt_GetAudioSources_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioSources, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_CreateProfile(ONVIF_DEVICE * p_dev, trt_CreateProfile_REQ * p_req, trt_CreateProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtCreateProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetProfile(ONVIF_DEVICE * p_dev, trt_GetProfile_REQ * p_req, trt_GetProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetProfile, p_dev, p_req, p_res);
}


HT_API BOOL onvif_trt_GetProfiles(ONVIF_DEVICE * p_dev, trt_GetProfiles_REQ * p_req, trt_GetProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetProfiles, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoEncoderConfiguration_REQ * p_req, trt_AddVideoEncoderConfiguration_RES * p_res)    
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddVideoEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoSourceConfiguration_REQ * p_req, trt_AddVideoSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddVideoSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioEncoderConfiguration_REQ * p_req, trt_AddAudioEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddAudioEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioSourceConfiguration_REQ * p_req, trt_AddAudioSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddAudioSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoSourceModes(ONVIF_DEVICE * p_dev, trt_GetVideoSourceModes_REQ * p_req, trt_GetVideoSourceModes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoSourceModes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetVideoSourceMode(ONVIF_DEVICE * p_dev, trt_SetVideoSourceMode_REQ * p_req, trt_SetVideoSourceMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetVideoSourceMode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddPTZConfiguration(ONVIF_DEVICE * p_dev, trt_AddPTZConfiguration_REQ * p_req, trt_AddPTZConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddPTZConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoEncoderConfiguration_REQ * p_req, trt_RemoveVideoEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveVideoEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoSourceConfiguration_REQ * p_req, trt_RemoveVideoSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveVideoSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioEncoderConfiguration_REQ * p_req, trt_RemoveAudioEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveAudioEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioSourceConfiguration_REQ * p_req, trt_RemoveAudioSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveAudioSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemovePTZConfiguration(ONVIF_DEVICE * p_dev, trt_RemovePTZConfiguration_REQ * p_req, trt_RemovePTZConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemovePTZConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_DeleteProfile(ONVIF_DEVICE * p_dev, trt_DeleteProfile_REQ * p_req, trt_DeleteProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtDeleteProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfigurations_REQ * p_req, trt_GetVideoSourceConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoSourceConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfigurations_REQ * p_req, trt_GetVideoEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfigurations_REQ * p_req, trt_GetAudioSourceConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioSourceConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfigurations_REQ * p_req, trt_GetAudioEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfiguration_REQ * p_req, trt_GetVideoSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfiguration_REQ * p_req, trt_GetVideoEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfiguration_REQ * p_req, trt_GetAudioSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfiguration_REQ * p_req, trt_GetAudioEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoSourceConfiguration_REQ * p_req, trt_SetVideoSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetVideoSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoEncoderConfiguration_REQ * p_req, trt_SetVideoEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetVideoEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioSourceConfiguration_REQ * p_req, trt_SetAudioSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetAudioSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioEncoderConfiguration_REQ * p_req, trt_SetAudioEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetAudioEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoSourceConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetVideoSourceConfigurationOptions_REQ * p_req, trt_GetVideoSourceConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoSourceConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetVideoEncoderConfigurationOptions_REQ * p_req, trt_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoEncoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioSourceConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioSourceConfigurationOptions_REQ * p_req, trt_GetAudioSourceConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioSourceConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioEncoderConfigurationOptions_REQ * p_req, trt_GetAudioEncoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioEncoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetStreamUri(ONVIF_DEVICE * p_dev, trt_GetStreamUri_REQ * p_req, trt_GetStreamUri_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetStreamUri, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, trt_SetSynchronizationPoint_REQ * p_req, trt_SetSynchronizationPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetSynchronizationPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetSnapshotUri(ONVIF_DEVICE * p_dev, trt_GetSnapshotUri_REQ * p_req, trt_GetSnapshotUri_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetSnapshotUri, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetGuaranteedNumberOfVideoEncoderInstances(ONVIF_DEVICE * p_dev, trt_GetGuaranteedNumberOfVideoEncoderInstances_REQ * p_req, trt_GetGuaranteedNumberOfVideoEncoderInstances_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetGuaranteedNumberOfVideoEncoderInstances, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioOutputs(ONVIF_DEVICE * p_dev, trt_GetAudioOutputs_REQ * p_req, trt_GetAudioOutputs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioOutputs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioOutputConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfigurations_REQ * p_req, trt_GetAudioOutputConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioOutputConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfiguration_REQ * p_req, trt_GetAudioOutputConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioOutputConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioOutputConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioOutputConfigurationOptions_REQ * p_req, trt_GetAudioOutputConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioOutputConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioOutputConfiguration_REQ * p_req, trt_SetAudioOutputConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetAudioOutputConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioDecoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfigurations_REQ * p_req, trt_GetAudioDecoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioDecoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfiguration_REQ * p_req, trt_GetAudioDecoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioDecoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetAudioDecoderConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetAudioDecoderConfigurationOptions_REQ * p_req, trt_GetAudioDecoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetAudioDecoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_SetAudioDecoderConfiguration_REQ * p_req, trt_SetAudioDecoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetAudioDecoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioOutputConfiguration_REQ * p_req, trt_AddAudioOutputConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddAudioOutputConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_AddAudioDecoderConfiguration_REQ * p_req, trt_AddAudioDecoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddAudioDecoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveAudioOutputConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioOutputConfiguration_REQ * p_req, trt_RemoveAudioOutputConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveAudioOutputConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveAudioDecoderConfiguration_REQ * p_req, trt_RemoveAudioDecoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveAudioDecoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetOSDs(ONVIF_DEVICE * p_dev, trt_GetOSDs_REQ * p_req, trt_GetOSDs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetOSDs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetOSD(ONVIF_DEVICE * p_dev, trt_GetOSD_REQ * p_req, trt_GetOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetOSD(ONVIF_DEVICE * p_dev, trt_SetOSD_REQ * p_req, trt_SetOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetOSDOptions(ONVIF_DEVICE * p_dev, trt_GetOSDOptions_REQ * p_req, trt_GetOSDOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetOSDOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_CreateOSD(ONVIF_DEVICE * p_dev, trt_CreateOSD_REQ * p_req, trt_CreateOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtCreateOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_DeleteOSD(ONVIF_DEVICE * p_dev, trt_DeleteOSD_REQ * p_req, trt_DeleteOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtDeleteOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoAnalyticsConfigurations(ONVIF_DEVICE * p_dev, trt_GetVideoAnalyticsConfigurations_REQ * p_req, trt_GetVideoAnalyticsConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoAnalyticsConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_AddVideoAnalyticsConfiguration_REQ * p_req, trt_AddVideoAnalyticsConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddVideoAnalyticsConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_GetVideoAnalyticsConfiguration_REQ * p_req, trt_GetVideoAnalyticsConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetVideoAnalyticsConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveVideoAnalyticsConfiguration_REQ * p_req, trt_RemoveVideoAnalyticsConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveVideoAnalyticsConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetVideoAnalyticsConfiguration(ONVIF_DEVICE * p_dev, trt_SetVideoAnalyticsConfiguration_REQ * p_req, trt_SetVideoAnalyticsConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetVideoAnalyticsConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetMetadataConfigurations(ONVIF_DEVICE * p_dev, trt_GetMetadataConfigurations_REQ * p_req, trt_GetMetadataConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetMetadataConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_AddMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_AddMetadataConfiguration_REQ * p_req, trt_AddMetadataConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtAddMetadataConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_GetMetadataConfiguration_REQ * p_req, trt_GetMetadataConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetMetadataConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_RemoveMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_RemoveMetadataConfiguration_REQ * p_req, trt_RemoveMetadataConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtRemoveMetadataConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_SetMetadataConfiguration(ONVIF_DEVICE * p_dev, trt_SetMetadataConfiguration_REQ * p_req, trt_SetMetadataConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtSetMetadataConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetMetadataConfigurationOptions(ONVIF_DEVICE * p_dev, trt_GetMetadataConfigurationOptions_REQ * p_req, trt_GetMetadataConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetMetadataConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetCompatibleVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleVideoEncoderConfigurations_REQ * p_req, trt_GetCompatibleVideoEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetCompatibleVideoEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetCompatibleAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleAudioEncoderConfigurations_REQ * p_req, trt_GetCompatibleAudioEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetCompatibleAudioEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetCompatibleVideoAnalyticsConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleVideoAnalyticsConfigurations_REQ * p_req, trt_GetCompatibleVideoAnalyticsConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetCompatibleVideoAnalyticsConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trt_GetCompatibleMetadataConfigurations(ONVIF_DEVICE * p_dev, trt_GetCompatibleMetadataConfigurations_REQ * p_req, trt_GetCompatibleMetadataConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrtGetCompatibleMetadataConfigurations, p_dev, p_req, p_res);
}

// onvif ptz service interfaces

HT_API BOOL onvif_ptz_GetServiceCapabilities(ONVIF_DEVICE * p_dev, ptz_GetServiceCapabilities_REQ * p_req, ptz_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetNodes(ONVIF_DEVICE * p_dev, ptz_GetNodes_REQ * p_req, ptz_GetNodes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetNodes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetNode(ONVIF_DEVICE * p_dev, ptz_GetNode_REQ * p_req, ptz_GetNode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetNode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetPresets(ONVIF_DEVICE * p_dev, ptz_GetPresets_REQ * p_req, ptz_GetPresets_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetPresets, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_SetPreset(ONVIF_DEVICE * p_dev, ptz_SetPreset_REQ * p_req, ptz_SetPreset_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzSetPreset, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_RemovePreset(ONVIF_DEVICE * p_dev, ptz_RemovePreset_REQ * p_req, ptz_RemovePreset_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzRemovePreset, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GotoPreset(ONVIF_DEVICE * p_dev, ptz_GotoPreset_REQ * p_req, ptz_GotoPreset_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGotoPreset, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GotoHomePosition(ONVIF_DEVICE * p_dev, ptz_GotoHomePosition_REQ * p_req, ptz_GotoHomePosition_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGotoHomePosition, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_SetHomePosition(ONVIF_DEVICE * p_dev, ptz_SetHomePosition_REQ * p_req, ptz_SetHomePosition_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzSetHomePosition, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetStatus(ONVIF_DEVICE * p_dev, ptz_GetStatus_REQ * p_req, ptz_GetStatus_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetStatus, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_ContinuousMove(ONVIF_DEVICE * p_dev, ptz_ContinuousMove_REQ * p_req, ptz_ContinuousMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzContinuousMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_RelativeMove(ONVIF_DEVICE * p_dev, ptz_RelativeMove_REQ * p_req, ptz_RelativeMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzRelativeMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_AbsoluteMove(ONVIF_DEVICE * p_dev, ptz_AbsoluteMove_REQ * p_req, ptz_AbsoluteMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzAbsoluteMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_Stop(ONVIF_DEVICE * p_dev, ptz_Stop_REQ * p_req, ptz_Stop_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzStop, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetConfigurations(ONVIF_DEVICE * p_dev, ptz_GetConfigurations_REQ * p_req, ptz_GetConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetConfiguration(ONVIF_DEVICE * p_dev, ptz_GetConfiguration_REQ * p_req, ptz_GetConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_SetConfiguration(ONVIF_DEVICE * p_dev, ptz_SetConfiguration_REQ * p_req, ptz_SetConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzSetConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetConfigurationOptions(ONVIF_DEVICE * p_dev, ptz_GetConfigurationOptions_REQ * p_req, ptz_GetConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetPresetTours(ONVIF_DEVICE * p_dev, ptz_GetPresetTours_REQ * p_req, ptz_GetPresetTours_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetPresetTours, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetPresetTour(ONVIF_DEVICE * p_dev, ptz_GetPresetTour_REQ * p_req, ptz_GetPresetTour_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetPresetTour, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GetPresetTourOptions(ONVIF_DEVICE * p_dev, ptz_GetPresetTourOptions_REQ * p_req, ptz_GetPresetTourOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGetPresetTourOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_CreatePresetTour(ONVIF_DEVICE * p_dev, ptz_CreatePresetTour_REQ * p_req, ptz_CreatePresetTour_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzCreatePresetTour, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_ModifyPresetTour(ONVIF_DEVICE * p_dev, ptz_ModifyPresetTour_REQ * p_req, ptz_ModifyPresetTour_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzModifyPresetTour, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_OperatePresetTour(ONVIF_DEVICE * p_dev, ptz_OperatePresetTour_REQ * p_req, ptz_OperatePresetTour_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzOperatePresetTour, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_RemovePresetTour(ONVIF_DEVICE * p_dev, ptz_RemovePresetTour_REQ * p_req, ptz_RemovePresetTour_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzRemovePresetTour, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_SendAuxiliaryCommand(ONVIF_DEVICE * p_dev, ptz_SendAuxiliaryCommand_REQ * p_req, ptz_SendAuxiliaryCommand_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzSendAuxiliaryCommand, p_dev, p_req, p_res);
}

HT_API BOOL onvif_ptz_GeoMove(ONVIF_DEVICE * p_dev, ptz_GeoMove_REQ * p_req, ptz_GeoMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.ptz.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.ptz.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eptzGeoMove, p_dev, p_req, p_res);
}

// onvif event service interfaces

HT_API BOOL onvif_tev_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tev_GetServiceCapabilities_REQ * p_req, tev_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_GetEventProperties(ONVIF_DEVICE * p_dev, tev_GetEventProperties_REQ * p_req, tev_GetEventProperties_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevGetEventProperties, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_Renew(ONVIF_DEVICE * p_dev, tev_Renew_REQ * p_req, tev_Renew_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etevRenew, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_Unsubscribe(ONVIF_DEVICE * p_dev, tev_Unsubscribe_REQ * p_req, tev_Unsubscribe_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etevUnsubscribe, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_Subscribe(ONVIF_DEVICE * p_dev, tev_Subscribe_REQ * p_req, tev_Subscribe_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevSubscribe, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_PauseSubscription(ONVIF_DEVICE * p_dev, tev_PauseSubscription_REQ * p_req, tev_PauseSubscription_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevPauseSubscription, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_ResumeSubscription(ONVIF_DEVICE * p_dev, tev_ResumeSubscription_REQ * p_req, tev_ResumeSubscription_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevResumeSubscription, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_CreatePullPointSubscription(ONVIF_DEVICE * p_dev, tev_CreatePullPointSubscription_REQ * p_req, tev_CreatePullPointSubscription_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevCreatePullPointSubscription, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_DestroyPullPoint(ONVIF_DEVICE * p_dev, tev_DestroyPullPoint_REQ * p_req, tev_DestroyPullPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevDestroyPullPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_PullMessages(ONVIF_DEVICE * p_dev, tev_PullMessages_REQ * p_req, tev_PullMessages_RES * p_res)
{
    int timeout = p_dev->timeout;
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);
    
    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    timeout = timeout < p_req->Timeout * 1000 ? p_req->Timeout * 1000 : timeout;
    
    return http_onvif_trans(&http_req, timeout, etevPullMessages, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_GetMessages(ONVIF_DEVICE * p_dev, tev_GetMessages_REQ * p_req, tev_GetMessages_RES * p_res)
{
    HTTPREQ http_req;
    onvif_XAddr xaddr;

    memset(&xaddr, 0, sizeof(xaddr));
    
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (parse_XAddr(p_dev->events.producter_addr, &xaddr))
    {
        onvif_init_req(&http_req, &xaddr);
    }
    else if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevGetMessages, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_Seek(ONVIF_DEVICE * p_dev, tev_Seek_REQ * p_req, tev_Seek_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevSeek, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tev_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, tev_SetSynchronizationPoint_REQ * p_req, tev_SetSynchronizationPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.events.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.events.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etevSetSynchronizationPoint, p_dev, p_req, p_res);
}

// onvif imaging service interfaces

HT_API BOOL onvif_img_GetServiceCapabilities(ONVIF_DEVICE * p_dev, img_GetServiceCapabilities_REQ * p_req, img_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetImagingSettings(ONVIF_DEVICE * p_dev, img_GetImagingSettings_REQ * p_req, img_GetImagingSettings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetImagingSettings, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_SetImagingSettings(ONVIF_DEVICE * p_dev, img_SetImagingSettings_REQ * p_req, img_SetImagingSettings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgSetImagingSettings, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetOptions(ONVIF_DEVICE * p_dev, img_GetOptions_REQ * p_req, img_GetOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_Move(ONVIF_DEVICE * p_dev, img_Move_REQ * p_req, img_Move_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_Stop(ONVIF_DEVICE * p_dev, img_Stop_REQ * p_req, img_Stop_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgStop, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetStatus(ONVIF_DEVICE * p_dev, img_GetStatus_REQ * p_req, img_GetStatus_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetStatus, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetMoveOptions(ONVIF_DEVICE * p_dev, img_GetMoveOptions_REQ * p_req, img_GetMoveOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetMoveOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetPresets(ONVIF_DEVICE * p_dev, img_GetPresets_REQ * p_req, img_GetPresets_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetPresets, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_GetCurrentPreset(ONVIF_DEVICE * p_dev, img_GetCurrentPreset_REQ * p_req, img_GetCurrentPreset_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgGetCurrentPreset, p_dev, p_req, p_res);
}

HT_API BOOL onvif_img_SetCurrentPreset(ONVIF_DEVICE * p_dev, img_SetCurrentPreset_REQ * p_req, img_SetCurrentPreset_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.image.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.image.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, eimgSetCurrentPreset, p_dev, p_req, p_res);
}

// onvif analytics service interfaces

HT_API BOOL onvif_tan_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tan_GetServiceCapabilities_REQ * p_req, tan_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetSupportedRules(ONVIF_DEVICE * p_dev, tan_GetSupportedRules_REQ * p_req, tan_GetSupportedRules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetSupportedRules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_CreateRules(ONVIF_DEVICE * p_dev, tan_CreateRules_REQ * p_req, tan_CreateRules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanCreateRules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_DeleteRules(ONVIF_DEVICE * p_dev, tan_DeleteRules_REQ * p_req, tan_DeleteRules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanDeleteRules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetRules(ONVIF_DEVICE * p_dev, tan_GetRules_REQ * p_req, tan_GetRules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetRules, p_dev, p_req, p_res);
}
    
HT_API BOOL onvif_tan_ModifyRules(ONVIF_DEVICE * p_dev, tan_ModifyRules_REQ * p_req, tan_ModifyRules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanModifyRules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_CreateAnalyticsModules(ONVIF_DEVICE * p_dev, tan_CreateAnalyticsModules_REQ * p_req, tan_CreateAnalyticsModules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanCreateAnalyticsModules, p_dev, p_req, p_res);
}    

HT_API BOOL onvif_tan_DeleteAnalyticsModules(ONVIF_DEVICE * p_dev, tan_DeleteAnalyticsModules_REQ * p_req, tan_DeleteAnalyticsModules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanDeleteAnalyticsModules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetAnalyticsModules(ONVIF_DEVICE * p_dev, tan_GetAnalyticsModules_REQ * p_req, tan_GetAnalyticsModules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetAnalyticsModules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_ModifyAnalyticsModules(ONVIF_DEVICE * p_dev, tan_ModifyAnalyticsModules_REQ * p_req, tan_ModifyAnalyticsModules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanModifyAnalyticsModules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetSupportedAnalyticsModules(ONVIF_DEVICE * p_dev, tan_GetSupportedAnalyticsModules_REQ * p_req, tan_GetSupportedAnalyticsModules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetSupportedAnalyticsModules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetRuleOptions(ONVIF_DEVICE * p_dev, tan_GetRuleOptions_REQ * p_req, tan_GetRuleOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetRuleOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetAnalyticsModuleOptions(ONVIF_DEVICE * p_dev, tan_GetAnalyticsModuleOptions_REQ * p_req, tan_GetAnalyticsModuleOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetAnalyticsModuleOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tan_GetSupportedMetadata(ONVIF_DEVICE * p_dev, tan_GetSupportedMetadata_REQ * p_req, tan_GetSupportedMetadata_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.analytics.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.analytics.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etanGetSupportedMetadata, p_dev, p_req, p_res);
}

// ONVIF media 2 service interfaces

HT_API BOOL onvif_tr2_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tr2_GetServiceCapabilities_REQ * p_req, tr2_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoEncoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderConfigurations_REQ * p_req, tr2_GetVideoEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderConfigurationOptions_REQ * p_req, tr2_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoEncoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetVideoEncoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetVideoEncoderConfiguration_REQ * p_req, tr2_SetVideoEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetVideoEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetProfiles(ONVIF_DEVICE * p_dev, tr2_GetProfiles_REQ * p_req, tr2_GetProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetProfiles, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_CreateProfile(ONVIF_DEVICE * p_dev, tr2_CreateProfile_REQ * p_req, tr2_CreateProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2CreateProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_DeleteProfile(ONVIF_DEVICE * p_dev, tr2_DeleteProfile_REQ * p_req, tr2_DeleteProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2DeleteProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetStreamUri(ONVIF_DEVICE * p_dev, tr2_GetStreamUri_REQ * p_req, tr2_GetStreamUri_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetStreamUri, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoSourceConfigurations(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceConfigurations_REQ * p_req, tr2_GetVideoSourceConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoSourceConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoSourceConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceConfigurationOptions_REQ * p_req, tr2_GetVideoSourceConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoSourceConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetVideoSourceConfiguration(ONVIF_DEVICE * p_dev, tr2_SetVideoSourceConfiguration_REQ * p_req, tr2_SetVideoSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetVideoSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetSynchronizationPoint(ONVIF_DEVICE * p_dev, tr2_SetSynchronizationPoint_REQ * p_req, tr2_SetSynchronizationPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }
    
    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetSynchronizationPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetMetadataConfigurations(ONVIF_DEVICE * p_dev, tr2_GetMetadataConfigurations_REQ * p_req, tr2_GetMetadataConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetMetadataConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetMetadataConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetMetadataConfigurationOptions_REQ * p_req, tr2_GetMetadataConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetMetadataConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetMetadataConfiguration(ONVIF_DEVICE * p_dev, tr2_SetMetadataConfiguration_REQ * p_req, tr2_SetMetadataConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetMetadataConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioEncoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioEncoderConfigurations_REQ * p_req, tr2_GetAudioEncoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioEncoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioSourceConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioSourceConfigurations_REQ * p_req, tr2_GetAudioSourceConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioSourceConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioSourceConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioSourceConfigurationOptions_REQ * p_req, tr2_GetAudioSourceConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioSourceConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetAudioSourceConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioSourceConfiguration_REQ * p_req, tr2_SetAudioSourceConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetAudioSourceConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetAudioEncoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioEncoderConfiguration_REQ * p_req, tr2_SetAudioEncoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetAudioEncoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioEncoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioEncoderConfigurationOptions_REQ * p_req, tr2_GetAudioEncoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioEncoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_AddConfiguration(ONVIF_DEVICE * p_dev, tr2_AddConfiguration_REQ * p_req, tr2_AddConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2AddConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_RemoveConfiguration(ONVIF_DEVICE * p_dev, tr2_RemoveConfiguration_REQ * p_req, tr2_RemoveConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2RemoveConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoEncoderInstances(ONVIF_DEVICE * p_dev, tr2_GetVideoEncoderInstances_REQ * p_req, tr2_GetVideoEncoderInstances_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoEncoderInstances, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioOutputConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioOutputConfigurations_REQ * p_req, tr2_GetAudioOutputConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioOutputConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioOutputConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioOutputConfigurationOptions_REQ * p_req, tr2_GetAudioOutputConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioOutputConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetAudioOutputConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioOutputConfiguration_REQ * p_req, tr2_SetAudioOutputConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetAudioOutputConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioDecoderConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAudioDecoderConfigurations_REQ * p_req, tr2_GetAudioDecoderConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioDecoderConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAudioDecoderConfigurationOptions(ONVIF_DEVICE * p_dev, tr2_GetAudioDecoderConfigurationOptions_REQ * p_req, tr2_GetAudioDecoderConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAudioDecoderConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetAudioDecoderConfiguration(ONVIF_DEVICE * p_dev, tr2_SetAudioDecoderConfiguration_REQ * p_req, tr2_SetAudioDecoderConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetAudioDecoderConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetSnapshotUri(ONVIF_DEVICE * p_dev, tr2_GetSnapshotUri_REQ * p_req, tr2_GetSnapshotUri_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetSnapshotUri, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_StartMulticastStreaming(ONVIF_DEVICE * p_dev, tr2_StartMulticastStreaming_REQ * p_req, tr2_StartMulticastStreaming_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2StartMulticastStreaming, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_StopMulticastStreaming(ONVIF_DEVICE * p_dev, tr2_StopMulticastStreaming_REQ * p_req, tr2_StopMulticastStreaming_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2StopMulticastStreaming, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetVideoSourceModes(ONVIF_DEVICE * p_dev, tr2_GetVideoSourceModes_REQ * p_req, tr2_GetVideoSourceModes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetVideoSourceModes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetVideoSourceMode(ONVIF_DEVICE * p_dev, tr2_SetVideoSourceMode_REQ * p_req, tr2_SetVideoSourceMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetVideoSourceMode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_CreateOSD(ONVIF_DEVICE * p_dev, tr2_CreateOSD_REQ * p_req, tr2_CreateOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2CreateOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_DeleteOSD(ONVIF_DEVICE * p_dev, tr2_DeleteOSD_REQ * p_req, tr2_DeleteOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2DeleteOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetOSDs(ONVIF_DEVICE * p_dev, tr2_GetOSDs_REQ * p_req, tr2_GetOSDs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetOSDs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetOSD(ONVIF_DEVICE * p_dev, tr2_SetOSD_REQ * p_req, tr2_SetOSD_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetOSD, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetOSDOptions(ONVIF_DEVICE * p_dev, tr2_GetOSDOptions_REQ * p_req, tr2_GetOSDOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetOSDOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetAnalyticsConfigurations(ONVIF_DEVICE * p_dev, tr2_GetAnalyticsConfigurations_REQ * p_req, tr2_GetAnalyticsConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetAnalyticsConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetMasks(ONVIF_DEVICE * p_dev, tr2_GetMasks_REQ * p_req, tr2_GetMasks_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetMasks, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_SetMask(ONVIF_DEVICE * p_dev, tr2_SetMask_REQ * p_req, tr2_SetMask_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2SetMask, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_CreateMask(ONVIF_DEVICE * p_dev, tr2_CreateMask_REQ * p_req, tr2_CreateMask_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2CreateMask, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_DeleteMask(ONVIF_DEVICE * p_dev, tr2_DeleteMask_REQ * p_req, tr2_DeleteMask_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2DeleteMask, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tr2_GetMaskOptions(ONVIF_DEVICE * p_dev, tr2_GetMaskOptions_REQ * p_req, tr2_GetMaskOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.media2.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.media2.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etr2GetMaskOptions, p_dev, p_req, p_res);
}

#ifdef DEVICEIO_SUPPORT

HT_API BOOL onvif_tmd_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tmd_GetServiceCapabilities_REQ * p_req, tmd_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetRelayOutputs(ONVIF_DEVICE * p_dev, tmd_GetRelayOutputs_REQ * p_req, tmd_GetRelayOutputs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetRelayOutputs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetRelayOutputOptions(ONVIF_DEVICE * p_dev, tmd_GetRelayOutputOptions_REQ * p_req, tmd_GetRelayOutputOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetRelayOutputOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_SetRelayOutputSettings(ONVIF_DEVICE * p_dev, tmd_SetRelayOutputSettings_REQ * p_req, tmd_SetRelayOutputSettings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdSetRelayOutputSettings, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_SetRelayOutputState(ONVIF_DEVICE * p_dev, tmd_SetRelayOutputState_REQ * p_req, tmd_SetRelayOutputState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdSetRelayOutputState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetDigitalInputs(ONVIF_DEVICE * p_dev, tmd_GetDigitalInputs_REQ * p_req, tmd_GetDigitalInputs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetDigitalInputs, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetDigitalInputConfigurationOptions(ONVIF_DEVICE * p_dev, tmd_GetDigitalInputConfigurationOptions_REQ * p_req, tmd_GetDigitalInputConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetDigitalInputConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_SetDigitalInputConfigurations(ONVIF_DEVICE * p_dev, tmd_SetDigitalInputConfigurations_REQ * p_req, tmd_SetDigitalInputConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdSetDigitalInputConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetSerialPorts(ONVIF_DEVICE * p_dev, tmd_GetSerialPorts_REQ * p_req, tmd_GetSerialPorts_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetSerialPorts, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetSerialPortConfiguration(ONVIF_DEVICE * p_dev, tmd_GetSerialPortConfiguration_REQ * p_req, tmd_GetSerialPortConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetSerialPortConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_SetSerialPortConfiguration(ONVIF_DEVICE * p_dev, tmd_SetSerialPortConfiguration_REQ * p_req, tmd_SetSerialPortConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdSetSerialPortConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_GetSerialPortConfigurationOptions(ONVIF_DEVICE * p_dev, tmd_GetSerialPortConfigurationOptions_REQ * p_req, tmd_GetSerialPortConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdGetSerialPortConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tmd_SendReceiveSerialCommand(ONVIF_DEVICE * p_dev, tmd_SendReceiveSerialCommand_REQ * p_req, tmd_SendReceiveSerialCommand_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.deviceIO.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.deviceIO.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etmdSendReceiveSerialCommand, p_dev, p_req, p_res);
}

#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT

// recording service interfaces

HT_API BOOL onvif_trc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trc_GetServiceCapabilities_REQ * p_req, trc_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trc_CreateRecording(ONVIF_DEVICE * p_dev, trc_CreateRecording_REQ * p_req, trc_CreateRecording_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcCreateRecording, p_dev, p_req, p_res);
}   

HT_API BOOL onvif_trc_DeleteRecording(ONVIF_DEVICE * p_dev, trc_DeleteRecording_REQ * p_req, trc_DeleteRecording_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcDeleteRecording, p_dev, p_req, p_res);
} 
   
HT_API BOOL onvif_trc_GetRecordings(ONVIF_DEVICE * p_dev, trc_GetRecordings_REQ * p_req, trc_GetRecordings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordings, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_SetRecordingConfiguration(ONVIF_DEVICE * p_dev, trc_SetRecordingConfiguration_REQ * p_req, trc_SetRecordingConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcSetRecordingConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetRecordingConfiguration(ONVIF_DEVICE * p_dev, trc_GetRecordingConfiguration_REQ * p_req, trc_GetRecordingConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordingConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetRecordingOptions(ONVIF_DEVICE * p_dev, trc_GetRecordingOptions_REQ * p_req, trc_GetRecordingOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordingOptions, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_CreateTrack(ONVIF_DEVICE * p_dev, trc_CreateTrack_REQ * p_req, trc_CreateTrack_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcCreateTrack, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_DeleteTrack(ONVIF_DEVICE * p_dev, trc_DeleteTrack_REQ * p_req, trc_DeleteTrack_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcDeleteTrack, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetTrackConfiguration(ONVIF_DEVICE * p_dev, trc_GetTrackConfiguration_REQ * p_req, trc_GetTrackConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetTrackConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_SetTrackConfiguration(ONVIF_DEVICE * p_dev, trc_SetTrackConfiguration_REQ * p_req, trc_SetTrackConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcSetTrackConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_CreateRecordingJob(ONVIF_DEVICE * p_dev, trc_CreateRecordingJob_REQ * p_req, trc_CreateRecordingJob_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcCreateRecordingJob, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_DeleteRecordingJob(ONVIF_DEVICE * p_dev, trc_DeleteRecordingJob_REQ * p_req, trc_DeleteRecordingJob_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcDeleteRecordingJob, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetRecordingJobs(ONVIF_DEVICE * p_dev, trc_GetRecordingJobs_REQ * p_req, trc_GetRecordingJobs_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordingJobs, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_SetRecordingJobConfiguration(ONVIF_DEVICE * p_dev, trc_SetRecordingJobConfiguration_REQ * p_req, trc_SetRecordingJobConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcSetRecordingJobConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetRecordingJobConfiguration(ONVIF_DEVICE * p_dev, trc_GetRecordingJobConfiguration_REQ * p_req, trc_GetRecordingJobConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordingJobConfiguration, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_SetRecordingJobMode(ONVIF_DEVICE * p_dev, trc_SetRecordingJobMode_REQ * p_req, trc_SetRecordingJobMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcSetRecordingJobMode, p_dev, p_req, p_res);
} 
 
HT_API BOOL onvif_trc_GetRecordingJobState(ONVIF_DEVICE * p_dev, trc_GetRecordingJobState_REQ * p_req, trc_GetRecordingJobState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetRecordingJobState, p_dev, p_req, p_res);
} 

HT_API BOOL onvif_trc_ExportRecordedData(ONVIF_DEVICE * p_dev, trc_ExportRecordedData_REQ * p_req, trc_ExportRecordedData_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcExportRecordedData, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trc_StopExportRecordedData(ONVIF_DEVICE * p_dev, trc_StopExportRecordedData_REQ * p_req, trc_StopExportRecordedData_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcStopExportRecordedData, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trc_GetExportRecordedDataState(ONVIF_DEVICE * p_dev, trc_GetExportRecordedDataState_REQ * p_req, trc_GetExportRecordedDataState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.recording.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.recording.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrcGetExportRecordedDataState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trp_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trp_GetServiceCapabilities_REQ * p_req, trp_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.replay.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.replay.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrpGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trp_GetReplayUri(ONVIF_DEVICE * p_dev, trp_GetReplayUri_REQ * p_req, trp_GetReplayUri_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.replay.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.replay.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrpGetReplayUri, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trp_GetReplayConfiguration(ONVIF_DEVICE * p_dev, trp_GetReplayConfiguration_REQ * p_req, trp_GetReplayConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.replay.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.replay.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrpGetReplayConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trp_SetReplayConfiguration(ONVIF_DEVICE * p_dev, trp_SetReplayConfiguration_REQ * p_req, trp_SetReplayConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.replay.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.replay.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrpSetReplayConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tse_GetServiceCapabilities_REQ * p_req, tse_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetRecordingSummary(ONVIF_DEVICE * p_dev, tse_GetRecordingSummary_REQ * p_req, tse_GetRecordingSummary_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetRecordingSummary, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetRecordingInformation(ONVIF_DEVICE * p_dev, tse_GetRecordingInformation_REQ * p_req, tse_GetRecordingInformation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetRecordingInformation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetMediaAttributes(ONVIF_DEVICE * p_dev, tse_GetMediaAttributes_REQ * p_req, tse_GetMediaAttributes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetMediaAttributes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_FindRecordings(ONVIF_DEVICE * p_dev, tse_FindRecordings_REQ * p_req, tse_FindRecordings_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseFindRecordings, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetRecordingSearchResults(ONVIF_DEVICE * p_dev, tse_GetRecordingSearchResults_REQ * p_req, tse_GetRecordingSearchResults_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetRecordingSearchResults, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_FindEvents(ONVIF_DEVICE * p_dev, tse_FindEvents_REQ * p_req, tse_FindEvents_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseFindEvents, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetEventSearchResults(ONVIF_DEVICE * p_dev, tse_GetEventSearchResults_REQ * p_req, tse_GetEventSearchResults_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetEventSearchResults, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_FindMetadata(ONVIF_DEVICE * p_dev, tse_FindMetadata_REQ * p_req, tse_FindMetadata_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseFindMetadata, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetMetadataSearchResults(ONVIF_DEVICE * p_dev, tse_GetMetadataSearchResults_REQ * p_req, tse_GetMetadataSearchResults_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetMetadataSearchResults, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_FindPTZPosition(ONVIF_DEVICE * p_dev, tse_FindPTZPosition_REQ * p_req, tse_FindPTZPosition_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseFindPTZPosition, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetPTZPositionSearchResults(ONVIF_DEVICE * p_dev, tse_GetPTZPositionSearchResults_REQ * p_req, tse_GetPTZPositionSearchResults_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetPTZPositionSearchResults, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_GetSearchState(ONVIF_DEVICE * p_dev, tse_GetSearchState_REQ * p_req, tse_GetSearchState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseGetSearchState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tse_EndSearch(ONVIF_DEVICE * p_dev, tse_EndSearch_REQ * p_req, tse_EndSearch_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.search.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.search.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etseEndSearch, p_dev, p_req, p_res);
}

#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

HT_API BOOL onvif_tac_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tac_GetServiceCapabilities_REQ * p_req, tac_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAccessPointInfoList(ONVIF_DEVICE * p_dev, tac_GetAccessPointInfoList_REQ * p_req, tac_GetAccessPointInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAccessPointInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAccessPointInfo(ONVIF_DEVICE * p_dev, tac_GetAccessPointInfo_REQ * p_req, tac_GetAccessPointInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAccessPointInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAccessPointList(ONVIF_DEVICE * p_dev, tac_GetAccessPointList_REQ * p_req, tac_GetAccessPointList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAccessPointList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAccessPoints(ONVIF_DEVICE * p_dev, tac_GetAccessPoints_REQ * p_req, tac_GetAccessPoints_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAccessPoints, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_CreateAccessPoint(ONVIF_DEVICE * p_dev, tac_CreateAccessPoint_REQ * p_req, tac_CreateAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacCreateAccessPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_SetAccessPoint(ONVIF_DEVICE * p_dev, tac_SetAccessPoint_REQ * p_req, tac_SetAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacSetAccessPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_ModifyAccessPoint(ONVIF_DEVICE * p_dev, tac_ModifyAccessPoint_REQ * p_req, tac_ModifyAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacModifyAccessPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_DeleteAccessPoint(ONVIF_DEVICE * p_dev, tac_DeleteAccessPoint_REQ * p_req, tac_DeleteAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacDeleteAccessPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAreaInfoList(ONVIF_DEVICE * p_dev, tac_GetAreaInfoList_REQ * p_req, tac_GetAreaInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAreaInfoList, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tac_GetAreaInfo(ONVIF_DEVICE * p_dev, tac_GetAreaInfo_REQ * p_req, tac_GetAreaInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAreaInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAreaList(ONVIF_DEVICE * p_dev, tac_GetAreaList_REQ * p_req, tac_GetAreaList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAreaList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAreas(ONVIF_DEVICE * p_dev, tac_GetAreas_REQ * p_req, tac_GetAreas_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAreas, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_CreateArea(ONVIF_DEVICE * p_dev, tac_CreateArea_REQ * p_req, tac_CreateArea_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacCreateArea, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_SetArea(ONVIF_DEVICE * p_dev, tac_SetArea_REQ * p_req, tac_SetArea_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacSetArea, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_ModifyArea(ONVIF_DEVICE * p_dev, tac_ModifyArea_REQ * p_req, tac_ModifyArea_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacModifyArea, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_DeleteArea(ONVIF_DEVICE * p_dev, tac_DeleteArea_REQ * p_req, tac_DeleteArea_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacDeleteArea, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_GetAccessPointState(ONVIF_DEVICE * p_dev, tac_GetAccessPointState_REQ * p_req, tac_GetAccessPointState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacGetAccessPointState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tac_EnableAccessPoint(ONVIF_DEVICE * p_dev, tac_EnableAccessPoint_REQ * p_req, tac_EnableAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacEnableAccessPoint, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tac_DisableAccessPoint(ONVIF_DEVICE * p_dev, tac_DisableAccessPoint_REQ * p_req, tac_DisableAccessPoint_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accesscontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accesscontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etacDisableAccessPoint, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tdc_GetServiceCapabilities_REQ * p_req, tdc_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_GetDoorInfoList(ONVIF_DEVICE * p_dev, tdc_GetDoorInfoList_REQ * p_req, tdc_GetDoorInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetDoorInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_GetDoorInfo(ONVIF_DEVICE * p_dev, tdc_GetDoorInfo_REQ * p_req, tdc_GetDoorInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetDoorInfo, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_GetDoorState(ONVIF_DEVICE * p_dev, tdc_GetDoorState_REQ * p_req, tdc_GetDoorState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetDoorState, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_AccessDoor(ONVIF_DEVICE * p_dev, tdc_AccessDoor_REQ * p_req, tdc_AccessDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcAccessDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_LockDoor(ONVIF_DEVICE * p_dev, tdc_LockDoor_REQ * p_req, tdc_LockDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcLockDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_UnlockDoor(ONVIF_DEVICE * p_dev, tdc_UnlockDoor_REQ * p_req, tdc_UnlockDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcUnlockDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_DoubleLockDoor(ONVIF_DEVICE * p_dev, tdc_DoubleLockDoor_REQ * p_req, tdc_DoubleLockDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcDoubleLockDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_BlockDoor(ONVIF_DEVICE * p_dev, tdc_BlockDoor_REQ * p_req, tdc_BlockDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcBlockDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_LockDownDoor(ONVIF_DEVICE * p_dev, tdc_LockDownDoor_REQ * p_req, tdc_LockDownDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcLockDownDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_LockDownReleaseDoor(ONVIF_DEVICE * p_dev, tdc_LockDownReleaseDoor_REQ * p_req, tdc_LockDownReleaseDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcLockDownReleaseDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_LockOpenDoor(ONVIF_DEVICE * p_dev, tdc_LockOpenDoor_REQ * p_req, tdc_LockOpenDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcLockOpenDoor, p_dev, p_req, p_res);
}
  
HT_API BOOL onvif_tdc_LockOpenReleaseDoor(ONVIF_DEVICE * p_dev, tdc_LockOpenReleaseDoor_REQ * p_req, tdc_LockOpenReleaseDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcLockOpenReleaseDoor, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_GetDoors(ONVIF_DEVICE * p_dev, tdc_GetDoors_REQ * p_req, tdc_GetDoors_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetDoors, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_GetDoorList(ONVIF_DEVICE * p_dev, tdc_GetDoorList_REQ * p_req, tdc_GetDoorList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcGetDoorList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_CreateDoor(ONVIF_DEVICE * p_dev, tdc_CreateDoor_REQ * p_req, tdc_CreateDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcCreateDoor, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_SetDoor(ONVIF_DEVICE * p_dev, tdc_SetDoor_REQ * p_req, tdc_SetDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcSetDoor, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_ModifyDoor(ONVIF_DEVICE * p_dev, tdc_ModifyDoor_REQ * p_req, tdc_ModifyDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcModifyDoor, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tdc_DeleteDoor(ONVIF_DEVICE * p_dev, tdc_DeleteDoor_REQ * p_req, tdc_DeleteDoor_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.doorcontrol.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.doorcontrol.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etdcDeleteDoor, p_dev, p_req, p_res);
}

#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT

HT_API BOOL onvif_tth_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tth_GetServiceCapabilities_REQ * p_req, tth_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_GetConfigurations(ONVIF_DEVICE * p_dev, tth_GetConfigurations_REQ * p_req, tth_GetConfigurations_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetConfigurations, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_GetConfiguration(ONVIF_DEVICE * p_dev, tth_GetConfiguration_REQ * p_req, tth_GetConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_SetConfiguration(ONVIF_DEVICE * p_dev, tth_SetConfiguration_REQ * p_req, tth_SetConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthSetConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_GetConfigurationOptions(ONVIF_DEVICE * p_dev, tth_GetConfigurationOptions_REQ * p_req, tth_GetConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetConfigurationOptions, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_GetRadiometryConfiguration(ONVIF_DEVICE * p_dev, tth_GetRadiometryConfiguration_REQ * p_req, tth_GetRadiometryConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetRadiometryConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_SetRadiometryConfiguration(ONVIF_DEVICE * p_dev, tth_SetRadiometryConfiguration_REQ * p_req, tth_SetRadiometryConfiguration_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthSetRadiometryConfiguration, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tth_GetRadiometryConfigurationOptions(ONVIF_DEVICE * p_dev, tth_GetRadiometryConfigurationOptions_REQ * p_req, tth_GetRadiometryConfigurationOptions_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.thermal.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.thermal.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etthGetRadiometryConfigurationOptions, p_dev, p_req, p_res);
}

#endif // end of THERMAL_SUPPORT


#ifdef CREDENTIAL_SUPPORT

HT_API BOOL onvif_tcr_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tcr_GetServiceCapabilities_REQ * p_req, tcr_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialInfo(ONVIF_DEVICE * p_dev, tcr_GetCredentialInfo_REQ * p_req, tcr_GetCredentialInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialInfoList(ONVIF_DEVICE * p_dev, tcr_GetCredentialInfoList_REQ * p_req, tcr_GetCredentialInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentials(ONVIF_DEVICE * p_dev, tcr_GetCredentials_REQ * p_req, tcr_GetCredentials_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentials, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialList(ONVIF_DEVICE * p_dev, tcr_GetCredentialList_REQ * p_req, tcr_GetCredentialList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_CreateCredential(ONVIF_DEVICE * p_dev, tcr_CreateCredential_REQ * p_req, tcr_CreateCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrCreateCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_ModifyCredential(ONVIF_DEVICE * p_dev, tcr_ModifyCredential_REQ * p_req, tcr_ModifyCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrModifyCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_DeleteCredential(ONVIF_DEVICE * p_dev, tcr_DeleteCredential_REQ * p_req, tcr_DeleteCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrDeleteCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialState(ONVIF_DEVICE * p_dev, tcr_GetCredentialState_REQ * p_req, tcr_GetCredentialState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialState, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_EnableCredential(ONVIF_DEVICE * p_dev, tcr_EnableCredential_REQ * p_req, tcr_EnableCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrEnableCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_DisableCredential(ONVIF_DEVICE * p_dev, tcr_DisableCredential_REQ * p_req, tcr_DisableCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrDisableCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_SetCredential(ONVIF_DEVICE * p_dev, tcr_SetCredential_REQ * p_req, tcr_SetCredential_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrSetCredential, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_ResetAntipassbackViolation(ONVIF_DEVICE * p_dev, tcr_ResetAntipassbackViolation_REQ * p_req, tcr_ResetAntipassbackViolation_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrResetAntipassbackViolation, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetSupportedFormatTypes(ONVIF_DEVICE * p_dev, tcr_GetSupportedFormatTypes_REQ * p_req, tcr_GetSupportedFormatTypes_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetSupportedFormatTypes, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialIdentifiers(ONVIF_DEVICE * p_dev, tcr_GetCredentialIdentifiers_REQ * p_req, tcr_GetCredentialIdentifiers_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialIdentifiers, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_SetCredentialIdentifier(ONVIF_DEVICE * p_dev, tcr_SetCredentialIdentifier_REQ * p_req, tcr_SetCredentialIdentifier_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrSetCredentialIdentifier, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_DeleteCredentialIdentifier(ONVIF_DEVICE * p_dev, tcr_DeleteCredentialIdentifier_REQ * p_req, tcr_DeleteCredentialIdentifier_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrDeleteCredentialIdentifier, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_GetCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_GetCredentialAccessProfiles_REQ * p_req, tcr_GetCredentialAccessProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrGetCredentialAccessProfiles, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_SetCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_SetCredentialAccessProfiles_REQ * p_req, tcr_SetCredentialAccessProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrSetCredentialAccessProfiles, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tcr_DeleteCredentialAccessProfiles(ONVIF_DEVICE * p_dev, tcr_DeleteCredentialAccessProfiles_REQ * p_req, tcr_DeleteCredentialAccessProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.credential.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.credential.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etcrDeleteCredentialAccessProfiles, p_dev, p_req, p_res);
}

#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

HT_API BOOL onvif_tar_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tar_GetServiceCapabilities_REQ * p_req, tar_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_GetAccessProfileInfo(ONVIF_DEVICE * p_dev, tar_GetAccessProfileInfo_REQ * p_req, tar_GetAccessProfileInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarGetAccessProfileInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_GetAccessProfileInfoList(ONVIF_DEVICE * p_dev, tar_GetAccessProfileInfoList_REQ * p_req, tar_GetAccessProfileInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarGetAccessProfileInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_GetAccessProfiles(ONVIF_DEVICE * p_dev, tar_GetAccessProfiles_REQ * p_req, tar_GetAccessProfiles_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarGetAccessProfiles, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_GetAccessProfileList(ONVIF_DEVICE * p_dev, tar_GetAccessProfileList_REQ * p_req, tar_GetAccessProfileList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarGetAccessProfileList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_CreateAccessProfile(ONVIF_DEVICE * p_dev, tar_CreateAccessProfile_REQ * p_req, tar_CreateAccessProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarCreateAccessProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_ModifyAccessProfile(ONVIF_DEVICE * p_dev, tar_ModifyAccessProfile_REQ * p_req, tar_ModifyAccessProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarModifyAccessProfile, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tar_DeleteAccessProfile(ONVIF_DEVICE * p_dev, tar_DeleteAccessProfile_REQ * p_req, tar_DeleteAccessProfile_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.accessrules.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.accessrules.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etarDeleteAccessProfile, p_dev, p_req, p_res);
}

#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

HT_API BOOL onvif_tsc_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tsc_GetServiceCapabilities_REQ * p_req, tsc_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetScheduleInfo(ONVIF_DEVICE * p_dev, tsc_GetScheduleInfo_REQ * p_req, tsc_GetScheduleInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetScheduleInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetScheduleInfoList(ONVIF_DEVICE * p_dev, tsc_GetScheduleInfoList_REQ * p_req, tsc_GetScheduleInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetScheduleInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetSchedules(ONVIF_DEVICE * p_dev, tsc_GetSchedules_REQ * p_req, tsc_GetSchedules_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetSchedules, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetScheduleList(ONVIF_DEVICE * p_dev, tsc_GetScheduleList_REQ * p_req, tsc_GetScheduleList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetScheduleList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_CreateSchedule(ONVIF_DEVICE * p_dev, tsc_CreateSchedule_REQ * p_req, tsc_CreateSchedule_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscCreateSchedule, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_ModifySchedule(ONVIF_DEVICE * p_dev, tsc_ModifySchedule_REQ * p_req, tsc_ModifySchedule_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscModifySchedule, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_DeleteSchedule(ONVIF_DEVICE * p_dev, tsc_DeleteSchedule_REQ * p_req, tsc_DeleteSchedule_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscDeleteSchedule, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetSpecialDayGroupInfo(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupInfo_REQ * p_req, tsc_GetSpecialDayGroupInfo_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetSpecialDayGroupInfo, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetSpecialDayGroupInfoList(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupInfoList_REQ * p_req, tsc_GetSpecialDayGroupInfoList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetSpecialDayGroupInfoList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetSpecialDayGroups(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroups_REQ * p_req, tsc_GetSpecialDayGroups_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetSpecialDayGroups, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetSpecialDayGroupList(ONVIF_DEVICE * p_dev, tsc_GetSpecialDayGroupList_REQ * p_req, tsc_GetSpecialDayGroupList_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetSpecialDayGroupList, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_CreateSpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_CreateSpecialDayGroup_REQ * p_req, tsc_CreateSpecialDayGroup_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscCreateSpecialDayGroup, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_ModifySpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_ModifySpecialDayGroup_REQ * p_req, tsc_ModifySpecialDayGroup_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscModifySpecialDayGroup, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_DeleteSpecialDayGroup(ONVIF_DEVICE * p_dev, tsc_DeleteSpecialDayGroup_REQ * p_req, tsc_DeleteSpecialDayGroup_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscDeleteSpecialDayGroup, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tsc_GetScheduleState(ONVIF_DEVICE * p_dev, tsc_GetScheduleState_REQ * p_req, tsc_GetScheduleState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.schedule.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.schedule.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etscGetScheduleState, p_dev, p_req, p_res);
}

#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

HT_API BOOL onvif_trv_GetServiceCapabilities(ONVIF_DEVICE * p_dev, trv_GetServiceCapabilities_REQ * p_req, trv_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_GetReceivers(ONVIF_DEVICE * p_dev, trv_GetReceivers_REQ * p_req, trv_GetReceivers_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvGetReceivers, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_GetReceiver(ONVIF_DEVICE * p_dev, trv_GetReceiver_REQ * p_req, trv_GetReceiver_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvGetReceiver, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_CreateReceiver(ONVIF_DEVICE * p_dev, trv_CreateReceiver_REQ * p_req, trv_CreateReceiver_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvCreateReceiver, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_DeleteReceiver(ONVIF_DEVICE * p_dev, trv_DeleteReceiver_REQ * p_req, trv_DeleteReceiver_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvDeleteReceiver, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_ConfigureReceiver(ONVIF_DEVICE * p_dev, trv_ConfigureReceiver_REQ * p_req, trv_ConfigureReceiver_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvConfigureReceiver, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_SetReceiverMode(ONVIF_DEVICE * p_dev, trv_SetReceiverMode_REQ * p_req, trv_SetReceiverMode_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvSetReceiverMode, p_dev, p_req, p_res);
}

HT_API BOOL onvif_trv_GetReceiverState(ONVIF_DEVICE * p_dev, trv_GetReceiverState_REQ * p_req, trv_GetReceiverState_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.receiver.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.receiver.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etrvGetReceiverState, p_dev, p_req, p_res);
}

#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

HT_API BOOL onvif_tpv_GetServiceCapabilities(ONVIF_DEVICE * p_dev, tpv_GetServiceCapabilities_REQ * p_req, tpv_GetServiceCapabilities_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvGetServiceCapabilities, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_PanMove(ONVIF_DEVICE * p_dev, tpv_PanMove_REQ * p_req, tpv_PanMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvPanMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_TiltMove(ONVIF_DEVICE * p_dev, tpv_TiltMove_REQ * p_req, tpv_TiltMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvTiltMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_ZoomMove(ONVIF_DEVICE * p_dev, tpv_ZoomMove_REQ * p_req, tpv_ZoomMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvZoomMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_RollMove(ONVIF_DEVICE * p_dev, tpv_RollMove_REQ * p_req, tpv_RollMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvRollMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_FocusMove(ONVIF_DEVICE * p_dev, tpv_FocusMove_REQ * p_req, tpv_FocusMove_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvFocusMove, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_Stop(ONVIF_DEVICE * p_dev, tpv_Stop_REQ * p_req, tpv_Stop_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvStop, p_dev, p_req, p_res);
}

HT_API BOOL onvif_tpv_GetUsage(ONVIF_DEVICE * p_dev, tpv_GetUsage_REQ * p_req, tpv_GetUsage_RES * p_res)
{
    HTTPREQ http_req;
    onvif_init_req(&http_req, &p_dev->binfo.XAddr);

    if (p_dev->Capabilities.provisioning.XAddr.url[0] != '\0')
    {
        strcpy(http_req.url, p_dev->Capabilities.provisioning.XAddr.url);
    }

    return http_onvif_trans(&http_req, p_dev->timeout, etpvGetUsage, p_dev, p_req, p_res);
}

#endif // end of PROVISIONING_SUPPORT



