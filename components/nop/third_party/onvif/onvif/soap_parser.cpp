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
#include "soap_parser.h"
#include "onvif_utils.h"
#include "util.h"

/***************************************************************************************/

HT_API BOOL parse_Bool(const char * pdata)
{    
    if (strcasecmp(pdata, "true") == 0)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

int parse_DeviceType1(const char * type)
{
    if (soap_strcmp(type, "dn:NetworkVideoTransmitter") == 0 || 
        soap_strcmp(type, "tds:Device") == 0)
    {
        return ODT_NVT;
    }
    else if (soap_strcmp(type, "dn:NetworkVideoDisplay") == 0)
    {
        return ODT_NVD;
    }
    else if (soap_strcmp(type, "dn:NetworkVideoStorage") == 0)
    {
        return ODT_NVS;
    }
    else if (soap_strcmp(type, "dn:NetworkVideoAnalytics") == 0)
    {
        return ODT_NVA;
    }

    return ODT_UNKNOWN;
}

/**
 * dn:NetworkVideoTransmitter tds:Device
 */
HT_API int parse_DeviceType(const char * pdata)
{
    int odt;
    const char *p1, *p2;
    char type[256] = {'\0'};
    
    p2 = pdata;
    p1 = strchr(pdata, ' ');
    while (p1)
    {
        strncpy(type, p2, p1-p2);

        odt = parse_DeviceType1(type);
        if (ODT_UNKNOWN != odt)
        {
            return odt;
        }

        p2 = p1+1;
        p1 = strchr(p2, ' ');
    }

    if (p2)
    {
        odt = parse_DeviceType1(p2);
        if (ODT_UNKNOWN != odt)
        {
            return odt;
        }
    }

    return parse_DeviceType1(pdata); 
}

/**
 * http://192.168.5.235/onvif/device_service http://[fe80::c256:e3ff:fea2:e019]/onvif/device_service 
 */
HT_API int parse_XAddr(const char * pdata, onvif_XAddr * p_xaddr)
{
    return onvif_parse_xaddr(pdata, p_xaddr->host, sizeof(p_xaddr->host), p_xaddr->url, sizeof(p_xaddr->url), &p_xaddr->port, &p_xaddr->https);   
}

int parse_Time(const char * pdata)
{
    return atoi(pdata+2);
}

BOOL parse_XSDDatetime(const char * s, time_t * p)
{
    if (NULL == s)
    {
        return FALSE;
    }
    
    char zone[32];
    struct tm T;
    const char *t;
    
    *zone = '\0';
    memset(&T, 0, sizeof(T));
    
    if (strchr(s, '-'))
    {
        t = "%d-%d-%dT%d:%d:%d%31s";
    }    
    else if (strchr(s, ':'))
    {
        t = "%4d%2d%2dT%d:%d:%d%31s";
    }    
    else /* parse non-XSD-standard alternative ISO 8601 format */
    {
        t = "%4d%2d%2dT%2d%2d%2d%31s";
    }
    
    if (sscanf(s, t, &T.tm_year, &T.tm_mon, &T.tm_mday, &T.tm_hour, &T.tm_min, &T.tm_sec, zone) < 6)
    {
        return FALSE;
    }
    
    if (T.tm_year == 1)
    {
        T.tm_year = 70;
    }    
    else
    {
        T.tm_year -= 1900;
    }
    
    T.tm_mon--;
    
    if (*zone == '.')
    { 
        for (s = zone + 1; *s; s++)
        {
            if (*s < '0' || *s > '9')
            {
                break;
            }    
        }    
    }
    else
    {
        s = zone;
    }
      
    if (*s)
    {
        if (*s == '+' || *s == '-')
        { 
            int h = 0, m = 0;
            if (s[3] == ':')
            { 
                /* +hh:mm */
                sscanf(s, "%d:%d", &h, &m);
                if (h < 0)
                    m = -m;
            }
            else /* +hhmm */
            {
                m = (int)strtol(s, NULL, 10);
                h = m / 100;
                m = m % 100;
            }
            
            T.tm_min -= m;
            T.tm_hour -= h;
            /* put hour and min in range */
            T.tm_hour += T.tm_min / 60;
            T.tm_min %= 60;
            
            if (T.tm_min < 0)
            { 
                T.tm_min += 60;
                T.tm_hour--;
            }
            
            T.tm_mday += T.tm_hour / 24;
            T.tm_hour %= 24;
            
            if (T.tm_hour < 0)
            {
                T.tm_hour += 24;
                T.tm_mday--;
            }
            /* note: day of the month may be out of range, timegm() handles it */
        }

        *p = onvif_timegm(&T);
    }
    else /* no UTC or timezone, so assume we got a localtime */
    { 
        T.tm_isdst = -1;
        *p = mktime(&T);
    }
    
    return TRUE;
}

BOOL parse_XSDDuration(const char *s, int *a)
{
    if (NULL == s)
    {
        return FALSE;
    }
    
    int sign = 1, Y = 0, M = 0, D = 0, H = 0, N = 0, S = 0;
    float f = 0;

    *a = 0;

    if (*s == '-')
    { 
        sign = -1;
        s++;
    }
    
    if (*s++ != 'P')
    {
        return FALSE;
    }
    
    /* date part */
    while (s && *s)
    { 
        int n;
        char k;
        if (*s == 'T')
        { 
            s++;
            break;
        }
        
        if (sscanf(s, "%d%c", &n, &k) != 2)
            return FALSE;
            
        s = strchr(s, k);
        if (!s)
            return FALSE;
            
        switch (k)
        { 
        case 'Y':
            Y = n;
            break;
            
        case 'M':
            M = n;
            break;
            
        case 'D':
            D = n;
            break;
            
        default:
            return FALSE;
        }
        
        s++;
    }
    
    /* time part */
    while (s && *s)
    { 
        int n;
        char k;
        if (sscanf(s, "%d%c", &n, &k) != 2)
            return FALSE;
            
        s = strchr(s, k);
        if (!s)
            return FALSE;
            
        switch (k)
        { 
        case 'H':
            H = n;
            break;
            
        case 'M':
            N = n;
            break;
            
        case '.':
            S = n;
            if (sscanf(s, "%g", &f) != 1)
                return FALSE;
            s = NULL;
            continue;
            
        case 'S':
            S = n;
            break;
            
        default:
            return FALSE;
        }
        
        s++;
    }
    /* convert Y-M-D H:N:S.f to signed int */
    *a = sign * ((((((((((Y * 12) + M) * 30) + D) * 24) + H) * 60) + N) * 60) + S);

    return TRUE;
}

BOOL parse_Fault(XMLN * p_node, onvif_Fault * p_res)
{
    XMLN * p_Fault;
    
    p_Fault = xml_node_soap_get(p_node, "Fault");
    if (p_Fault)
    {
        XMLN * p_Code;
        XMLN * p_Reason;        

        p_Code = xml_node_soap_get(p_Fault, "Code");
        if (p_Code)
        {
            XMLN * p_Value;
            XMLN * p_Subcode;

            p_Value = xml_node_soap_get(p_Code, "Value");
            if (p_Value && p_Value->data)
            {
                strncpy(p_res->Code, p_Value->data, sizeof(p_res->Code)-1);
            }

            p_Subcode = xml_node_soap_get(p_Code, "Subcode");
            if (p_Subcode)
            {
                p_Value = xml_node_soap_get(p_Subcode, "Value");
                if (p_Value && p_Value->data)
                {
                    strncpy(p_res->Subcode, p_Value->data, sizeof(p_res->Subcode)-1);
                }
            }
        }

        p_Reason = xml_node_soap_get(p_Fault, "Reason");
        if (p_Reason)
        {
            XMLN * p_Text;

            p_Text = xml_node_soap_get(p_Reason, "Text");
            if (p_Text && p_Text->data)
            {
                strncpy(p_res->Reason, p_Text->data, sizeof(p_res->Reason)-1);
            }
        }

        return TRUE;
    }
    else
    {
        return FALSE;
    }    
}

/* format : "1.0 2.0" */
BOOL parse_FloatRangeList(const char * p_buff, onvif_FloatRange * p_res)
{
    int i = 0;
    float min = 0.0, max = 0.0;
    const char * p_buf = p_buff;
    char p_min[32];
    
    // remove space
    while (p_buf && *p_buf != '\0') 
    {
        if (*p_buf == ' ') 
            p_buf++;
        else 
            break;
    }
    
    while (p_buf && *p_buf != '\0') 
    {
        if (*p_buf == ' ')
        {
            p_buf++;
            break;
        }
        else if (i < 31)
        {
            p_min[i++] = *p_buf;
            p_buf++;
        }
    }

    p_min[i] = '\0';
    min = (float)atof(p_min);

    while (p_buf && *p_buf != '\0')
    {
        if (*p_buf == ' ') 
            p_buf++;
        else 
            break;
    }

    if (p_buf)
    {
        max = (float)atof(p_buf);
    }

    p_res->Min = min;
    p_res->Max = max;
    
    return TRUE;
}

BOOL parse_IntList(XMLN * p_node, onvif_IntList * p_res)
{
    XMLN * p_Items;

    p_res->sizeItems = 0;
    
    p_Items = xml_node_soap_get(p_node, "Items");
    while (p_Items && p_Items->data && soap_strcmp(p_Items->name, "Items") == 0)
    {
        uint32 idx = p_res->sizeItems;

        p_res->Items[idx] = atoi(p_Items->data);

        p_res->sizeItems++;
        if (p_res->sizeItems >= ARRAY_SIZE(p_res->Items))
        {
            break;
        }

        p_Items = p_Items->next;
    }

    return TRUE;
}

BOOL parse_FloatList(XMLN * p_node, onvif_FloatList * p_req)
{
    XMLN * p_Items;

    p_req->sizeItems = 0;
    
    p_Items = xml_node_soap_get(p_node, "Items");
    while (p_Items && p_Items->data && soap_strcmp(p_Items->name, "Items") == 0)
    {
        int idx = p_req->sizeItems;

        p_req->Items[idx] = (float)atof(p_Items->data);

        p_req->sizeItems++;
        if (p_req->sizeItems >= ARRAY_SIZE(p_req->Items))
        {
            break;
        }

        p_Items = p_Items->next;
    }

    return TRUE;
}

BOOL parse_IntRange(XMLN * p_node, onvif_IntRange * p_res)
{
    XMLN * p_Min;
    XMLN * p_Max;

    p_Min = xml_node_soap_get(p_node, "Min");
    if (p_Min && p_Min->data)
    {
        p_res->Min = atoi(p_Min->data);
    }

    p_Max = xml_node_soap_get(p_node, "Max");
    if (p_Max && p_Max->data)
    {
        p_res->Max = atoi(p_Max->data);
    }

    return TRUE;
}

BOOL parse_FloatRange(XMLN * p_node, onvif_FloatRange * p_res)
{
    XMLN * p_Min;
    XMLN * p_Max;

    p_Min = xml_node_soap_get(p_node, "Min");
    if (p_Min && p_Min->data)
    {
        p_res->Min = (float)atof(p_Min->data);
    }
    
    p_Max = xml_node_soap_get(p_node, "Max");
    if (p_Max && p_Max->data)
    {
        p_res->Max = (float)atof(p_Max->data);
    }

    return TRUE;
}

/* format : "JPEG MPEG4 H264 G711 G726 AAC" */
BOOL parse_EncodingList(const char * p_encoding, onvif_RecordingCapabilities * p_res)
{
    int i = 0;
    const char * p_buf = p_encoding;
    char p_buff[32];

    while (*p_buf != '\0')
    {
        // remove space
        while (*p_buf != '\0') 
        {
            if (*p_buf == ' ') 
                p_buf++;
            else 
                break;
        }

        i = 0;
        while (*p_buf != '\0') 
        {
            if (*p_buf == ' ')
            {
                p_buf++;
                break;
            }
            else if (i < 31)
            {
                p_buff[i++] = *p_buf;
                p_buf++;
            }
        }

        p_buff[i] = '\0';

        if (strcasecmp(p_buff, "JPEG") == 0)
        {
            p_res->JPEG = 1;
        }
        else if (strcasecmp(p_buff, "MPEG4") == 0)
        {
            p_res->MPEG4 = 1;
        }
        else if (strcasecmp(p_buff, "H264") == 0)
        {
            p_res->H264 = 1;
        }
        else if (strcasecmp(p_buff, "H265") == 0)
        {
            p_res->H265 = 1;
        }
        else if (strcasecmp(p_buff, "G711") == 0)
        {
            p_res->G711 = 1;
        }
        else if (strcasecmp(p_buff, "G726") == 0)
        {
            p_res->G726 = 1;
        }
        else if (strcasecmp(p_buff, "AAC") == 0)
        {
            p_res->AAC = 1;
        }
    }
    
    return TRUE;
}

BOOL parse_Vector(XMLN * p_node, onvif_Vector * p_res)
{
    const char * p_x;
    const char * p_y;
    
    p_x = xml_attr_get(p_node, "x");
    if (p_x)
    {            
        p_res->x = (float)atof(p_x);
    }

    p_y = xml_attr_get(p_node, "y");
    if (p_y)
    {
        p_res->y = (float)atof(p_y);
    }

    return TRUE;
}

BOOL parse_Vector1D(XMLN * p_node, onvif_Vector1D * p_res)
{
    const char * p_x;
    
    p_x = xml_attr_get(p_node, "x");
    if (p_x)
    {            
        p_res->x = (float)atof(p_x);
    }

    return TRUE;
}

BOOL parse_PTZSpeed(XMLN * p_node, onvif_PTZSpeed * p_res)
{
    XMLN * p_PanTilt;
    XMLN * p_Zoom;

    p_PanTilt = xml_node_soap_get(p_node, "PanTilt");
    if (p_PanTilt)
    {
        p_res->PanTiltFlag = 1;        
        parse_Vector(p_PanTilt, &p_res->PanTilt);
    }

    p_Zoom = xml_node_soap_get(p_node, "Zoom");
    if (p_Zoom)
    {
        p_res->ZoomFlag = 1;        
        parse_Vector1D(p_Zoom, &p_res->Zoom);
    }

    return TRUE;
}

BOOL parse_PTZVector(XMLN * p_node, onvif_PTZVector * p_res)
{
    XMLN * p_PanTilt;
    XMLN * p_Zoom;

    p_PanTilt = xml_node_soap_get(p_node, "PanTilt");
    if (p_PanTilt)
    {    
        p_res->PanTiltFlag = 1;        
        parse_Vector(p_PanTilt, &p_res->PanTilt);
    }

    p_Zoom = xml_node_soap_get(p_node, "Zoom");
    if (p_Zoom)
    {    
        p_res->ZoomFlag = 1;        
        parse_Vector1D(p_Zoom, &p_res->Zoom);
    }

    return TRUE;
}


/***************************************************************************************/

BOOL parse_AnalyticsCapabilities(XMLN * p_node, onvif_AnalyticsCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_RuleSupport;
    XMLN * p_AnalyticsModuleSupport;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_RuleSupport = xml_node_soap_get(p_node, "RuleSupport");
    if (p_RuleSupport && p_RuleSupport->data)
    {
        p_res->RuleSupport = parse_Bool(p_RuleSupport->data);
    }

    p_AnalyticsModuleSupport = xml_node_soap_get(p_node, "_AnalyticsModuleSupport");
    if (p_AnalyticsModuleSupport && p_AnalyticsModuleSupport->data)
    {
        p_res->AnalyticsModuleSupport = parse_Bool(p_AnalyticsModuleSupport->data);
    }

    return TRUE;
}

BOOL parse_NetworkCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_IPFilter;
    XMLN * p_ZeroConfiguration;
    XMLN * p_IPVersion6;
    XMLN * p_DynDNS;
    XMLN * p_Extension;

    p_IPFilter = xml_node_soap_get(p_node, "IPFilter");
    if (p_IPFilter && p_IPFilter->data)
    {
        p_res->IPFilter = parse_Bool(p_IPFilter->data);
    }

    p_ZeroConfiguration = xml_node_soap_get(p_node, "ZeroConfiguration");
    if (p_ZeroConfiguration && p_ZeroConfiguration->data)
    {
        p_res->ZeroConfiguration = parse_Bool(p_ZeroConfiguration->data);
    }

    p_IPVersion6 = xml_node_soap_get(p_node, "IPVersion6");
    if (p_IPVersion6 && p_IPVersion6->data)
    {
        p_res->IPVersion6 = parse_Bool(p_IPVersion6->data);
    }

    p_DynDNS = xml_node_soap_get(p_node, "DynDNS");
    if (p_DynDNS && p_DynDNS->data)
    {
        p_res->DynDNS = parse_Bool(p_DynDNS->data);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_Dot11Configuration;
        
        p_Dot11Configuration = xml_node_soap_get(p_Extension, "Dot11Configuration");
        if (p_Dot11Configuration && p_Dot11Configuration->data)
        {
            p_res->Dot11Configuration = parse_Bool(p_Dot11Configuration->data);
        }
    }

    return TRUE;
}

BOOL parse_IOCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_InputConnectors;
    XMLN * p_RelayOutputs;
    XMLN * p_Extension;

    p_InputConnectors = xml_node_soap_get(p_node, "InputConnectors");
    if (p_InputConnectors && p_InputConnectors->data)
    {
        p_res->InputConnectors = atoi(p_InputConnectors->data);
    }

    p_RelayOutputs = xml_node_soap_get(p_node, "RelayOutputs");
    if (p_RelayOutputs && p_RelayOutputs->data)
    {
        p_res->RelayOutputs = atoi(p_RelayOutputs->data);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_Auxiliary;
        XMLN * p_AuxiliaryCommands;
    
        p_Auxiliary = xml_node_soap_get(p_Extension, "Auxiliary");
        if (p_Auxiliary && p_Auxiliary->data)
        {
            p_res->Auxiliary = parse_Bool(p_Auxiliary->data);
        }

        strcpy(p_res->AuxiliaryCommands, "");
      
        p_AuxiliaryCommands = xml_node_soap_get(p_Extension, "AuxiliaryCommands");
        while (p_AuxiliaryCommands && p_AuxiliaryCommands->data && soap_strcmp(p_AuxiliaryCommands->name, "AuxiliaryCommands") == 0)
        {
            strncat(p_res->AuxiliaryCommands, p_AuxiliaryCommands->data, sizeof(p_res->AuxiliaryCommands)-1);
            strncat(p_res->AuxiliaryCommands, " ", sizeof(p_res->AuxiliaryCommands)-1);
            
            p_AuxiliaryCommands = p_AuxiliaryCommands->next;
        }
    }

    return TRUE;
}

BOOL parse_SystemCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_DiscoveryResolve;
    XMLN * p_DiscoveryBye;
    XMLN * p_RemoteDiscovery;
    XMLN * p_SystemBackup;
    XMLN * p_SystemLogging;
    XMLN * p_FirmwareUpgrade;
    XMLN * p_SupportedVersions;
    XMLN * p_Extension;

    p_DiscoveryResolve = xml_node_soap_get(p_node, "DiscoveryResolve");
    if (p_DiscoveryResolve && p_DiscoveryResolve->data)
    {
        p_res->DiscoveryResolve = parse_Bool(p_DiscoveryResolve->data);
    }

    p_DiscoveryBye = xml_node_soap_get(p_node, "DiscoveryBye");
    if (p_DiscoveryBye && p_DiscoveryBye->data)
    {
        p_res->DiscoveryBye = parse_Bool(p_DiscoveryBye->data);
    }

    p_RemoteDiscovery = xml_node_soap_get(p_node, "RemoteDiscovery");
    if (p_RemoteDiscovery && p_RemoteDiscovery->data)
    {
        p_res->RemoteDiscovery = parse_Bool(p_RemoteDiscovery->data);
    }

    p_SystemBackup = xml_node_soap_get(p_node, "SystemBackup");
    if (p_SystemBackup && p_SystemBackup->data)
    {
        p_res->SystemBackup = parse_Bool(p_SystemBackup->data);
    }

    p_SystemLogging = xml_node_soap_get(p_node, "SystemLogging");
    if (p_SystemLogging && p_SystemLogging->data)
    {
        p_res->SystemLogging = parse_Bool(p_SystemLogging->data);
    }

    p_FirmwareUpgrade = xml_node_soap_get(p_node, "FirmwareUpgrade");
    if (p_FirmwareUpgrade && p_FirmwareUpgrade->data)
    {
        p_res->FirmwareUpgrade = parse_Bool(p_FirmwareUpgrade->data);
    }

    p_res->sizeSupportedVersions = 0;
    
    p_SupportedVersions = xml_node_soap_get(p_node, "SupportedVersions");
    while (p_SupportedVersions && soap_strcmp(p_SupportedVersions->name, "SupportedVersions") == 0)
    {
        uint32 idx = p_res->sizeSupportedVersions;
        
        if (parse_Version(p_SupportedVersions, &p_res->SupportedVersions[idx]))
        {
            p_res->sizeSupportedVersions++;
            if (p_res->sizeSupportedVersions >= ARRAY_SIZE(p_res->SupportedVersions))
            {
                break;
            }
        }
        
        p_SupportedVersions = p_SupportedVersions->next;
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_HttpFirmwareUpgrade;
        XMLN * p_HttpSystemBackup;
        XMLN * p_HttpSystemLogging;
        XMLN * p_HttpSupportInformation;
        
        p_HttpFirmwareUpgrade = xml_node_soap_get(p_Extension, "HttpFirmwareUpgrade");
        if (p_HttpFirmwareUpgrade && p_HttpFirmwareUpgrade->data)
        {
            p_res->HttpFirmwareUpgrade = parse_Bool(p_HttpFirmwareUpgrade->data);
        }

        p_HttpSystemBackup = xml_node_soap_get(p_Extension, "HttpSystemBackup");
        if (p_HttpSystemBackup && p_HttpSystemBackup->data)
        {
            p_res->HttpSystemBackup = parse_Bool(p_HttpSystemBackup->data);
        }

        p_HttpSystemLogging = xml_node_soap_get(p_Extension, "HttpSystemLogging");
        if (p_HttpSystemLogging && p_HttpSystemLogging->data)
        {
            p_res->HttpSystemLogging = parse_Bool(p_HttpSystemLogging->data);
        }

        p_HttpSupportInformation = xml_node_soap_get(p_Extension, "HttpSupportInformation");
        if (p_HttpSupportInformation && p_HttpSupportInformation->data)
        {
            p_res->HttpSupportInformation = parse_Bool(p_HttpSupportInformation->data);
        }
    }

    return TRUE;
}

BOOL parse_SecurityExtensionCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_TLS10;
    XMLN * p_Extension;
    
    p_TLS10 = xml_node_soap_get(p_node, "TLS1.0");
    if (p_TLS10 && p_TLS10->data)
    {
        p_res->TLS10 = parse_Bool(p_TLS10->data);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_Dot1X;
        XMLN * p_SupportedEAPMethod;
        XMLN * p_RemoteUserHandling;
        
        p_Dot1X = xml_node_soap_get(p_Extension, "Dot1X");
        if (p_Dot1X && p_Dot1X->data)
        {
            p_res->Dot1X = parse_Bool(p_Dot1X->data);
        }

        p_SupportedEAPMethod = xml_node_soap_get(p_Extension, "SupportedEAPMethod");
        if (p_SupportedEAPMethod && p_SupportedEAPMethod->data)
        {
            p_res->SupportedEAPMethods = atoi(p_SupportedEAPMethod->data);
        }

        p_RemoteUserHandling = xml_node_soap_get(p_Extension, "RemoteUserHandling");
        if (p_RemoteUserHandling && p_RemoteUserHandling->data)
        {
            p_res->RemoteUserHandling = atoi(p_RemoteUserHandling->data);
        }
    }

    return TRUE;
}

BOOL parse_SecurityCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_TLS11;
    XMLN * p_TLS12;
    XMLN * p_OnboardKeyGeneration;
    XMLN * p_AccessPolicyConfig;
    XMLN * p_X509Token;
    XMLN * p_SAMLToken;
    XMLN * p_KerberosToken;
    XMLN * p_RELToken;
    XMLN * p_Extension;

    p_TLS11 = xml_node_soap_get(p_node, "TLS1.1");
    if (p_TLS11 && p_TLS11->data)
    {
        p_res->TLS11 = parse_Bool(p_TLS11->data);
    }

    p_TLS12 = xml_node_soap_get(p_node, "TLS1.2");
    if (p_TLS12 && p_TLS12->data)
    {
        p_res->TLS12 = parse_Bool(p_TLS12->data);
    }

    p_OnboardKeyGeneration = xml_node_soap_get(p_node, "OnboardKeyGeneration");
    if (p_OnboardKeyGeneration && p_OnboardKeyGeneration->data)
    {
        p_res->OnboardKeyGeneration = parse_Bool(p_OnboardKeyGeneration->data);
    }

    p_AccessPolicyConfig = xml_node_soap_get(p_node, "AccessPolicyConfig");
    if (p_AccessPolicyConfig && p_AccessPolicyConfig->data)
    {
        p_res->AccessPolicyConfig = parse_Bool(p_AccessPolicyConfig->data);
    }

    p_X509Token = xml_node_soap_get(p_node, "X.509Token");
    if (p_X509Token && p_X509Token->data)
    {
        p_res->X509Token = parse_Bool(p_X509Token->data);
    }

    p_SAMLToken = xml_node_soap_get(p_node, "SAMLToken");
    if (p_SAMLToken && p_SAMLToken->data)
    {
        p_res->SAMLToken = parse_Bool(p_SAMLToken->data);
    }

    p_KerberosToken = xml_node_soap_get(p_node, "KerberosToken");
    if (p_KerberosToken && p_KerberosToken->data)
    {
        p_res->KerberosToken = parse_Bool(p_KerberosToken->data);
    }

    p_RELToken = xml_node_soap_get(p_node, "RELToken");
    if (p_RELToken && p_RELToken->data)
    {
        p_res->RELToken = parse_Bool(p_RELToken->data);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        parse_SecurityExtensionCapabilities(p_Extension, p_res);
    }

    return TRUE;
}

BOOL parse_DeviceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_Network;
    XMLN * p_System;
    XMLN * p_IO;
    XMLN * p_Security;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_Network = xml_node_soap_get(p_node, "Network");
    if (p_Network)
    {
        parse_NetworkCapabilities(p_Network, p_res);
    }

    p_IO = xml_node_soap_get(p_node, "IO");
    if (p_IO)
    {
        parse_IOCapabilities(p_IO, p_res);
    }
    
    p_System = xml_node_soap_get(p_node, "System");
    if (p_System)
    {
        parse_SystemCapabilities(p_System, p_res);
    }

    p_Security = xml_node_soap_get(p_node, "Security");
    if (p_Security)
    {
        parse_SecurityCapabilities(p_Security, p_res);
    }
    
    return TRUE;
}

BOOL parse_EventsCapabilities(XMLN * p_node, onvif_EventCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_WSSubscriptionPolicySupport;
    XMLN * p_WSPullPointSupport;
    XMLN * p_WSPausableSubscriptionManagerInterfaceSupport;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_WSSubscriptionPolicySupport = xml_node_soap_get(p_node, "WSSubscriptionPolicySupport");
    if (p_WSSubscriptionPolicySupport && p_WSSubscriptionPolicySupport->data)
    {
        p_res->WSSubscriptionPolicySupport = parse_Bool(p_WSSubscriptionPolicySupport->data);
    }

    p_WSPullPointSupport = xml_node_soap_get(p_node, "WSPullPointSupport");
    if (p_WSPullPointSupport && p_WSPullPointSupport->data)
    {
        p_res->WSPullPointSupport = parse_Bool(p_WSPullPointSupport->data);
    }

    p_WSPausableSubscriptionManagerInterfaceSupport = xml_node_soap_get(p_node, "WSPausableSubscriptionManagerInterfaceSupport");
    if (p_WSPausableSubscriptionManagerInterfaceSupport && p_WSPausableSubscriptionManagerInterfaceSupport->data)
    {
        p_res->WSPausableSubscriptionManagerInterfaceSupport = parse_Bool(p_WSPausableSubscriptionManagerInterfaceSupport->data);
    }

    return TRUE;
}

BOOL parse_ImageCapabilities(XMLN * p_node, onvif_ImagingCapabilities * p_res)
{
    XMLN * p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL parse_StreamingCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res)
{
    XMLN * p_RTPMulticast;
    XMLN * p_RTP_TCP;
    XMLN * p_RTP_RTSP_TCP;

    p_RTPMulticast = xml_node_soap_get(p_node, "RTPMulticast");
    if (p_RTPMulticast && p_RTPMulticast->data)
    {
        p_res->RTPMulticast = parse_Bool(p_RTPMulticast->data);
    }

    p_RTP_TCP = xml_node_soap_get(p_node, "RTP_TCP");
    if (p_RTP_TCP && p_RTP_TCP->data)
    {
        p_res->RTP_TCP = parse_Bool(p_RTP_TCP->data);
    }

    p_RTP_RTSP_TCP = xml_node_soap_get(p_node, "RTP_RTSP_TCP");
    if (p_RTP_RTSP_TCP && p_RTP_RTSP_TCP->data)
    {
        p_res->RTP_RTSP_TCP = parse_Bool(p_RTP_RTSP_TCP->data);
    }

    return TRUE;
}

BOOL parse_MediaCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_StreamingCapabilities;
    XMLN * p_Extension;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_StreamingCapabilities = xml_node_soap_get(p_node, "StreamingCapabilities");
    if (p_StreamingCapabilities)
    {
        parse_StreamingCapabilities(p_StreamingCapabilities, p_res);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_ProfileCapabilities;
        
        p_ProfileCapabilities = xml_node_soap_get(p_Extension, "ProfileCapabilities");
        if (p_ProfileCapabilities)
        {
            XMLN * p_MaximumNumberOfProfiles;
            
            p_MaximumNumberOfProfiles = xml_node_soap_get(p_ProfileCapabilities, "MaximumNumberOfProfiles");
            if (p_MaximumNumberOfProfiles && p_MaximumNumberOfProfiles->data)
            {
                p_res->MaximumNumberOfProfiles = atoi(p_MaximumNumberOfProfiles->data);
            }
        }
    }
                    
    return TRUE;
}

BOOL parse_PTZCapabilities(XMLN * p_node, onvif_PTZCapabilities * p_res)
{
    XMLN * p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL parse_DeviceIOCapabilities(XMLN * p_node, onvif_DeviceIOCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_VideoSources;
    XMLN * p_VideoOutputs;
    XMLN * p_AudioSources;
    XMLN * p_AudioOutputs;
    XMLN * p_RelayOutputs;
    XMLN * p_SerialPorts;
    XMLN * p_DigitalInputs;
    XMLN * p_DigitalInputOptions;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_VideoSources = xml_node_soap_get(p_node, "VideoSources");
    if (p_VideoSources && p_VideoSources->data)
    {
        p_res->VideoSourcesFlag = 1;
        p_res->VideoSources = atoi(p_VideoSources->data);
    }

    p_VideoOutputs = xml_node_soap_get(p_node, "VideoOutputs");
    if (p_VideoOutputs && p_VideoOutputs->data)
    {
        p_res->VideoOutputsFlag = 1;
        p_res->VideoOutputs = atoi(p_VideoOutputs->data);
    }

    p_AudioSources = xml_node_soap_get(p_node, "AudioSources");
    if (p_AudioSources && p_AudioSources->data)
    {
        p_res->AudioSourcesFlag = 1;
        p_res->AudioSources = atoi(p_AudioSources->data);
    }

    p_AudioOutputs = xml_node_soap_get(p_node, "AudioOutputs");
    if (p_AudioOutputs && p_AudioOutputs->data)
    {
        p_res->AudioOutputsFlag = 1;
        p_res->AudioOutputs = atoi(p_AudioOutputs->data);
    }

    p_RelayOutputs = xml_node_soap_get(p_node, "RelayOutputs");
    if (p_RelayOutputs && p_RelayOutputs->data)
    {
        p_res->RelayOutputsFlag = 1;
        p_res->RelayOutputs = atoi(p_RelayOutputs->data);
    }

    p_SerialPorts = xml_node_soap_get(p_node, "SerialPorts");
    if (p_SerialPorts && p_SerialPorts->data)
    {
        p_res->SerialPortsFlag = 1;
        p_res->SerialPorts = atoi(p_SerialPorts->data);
    }

    p_DigitalInputs = xml_node_soap_get(p_node, "DigitalInputs");
    if (p_DigitalInputs && p_DigitalInputs->data)
    {
        p_res->DigitalInputsFlag = 1;
        p_res->DigitalInputs = atoi(p_DigitalInputs->data);
    }

    p_DigitalInputOptions = xml_node_soap_get(p_node, "DigitalInputOptions");
    if (p_DigitalInputOptions && p_DigitalInputOptions->data)
    {
        p_res->DigitalInputOptionsFlag = 1;
        p_res->DigitalInputOptions = parse_Bool(p_DigitalInputOptions->data);
    }        

    return TRUE;
}

BOOL parse_RecordingCapabilities(XMLN * p_node, onvif_RecordingCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_ReceiverSource;
    XMLN * p_MediaProfileSource;
    XMLN * p_DynamicRecordings;
    XMLN * p_DynamicTracks;
    XMLN * p_MaxStringLength;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_ReceiverSource = xml_node_soap_get(p_node, "ReceiverSource");
    if (p_ReceiverSource && p_ReceiverSource->data)
    {
        p_res->ReceiverSource = parse_Bool(p_ReceiverSource->data);
    }

    p_MediaProfileSource = xml_node_soap_get(p_node, "MediaProfileSource");
    if (p_MediaProfileSource && p_MediaProfileSource->data)
    {
        p_res->MediaProfileSource = parse_Bool(p_MediaProfileSource->data);
    }

    p_DynamicRecordings = xml_node_soap_get(p_node, "DynamicRecordings");
    if (p_DynamicRecordings && p_DynamicRecordings->data)
    {
        p_res->DynamicRecordings = parse_Bool(p_DynamicRecordings->data);
    }

    p_DynamicTracks = xml_node_soap_get(p_node, "DynamicTracks");
    if (p_DynamicTracks && p_DynamicTracks->data)
    {
        p_res->DynamicTracks = parse_Bool(p_DynamicTracks->data);
    }

    p_MaxStringLength = xml_node_soap_get(p_node, "MaxStringLength");
    if (p_MaxStringLength && p_MaxStringLength->data)
    {
        p_res->MaxStringLength = atoi(p_MaxStringLength->data);
    }
    
    return TRUE;
}

BOOL parse_SearchCapabilities(XMLN * p_node, onvif_SearchCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_MetadataSearch;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_MetadataSearch = xml_node_soap_get(p_node, "MetadataSearch");
    if (p_MetadataSearch && p_MetadataSearch->data)
    {
        p_res->MetadataSearch = parse_Bool(p_MetadataSearch->data);
    }

    return TRUE;
}

BOOL parse_ReplayCapabilities(XMLN * p_node, onvif_ReplayCapabilities * p_res)
{
    XMLN * p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL parse_Version(XMLN * p_node, onvif_Version * p_res)
{
    XMLN * p_Major;
    XMLN * p_Minor;

    p_Major = xml_node_soap_get(p_node, "Major");
    if (p_Major && p_Major->data)
    {
        p_res->Major = atoi(p_Major->data);
    }

    p_Minor = xml_node_soap_get(p_node, "Minor");
    if (p_Minor && p_Minor->data)
    {
        p_res->Minor = atoi(p_Minor->data);
    }

    return TRUE;
}

BOOL parse_NetworkServiceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    const char * p_IPFilter;
    const char * p_ZeroConfiguration;
    const char * p_IPVersion6;
    const char * p_DynDNS;
    const char * p_Dot11Configuration;
    const char * p_HostnameFromDHCP;
    const char * p_DHCPv6;
    const char * p_Dot1XConfigurations;
    const char * p_NTP;

    p_IPFilter = xml_attr_get(p_node, "IPFilter");
    if (p_IPFilter)
    {
        p_res->IPFilter = parse_Bool(p_IPFilter);
    }

    p_ZeroConfiguration = xml_attr_get(p_node, "ZeroConfiguration");
    if (p_ZeroConfiguration)
    {
        p_res->ZeroConfiguration = parse_Bool(p_ZeroConfiguration);
    }

    p_IPVersion6 = xml_attr_get(p_node, "IPVersion6");
    if (p_IPVersion6)
    {
        p_res->IPVersion6 = parse_Bool(p_IPVersion6);
    }

    p_DynDNS = xml_attr_get(p_node, "DynDNS");
    if (p_DynDNS)
    {
        p_res->DynDNS = parse_Bool(p_DynDNS);
    }

    p_Dot11Configuration = xml_attr_get(p_node, "Dot11Configuration");
    if (p_Dot11Configuration)
    {
        p_res->Dot11Configuration = parse_Bool(p_Dot11Configuration);
    }

    p_HostnameFromDHCP = xml_attr_get(p_node, "HostnameFromDHCP");
    if (p_HostnameFromDHCP)
    {
        p_res->HostnameFromDHCP = parse_Bool(p_HostnameFromDHCP);
    }

    p_DHCPv6 = xml_attr_get(p_node, "DHCPv6");
    if (p_DHCPv6)
    {
        p_res->DHCPv6 = parse_Bool(p_DHCPv6);
    }

    p_Dot1XConfigurations = xml_attr_get(p_node, "Dot1XConfigurations");
    if (p_Dot1XConfigurations)
    {
        p_res->Dot1XConfigurations = atoi(p_Dot1XConfigurations);
    }

    p_NTP = xml_attr_get(p_node, "NTP");
    if (p_NTP)
    {
        p_res->NTP = atoi(p_NTP);
    }

    return TRUE;
}

BOOL parse_SecurityServiceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    const char * p_TLS10;
    const char * p_TLS11;
    const char * p_TLS12;
    const char * p_OnboardKeyGeneration;
    const char * p_AccessPolicyConfig;
    const char * p_DefaultAccessPolicy;
    const char * p_Dot1X;
    const char * p_RemoteUserHandling;
    const char * p_X509Token;
    const char * p_SAMLToken;
    const char * p_KerberosToken;
    const char * p_UsernameToken;
    const char * p_HttpDigest;
    const char * p_RELToken;
    const char * p_JsonWebToken;
    const char * p_SupportedEAPMethods;
    const char * p_MaxUsers;
    const char * p_MaxUserNameLength;
    const char * p_MaxPasswordLength;
    const char * p_SecurityPolicies;
    const char * p_HashingAlgorithms;

    p_TLS10 = xml_attr_get(p_node, "TLS1.0");
    if (p_TLS10)
    {
        p_res->TLS10 = parse_Bool(p_TLS10);
    }

    p_TLS11 = xml_attr_get(p_node, "TLS1.1");
    if (p_TLS11)
    {
        p_res->TLS11 = parse_Bool(p_TLS11);
    }

    p_TLS12 = xml_attr_get(p_node, "TLS1.2");
    if (p_TLS12)
    {
        p_res->TLS12 = parse_Bool(p_TLS12);
    }

    p_OnboardKeyGeneration = xml_attr_get(p_node, "OnboardKeyGeneration");
    if (p_OnboardKeyGeneration)
    {
        p_res->OnboardKeyGeneration = parse_Bool(p_OnboardKeyGeneration);
    }

    p_AccessPolicyConfig = xml_attr_get(p_node, "AccessPolicyConfig");
    if (p_AccessPolicyConfig)
    {
        p_res->AccessPolicyConfig = parse_Bool(p_AccessPolicyConfig);
    }

    p_DefaultAccessPolicy = xml_attr_get(p_node, "DefaultAccessPolicy");
    if (p_DefaultAccessPolicy)
    {
        p_res->DefaultAccessPolicy = parse_Bool(p_DefaultAccessPolicy);
    }

    p_Dot1X = xml_attr_get(p_node, "Dot1X");
    if (p_Dot1X)
    {
        p_res->Dot1X = parse_Bool(p_Dot1X);
    }

    p_RemoteUserHandling = xml_attr_get(p_node, "RemoteUserHandling");
    if (p_RemoteUserHandling)
    {
        p_res->RemoteUserHandling = parse_Bool(p_RemoteUserHandling);
    }

    p_X509Token = xml_attr_get(p_node, "X.509Token");
    if (p_X509Token)
    {
        p_res->X509Token = parse_Bool(p_X509Token);
    }

    p_SAMLToken = xml_attr_get(p_node, "SAMLToken");
    if (p_SAMLToken)
    {
        p_res->SAMLToken = parse_Bool(p_SAMLToken);
    }

    p_KerberosToken = xml_attr_get(p_node, "KerberosToken");
    if (p_KerberosToken)
    {
        p_res->KerberosToken = parse_Bool(p_KerberosToken);
    }

    p_UsernameToken = xml_attr_get(p_node, "UsernameToken");
    if (p_UsernameToken)
    {
        p_res->UsernameToken = parse_Bool(p_UsernameToken);
    }

    p_HttpDigest = xml_attr_get(p_node, "HttpDigest");
    if (p_HttpDigest)
    {
        p_res->HttpDigest = parse_Bool(p_HttpDigest);
    }

    p_RELToken = xml_attr_get(p_node, "RELToken");
    if (p_RELToken)
    {
        p_res->RELToken = parse_Bool(p_RELToken);
    }

    p_JsonWebToken = xml_attr_get(p_node, "JsonWebToken");
    if (p_JsonWebToken)
    {
        p_res->JsonWebToken = parse_Bool(p_JsonWebToken);
    }
    
    p_SupportedEAPMethods = xml_attr_get(p_node, "SupportedEAPMethods");
    if (p_SupportedEAPMethods)
    {
        p_res->SupportedEAPMethods = atoi(p_SupportedEAPMethods);
    }

    p_MaxUsers = xml_attr_get(p_node, "MaxUsers");
    if (p_MaxUsers)
    {
        p_res->MaxUsers = atoi(p_MaxUsers);
    }

    p_MaxUserNameLength = xml_attr_get(p_node, "MaxUserNameLength");
    if (p_MaxUserNameLength)
    {
        p_res->MaxUserNameLength = atoi(p_MaxUserNameLength);
    }

    p_MaxPasswordLength = xml_attr_get(p_node, "MaxPasswordLength");
    if (p_MaxPasswordLength)
    {
        p_res->MaxPasswordLength = atoi(p_MaxPasswordLength);
    }

    p_SecurityPolicies = xml_attr_get(p_node, "SecurityPolicies");
    if (p_SecurityPolicies)
    {
        strncpy(p_res->SecurityPolicies, p_SecurityPolicies, sizeof(p_res->SecurityPolicies)-1);
    }
    
    p_HashingAlgorithms = xml_attr_get(p_node, "HashingAlgorithms");
    if (p_HashingAlgorithms)
    {
        strncpy(p_res->HashingAlgorithms, p_HashingAlgorithms, sizeof(p_res->HashingAlgorithms)-1);
    }

    return TRUE;
}

BOOL parse_SystemServiceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    const char * p_DiscoveryResolve;
    const char * p_DiscoveryBye;
    const char * p_RemoteDiscovery;
    const char * p_SystemBackup;
    const char * p_SystemLogging;
    const char * p_FirmwareUpgrade;
    const char * p_HttpFirmwareUpgrade;
    const char * p_HttpSystemBackup;
    const char * p_HttpSystemLogging;
    const char * p_HttpSupportInformation;
    const char * p_StorageConfiguration;
    const char * p_MaxStorageConfigurations;
    const char * p_GeoLocationEntries;
    const char * p_AutoGeo;
    const char * p_StorageTypesSupported;
    const char * p_DiscoveryNotSupported;
    const char * p_NetworkConfigNotSupported;
    const char * p_UserConfigNotSupported;
    const char * p_Addons;
    const char * p_HardwareType;

    p_DiscoveryResolve = xml_attr_get(p_node, "DiscoveryResolve");
    if (p_DiscoveryResolve)
    {
        p_res->DiscoveryResolve = parse_Bool(p_DiscoveryResolve);
    }

    p_DiscoveryBye = xml_attr_get(p_node, "DiscoveryBye");
    if (p_DiscoveryBye)
    {
        p_res->DiscoveryBye = parse_Bool(p_DiscoveryBye);
    }

    p_RemoteDiscovery = xml_attr_get(p_node, "RemoteDiscovery");
    if (p_RemoteDiscovery)
    {
        p_res->RemoteDiscovery = parse_Bool(p_RemoteDiscovery);
    }

    p_SystemBackup = xml_attr_get(p_node, "SystemBackup");
    if (p_SystemBackup)
    {
        p_res->SystemBackup = parse_Bool(p_SystemBackup);
    }

    p_SystemLogging = xml_attr_get(p_node, "SystemLogging");
    if (p_SystemLogging)
    {
        p_res->SystemLogging = parse_Bool(p_SystemLogging);
    }

    p_FirmwareUpgrade = xml_attr_get(p_node, "FirmwareUpgrade");
    if (p_FirmwareUpgrade)
    {
        p_res->FirmwareUpgrade = parse_Bool(p_FirmwareUpgrade);
    }

    p_HttpFirmwareUpgrade = xml_attr_get(p_node, "HttpFirmwareUpgrade");
    if (p_HttpFirmwareUpgrade)
    {
        p_res->HttpFirmwareUpgrade = parse_Bool(p_HttpFirmwareUpgrade);
    }

    p_HttpSystemBackup = xml_attr_get(p_node, "HttpSystemBackup");
    if (p_HttpSystemBackup)
    {
        p_res->HttpSystemBackup = parse_Bool(p_HttpSystemBackup);
    }

    p_HttpSystemLogging = xml_attr_get(p_node, "HttpSystemLogging");
    if (p_HttpSystemLogging)
    {
        p_res->HttpSystemLogging = parse_Bool(p_HttpSystemLogging);
    }

    p_HttpSupportInformation = xml_attr_get(p_node, "HttpSupportInformation");
    if (p_HttpSupportInformation)
    {
        p_res->HttpSupportInformation = parse_Bool(p_HttpSupportInformation);
    }

    p_StorageConfiguration = xml_attr_get(p_node, "StorageConfiguration");
    if (p_StorageConfiguration)
    {
        p_res->StorageConfiguration = parse_Bool(p_StorageConfiguration);
    }

    p_MaxStorageConfigurations = xml_attr_get(p_node, "MaxStorageConfigurations");
    if (p_MaxStorageConfigurations)
    {
        p_res->MaxStorageConfigurations = atoi(p_MaxStorageConfigurations);
    }

    p_GeoLocationEntries = xml_attr_get(p_node, "GeoLocationEntries");
    if (p_GeoLocationEntries)
    {
        p_res->GeoLocationEntries = atoi(p_GeoLocationEntries);
    }

    p_AutoGeo = xml_attr_get(p_node, "AutoGeo");
    if (p_AutoGeo)
    {
        strncpy(p_res->AutoGeo, p_AutoGeo, sizeof(p_res->AutoGeo)-1);
    }

    p_StorageTypesSupported = xml_attr_get(p_node, "StorageTypesSupported");
    if (p_StorageTypesSupported)
    {
        strncpy(p_res->StorageTypesSupported, p_StorageTypesSupported, sizeof(p_res->StorageTypesSupported)-1);
    }

    p_DiscoveryNotSupported = xml_attr_get(p_node, "DiscoveryNotSupported");
    if (p_DiscoveryNotSupported)
    {
        p_res->DiscoveryNotSupported = parse_Bool(p_DiscoveryNotSupported);
    }

    p_NetworkConfigNotSupported = xml_attr_get(p_node, "NetworkConfigNotSupported");
    if (p_NetworkConfigNotSupported)
    {
        p_res->NetworkConfigNotSupported = parse_Bool(p_NetworkConfigNotSupported);
    }

    p_UserConfigNotSupported = xml_attr_get(p_node, "UserConfigNotSupported");
    if (p_UserConfigNotSupported)
    {
        p_res->UserConfigNotSupported = parse_Bool(p_UserConfigNotSupported);
    }

    p_Addons = xml_attr_get(p_node, "Addons");
    if (p_Addons)
    {
        strncpy(p_res->Addons, p_Addons, sizeof(p_res->Addons)-1);
    }

    p_HardwareType = xml_attr_get(p_node, "HardwareType");
    if (p_HardwareType)
    {
        strncpy(p_res->HardwareType, p_HardwareType, sizeof(p_res->HardwareType)-1);
    }

    return TRUE;
}

BOOL parse_DeviceServiceCapabilities(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_Network;
    XMLN * p_Security;
    XMLN * p_System;
    XMLN * p_Misc;

    p_Network = xml_node_soap_get(p_node, "Network");
    if (p_Network)
    {
        parse_NetworkServiceCapabilities(p_Network, p_res);
    }

    p_Security = xml_node_soap_get(p_node, "Security");
    if (p_Security)
    {
        parse_SecurityServiceCapabilities(p_Security, p_res);
    }

    p_System = xml_node_soap_get(p_node, "System");
    if (p_System)
    {
        parse_SystemServiceCapabilities(p_System, p_res);
    }

    p_Misc = xml_node_soap_get(p_node, "Misc");
    if (p_Misc)
    {
        const char * p_AuxiliaryCommands = xml_attr_get(p_Misc, "AuxiliaryCommands");
        if (p_AuxiliaryCommands)
        {
            strncpy(p_res->AuxiliaryCommands, p_AuxiliaryCommands, sizeof(p_res->AuxiliaryCommands)-1);
        }
    }

    return TRUE;
}

BOOL parse_DeviceService(XMLN * p_node, onvif_DevicesCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;

    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_DeviceServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_MediaStreamingCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res)
{
    const char * p_RTPMulticast;
    const char * p_RTP_TCP;
    const char * p_RTP_RTSP_TCP;
    const char * p_NonAggregateControl;
    const char * p_NoRTSPStreaming;

    p_RTPMulticast = xml_attr_get(p_node, "RTPMulticast");
    if (p_RTPMulticast)
    {
        p_res->RTPMulticast = parse_Bool(p_RTPMulticast);
    }
    
    p_RTP_TCP = xml_attr_get(p_node, "RTP_TCP");
    if (p_RTP_TCP)
    {
        p_res->RTP_TCP = parse_Bool(p_RTP_TCP);
    }

    p_RTP_RTSP_TCP = xml_attr_get(p_node, "RTP_RTSP_TCP");
    if (p_RTP_RTSP_TCP)
    {
        p_res->RTP_RTSP_TCP = parse_Bool(p_RTP_RTSP_TCP);
    }

    p_NonAggregateControl = xml_attr_get(p_node, "NonAggregateControl");
    if (p_NonAggregateControl)
    {
        p_res->NonAggregateControl = parse_Bool(p_NonAggregateControl);
    }

    p_NoRTSPStreaming = xml_attr_get(p_node, "NoRTSPStreaming");
    if (p_NoRTSPStreaming)
    {
        p_res->NoRTSPStreaming = parse_Bool(p_NoRTSPStreaming);
    }

    return TRUE;
}

BOOL parse_MediaServiceCapabilities(XMLN * p_node, onvif_MediaCapabilities * p_res)
{
    XMLN * p_ProfileCapabilities;
    XMLN * p_StreamingCapabilities;
    const char * p_SnapshotUri;
    const char * p_Rotation;
    const char * p_VideoSourceMode;
    const char * p_OSD;
    const char * p_TemporaryOSDText;
    const char * p_EXICompression;

    p_SnapshotUri = xml_attr_get(p_node, "SnapshotUri");
    if (p_SnapshotUri)
    {
        p_res->SnapshotUri = parse_Bool(p_SnapshotUri);
    }

    p_Rotation = xml_attr_get(p_node, "Rotation");
    if (p_Rotation)
    {
        p_res->Rotation = parse_Bool(p_Rotation);
    }

    p_VideoSourceMode = xml_attr_get(p_node, "VideoSourceMode");
    if (p_VideoSourceMode)
    {
        p_res->VideoSourceMode = parse_Bool(p_VideoSourceMode);
    }

    p_OSD = xml_attr_get(p_node, "OSD");
    if (p_OSD)
    {
        p_res->OSD = parse_Bool(p_OSD);
    }

    p_TemporaryOSDText = xml_attr_get(p_node, "TemporaryOSDText");
    if (p_TemporaryOSDText)
    {
        p_res->TemporaryOSDText = parse_Bool(p_TemporaryOSDText);
    }

    p_EXICompression = xml_attr_get(p_node, "EXICompression");
    if (p_EXICompression)
    {
        p_res->EXICompression = parse_Bool(p_EXICompression);
    }

    p_ProfileCapabilities = xml_node_soap_get(p_node, "ProfileCapabilities");
    if (p_ProfileCapabilities)
    {
        const char * p_MaximumNumberOfProfiles = xml_attr_get(p_ProfileCapabilities, "MaximumNumberOfProfiles");
        if (p_MaximumNumberOfProfiles)
        {
            p_res->MaximumNumberOfProfiles = atoi(p_MaximumNumberOfProfiles);
        }
    }

    p_StreamingCapabilities = xml_node_soap_get(p_node, "StreamingCapabilities");
    if (p_StreamingCapabilities)
    {
        parse_MediaStreamingCapabilities(p_StreamingCapabilities, p_res);
    }

    return TRUE;
}

BOOL parse_MediaService(XMLN * p_node, onvif_MediaCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_MediaServiceCapabilities(p_Capabilities, p_res);
        }
    }   

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_Media2ProfileCapabilities(XMLN * p_node, onvif_ProfileCapabilities * p_res)
{
    const char * p_MaximumNumberOfProfiles;
    const char * p_ConfigurationsSupported;

    p_MaximumNumberOfProfiles = xml_attr_get(p_node, "MaximumNumberOfProfiles");
    if (p_MaximumNumberOfProfiles)
    {
        p_res->MaximumNumberOfProfilesFlag = 1;
        p_res->MaximumNumberOfProfiles = atoi(p_MaximumNumberOfProfiles);
    }

    p_ConfigurationsSupported = xml_attr_get(p_node, "ConfigurationsSupported");
    if (p_ConfigurationsSupported)
    {
        p_res->ConfigurationsSupportedFlag = 1;
        strncpy(p_res->ConfigurationsSupported, p_ConfigurationsSupported, sizeof(p_res->ConfigurationsSupported)-1);
    }

    return TRUE;
}

BOOL parse_Media2StreamingCapabilities(XMLN * p_node, onvif_StreamingCapabilities * p_res)
{
    const char * p_RTPMulticast;
    const char * p_RTP_RTSP_TCP;
    const char * p_NonAggregateControl;
    const char * p_RTSPStreaming;
    const char * p_RTSPWebSocketUri;
    const char * p_AutoStartMulticast;
    const char * p_SecureRTSPStreaming;

    p_RTSPStreaming = xml_attr_get(p_node, "RTSPStreaming");
    if (p_RTSPStreaming)
    {
        p_res->RTSPStreaming = parse_Bool(p_RTSPStreaming);
    }

    p_RTPMulticast = xml_attr_get(p_node, "RTPMulticast");
    if (p_RTPMulticast)
    {
        p_res->RTPMulticast = parse_Bool(p_RTPMulticast);
    }
    
    p_RTP_RTSP_TCP = xml_attr_get(p_node, "RTP_RTSP_TCP");
    if (p_RTP_RTSP_TCP)
    {
        p_res->RTP_RTSP_TCP = parse_Bool(p_RTP_RTSP_TCP);
    }

    p_NonAggregateControl = xml_attr_get(p_node, "NonAggregateControl");
    if (p_NonAggregateControl)
    {
        p_res->NonAggregateControl = parse_Bool(p_NonAggregateControl);
    }

    p_RTSPWebSocketUri = xml_attr_get(p_node, "RTSPWebSocketUri");
    if (p_RTSPWebSocketUri)
    {
        strncpy(p_res->RTSPWebSocketUri, p_RTSPWebSocketUri, sizeof(p_res->RTSPWebSocketUri)-1);
    }

    p_AutoStartMulticast = xml_attr_get(p_node, "AutoStartMulticast");
    if (p_AutoStartMulticast)
    {
        p_res->AutoStartMulticast = parse_Bool(p_AutoStartMulticast);
    }

    p_SecureRTSPStreaming = xml_attr_get(p_node, "SecureRTSPStreaming");
    if (p_SecureRTSPStreaming)
    {
        p_res->SecureRTSPStreaming = parse_Bool(p_SecureRTSPStreaming);
    }

    return TRUE;
}

BOOL parse_Media2MediaSigningCapabilities(XMLN * p_node, onvif_MediaSigningCapabilities * p_res)
{
    const char * p_MediaSigningSupported;

    p_MediaSigningSupported = xml_attr_get(p_node, "MediaSigningSupported");
    if (p_MediaSigningSupported)
    {
        p_res->MediaSigningSupported = parse_Bool(p_MediaSigningSupported);
    }

    return TRUE;
}

BOOL parse_Media2ServiceCapabilities(XMLN * p_node, onvif_MediaCapabilities2 * p_res)
{
    XMLN * p_ProfileCapabilities;
    XMLN * p_StreamingCapabilities;
    XMLN * p_MediaSigningCapabilities;
    const char * p_SnapshotUri;
    const char * p_Rotation;
    const char * p_VideoSourceMode;
    const char * p_OSD;
    const char * p_TemporaryOSDText;
    const char * p_Mask;
    const char * p_SourceMask;

    p_SnapshotUri = xml_attr_get(p_node, "SnapshotUri");
    if (p_SnapshotUri)
    {
        p_res->SnapshotUri = parse_Bool(p_SnapshotUri);
    }

    p_Rotation = xml_attr_get(p_node, "Rotation");
    if (p_Rotation)
    {
        p_res->Rotation = parse_Bool(p_Rotation);
    }

    p_VideoSourceMode = xml_attr_get(p_node, "VideoSourceMode");
    if (p_VideoSourceMode)
    {
        p_res->VideoSourceMode = parse_Bool(p_VideoSourceMode);
    }

    p_OSD = xml_attr_get(p_node, "OSD");
    if (p_OSD)
    {
        p_res->OSD = parse_Bool(p_OSD);
    }

    p_TemporaryOSDText = xml_attr_get(p_node, "TemporaryOSDText");
    if (p_TemporaryOSDText)
    {
        p_res->TemporaryOSDText = parse_Bool(p_TemporaryOSDText);
    }

    p_Mask = xml_attr_get(p_node, "Mask");
    if (p_Mask)
    {
        p_res->Mask = parse_Bool(p_Mask);
    }

    p_SourceMask = xml_attr_get(p_node, "SourceMask");
    if (p_SourceMask)
    {
        p_res->SourceMask = parse_Bool(p_SourceMask);
    }

    p_ProfileCapabilities = xml_node_soap_get(p_node, "ProfileCapabilities");
    if (p_ProfileCapabilities)
    {
        parse_Media2ProfileCapabilities(p_ProfileCapabilities, &p_res->ProfileCapabilities);
    }

    p_StreamingCapabilities = xml_node_soap_get(p_node, "StreamingCapabilities");
    if (p_StreamingCapabilities)
    {
        parse_Media2StreamingCapabilities(p_StreamingCapabilities, &p_res->StreamingCapabilities);
    }

    p_MediaSigningCapabilities = xml_node_soap_get(p_node, "MediaSigningCapabilities");
    if (p_MediaSigningCapabilities)
    {
        parse_Media2MediaSigningCapabilities(p_MediaSigningCapabilities, &p_res->MediaSigningCapabilities);
    }

    return TRUE;
}

BOOL parse_Media2Service(XMLN * p_node, onvif_MediaCapabilities2 * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_Media2ServiceCapabilities(p_Capabilities, p_res);
        }
    }   

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_EventServiceCapabilities(XMLN * p_node, onvif_EventCapabilities * p_res)
{
    const char * p_WSSubscriptionPolicySupport;
    const char * p_WSPullPointSupport;
    const char * p_WSPausableSubscriptionManagerInterfaceSupport;
    const char * p_MaxNotificationProducers;
    const char * p_MaxPullPoints;
    const char * p_PersistentNotificationStorage;
    const char * p_EventBrokerProtocols;
    const char * p_MaxEventBrokers;

    p_WSSubscriptionPolicySupport = xml_attr_get(p_node, "WSSubscriptionPolicySupport");
    if (p_WSSubscriptionPolicySupport)
    {
        p_res->WSSubscriptionPolicySupport = parse_Bool(p_WSSubscriptionPolicySupport);
    }

    p_WSPullPointSupport = xml_attr_get(p_node, "WSPullPointSupport");
    if (p_WSPullPointSupport)
    {
        p_res->WSPullPointSupport = parse_Bool(p_WSPullPointSupport);
    }

    p_WSPausableSubscriptionManagerInterfaceSupport = xml_attr_get(p_node, "WSPausableSubscriptionManagerInterfaceSupport");
    if (p_WSPausableSubscriptionManagerInterfaceSupport)
    {
        p_res->WSPausableSubscriptionManagerInterfaceSupport = parse_Bool(p_WSPausableSubscriptionManagerInterfaceSupport);
    }

    p_MaxNotificationProducers = xml_attr_get(p_node, "MaxNotificationProducers");
    if (p_MaxNotificationProducers)
    {
        p_res->MaxNotificationProducers = atoi(p_MaxNotificationProducers);
    }

    p_MaxPullPoints = xml_attr_get(p_node, "MaxPullPoints");
    if (p_MaxPullPoints)
    {
        p_res->MaxPullPoints = atoi(p_MaxPullPoints);
    }

    p_PersistentNotificationStorage = xml_attr_get(p_node, "PersistentNotificationStorage");
    if (p_PersistentNotificationStorage)
    {
        p_res->PersistentNotificationStorage = parse_Bool(p_PersistentNotificationStorage);
    }

    p_EventBrokerProtocols = xml_attr_get(p_node, "EventBrokerProtocols");
    if (p_EventBrokerProtocols)
    {
        strncpy(p_res->EventBrokerProtocols, p_EventBrokerProtocols, sizeof(p_res->EventBrokerProtocols)-1);
    }

    p_MaxEventBrokers = xml_attr_get(p_node, "MaxEventBrokers");
    if (p_MaxEventBrokers)
    {
        p_res->MaxEventBrokers = atoi(p_MaxEventBrokers);
    }
    
    return TRUE;
}

BOOL parse_EventsService(XMLN * p_node, onvif_EventCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_EventServiceCapabilities(p_Capabilities, p_res);
        }
    } 

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_PTZServiceCapabilities(XMLN * p_node, onvif_PTZCapabilities * p_res)
{
    const char * p_EFlip;
    const char * p_Reverse;
    const char * p_GetCompatibleConfigurations;
    const char * p_MoveStatus;
    const char * p_StatusPosition;
    const char * p_MoveAndTrack;

    p_EFlip = xml_attr_get(p_node, "EFlip");
    if (p_EFlip)
    {
        p_res->EFlip = parse_Bool(p_EFlip);
    }

    p_Reverse = xml_attr_get(p_node, "Reverse");
    if (p_Reverse)
    {
        p_res->Reverse = parse_Bool(p_Reverse);
    }

    p_GetCompatibleConfigurations = xml_attr_get(p_node, "GetCompatibleConfigurations");
    if (p_GetCompatibleConfigurations)
    {
        p_res->GetCompatibleConfigurations = parse_Bool(p_GetCompatibleConfigurations);
    }

    p_MoveStatus = xml_attr_get(p_node, "MoveStatus");
    if (p_MoveStatus)
    {
        p_res->MoveStatus = parse_Bool(p_MoveStatus);
    }

    p_StatusPosition = xml_attr_get(p_node, "StatusPosition");
    if (p_StatusPosition)
    {
        p_res->StatusPosition = parse_Bool(p_StatusPosition);
    }

    p_MoveAndTrack = xml_attr_get(p_node, "MoveAndTrack");
    if (p_MoveAndTrack)
    {
        strncpy(p_res->MoveAndTrack, p_MoveAndTrack, sizeof(p_res->MoveAndTrack)-1);
    }

    return TRUE;
}

BOOL parse_PTZService(XMLN * p_node, onvif_PTZCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_PTZServiceCapabilities(p_Capabilities, p_res);
        }
    } 

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_ImagingServiceCapabilities(XMLN * p_node, onvif_ImagingCapabilities * p_res)
{
    const char * p_ImageStabilization;
    const char * p_Presets;
    const char * p_AdaptablePreset;

    p_ImageStabilization = xml_attr_get(p_node, "ImageStabilization");
    if (p_ImageStabilization)
    {
        p_res->ImageStabilization = parse_Bool(p_ImageStabilization);
    }

    p_Presets = xml_attr_get(p_node, "Presets");
    if (p_Presets)
    {
        p_res->Presets = parse_Bool(p_Presets);
    }

    p_AdaptablePreset = xml_attr_get(p_node, "AdaptablePreset");
    if (p_AdaptablePreset)
    {
        p_res->AdaptablePreset = parse_Bool(p_AdaptablePreset);
    }

    return TRUE;
}

BOOL parse_ImagingService(XMLN * p_node, onvif_ImagingCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ImagingServiceCapabilities(p_Capabilities, p_res);
        }
    } 

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_AnalyticsServiceCapabilities(XMLN * p_node, onvif_AnalyticsCapabilities * p_res)
{
    const char * p_RuleSupport;
    const char * p_AnalyticsModuleSupport;
    const char * p_CellBasedSceneDescriptionSupported;
    const char * p_RuleOptionsSupported;
    const char * p_AnalyticsModuleOptionsSupported;
    const char * p_SupportedMetadata;
    const char * p_ImageSendingType;

    p_RuleSupport = xml_attr_get(p_node, "RuleSupport");
    if (p_RuleSupport)
    {
        p_res->RuleSupport = parse_Bool(p_RuleSupport);
    }

    p_AnalyticsModuleSupport = xml_attr_get(p_node, "AnalyticsModuleSupport");
    if (p_AnalyticsModuleSupport)
    {
        p_res->AnalyticsModuleSupport = parse_Bool(p_AnalyticsModuleSupport);
    }

    p_CellBasedSceneDescriptionSupported = xml_attr_get(p_node, "CellBasedSceneDescriptionSupported");
    if (p_CellBasedSceneDescriptionSupported)
    {
        p_res->CellBasedSceneDescriptionSupported = parse_Bool(p_CellBasedSceneDescriptionSupported);
    }

    p_RuleOptionsSupported = xml_attr_get(p_node, "RuleOptionsSupported");
    if (p_RuleOptionsSupported)
    {
        p_res->RuleOptionsSupported = parse_Bool(p_RuleOptionsSupported);
    }

    p_AnalyticsModuleOptionsSupported = xml_attr_get(p_node, "AnalyticsModuleOptionsSupported");
    if (p_AnalyticsModuleOptionsSupported)
    {
        p_res->AnalyticsModuleOptionsSupported = parse_Bool(p_AnalyticsModuleOptionsSupported);
    }

    p_SupportedMetadata = xml_attr_get(p_node, "SupportedMetadata");
    if (p_SupportedMetadata)
    {
        p_res->SupportedMetadata = parse_Bool(p_SupportedMetadata);
    }

    p_ImageSendingType = xml_attr_get(p_node, "ImageSendingType");
    if (p_ImageSendingType)
    {
        strncpy(p_res->ImageSendingType, p_ImageSendingType, sizeof(p_res->ImageSendingType)-1);
    }

    return TRUE;
}

BOOL parse_AnalyticsService(XMLN * p_node, onvif_AnalyticsCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_AnalyticsServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_IntRectangle(XMLN * p_node, onvif_IntRectangle * p_res)
{
    const char * p_height;
    const char * p_width;
    const char * p_x;
    const char * p_y;

    p_height = xml_attr_get(p_node, "height");
    if (p_height)
    {
        p_res->height = atoi(p_height);
    }

    p_width = xml_attr_get(p_node, "width");
    if (p_width)
    {
        p_res->width = atoi(p_width);
    }

    p_x = xml_attr_get(p_node, "x");
    if (p_x)
    {
        p_res->x = atoi(p_x);
    }

    p_y = xml_attr_get(p_node, "y");
    if (p_y)
    {
        p_res->y = atoi(p_y);
    }

    return TRUE;
}

BOOL parse_Rotate(XMLN * p_node, onvif_Rotate * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Degree;

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToRotateMode(p_Mode->data);
    }

    p_Degree = xml_node_soap_get(p_node, "Degree");
    if (p_Degree && p_Degree->data)
    {
        p_res->DegreeFlag = 1;
        p_res->Degree = atoi(p_Degree->data);
    }

    return TRUE;
}

BOOL parse_VideoSourceConfigurationExtension(XMLN * p_node, onvif_VideoSourceConfigurationExtension * p_res)
{
    XMLN * p_Rotate;

    p_Rotate = xml_node_soap_get(p_node, "Rotate");
    if (p_Rotate)
    {
        p_res->RotateFlag = parse_Rotate(p_Rotate, &p_res->Rotate);
    }

    return TRUE;
}

BOOL parse_VideoSourceConfiguration(XMLN * p_node, onvif_VideoSourceConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_SourceToken;
    XMLN * p_Bounds;
    XMLN * p_Extension;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
            
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_SourceToken = xml_node_soap_get(p_node, "SourceToken");
    if (p_SourceToken && p_SourceToken->data)
    {
        strncpy(p_res->SourceToken, p_SourceToken->data, sizeof(p_res->SourceToken)-1);
    }

    p_Bounds = xml_node_soap_get(p_node, "Bounds");
    if (p_Bounds)
    {
        parse_IntRectangle(p_Bounds, &p_res->Bounds);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        p_res->ExtensionFlag = parse_VideoSourceConfigurationExtension(p_Extension, &p_res->Extension);
    }
    
    return TRUE;
}

BOOL parse_AudioSourceConfiguration(XMLN * p_node, onvif_AudioSourceConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_SourceToken;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");  
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
            
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_SourceToken = xml_node_soap_get(p_node, "SourceToken");
    if (p_SourceToken && p_SourceToken->data)
    {
        strncpy(p_res->SourceToken, p_SourceToken->data, sizeof(p_res->SourceToken)-1);
    }
    
    return TRUE;
}

BOOL parse_MulticastConfiguration(XMLN * p_node, onvif_MulticastConfiguration * p_res)
{
    XMLN * p_Address;
    XMLN * p_Port;
    XMLN * p_TTL;
    
    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address)
    {
        XMLN * p_IPv4Address;

        p_IPv4Address = xml_node_soap_get(p_Address, "IPv4Address");
        if (p_IPv4Address && p_IPv4Address->data)
        {
            strncpy(p_res->IPv4Address, p_IPv4Address->data, sizeof(p_res->IPv4Address)-1);
        }
    }

    p_Port = xml_node_soap_get(p_node, "Port");
    if (p_Port && p_Port->data)
    {
        p_res->Port = atoi(p_Port->data);
    }

    p_TTL = xml_node_soap_get(p_node, "TTL");
    if (p_TTL && p_TTL->data)
    {
        p_res->TTL = atoi(p_TTL->data);
    }

    return TRUE;
}

BOOL parse_VideoResolution(XMLN * p_node, onvif_VideoResolution * p_res)
{
    XMLN * p_Width;
    XMLN * p_Height;

    p_Width = xml_node_soap_get(p_node, "Width");
    if (p_Width && p_Width->data)
    {
        p_res->Width = atoi(p_Width->data);
    }

    p_Height = xml_node_soap_get(p_node, "Height");
    if (p_Height && p_Height->data)
    {
        p_res->Height = atoi(p_Height->data);
    }

    return TRUE;
}

BOOL parse_BacklightCompensation(XMLN * p_node, onvif_BacklightCompensation * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Level;

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToBacklightCompensationMode(p_Mode->data);
    }

    p_Level = xml_node_soap_get(p_node, "Level");
    if (p_Level && p_Level->data)
    {
        p_res->LevelFlag = 1;
        p_res->Level = (float)atof(p_Level->data);
    }

    return TRUE;
}

BOOL parse_Rectangle(XMLN * p_node, onvif_Rectangle * p_res)
{
    const char * p_bottom;
    const char * p_top;
    const char * p_right;
    const char * p_left;

    p_bottom = xml_attr_get(p_node, "bottom");
    if (p_bottom)
    {
        p_res->bottom = (float)atof(p_bottom);
    }

    p_top = xml_attr_get(p_node, "top");
    if (p_top)
    {
        p_res->top = (float)atof(p_top);
    }

    p_right = xml_attr_get(p_node, "right");
    if (p_right)
    {
        p_res->right = (float)atof(p_right);
    }

    p_left = xml_attr_get(p_node, "left");
    if (p_left)
    {
        p_res->left = (float)atof(p_left);
    }

    return TRUE;
}

BOOL parse_Exposure(XMLN * p_node, onvif_Exposure * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Priority;
    XMLN * p_Window;
    XMLN * p_MinExposureTime;
    XMLN * p_MaxExposureTime;
    XMLN * p_MinGain;
    XMLN * p_MaxGain;
    XMLN * p_MinIris;
    XMLN * p_MaxIris;
    XMLN * p_ExposureTime;
    XMLN * p_Gain;
    XMLN * p_Iris;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToExposureMode(p_Mode->data);
    }

    p_Window = xml_node_soap_get(p_node, "Window");
    if (p_Window)
    {
        p_res->WindowFlag = parse_Rectangle(p_Window, &p_res->Window);
    }

    p_Priority = xml_node_soap_get(p_node, "Priority");
    if (p_Priority && p_Priority->data)
    {
        p_res->PriorityFlag = 1;
        p_res->Priority = onvif_StringToExposurePriority(p_Priority->data);
    }
    
    p_MinExposureTime = xml_node_soap_get(p_node, "MinExposureTime");
    if (p_MinExposureTime && p_MinExposureTime->data)
    {
        p_res->MinExposureTimeFlag = 1;
        p_res->MinExposureTime = (float)atof(p_MinExposureTime->data);
    }

    p_MaxExposureTime = xml_node_soap_get(p_node, "MaxExposureTime");
    if (p_MaxExposureTime && p_MaxExposureTime->data)
    {
        p_res->MaxExposureTimeFlag = 1;
        p_res->MaxExposureTime = (float)atof(p_MaxExposureTime->data);
    }

    p_MinGain = xml_node_soap_get(p_node, "MinGain");
    if (p_MinGain && p_MinGain->data)
    {
        p_res->MinGainFlag = 1;
        p_res->MinGain = (float)atof(p_MinGain->data);
    }

    p_MaxGain = xml_node_soap_get(p_node, "MaxGain");
    if (p_MaxGain && p_MaxGain->data)
    {
        p_res->MaxGainFlag = 1;
        p_res->MaxGain = (float)atof(p_MaxGain->data);
    }

    p_MinIris = xml_node_soap_get(p_node, "MinIris");
    if (p_MinIris && p_MinIris->data)
    {
        p_res->MinIrisFlag = 1;
        p_res->MinIris = (float)atof(p_MinIris->data);
    }

    p_MaxIris = xml_node_soap_get(p_node, "MaxIris");
    if (p_MaxIris && p_MaxIris->data)
    {
        p_res->MaxIrisFlag = 1;
        p_res->MaxIris = (float)atof(p_MaxIris->data);
    }

    p_ExposureTime = xml_node_soap_get(p_node, "ExposureTime");
    if (p_ExposureTime && p_ExposureTime->data)
    {
        p_res->ExposureTimeFlag = 1;
        p_res->ExposureTime = (float)atof(p_ExposureTime->data);
    }

    p_Gain = xml_node_soap_get(p_node, "Gain");
    if (p_Gain && p_Gain->data)
    {
        p_res->GainFlag = 1;
        p_res->Gain = (float)atof(p_Gain->data);
    }

    p_Iris = xml_node_soap_get(p_node, "Iris");
    if (p_Iris && p_Iris->data)
    {
        p_res->IrisFlag = 1;
        p_res->Iris = (float)atof(p_Iris->data);
    }

    return TRUE;
}

BOOL parse_FocusConfiguration(XMLN * p_node, onvif_FocusConfiguration * p_res)
{
    XMLN * p_AutoFocusMode;
    XMLN * p_DefaultSpeed;
    XMLN * p_NearLimit;
    XMLN * p_FarLimit;
    
    p_AutoFocusMode = xml_node_soap_get(p_node, "AutoFocusMode");
    if (p_AutoFocusMode && p_AutoFocusMode->data)
    {
        p_res->AutoFocusMode = onvif_StringToAutoFocusMode(p_AutoFocusMode->data);
    }

    p_DefaultSpeed = xml_node_soap_get(p_node, "DefaultSpeed");
    if (p_DefaultSpeed && p_DefaultSpeed->data)
    {
        p_res->DefaultSpeedFlag = 1;
        p_res->DefaultSpeed = (float)atof(p_DefaultSpeed->data);
    }

    p_NearLimit = xml_node_soap_get(p_node, "NearLimit");
    if (p_NearLimit && p_NearLimit->data)
    {
        p_res->NearLimitFlag = 1;
        p_res->NearLimit = (float)atof(p_NearLimit->data);
    }

    p_FarLimit = xml_node_soap_get(p_node, "FarLimit");
    if (p_FarLimit && p_FarLimit->data)
    {
        p_res->FarLimitFlag = 1;
        p_res->FarLimit = (float)atof(p_FarLimit->data);
    }

    return TRUE;
}

BOOL parse_WideDynamicRange(XMLN * p_node, onvif_WideDynamicRange * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Level;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToWideDynamicMode(p_Mode->data);
    }

    p_Level = xml_node_soap_get(p_node, "Level");
    if (p_Level && p_Level->data)
    {
        p_res->LevelFlag = 1;
        p_res->Level = (float)atof(p_Level->data);
    }

    return TRUE;
}

BOOL parse_WhiteBalance(XMLN * p_node, onvif_WhiteBalance * p_res)
{
    XMLN * p_Mode;
    XMLN * p_CrGain;
    XMLN * p_CbGain;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToWhiteBalanceMode(p_Mode->data);
    }

    p_CrGain = xml_node_soap_get(p_node, "CrGain");
    if (p_CrGain && p_CrGain->data)
    {
        p_res->CrGainFlag = 1;
        p_res->CrGain = (float)atof(p_CrGain->data);
    }

    p_CbGain = xml_node_soap_get(p_node, "CbGain");
    if (p_CbGain && p_CbGain->data)
    {
        p_res->CbGainFlag = 1;
        p_res->CbGain = (float)atof(p_CbGain->data);
    }

    return TRUE;
}

BOOL parse_ImagingSettings(XMLN * p_node, onvif_ImagingSettings * p_res)
{
    XMLN * p_BacklightCompensation;
    XMLN * p_Brightness;
    XMLN * p_ColorSaturation;
    XMLN * p_Contrast;
    XMLN * p_Exposure;
    XMLN * p_Focus;
    XMLN * p_IrCutFilter;
    XMLN * p_Sharpness;
    XMLN * p_WideDynamicRange;
    XMLN * p_WhiteBalance;
    
    p_BacklightCompensation = xml_node_soap_get(p_node, "BacklightCompensation");
    if (p_BacklightCompensation)
    {
        p_res->BacklightCompensationFlag = parse_BacklightCompensation(p_BacklightCompensation, &p_res->BacklightCompensation);
    }

    p_Brightness = xml_node_soap_get(p_node, "Brightness");
    if (p_Brightness && p_Brightness->data)
    {
        p_res->BrightnessFlag = 1;
        p_res->Brightness = (float)atof(p_Brightness->data);
    }

    p_ColorSaturation = xml_node_soap_get(p_node, "ColorSaturation");
    if (p_ColorSaturation && p_ColorSaturation->data)
    {
        p_res->ColorSaturationFlag = 1;
        p_res->ColorSaturation = (float)atof(p_ColorSaturation->data);
    }

    p_Contrast = xml_node_soap_get(p_node, "Contrast");
    if (p_Contrast && p_Contrast->data)
    {
        p_res->ContrastFlag = 1;
        p_res->Contrast = (float)atof(p_Contrast->data);
    }

    p_Exposure = xml_node_soap_get(p_node, "Exposure");
    if (p_Exposure)
    {
        p_res->ExposureFlag = parse_Exposure(p_Exposure, &p_res->Exposure);
    }

    p_Focus = xml_node_soap_get(p_node, "Focus");
    if (p_Focus)
    {
        p_res->FocusFlag = parse_FocusConfiguration(p_Focus, &p_res->Focus);
    }
    
    p_IrCutFilter = xml_node_soap_get(p_node, "IrCutFilter");
    if (p_IrCutFilter && p_IrCutFilter->data)
    {
        p_res->IrCutFilterFlag = 1;
        p_res->IrCutFilter = onvif_StringToIrCutFilterMode(p_IrCutFilter->data);
    }

    p_Sharpness = xml_node_soap_get(p_node, "Sharpness");
    if (p_Sharpness && p_Sharpness->data)
    {
        p_res->SharpnessFlag = 1;
        p_res->Sharpness = (float)atof(p_Sharpness->data);
    }

    p_WideDynamicRange = xml_node_soap_get(p_node, "WideDynamicRange");
    if (p_WideDynamicRange)
    {
        p_res->WideDynamicRangeFlag = parse_WideDynamicRange(p_WideDynamicRange, &p_res->WideDynamicRange);
    }

    p_WhiteBalance = xml_node_soap_get(p_node, "WhiteBalance");
    if (p_WhiteBalance)
    {
        p_res->WhiteBalanceFlag = parse_WhiteBalance(p_WhiteBalance, &p_res->WhiteBalance);
    }
    
    return TRUE;
}

BOOL parse_VideoSource(XMLN * p_node, onvif_VideoSource * p_res)
{
    XMLN * p_Framerate;
    XMLN * p_Resolution;
    XMLN * p_Imaging;

    p_Framerate = xml_node_soap_get(p_node, "Framerate");
    if (p_Framerate && p_Framerate->data)
    {
        p_res->Framerate = (float) atof(p_Framerate->data);
    }

    p_Resolution = xml_node_soap_get(p_node, "Resolution");
    if (p_Resolution)
    {
        parse_VideoResolution(p_Resolution, &p_res->Resolution);
    }

    p_Imaging = xml_node_soap_get(p_node, "Imaging");
    if (p_Imaging)
    {
        p_res->ImagingSettingsFlag = parse_ImagingSettings(p_Imaging, &p_res->ImagingSettings);
    }
    
    return TRUE;
}

BOOL parse_AudioSource(XMLN * p_node, onvif_AudioSource * p_res)
{
    XMLN * p_Channels = xml_node_soap_get(p_node, "Channels");
    if (p_Channels && p_Channels->data)
    {
        p_res->Channels = atoi(p_Channels->data);
    }
    
    return TRUE;
}

BOOL parse_SimpleItem(XMLN * p_node, onvif_SimpleItem * p_res)
{
    const char * p_Name;
    const char * p_Value;

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_Value = xml_attr_get(p_node, "Value");
    if (p_Value)
    {
        strncpy(p_res->Value, p_Value, sizeof(p_res->Value)-1);
    }

    return TRUE;
}

BOOL parse_ElementItem(XMLN * p_node, onvif_ElementItem * p_res)
{
    const char * p_Name;

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);

        if (p_node->f_child)
        {
            int len = xml_calc_buf_len(p_node->f_child);
            
            p_res->AnyFlag = 1;
            p_res->Any = (char *) malloc(len+8);
            if (p_res->Any)
            {
                memset(p_res->Any, 0, len + 8);
                xml_write_buf(p_node->f_child, p_res->Any, len);
            }
        }
    }

    return TRUE;
}

BOOL parse_ItemList(XMLN * p_node, onvif_ItemList * p_res)
{
    XMLN * p_SimpleItem;
    XMLN * p_ElementItem;

    p_SimpleItem = xml_node_soap_get(p_node, "SimpleItem");
    while (p_SimpleItem && soap_strcmp(p_SimpleItem->name, "SimpleItem") == 0)
    {
        SimpleItemList * p_item = onvif_add_SimpleItem(&p_res->SimpleItem);
        if (p_item)
        {
            if (!parse_SimpleItem(p_SimpleItem, &p_item->SimpleItem))
            {
                onvif_free_SimpleItems(&p_res->SimpleItem);
                break;
            }
        }
        
        p_SimpleItem = p_SimpleItem->next;
    }

    p_ElementItem = xml_node_soap_get(p_node, "ElementItem");
    while (p_ElementItem && soap_strcmp(p_ElementItem->name, "ElementItem") == 0)
    {
        ElementItemList * p_item = onvif_add_ElementItem(&p_res->ElementItem);
        if (p_item)
        {
            if (!parse_ElementItem(p_ElementItem, &p_item->ElementItem))
            {
                onvif_free_ElementItems(&p_res->ElementItem);
                break;
            }
        }
        
        p_ElementItem = p_ElementItem->next;
    }

    return TRUE;
}

BOOL parse_Config(XMLN * p_node, onvif_Config * p_res)
{
    XMLN * p_Parameters;    
    const char * p_Name;
    const char * p_Type;

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        if (p_node->l_attrib && soap_strcmp(p_node->l_attrib->name, "Type"))
        {
            strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
            
            p_res->attrFlag = 1;
            snprintf(p_res->attr, sizeof(p_res->attr)-1, 
                "%s=\"%s\"", 
                p_node->l_attrib->name, 
                p_node->l_attrib->data);
        }
        else
        {
            strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
        }
    }

    p_Parameters = xml_node_soap_get(p_node, "Parameters");
    if (p_Parameters)
    {
        parse_ItemList(p_Parameters, &p_res->Parameters);
    }

    return TRUE;
}

BOOL parse_VideoRateControl(XMLN * p_node, onvif_VideoRateControl * p_res)
{
    XMLN * p_FrameRateLimit;
    XMLN * p_EncodingInterval;
    XMLN * p_BitrateLimit;
    const char * p_ConstantBitRate;

    p_ConstantBitRate = xml_attr_get(p_node, "ConstantBitRate");
    if (p_ConstantBitRate)
    {
        p_res->ConstantBitRateFlag = 1;
        p_res->ConstantBitRate = parse_Bool(p_ConstantBitRate);
    }
    
    p_FrameRateLimit = xml_node_soap_get(p_node, "FrameRateLimit");
    if (p_FrameRateLimit && p_FrameRateLimit->data)
    {
        p_res->FrameRateLimit = (int)atof(p_FrameRateLimit->data);
    }

    p_EncodingInterval = xml_node_soap_get(p_node, "EncodingInterval");
    if (p_EncodingInterval && p_EncodingInterval->data)
    {
        p_res->EncodingInterval = atoi(p_EncodingInterval->data);
    }

    p_BitrateLimit = xml_node_soap_get(p_node, "BitrateLimit");
    if (p_BitrateLimit && p_BitrateLimit->data)
    {
        p_res->BitrateLimit = atoi(p_BitrateLimit->data);
    }

    return TRUE;
}

BOOL parse_H264Configuration(XMLN * p_node, onvif_H264Configuration * p_res)
{
    XMLN * p_GovLength;
    XMLN * p_H264Profile;
    
    p_GovLength = xml_node_soap_get(p_node, "GovLength");
    if (p_GovLength && p_GovLength->data)
    {
        p_res->GovLength = atoi(p_GovLength->data);
    }

    p_H264Profile = xml_node_soap_get(p_node, "H264Profile");
    if (p_H264Profile && p_H264Profile->data)
    {
        p_res->H264Profile = onvif_StringToH264Profile(p_H264Profile->data);
    }

    return TRUE;
}

BOOL parse_Mpeg4Configuration(XMLN * p_node, onvif_Mpeg4Configuration * p_res)
{
    XMLN * p_GovLength;
    XMLN * p_Mpeg4Profile;
    
    p_GovLength = xml_node_soap_get(p_node, "GovLength");
    if (p_GovLength && p_GovLength->data)
    {
        p_res->GovLength = atoi(p_GovLength->data);
    }

    p_Mpeg4Profile = xml_node_soap_get(p_node, "Mpeg4Profile");
    if (p_Mpeg4Profile && p_Mpeg4Profile->data)
    {
        p_res->Mpeg4Profile = onvif_StringToMpeg4Profile(p_Mpeg4Profile->data);
    }

    return TRUE;
}

BOOL parse_VideoEncoderConfiguration(XMLN * p_node, onvif_VideoEncoderConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_Encoding;
    XMLN * p_Resolution;
    XMLN * p_Quality;
    XMLN * p_RateControl;
    XMLN * p_Multicast;
    XMLN * p_SessionTimeout;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
            
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        p_res->Encoding = onvif_StringToVideoEncoding(p_Encoding->data);
    }

    p_Resolution = xml_node_soap_get(p_node, "Resolution");
    if (p_Resolution)
    {
        parse_VideoResolution(p_Resolution, &p_res->Resolution);
    }

    p_Quality = xml_node_soap_get(p_node, "Quality");
    if (p_Quality && p_Quality->data)
    {
        p_res->Quality = atoi(p_Quality->data);
    }

    p_RateControl = xml_node_soap_get(p_node, "RateControl");
    if (p_RateControl)
    {
        p_res->RateControlFlag = parse_VideoRateControl(p_RateControl, &p_res->RateControl);
    }

    if (VideoEncoding_H264 == p_res->Encoding)
    {
        XMLN * p_H264 = xml_node_soap_get(p_node, "H264");
        if (p_H264)
        {
            p_res->H264Flag = parse_H264Configuration(p_H264, &p_res->H264);
        }    
    }
    else if (VideoEncoding_MPEG4 == p_res->Encoding)
    {
        XMLN * p_MPEG4 = xml_node_soap_get(p_node, "MPEG4");
        if (p_MPEG4)
        {
            p_res->MPEG4Flag = parse_Mpeg4Configuration(p_MPEG4, &p_res->MPEG4);
        }
    }

    p_Multicast = xml_node_soap_get(p_node, "Multicast");
    if (p_Multicast)
    {
        parse_MulticastConfiguration(p_Multicast, &p_res->Multicast);
    }
    
    p_SessionTimeout = xml_node_soap_get(p_node, "SessionTimeout");
    if (p_SessionTimeout && p_SessionTimeout->data)
    {
        parse_XSDDuration(p_SessionTimeout->data, &p_res->SessionTimeout);
    }
                    
    return TRUE;
}

BOOL parse_AudioEncoderConfiguration(XMLN * p_node, onvif_AudioEncoderConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_Encoding;
    XMLN * p_Bitrate;
    XMLN * p_SampleRate;
    XMLN * p_Multicast;
    XMLN * p_SessionTimeout;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
            
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        p_res->Encoding = onvif_StringToAudioEncoding(p_Encoding->data);
    }

       p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate && p_Bitrate->data)
    {
        p_res->Bitrate = atoi(p_Bitrate->data);
    }

    p_SampleRate = xml_node_soap_get(p_node, "SampleRate");
    if (p_SampleRate && p_SampleRate->data)
    {
        p_res->SampleRate = atoi(p_SampleRate->data);
    }

    p_Multicast = xml_node_soap_get(p_node, "Multicast");
    if (p_Multicast)
    {
        parse_MulticastConfiguration(p_Multicast, &p_res->Multicast);
    }
    
    p_SessionTimeout = xml_node_soap_get(p_node, "SessionTimeout");
    if (p_SessionTimeout && p_SessionTimeout->data)
    {
        parse_XSDDuration(p_SessionTimeout->data, &p_res->SessionTimeout);
    }
                    
    return TRUE;
}

BOOL parse_PanTiltLimits(XMLN * p_node, onvif_PanTiltLimits * p_res)
{
    XMLN * p_Range;
        
    p_Range = xml_node_soap_get(p_node, "Range");
    if (p_Range)
    {
        XMLN * p_XRange;
        XMLN * p_YRange;

        p_XRange = xml_node_soap_get(p_Range, "XRange");
        if (p_XRange)
        {
            parse_FloatRange(p_XRange, &p_res->XRange);
        }
        
        p_YRange = xml_node_soap_get(p_Range, "YRange");
        if (p_YRange)
        {
            parse_FloatRange(p_YRange, &p_res->YRange);
        }
    }

    return TRUE;
}

BOOL parse_ZoomLimits(XMLN * p_node, onvif_ZoomLimits * p_res)
{
    XMLN * p_Range;
        
    p_Range = xml_node_soap_get(p_node, "Range");
    if (p_Range)
    {
        XMLN * p_XRange = xml_node_soap_get(p_Range, "XRange");
        if (p_XRange)
        {
            parse_FloatRange(p_XRange, &p_res->XRange);
        }
    }

    return TRUE;
}

BOOL parse_PTZConfiguration(XMLN * p_node, onvif_PTZConfiguration * p_res)
{
    const char * p_token;
    const char * p_MoveRamp;
    const char * p_PresetRamp;
    const char * p_PresetTourRamp;
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_NodeToken;
    XMLN * p_DefaultPTZSpeed;
    XMLN * p_DefaultPTZTimeout;
    XMLN * p_PanTiltLimits;
    XMLN * p_ZoomLimits;
    
    p_token = xml_attr_get(p_node, "token");    
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_MoveRamp = xml_attr_get(p_node, "MoveRamp");
    if (p_MoveRamp)
    {
        p_res->MoveRampFlag = 1;
        p_res->MoveRamp = atoi(p_MoveRamp);
    }

    p_PresetRamp = xml_attr_get(p_node, "PresetRamp");
    if (p_PresetRamp)
    {
        p_res->PresetRampFlag = 1;
        p_res->PresetRamp = atoi(p_PresetRamp);
    }

    p_PresetTourRamp = xml_attr_get(p_node, "PresetTourRamp");
    if (p_PresetTourRamp)
    {
        p_res->PresetTourRampFlag = 1;
        p_res->PresetTourRamp = atoi(p_PresetTourRamp);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_NodeToken = xml_node_soap_get(p_node, "NodeToken");
    if (p_NodeToken && p_NodeToken->data)
    {
        strncpy(p_res->NodeToken, p_NodeToken->data, sizeof(p_res->NodeToken)-1);
    }

    p_DefaultPTZSpeed = xml_node_soap_get(p_node, "DefaultPTZSpeed");
    if (p_DefaultPTZSpeed)
    {
        p_res->DefaultPTZSpeedFlag = parse_PTZSpeed(p_DefaultPTZSpeed, &p_res->DefaultPTZSpeed);
    }

    p_DefaultPTZTimeout = xml_node_soap_get(p_node, "DefaultPTZTimeout");
    if (p_DefaultPTZTimeout && p_DefaultPTZTimeout->data)
    {
        p_res->DefaultPTZTimeoutFlag = 1;
        parse_XSDDuration(p_DefaultPTZTimeout->data, &p_res->DefaultPTZTimeout);
    }

    p_PanTiltLimits = xml_node_soap_get(p_node, "PanTiltLimits");
    if (p_PanTiltLimits)
    {
        p_res->PanTiltLimitsFlag = parse_PanTiltLimits(p_PanTiltLimits, &p_res->PanTiltLimits);
    }
    
    p_ZoomLimits = xml_node_soap_get(p_node, "ZoomLimits");
    if (p_ZoomLimits)
    {
        p_res->ZoomLimitsFlag = parse_ZoomLimits(p_ZoomLimits, &p_res->ZoomLimits);
    }
                
    return TRUE;
}

BOOL parse_AnalyticsEngineConfiguration(XMLN * p_node, onvif_AnalyticsEngineConfiguration * p_res)
{
    XMLN * p_AnalyticsModule;
    ConfigList * p_config;

    p_AnalyticsModule = xml_node_soap_get(p_node, "AnalyticsModule");
    while (p_AnalyticsModule && soap_strcmp(p_AnalyticsModule->name, "AnalyticsModule") == 0)
    {
        p_config = onvif_add_Config(&p_res->AnalyticsModule);
        if (p_config)
        {
            parse_Config(p_AnalyticsModule, &p_config->Config);
        }
        
        p_AnalyticsModule = p_AnalyticsModule->next;
    }

    return TRUE;
}

BOOL parse_RuleEngineConfiguration(XMLN * p_node, onvif_RuleEngineConfiguration * p_res)
{
    XMLN * p_Rule;
    ConfigList * p_config;

    p_Rule = xml_node_soap_get(p_node, "Rule");
    while (p_Rule && soap_strcmp(p_Rule->name, "Rule") == 0)
    {
        p_config = onvif_add_Config(&p_res->Rule);
        if (p_config)
        {
            parse_Config(p_Rule, &p_config->Config);
        }
        
        p_Rule = p_Rule->next;
    }

    return TRUE;
}

BOOL parse_VideoAnalyticsConfiguration(XMLN * p_node, onvif_VideoAnalyticsConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_AnalyticsEngineConfiguration;
    XMLN * p_RuleEngineConfiguration;    
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_AnalyticsEngineConfiguration = xml_node_soap_get(p_node, "AnalyticsEngineConfiguration");
    if (p_AnalyticsEngineConfiguration)
    {
        parse_AnalyticsEngineConfiguration(p_AnalyticsEngineConfiguration, &p_res->AnalyticsEngineConfiguration);
    }

    p_RuleEngineConfiguration = xml_node_soap_get(p_node, "RuleEngineConfiguration");
    if (p_RuleEngineConfiguration)
    {
        parse_RuleEngineConfiguration(p_RuleEngineConfiguration, &p_res->RuleEngineConfiguration);
    }
    
    return TRUE;
}

BOOL parse_Profile(XMLN * p_node, ONVIF_PROFILE * p_profile)
{
    XMLN * p_Name;
    XMLN * p_VideoSourceConfiguration;
    XMLN * p_AudioSourceConfiguration;
    XMLN * p_VideoEncoderConfiguration;
    XMLN * p_AudioEncoderConfiguration;
    XMLN * p_PTZConfiguration;
    XMLN * p_VideoAnalyticsConfiguration;
    XMLN * p_MetadataConfiguration;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_profile->name, p_Name->data, sizeof(p_profile->name)-1);
    }

    p_VideoSourceConfiguration = xml_node_soap_get(p_node, "VideoSourceConfiguration");
    if (p_VideoSourceConfiguration)
    {
        VideoSourceConfigurationList * p_cfg = onvif_add_VideoSourceConfiguration(&p_profile->v_src_cfg);
        if (p_cfg)
        {
            parse_VideoSourceConfiguration(p_VideoSourceConfiguration, &p_cfg->Configuration);
        }
    }

    p_AudioSourceConfiguration = xml_node_soap_get(p_node, "AudioSourceConfiguration");
    if (p_AudioSourceConfiguration)
    {
        AudioSourceConfigurationList * p_cfg = onvif_add_AudioSourceConfiguration(&p_profile->a_src_cfg);
        if (p_cfg)
        {
            parse_AudioSourceConfiguration(p_AudioSourceConfiguration, &p_cfg->Configuration);
        }    
    }
    
    p_VideoEncoderConfiguration = xml_node_soap_get(p_node, "VideoEncoderConfiguration");
    if (p_VideoEncoderConfiguration)
    {
        VideoEncoderConfigurationList * p_cfg = onvif_add_VideoEncoderConfiguration(&p_profile->v_enc_cfg);
        if (p_cfg)
        {
            parse_VideoEncoderConfiguration(p_VideoEncoderConfiguration, &p_cfg->Configuration);
        }    
    }

    p_AudioEncoderConfiguration = xml_node_soap_get(p_node, "AudioEncoderConfiguration");
    if (p_AudioEncoderConfiguration)
    {
        AudioEncoderConfigurationList * p_cfg = onvif_add_AudioEncoderConfiguration(&p_profile->a_enc_cfg);
        if (p_cfg)
        {
            parse_AudioEncoderConfiguration(p_AudioEncoderConfiguration, &p_cfg->Configuration);
        }    
    }

    p_PTZConfiguration = xml_node_soap_get(p_node, "PTZConfiguration");
    if (p_PTZConfiguration)
    {
        PTZConfigurationList * p_cfg = onvif_add_PTZConfiguration(&p_profile->ptz_cfg);
        if (p_cfg)
        {
            parse_PTZConfiguration(p_PTZConfiguration, &p_cfg->Configuration);
        }
    }   

    p_VideoAnalyticsConfiguration = xml_node_soap_get(p_node, "VideoAnalyticsConfiguration");
    if (p_VideoAnalyticsConfiguration)
    {
        VideoAnalyticsConfigurationList * p_cfg = onvif_add_VideoAnalyticsConfiguration(&p_profile->va_cfg);
        if (p_cfg)
        {
            parse_VideoAnalyticsConfiguration(p_VideoAnalyticsConfiguration, &p_cfg->Configuration);
        }
    }

    p_MetadataConfiguration = xml_node_soap_get(p_node, "MetadataConfiguration");
    if (p_MetadataConfiguration)
    {
        MetadataConfigurationList * p_cfg = onvif_add_MetadataConfiguration(&p_profile->metadata_cfg);
        if (p_cfg)
        {
            parse_MetadataConfiguration(p_MetadataConfiguration, &p_cfg->Configuration);
        }
    }
            
    return TRUE;
}

BOOL parse_Dot11PSKSet(XMLN * p_node, onvif_Dot11PSKSet * p_res)
{
    XMLN * p_Key;
    XMLN * p_Passphrase;
    
    p_Key = xml_node_soap_get(p_node, "Key");
    if (p_Key && p_Key->data)
    {
        p_res->KeyFlag = 1;
        strncpy(p_res->Key, p_Key->data, sizeof(p_res->Key)-1);
    }

    p_Passphrase = xml_node_soap_get(p_node, "Passphrase");
    if (p_Passphrase && p_Passphrase->data)
    {
        p_res->PassphraseFlag = 1;
        strncpy(p_res->Passphrase, p_Passphrase->data, sizeof(p_res->Passphrase)-1);
    }

    return TRUE;
}

BOOL parse_Dot11SecurityConfiguration(XMLN * p_node, onvif_Dot11SecurityConfiguration * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Algorithm;
    XMLN * p_PSK;
    XMLN * p_Dot1X;

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToDot11SecurityMode(p_Mode->data);
    }

    p_Algorithm = xml_node_soap_get(p_node, "Algorithm");
    if (p_Algorithm && p_Algorithm->data)
    {
        p_res->AlgorithmFlag = 1;
        p_res->Algorithm = onvif_StringToDot11Cipher(p_Algorithm->data);
    }

    p_PSK = xml_node_soap_get(p_node, "PSK");
    if (p_PSK)
    {
        p_res->PSKFlag = parse_Dot11PSKSet(p_PSK, &p_res->PSK);
    }

    p_Dot1X = xml_node_soap_get(p_node, "Dot1X");
    if (p_Dot1X && p_Dot1X->data)
    {
        p_res->Dot1XFlag = 1;
        strncpy(p_res->Dot1X, p_Dot1X->data, sizeof(p_res->Dot1X)-1);
    }

    return TRUE;
}

BOOL parse_Dot11Configuration(XMLN * p_node, onvif_Dot11Configuration * p_res)
{
    XMLN * p_SSID;
    XMLN * p_Mode;
    XMLN * p_Alias;
    XMLN * p_Priority;
    XMLN * p_Security;

    p_SSID = xml_node_soap_get(p_node, "SSID");
    if (p_SSID && p_SSID->data)
    {
        strncpy(p_res->SSID, p_SSID->data, sizeof(p_res->SSID)-1);
    }

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToDot11StationMode(p_Mode->data);
    }

    p_Alias = xml_node_soap_get(p_node, "Alias");
    if (p_Alias && p_Alias->data)
    {
        strncpy(p_res->Alias, p_Alias->data, sizeof(p_res->Alias)-1);
    }

    p_Priority = xml_node_soap_get(p_node, "Priority");
    if (p_Priority && p_Priority->data)
    {
        p_res->Priority = atoi(p_Priority->data);
    }

    p_Security = xml_node_soap_get(p_node, "Security");
    if (p_Security)
    {
        parse_Dot11SecurityConfiguration(p_Security, &p_res->Security);
    }

    return TRUE;
}

BOOL parse_NetworkInterfaceInfo(XMLN * p_node, onvif_NetworkInterfaceInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_HwAddress;
    XMLN * p_MTU;
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        p_res->NameFlag = 1;
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_HwAddress = xml_node_soap_get(p_node, "HwAddress");
    if (p_HwAddress && p_HwAddress->data)
    {
        strncpy(p_res->HwAddress, p_HwAddress->data, sizeof(p_res->HwAddress)-1);
    }

    p_MTU = xml_node_soap_get(p_node, "MTU");
    if (p_MTU && p_MTU->data)
    {
        p_res->MTUFlag = 1;
        p_res->MTU = atoi(p_MTU->data);
    }

    return TRUE;
}

BOOL parse_IPv4Address(XMLN * p_node, onvif_PrefixedIPv4Address * p_res)
{
    XMLN * p_Address;
    XMLN * p_PrefixLength;
    
    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address && p_Address->data)
    {   
        strncpy(p_res->Address, p_Address->data, sizeof(p_res->Address)-1);
    }

    p_PrefixLength = xml_node_soap_get(p_node, "PrefixLength");
    if (p_PrefixLength && p_PrefixLength->data)
    {   
        p_res->PrefixLength = atoi(p_PrefixLength->data);
    }

    return TRUE;
}

BOOL parse_IPv4NetworkInterface(XMLN * p_node, onvif_IPv4NetworkInterface * p_res)
{
    XMLN * p_Enabled;
    XMLN * p_Config;
    
    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {   
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_Config = xml_node_soap_get(p_node, "Config");
    if (p_Config)
    {   
        XMLN * p_Manual;
        XMLN * p_FromDHCP;
        XMLN * p_DHCP;
        XMLN * p_LinkLocal;
        
        p_res->Config.sizeAddress = 0;
        
        p_Manual = xml_node_soap_get(p_Config, "Manual");
        while (p_Manual && soap_strcmp(p_Manual->name, "Manual") == 0)
        {
            parse_IPv4Address(p_Manual, &p_res->Config.Address[p_res->Config.sizeAddress]);

            p_res->Config.sizeAddress++;
            if (p_res->Config.sizeAddress >= ARRAY_SIZE(p_res->Config.Address))
            {
                break;
            }

            p_Manual = p_Manual->next;
        }

        p_FromDHCP = xml_node_soap_get(p_Config, "FromDHCP");
        while (p_FromDHCP && soap_strcmp(p_FromDHCP->name, "FromDHCP") == 0)
        {
            parse_IPv4Address(p_FromDHCP, &p_res->Config.Address[p_res->Config.sizeAddress]);

            p_res->Config.sizeAddress++;
            if (p_res->Config.sizeAddress >= ARRAY_SIZE(p_res->Config.Address))
            {
                break;
            }

            p_FromDHCP = p_FromDHCP->next;
        }

        p_LinkLocal = xml_node_soap_get(p_Config, "LinkLocal");
        if (p_LinkLocal)
        {
            p_res->Config.LinkLocalFlag = parse_IPv4Address(p_Manual, &p_res->Config.LinkLocal);
        }

        p_DHCP = xml_node_soap_get(p_Config, "DHCP");
        if (p_DHCP && p_DHCP->data)
        {
            p_res->Config.DHCP = parse_Bool(p_DHCP->data);
        }
    }

    return TRUE;
}

BOOL parse_IPv6Address(XMLN * p_node, onvif_PrefixedIPv6Address * p_res)
{
    XMLN * p_Address;
    XMLN * p_PrefixLength;
    
    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address && p_Address->data)
    {   
        strncpy(p_res->Address, p_Address->data, sizeof(p_res->Address)-1);
    }

    p_PrefixLength = xml_node_soap_get(p_node, "PrefixLength");
    if (p_PrefixLength && p_PrefixLength->data)
    {   
        p_res->PrefixLength = atoi(p_PrefixLength->data);
    }

    return TRUE;
}

BOOL parse_IPv6NetworkInterface(XMLN * p_node, onvif_IPv6NetworkInterface * p_res)
{
    XMLN * p_Enabled;
    XMLN * p_Config;
    
    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {   
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_Config = xml_node_soap_get(p_node, "Config");
    if (p_Config)
    {
        XMLN * p_AcceptRouterAdvert;
        XMLN * p_Manual;
        XMLN * p_FromDHCP;
        XMLN * p_DHCP;
        XMLN * p_LinkLocal;
        
        p_res->Config.sizeAddress = 0;

        p_AcceptRouterAdvert = xml_node_soap_get(p_Config, "AcceptRouterAdvert");
        if (p_AcceptRouterAdvert && p_AcceptRouterAdvert->data)
        {
            p_res->Config.AcceptRouterAdvert = parse_Bool(p_AcceptRouterAdvert->data);
        }

        p_DHCP = xml_node_soap_get(p_Config, "DHCP");
        if (p_DHCP && p_DHCP->data)
        {
            p_res->Config.DHCP = onvif_StringToIPv6DHCPConfiguration(p_DHCP->data);
        }
        
        p_Manual = xml_node_soap_get(p_Config, "Manual");
        while (p_Manual && soap_strcmp(p_Manual->name, "Manual") == 0)
        {
            parse_IPv6Address(p_Manual, &p_res->Config.Address[p_res->Config.sizeAddress]);

            p_res->Config.sizeAddress++;
            if (p_res->Config.sizeAddress >= ARRAY_SIZE(p_res->Config.Address))
            {
                break;
            }

            p_Manual = p_Manual->next;
        }

        p_FromDHCP = xml_node_soap_get(p_Config, "FromDHCP");
        while (p_FromDHCP && soap_strcmp(p_FromDHCP->name, "FromDHCP") == 0)
        {
            parse_IPv6Address(p_FromDHCP, &p_res->Config.Address[p_res->Config.sizeAddress]);

            p_res->Config.sizeAddress++;
            if (p_res->Config.sizeAddress >= ARRAY_SIZE(p_res->Config.Address))
            {
                break;
            }

            p_FromDHCP = p_FromDHCP->next;
        }

        p_res->Config.sizeLinkLocal = 0;
        
        p_LinkLocal = xml_node_soap_get(p_Config, "LinkLocal");
        while (p_LinkLocal && soap_strcmp(p_LinkLocal->name, "LinkLocal") == 0)
        {
            parse_IPv6Address(p_LinkLocal, &p_res->Config.LinkLocal[p_res->Config.sizeLinkLocal]);

            p_res->Config.sizeLinkLocal++;
            if (p_res->Config.sizeLinkLocal >= ARRAY_SIZE(p_res->Config.LinkLocal))
            {
                break;
            }

            p_LinkLocal = p_LinkLocal->next;
        }
    }

    return TRUE;
}

BOOL parse_NetworkInterfaceExtension(XMLN * p_node, onvif_NetworkInterfaceExtension * p_res)
{
    XMLN * p_InterfaceType;
    XMLN * p_Dot11;

    p_InterfaceType = xml_node_soap_get(p_node, "InterfaceType");
    if (p_InterfaceType && p_InterfaceType->data)
    {
        p_res->InterfaceType = atoi(p_InterfaceType->data);
    }

    p_res->sizeDot11 = 0;
    
    p_Dot11 = xml_node_soap_get(p_node, "Dot11");
    while (p_Dot11 && soap_strcmp(p_Dot11->name, "Dot11") == 0)
    {
        uint32 idx = p_res->sizeDot11;

        parse_Dot11Configuration(p_Dot11, &p_res->Dot11[idx]);
        
        p_res->sizeDot11++;
        if (p_res->sizeDot11 >= ARRAY_SIZE(p_res->Dot11))
        {
            break;
        }

        p_Dot11 = p_Dot11->next;
    }

    return TRUE;
}

BOOL parse_NetworkInterface(XMLN * p_node, onvif_NetworkInterface * p_res)
{
    XMLN * p_Enabled;
    XMLN * p_Info;
    XMLN * p_IPv4;
    XMLN * p_IPv6;
    XMLN * p_Extension;

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {   
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_Info = xml_node_soap_get(p_node, "Info");
    if (p_Info)
    {
        p_res->InfoFlag = parse_NetworkInterfaceInfo(p_Info, &p_res->Info);
    }

    p_IPv4 = xml_node_soap_get(p_node, "IPv4");
    if (p_IPv4)
    {
        p_res->IPv4Flag = parse_IPv4NetworkInterface(p_IPv4, &p_res->IPv4);
    }

    p_IPv6 = xml_node_soap_get(p_node, "IPv6");
    if (p_IPv6)
    {
        p_res->IPv6Flag = parse_IPv6NetworkInterface(p_IPv6, &p_res->IPv6);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        p_res->ExtensionFlag = parse_NetworkInterfaceExtension(p_Extension, &p_res->Extension);
    }
    
    return TRUE;
}

BOOL parse_User(XMLN * p_node, onvif_User * p_res)
{
    XMLN * p_Username;
    XMLN * p_Password;
    XMLN * p_UserLevel;
    
    p_Username = xml_node_soap_get(p_node, "Username");
    if (p_Username && p_Username->data)
    {   
        strncpy(p_res->Username, p_Username->data, sizeof(p_res->Username)-1);
    }

    p_Password = xml_node_soap_get(p_node, "Password");
    if (p_Password && p_Password->data)
    {   
        p_res->PasswordFlag = 1;
        strncpy(p_res->Password, p_Password->data, sizeof(p_res->Password)-1);
    }

    p_UserLevel = xml_node_soap_get(p_node, "UserLevel");
    if (p_UserLevel && p_UserLevel->data)
    {   
        p_res->UserLevel = onvif_StringToUserLevel(p_UserLevel->data);
    }

    return TRUE;
}

BOOL parse_Datetime(XMLN * p_node, onvif_DateTime * p_datetime)
{
    XMLN * p_Time;
    XMLN * p_Hour;
    XMLN * p_Minute;
    XMLN * p_Second;
    XMLN * p_Date;
    XMLN * p_Year;
    XMLN * p_Month;
    XMLN * p_Day;

    p_Time = xml_node_soap_get(p_node, "Time");
    if (NULL == p_Time)
    {
        return FALSE;
    }

    p_Hour = xml_node_soap_get(p_Time, "Hour");
    if (p_Hour && p_Hour->data)
    {
        p_datetime->Time.Hour = atoi(p_Hour->data);
    }

    p_Minute = xml_node_soap_get(p_Time, "Minute");
    if (p_Minute && p_Minute->data)
    {
        p_datetime->Time.Minute = atoi(p_Minute->data);
    }

    p_Second = xml_node_soap_get(p_Time, "Second");
    if (p_Second && p_Second->data)
    {
        p_datetime->Time.Second = atoi(p_Second->data);
    }

    p_Date = xml_node_soap_get(p_node, "Date");
    if (NULL == p_Date)
    {
        return FALSE;
    }

    p_Year = xml_node_soap_get(p_Date, "Year");
    if (p_Year && p_Year->data)
    {
        p_datetime->Date.Year = atoi(p_Year->data);
    }

    p_Month = xml_node_soap_get(p_Date, "Month");
    if (p_Month && p_Month->data)
    {
        p_datetime->Date.Month = atoi(p_Month->data);
    }

    p_Day = xml_node_soap_get(p_Date, "Day");
    if (p_Day && p_Day->data)
    {
        p_datetime->Date.Day = atoi(p_Day->data);
    }

    return TRUE;
}

BOOL parse_Dot11AvailableNetworks(XMLN * p_node, onvif_Dot11AvailableNetworks * p_res)
{
    XMLN * p_SSID;
    XMLN * p_BSSID;
    XMLN * p_AuthAndMangementSuite;
    XMLN * p_PairCipher;
    XMLN * p_GroupCipher;
    XMLN * p_SignalStrength;
    
    p_SSID = xml_node_soap_get(p_node, "SSID");
    if (p_SSID && p_SSID->data)
    {
        strncpy(p_res->SSID, p_SSID->data, sizeof(p_res->SSID)-1);
    }

    p_BSSID = xml_node_soap_get(p_node, "BSSID");
    if (p_BSSID && p_BSSID->data)
    {
        p_res->BSSIDFlag = 1;
        strncpy(p_res->BSSID, p_BSSID->data, sizeof(p_res->BSSID)-1);
    }

    p_res->sizeAuthAndMangementSuite = 0;
    
    p_AuthAndMangementSuite = xml_node_soap_get(p_node, "AuthAndMangementSuite");
    while (p_AuthAndMangementSuite && p_AuthAndMangementSuite->data && soap_strcmp(p_AuthAndMangementSuite->name, "AuthAndMangementSuite") == 0)
    {
        uint32 idx = p_res->sizeAuthAndMangementSuite;
        
        p_res->AuthAndMangementSuite[idx] = onvif_StringToDot11AuthAndMangementSuite(p_AuthAndMangementSuite->data);

        if (++p_res->sizeAuthAndMangementSuite >= ARRAY_SIZE(p_res->AuthAndMangementSuite))
        {
            break;
        }
        
        p_AuthAndMangementSuite = p_AuthAndMangementSuite->next;
    }

    p_res->sizePairCipher = 0;
    
    p_PairCipher = xml_node_soap_get(p_node, "PairCipher");
    while (p_PairCipher && p_PairCipher->data && soap_strcmp(p_PairCipher->name, "PairCipher") == 0)
    {
        uint32 idx = p_res->sizePairCipher;
        
        p_res->PairCipher[idx] = onvif_StringToDot11Cipher(p_PairCipher->data);

        if (++p_res->sizePairCipher >= ARRAY_SIZE(p_res->PairCipher))
        {
            break;
        }
        
        p_PairCipher = p_PairCipher->next;
    }

    p_res->sizeGroupCipher = 0;
    
    p_GroupCipher = xml_node_soap_get(p_node, "GroupCipher");
    while (p_GroupCipher && p_GroupCipher->data && soap_strcmp(p_GroupCipher->name, "PairCipher") == 0)
    {
        uint32 idx = p_res->sizeGroupCipher;
        
        p_res->GroupCipher[idx] = onvif_StringToDot11Cipher(p_GroupCipher->data);

        if (++p_res->sizeGroupCipher >= ARRAY_SIZE(p_res->GroupCipher))
        {
            break;
        }
        
        p_GroupCipher = p_GroupCipher->next;
    }

    p_SignalStrength = xml_node_soap_get(p_node, "SignalStrength");
    if (p_SignalStrength && p_SignalStrength->data)
    {
        p_res->SignalStrengthFlag = 1;
        p_res->SignalStrength = onvif_StringToDot11SignalStrength(p_SignalStrength->data);
    }
        
    return TRUE;
}

BOOL parse_GeoLocation(XMLN * p_node, onvif_GeoLocation * p_res)
{
    const char * p_lon;
    const char * p_lat;
    const char * p_elevation;
    
    p_lon = xml_attr_get(p_node, "lon");
    if (p_lon)
    {
        p_res->lonFlag = 1;
        p_res->lon = atof(p_lon);
    }

    p_lat = xml_attr_get(p_node, "lat");
    if (p_lat)
    {
        p_res->latFlag = 1;
        p_res->lat = atof(p_lat);
    }

    p_elevation = xml_attr_get(p_node, "elevation");
    if (p_elevation)
    {
        p_res->elevationFlag = 1;
        p_res->elevation = (float)atof(p_elevation);
    }

    return TRUE;
}

BOOL parse_GeoOrientation(XMLN * p_node, onvif_GeoOrientation * p_res)
{
    const char * p_roll;
    const char * p_pitch;
    const char * p_yaw;
    
    p_roll = xml_attr_get(p_node, "roll");
    if (p_roll)
    {
        p_res->rollFlag = 1;
        p_res->roll = (float)atof(p_roll);
    }

    p_pitch = xml_attr_get(p_node, "pitch");
    if (p_pitch)
    {
        p_res->pitchFlag = 1;
        p_res->pitch = (float)atof(p_pitch);
    }

    p_yaw = xml_attr_get(p_node, "yaw");
    if (p_yaw)
    {
        p_res->yawFlag = 1;
        p_res->yaw = (float)atof(p_yaw);
    }

    return TRUE;
}

BOOL parse_LocalLocation(XMLN * p_node, onvif_LocalLocation * p_res)
{
    const char * p_x;
    const char * p_y;
    const char * p_z;
    
    p_x = xml_attr_get(p_node, "x");
    if (p_x)
    {
        p_res->xFlag = 1;
        p_res->x = (float)atof(p_x);
    }

    p_y = xml_attr_get(p_node, "y");
    if (p_y)
    {
        p_res->yFlag = 1;
        p_res->y = (float)atof(p_y);
    }

    p_z = xml_attr_get(p_node, "z");
    if (p_z)
    {
        p_res->zFlag = 1;
        p_res->z = (float)atof(p_z);
    }

    return TRUE;
}

BOOL parse_LocalOrientation(XMLN * p_node, onvif_LocalOrientation * p_res)
{
    const char * p_pan;
    const char * p_tilt;
    const char * p_roll;
    
    p_pan = xml_attr_get(p_node, "pan");
    if (p_pan)
    {
        p_res->panFlag = 1;
        p_res->pan = (float)atof(p_pan);
    }

    p_tilt = xml_attr_get(p_node, "tilt");
    if (p_tilt)
    {
        p_res->tiltFlag = 1;
        p_res->tilt = (float)atof(p_tilt);
    }

    p_roll = xml_attr_get(p_node, "roll");
    if (p_roll)
    {
        p_res->rollFlag = 1;
        p_res->roll = (float)atof(p_roll);
    }

    return TRUE;
}

BOOL parse_LocationEntity(XMLN * p_node, onvif_LocationEntity * p_res)
{
    const char * p_Entity;
    const char * p_Token;
    const char * p_Fixed;
    const char * p_GeoSource;
    const char * p_AutoGeo;
    XMLN * p_GeoLocation;
    XMLN * p_GeoOrientation;
    XMLN * p_LocalLocation;
    XMLN * p_LocalOrientation;

    p_Entity = xml_attr_get(p_node, "Entity");
    if (p_Entity)
    {
        p_res->EntityFlag = 1;
        strncpy(p_res->Entity, p_Entity, sizeof(p_res->Entity)-1);
    }

    p_Token = xml_attr_get(p_node, "Token");
    if (p_Token)
    {
        p_res->TokenFlag = 1;
        strncpy(p_res->Token, p_Token, sizeof(p_res->Token)-1);
    }

    p_Fixed = xml_attr_get(p_node, "Fixed");
    if (p_Fixed)
    {
        p_res->FixedFlag = 1;
        p_res->Fixed = parse_Bool(p_Fixed);
    }

    p_GeoSource = xml_attr_get(p_node, "GeoSource");
    if (p_GeoSource)
    {
        p_res->GeoSourceFlag = 1;
        strncpy(p_res->GeoSource, p_GeoSource, sizeof(p_res->GeoSource)-1);
    }
    
    p_AutoGeo = xml_attr_get(p_node, "AutoGeo");
    if (p_AutoGeo)
    {
        p_res->AutoGeoFlag = 1;
        p_res->AutoGeo = parse_Bool(p_AutoGeo);
    }

    p_GeoLocation = xml_node_soap_get(p_node, "GeoLocation");
    if (p_GeoLocation)
    {
        p_res->GeoLocationFlag = parse_GeoLocation(p_GeoLocation, &p_res->GeoLocation);
    }

    p_GeoOrientation = xml_node_soap_get(p_node, "GeoOrientation");
    if (p_GeoOrientation)
    {
        p_res->GeoOrientationFlag = parse_GeoOrientation(p_GeoOrientation, &p_res->GeoOrientation);
    }

    p_LocalLocation = xml_node_soap_get(p_node, "LocalLocation");
    if (p_LocalLocation)
    {
        p_res->LocalLocationFlag = parse_LocalLocation(p_LocalLocation, &p_res->LocalLocation);
    }

    p_LocalOrientation = xml_node_soap_get(p_node, "LocalOrientation");
    if (p_LocalOrientation)
    {
        p_res->LocalOrientationFlag = parse_LocalOrientation(p_LocalOrientation, &p_res->LocalOrientation);
    }
    
    return TRUE;
}

#ifdef IPFILTER_SUPPORT

BOOL parse_PrefixedIPAddress(XMLN * p_node, onvif_PrefixedIPAddress * p_res)
{
    XMLN * p_Address;
    XMLN * p_PrefixLength;

    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address && p_Address->data)
    {
        strncpy(p_res->Address, p_Address->data, sizeof(p_res->Address)-1);
    }

    p_PrefixLength = xml_node_soap_get(p_node, "PrefixLength");
    if (p_PrefixLength && p_PrefixLength->data)
    {
        p_res->PrefixLength = atoi(p_PrefixLength->data);
    }

    return TRUE;
}

BOOL parse_IPAddressFilter(XMLN * p_node, onvif_IPAddressFilter * p_res)
{
    uint32 idx = 0;
    XMLN * p_Type;
    XMLN * p_IPv4Address;
    XMLN * p_IPv6Address;

    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type && p_Type->data)
    {
        p_res->Type = onvif_StringToIPAddressFilterType(p_Type->data);
    }

    idx = 0;
    
    p_IPv4Address = xml_node_soap_get(p_node, "IPv4Address");
    while (p_IPv4Address && soap_strcmp(p_IPv4Address->name, "IPv4Address") == 0)
    {
        parse_PrefixedIPAddress(p_IPv4Address, &p_res->IPv4Address[idx]);

        if (++idx >= ARRAY_SIZE(p_res->IPv4Address))
        {
            break;
        }
        
        p_IPv4Address = p_IPv4Address->next;
    }

    idx = 0;
    
    p_IPv6Address = xml_node_soap_get(p_node, "IPv6Address");
    while (p_IPv6Address && soap_strcmp(p_IPv6Address->name, "IPv6Address") == 0)
    {
        parse_PrefixedIPAddress(p_IPv6Address, &p_res->IPv6Address[idx]);

        if (++idx >= ARRAY_SIZE(p_res->IPv6Address))
        {
            break;
        }
        
        p_IPv6Address = p_IPv6Address->next;
    }

    return TRUE;
}

#endif // end of IPFILTER_SUPPORT

BOOL parse_UserCredential(XMLN * p_node, onvif_UserCredential * p_res)
{
    XMLN * p_UserName;
    XMLN * p_Password;

    p_UserName = xml_node_soap_get(p_node, "UserName");
    if (p_UserName && p_UserName->data)
    {
        strncpy(p_res->UserName, p_UserName->data, sizeof(p_res->UserName)-1);
    }

    p_Password = xml_node_soap_get(p_node, "Password");
    if (p_Password && p_Password->data)
    {
        p_res->PasswordFlag = 1;
        strncpy(p_res->Password, p_Password->data, sizeof(p_res->Password)-1);
    }

    return TRUE;
}

BOOL parse_StorageConfigurationData(XMLN * p_node, onvif_StorageConfigurationData * p_res)
{
    const char * p_type;
    const char * p_Region;
    XMLN * p_LocalPath;
    XMLN * p_StorageUri;
    XMLN * p_User;
    XMLN * p_CertPathValidationPolicyID;

    p_type = xml_attr_get(p_node, "type");
    if (p_type)
    {
        strncpy(p_res->type, p_type, sizeof(p_res->type)-1);
    }

    p_Region = xml_attr_get(p_node, "Region");
    if (p_Region)
    {
        strncpy(p_res->Region, p_Region, sizeof(p_res->Region)-1);
    }

    p_LocalPath = xml_node_soap_get(p_node, "LocalPath");
    if (p_LocalPath && p_LocalPath->data)
    {
        p_res->LocalPathFlag = 1;
        strncpy(p_res->LocalPath, p_LocalPath->data, sizeof(p_res->LocalPath)-1);
    }

    p_StorageUri = xml_node_soap_get(p_node, "StorageUri");
    if (p_StorageUri && p_StorageUri->data)
    {
        p_res->StorageUriFlag = 1;
        strncpy(p_res->StorageUri, p_StorageUri->data, sizeof(p_res->StorageUri)-1);
    }

    p_User = xml_node_soap_get(p_node, "User");
    if (p_User)
    {
        p_res->UserFlag = parse_UserCredential(p_User, &p_res->User);
    }

    p_CertPathValidationPolicyID = xml_node_soap_get(p_node, "CertPathValidationPolicyID");
    if (p_CertPathValidationPolicyID && p_CertPathValidationPolicyID->data)
    {
        p_res->CertPathValidationPolicyIDFlag = 1;
        strncpy(p_res->CertPathValidationPolicyID, p_CertPathValidationPolicyID->data, sizeof(p_res->CertPathValidationPolicyID)-1);
    }

    return TRUE;
}

BOOL parse_StorageConfiguration(XMLN * p_node, onvif_StorageConfiguration * p_res)
{
    const char * p_token;
    XMLN * p_Data;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Data = xml_node_soap_get(p_node, "Data");
    if (p_Data)
    {
        parse_StorageConfigurationData(p_Data, &p_res->Data);
    }

    return TRUE;
}

BOOL parse_RelayOutputSettings(XMLN * p_node, onvif_RelayOutputSettings * p_res)
{
    XMLN * p_Mode;
    XMLN * p_DelayTime;
    XMLN * p_IdleState;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToRelayMode(p_Mode->data);
    }

    p_DelayTime = xml_node_soap_get(p_node, "DelayTime");
    if (p_DelayTime && p_DelayTime->data)
    {
        parse_XSDDuration(p_DelayTime->data, (int*)&p_res->DelayTime);
    }

    p_IdleState = xml_node_soap_get(p_node, "IdleState");
    if (p_IdleState && p_IdleState->data)
    {
        p_res->IdleState = onvif_StringToRelayIdleState(p_IdleState->data);
    }

    return TRUE;
}

BOOL parse_Dot11Capabilities(XMLN * p_node, onvif_Dot11Capabilities * p_res)
{
    XMLN * p_TKIP;
    XMLN * p_ScanAvailableNetworks;
    XMLN * p_MultipleConfiguration;
    XMLN * p_AdHocStationMode;
    XMLN * p_WEP;

    p_TKIP = xml_node_soap_get(p_node, "TKIP");
    if (p_TKIP && p_TKIP->data)
    {
        p_res->TKIP = parse_Bool(p_TKIP->data);
    }

    p_ScanAvailableNetworks = xml_node_soap_get(p_node, "ScanAvailableNetworks");
    if (p_ScanAvailableNetworks && p_ScanAvailableNetworks->data)
    {
        p_res->ScanAvailableNetworks = parse_Bool(p_ScanAvailableNetworks->data);
    }

    p_MultipleConfiguration = xml_node_soap_get(p_node, "MultipleConfiguration");
    if (p_MultipleConfiguration && p_MultipleConfiguration->data)
    {
        p_res->MultipleConfiguration = parse_Bool(p_MultipleConfiguration->data);
    }

    p_AdHocStationMode = xml_node_soap_get(p_node, "AdHocStationMode");
    if (p_AdHocStationMode && p_AdHocStationMode->data)
    {
        p_res->AdHocStationMode = parse_Bool(p_AdHocStationMode->data);
    }

    p_WEP = xml_node_soap_get(p_node, "WEP");
    if (p_WEP && p_WEP->data)
    {
        p_res->WEP = parse_Bool(p_WEP->data);
    }

    return TRUE;
}

BOOL parse_Dot11Status(XMLN * p_node, onvif_Dot11Status * p_res)
{
    XMLN * p_SSID;
    XMLN * p_BSSID;
    XMLN * p_PairCipher;
    XMLN * p_GroupCipher;
    XMLN * p_SignalStrength;
    XMLN * p_ActiveConfigAlias;

    p_SSID = xml_node_soap_get(p_node, "SSID");
    if (p_SSID && p_SSID->data)
    {
        strncpy(p_res->SSID, p_SSID->data, sizeof(p_res->SSID)-1);
    }

    p_BSSID = xml_node_soap_get(p_node, "BSSID");
    if (p_BSSID && p_BSSID->data)
    {
        p_res->BSSIDFlag = 1;
        strncpy(p_res->BSSID, p_BSSID->data, sizeof(p_res->BSSID)-1);
    }

    p_PairCipher = xml_node_soap_get(p_node, "PairCipher");
    if (p_PairCipher && p_PairCipher->data)
    {
        p_res->PairCipherFlag = 1;
        p_res->PairCipher = onvif_StringToDot11Cipher(p_PairCipher->data);
    }

    p_GroupCipher = xml_node_soap_get(p_node, "GroupCipher");
    if (p_GroupCipher && p_GroupCipher->data)
    {
        p_res->GroupCipherFlag = 1;
        p_res->GroupCipher = onvif_StringToDot11Cipher(p_GroupCipher->data);
    }

    p_SignalStrength = xml_node_soap_get(p_node, "SignalStrength");
    if (p_SignalStrength && p_SignalStrength->data)
    {
        p_res->SignalStrengthFlag = 1;
        p_res->SignalStrength = onvif_StringToDot11SignalStrength(p_SignalStrength->data);
    }

    p_ActiveConfigAlias = xml_node_soap_get(p_node, "ActiveConfigAlias");
    if (p_ActiveConfigAlias && p_ActiveConfigAlias->data)
    {
        strncpy(p_res->ActiveConfigAlias, p_ActiveConfigAlias->data, sizeof(p_res->ActiveConfigAlias)-1);
    }

    return TRUE;
}

BOOL parse_BinaryData(XMLN * p_node, onvif_BinaryData * p_res)
{
    const char * p_contentType;
    XMLN * p_Data;

    p_contentType = xml_attr_get(p_node, "contentType");
    if (p_contentType)
    {
        p_res->contentTypeFlag = 1;
        strncpy(p_res->contentType, p_contentType, sizeof(p_res->contentType)-1);
    }

    p_Data = xml_node_soap_get(p_node, "Data");
    if (p_Data && p_Data->data)
    {
        int len = (int)strlen(p_Data->data);

        p_res->Data.ptr = (char *) malloc(len+1);
        if (p_res->Data.ptr)
        {
            strcpy(p_res->Data.ptr, p_Data->data);
            p_res->Data.size = len;
        }
    }

    return TRUE;
}

BOOL parse_NetworkZeroConfiguration(XMLN * p_node, onvif_NetworkZeroConfiguration * p_res)
{
    XMLN * p_InterfaceToken;
    XMLN * p_Enabled;
    XMLN * p_Addresses;

    p_InterfaceToken = xml_node_soap_get(p_node, "InterfaceToken");
    if (p_InterfaceToken && p_InterfaceToken->data)
    {
        strncpy(p_res->InterfaceToken, p_InterfaceToken->data, sizeof(p_res->InterfaceToken)-1);
    }

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_res->sizeAddresses = 0;
    
    p_Addresses = xml_node_soap_get(p_node, "Addresses");
    while (p_Addresses && p_Addresses->data && soap_strcmp(p_Addresses->name, "Addresses") == 0)
    {
        uint32 idx = p_res->sizeAddresses;

        strncpy(p_res->Addresses[idx], p_Addresses->data, sizeof(p_res->Addresses[idx])-1);
        
        p_res->sizeAddresses++;
        if (p_res->sizeAddresses >= ARRAY_SIZE(p_res->Addresses))
        {
            break;
        }
        
        p_Addresses = p_Addresses->next;
    }

    return TRUE;
}

BOOL parse_Scope(XMLN * p_node, onvif_Scope * p_res)
{
    XMLN * p_ScopeDef;
    XMLN * p_ScopeItem;
    
    p_ScopeDef = xml_node_soap_get(p_node, "ScopeDef");
    if (p_ScopeDef && p_ScopeDef->data)
    {
        p_res->ScopeDef = onvif_StringToScopeDefinition(p_ScopeDef->data);
    }

    p_ScopeItem = xml_node_soap_get(p_node, "ScopeItem");
    if (p_ScopeItem && p_ScopeItem->data)
    {
        strncpy(p_res->ScopeItem, p_ScopeItem->data, sizeof(p_res->ScopeItem)-1);
    }

    return TRUE;
}

BOOL parse_RemoteUser(XMLN * p_node, onvif_RemoteUser * p_res)
{
    XMLN * p_Username;
    XMLN * p_Password;
    XMLN * p_UseDerivedPassword;

    p_Username = xml_node_soap_get(p_node, "Username");
    if (p_Username && p_Username->data)
    {
        strncpy(p_res->Username, p_Username->data, sizeof(p_res->Username)-1);
    }

    p_Password = xml_node_soap_get(p_node, "Password");
    if (p_Password && p_Password->data)
    {
        p_res->PasswordFlag = 1;
        strncpy(p_res->Password, p_Password->data, sizeof(p_res->Password)-1);
    }

    p_UseDerivedPassword = xml_node_soap_get(p_node, "UseDerivedPassword");
    if (p_UseDerivedPassword && p_UseDerivedPassword->data)
    {
        p_res->UseDerivedPassword = parse_Bool(p_UseDerivedPassword->data);
    }

    return TRUE;
}

BOOL parse_NetworkProtocol(XMLN * p_node, onvif_NetworkProtocol * p_res)
{
    char    name[32];
    BOOL    enable;
    int     port[MAX_SERVER_PORT];
    uint32  i = 0;
    XMLN  * p_Name;
    XMLN  * p_Enabled;
    XMLN  * p_Port;
    
    enable = FALSE;
    memset(name, 0, sizeof(name));
    memset(port, 0, sizeof(int)*MAX_SERVER_PORT);
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(name, p_Name->data, sizeof(name)-1);
    }

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        enable = parse_Bool(p_Enabled->data);
    }
    
    p_Port = xml_node_soap_get(p_node, "Port");
    while (p_Port && p_Port->data && soap_strcmp(p_Port->name, "Port") == 0)
    {
        if (i < ARRAY_SIZE(port))
        {
            port[i++] = atoi(p_Port->data);
        }
        
        p_Port = p_Port->next;
    }

    if (strcasecmp(name, "HTTP") == 0)
    {
        p_res->HTTPFlag = 1;
        p_res->HTTPEnabled = enable;
        memcpy(p_res->HTTPPort, port, sizeof(int)*MAX_SERVER_PORT);
    }
    else if (strcasecmp(name, "HTTPS") == 0)
    {
        p_res->HTTPSFlag = 1;
        p_res->HTTPSEnabled = enable;
        memcpy(p_res->HTTPSPort, port, sizeof(int)*MAX_SERVER_PORT);
    }
    else if (strcasecmp(name, "RTSP") == 0)
    {
        p_res->RTSPFlag = 1;
        p_res->RTSPEnabled = enable;
        memcpy(p_res->RTSPPort, port, sizeof(int)*MAX_SERVER_PORT);
    }

    return TRUE;    
}

BOOL parse_NetworkGateway(XMLN * p_node, onvif_NetworkGateway * p_res)
{
    uint32 i = 0;
    XMLN * p_IPv4Address;
    XMLN * p_IPv6Address;
    
    p_IPv4Address = xml_node_soap_get(p_node, "IPv4Address");
    while (p_IPv4Address && p_IPv4Address->data && soap_strcmp(p_IPv4Address->name, "IPv4Address") == 0)
    {
        if (is_ipv4_address(p_IPv4Address->data) == FALSE)
        {
            return FALSE;
        }

        if (i < ARRAY_SIZE(p_res->IPv4Address))
        {
            strncpy(p_res->IPv4Address[i++], p_IPv4Address->data, sizeof(p_res->IPv4Address[0])-1);
        }

        p_IPv4Address = p_IPv4Address->next;
    }

    i = 0;

    p_IPv6Address = xml_node_soap_get(p_node, "IPv6Address");
    while (p_IPv6Address && p_IPv6Address->data && soap_strcmp(p_IPv6Address->name, "IPv6Address") == 0)
    {
        if (is_ipv6_address(p_IPv6Address->data) == FALSE)
        {
            return FALSE;
        }

        if (i < ARRAY_SIZE(p_res->IPv6Address))
        {
            strncpy(p_res->IPv6Address[i++], p_IPv6Address->data, sizeof(p_res->IPv6Address[0])-1);
        }

        p_IPv6Address = p_IPv6Address->next;
    }

    return ONVIF_OK;
}

/***************************************************************************************/
BOOL parse_tds_GetCapabilities(XMLN * p_node, tds_GetCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;
    XMLN * p_Analytics;
    XMLN * p_Device;
    XMLN * p_Events;
    XMLN * p_Imaging;
    XMLN * p_Media;
    XMLN * p_PTZ;
    XMLN * p_Extension;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (NULL == p_Capabilities)
    {
        return FALSE;
    }

    p_Analytics = xml_node_soap_get(p_Capabilities, "Analytics");
    if (p_Analytics)
    {
        p_res->Capabilities.analytics.support = parse_AnalyticsCapabilities(p_Analytics, &p_res->Capabilities.analytics);
    }
    
    p_Device = xml_node_soap_get(p_Capabilities, "Device");
    if (p_Device)
    {
        parse_DeviceCapabilities(p_Device, &p_res->Capabilities.device);
    }

    p_Events = xml_node_soap_get(p_Capabilities, "Events");
    if (p_Events)
    {
        p_res->Capabilities.events.support = parse_EventsCapabilities(p_Events, &p_res->Capabilities.events);
    }

    p_Imaging = xml_node_soap_get(p_Capabilities, "Imaging");
    if (p_Imaging)
    {
        p_res->Capabilities.image.support = parse_ImageCapabilities(p_Imaging, &p_res->Capabilities.image);
    }
    
    p_Media = xml_node_soap_get(p_Capabilities, "Media");
    if (p_Media)
    {
        p_res->Capabilities.media.support = parse_MediaCapabilities(p_Media, &p_res->Capabilities.media);
    }

    p_PTZ = xml_node_soap_get(p_Capabilities, "PTZ");
    if (p_PTZ)
    {
        p_res->Capabilities.ptz.support = parse_PTZCapabilities(p_PTZ, &p_res->Capabilities.ptz);
    }

    p_Extension = xml_node_soap_get(p_Capabilities, "Extension");
    if (p_Extension)
    {
        XMLN * p_DeviceIO;
        XMLN * p_Recording;
        XMLN * p_Search;
        XMLN * p_Replay;

        p_DeviceIO = xml_node_soap_get(p_Extension, "DeviceIO");
        if (p_DeviceIO)
        {
            p_res->Capabilities.deviceIO.support = parse_DeviceIOCapabilities(p_DeviceIO, &p_res->Capabilities.deviceIO);
        }
        
        p_Recording = xml_node_soap_get(p_Extension, "Recording");
        if (p_Recording)
        {
            p_res->Capabilities.recording.support = parse_RecordingCapabilities(p_Recording, &p_res->Capabilities.recording);
        }

        p_Search = xml_node_soap_get(p_Extension, "Search");
        if (p_Search)
        {
            p_res->Capabilities.search.support = parse_SearchCapabilities(p_Search, &p_res->Capabilities.search);
        }

        p_Replay = xml_node_soap_get(p_Extension, "Replay");
        if (p_Replay)
        {
            p_res->Capabilities.replay.support = parse_ReplayCapabilities(p_Replay, &p_res->Capabilities.replay);
        }
    }
    
    return TRUE;
}

BOOL parse_tds_GetServices(XMLN * p_node, tds_GetServices_RES * p_res)
{
    XMLN * p_Service;
    
    p_Service = xml_node_soap_get(p_node, "Service");
    while (p_Service)
    {
        XMLN * p_Namespace = xml_node_soap_get(p_Service, "Namespace");
        if (p_Namespace && p_Namespace->data)
        {
#ifdef DEVICEIO_SUPPORT            
            if (strstr(p_Namespace->data, "deviceIO"))
            {
                p_res->Capabilities.deviceIO.support = parse_DeviceIOService(p_Service, &p_res->Capabilities.deviceIO);
            }
            else 
#endif            
            if (strstr(p_Namespace->data, "device"))
            {
                parse_DeviceService(p_Service, &p_res->Capabilities.device);
            }
            else if (strstr(p_Namespace->data, "ver10/media"))
            {
                p_res->Capabilities.media.support = parse_MediaService(p_Service, &p_res->Capabilities.media);
            }
            else if (strstr(p_Namespace->data, "ver20/media"))
            {
                p_res->Capabilities.media2.support = parse_Media2Service(p_Service, &p_res->Capabilities.media2);
            }
            else if (strstr(p_Namespace->data, "events"))
            {
                p_res->Capabilities.events.support = parse_EventsService(p_Service, &p_res->Capabilities.events);
            }
            else if (strstr(p_Namespace->data, "ptz"))
            {
                p_res->Capabilities.ptz.support = parse_PTZService(p_Service, &p_res->Capabilities.ptz);
            }
            else if (strstr(p_Namespace->data, "imaging"))
            {
                p_res->Capabilities.image.support = parse_ImagingService(p_Service, &p_res->Capabilities.image);
            }  
            else if (strstr(p_Namespace->data, "analytics"))
            {
                p_res->Capabilities.analytics.support = parse_AnalyticsService(p_Service, &p_res->Capabilities.analytics);
            }
            
#ifdef PROFILE_G_SUPPORT            
            else if (strstr(p_Namespace->data, "recording"))
            {
                p_res->Capabilities.recording.support = parse_RecordingService(p_Service, &p_res->Capabilities.recording);
            }
            else if (strstr(p_Namespace->data, "search"))
            {
                p_res->Capabilities.search.support = parse_SearchService(p_Service, &p_res->Capabilities.search);
            }
            else if (strstr(p_Namespace->data, "replay"))
            {
                p_res->Capabilities.replay.support = parse_ReplayService(p_Service, &p_res->Capabilities.replay);
            }
#endif // end of PROFILE_G_SUPPORT       

#ifdef PROFILE_C_SUPPORT            
            else if (strstr(p_Namespace->data, "accesscontrol"))
            {
                p_res->Capabilities.accesscontrol.support = parse_AccessControlService(p_Service, &p_res->Capabilities.accesscontrol);
            }
            else if (strstr(p_Namespace->data, "doorcontrol"))
            {
                p_res->Capabilities.doorcontrol.support = parse_DoorControlService(p_Service, &p_res->Capabilities.doorcontrol);
            }
#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT
            else if (strstr(p_Namespace->data, "thermal"))
            {
                p_res->Capabilities.thermal.support = parse_ThermalService(p_Service, &p_res->Capabilities.thermal);
            }
#endif

#ifdef CREDENTIAL_SUPPORT
            else if (strstr(p_Namespace->data, "credential"))
            {
                p_res->Capabilities.credential.support = parse_CredentialService(p_Service, &p_res->Capabilities.credential);
            }
#endif

#ifdef ACCESS_RULES
            else if (strstr(p_Namespace->data, "accessrules"))
            {
                p_res->Capabilities.accessrules.support = parse_AccessRulesService(p_Service, &p_res->Capabilities.accessrules);
            }
#endif

#ifdef SCHEDULE_SUPPORT
            else if (strstr(p_Namespace->data, "schedule"))
            {
                p_res->Capabilities.schedule.support = parse_ScheduleService(p_Service, &p_res->Capabilities.schedule);
            }
#endif

#ifdef RECEIVER_SUPPORT
            else if (strstr(p_Namespace->data, "receiver"))
            {
                p_res->Capabilities.receiver.support = parse_ReceiverService(p_Service, &p_res->Capabilities.receiver);
            }
#endif
#ifdef PROVISIONING_SUPPORT
            else if (strstr(p_Namespace->data, "provisioning"))
            {
                p_res->Capabilities.provisioning.support = parse_ProvisioningService(p_Service, &p_res->Capabilities.provisioning);
            }
#endif
        }

        p_Service = p_Service->next;
    }

    return TRUE;
}

BOOL parse_tds_GetServiceCapabilities(XMLN * p_node, tds_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        return parse_DeviceServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return FALSE;
}

BOOL parse_tds_GetDeviceInformation(XMLN * p_node, tds_GetDeviceInformation_RES * p_res)
{
    XMLN * p_Manufacturer;
    XMLN * p_Model;
    XMLN * p_FirmwareVersion;
    XMLN * p_SerialNumber;
    XMLN * p_HardwareId;
    onvif_DeviceInformation * p_di = &p_res->DeviceInformation;

    p_Manufacturer = xml_node_soap_get(p_node, "Manufacturer");
    if (p_Manufacturer && p_Manufacturer->data)
    {   
        strncpy(p_di->Manufacturer, p_Manufacturer->data, sizeof(p_di->Manufacturer)-1);
    }

    p_Model = xml_node_soap_get(p_node, "Model");
    if (p_Model && p_Model->data)
    {   
        strncpy(p_di->Model, p_Model->data, sizeof(p_di->Model)-1);
    }

    p_FirmwareVersion = xml_node_soap_get(p_node, "FirmwareVersion");
    if (p_FirmwareVersion && p_FirmwareVersion->data)
    {   
        strncpy(p_di->FirmwareVersion, p_FirmwareVersion->data, sizeof(p_di->FirmwareVersion)-1);
    }

    p_SerialNumber = xml_node_soap_get(p_node, "SerialNumber");
    if (p_SerialNumber && p_SerialNumber->data)
    {   
        strncpy(p_di->SerialNumber, p_SerialNumber->data, sizeof(p_di->SerialNumber)-1);
    }

    p_HardwareId = xml_node_soap_get(p_node, "HardwareId");
    if (p_HardwareId && p_HardwareId->data)
    {   
        strncpy(p_di->HardwareId, p_HardwareId->data, sizeof(p_di->HardwareId)-1);
    }    

    return TRUE;
}

BOOL parse_tds_GetUsers(XMLN * p_node, tds_GetUsers_RES * p_res)
{
    XMLN * p_User;

    p_User = xml_node_soap_get(p_node, "User");
    while (p_User)
    {
        UserList * p_user = onvif_add_User(&p_res->User);
        if (p_user)
        {
            if (!parse_User(p_User, &p_user->User))
            {
                onvif_free_Users(&p_res->User);
                return FALSE;
            }
        }

        p_User = p_User->next;
    }
    
    return TRUE;
}

BOOL parse_tds_GetRemoteUser(XMLN * p_node, tds_GetRemoteUser_RES * p_res)
{
    XMLN * p_RemoteUser = xml_node_soap_get(p_node, "RemoteUser");
    if (p_RemoteUser)
    {
        p_res->RemoteUserFlag = parse_RemoteUser(p_RemoteUser, &p_res->RemoteUser);
    }

    return TRUE;
}

BOOL parse_tds_GetNetworkInterfaces(XMLN * p_node, tds_GetNetworkInterfaces_RES * p_res)
{
    XMLN * p_NetworkInterfaces;

    p_NetworkInterfaces = xml_node_soap_get(p_node, "NetworkInterfaces");
    while (p_NetworkInterfaces)
    {
        NetworkInterfaceList * p_net_inf = onvif_add_NetworkInterface(&p_res->NetworkInterfaces);
        if (NULL != p_net_inf)
        {
            const char * p_token = xml_attr_get(p_NetworkInterfaces, "token");
            if (p_token)
            {
                strncpy(p_net_inf->NetworkInterface.token, p_token, sizeof(p_net_inf->NetworkInterface.token)-1);
            }
            
            if (!parse_NetworkInterface(p_NetworkInterfaces, &p_net_inf->NetworkInterface))
            {
                onvif_free_NetworkInterfaces(&p_res->NetworkInterfaces);
                return FALSE;
            }    
        }

        p_NetworkInterfaces = p_NetworkInterfaces->next;
    }
        
    return TRUE;
}

BOOL parse_tds_SetNetworkInterfaces(XMLN * p_node, tds_SetNetworkInterfaces_RES * p_res)
{
    XMLN * p_RebootNeeded = xml_node_soap_get(p_node, "RebootNeeded");
    if (p_RebootNeeded && p_RebootNeeded->data)
    {
        p_res->RebootNeeded = parse_Bool(p_RebootNeeded->data);
    }

    return TRUE;
}

BOOL parse_tds_GetNTP(XMLN * p_node, tds_GetNTP_RES * p_res)
{
    uint32 i = 0;    
    char node[32];
    XMLN * p_NTPInformation;
    XMLN * p_FromDHCP;
    XMLN * p_NTP;
    onvif_NTPInformation * p_ni = &p_res->NTPInformation;

    p_NTPInformation = xml_node_soap_get(p_node, "NTPInformation");
    if (NULL == p_NTPInformation)
    {
        return FALSE;
    }
    
    p_FromDHCP = xml_node_soap_get(p_NTPInformation, "FromDHCP");
    if (p_FromDHCP && p_FromDHCP->data)
    {
        p_ni->FromDHCP = parse_Bool(p_FromDHCP->data);
    }    

    if (p_ni->FromDHCP)
    {
        strcpy(node, "NTPFromDHCP");
    }
    else
    {
        strcpy(node, "NTPManual");
    }
    
    p_NTP = xml_node_soap_get(p_NTPInformation, node);
    while (p_NTP && soap_strcmp(p_NTP->name, node) == 0)
    {
        XMLN * p_Type;
        XMLN * p_IPv4Address;
        XMLN * p_IPv6Address;
        XMLN * p_DNSname;

        p_Type = xml_node_soap_get(p_NTP, "Type");
        if (p_Type && p_Type->data)
        {
            if (strcasecmp(p_Type->data, "IPv4") != 0 && 
                strcasecmp(p_Type->data, "IPv6") != 0 && 
                strcasecmp(p_Type->data, "DNS") != 0)
            {
                p_NTP = p_NTP->next;
                continue;
            }
        }

        p_IPv4Address = xml_node_soap_get(p_NTP, "IPv4Address");
        if (p_IPv4Address && p_IPv4Address->data)
        {
            if (is_ipv4_address(p_IPv4Address->data) && i < ARRAY_SIZE(p_ni->NTPServer))
            {
                strncpy(p_ni->NTPServer[i], p_IPv4Address->data, sizeof(p_ni->NTPServer[i])-1);
                ++i;
            }
        }

        p_IPv6Address = xml_node_soap_get(p_NTP, "IPv6Address");
        if (p_IPv6Address && p_IPv6Address->data)
        {
            if (is_ipv6_address(p_IPv6Address->data) && i < ARRAY_SIZE(p_ni->NTPServer))
            {
                strncpy(p_ni->NTPServer[i], p_IPv6Address->data, sizeof(p_ni->NTPServer[i])-1);
                ++i;
            }
        }

        p_DNSname = xml_node_soap_get(p_NTP, "DNSname");
        if (p_DNSname && p_DNSname->data)
        {
            if (i < ARRAY_SIZE(p_ni->NTPServer))
            {
                strncpy(p_ni->NTPServer[i], p_DNSname->data, sizeof(p_ni->NTPServer[i])-1);
                ++i;
            }
        }
        
        p_NTP = p_NTP->next;
    }

    return TRUE;
}

BOOL parse_tds_GetHostname(XMLN * p_node, tds_GetHostname_RES * p_res)
{
    XMLN * p_HostnameInformation;
    XMLN * p_FromDHCP;
    XMLN * p_Name;

    p_HostnameInformation = xml_node_soap_get(p_node, "HostnameInformation");
    if (NULL == p_HostnameInformation)
    {
        return FALSE;
    }    
    
    p_FromDHCP = xml_node_soap_get(p_HostnameInformation, "FromDHCP");
    if (p_FromDHCP && p_FromDHCP->data)
    {
        p_res->HostnameInformation.FromDHCP = parse_Bool(p_FromDHCP->data);
    }
    
    p_Name = xml_node_soap_get(p_HostnameInformation, "Name");
    if (p_Name && p_Name->data)
    {
        p_res->HostnameInformation.NameFlag = 1;
        strncpy(p_res->HostnameInformation.Name, p_Name->data, sizeof(p_res->HostnameInformation.Name)-1);
    }

    return TRUE;
}

BOOL parse_tds_SetHostnameFromDHCP(XMLN * p_node, tds_SetHostnameFromDHCP_RES * p_res)
{
    XMLN * p_RebootNeeded = xml_node_soap_get(p_node, "RebootNeeded");
    if (p_RebootNeeded && p_RebootNeeded->data)
    {
        p_res->RebootNeeded = parse_Bool(p_RebootNeeded->data);
    }

    return TRUE;
}

BOOL parse_tds_GetDNS(XMLN * p_node, tds_GetDNS_RES * p_res)
{
    uint32 i = 0;
    char node[32];
    XMLN * p_DNSInformation;
    XMLN * p_FromDHCP;
    XMLN * p_SearchDomain;
    XMLN * p_DNS;
    onvif_DNSInformation * p_di = &p_res->DNSInformation;

    p_DNSInformation = xml_node_soap_get(p_node, "DNSInformation");
    if (NULL == p_DNSInformation)
    {
        return FALSE;
    }
    
    p_FromDHCP = xml_node_soap_get(p_DNSInformation, "FromDHCP");
    if (p_FromDHCP && p_FromDHCP->data)
    {
        p_di->FromDHCP = parse_Bool(p_FromDHCP->data);
    }
    
    p_SearchDomain = xml_node_soap_get(p_DNSInformation, "SearchDomain");
    while (p_SearchDomain && p_SearchDomain->data && soap_strcmp(p_SearchDomain->name, "SearchDomain") == 0)
    {
        p_di->SearchDomainFlag = 1;
        
        if (i < ARRAY_SIZE(p_di->SearchDomain))
        {
            strncpy(p_di->SearchDomain[i], p_SearchDomain->data, sizeof(p_di->SearchDomain[i])-1);
            ++i;
        }

        p_SearchDomain = p_SearchDomain->next;
    }
    
    i = 0;

    if (p_di->FromDHCP)
    {
        strcpy(node, "DNSFromDHCP");
    }
    else
    {
        strcpy(node, "DNSManual");
    }
    
    p_DNS = xml_node_soap_get(p_DNSInformation, node);
    while (p_DNS && soap_strcmp(p_DNS->name, node) == 0)
    {
        XMLN * p_Type;
        XMLN * p_IPv4Address;
        XMLN * p_IPv6Address;

        p_Type = xml_node_soap_get(p_DNS, "Type");
        if (p_Type && p_Type->data)
        {
            if (strcasecmp(p_Type->data, "IPv4") != 0 && strcasecmp(p_Type->data, "IPv6") != 0)
            {
                p_DNS = p_DNS->next;
                continue;
            }
        }

        p_IPv4Address = xml_node_soap_get(p_DNS, "IPv4Address");
        if (p_IPv4Address && p_IPv4Address->data)
        {
            if (is_ipv4_address(p_IPv4Address->data) && i < ARRAY_SIZE(p_di->DNSServer))
            {
                strncpy(p_di->DNSServer[i], p_IPv4Address->data, sizeof(p_di->DNSServer[i])-1);
                ++i;
            }
        }

        p_IPv6Address = xml_node_soap_get(p_DNS, "IPv6Address");
        if (p_IPv6Address && p_IPv6Address->data)
        {
            if (is_ipv6_address(p_IPv6Address->data) && i < ARRAY_SIZE(p_di->DNSServer))
            {
                strncpy(p_di->DNSServer[i], p_IPv6Address->data, sizeof(p_di->DNSServer[i])-1);
                ++i;
            }
        }
        
        p_DNS = p_DNS->next;
    }

    return TRUE;
}

BOOL parse_tds_GetDynamicDNS(XMLN * p_node, tds_GetDynamicDNS_RES * p_res)
{
    XMLN * p_DynamicDNSInformation;
    XMLN * p_Type;
    XMLN * p_Name;
    XMLN * p_TTL;

    p_DynamicDNSInformation = xml_node_soap_get(p_node, "DynamicDNSInformation");
    if (NULL == p_DynamicDNSInformation)
    {
        return FALSE;
    }

    p_Type = xml_node_soap_get(p_DynamicDNSInformation, "Type");
    if (p_Type && p_Type->data)
    {
        p_res->DynamicDNSInformation.Type = onvif_StringToDynamicDNSType(p_Type->data);
    }

    p_Name = xml_node_soap_get(p_DynamicDNSInformation, "Name");
    if (p_Name && p_Name->data)
    {
        p_res->DynamicDNSInformation.NameFlag = 1;
        strncpy(p_res->DynamicDNSInformation.Name, p_Name->data, sizeof(p_res->DynamicDNSInformation.Name)-1);
    }

    p_TTL = xml_node_soap_get(p_DynamicDNSInformation, "TTL");
    if (p_TTL && p_TTL->data)
    {
        p_res->DynamicDNSInformation.TTLFlag = 1;
        p_res->DynamicDNSInformation.TTL = atoi(p_TTL->data);
    }

    return TRUE;
}

BOOL parse_tds_GetNetworkProtocols(XMLN * p_node, tds_GetNetworkProtocols_RES * p_res)
{
    XMLN * p_NetworkProtocols = xml_node_soap_get(p_node, "NetworkProtocols");
    while (p_NetworkProtocols && soap_strcmp(p_NetworkProtocols->name, "NetworkProtocols") == 0)
    {
        parse_NetworkProtocol(p_NetworkProtocols, &p_res->NetworkProtocols);
        
        p_NetworkProtocols = p_NetworkProtocols->next;
    }

    return TRUE;
}

BOOL parse_tds_GetDiscoveryMode(XMLN * p_node, tds_GetDiscoveryMode_RES * p_res)
{
    XMLN * p_DiscoveryMode = xml_node_soap_get(p_node, "DiscoveryMode");
    if (p_DiscoveryMode && p_DiscoveryMode->data)
    {
        p_res->DiscoveryMode = onvif_StringToDiscoveryMode(p_DiscoveryMode->data);
    }

    return TRUE;
}

BOOL parse_tds_GetNetworkDefaultGateway(XMLN * p_node, tds_GetNetworkDefaultGateway_RES * p_res)
{
    XMLN * p_NetworkGateway;

    p_NetworkGateway = xml_node_soap_get(p_node, "NetworkGateway");
    if (p_NetworkGateway)
    {
        parse_NetworkGateway(p_NetworkGateway, &p_res->NetworkGateway);
    }

    return TRUE;
}

BOOL parse_tds_GetZeroConfiguration(XMLN * p_node, tds_GetZeroConfiguration_RES * p_res)
{
    XMLN * p_ZeroConfiguration;
    
    p_ZeroConfiguration = xml_node_soap_get(p_node, "ZeroConfiguration");
    if (p_ZeroConfiguration)
    {
        parse_NetworkZeroConfiguration(p_ZeroConfiguration, &p_res->ZeroConfiguration);
    }

    return TRUE;
}

BOOL parse_tds_GetEndpointReference(XMLN * p_node, tds_GetEndpointReference_RES * p_res)
{
    XMLN * p_GUID;

    p_GUID = xml_node_soap_get(p_node, "GUID");
    if (p_GUID && p_GUID->data)
    {
        strncpy(p_res->GUID, p_GUID->data, sizeof(p_res->GUID)-1);
    }

    return TRUE;
}

BOOL parse_tds_SendAuxiliaryCommand(XMLN * p_node, tds_SendAuxiliaryCommand_RES * p_res)
{
    XMLN * p_AuxiliaryCommandResponse;

    p_AuxiliaryCommandResponse = xml_node_soap_get(p_node, "AuxiliaryCommandResponse");
    if (p_AuxiliaryCommandResponse && p_AuxiliaryCommandResponse->data)
    {
        p_res->AuxiliaryCommandResponseFlag = 1;
        strncpy(p_res->AuxiliaryCommandResponse, p_AuxiliaryCommandResponse->data, sizeof(p_res->AuxiliaryCommandResponse)-1);
    }

    return TRUE;
}

BOOL parse_tds_GetRelayOutputs(XMLN * p_node, tds_GetRelayOutputs_RES * p_res)
{
    XMLN * p_RelayOutputs;

    p_RelayOutputs = xml_node_soap_get(p_node, "RelayOutputs");
    while (p_RelayOutputs && soap_strcmp(p_RelayOutputs->name, "RelayOutputs") == 0)
    {
        RelayOutputList * p_RelayOutput = onvif_add_RelayOutput(&p_res->RelayOutputs);
        if (p_RelayOutput)
        {
            XMLN * p_Properties;
            const char * p_token;
            
            p_token = xml_attr_get(p_RelayOutputs, "token");
            if (p_token)
            {
                strncpy(p_RelayOutput->RelayOutput.token, p_token, sizeof(p_RelayOutput->RelayOutput.token)-1);
            }

            p_Properties = xml_node_soap_get(p_RelayOutputs, "Properties");
            if (p_Properties)
            {
                parse_RelayOutputSettings(p_Properties, &p_RelayOutput->RelayOutput.Properties);
            }
        }
        
        p_RelayOutputs = p_RelayOutputs->next;
    }

    return TRUE;
}

BOOL parse_tds_GetSystemDateAndTime(XMLN * p_node, tds_GetSystemDateAndTime_RES * p_res)
{
    XMLN * p_SystemDateAndTime;
    XMLN * p_DateTimeType;
    XMLN * p_DaylightSavings;
    XMLN * p_TimeZone;
    XMLN * p_UTCDateTime;
    XMLN * p_LocalDateTime;

    p_SystemDateAndTime = xml_node_soap_get(p_node, "SystemDateAndTime");
    if (NULL == p_SystemDateAndTime)
    {
        return FALSE;
    }

    p_DateTimeType = xml_node_soap_get(p_SystemDateAndTime, "DateTimeType");
    if (p_DateTimeType && p_DateTimeType->data)
    {
        p_res->SystemDateTime.DateTimeType = onvif_StringToSetDateTimeType(p_DateTimeType->data);
    }

    p_DaylightSavings = xml_node_soap_get(p_SystemDateAndTime, "DaylightSavings");
    if (p_DaylightSavings && p_DaylightSavings->data)
    {
        p_res->SystemDateTime.DaylightSavings = parse_Bool(p_DaylightSavings->data);
    }

    p_TimeZone = xml_node_soap_get(p_SystemDateAndTime, "TimeZone");
    if (p_TimeZone)
    {
        XMLN * p_TZ;

        p_res->SystemDateTime.TimeZoneFlag = 1;
        
        p_TZ = xml_node_soap_get(p_TimeZone, "TZ");
        if (p_TZ && p_TZ->data)
        {
            strncpy(p_res->SystemDateTime.TimeZone.TZ, p_TZ->data, sizeof(p_res->SystemDateTime.TimeZone.TZ)-1);
        }
    }

    p_UTCDateTime = xml_node_soap_get(p_SystemDateAndTime, "UTCDateTime");
    if (p_UTCDateTime)
    {
        p_res->UTCDateTimeFlag = parse_Datetime(p_UTCDateTime, &p_res->UTCDateTime);
    }

    p_LocalDateTime = xml_node_soap_get(p_SystemDateAndTime, "LocalDateTime");
    if (p_LocalDateTime)
    {
        p_res->LocalDateTimeFlag = parse_Datetime(p_LocalDateTime, &p_res->LocalDateTime);
    }

    return TRUE;
}

BOOL parse_tds_GetSystemLog(XMLN * p_node, tds_GetSystemLog_RES * p_res)
{
    XMLN * p_SystemLog;
    XMLN * p_String;
    
    p_SystemLog = xml_node_soap_get(p_node, "SystemLog");
    if (NULL == p_SystemLog)
    {
        return FALSE;
    }

    p_res->String = NULL;
    
    p_String = xml_node_soap_get(p_SystemLog, "String");
    if (p_String && p_String->data)
    {
        p_res->String = strdup(p_String->data);
    }

    return TRUE;
}

BOOL parse_tds_GetScopes(XMLN * p_node, tds_GetScopes_RES * p_res)
{
    XMLN * p_Scopes;

    p_res->sizeScopes = 0;
    
    p_Scopes = xml_node_soap_get(p_node, "Scopes");
    while (p_Scopes && soap_strcmp(p_Scopes->name, "Scopes") == 0)
    {
        uint32 idx = p_res->sizeScopes;

        parse_Scope(p_Scopes, &p_res->Scopes[idx]);

        p_res->sizeScopes++;
        if (p_res->sizeScopes >= ARRAY_SIZE(p_res->Scopes))
        {
            break;
        }
        
        p_Scopes = p_Scopes->next;
    }

    return TRUE;
}

BOOL parse_tds_StartFirmwareUpgrade(XMLN * p_node, tds_StartFirmwareUpgrade_RES * p_res)
{
    XMLN * p_UploadUri;
    XMLN * p_UploadDelay;
    XMLN * p_ExpectedDownTime;

    p_UploadUri = xml_node_soap_get(p_node, "UploadUri");
    if (p_UploadUri && p_UploadUri->data)
    {
        strncpy(p_res->UploadUri, p_UploadUri->data, sizeof(p_res->UploadUri)-1);
    }

    p_UploadDelay = xml_node_soap_get(p_node, "UploadDelay");
    if (p_UploadDelay && p_UploadDelay->data)
    {
        p_res->UploadDelay = atoi(p_UploadDelay->data);
    }

    p_ExpectedDownTime = xml_node_soap_get(p_node, "ExpectedDownTime");
    if (p_ExpectedDownTime && p_ExpectedDownTime->data)
    {
        parse_XSDDuration(p_ExpectedDownTime->data, &p_res->ExpectedDownTime);
    }

    return TRUE;
}

BOOL parse_tds_GetSystemUris(XMLN * p_node, tds_GetSystemUris_RES * p_res)
{
    XMLN * p_SystemLogUris;
    XMLN * p_SupportInfoUri;
    XMLN * p_SystemBackupUri;

    p_SystemLogUris = xml_node_soap_get(p_node, "SystemLogUris");
    if (p_SystemLogUris)
    {
        XMLN * p_SystemLog;

        p_SystemLog = xml_node_soap_get(p_SystemLogUris, "SystemLog");
        while (p_SystemLog && soap_strcmp(p_SystemLog->name, "SystemLog") == 0)
        {
            int type = -1;
            XMLN * p_Type;
            XMLN * p_Uri;

            p_Type = xml_node_soap_get(p_SystemLog, "Type");
            if (p_Type && p_Type->data)
            {
                if (soap_strcmp(p_Type->data, "System") == 0)
                {
                    type = 0;
                }
                else if (soap_strcmp(p_Type->data, "Access") == 0)
                {
                    type = 1;
                }
            }

            p_Uri = xml_node_soap_get(p_SystemLog, "Uri");
            if (p_Uri && p_Uri->data)
            {
                if (0 == type)
                {
                    p_res->SystemLogUriFlag = 1;
                    strncpy(p_res->SystemLogUri, p_Uri->data, sizeof(p_res->SystemLogUri)-1);
                }
                else if (1 == type)
                {
                    p_res->AccessLogUriFlag = 1;
                    strncpy(p_res->AccessLogUri, p_Uri->data, sizeof(p_res->AccessLogUri)-1);
                }
            }
    
            p_SystemLog = p_SystemLog->next;
        }
    }

    p_SupportInfoUri = xml_node_soap_get(p_node, "SupportInfoUri");
    if (p_SupportInfoUri && p_SupportInfoUri->data)
    {
        p_res->SupportInfoUriFlag = 1;
        strncpy(p_res->SupportInfoUri, p_SupportInfoUri->data, sizeof(p_res->SupportInfoUri)-1);
    }

    p_SystemBackupUri = xml_node_soap_get(p_node, "SystemBackupUri");
    if (p_SystemBackupUri && p_SystemBackupUri->data)
    {
        p_res->SystemBackupUriFlag = 1;
        strncpy(p_res->SystemBackupUri, p_SystemBackupUri->data, sizeof(p_res->SystemBackupUri)-1);
    }

    return TRUE;
}

BOOL parse_tds_StartSystemRestore(XMLN * p_node, tds_StartSystemRestore_RES * p_res)
{
    XMLN * p_UploadUri;
    XMLN * p_ExpectedDownTime;

    p_UploadUri = xml_node_soap_get(p_node, "UploadUri");
    if (p_UploadUri && p_UploadUri->data)
    {
        strncpy(p_res->UploadUri, p_UploadUri->data, sizeof(p_res->UploadUri)-1);
    }

    p_ExpectedDownTime = xml_node_soap_get(p_node, "ExpectedDownTime");
    if (p_ExpectedDownTime && p_ExpectedDownTime->data)
    {
        parse_XSDDuration(p_ExpectedDownTime->data, &p_res->ExpectedDownTime);
    }

    return TRUE;
}

BOOL parse_tds_GetWsdlUrl(XMLN * p_node, tds_GetWsdlUrl_RES * p_res)
{
    XMLN * p_WsdlUrl;

    p_WsdlUrl = xml_node_soap_get(p_node, "WsdlUrl");
    if (p_WsdlUrl && p_WsdlUrl->data)
    {
        strncpy(p_res->WsdlUrl, p_WsdlUrl->data, sizeof(p_res->WsdlUrl)-1);
    }

    return TRUE;
}

BOOL parse_tds_GetDot11Capabilities(XMLN * p_node, tds_GetDot11Capabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        parse_Dot11Capabilities(p_Capabilities, &p_res->Capabilities);
    }

    return TRUE;
}

BOOL parse_tds_GetDot11Status(XMLN * p_node, tds_GetDot11Status_RES * p_res)
{
    XMLN * p_Status;

    p_Status = xml_node_soap_get(p_node, "Status");
    if (p_Status)
    {
        parse_Dot11Status(p_Status, &p_res->Status);
    }

    return TRUE;
}

BOOL parse_tds_ScanAvailableDot11Networks(XMLN * p_node, tds_ScanAvailableDot11Networks_RES * p_res)
{
    XMLN * p_Networks;

    p_Networks = xml_node_soap_get(p_node, "Networks");
    while (p_Networks && soap_strcmp(p_Networks->name, "Networks") == 0)
    {
        Dot11AvailableNetworksList * p_info = onvif_add_Dot11AvailableNetworks(&p_res->Networks);
        if (p_info)
        {
            parse_Dot11AvailableNetworks(p_Networks, &p_info->Networks);
        }
        
        p_Networks = p_Networks->next;
    }

    return TRUE;
}

BOOL parse_tds_GetGeoLocation(XMLN * p_node, tds_GetGeoLocation_RES * p_res)
{
    XMLN * p_Location;

    p_Location = xml_node_soap_get(p_node, "Location");
    while (p_Location && soap_strcmp(p_Location->name, "Location") == 0)
    {
        LocationEntityList * p_info = onvif_add_LocationEntity(&p_res->Location);
        if (p_info)
        {
            parse_LocationEntity(p_Location, &p_info->Location);
        }
        
        p_Location = p_Location->next;
    }

    return TRUE;
}

#ifdef IPFILTER_SUPPORT

BOOL parse_tds_GetIPAddressFilter(XMLN * p_node, tds_GetIPAddressFilter_RES * p_res)
{
    XMLN * p_IPAddressFilter;

    p_IPAddressFilter = xml_node_soap_get(p_node, "IPAddressFilter");
    if (p_IPAddressFilter)
    {
        parse_IPAddressFilter(p_IPAddressFilter, &p_res->IPAddressFilter);
    }

    return TRUE;
}

#endif // end of IPFILTER_SUPPORT

BOOL parse_tds_GetAccessPolicy(XMLN * p_node, tds_GetAccessPolicy_RES * p_res)
{
    XMLN * p_PolicyFile;

    p_PolicyFile = xml_node_soap_get(p_node, "PolicyFile");
    if (p_PolicyFile)
    {
        parse_BinaryData(p_PolicyFile, &p_res->PolicyFile);
    }

    return TRUE;
}

BOOL parse_tds_GetStorageConfigurations(XMLN * p_node, tds_GetStorageConfigurations_RES * p_res)
{
    XMLN * p_StorageConfigurations;

    p_StorageConfigurations = xml_node_soap_get(p_node, "StorageConfigurations");
    while (p_StorageConfigurations && soap_strcmp(p_StorageConfigurations->name, "StorageConfigurations") == 0)
    {
        StorageConfigurationList * p_info = onvif_add_StorageConfiguration(&p_res->StorageConfigurations);
        if (p_info)
        {
            parse_StorageConfiguration(p_StorageConfigurations, &p_info->Configuration);
        }
        
        p_StorageConfigurations = p_StorageConfigurations->next;
    }

    return TRUE;
}

BOOL parse_tds_CreateStorageConfiguration(XMLN * p_node, tds_CreateStorageConfiguration_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tds_GetStorageConfiguration(XMLN * p_node, tds_GetStorageConfiguration_RES * p_res)
{
    XMLN * p_StorageConfiguration;

    p_StorageConfiguration = xml_node_soap_get(p_node, "StorageConfiguration");
    if (p_StorageConfiguration)
    {
        parse_StorageConfiguration(p_StorageConfiguration, &p_res->StorageConfiguration);
    }

    return TRUE;
}

/***************************************************************************************/

BOOL parse_JPEGOptions(XMLN * p_node, onvif_JpegOptions * p_res)
{
    uint32    i = 0;        
    XMLN * p_ResolutionsAvailable;
    XMLN * p_FrameRateRange;
    XMLN * p_EncodingIntervalRange;    
    
    p_ResolutionsAvailable = xml_node_soap_get(p_node, "ResolutionsAvailable");
    while (p_ResolutionsAvailable && soap_strcmp(p_ResolutionsAvailable->name, "ResolutionsAvailable") == 0)
    {
        parse_VideoResolution(p_ResolutionsAvailable, &p_res->ResolutionsAvailable[i++]);

        if (i >= ARRAY_SIZE(p_res->ResolutionsAvailable))
        {
            break;
        }
        
        p_ResolutionsAvailable = p_ResolutionsAvailable->next;
    }

    p_FrameRateRange = xml_node_soap_get(p_node, "FrameRateRange");
    if (p_FrameRateRange)
    {
        parse_IntRange(p_FrameRateRange, &p_res->FrameRateRange);            
    }

    p_EncodingIntervalRange = xml_node_soap_get(p_node, "EncodingIntervalRange");
    if (p_EncodingIntervalRange)
    {
        parse_IntRange(p_EncodingIntervalRange, &p_res->EncodingIntervalRange);    
    }

    return TRUE;
}

BOOL parse_MPEG4Options(XMLN * p_node, onvif_Mpeg4Options * p_res)
{
    uint32    i = 0;        
    XMLN * p_ResolutionsAvailable;
    XMLN * p_GovLengthRange;
    XMLN * p_FrameRateRange;
    XMLN * p_EncodingIntervalRange;
    XMLN * p_Mpeg4ProfilesSupported;
    
    p_ResolutionsAvailable = xml_node_soap_get(p_node, "ResolutionsAvailable");
    while (p_ResolutionsAvailable && soap_strcmp(p_ResolutionsAvailable->name, "ResolutionsAvailable") == 0)
    {
        parse_VideoResolution(p_ResolutionsAvailable, &p_res->ResolutionsAvailable[i++]);

        if (i >= ARRAY_SIZE(p_res->ResolutionsAvailable))
        {
            break;
        }
        
        p_ResolutionsAvailable = p_ResolutionsAvailable->next;
    }

    p_GovLengthRange = xml_node_soap_get(p_node, "GovLengthRange");
    if (p_GovLengthRange)
    {
        parse_IntRange(p_GovLengthRange, &p_res->GovLengthRange);
    }
    
    p_FrameRateRange = xml_node_soap_get(p_node, "FrameRateRange");
    if (p_FrameRateRange)
    {
        parse_IntRange(p_FrameRateRange, &p_res->FrameRateRange);
    }

    p_EncodingIntervalRange = xml_node_soap_get(p_node, "EncodingIntervalRange");
    if (p_EncodingIntervalRange)
    {
        parse_IntRange(p_EncodingIntervalRange, &p_res->EncodingIntervalRange);
    }

    p_Mpeg4ProfilesSupported = xml_node_soap_get(p_node, "Mpeg4ProfilesSupported");
    while (p_Mpeg4ProfilesSupported && p_Mpeg4ProfilesSupported->data && soap_strcmp(p_Mpeg4ProfilesSupported->name, "Mpeg4ProfilesSupported") == 0)
    {
        if (strcasecmp(p_Mpeg4ProfilesSupported->data, "SP") == 0)
        {
            p_res->Mpeg4Profile_SP = 1;
        }
        else if (strcasecmp(p_Mpeg4ProfilesSupported->data, "ASP") == 0)
        {
            p_res->Mpeg4Profile_ASP = 1;
        }
        
        p_Mpeg4ProfilesSupported = p_Mpeg4ProfilesSupported->next;
    }

    return TRUE;
}

BOOL parse_H264Options(XMLN * p_node, onvif_H264Options * p_res)
{
    uint32    i = 0;        
    XMLN * p_ResolutionsAvailable;
    XMLN * p_GovLengthRange;
    XMLN * p_FrameRateRange;
    XMLN * p_EncodingIntervalRange;
    XMLN * p_H264ProfilesSupported;

    p_ResolutionsAvailable = xml_node_soap_get(p_node, "ResolutionsAvailable");
    while (p_ResolutionsAvailable && soap_strcmp(p_ResolutionsAvailable->name, "ResolutionsAvailable") == 0)
    {
        parse_VideoResolution(p_ResolutionsAvailable, &p_res->ResolutionsAvailable[i++]);

        if (i >= ARRAY_SIZE(p_res->ResolutionsAvailable))
        {
            break;
        }
        
        p_ResolutionsAvailable = p_ResolutionsAvailable->next;
    }

    p_GovLengthRange = xml_node_soap_get(p_node, "GovLengthRange");
    if (p_GovLengthRange)
    {
        parse_IntRange(p_GovLengthRange, &p_res->GovLengthRange);
    }
    
    p_FrameRateRange = xml_node_soap_get(p_node, "FrameRateRange");
    if (p_FrameRateRange)
    {
        parse_IntRange(p_FrameRateRange, &p_res->FrameRateRange);
    }

    p_EncodingIntervalRange = xml_node_soap_get(p_node, "EncodingIntervalRange");
    if (p_EncodingIntervalRange)
    {
        parse_IntRange(p_EncodingIntervalRange, &p_res->EncodingIntervalRange);
    }

    p_H264ProfilesSupported = xml_node_soap_get(p_node, "H264ProfilesSupported");
    while (p_H264ProfilesSupported && p_H264ProfilesSupported->data && soap_strcmp(p_H264ProfilesSupported->name, "H264ProfilesSupported") == 0)
    {
        if (strcasecmp(p_H264ProfilesSupported->data, "Baseline") == 0)
        {
            p_res->H264Profile_Baseline = 1;
        }
        else if (strcasecmp(p_H264ProfilesSupported->data, "Main") == 0)
        {
            p_res->H264Profile_Main = 1;
        }
        else if (strcasecmp(p_H264ProfilesSupported->data, "High") == 0)
        {
            p_res->H264Profile_High = 1;
        }
        else if (strcasecmp(p_H264ProfilesSupported->data, "Extended") == 0)
        {
            p_res->H264Profile_Extended = 1;
        }
        
        p_H264ProfilesSupported = p_H264ProfilesSupported->next;
    }

    return TRUE;
}

BOOL parse_AudioOutputConfiguration(XMLN * p_node, onvif_AudioOutputConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_OutputToken;
    XMLN * p_SendPrimacy;
    XMLN * p_OutputLevel;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_OutputToken = xml_node_soap_get(p_node, "OutputToken");
    if (p_OutputToken && p_OutputToken->data)
    {
        strncpy(p_res->OutputToken, p_OutputToken->data, sizeof(p_res->OutputToken)-1);
    }

    p_SendPrimacy = xml_node_soap_get(p_node, "SendPrimacy");
    if (p_SendPrimacy && p_SendPrimacy->data)
    {
        p_res->SendPrimacyFlag = 1;
        strncpy(p_res->SendPrimacy, p_SendPrimacy->data, sizeof(p_res->SendPrimacy)-1);
    }

    p_OutputLevel = xml_node_soap_get(p_node, "OutputLevel");
    if (p_OutputLevel && p_OutputLevel->data)
    {
        p_res->OutputLevel = atoi(p_OutputLevel->data);
    }

    return TRUE;
}

BOOL parse_AudioOutputConfigurationOptions(XMLN * p_node, onvif_AudioOutputConfigurationOptions * p_res)
{
    XMLN * p_OutputTokensAvailable;
    XMLN * p_SendPrimacyOptions;
    XMLN * p_OutputLevelRange;

    p_res->sizeOutputTokensAvailable = 0;
    
    p_OutputTokensAvailable = xml_node_soap_get(p_node, "OutputTokensAvailable");
    while (p_OutputTokensAvailable && p_OutputTokensAvailable->data && soap_strcmp(p_OutputTokensAvailable->name, "OutputTokensAvailable") == 0)
    {
        uint32 idx = p_res->sizeOutputTokensAvailable;

        strncpy(p_res->OutputTokensAvailable[idx], p_OutputTokensAvailable->data, sizeof(p_res->OutputTokensAvailable[idx]));

        p_res->sizeOutputTokensAvailable++;
        if (p_res->sizeOutputTokensAvailable >= ARRAY_SIZE(p_res->OutputTokensAvailable))
        {
            break;
        }
        
        p_OutputTokensAvailable = p_OutputTokensAvailable->next;
    }

    p_res->sizeSendPrimacyOptions = 0;
    
    p_SendPrimacyOptions = xml_node_soap_get(p_node, "SendPrimacyOptions");
    while (p_SendPrimacyOptions && p_SendPrimacyOptions->data && soap_strcmp(p_SendPrimacyOptions->name, "SendPrimacyOptions") == 0)
    {
        uint32 idx = p_res->sizeSendPrimacyOptions;

        strncpy(p_res->SendPrimacyOptions[idx], p_SendPrimacyOptions->data, sizeof(p_res->SendPrimacyOptions[idx]));

        p_res->sizeSendPrimacyOptions++;
        if (p_res->sizeSendPrimacyOptions >= ARRAY_SIZE(p_res->SendPrimacyOptions))
        {
            break;
        }
        
        p_SendPrimacyOptions = p_SendPrimacyOptions->next;
    }
    
    p_OutputLevelRange = xml_node_soap_get(p_node, "OutputLevelRange");
    if (p_OutputLevelRange)
    {
        parse_IntRange(p_OutputLevelRange, &p_res->OutputLevelRange);
    }

    return TRUE;
}

BOOL parse_AudioDecoderConfiguration(XMLN * p_node, onvif_AudioDecoderConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }
    
    return TRUE;
}

BOOL parse_AACDecOptions(XMLN * p_node, onvif_AACDecOptions * p_res)
{
    XMLN * p_Bitrate;
    XMLN * p_SampleRateRange;
    
    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate)    
    {
        parse_IntList(p_Bitrate, &p_res->Bitrate);
    }

    p_SampleRateRange = xml_node_soap_get(p_node, "SampleRateRange");
    if (p_SampleRateRange)    
    {
        parse_IntList(p_SampleRateRange, &p_res->SampleRateRange);
    }

    return TRUE;
}

BOOL parse_G711DecOptions(XMLN * p_node, onvif_G711DecOptions * p_res)
{
    XMLN * p_Bitrate;
    XMLN * p_SampleRateRange;
    
    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate)    
    {
        parse_IntList(p_Bitrate, &p_res->Bitrate);
    }

    p_SampleRateRange = xml_node_soap_get(p_node, "SampleRateRange");
    if (p_SampleRateRange)    
    {
        parse_IntList(p_SampleRateRange, &p_res->SampleRateRange);
    }

    return TRUE;
}

BOOL parse_G726DecOptions(XMLN * p_node, onvif_G726DecOptions * p_res)
{
    XMLN * p_Bitrate;
    XMLN * p_SampleRateRange;
    
    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate)    
    {
        parse_IntList(p_Bitrate, &p_res->Bitrate);
    }

    p_SampleRateRange = xml_node_soap_get(p_node, "SampleRateRange");
    if (p_SampleRateRange)    
    {
        parse_IntList(p_SampleRateRange, &p_res->SampleRateRange);
    }

    return TRUE;
}

BOOL parse_AudioDecoderConfigurationOptions(XMLN * p_node, onvif_AudioDecoderConfigurationOptions * p_res)
{
    XMLN * p_AACDecOptions;
    XMLN * p_G711DecOptions;
    XMLN * p_G726DecOptions;

    p_AACDecOptions = xml_node_soap_get(p_node, "AACDecOptions");
    if (p_AACDecOptions)
    {
        p_res->AACDecOptionsFlag = parse_AACDecOptions(p_AACDecOptions, &p_res->AACDecOptions);
    }

    p_G711DecOptions = xml_node_soap_get(p_node, "G711DecOptions");
    if (p_G711DecOptions)
    {
        p_res->G711DecOptionsFlag = parse_G711DecOptions(p_G711DecOptions, &p_res->G711DecOptions);
    }

    p_G726DecOptions = xml_node_soap_get(p_node, "G726DecOptions");
    if (p_G726DecOptions)
    {
        p_res->G726DecOptionsFlag = parse_G726DecOptions(p_G726DecOptions, &p_res->G726DecOptions);
    }

    return TRUE;
}

BOOL parse_VideoSourceMode(XMLN * p_node, onvif_VideoSourceMode * p_res)
{
    XMLN * p_MaxFramerate;
    XMLN * p_MaxResolution;
    XMLN * p_Encodings;
    XMLN * p_Reboot;
    XMLN * p_Description;
    const char * p_token;
    const char * p_Enabled;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Enabled = xml_attr_get(p_node, "Enabled");
    if (p_Enabled)
    {
        p_res->Enabled = parse_Bool(p_Enabled);
    }
    
    p_MaxFramerate = xml_node_soap_get(p_node, "MaxFramerate");
    if (p_MaxFramerate && p_MaxFramerate->data)
    {
        p_res->MaxFramerate = (float)atof(p_MaxFramerate->data);
    }

    p_MaxResolution = xml_node_soap_get(p_node, "MaxResolution");
    if (p_MaxResolution)
    {
        parse_VideoResolution(p_MaxResolution, &p_res->MaxResolution);
    }

    p_Encodings = xml_node_soap_get(p_node, "Encodings");
    if (p_Encodings && p_Encodings->data)
    {
        strncpy(p_res->Encodings, p_Encodings->data, sizeof(p_res->Encodings)-1);
    }

    p_Reboot = xml_node_soap_get(p_node, "Reboot");
    if (p_Reboot && p_Reboot->data)
    {
        p_res->Reboot = parse_Bool(p_Reboot->data);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;    
}

BOOL parse_Color(XMLN * p_node, onvif_Color * p_res)
{
    const char * p_X;
    const char * p_Y;
    const char * p_Z;
    const char * p_Colorspace;
    const char * p_Likelihood;

    p_X = xml_attr_get(p_node, "X");
    if (p_X)
    {
        p_res->X = (float)atof(p_X);
    }

    p_Y = xml_attr_get(p_node, "Y");
    if (p_Y)
    {
        p_res->Y = (float)atof(p_Y);
    }

    p_Z = xml_attr_get(p_node, "Z");
    if (p_Z)
    {
        p_res->Z = (float)atof(p_Z);
    }
    
    p_Colorspace = xml_attr_get(p_node, "Colorspace");
    if (p_Colorspace)
    {
        p_res->ColorspaceFlag = 1;
        strncpy(p_res->Colorspace, p_Colorspace, sizeof(p_res->Colorspace)-1);
    }

    p_Likelihood = xml_attr_get(p_node, "Likelihood");
    if (p_Likelihood)
    {
        p_res->LikelihoodFlag = 1;
        p_res->Likelihood = (float) atof(p_Likelihood);
    }

    return TRUE;
}

BOOL parse_ColorspaceRange(XMLN * p_node, onvif_ColorspaceRange * p_res)
{
    XMLN * p_X;
    XMLN * p_Y;
    XMLN * p_Z;
    XMLN * p_Colorspace;
    
    p_X = xml_node_soap_get(p_node, "X");
    if (p_X)
    {
        parse_FloatRange(p_X, &p_res->X);
    }

    p_Y = xml_node_soap_get(p_node, "Y");
    if (p_Y)
    {
        parse_FloatRange(p_Y, &p_res->Y);
    }

    p_Z = xml_node_soap_get(p_node, "Z");
    if (p_Z)
    {
        parse_FloatRange(p_Z, &p_res->Z);
    }

    p_Colorspace = xml_node_soap_get(p_node, "Colorspace");
    if (p_Colorspace && p_Colorspace->data)
    {
        strncpy(p_res->Colorspace, p_Colorspace->data, sizeof(p_res->Colorspace)-1);
    }

    return TRUE;
}

BOOL parse_ColorOptions(XMLN * p_node, onvif_ColorOptions * p_res)
{
    XMLN * p_ColorList;
    XMLN * p_ColorspaceRange;

    p_res->sizeColorList = 0;
    
    p_ColorList = xml_node_soap_get(p_node, "ColorList");
    while (p_ColorList && soap_strcmp(p_ColorList->name, "ColorList") == 0)
    {
        uint32 index = p_res->sizeColorList;
        if (index < ARRAY_SIZE(p_res->ColorList))
        {
            p_res->sizeColorList++;
            
            parse_Color(p_ColorList, &p_res->ColorList[index]);
        }
        
        p_ColorList = p_ColorList->next;
    }

    p_res->sizeColorspaceRange = 0;
    
    p_ColorspaceRange = xml_node_soap_get(p_node, "ColorspaceRange");
    while (p_ColorspaceRange && soap_strcmp(p_ColorspaceRange->name, "ColorspaceRange") == 0)
    {
        uint32 index = p_res->sizeColorspaceRange;
        if (index < ARRAY_SIZE(p_res->ColorspaceRange))
        {
            p_res->sizeColorspaceRange++;

            parse_ColorspaceRange(p_ColorspaceRange, &p_res->ColorspaceRange[index]);
        }
        
        p_ColorspaceRange = p_ColorspaceRange->next;
    }

    return TRUE;
}

BOOL parse_OSDColorOptions(XMLN * p_node, onvif_OSDColorOptions * p_res)
{
    XMLN * p_Color;
    XMLN * p_Transparent;

    p_Color = xml_node_soap_get(p_node, "Color");
    if (p_Color)
    {
        p_res->ColorFlag = parse_ColorOptions(p_Color, &p_res->Color);        
    }

    p_Transparent = xml_node_soap_get(p_node, "Transparent");
    if (p_Transparent)
    {
        p_res->TransparentFlag = parse_IntRange(p_Transparent, &p_res->Transparent);
    }

    return TRUE;
}

BOOL parse_OSDTextOptions(XMLN * p_node, onvif_OSDTextOptions * p_res)
{
    XMLN * p_Type;
    XMLN * p_FontSizeRange;
    XMLN * p_DateFormat;
    XMLN * p_TimeFormat;
    XMLN * p_FontColor;
    XMLN * p_BackgroundColor;

    p_Type = xml_node_soap_get(p_node, "Type");
    while (p_Type && p_Type->data && soap_strcmp(p_Type->name, "Type") == 0)
    {
        if (soap_strcmp(p_Type->data, "Plain") == 0)
        {
            p_res->OSDTextType_Plain = 1;
        }
        else if (soap_strcmp(p_Type->data, "Date") == 0)
        {
            p_res->OSDTextType_Date = 1;
        }
        else if (soap_strcmp(p_Type->data, "Time") == 0)
        {
            p_res->OSDTextType_Time = 1;
        }
        else if (soap_strcmp(p_Type->data, "DateAndTime") == 0)
        {
            p_res->OSDTextType_DateAndTime = 1;
        }
        
        p_Type = p_Type->next;
    }

    p_FontSizeRange = xml_node_soap_get(p_node, "FontSizeRange");
    if (p_FontSizeRange)
    {
        p_res->FontSizeRangeFlag = parse_IntRange(p_FontSizeRange, &p_res->FontSizeRange);
    }

    p_res->DateFormatSize = 0;
    
    p_DateFormat = xml_node_soap_get(p_node, "DateFormat");
    while (p_DateFormat && p_DateFormat->data && soap_strcmp(p_DateFormat->name, "DateFormat") == 0)
    {
        uint32 index = p_res->DateFormatSize;                
        if (index < ARRAY_SIZE(p_res->DateFormat))
        {
            p_res->DateFormatSize++;
            strncpy(p_res->DateFormat[index], p_DateFormat->data, sizeof(p_res->DateFormat[0])-1);
        }            
        
        p_DateFormat = p_DateFormat->next;
    }

    p_res->TimeFormatSize = 0;
    
    p_TimeFormat = xml_node_soap_get(p_node, "TimeFormat");
    while (p_TimeFormat && p_TimeFormat->data && soap_strcmp(p_TimeFormat->name, "TimeFormat") == 0)
    {
        uint32 index = p_res->TimeFormatSize;
        if (index < ARRAY_SIZE(p_res->TimeFormat))
        {
            p_res->TimeFormatSize++;
            strncpy(p_res->TimeFormat[index], p_TimeFormat->data, sizeof(p_res->TimeFormat[0])-1);
        }
        
        p_TimeFormat = p_TimeFormat->next;
    }

    p_FontColor = xml_node_soap_get(p_node, "FontColor");
    if (p_FontColor)
    {
        p_res->FontColorFlag = parse_OSDColorOptions(p_FontColor, &p_res->FontColor);
    }

    p_BackgroundColor = xml_node_soap_get(p_node, "BackgroundColor");
    if (p_BackgroundColor)
    {
        p_res->BackgroundColorFlag = parse_OSDColorOptions(p_BackgroundColor, &p_res->BackgroundColor);                
    }

    return TRUE;
}

BOOL parse_OSDImgOptions(XMLN * p_node, onvif_OSDImgOptions * p_res)
{
    XMLN * p_ImagePath;

    p_res->ImagePathSize = 0;
    
    p_ImagePath = xml_node_soap_get(p_node, "ImagePath");
    while (p_ImagePath && p_ImagePath->data && soap_strcmp(p_ImagePath->name, "ImagePath") == 0)
    {
        uint32 index = p_res->ImagePathSize;
        if (index < ARRAY_SIZE(p_res->ImagePath))
        {
            p_res->ImagePathSize++;
            strncpy(p_res->ImagePath[index], p_ImagePath->data, sizeof(p_res->ImagePath[0])-1);
        }
        
        p_ImagePath = p_ImagePath->next;
    }

    return TRUE;
}

BOOL parse_MaximumNumberOfOSDs(XMLN * p_node, onvif_MaximumNumberOfOSDs * p_res)
{
    const char * p_Total;
    const char * p_Image;
    const char * p_PlainText;
    const char * p_Date;
    const char * p_Time;
    const char * p_DateAndTime;

    p_Total = xml_attr_get(p_node, "Total");
    if (p_Total)
    {
        p_res->Total = atoi(p_Total);
    }

    p_Image = xml_attr_get(p_node, "Image");
    if (p_Image)
    {
        p_res->ImageFlag = 1;
        p_res->Image = atoi(p_Image);
    }

    p_PlainText = xml_attr_get(p_node, "PlainText");
    if (p_PlainText)
    {
        p_res->PlainTextFlag = 1;
        p_res->PlainText = atoi(p_PlainText);
    }

    p_Date = xml_attr_get(p_node, "Date");
    if (p_Date)
    {
        p_res->DateFlag = 1;
        p_res->Date = atoi(p_Date);
    }

    p_Time = xml_attr_get(p_node, "Time");
    if (p_Time)
    {
        p_res->TimeFlag = 1;
        p_res->Time = atoi(p_Time);
    }

    p_DateAndTime = xml_attr_get(p_node, "DateAndTime");
    if (p_DateAndTime)
    {
        p_res->DateAndTimeFlag = 1;
        p_res->DateAndTime = atoi(p_DateAndTime);
    }

    return TRUE;
}

BOOL parse_OSDOptions(XMLN * p_node, onvif_OSDConfigurationOptions * p_res)
{
    XMLN * p_MaximumNumberOfOSDs;
    XMLN * p_Type;
    XMLN * p_PositionOption;
    XMLN * p_TextOption;
    XMLN * p_ImageOption;

    p_MaximumNumberOfOSDs = xml_node_soap_get(p_node, "MaximumNumberOfOSDs");
    if (p_MaximumNumberOfOSDs)
    {
        parse_MaximumNumberOfOSDs(p_MaximumNumberOfOSDs, &p_res->MaximumNumberOfOSDs);
    }

    p_Type = xml_node_soap_get(p_node, "Type");
    while (p_Type && p_Type->data && soap_strcmp(p_Type->name, "Type") == 0)
    {
        if (soap_strcmp(p_Type->data, "Text") == 0)
        {
            p_res->OSDType_Text = 1;
        }
        else if (soap_strcmp(p_Type->data, "Image") == 0)
        {
            p_res->OSDType_Image = 1;
        }
        else if (soap_strcmp(p_Type->data, "Extended") == 0)
        {
            p_res->OSDType_Extended = 1;
        }
        
        p_Type = p_Type->next;
    }

    p_PositionOption = xml_node_soap_get(p_node, "PositionOption");
    while (p_PositionOption && p_PositionOption->data && 
           soap_strcmp(p_PositionOption->name, "PositionOption") == 0)
    {
        if (soap_strcmp(p_PositionOption->data, "UpperLeft") == 0)
        {
            p_res->OSDPosType_UpperLeft = 1;
        }
        else if (soap_strcmp(p_PositionOption->data, "UpperRight") == 0)
        {
            p_res->OSDPosType_UpperRight = 1;
        }
        else if (soap_strcmp(p_PositionOption->data, "LowerLeft") == 0)
        {
            p_res->OSDPosType_LowerLeft = 1;
        }
        else if (soap_strcmp(p_PositionOption->data, "LowerRight") == 0)
        {
            p_res->OSDPosType_LowerRight = 1;
        }
        else if (soap_strcmp(p_PositionOption->data, "Custom") == 0)
        {
            p_res->OSDPosType_Custom = 1;
        }
        
        p_PositionOption = p_PositionOption->next;
    }

    p_TextOption = xml_node_soap_get(p_node, "TextOption");
    if (p_TextOption)
    {
        p_res->TextOptionFlag = parse_OSDTextOptions(p_TextOption, &p_res->TextOption);
    }

    p_ImageOption = xml_node_soap_get(p_node, "ImageOption");
    if (p_ImageOption)
    {
        p_res->ImageOptionFlag = parse_OSDImgOptions(p_ImageOption, &p_res->ImageOption);
    }

    return TRUE;
}

BOOL parse_OSDColor(XMLN * p_node, onvif_OSDColor * p_res)
{
    XMLN * p_Color;    
    const char * p_Transparent;

    p_Transparent = xml_attr_get(p_node, "Transparent");
    if (p_Transparent)
    {
        p_res->TransparentFlag = 1;
        p_res->Transparent = atoi(p_Transparent);
    }
            
    p_Color = xml_node_soap_get(p_node, "Color");
    if (p_Color)
    {
        const char * p_X;
        const char * p_Y;
        const char * p_Z;
        const char * p_Colorspace;

        p_X = xml_attr_get(p_Color, "X");
        if (p_X)
        {
            p_res->X = (float)atof(p_X);
        }

        p_Y = xml_attr_get(p_Color, "Y");
        if (p_Y)
        {
            p_res->Y = (float)atof(p_Y);
        }

        p_Z = xml_attr_get(p_Color, "Z");
        if (p_Z)
        {
            p_res->Z = (float)atof(p_Z);
        }

        p_Colorspace = xml_attr_get(p_Color, "Colorspace");
        if (p_Colorspace)
        {
            p_res->ColorspaceFlag = 1;
            strncpy(p_res->Colorspace, p_Colorspace, sizeof(p_res->Colorspace)-1);
        }
    }

    return TRUE;
}

BOOL parse_OSDPosConfiguration(XMLN * p_node, onvif_OSDPosConfiguration * p_res)
{
    XMLN * p_Type;
    XMLN * p_Pos;

    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type && p_Type->data)
    {
        p_res->Type = onvif_StringToOSDPosType(p_Type->data);
    }

    p_Pos = xml_node_soap_get(p_node, "Pos");
    if (p_Pos)
    {
        p_res->PosFlag = parse_Vector(p_Pos, &p_res->Pos);
    }

    return TRUE;
}

BOOL parse_OSDTextConfiguration(XMLN * p_node, onvif_OSDTextConfiguration * p_res)
{
    XMLN * p_Type;
    XMLN * p_DateFormat;
    XMLN * p_TimeFormat;
    XMLN * p_FontSize;
    XMLN * p_FontColor;
    XMLN * p_BackgroundColor;
    XMLN * p_PlainText;
    
    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type && p_Type->data)
    {
        p_res->Type = onvif_StringToOSDTextType(p_Type->data);
    }

    p_DateFormat = xml_node_soap_get(p_node, "DateFormat");
    if (p_DateFormat && p_DateFormat->data)
    {
        p_res->DateFormatFlag = 1;
        strncpy(p_res->DateFormat, p_DateFormat->data, sizeof(p_res->DateFormat)-1);
    }

    p_TimeFormat = xml_node_soap_get(p_node, "TimeFormat");
    if (p_TimeFormat && p_TimeFormat->data)
    {
        p_res->TimeFormatFlag = 1;
        strncpy(p_res->TimeFormat, p_TimeFormat->data, sizeof(p_res->TimeFormat)-1);
    }

    p_FontSize = xml_node_soap_get(p_node, "FontSize");
    if (p_FontSize && p_FontSize->data)
    {
        p_res->FontSizeFlag = 1;
        p_res->FontSize = atoi(p_FontSize->data);
    }

    p_FontColor = xml_node_soap_get(p_node, "FontColor");
    if (p_FontColor)
    {
        p_res->FontColorFlag = parse_OSDColor(p_FontColor, &p_res->FontColor);
    }

    p_BackgroundColor = xml_node_soap_get(p_node, "BackgroundColor");
    if (p_BackgroundColor)
    {
        p_res->BackgroundColorFlag = parse_OSDColor(p_BackgroundColor, &p_res->BackgroundColor);
    }

    p_PlainText = xml_node_soap_get(p_node, "PlainText");
    if (p_PlainText && p_PlainText->data)
    {
        p_res->PlainTextFlag = 1;
        strncpy(p_res->PlainText, p_PlainText->data, sizeof(p_res->PlainText)-1);
    }

    return TRUE;
}

BOOL parse_OSDImgConfiguration(XMLN * p_node, onvif_OSDImgConfiguration * p_res)
{
    XMLN * p_ImgPath;
    
    p_ImgPath = xml_node_soap_get(p_node, "ImgPath");
    if (p_ImgPath && p_ImgPath->data)
    {
        strncpy(p_res->ImgPath, p_ImgPath->data, sizeof(p_res->ImgPath)-1);
    }

    return TRUE;
}

BOOL parse_OSDConfiguration(XMLN * p_node, onvif_OSDConfiguration * p_res)
{
    XMLN * p_VideoSourceConfigurationToken;
    XMLN * p_Type;
    XMLN * p_Position;
    XMLN * p_TextString;
    XMLN * p_Image;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
            
    p_VideoSourceConfigurationToken = xml_node_soap_get(p_node, "VideoSourceConfigurationToken");
    if (p_VideoSourceConfigurationToken && p_VideoSourceConfigurationToken->data)
    {
        strncpy(p_res->VideoSourceConfigurationToken, 
            p_VideoSourceConfigurationToken->data, 
            sizeof(p_res->VideoSourceConfigurationToken)-1);
    }

    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type && p_Type->data)
    {
        p_res->Type = onvif_StringToOSDType(p_Type->data);
    }

    p_Position = xml_node_soap_get(p_node, "Position");
    if (p_Position)
    {
        parse_OSDPosConfiguration(p_Position, &p_res->Position);
    }

    p_TextString = xml_node_soap_get(p_node, "TextString");
    if (p_TextString)
    {
        p_res->TextStringFlag = parse_OSDTextConfiguration(p_TextString, &p_res->TextString);
    }

    p_Image = xml_node_soap_get(p_node, "Image");
    if (p_Image)
    {
        p_res->ImageFlag = parse_OSDImgConfiguration(p_Image, &p_res->Image);
    }
    
    return TRUE;
}

BOOL parse_PTZStatusFilterOptions(XMLN * p_node, onvif_PTZStatusFilterOptions * p_res)
{
    XMLN * p_PanTiltStatusSupported;
    XMLN * p_ZoomStatusSupported;
    XMLN * p_PanTiltPositionSupported;
    XMLN * p_ZoomPositionSupported;

    p_PanTiltStatusSupported = xml_node_soap_get(p_node, "PanTiltStatusSupported");
    if (p_PanTiltStatusSupported && p_PanTiltStatusSupported->data)
    {
        p_res->PanTiltStatusSupported = parse_Bool(p_PanTiltStatusSupported->data);
    }

    p_ZoomStatusSupported = xml_node_soap_get(p_node, "ZoomStatusSupported");
    if (p_ZoomStatusSupported && p_ZoomStatusSupported->data)
    {
        p_res->ZoomStatusSupported = parse_Bool(p_ZoomStatusSupported->data);
    }

    p_PanTiltPositionSupported = xml_node_soap_get(p_node, "PanTiltPositionSupported");
    if (p_PanTiltPositionSupported && p_PanTiltPositionSupported->data)
    {
        p_res->PanTiltPositionSupported = parse_Bool(p_PanTiltPositionSupported->data);
    }

    p_ZoomPositionSupported = xml_node_soap_get(p_node, "ZoomPositionSupported");
    if (p_ZoomPositionSupported && p_ZoomPositionSupported->data)
    {
        p_res->ZoomPositionSupported = parse_Bool(p_ZoomPositionSupported->data);
    }
    
    return TRUE;
}

BOOL parse_MetadataConfigurationOptionsExtension(XMLN * p_node, onvif_MetadataConfigurationOptionsExtension * p_res)
{
    XMLN * p_CompressionType;

    p_res->sizeCompressionType = 0;
    
    p_CompressionType = xml_node_soap_get(p_node, "CompressionType");
    while (p_CompressionType && p_CompressionType->data && 
        soap_strcmp(p_CompressionType->name, "CompressionType") == 0)
    {
        uint32 idx = p_res->sizeCompressionType;

        strncpy(p_res->CompressionType[idx], 
            p_CompressionType->data, 
            sizeof(p_res->CompressionType[idx])-1);

        p_res->sizeCompressionType++;
        if (p_res->sizeCompressionType >= ARRAY_SIZE(p_res->CompressionType))
        {
            break;
        }
        
        p_CompressionType = p_CompressionType->next;
    }

    return TRUE;
}

BOOL parse_MetadataConfigurationOptions(XMLN * p_node, onvif_MetadataConfigurationOptions * p_res)
{
    XMLN * p_PTZStatusFilterOptions;
    XMLN * p_Extension;
    const char * p_GeoLocation;

    p_GeoLocation = xml_attr_get(p_node, "GeoLocation");
    if (p_GeoLocation)
    {
        p_res->GeoLocation = parse_Bool(p_GeoLocation);
    }

    p_PTZStatusFilterOptions = xml_node_soap_get(p_node, "PTZStatusFilterOptions");
    if (p_PTZStatusFilterOptions)
    {
        parse_PTZStatusFilterOptions(p_PTZStatusFilterOptions, &p_res->PTZStatusFilterOptions);
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        p_res->ExtensionFlag = parse_MetadataConfigurationOptionsExtension(p_Extension, &p_res->Extension);
    }

    return TRUE;
}

BOOL parse_IntRectangleRange(XMLN * p_node, onvif_IntRectangleRange * p_res)
{
    XMLN * p_XRange;
    XMLN * p_YRange;
    XMLN * p_WidthRange;
    XMLN * p_HeightRange;

    p_XRange = xml_node_soap_get(p_node, "XRange");
    if (p_XRange)
    {
        parse_IntRange(p_XRange, &p_res->XRange);
    }
    
    p_YRange = xml_node_soap_get(p_node, "YRange");
    if (p_YRange)
    {
        parse_IntRange(p_YRange, &p_res->YRange);
    }

    p_WidthRange = xml_node_soap_get(p_node, "WidthRange");
    if (p_WidthRange)
    {
        parse_IntRange(p_WidthRange, &p_res->WidthRange);
    }

    p_HeightRange = xml_node_soap_get(p_node, "HeightRange");
    if (p_HeightRange)
    {
        parse_IntRange(p_HeightRange, &p_res->HeightRange);
    }

    return TRUE;
}

BOOL parse_RotateOptions(XMLN * p_node, onvif_RotateOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_DegreeList;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (strcasecmp(p_Mode->data, "OFF") == 0)
        {
            p_res->RotateMode_OFF = 1;
        }
        else if (strcasecmp(p_Mode->data, "ON") == 0)
        {
            p_res->RotateMode_ON = 1;
        }
        else if (strcasecmp(p_Mode->data, "AUTO") == 0)
        {
            p_res->RotateMode_AUTO = 1;
        }
        
        p_Mode = p_Mode->next;
    }

    p_DegreeList = xml_node_soap_get(p_node, "DegreeList");
    if (p_DegreeList)
    {
        XMLN * p_Items;
        
        p_res->sizeDegreeList = 0;
        
        p_Items = xml_node_soap_get(p_DegreeList, "Items"); 
        while (p_Items && p_Items->data && soap_strcmp(p_Items->name, "Items") == 0)
        {
            uint32 idx = p_res->sizeDegreeList;
            if (idx < ARRAY_SIZE(p_res->DegreeList))
            {
                p_res->DegreeList[idx] = atoi(p_Items->data);
                p_res->sizeDegreeList++;
            }
            
            p_Items = p_Items->next;
        }
    }

    return TRUE;
}

BOOL parse_VideoSourceConfigurationOptionsExtension(XMLN * p_node, onvif_VideoSourceConfigurationOptionsExtension * p_res)
{
    XMLN * p_Rotate;
        
    p_Rotate = xml_node_soap_get(p_node, "Rotate");
    if (p_Rotate)
    {
        const char * p_Reboot = xml_attr_get(p_Rotate, "Reboot");
        if (p_Reboot)
        {
            p_res->Rotate.Reboot = parse_Bool(p_Reboot);
        }
        
        p_res->RotateFlag = parse_RotateOptions(p_Rotate, &p_res->Rotate);
    }

    return TRUE;
}

BOOL parse_VideoEncoderOptionsExtension(XMLN * p_node, onvif_VideoEncoderOptionsExtension * p_res)
{
    XMLN * p_JPEG;
    XMLN * p_MPEG4;
    XMLN * p_H264;
    XMLN * p_BitrateRange;

    p_JPEG = xml_node_soap_get(p_node, "JPEG");
    if (p_JPEG)
    {
        p_res->JPEGFlag = parse_JPEGOptions(p_JPEG, &p_res->JPEG.JpegOptions);

        p_BitrateRange = xml_node_soap_get(p_JPEG, "BitrateRange");
        if (p_BitrateRange)
        {
            parse_IntRange(p_BitrateRange, &p_res->JPEG.BitrateRange);            
        }
    }

    p_MPEG4 = xml_node_soap_get(p_node, "MPEG4");
    if (p_MPEG4)
    {
        p_res->MPEG4Flag = parse_MPEG4Options(p_MPEG4, &p_res->MPEG4.Mpeg4Options);

        p_BitrateRange = xml_node_soap_get(p_MPEG4, "BitrateRange");
        if (p_BitrateRange)
        {
            parse_IntRange(p_BitrateRange, &p_res->MPEG4.BitrateRange);            
        }
    }    

    p_H264 = xml_node_soap_get(p_node, "H264");
    if (p_H264)
    {
        p_res->H264Flag = parse_H264Options(p_H264, &p_res->H264.H264Options);

        p_BitrateRange = xml_node_soap_get(p_H264, "BitrateRange");
        if (p_BitrateRange)
        {
            parse_IntRange(p_BitrateRange, &p_res->H264.BitrateRange);            
        }
    }

    return TRUE;
}

BOOL parse_AudioEncoderConfigurationOption(XMLN * p_node, onvif_AudioEncoderConfigurationOption * p_res)
{
    XMLN * p_Encoding;
    XMLN * p_BitrateList;
    XMLN * p_SampleRateList;
    
    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        p_res->Encoding = onvif_StringToAudioEncoding(p_Encoding->data);                
    }

    p_BitrateList = xml_node_soap_get(p_node, "BitrateList");
    if (p_BitrateList)
    {
        parse_IntList(p_BitrateList, &p_res->BitrateList);
    }

    p_SampleRateList = xml_node_soap_get(p_node, "SampleRateList");
    if (p_SampleRateList)
    {
        parse_IntList(p_SampleRateList, &p_res->SampleRateList);
    }

    return TRUE;
}

BOOL parse_trt_GetServiceCapabilities(XMLN * p_node, trt_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_MediaServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_trt_GetVideoSources(XMLN * p_node, trt_GetVideoSources_RES * p_res)
{
    XMLN * p_VideoSources;

    p_VideoSources = xml_node_soap_get(p_node, "VideoSources");
    while (p_VideoSources && soap_strcmp(p_VideoSources->name, "VideoSources") == 0)
    {
        VideoSourceList * p_v_src = onvif_add_VideoSource(&p_res->VideoSources);
        if (p_v_src)
        {
            const char * p_token = xml_attr_get(p_VideoSources, "token");
            if (p_token)
            {
                strncpy(p_v_src->VideoSource.token, p_token, sizeof(p_v_src->VideoSource.token)-1);
            }
            
            if (!parse_VideoSource(p_VideoSources, &p_v_src->VideoSource))
            {
                onvif_free_VideoSources(&p_res->VideoSources);
                return FALSE;
            }
        }

        p_VideoSources = p_VideoSources->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioSources(XMLN * p_node, trt_GetAudioSources_RES * p_res)
{
    XMLN * p_AudioSources;

    p_AudioSources = xml_node_soap_get(p_node, "AudioSources");
    while (p_AudioSources)
    {
        AudioSourceList * p_a_src = onvif_add_AudioSource(&p_res->AudioSources);
        if (p_a_src)
        {
            const char * p_token = xml_attr_get(p_AudioSources, "token");
            if (p_token)
            {
                strncpy(p_a_src->AudioSource.token, p_token, sizeof(p_a_src->AudioSource.token)-1);
            }
            
            if (!parse_AudioSource(p_AudioSources, &p_a_src->AudioSource))
            {
                onvif_free_AudioSources(&p_res->AudioSources);
                return FALSE;
            }
        }

        p_AudioSources = p_AudioSources->next;
    }
    
    return TRUE;
}

BOOL parse_trt_CreateProfile(XMLN * p_node, trt_CreateProfile_RES * p_res)
{
    XMLN * p_Profile;

    p_Profile = xml_node_soap_get(p_node, "Profile");
    if (p_Profile)
    {
        const char * p_fixed;
        const char * p_token;

        p_fixed = xml_attr_get(p_Profile, "fixed");
        if (p_fixed)
        {
            p_res->Profile.fixed = parse_Bool(p_fixed);
        }

        p_token = xml_attr_get(p_Profile, "token");
        if (p_token)
        {
            strncpy(p_res->Profile.token, p_token, sizeof(p_res->Profile.token)-1);
        }
        
        if (!parse_Profile(p_Profile, &p_res->Profile))
        {
            return FALSE;
        }
    }
    
    return TRUE;
}

BOOL parse_trt_GetProfile(XMLN * p_node, trt_GetProfile_RES * p_res)
{
    XMLN * p_Profile;

    p_Profile = xml_node_soap_get(p_node, "Profile");
    if (p_Profile)
    {
        const char * p_fixed;
        const char * p_token;

        p_fixed = xml_attr_get(p_Profile, "fixed");
        if (p_fixed)
        {
            p_res->Profile.fixed = parse_Bool(p_fixed);
        }

        p_token = xml_attr_get(p_Profile, "token");
        if (p_token)
        {
            strncpy(p_res->Profile.token, p_token, sizeof(p_res->Profile.token)-1);
        }
        
        if (!parse_Profile(p_Profile, &p_res->Profile))
        {
            onvif_free_profile(&p_res->Profile);
            return FALSE;
        }
    }
    
    return TRUE;
}

BOOL parse_trt_GetProfiles(XMLN * p_node, trt_GetProfiles_RES * p_res)
{
    XMLN * p_Profiles;

    p_Profiles = xml_node_soap_get(p_node, "Profiles");
    while (p_Profiles)
    {
        ONVIF_PROFILE * p_profile = onvif_add_profile(&p_res->Profiles);
        if (p_profile)
        {
            const char * p_fixed;
            const char * p_token;

            p_fixed = xml_attr_get(p_Profiles, "fixed");
            if (p_fixed)
            {
                p_profile->fixed = parse_Bool(p_fixed);
            }

            p_token = xml_attr_get(p_Profiles, "token");
            if (p_token)
            {
                strncpy(p_profile->token, p_token, sizeof(p_profile->token)-1);
            }
            
            if (!parse_Profile(p_Profiles, p_profile))
            {
                onvif_free_profiles(&p_res->Profiles);
                return FALSE;
            }
        }

        p_Profiles = p_Profiles->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetVideoSourceModes(XMLN * p_node, trt_GetVideoSourceModes_RES * p_res)
{
    XMLN * p_VideoSourceModes;

    p_VideoSourceModes = xml_node_soap_get(p_node, "VideoSourceModes");
    while (p_VideoSourceModes)
    {
        VideoSourceModeList * p_v_src_mode = onvif_add_VideoSourceMode(&p_res->VideoSourceModes);
        if (p_v_src_mode)
        {
            if (!parse_VideoSourceMode(p_VideoSourceModes, &p_v_src_mode->VideoSourceMode))
            {
                onvif_free_VideoSourceModes(&p_res->VideoSourceModes);
                return FALSE;
            }
        }

        p_VideoSourceModes = p_VideoSourceModes->next;
    }

    return TRUE;
}

BOOL parse_trt_SetVideoSourceMode(XMLN * p_node, trt_SetVideoSourceMode_RES * p_res)
{
    XMLN * p_Reboot = xml_node_soap_get(p_node, "Reboot");
    if (p_Reboot && p_Reboot->data)
    {
        p_res->Reboot = parse_Bool(p_Reboot->data);
    }

    return TRUE;    
}

BOOL parse_trt_GetVideoSourceConfigurations(XMLN * p_node, trt_GetVideoSourceConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        VideoSourceConfigurationList * p_v_src_cfg = onvif_add_VideoSourceConfiguration(&p_res->Configurations);
        if (p_v_src_cfg)
        {
            if (!parse_VideoSourceConfiguration(p_Configurations, &p_v_src_cfg->Configuration))
            {
                onvif_free_VideoSourceConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetVideoEncoderConfigurations(XMLN * p_node, trt_GetVideoEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        VideoEncoderConfigurationList * p_v_enc = onvif_add_VideoEncoderConfiguration(&p_res->Configurations);
        if (p_v_enc)
        {            
            if (!parse_VideoEncoderConfiguration(p_Configurations, &p_v_enc->Configuration))
            {
                onvif_free_VideoEncoderConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioSourceConfigurations(XMLN * p_node, trt_GetAudioSourceConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        AudioSourceConfigurationList * p_a_src_cfg = onvif_add_AudioSourceConfiguration(&p_res->Configurations);
        if (p_a_src_cfg)
        {
            if (!parse_AudioSourceConfiguration(p_Configurations, &p_a_src_cfg->Configuration))
            {
                onvif_free_AudioSourceConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioEncoderConfigurations(XMLN * p_node, trt_GetAudioEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        AudioEncoderConfigurationList * p_a_enc = onvif_add_AudioEncoderConfiguration(&p_res->Configurations);
        if (p_a_enc)
        {
            if (!parse_AudioEncoderConfiguration(p_Configurations, &p_a_enc->Configuration))
            {
                onvif_free_AudioEncoderConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetVideoSourceConfiguration(XMLN * p_node, trt_GetVideoSourceConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        return parse_VideoSourceConfiguration(p_Configuration, &p_res->Configuration);
    }
    
    return FALSE;
}

BOOL parse_trt_GetVideoEncoderConfiguration(XMLN * p_node, trt_GetVideoEncoderConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        const char * p_token = xml_attr_get(p_Configuration, "token");
        if (p_token)
        {
            strncpy(p_res->Configuration.token, p_token, sizeof(p_res->Configuration.token)-1);
        }    

        if (!parse_VideoEncoderConfiguration(p_Configuration, &p_res->Configuration))
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioSourceConfiguration(XMLN * p_node, trt_GetAudioSourceConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        return parse_AudioSourceConfiguration(p_Configuration, &p_res->Configuration);
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioEncoderConfiguration(XMLN * p_node, trt_GetAudioEncoderConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        const char * p_token = xml_attr_get(p_Configuration, "token");
        if (p_token)
        {
            strncpy(p_res->Configuration.token, p_token, sizeof(p_res->Configuration.token)-1);
        }    

        if (!parse_AudioEncoderConfiguration(p_Configuration, &p_res->Configuration))
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
    
    return TRUE;
}

BOOL parse_trt_GetVideoSourceConfigurationOptions(XMLN * p_node, trt_GetVideoSourceConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;
    XMLN * p_BoundsRange;
    XMLN * p_VideoSourceTokensAvailable;
    XMLN * p_Extension;
    const char * p_MaximumNumberOfProfiles;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (NULL == p_Options)
    {
        return FALSE;
    }

    p_MaximumNumberOfProfiles = xml_attr_get(p_Options, "MaximumNumberOfProfiles");
    if (p_MaximumNumberOfProfiles)
    {
        p_res->Options.MaximumNumberOfProfilesFlag = 1;
        p_res->Options.MaximumNumberOfProfiles = atoi(p_MaximumNumberOfProfiles);
    }
    
    p_BoundsRange = xml_node_soap_get(p_Options, "BoundsRange");
    if (p_BoundsRange)
    {
        parse_IntRectangleRange(p_BoundsRange, &p_res->Options.BoundsRange);
    }

    p_res->Options.sizeVideoSourceTokensAvailable = 0;
    
    p_VideoSourceTokensAvailable = xml_node_soap_get(p_Options, "VideoSourceTokensAvailable");
    while (p_VideoSourceTokensAvailable && 
           p_VideoSourceTokensAvailable->data && 
           soap_strcmp(p_VideoSourceTokensAvailable->name, "VideoSourceTokensAvailable") == 0)
    {
        uint32 idx = p_res->Options.sizeVideoSourceTokensAvailable;
        if (idx < ARRAY_SIZE(p_res->Options.VideoSourceTokensAvailable))
        {
            strncpy(p_res->Options.VideoSourceTokensAvailable[idx], 
                p_VideoSourceTokensAvailable->data, 
                sizeof(p_res->Options.VideoSourceTokensAvailable[idx])-1);
            p_res->Options.sizeVideoSourceTokensAvailable++;
        }

        p_VideoSourceTokensAvailable = p_VideoSourceTokensAvailable->next;
    }

    p_Extension = xml_node_soap_get(p_Options, "Extension");
    if (p_Extension)
    {
        p_res->Options.ExtensionFlag = parse_VideoSourceConfigurationOptionsExtension(p_Extension, &p_res->Options.Extension);
    }

    return TRUE;
}

BOOL parse_trt_GetVideoEncoderConfigurationOptions(XMLN * p_node, trt_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;
    XMLN * p_QualityRange;
    XMLN * p_JPEG;
    XMLN * p_MPEG4;
    XMLN * p_H264;
    XMLN * p_Extension;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (NULL == p_Options)
    {
        return FALSE;
    }

    p_QualityRange = xml_node_soap_get(p_Options, "QualityRange");
    if (p_QualityRange)
    {
        parse_IntRange(p_QualityRange, &p_res->Options.QualityRange);
    }

    p_JPEG = xml_node_soap_get(p_Options, "JPEG");
    if (p_JPEG)
    {
        p_res->Options.JPEGFlag = parse_JPEGOptions(p_JPEG, &p_res->Options.JPEG);
    }

    p_MPEG4 = xml_node_soap_get(p_Options, "MPEG4");
    if (p_MPEG4)
    {
        p_res->Options.MPEG4Flag = parse_MPEG4Options(p_MPEG4, &p_res->Options.MPEG4);
    }    

    p_H264 = xml_node_soap_get(p_Options, "H264");
    if (p_H264)
    {
        p_res->Options.H264Flag = parse_H264Options(p_H264, &p_res->Options.H264);
    }    

    p_Extension = xml_node_soap_get(p_Options, "Extension");
    if (p_Extension)
    {
        p_res->Options.ExtensionFlag = parse_VideoEncoderOptionsExtension(p_Extension, &p_res->Options.Extension);
    }
    
    return TRUE;
}

BOOL parse_trt_GetAudioEncoderConfigurationOptions(XMLN * p_node, trt_GetAudioEncoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {    
        XMLN * p_Options1;
        
        p_Options1 = xml_node_soap_get(p_Options, "Options");
        while (p_Options1 && soap_strcmp(p_Options1->name, "Options") == 0)
        {
            uint32 idx = p_res->Options.sizeOptions;
            
            parse_AudioEncoderConfigurationOption(p_Options1, &p_res->Options.Options[idx]);

            p_res->Options.sizeOptions++;
            if (p_res->Options.sizeOptions >= ARRAY_SIZE(p_res->Options.Options))
            {
                break;
            }

            p_Options1 = p_Options1->next;
        }
    }

    return TRUE;
}

BOOL parse_trt_GetStreamUri(XMLN * p_node, trt_GetStreamUri_RES * p_res)
{
    XMLN * p_MediaUri;
    XMLN * p_Uri;
    XMLN * p_InvalidAfterConnect;
    XMLN * p_InvalidAfterReboot;
    XMLN * p_Timeout;

    p_MediaUri = xml_node_soap_get(p_node, "MediaUri");
    if (NULL == p_MediaUri)
    {
        return FALSE;
    }
    
    p_Uri = xml_node_soap_get(p_MediaUri, "Uri");
    if (p_Uri && p_Uri->data)
    {
        onvif_parse_uri(p_Uri->data, p_res->Uri, sizeof(p_res->Uri));
    }

    p_InvalidAfterConnect = xml_node_soap_get(p_MediaUri, "InvalidAfterConnect");
    if (p_InvalidAfterConnect && p_InvalidAfterConnect->data)
    {
        p_res->InvalidAfterConnect = parse_Bool(p_InvalidAfterConnect->data);
    }

    p_InvalidAfterReboot = xml_node_soap_get(p_MediaUri, "InvalidAfterReboot");
    if (p_InvalidAfterReboot && p_InvalidAfterReboot->data)
    {
        p_res->InvalidAfterReboot = parse_Bool(p_InvalidAfterReboot->data);
    }

    p_Timeout = xml_node_soap_get(p_MediaUri, "Timeout");
    if (p_Timeout && p_Timeout->data)
    {
        p_res->Timeout = parse_Time(p_Timeout->data);
    }
    
    return TRUE;
}

BOOL parse_trt_GetSnapshotUri(XMLN * p_node, trt_GetSnapshotUri_RES * p_res)
{
    XMLN * p_MediaUri;
    XMLN * p_Uri;
    XMLN * p_InvalidAfterConnect;
    XMLN * p_InvalidAfterReboot;
    XMLN * p_Timeout;

    p_MediaUri = xml_node_soap_get(p_node, "MediaUri");
    if (NULL == p_MediaUri)
    {
        return FALSE;
    }
    
    p_Uri = xml_node_soap_get(p_MediaUri, "Uri");
    if (p_Uri && p_Uri->data)
    {
        onvif_parse_uri(p_Uri->data, p_res->Uri, sizeof(p_res->Uri));
    }

    p_InvalidAfterConnect = xml_node_soap_get(p_MediaUri, "InvalidAfterConnect");
    if (p_InvalidAfterConnect && p_InvalidAfterConnect->data)
    {
        p_res->InvalidAfterConnect = parse_Bool(p_InvalidAfterConnect->data);
    }

    p_InvalidAfterReboot = xml_node_soap_get(p_MediaUri, "InvalidAfterReboot");
    if (p_InvalidAfterReboot && p_InvalidAfterReboot->data)
    {
        p_res->InvalidAfterReboot = parse_Bool(p_InvalidAfterReboot->data);
    }

    p_Timeout = xml_node_soap_get(p_MediaUri, "Timeout");
    if (p_Timeout && p_Timeout->data)
    {
        p_res->Timeout = parse_Time(p_Timeout->data);
    }
    
    return TRUE;
}

BOOL parse_trt_GetGuaranteedNumberOfVideoEncoderInstances(XMLN * p_node, trt_GetGuaranteedNumberOfVideoEncoderInstances_RES * p_res)
{
    XMLN * p_TotalNumber;
    XMLN * p_JPEG;
    XMLN * p_H264;
    XMLN * p_MPEG4;

    p_TotalNumber = xml_node_soap_get(p_node, "TotalNumber");
    if (p_TotalNumber && p_TotalNumber->data)
    {
        p_res->TotalNumber = atoi(p_TotalNumber->data);
    }

    p_JPEG = xml_node_soap_get(p_node, "JPEG");
    if (p_JPEG && p_JPEG->data)
    {
        p_res->JPEGFlag = 1;
        p_res->JPEG = atoi(p_JPEG->data);
    }

    p_H264 = xml_node_soap_get(p_node, "H264");
    if (p_H264 && p_H264->data)
    {
        p_res->H264Flag = 1;
        p_res->H264 = atoi(p_H264->data);
    }

    p_MPEG4 = xml_node_soap_get(p_node, "MPEG4");
    if (p_MPEG4 && p_MPEG4->data)
    {
        p_res->MPEG4Flag = 1;
        p_res->MPEG4 = atoi(p_MPEG4->data);
    }

    return TRUE;
}

BOOL parse_trt_GetAudioOutputs(XMLN * p_node, trt_GetAudioOutputs_RES * p_res)
{
    XMLN * p_AudioOutputs;

    p_AudioOutputs = xml_node_soap_get(p_node, "AudioOutputs");
    while (p_AudioOutputs && soap_strcmp(p_AudioOutputs->name, "AudioOutputs") == 0)
    {
        AudioOutputList * p_output = onvif_add_AudioOutput(&p_res->AudioOutputs);
        if (p_output)
        {
            const char * p_token = xml_attr_get(p_AudioOutputs, "token");
            if (p_token)
            {
                strncpy(p_output->AudioOutput.token, p_token, sizeof(p_output->AudioOutput.token)-1);
            }
        }

        p_AudioOutputs = p_AudioOutputs->next;
    }

    return TRUE;
}

BOOL parse_trt_GetAudioOutputConfigurations(XMLN * p_node, trt_GetAudioOutputConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioOutputConfigurationList * p_cfg = onvif_add_AudioOutputConfiguration(&p_res->Configurations);
        if (p_cfg)
        {
            parse_AudioOutputConfiguration(p_Configurations, &p_cfg->Configuration);
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_trt_GetAudioOutputConfiguration(XMLN * p_node, trt_GetAudioOutputConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_AudioOutputConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_trt_GetAudioOutputConfigurationOptions(XMLN * p_node, trt_GetAudioOutputConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_AudioOutputConfigurationOptions(p_Options, &p_res->Options);
    }    

    return TRUE;
}

BOOL parse_trt_GetAudioDecoderConfigurations(XMLN * p_node, trt_GetAudioDecoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioDecoderConfigurationList * p_cfg = onvif_add_AudioDecoderConfiguration(&p_res->Configurations);
        if (p_cfg)
        {
            parse_AudioDecoderConfiguration(p_Configurations, &p_cfg->Configuration);
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_trt_GetAudioDecoderConfiguration(XMLN * p_node, trt_GetAudioDecoderConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_AudioDecoderConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_trt_GetAudioDecoderConfigurationOptions(XMLN * p_node, trt_GetAudioDecoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_AudioDecoderConfigurationOptions(p_Options, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_trt_GetOSDs(XMLN * p_node, trt_GetOSDs_RES * p_res)
{
    XMLN * p_OSDs;

    p_OSDs = xml_node_soap_get(p_node, "OSDs");
    while (p_OSDs)
    {
        OSDConfigurationList * p_osd = onvif_add_OSDConfiguration(&p_res->OSDs);
        if (p_osd)
        {
            if (!parse_OSDConfiguration(p_OSDs, &p_osd->OSD))
            {
                onvif_free_OSDConfigurations(&p_res->OSDs);
                return FALSE;
            }
        }

        p_OSDs = p_OSDs->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetOSD(XMLN * p_node, trt_GetOSD_RES * p_res)
{
    XMLN * p_OSD;

    p_OSD = xml_node_soap_get(p_node, "OSD");
    if (p_OSD)
    {        
        if (!parse_OSDConfiguration(p_OSD, &p_res->OSD))
        {
            return FALSE;
        }
    }
    
    return TRUE;
}

BOOL parse_trt_GetOSDOptions(XMLN * p_node, trt_GetOSDOptions_RES * p_res)
{
    XMLN * p_OSDOptions;

    p_OSDOptions = xml_node_soap_get(p_node, "OSDOptions");
    if (p_OSDOptions)
    {
        parse_OSDOptions(p_OSDOptions, &p_res->OSDOptions);
    }
    
    return TRUE;
}

BOOL parse_trt_CreateOSD(XMLN * p_node, trt_CreateOSD_RES * p_res)
{
    XMLN * p_OSDToken;
    
    p_OSDToken = xml_node_soap_get(p_node, "OSDToken");
    if (p_OSDToken && p_OSDToken->data)
    {
        strncpy(p_res->OSDToken, p_OSDToken->data, sizeof(p_res->OSDToken)-1);
    }

    return TRUE;
}

BOOL parse_trt_GetVideoAnalyticsConfigurations(XMLN * p_node, trt_GetVideoAnalyticsConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        VideoAnalyticsConfigurationList * p_config = onvif_add_VideoAnalyticsConfiguration(&p_res->Configurations);
        if (p_config)
        {
            if (!parse_VideoAnalyticsConfiguration(p_Configurations, &p_config->Configuration))
            {
                onvif_free_VideoAnalyticsConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_trt_GetVideoAnalyticsConfiguration(XMLN * p_node, trt_GetVideoAnalyticsConfiguration_RES * p_res)
{
    return parse_VideoAnalyticsConfiguration(p_node, &p_res->Configuration);
}

BOOL parse_trt_GetMetadataConfigurations(XMLN * p_node, trt_GetMetadataConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        MetadataConfigurationList * p_config = onvif_add_MetadataConfiguration(&p_res->Configurations);
        if (p_config)
        {
            if (!parse_MetadataConfiguration(p_Configurations, &p_config->Configuration))
            {
                onvif_free_MetadataConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_trt_GetMetadataConfiguration(XMLN * p_node, trt_GetMetadataConfiguration_RES * p_res)
{
    return parse_MetadataConfiguration(p_node, &p_res->Configuration);
}

BOOL parse_trt_GetMetadataConfigurationOptions(XMLN * p_node, trt_GetMetadataConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_MetadataConfigurationOptions(p_Options, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_trt_GetCompatibleVideoEncoderConfigurations(XMLN * p_node, trt_GetCompatibleVideoEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        VideoEncoderConfigurationList * p_v_enc = onvif_add_VideoEncoderConfiguration(&p_res->Configurations);
        if (p_v_enc)
        {            
            if (!parse_VideoEncoderConfiguration(p_Configurations, &p_v_enc->Configuration))
            {
                onvif_free_VideoEncoderConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetCompatibleAudioEncoderConfigurations(XMLN * p_node, trt_GetCompatibleAudioEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations)
    {
        AudioEncoderConfigurationList * p_a_enc = onvif_add_AudioEncoderConfiguration(&p_res->Configurations);
        if (p_a_enc)
        {
            if (!parse_AudioEncoderConfiguration(p_Configurations, &p_a_enc->Configuration))
            {
                onvif_free_AudioEncoderConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_trt_GetCompatibleVideoAnalyticsConfigurations(XMLN * p_node, trt_GetCompatibleVideoAnalyticsConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        VideoAnalyticsConfigurationList * p_config = onvif_add_VideoAnalyticsConfiguration(&p_res->Configurations);
        if (p_config)
        {
            if (!parse_VideoAnalyticsConfiguration(p_Configurations, &p_config->Configuration))
            {
                onvif_free_VideoAnalyticsConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_trt_GetCompatibleMetadataConfigurations(XMLN * p_node, trt_GetCompatibleMetadataConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        MetadataConfigurationList * p_config = onvif_add_MetadataConfiguration(&p_res->Configurations);
        if (p_config)
        {
            if (!parse_MetadataConfiguration(p_Configurations, &p_config->Configuration))
            {
                onvif_free_MetadataConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

/***************************************************************************************/

BOOL parse_Space2DDescription(XMLN * p_node, onvif_Space2DDescription * p_res)
{
    XMLN * p_XRange;
    XMLN * p_YRange;

    p_XRange = xml_node_soap_get(p_node, "XRange");
    if (p_XRange)
    {
        parse_FloatRange(p_XRange, &p_res->XRange);
    }

    p_YRange = xml_node_soap_get(p_node, "YRange");
    if (p_YRange)
    {
        parse_FloatRange(p_YRange, &p_res->YRange);
    }

    return TRUE;
}

BOOL parse_Space1DDescription(XMLN * p_node, onvif_Space1DDescription * p_res)
{
    XMLN * p_XRange;

    p_XRange = xml_node_soap_get(p_node, "XRange");
    if (p_XRange)
    {
        parse_FloatRange(p_XRange, &p_res->XRange);
    }

    return TRUE;
}

BOOL parse_PTZSpaces(XMLN * p_node, onvif_PTZSpaces * p_res)
{
    XMLN * p_AbsolutePanTiltPositionSpace;
    XMLN * p_AbsoluteZoomPositionSpace;
    XMLN * p_RelativePanTiltTranslationSpace;
    XMLN * p_RelativeZoomTranslationSpace;
    XMLN * p_ContinuousPanTiltVelocitySpace;
    XMLN * p_ContinuousZoomVelocitySpace;
    XMLN * p_PanTiltSpeedSpace;
    XMLN * p_ZoomSpeedSpace;

    p_AbsolutePanTiltPositionSpace = xml_node_soap_get(p_node, "AbsolutePanTiltPositionSpace");
    if (p_AbsolutePanTiltPositionSpace)
    {
        p_res->AbsolutePanTiltPositionSpaceFlag = 1;            
        parse_Space2DDescription(p_AbsolutePanTiltPositionSpace, &p_res->AbsolutePanTiltPositionSpace);
    }

    p_AbsoluteZoomPositionSpace = xml_node_soap_get(p_node, "AbsoluteZoomPositionSpace");
    if (p_AbsoluteZoomPositionSpace)
    {
        p_res->AbsoluteZoomPositionSpaceFlag = 1;            
        parse_Space1DDescription(p_AbsoluteZoomPositionSpace, &p_res->AbsoluteZoomPositionSpace);
    }

    p_RelativePanTiltTranslationSpace = xml_node_soap_get(p_node, "RelativePanTiltTranslationSpace");
    if (p_RelativePanTiltTranslationSpace)
    {
        p_res->RelativePanTiltTranslationSpaceFlag = 1;            
        parse_Space2DDescription(p_RelativePanTiltTranslationSpace, &p_res->RelativePanTiltTranslationSpace);
    }

    p_RelativeZoomTranslationSpace = xml_node_soap_get(p_node, "RelativeZoomTranslationSpace");
    if (p_RelativeZoomTranslationSpace)
    {
        p_res->RelativeZoomTranslationSpaceFlag = 1;            
        parse_Space1DDescription(p_RelativeZoomTranslationSpace, &p_res->RelativeZoomTranslationSpace);
    }

    p_ContinuousPanTiltVelocitySpace = xml_node_soap_get(p_node, "ContinuousPanTiltVelocitySpace");
    if (p_ContinuousPanTiltVelocitySpace)
    {
        p_res->ContinuousPanTiltVelocitySpaceFlag = 1;            
        parse_Space2DDescription(p_ContinuousPanTiltVelocitySpace, &p_res->ContinuousPanTiltVelocitySpace);
    }

    p_ContinuousZoomVelocitySpace = xml_node_soap_get(p_node, "ContinuousZoomVelocitySpace");
    if (p_ContinuousZoomVelocitySpace)
    {
        p_res->ContinuousZoomVelocitySpaceFlag = 1;            
        parse_Space1DDescription(p_ContinuousZoomVelocitySpace, &p_res->ContinuousZoomVelocitySpace);
    }

    p_PanTiltSpeedSpace = xml_node_soap_get(p_node, "PanTiltSpeedSpace");
    if (p_PanTiltSpeedSpace)
    {
        p_res->PanTiltSpeedSpaceFlag = 1;            
        parse_Space1DDescription(p_PanTiltSpeedSpace, &p_res->PanTiltSpeedSpace);
    }

    p_ZoomSpeedSpace = xml_node_soap_get(p_node, "ZoomSpeedSpace");
    if (p_ZoomSpeedSpace)
    {
        p_res->ZoomSpeedSpaceFlag = 1;            
        parse_Space1DDescription(p_ZoomSpeedSpace, &p_res->ZoomSpeedSpace);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourSupported(XMLN * p_node, onvif_PTZPresetTourSupported * p_res)
{
    XMLN * p_MaximumNumberOfPresetTours;
    XMLN * p_PTZPresetTourOperation;

    p_MaximumNumberOfPresetTours = xml_node_soap_get(p_node, "MaximumNumberOfPresetTours");
    if (p_MaximumNumberOfPresetTours && p_MaximumNumberOfPresetTours->data)
    {
        p_res->MaximumNumberOfPresetTours = atoi(p_MaximumNumberOfPresetTours->data);
    }

    p_PTZPresetTourOperation = xml_node_soap_get(p_node, "PTZPresetTourOperation");
    while (p_PTZPresetTourOperation && p_PTZPresetTourOperation->data && 
        soap_strcmp(p_PTZPresetTourOperation->name, "PTZPresetTourOperation") == 0)
    {
        if (strcasecmp(p_PTZPresetTourOperation->data, "Start") == 0)
        {
            p_res->PTZPresetTourOperation_Start = 1;
        }
        else if (strcasecmp(p_PTZPresetTourOperation->data, "Stop") == 0)
        {
            p_res->PTZPresetTourOperation_Stop = 1;
        }
        else if (strcasecmp(p_PTZPresetTourOperation->data, "Pause") == 0)
        {
            p_res->PTZPresetTourOperation_Pause = 1;
        }
        else if (strcasecmp(p_PTZPresetTourOperation->data, "Extended") == 0)
        {
            p_res->PTZPresetTourOperation_Extended = 1;
        }

        p_PTZPresetTourOperation = p_PTZPresetTourOperation->next;
    }

    return TRUE;
}

BOOL parse_PTZNode(XMLN * p_node, onvif_PTZNode * p_res)
{
    XMLN * p_Name;
    XMLN * p_SupportedPTZSpaces;
    XMLN * p_MaximumNumberOfPresets;
    XMLN * p_HomeSupported;
    XMLN * p_AuxiliaryCommands;
    XMLN * p_Extension;
    const char * p_FixedHomePosition;
    const char * p_token;
    const char * p_GeoMove;

    p_FixedHomePosition = xml_attr_get(p_node, "FixedHomePosition");
    if (p_FixedHomePosition)
    {
        p_res->FixedHomePosition =  parse_Bool(p_FixedHomePosition);
    }

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_GeoMove = xml_attr_get(p_node, "GeoMove");
    if (p_GeoMove)
    {
        p_res->GeoMove =  parse_Bool(p_GeoMove);
    }
        
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        p_res->NameFlag = 1;
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_SupportedPTZSpaces = xml_node_soap_get(p_node, "SupportedPTZSpaces");
    if (p_SupportedPTZSpaces)
    {
        parse_PTZSpaces(p_SupportedPTZSpaces, &p_res->SupportedPTZSpaces);
    }

    p_MaximumNumberOfPresets = xml_node_soap_get(p_node, "MaximumNumberOfPresets");
    if (p_MaximumNumberOfPresets && p_MaximumNumberOfPresets->data)
    {
        p_res->MaximumNumberOfPresets = atoi(p_MaximumNumberOfPresets->data);
    }
    
    p_HomeSupported = xml_node_soap_get(p_node, "HomeSupported");
    if (p_HomeSupported && p_HomeSupported->data)
    {
        p_res->HomeSupported = parse_Bool(p_HomeSupported->data);
    }

    p_res->sizeAuxiliaryCommands = 0;
    
    p_AuxiliaryCommands = xml_node_soap_get(p_node, "AuxiliaryCommands");
    while (p_AuxiliaryCommands && p_AuxiliaryCommands->data && soap_strcmp(p_AuxiliaryCommands->name, "AuxiliaryCommands") == 0)
    {
        uint32 idx = p_res->sizeAuxiliaryCommands;

        strncpy(p_res->AuxiliaryCommands[idx], p_AuxiliaryCommands->data, sizeof(p_res->AuxiliaryCommands[idx])-1);

        p_res->sizeAuxiliaryCommands++;
        if (p_res->sizeAuxiliaryCommands >= ARRAY_SIZE(p_res->AuxiliaryCommands))
        {
            break;
        }

        p_AuxiliaryCommands = p_AuxiliaryCommands->next;
    }
    
    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        XMLN * p_SupportedPresetTour;
        
        p_res->ExtensionFlag = 1;
        
        p_SupportedPresetTour = xml_node_soap_get(p_Extension, "SupportedPresetTour");
        if (p_SupportedPresetTour)
        {
            p_res->Extension.SupportedPresetTourFlag = parse_PTZPresetTourSupported(p_SupportedPresetTour, &p_res->Extension.SupportedPresetTour);
        }
    }
    
    return TRUE;
}

BOOL parse_Preset(XMLN * p_node, onvif_PTZPreset * p_res)
{
    XMLN * p_Name;
    XMLN * p_PTZPosition;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_PTZPosition = xml_node_soap_get(p_node, "PTZPosition");
    if (p_PTZPosition)
    {
        p_res->PTZPositionFlag = parse_PTZVector(p_PTZPosition, &p_res->PTZPosition);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourPresetDetail(XMLN * p_node, onvif_PTZPresetTourPresetDetail * p_res)
{
    XMLN * p_PresetToken;
    XMLN * p_Home;
    XMLN * p_PTZPosition;

    p_PresetToken = xml_node_soap_get(p_node, "PresetToken");
    if (p_PresetToken && p_PresetToken->data)
    {
        p_res->PresetTokenFlag = 1;
        strncpy(p_res->PresetToken, p_PresetToken->data, sizeof(p_res->PresetToken)-1);
    }

    p_Home = xml_node_soap_get(p_node, "Home");
    if (p_Home && p_Home->data)
    {
        p_res->HomeFlag = 1;
        p_res->Home = parse_Bool(p_Home->data);            
    }

    p_PTZPosition = xml_node_soap_get(p_node, "PTZPosition");
    if (p_PTZPosition)
    {
        p_res->PTZPositionFlag = parse_PTZVector(p_PTZPosition, &p_res->PTZPosition);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourSpot(XMLN * p_node, onvif_PTZPresetTourSpot * p_res)
{
    XMLN * p_PresetDetail;
    XMLN * p_Speed;
    XMLN * p_StayTime;

    p_PresetDetail = xml_node_soap_get(p_node, "PresetDetail");
    if (p_PresetDetail)
    {
        parse_PTZPresetTourPresetDetail(p_PresetDetail, &p_res->PresetDetail);
    }

    p_Speed = xml_node_soap_get(p_node, "Speed");
    if (p_Speed)
    {
        p_res->SpeedFlag = parse_PTZSpeed(p_Speed, &p_res->Speed);
    }

    p_StayTime = xml_node_soap_get(p_node, "StayTime");
    if (p_StayTime && p_StayTime->data)
    {
        p_res->StayTimeFlag = 1;
        p_res->StayTime = atoi(p_StayTime->data);
    }
    
    return TRUE;
}

BOOL parse_PTZPresetTourStatus(XMLN * p_node, onvif_PTZPresetTourStatus * p_res)
{
    XMLN * p_State;
    XMLN * p_CurrentTourSpot;
    
    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        p_res->State = onvif_StringToPTZPresetTourState(p_State->data);
    }

    p_CurrentTourSpot = xml_node_soap_get(p_node, "CurrentTourSpot");
    if (p_CurrentTourSpot)
    {
        p_res->CurrentTourSpotFlag = parse_PTZPresetTourSpot(p_CurrentTourSpot, &p_res->CurrentTourSpot);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourStartingCondition(XMLN * p_node, onvif_PTZPresetTourStartingCondition * p_res)
{
    const char * p_RandomPresetOrder;
    XMLN * p_RecurringTime;
    XMLN * p_RecurringDuration;
    XMLN * p_Direction;

    p_RandomPresetOrder = xml_attr_get(p_node, "RandomPresetOrder");
    if (p_RandomPresetOrder)
    {
        p_res->RandomPresetOrderFlag = 1;
        p_res->RandomPresetOrder = parse_Bool(p_RandomPresetOrder);
    }

    p_RecurringTime = xml_node_soap_get(p_node, "RecurringTime");
    if (p_RecurringTime && p_RecurringTime->data)
    {
        p_res->RecurringTimeFlag = 1;
        p_res->RecurringTime = atoi(p_RecurringTime->data);
    }

    p_RecurringDuration = xml_node_soap_get(p_node, "RecurringDuration");
    if (p_RecurringDuration && p_RecurringDuration->data)
    {
        p_res->RecurringDurationFlag = 1;
        parse_XSDDuration(p_RecurringDuration->data, &p_res->RecurringDuration);
    }

    p_Direction = xml_node_soap_get(p_node, "Direction");
    if (p_Direction && p_Direction->data)
    {
        p_res->DirectionFlag = 1;
        p_res->Direction = onvif_StringToPTZPresetTourDirection(p_Direction->data);
    }

    return TRUE;
}

BOOL parse_PresetTour(XMLN * p_node, onvif_PresetTour * p_res)
{
    const char * p_token;
    XMLN * p_Name;
    XMLN * p_Status;
    XMLN * p_AutoStart;
    XMLN * p_StartingCondition;
    XMLN * p_TourSpot;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }
    
    p_Status = xml_node_soap_get(p_node, "Status");
    if (p_Status)
    {
        parse_PTZPresetTourStatus(p_Status, &p_res->Status);
    }

    p_AutoStart = xml_node_soap_get(p_node, "AutoStart");
    if (p_AutoStart && p_AutoStart->data)
    {
        p_res->AutoStart = parse_Bool(p_AutoStart->data);
    }

    p_StartingCondition = xml_node_soap_get(p_node, "StartingCondition");
    if (p_StartingCondition)
    {
        parse_PTZPresetTourStartingCondition(p_StartingCondition, &p_res->StartingCondition);
    }

    p_TourSpot = xml_node_soap_get(p_node, "TourSpot");
    while (p_TourSpot && soap_strcmp(p_TourSpot->name, "TourSpot") == 0)
    {
        PTZPresetTourSpotList * p_tour_spot = onvif_add_PTZPresetTourSpot(&p_res->TourSpot);
        if (p_tour_spot)
        {
            if (!parse_PTZPresetTourSpot(p_TourSpot, &p_tour_spot->PTZPresetTourSpot))
            {
                onvif_free_PTZPresetTourSpots(&p_res->TourSpot);
                break;
            }
        }
        
        p_TourSpot = p_TourSpot->next;
    }

    return TRUE;
}

BOOL parse_DurationRange(XMLN * p_node, onvif_DurationRange * p_res)
{
    XMLN * p_Min;
    XMLN * p_Max;

    p_Min = xml_node_soap_get(p_node, "Min");
    if (p_Min && p_Min->data)
    {
        parse_XSDDuration(p_Min->data, &p_res->Min);
    }

    p_Max = xml_node_soap_get(p_node, "Max");
    if (p_Max && p_Max->data)
    {
        parse_XSDDuration(p_Max->data, &p_res->Max);
    }

    return TRUE;
}

BOOL parse_PTZMoveStatus(XMLN * p_node, onvif_PTZMoveStatus * p_res)
{
    XMLN * p_PanTilt;
    XMLN * p_Zoom;
    
    p_PanTilt = xml_node_soap_get(p_node, "PanTilt");
    if (p_PanTilt && p_PanTilt->data)
    {
        p_res->PanTiltFlag = 1;
        p_res->PanTilt = onvif_StringToMoveStatus(p_PanTilt->data);
    }

    p_Zoom = xml_node_soap_get(p_node, "Zoom");
    if (p_Zoom && p_Zoom->data)
    {
        p_res->ZoomFlag = 1;
        p_res->Zoom = onvif_StringToMoveStatus(p_Zoom->data);
    }

    return TRUE;
}

BOOL parse_PTControlDirectionOptions(XMLN * p_node, onvif_PTControlDirectionOptions * p_res)
{
    XMLN * p_EFlip;
    XMLN * p_Reverse;
    
    p_EFlip = xml_node_soap_get(p_node, "EFlip");
    if (p_EFlip)
    {
        XMLN * p_Mode;
        
        p_Mode = xml_node_soap_get(p_EFlip, "Mode");
        while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
        {
            if (strcasecmp(p_Mode->data, "ON") == 0)
            {
                p_res->EFlipMode_ON = 1;
            }    
            else if (strcasecmp(p_Mode->data, "OFF") == 0)
            {
                p_res->EFlipMode_OFF = 1;
            }
            else if (strcasecmp(p_Mode->data, "Extended") == 0)
            {
                p_res->EFlipMode_Extended = 1;
            }

            p_Mode = p_Mode->next;
        }
    }

    p_Reverse = xml_node_soap_get(p_node, "Reverse");
    if (p_Reverse)
    {
        XMLN * p_Mode;
        
        p_Mode = xml_node_soap_get(p_Reverse, "Mode");
        while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
        {
            if (strcasecmp(p_Mode->data, "ON") == 0)
            {
                p_res->ReverseMode_ON = 1;
            }    
            else if (strcasecmp(p_Mode->data, "OFF") == 0)
            {
                p_res->ReverseMode_OFF = 1;
            }
            else if (strcasecmp(p_Mode->data, "Extended") == 0)
            {
                p_res->ReverseMode_Extended = 1;
            }
            else if (strcasecmp(p_Mode->data, "AUTO") == 0)
            {
                p_res->ReverseMode_AUTO = 1;
            }

            p_Mode = p_Mode->next;
        }
    }

    return TRUE;
}

BOOL parse_PTZPresetTourStartingConditionOptions(XMLN * p_node, onvif_PTZPresetTourStartingConditionOptions * p_res)
{
    XMLN * p_RecurringTime;
    XMLN * p_RecurringDuration;
    XMLN * p_Direction;

    p_RecurringTime = xml_node_soap_get(p_node, "RecurringTime");
    if (p_RecurringTime)
    {
        p_res->RecurringTimeFlag = parse_IntRange(p_RecurringTime, &p_res->RecurringTime);
    }

    p_RecurringDuration = xml_node_soap_get(p_node, "RecurringDuration");
    if (p_RecurringDuration)
    {
        p_res->RecurringDurationFlag = parse_DurationRange(p_RecurringDuration, &p_res->RecurringDuration);
    }

    p_Direction = xml_node_soap_get(p_node, "Direction");
    while (p_Direction && p_Direction->data && soap_strcmp(p_Direction->name, "Direction") == 0)
    {
        if (strcmp(p_Direction->data, "Forward") == 0)
        {
            p_res->PTZPresetTourDirection_Forward = 1;
        }
        else if (strcmp(p_Direction->data, "Backward") == 0)
        {
            p_res->PTZPresetTourDirection_Backward = 1;
        }
        else if (strcmp(p_Direction->data, "Extended") == 0)
        {
            p_res->PTZPresetTourDirection_Extended = 1;
        }
        
        p_Direction = p_Direction->next;
    }
    
    return TRUE;
}

BOOL parse_PTZPresetTourPresetDetailOptions(XMLN * p_node, onvif_PTZPresetTourPresetDetailOptions * p_res)
{
    XMLN * p_PresetToken;
    XMLN * p_Home;
    XMLN * p_PanTiltPositionSpace;
    XMLN * p_ZoomPositionSpace;

    p_res->sizePresetToken = 0;
    
    p_PresetToken = xml_node_soap_get(p_node, "PresetToken");
    while (p_PresetToken && p_PresetToken->data && 
           soap_strcmp(p_PresetToken->name, "PresetToken") == 0)
    {
        uint32 idx = p_res->sizePresetToken;

        strncpy(p_res->PresetToken[idx], p_PresetToken->data, sizeof(p_res->PresetToken[idx])-1);

        p_res->sizePresetToken++;
        if (p_res->sizePresetToken >= ARRAY_SIZE(p_res->PresetToken))
        {
            break;
        }
        
        p_PresetToken = p_PresetToken->next;
    }

    p_Home = xml_node_soap_get(p_node, "Home");
    if (p_Home && p_Home->data)
    {
        p_res->HomeFlag = 1;
        p_res->Home = parse_Bool(p_Home->data);
    }

    p_PanTiltPositionSpace = xml_node_soap_get(p_node, "PanTiltPositionSpace");
    if (p_PanTiltPositionSpace)
    {
        p_res->PanTiltPositionSpaceFlag = 1;
        parse_Space2DDescription(p_PanTiltPositionSpace, &p_res->PanTiltPositionSpace);
    }

    p_ZoomPositionSpace = xml_node_soap_get(p_node, "ZoomPositionSpace");
    if (p_ZoomPositionSpace)
    {
        p_res->ZoomPositionSpaceFlag = 1;
        parse_Space1DDescription(p_ZoomPositionSpace, &p_res->ZoomPositionSpace);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourSpotOptions(XMLN * p_node, onvif_PTZPresetTourSpotOptions * p_res)
{
    XMLN * p_PresetDetail;
    XMLN * p_StayTime;

    p_PresetDetail = xml_node_soap_get(p_node, "PresetDetail");
    if (p_PresetDetail)
    {
        parse_PTZPresetTourPresetDetailOptions(p_PresetDetail, &p_res->PresetDetail);
    }

    p_StayTime = xml_node_soap_get(p_node, "StayTime");
    if (p_StayTime)
    {
        parse_DurationRange(p_StayTime, &p_res->StayTime);
    }

    return TRUE;
}

BOOL parse_PTZPresetTourOptions(XMLN * p_node, onvif_PTZPresetTourOptions * p_res)
{
    XMLN * p_AutoStart;
    XMLN * p_StartingCondition;
    XMLN * p_TourSpot;

    p_AutoStart = xml_node_soap_get(p_node, "AutoStart");
    if (p_AutoStart && p_AutoStart->data)
    {
        p_res->AutoStart = parse_Bool(p_AutoStart->data);
    }

    p_StartingCondition = xml_node_soap_get(p_node, "StartingCondition");
    if (p_StartingCondition)
    {
        parse_PTZPresetTourStartingConditionOptions(p_StartingCondition, &p_res->StartingCondition);
    }

    p_TourSpot = xml_node_soap_get(p_node, "TourSpot");
    if (p_TourSpot)
    {
        parse_PTZPresetTourSpotOptions(p_TourSpot, &p_res->TourSpot);
    }

    return TRUE;
}

BOOL parse_ptz_GetServiceCapabilities(XMLN * p_node, ptz_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_PTZServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_ptz_GetNodes(XMLN * p_node, ptz_GetNodes_RES * p_res)
{
    XMLN * p_PTZNode;

    p_PTZNode = xml_node_soap_get(p_node, "PTZNode");
    while (p_PTZNode)
    {
        PTZNodeList * p_ptz_node = onvif_add_PTZNode(&p_res->PTZNode);
        if (p_ptz_node)
        {
            if (!parse_PTZNode(p_PTZNode, &p_ptz_node->PTZNode))
            {
                onvif_free_PTZNodes(&p_res->PTZNode);
                return FALSE;
            }
        }

        p_PTZNode = p_PTZNode->next;
    }
    
    return TRUE;
}

BOOL parse_ptz_GetNode(XMLN * p_node, ptz_GetNode_RES * p_res)
{
    XMLN * p_PTZNode;

    p_PTZNode = xml_node_soap_get(p_node, "PTZNode");
    if (p_PTZNode)
    {
        if (!parse_PTZNode(p_PTZNode, &p_res->PTZNode))
        {
            return FALSE;
        }
    }
    
    return TRUE;
}

BOOL parse_ptz_GetPresets(XMLN * p_node, ptz_GetPresets_RES * p_res)
{
    XMLN * p_Preset;

    p_Preset = xml_node_soap_get(p_node, "Preset");
    while (p_Preset)
    {        
        PTZPresetList * p_ptz_preset = onvif_add_PTZPreset(&p_res->PTZPresets);
        if (p_ptz_preset)
        {
            const char * p_token;

            p_token = xml_attr_get(p_Preset, "token");
            if (p_token)
            {
                strncpy(p_ptz_preset->PTZPreset.token, p_token, sizeof(p_ptz_preset->PTZPreset.token)-1);
            }    

            if (!parse_Preset(p_Preset, &p_ptz_preset->PTZPreset))
            {
                onvif_free_PTZPresets(&p_res->PTZPresets);
                return FALSE;
            }
        }
        
        p_Preset = p_Preset->next;
    }
    
    return TRUE;
}

BOOL parse_ptz_SetPreset(XMLN * p_node, ptz_SetPreset_RES * p_res)
{
    XMLN * p_PresetToken = xml_node_soap_get(p_node, "PresetToken");
    if (p_PresetToken && p_PresetToken->data)
    {
        strncpy(p_res->PresetToken, p_PresetToken->data, sizeof(p_res->PresetToken)-1);
    }    

    return TRUE;
}

BOOL parse_ptz_GetStatus(XMLN * p_node, ptz_GetStatus_RES * p_res)
{    
    XMLN * p_PTZStatus;
    XMLN * p_Position;
    XMLN * p_MoveStatus;
    XMLN * p_Error;
    XMLN * p_UtcTime;

    p_PTZStatus = xml_node_soap_get(p_node, "PTZStatus");
    if (NULL == p_PTZStatus)
    {
        return FALSE;
    }

    p_Position = xml_node_soap_get(p_PTZStatus, "Position");
    if (p_Position)
    {
        p_res->PTZStatus.PositionFlag = 1;
        parse_PTZVector(p_Position, &p_res->PTZStatus.Position);
    }

    p_MoveStatus = xml_node_soap_get(p_PTZStatus, "MoveStatus");
    if (p_MoveStatus)
    {
        p_res->PTZStatus.MoveStatusFlag = parse_PTZMoveStatus(p_MoveStatus, &p_res->PTZStatus.MoveStatus);
    }

    p_Error = xml_node_soap_get(p_PTZStatus, "Error");
    if (p_Error && p_Error->data)
    {
        p_res->PTZStatus.ErrorFlag = 1;
        strncpy(p_res->PTZStatus.Error, p_Error->data, sizeof(p_res->PTZStatus.Error)-1);
    }

    p_UtcTime = xml_node_soap_get(p_PTZStatus, "UtcTime");
    if (p_UtcTime && p_UtcTime->data)
    {
        parse_XSDDatetime(p_UtcTime->data, &p_res->PTZStatus.UtcTime);
    }
    
    return TRUE;
}

BOOL parse_ptz_GetConfigurations(XMLN * p_node, ptz_GetConfigurations_RES * p_res)
{
    XMLN * p_PTZConfiguration;

    p_PTZConfiguration = xml_node_soap_get(p_node, "PTZConfiguration");
    while (p_PTZConfiguration)
    {
        PTZConfigurationList * p_ptz_cfg = onvif_add_PTZConfiguration(&p_res->PTZConfiguration);
        if (p_ptz_cfg)
        {
            if (!parse_PTZConfiguration(p_PTZConfiguration, &p_ptz_cfg->Configuration))
            {
                onvif_free_PTZConfigurations(&p_res->PTZConfiguration);
                return FALSE;
            }        
        }

        p_PTZConfiguration = p_PTZConfiguration->next;
    }
        
    return TRUE;
}

BOOL parse_ptz_GetConfiguration(XMLN * p_node, ptz_GetConfiguration_RES * p_res)
{
    XMLN * p_PTZConfiguration;

    p_PTZConfiguration = xml_node_soap_get(p_node, "PTZConfiguration");
    if (p_PTZConfiguration)
    {
        if (!parse_PTZConfiguration(p_PTZConfiguration, &p_res->PTZConfiguration))
        {
            return FALSE;
        }        
    }
    
    return TRUE;
}

BOOL parse_ptz_GetConfigurationOptions(XMLN * p_node, ptz_GetConfigurationOptions_RES * p_res)
{
    XMLN * p_PTZConfigurationOptions;
    XMLN * p_Spaces;
    XMLN * p_PTZTimeout;
    XMLN * p_PTControlDirection;

    p_PTZConfigurationOptions = xml_node_soap_get(p_node, "PTZConfigurationOptions");
    if (NULL == p_PTZConfigurationOptions)
    {
        return FALSE;
    }

    p_Spaces = xml_node_soap_get(p_PTZConfigurationOptions, "Spaces");
    if (p_Spaces)
    {
        parse_PTZSpaces(p_Spaces, &p_res->PTZConfigurationOptions.Spaces);
    }
    
    p_PTZTimeout = xml_node_soap_get(p_PTZConfigurationOptions, "PTZTimeout");
    if (p_PTZTimeout)
    {
        parse_IntRange(p_PTZTimeout, &p_res->PTZConfigurationOptions.PTZTimeout);
    }

    p_PTControlDirection = xml_node_soap_get(p_PTZConfigurationOptions, "PTControlDirection");
    if (p_PTControlDirection)
    {
        p_res->PTZConfigurationOptions.PTControlDirectionFlag = 1;
        parse_PTControlDirectionOptions(p_PTControlDirection, &p_res->PTZConfigurationOptions.PTControlDirection);
    }
    
    return TRUE;
}

BOOL parse_ptz_GetPresetTours(XMLN * p_node, ptz_GetPresetTours_RES * p_res)
{
    XMLN * p_PresetTour;

    p_PresetTour = xml_node_soap_get(p_node, "PresetTour");
    while (p_PresetTour && soap_strcmp(p_PresetTour->name, "PresetTour") == 0)
    {
        PresetTourList * p_preset_tour = onvif_add_PresetTour(&p_res->PresetTour);
        if (p_preset_tour)
        {
            if (!parse_PresetTour(p_PresetTour, &p_preset_tour->PresetTour))
            {
                onvif_free_PresetTours(&p_res->PresetTour);
                return FALSE;
            }
        }

        p_PresetTour = p_PresetTour->next;
    }

    return TRUE;
}

BOOL parse_ptz_GetPresetTour(XMLN * p_node, ptz_GetPresetTour_RES * p_res)
{
    BOOL ret = FALSE;
    XMLN * p_PresetTour;

    p_PresetTour = xml_node_soap_get(p_node, "PresetTour");
    if (p_PresetTour)
    {
        ret = parse_PresetTour(p_PresetTour, &p_res->PresetTour);
    }

    return ret;
}

BOOL parse_ptz_GetPresetTourOptions(XMLN * p_node, ptz_GetPresetTourOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_PTZPresetTourOptions(p_Options, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_ptz_CreatePresetTour(XMLN * p_node, ptz_CreatePresetTour_RES * p_res)
{
    XMLN * p_PresetTourToken;

    p_PresetTourToken = xml_node_soap_get(p_node, "PresetTourToken");
    if (p_PresetTourToken && p_PresetTourToken->data)
    {
        strncpy(p_res->PresetTourToken, p_PresetTourToken->data, sizeof(p_res->PresetTourToken)-1);
    }

    return TRUE;
}

BOOL parse_ptz_SendAuxiliaryCommand(XMLN * p_node, ptz_SendAuxiliaryCommand_RES * p_res)
{
    XMLN * p_AuxiliaryResponse;

    p_AuxiliaryResponse = xml_node_soap_get(p_node, "AuxiliaryResponse");
    if (p_AuxiliaryResponse && p_AuxiliaryResponse->data)
    {
        strncpy(p_res->AuxiliaryResponse, p_AuxiliaryResponse->data, sizeof(p_res->AuxiliaryResponse)-1);
    }

    return TRUE;
}

/***************************************************************************************/

BOOL parse_Message(XMLN * p_node, onvif_Message * p_res)
{
    XMLN * p_Message = xml_node_soap_get(p_node, "Message");
    if (p_Message)
    {
        XMLN * p_Source;
        XMLN * p_Key;
        XMLN * p_Data;
        const char * p_UtcTime;
        const char * p_PropertyOperation;

        p_UtcTime = xml_attr_get(p_Message, "UtcTime");
        if (p_UtcTime)
        {
            parse_XSDDatetime(p_UtcTime, &p_res->UtcTime);
        }

        p_PropertyOperation = xml_attr_get(p_Message, "PropertyOperation");
        if (p_PropertyOperation)
        {
            p_res->PropertyOperationFlag = 1;
            p_res->PropertyOperation = onvif_StringToPropertyOperation(p_PropertyOperation);
        }

        p_Source = xml_node_soap_get(p_Message, "Source");
        if (p_Source)
        {
            p_res->SourceFlag = parse_ItemList(p_Source, &p_res->Source);
        }

        p_Key = xml_node_soap_get(p_Message, "Key");
        if (p_Key)
        {    
            p_res->KeyFlag = parse_ItemList(p_Key, &p_res->Key);
        }

        p_Data = xml_node_soap_get(p_Message, "Data");
        if (p_Data)
        {
            p_res->DataFlag = parse_ItemList(p_Data, &p_res->Data);
        }
    }

    return TRUE;
}

BOOL parse_NotificationMessage(XMLN * p_node, onvif_NotificationMessage * p_res)
{
    XMLN * p_SubscriptionReference;
    XMLN * p_Topic;
    XMLN * p_ProducerReference;
    XMLN * p_Message;

    p_SubscriptionReference = xml_node_soap_get(p_node, "SubscriptionReference");
    if (p_SubscriptionReference)
    {
        XMLN * p_Address = xml_node_soap_get(p_SubscriptionReference, "Address");
        if (p_Address && p_Address->data)
        {
            strncpy(p_res->ConsumerAddress, p_Address->data, sizeof(p_res->ConsumerAddress)-1);
        }
    }

    p_Topic = xml_node_soap_get(p_node, "Topic");
    if (p_Topic && p_Topic->data)
    {
        const char * p_Dialect = xml_attr_get(p_Topic, "Dialect");
        if (p_Dialect)
        {
            strncpy(p_res->Dialect, p_Dialect, sizeof(p_res->Dialect)-1);
        }
        
        strncpy(p_res->Topic, p_Topic->data, sizeof(p_res->Topic)-1);
    }

    p_ProducerReference = xml_node_soap_get(p_node, "ProducerReference");
    if (p_ProducerReference)
    {
        XMLN * p_Address = xml_node_soap_get(p_ProducerReference, "Address");
        if (p_Address && p_Address->data)
        {
            strncpy(p_res->ProducterAddress, p_Address->data, sizeof(p_res->ProducterAddress)-1);
        }
    }

    p_Message = xml_node_soap_get(p_node, "Message");
    if (p_Message)
    {
        parse_Message(p_Message, &p_res->Message);
    }
    
    return TRUE;
}

BOOL parse_NotifyMessage(XMLN * p_node, NotificationMessageList ** p_res)
{
    XMLN * p_NotificationMessage;
    
    p_NotificationMessage = xml_node_soap_get(p_node, "NotificationMessage");
    while (p_NotificationMessage && soap_strcmp(p_NotificationMessage->name, "NotificationMessage") == 0)
    {
        NotificationMessageList * p_notify = onvif_add_NotificationMessage(p_res);
        if (p_notify)
        {
            if (!parse_NotificationMessage(p_NotificationMessage, &p_notify->NotificationMessage))
            {
                onvif_free_NotificationMessages(p_res);
                return FALSE;
            }
        }

        p_NotificationMessage = p_NotificationMessage->next;
    }

    return TRUE;
}

BOOL parse_tev_GetServiceCapabilities(XMLN * p_node, tev_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_EventServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tev_GetEventProperties(XMLN * p_node, tev_GetEventProperties_RES * p_res)
{
    XMLN * p_TopicNamespaceLocation;
    XMLN * p_FixedTopicSet;
    XMLN * p_TopicSet;
    XMLN * p_TopicExpressionDialect;
    XMLN * p_MessageContentFilterDialect;
    XMLN * p_ProducerPropertiesFilterDialect;
    XMLN * p_MessageContentSchemaLocation;

    p_res->sizeTopicNamespaceLocation = 0;
    
    p_TopicNamespaceLocation = xml_node_soap_get(p_node, "TopicNamespaceLocation");
    while (p_TopicNamespaceLocation && p_TopicNamespaceLocation->data && 
        soap_strcmp(p_TopicNamespaceLocation->name, "TopicNamespaceLocation") == 0)
    {
        uint32 idx = p_res->sizeTopicNamespaceLocation;

        strncpy(p_res->TopicNamespaceLocation[idx], p_TopicNamespaceLocation->data, sizeof(p_res->TopicNamespaceLocation[idx])-1);

        p_res->sizeTopicNamespaceLocation++;
        if (p_res->sizeTopicNamespaceLocation >= ARRAY_SIZE(p_res->TopicNamespaceLocation))
        {
            break;
        }

        p_TopicNamespaceLocation = p_TopicNamespaceLocation->next;
    }

    p_FixedTopicSet = xml_node_soap_get(p_node, "FixedTopicSet");
    if (p_FixedTopicSet && p_FixedTopicSet->data)
    {
        p_res->FixedTopicSet = parse_Bool(p_FixedTopicSet->data);
    }

    p_TopicSet = xml_node_soap_get(p_node, "TopicSet");
    if (p_TopicSet)
    {
        int len = xml_calc_buf_len(p_TopicSet);
        
        p_res->TopicSet = (char *) malloc(len+8);
        if (p_res->TopicSet)
        {
            memset(p_res->TopicSet, 0, len+8);
            xml_write_buf(p_TopicSet, p_res->TopicSet, len);
        }
    }

    p_res->sizeTopicExpressionDialect = 0;
    
    p_TopicExpressionDialect = xml_node_soap_get(p_node, "TopicExpressionDialect");
    while (p_TopicExpressionDialect && p_TopicExpressionDialect->data && 
        soap_strcmp(p_TopicExpressionDialect->name, "TopicExpressionDialect") == 0)
    {
        uint32 idx = p_res->sizeTopicExpressionDialect;

        strncpy(p_res->TopicExpressionDialect[idx], 
            p_TopicExpressionDialect->data, 
            sizeof(p_res->TopicExpressionDialect[idx])-1);

        p_res->sizeTopicExpressionDialect++;
        if (p_res->sizeTopicExpressionDialect >= ARRAY_SIZE(p_res->TopicExpressionDialect))
        {
            break;
        }

        p_TopicExpressionDialect = p_TopicExpressionDialect->next;
    }

    p_res->sizeMessageContentFilterDialect = 0;
    
    p_MessageContentFilterDialect = xml_node_soap_get(p_node, "MessageContentFilterDialect");
    while (p_MessageContentFilterDialect && p_MessageContentFilterDialect->data && 
        soap_strcmp(p_MessageContentFilterDialect->name, "MessageContentFilterDialect") == 0)
    {
        uint32 idx = p_res->sizeMessageContentFilterDialect;

        strncpy(p_res->MessageContentFilterDialect[idx], 
            p_MessageContentFilterDialect->data, 
            sizeof(p_res->MessageContentFilterDialect[idx])-1);

        p_res->sizeMessageContentFilterDialect++;
        if (p_res->sizeMessageContentFilterDialect >= ARRAY_SIZE(p_res->MessageContentFilterDialect))
        {
            break;
        }

        p_MessageContentFilterDialect = p_MessageContentFilterDialect->next;
    }

    p_res->sizeProducerPropertiesFilterDialect = 0;
    
    p_ProducerPropertiesFilterDialect = xml_node_soap_get(p_node, "ProducerPropertiesFilterDialect");
    while (p_ProducerPropertiesFilterDialect && p_ProducerPropertiesFilterDialect->data && 
        soap_strcmp(p_ProducerPropertiesFilterDialect->name, "ProducerPropertiesFilterDialect") == 0)
    {
        uint32 idx = p_res->sizeProducerPropertiesFilterDialect;

        strncpy(p_res->ProducerPropertiesFilterDialect[idx], 
            p_ProducerPropertiesFilterDialect->data, 
            sizeof(p_res->ProducerPropertiesFilterDialect[idx])-1);

        p_res->sizeProducerPropertiesFilterDialect++;
        if (p_res->sizeProducerPropertiesFilterDialect >= ARRAY_SIZE(p_res->ProducerPropertiesFilterDialect))
        {
            break;
        }

        p_ProducerPropertiesFilterDialect = p_ProducerPropertiesFilterDialect->next;
    }

    p_res->sizeMessageContentSchemaLocation = 0;
    
    p_MessageContentSchemaLocation = xml_node_soap_get(p_node, "MessageContentSchemaLocation");
    while (p_MessageContentSchemaLocation && p_MessageContentSchemaLocation->data && 
        soap_strcmp(p_MessageContentSchemaLocation->name, "MessageContentSchemaLocation") == 0)
    {
        uint32 idx = p_res->sizeMessageContentSchemaLocation;

        strncpy(p_res->MessageContentSchemaLocation[idx], 
            p_MessageContentSchemaLocation->data, 
            sizeof(p_res->MessageContentSchemaLocation[idx])-1);

        p_res->sizeMessageContentSchemaLocation++;
        if (p_res->sizeMessageContentSchemaLocation >= ARRAY_SIZE(p_res->MessageContentSchemaLocation))
        {
            break;
        }

        p_MessageContentSchemaLocation = p_MessageContentSchemaLocation->next;
    }

    return TRUE;
}

BOOL parse_tev_Subscribe(XMLN * p_node, tev_Subscribe_RES * p_res)
{
    XMLN * p_SubscriptionReference;
    XMLN * p_Address;
    XMLN * p_ReferenceParameters;

    p_SubscriptionReference = xml_node_soap_get(p_node, "SubscriptionReference");
    if (NULL == p_SubscriptionReference)
    {
        return FALSE;
    }

    p_Address = xml_node_soap_get(p_SubscriptionReference, "Address");
    if (p_Address && p_Address->data)
    {
        onvif_parse_uri(p_Address->data, p_res->producter_addr, sizeof(p_res->producter_addr));
    }
    else if (p_SubscriptionReference->data)
    {
        onvif_parse_uri(p_SubscriptionReference->data, p_res->producter_addr, sizeof(p_res->producter_addr));
    }

    p_ReferenceParameters = xml_node_soap_get(p_SubscriptionReference, "ReferenceParameters");
    if (p_ReferenceParameters)
    {
        if (p_ReferenceParameters->data)
        {
            strncpy(p_res->ReferenceParameters, p_ReferenceParameters->data, sizeof(p_res->ReferenceParameters)-1);
        }
        else if (p_ReferenceParameters->f_child)
        {
            xml_write_buf(p_ReferenceParameters->f_child, p_res->ReferenceParameters, sizeof(p_res->ReferenceParameters)-1);
        }
    }

    return TRUE;
}

BOOL parse_tev_CreatePullPointSubscription(XMLN * p_node, tev_CreatePullPointSubscription_RES * p_res)
{
    XMLN * p_SubscriptionReference;
    XMLN * p_Address;
    XMLN * p_ReferenceParameters;
    XMLN * p_CurrentTime;
    XMLN * p_TerminationTime;

    p_SubscriptionReference = xml_node_soap_get(p_node, "SubscriptionReference");
    if (NULL == p_SubscriptionReference)
    {
        return FALSE;
    }

    p_Address = xml_node_soap_get(p_SubscriptionReference, "Address");
    if (p_Address && p_Address->data)
    {
        onvif_parse_uri(p_Address->data, p_res->producter_addr, sizeof(p_res->producter_addr));
    }
    else if (p_SubscriptionReference->data)
    {
        onvif_parse_uri(p_SubscriptionReference->data, p_res->producter_addr, sizeof(p_res->producter_addr));
    }

    p_ReferenceParameters = xml_node_soap_get(p_SubscriptionReference, "ReferenceParameters");
    if (p_ReferenceParameters)
    {
        if (p_ReferenceParameters->data)
        {
            strncpy(p_res->ReferenceParameters, p_ReferenceParameters->data, sizeof(p_res->ReferenceParameters)-1);
        }
        else if (p_ReferenceParameters->f_child)
        {
            xml_write_buf(p_ReferenceParameters->f_child, p_res->ReferenceParameters, sizeof(p_res->ReferenceParameters)-1);
        }
    }

    p_CurrentTime = xml_node_soap_get(p_node, "CurrentTime");
    if (p_CurrentTime && p_CurrentTime->data)
    {
        parse_XSDDatetime(p_CurrentTime->data, &p_res->CurrentTime);
    }

    p_TerminationTime = xml_node_soap_get(p_node, "TerminationTime");
    if (p_TerminationTime && p_TerminationTime->data)
    {
        parse_XSDDatetime(p_TerminationTime->data, &p_res->TerminationTime);
    }

    return TRUE;
}

BOOL parse_tev_PullMessages(XMLN * p_node, tev_PullMessages_RES * p_res)
{
    XMLN * p_CurrentTime;
    XMLN * p_TerminationTime;    
    
    p_CurrentTime = xml_node_soap_get(p_node, "CurrentTime");
    if (p_CurrentTime && p_CurrentTime->data)
    {
        parse_XSDDatetime(p_CurrentTime->data, &p_res->CurrentTime);
    }

    p_TerminationTime = xml_node_soap_get(p_node, "TerminationTime");
    if (p_TerminationTime && p_TerminationTime->data)
    {
        parse_XSDDatetime(p_TerminationTime->data, &p_res->TerminationTime);
    }

    return parse_NotifyMessage(p_node, &p_res->NotifyMessages);
}

BOOL parse_tev_GetMessages(XMLN * p_node, tev_GetMessages_RES * p_res)
{
    return parse_NotifyMessage(p_node, &p_res->NotifyMessages);
}

/***************************************************************************************/

BOOL parse_BacklightCompensationOptions(XMLN * p_node, onvif_BacklightCompensationOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Level;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (strcasecmp(p_Mode->data, "OFF") == 0)
        {
            p_res->Mode_OFF = 1;
        }
        else if (strcasecmp(p_Mode->data, "ON") == 0)
        {
            p_res->Mode_ON = 1;
        }

        p_Mode = p_Mode->next;
    }

    p_Level = xml_node_soap_get(p_node, "Level");
    if (p_Level)
    {
        p_res->LevelFlag = parse_FloatRange(p_Level, &p_res->Level);
    }

    return TRUE;
}

BOOL parse_ExposureOptions(XMLN * p_node, onvif_ExposureOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Priority;
    XMLN * p_MinExposureTime;
    XMLN * p_MaxExposureTime;
    XMLN * p_MinGain;
    XMLN * p_MaxGain;
    XMLN * p_MinIris;
    XMLN * p_MaxIris;
    XMLN * p_ExposureTime;
    XMLN * p_Gain;
    XMLN * p_Iris;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (strcasecmp(p_Mode->data, "Auto") == 0)
        {
            p_res->Mode_AUTO = 1;
        }
        else if (strcasecmp(p_Mode->data, "Manual") == 0)
        {
            p_res->Mode_MANUAL = 1;
        }

        p_Mode = p_Mode->next;
    }

    p_Priority = xml_node_soap_get(p_node, "Priority");
    while (p_Priority && p_Priority->data && soap_strcmp(p_Priority->name, "Priority") == 0)
    {
        if (strcasecmp(p_Priority->data, "LowNoise") == 0)
        {
            p_res->Priority_LowNoise = 1;
        }
        else if (strcasecmp(p_Priority->data, "FrameRate") == 0)
        {
            p_res->Priority_FrameRate = 1;
        }

        p_Priority = p_Priority->next;
    }
    
    p_MinExposureTime = xml_node_soap_get(p_node, "MinExposureTime");
    if (p_MinExposureTime)
    {
        p_res->MinExposureTimeFlag = parse_FloatRange(p_MinExposureTime, &p_res->MinExposureTime);
    }

    p_MaxExposureTime = xml_node_soap_get(p_node, "MaxExposureTime");
    if (p_MaxExposureTime)
    {
        p_res->MaxExposureTimeFlag = parse_FloatRange(p_MaxExposureTime, &p_res->MaxExposureTime);
    }

    p_MinGain = xml_node_soap_get(p_node, "MinGain");
    if (p_MinGain)
    {
        p_res->MinGainFlag = parse_FloatRange(p_MinGain, &p_res->MinGain);
    }

    p_MaxGain = xml_node_soap_get(p_node, "MaxGain");
    if (p_MaxGain)
    {
        p_res->MaxGainFlag = parse_FloatRange(p_MaxGain, &p_res->MaxGain);
    }

    p_MinIris = xml_node_soap_get(p_node, "MinIris");
    if (p_MinIris)
    {
        p_res->MinIrisFlag = parse_FloatRange(p_MinIris, &p_res->MinIris);
    }

    p_MaxIris = xml_node_soap_get(p_node, "MaxIris");
    if (p_MaxIris)
    {
        p_res->MaxIrisFlag = parse_FloatRange(p_MaxIris, &p_res->MaxIris);
    }

    p_ExposureTime = xml_node_soap_get(p_node, "ExposureTime");
    if (p_ExposureTime)
    {
        p_res->ExposureTimeFlag = parse_FloatRange(p_ExposureTime, &p_res->ExposureTime);
    }

    p_Gain = xml_node_soap_get(p_node, "Gain");
    if (p_Gain)
    {
        p_res->GainFlag = parse_FloatRange(p_Gain, &p_res->Gain);
    }

    p_Iris = xml_node_soap_get(p_node, "Iris");
    if (p_Iris)
    {
        p_res->IrisFlag = parse_FloatRange(p_Iris, &p_res->Iris);
    }

    return TRUE;
}

BOOL parse_FocusOptions(XMLN * p_node, onvif_FocusOptions * p_res)
{
    XMLN * p_AutoFocusModes;
    XMLN * p_DefaultSpeed;
    XMLN * p_NearLimit;
    XMLN * p_FarLimit;
    
    p_AutoFocusModes = xml_node_soap_get(p_node, "AutoFocusModes");
    while (p_AutoFocusModes && p_AutoFocusModes->data && 
           soap_strcmp(p_AutoFocusModes->name, "AutoFocusModes") == 0)
    {
        if (strcasecmp(p_AutoFocusModes->data, "Auto") == 0)
        {
            p_res->AutoFocusModes_AUTO = 1;
        }
        else if (strcasecmp(p_AutoFocusModes->data, "Manual") == 0)
        {
            p_res->AutoFocusModes_MANUAL = 1;
        }

        p_AutoFocusModes = p_AutoFocusModes->next;
    }

    p_DefaultSpeed = xml_node_soap_get(p_node, "DefaultSpeed");
    if (p_DefaultSpeed)
    {
        p_res->DefaultSpeedFlag = parse_FloatRange(p_DefaultSpeed, &p_res->DefaultSpeed);
    }

    p_NearLimit = xml_node_soap_get(p_node, "NearLimit");
    if (p_NearLimit)
    {
        p_res->NearLimitFlag = parse_FloatRange(p_NearLimit, &p_res->NearLimit);
    }

    p_FarLimit = xml_node_soap_get(p_node, "FarLimit");
    if (p_FarLimit)
    {
        p_res->FarLimitFlag = parse_FloatRange(p_FarLimit, &p_res->FarLimit);
    }

    return TRUE;
}

BOOL parse_WideDynamicRangeOptions(XMLN * p_node, onvif_WideDynamicRangeOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_Level;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (strcasecmp(p_Mode->data, "ON") == 0)
        {
            p_res->Mode_ON = 1;
        }
        else if (strcasecmp(p_Mode->data, "OFF") == 0)
        {
            p_res->Mode_OFF = 1;
        }

        p_Mode = p_Mode->next;
    }

    p_Level = xml_node_soap_get(p_node, "Level");
    if (p_Level)
    {
        p_res->LevelFlag = parse_FloatRange(p_Level, &p_res->Level);
    }

    return TRUE;
}

BOOL parse_WhiteBalanceOptions(XMLN * p_node, onvif_WhiteBalanceOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_YrGain;
    XMLN * p_YbGain;
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (strcasecmp(p_Mode->data, "Auto") == 0)
        {
            p_res->Mode_AUTO = 1;
        }
        else if (strcasecmp(p_Mode->data, "Manual") == 0)
        {
            p_res->Mode_MANUAL = 1;
        }

        p_Mode = p_Mode->next;
    }
    
    p_YrGain = xml_node_soap_get(p_node, "YrGain");
    if (p_YrGain)
    {
        p_res->YrGainFlag = parse_FloatRange(p_YrGain, &p_res->YrGain);
    }

    p_YbGain = xml_node_soap_get(p_node, "YbGain");
    if (p_YbGain)
    {
        p_res->YbGainFlag = parse_FloatRange(p_YrGain, &p_res->YbGain);
    }

    return TRUE;
}

BOOL parse_ImagingOptions(XMLN * p_node, onvif_ImagingOptions * p_res)
{
    XMLN * p_BacklightCompensation;
    XMLN * p_Brightness;
    XMLN * p_ColorSaturation;
    XMLN * p_Contrast;
    XMLN * p_Exposure;
    XMLN * p_Focus;
    XMLN * p_IrCutFilterModes;
    XMLN * p_Sharpness;
    XMLN * p_WideDynamicRange;
    XMLN * p_WhiteBalance;
    
    p_BacklightCompensation = xml_node_soap_get(p_node, "BacklightCompensation");
    if (p_BacklightCompensation)
    {
        p_res->BacklightCompensationFlag = parse_BacklightCompensationOptions(p_BacklightCompensation, &p_res->BacklightCompensation);
    }

    p_Brightness = xml_node_soap_get(p_node, "Brightness");
    if (p_Brightness)
    {
        p_res->BrightnessFlag = parse_FloatRange(p_Brightness, &p_res->Brightness);
    }

    p_ColorSaturation = xml_node_soap_get(p_node, "ColorSaturation");
    if (p_ColorSaturation)
    {
        p_res->ColorSaturationFlag = parse_FloatRange(p_ColorSaturation, &p_res->ColorSaturation);
    }

    p_Contrast = xml_node_soap_get(p_node, "Contrast");
    if (p_Contrast)
    {
        p_res->ContrastFlag = parse_FloatRange(p_Contrast, &p_res->Contrast);
    }

    p_Exposure = xml_node_soap_get(p_node, "Exposure");
    if (p_Exposure)
    {
        p_res->ExposureFlag = parse_ExposureOptions(p_Exposure, &p_res->Exposure);
    }

    p_Focus = xml_node_soap_get(p_node, "Focus");
    if (p_Focus)
    {
        p_res->FocusFlag = parse_FocusOptions(p_Focus, &p_res->Focus);
    }
    
    p_IrCutFilterModes = xml_node_soap_get(p_node, "IrCutFilterModes");
    while (p_IrCutFilterModes && soap_strcmp(p_IrCutFilterModes->name, "IrCutFilterModes") == 0)
    {
        if (p_IrCutFilterModes->data)
        {
            if (strcasecmp(p_IrCutFilterModes->data, "ON") == 0)
            {
                p_res->IrCutFilterMode_ON = 1;
            }
            else if (strcasecmp(p_IrCutFilterModes->data, "OFF") == 0)
            {
                p_res->IrCutFilterMode_OFF = 1;
            }
            else if (strcasecmp(p_IrCutFilterModes->data, "Auto") == 0)
            {
                p_res->IrCutFilterMode_AUTO = 1;
            }
        }

        p_IrCutFilterModes = p_IrCutFilterModes->next;
    }

    p_Sharpness = xml_node_soap_get(p_node, "Sharpness");
    if (p_Sharpness)
    {
        p_res->SharpnessFlag = parse_FloatRange(p_Sharpness, &p_res->Sharpness);
    }

    p_WideDynamicRange = xml_node_soap_get(p_node, "WideDynamicRange");
    if (p_WideDynamicRange)
    {
        p_res->WideDynamicRangeFlag = parse_WideDynamicRangeOptions(p_WideDynamicRange, &p_res->WideDynamicRange);
    }

    p_WhiteBalance = xml_node_soap_get(p_node, "WhiteBalance");
    if (p_WhiteBalance)
    {
        p_res->WhiteBalanceFlag = parse_WhiteBalanceOptions(p_WhiteBalance, &p_res->WhiteBalance);
    }

    return TRUE;
}

BOOL parse_ImagingPreset(XMLN * p_node, onvif_ImagingPreset * p_res)
{
    const char * p_token;
    const char * p_type;
    XMLN * p_Name;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_type = xml_attr_get(p_node, "type");
    if (p_type)
    {
        strncpy(p_res->type, p_type, sizeof(p_res->type)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    return TRUE;
}

BOOL parse_FocusStatus(XMLN * p_node, onvif_FocusStatus * p_res)
{
    XMLN * p_Position;
    XMLN * p_MoveStatus;
    XMLN * p_Error;
    
    p_Position = xml_node_soap_get(p_node, "Position");
    if (p_Position && p_Position->data)
    {
        p_res->Position = (float)atof(p_Position->data);
    }

    p_MoveStatus = xml_node_soap_get(p_node, "MoveStatus");
    if (p_MoveStatus && p_MoveStatus->data)
    {
        p_res->MoveStatus = onvif_StringToMoveStatus(p_MoveStatus->data);
    }

    p_Error = xml_node_soap_get(p_node, "Error");
    if (p_Error && p_Error->data)
    {
        p_res->ErrorFlag = 1;
        strncpy(p_res->Error, p_Error->data, sizeof(p_res->Error)-1);
    }

    return TRUE;
}

BOOL parse_ImagingStatus(XMLN * p_node, onvif_ImagingStatus * p_res)
{
    XMLN * p_FocusStatus20;

    p_FocusStatus20 = xml_node_soap_get(p_node, "FocusStatus20");
    if (p_FocusStatus20)
    {
        p_res->FocusStatusFlag = parse_FocusStatus(p_FocusStatus20, &p_res->FocusStatus);
    }

    return TRUE;
}

BOOL parse_AbsoluteFocusOptions(XMLN * p_node, onvif_AbsoluteFocusOptions * p_res)
{
    XMLN * p_Position;
    XMLN * p_Speed;

    p_Position = xml_node_soap_get(p_node, "Position");
    if (p_Position)
    {
        parse_FloatRange(p_Position, &p_res->Position);
    }

    p_Speed = xml_node_soap_get(p_node, "Speed");
    if (p_Speed)
    {
        p_res->SpeedFlag = parse_FloatRange(p_Speed, &p_res->Speed);
    }

    return TRUE;
}

BOOL parse_RelativeFocusOptions20(XMLN * p_node, onvif_RelativeFocusOptions20 * p_res)
{
    XMLN * p_Distance;
    XMLN * p_Speed;

    p_Distance = xml_node_soap_get(p_node, "Distance");
    if (p_Distance)
    {
        parse_FloatRange(p_Distance, &p_res->Distance);
    }

    p_Speed = xml_node_soap_get(p_node, "Speed");
    if (p_Speed)
    {
        p_res->SpeedFlag = parse_FloatRange(p_Speed, &p_res->Speed);
    }

    return TRUE;
}

BOOL parse_ContinuousFocusOptions(XMLN * p_node, onvif_ContinuousFocusOptions * p_res)
{
    XMLN * p_Speed;

    p_Speed = xml_node_soap_get(p_node, "Speed");
    if (p_Speed)
    {
        parse_FloatRange(p_Speed, &p_res->Speed);
    }

    return TRUE;
}

BOOL parse_MoveOptions20(XMLN * p_node, onvif_MoveOptions20 * p_res)
{
    XMLN * p_Absolute;
    XMLN * p_Relative;
    XMLN * p_Continuous;

    p_Absolute = xml_node_soap_get(p_node, "Absolute");
    if (p_Absolute)
    {
        p_res->AbsoluteFlag = parse_AbsoluteFocusOptions(p_Absolute, &p_res->Absolute);
    }

    p_Relative = xml_node_soap_get(p_node, "Relative");
    if (p_Relative)
    {
        p_res->RelativeFlag = parse_RelativeFocusOptions20(p_Relative, &p_res->Relative);
    }

    p_Continuous = xml_node_soap_get(p_node, "Continuous");
    if (p_Continuous)
    {
        p_res->ContinuousFlag = parse_ContinuousFocusOptions(p_Continuous, &p_res->Continuous);
    }

    return TRUE;
}

BOOL parse_img_GetServiceCapabilities(XMLN * p_node, img_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ImagingServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_img_GetImagingSettings(XMLN * p_node, img_GetImagingSettings_RES * p_res)
{
    XMLN * p_ImagingSettings;

    p_ImagingSettings = xml_node_soap_get(p_node, "ImagingSettings");
    if (NULL == p_ImagingSettings)
    {
        return FALSE;
    }
    
    return parse_ImagingSettings(p_ImagingSettings, &p_res->ImagingSettings);
}

BOOL parse_img_GetOptions(XMLN * p_node, img_GetOptions_RES * p_res)
{
    XMLN * p_ImagingOptions;

    p_ImagingOptions = xml_node_soap_get(p_node, "ImagingOptions");
    if (p_ImagingOptions)
    {
        parse_ImagingOptions(p_ImagingOptions, &p_res->ImagingOptions);
    }
    
    return TRUE;
}

BOOL parse_img_GetStatus(XMLN * p_node, img_GetStatus_RES * p_res)
{
    XMLN * p_Status;

    p_Status = xml_node_soap_get(p_node, "Status");
    if (p_Status)
    {
        parse_ImagingStatus(p_Status, &p_res->Status);
    }

    return TRUE;
}

BOOL parse_img_GetMoveOptions(XMLN * p_node, img_GetMoveOptions_RES * p_res)
{
    XMLN * p_MoveOptions;

    p_MoveOptions = xml_node_soap_get(p_node, "MoveOptions");
    if (p_MoveOptions)
    {
        parse_MoveOptions20(p_MoveOptions, &p_res->MoveOptions);
    }

    return TRUE;
}

BOOL parse_img_GetPresets(XMLN * p_node, img_GetPresets_RES * p_res)
{
    XMLN * p_Preset;

    p_Preset = xml_node_soap_get(p_node, "Preset");
    while (p_Preset && soap_strcmp(p_Preset->name, "Preset") == 0)
    {
        ImagingPresetList * p_new = onvif_add_ImagingPreset(&p_res->Preset);
        if (p_new)
        {
            parse_ImagingPreset(p_Preset, &p_new->Preset);
        }

        p_Preset = p_Preset->next;
    }

    return TRUE;
}

BOOL parse_img_GetCurrentPreset(XMLN * p_node, img_GetCurrentPreset_RES * p_res)
{
    XMLN * p_Preset;

    p_Preset = xml_node_soap_get(p_node, "Preset");
    if (p_Preset)
    {
        p_res->PresetFlag = parse_ImagingPreset(p_Preset, &p_res->Preset);
    }

    return TRUE;
}

/***************************************************************************************/

int parse_SimpleItemDescriptions(XMLN * p_node, const char * name, SimpleItemDescriptionList ** p_res)
{
    int idx = 0;
    XMLN * p_Item = xml_node_soap_get(p_node, name);
    while (p_Item && soap_strcmp(p_Item->name, name) == 0)
    {
        const char * p_Name;
        const char * p_Type;

        SimpleItemDescriptionList * p_item = onvif_add_SimpleItemDescription(p_res);

        p_Name = xml_attr_get(p_Item, "Name");
        if (p_Name)
        {
            strncpy(p_item->SimpleItemDescription.Name, p_Name, sizeof(p_item->SimpleItemDescription.Name)-1);
        }

        p_Type = xml_attr_get(p_Item, "Type");
        if (p_Type)
        {
            strncpy(p_item->SimpleItemDescription.Type, p_Type, sizeof(p_item->SimpleItemDescription.Type)-1);
        }

        p_Item = p_Item->next;
    }

    return idx;
}

BOOL parse_ItemListDescription(XMLN * p_node, onvif_ItemListDescription * p_res)
{
    parse_SimpleItemDescriptions(p_node, "SimpleItemDescription", &p_res->SimpleItemDescription);
    parse_SimpleItemDescriptions(p_node, "ElementItemDescription", &p_res->ElementItemDescription);

    return TRUE;
}

BOOL parse_ConfigDescription_Messages(XMLN * p_node, onvif_ConfigDescription_Messages * p_res)
{
    XMLN * p_Source;
    XMLN * p_Key;
    XMLN * p_Data;
    XMLN * p_ParentTopic;

    p_Source = xml_node_soap_get(p_node, "Source");
    if (p_Source)
    {
        p_res->SourceFlag = parse_ItemListDescription(p_Source, &p_res->Source);
    }

    p_Key = xml_node_soap_get(p_node, "Key");
    if (p_Key)
    {
        p_res->KeyFlag = parse_ItemListDescription(p_Key, &p_res->Key);
    }

    p_Data = xml_node_soap_get(p_node, "Data");
    if (p_Data)
    {
        p_res->DataFlag = parse_ItemListDescription(p_Data, &p_res->Data);
    }

    p_ParentTopic = xml_node_soap_get(p_node, "ParentTopic");
    if (p_ParentTopic && p_ParentTopic->data)
    {
        strncpy(p_res->ParentTopic, p_ParentTopic->data, sizeof(p_res->ParentTopic)-1);
    }
    
    return TRUE;
}

BOOL parse_RuleDescription(XMLN * p_node, onvif_ConfigDescription * p_res)
{
    XMLN * p_Parameters;
    XMLN * p_Messages;
    const char * p_Name;
    const char * p_fixed;
    const char * p_maxInstances;

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_fixed = xml_attr_get(p_node, "fixed");
    if (p_fixed)
    {
        p_res->fixedFlag = 1;
        p_res->fixed = parse_Bool(p_fixed);
    }

    p_maxInstances = xml_attr_get(p_node, "maxInstances");
    if (p_maxInstances)
    {
        p_res->maxInstancesFlag = 1;
        p_res->maxInstances = atoi(p_maxInstances);
    }

    p_Parameters = xml_node_soap_get(p_node, "Parameters");
    if (p_Parameters)
    {
        parse_ItemListDescription(p_Parameters, &p_res->Parameters);
    }

    p_Messages = xml_node_soap_get(p_node, "Messages");
    while (p_Messages && soap_strcmp(p_Messages->name, "Messages") == 0)
    {
        const char * p_IsProperty;
        ConfigDescription_MessagesList * p_item = onvif_add_ConfigDescription_Message(&p_res->Messages);

        p_IsProperty = xml_attr_get(p_node, "IsProperty");
        if (p_IsProperty)
        {
            p_item->Messages.IsPropertyFlag = 1;
            p_item->Messages.IsProperty = parse_Bool(p_IsProperty);
        }

        parse_ConfigDescription_Messages(p_Messages, &p_item->Messages);

        p_Messages = p_Messages->next;
    }

    return TRUE;
}

BOOL parse_ConfigDescription(XMLN * p_node, onvif_ConfigDescription * p_res)
{
    const char * p_Name;
    const char * p_fixed;
    const char * p_maxInstances;
    XMLN * p_Parameters;
    XMLN * p_Messages;
    
    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_fixed = xml_attr_get(p_node, "fixed");
    if (p_fixed)
    {
        p_res->fixedFlag = 1;
        p_res->fixed = parse_Bool(p_fixed);
    }

    p_maxInstances = xml_attr_get(p_node, "maxInstances");
    if (p_maxInstances)
    {
        p_res->maxInstancesFlag = 1;
        p_res->maxInstances = atoi(p_maxInstances);
    }

    p_Parameters = xml_node_soap_get(p_node, "Parameters");
    if (p_Parameters)
    {
        parse_ItemListDescription(p_Parameters, &p_res->Parameters);
    }

    p_Messages = xml_node_soap_get(p_node, "Messages");
    while (p_Messages && soap_strcmp(p_Messages->name, "Messages") == 0)
    {
        const char * p_IsProperty;
        ConfigDescription_MessagesList * p_item = onvif_add_ConfigDescription_Message(&p_res->Messages);

        p_IsProperty = xml_attr_get(p_node, "IsProperty");
        if (p_IsProperty)
        {
            p_item->Messages.IsPropertyFlag = 1;
            p_item->Messages.IsProperty = parse_Bool(p_IsProperty);
        }

        parse_ConfigDescription_Messages(p_Messages, &p_item->Messages);

        p_Messages = p_Messages->next;
    }

    return TRUE;
}

BOOL parse_ConfigOptions(XMLN * p_node, onvif_ConfigOptions * p_res)
{
    const char * p_RuleType;
    const char * p_Name;
    const char * p_Type;
    const char * p_AnalyticsModule;

    p_RuleType = xml_attr_get(p_node, "RuleType");
    if (p_RuleType)
    {
        p_res->RuleTypeFlag = 1;
        strncpy(p_res->RuleType, p_RuleType, sizeof(p_res->RuleType)-1);
    }

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
    }

    p_AnalyticsModule = xml_attr_get(p_node, "AnalyticsModule");
    if (p_AnalyticsModule)
    {
        p_res->AnalyticsModuleFlag = 1;
        strncpy(p_res->AnalyticsModule, p_AnalyticsModule, sizeof(p_res->AnalyticsModule)-1);
    }

    if (p_node->f_child)
    {
        int len = xml_calc_buf_len(p_node->f_child);
        
        p_res->any = (char *) malloc(len+8);
        if (p_res->any)
        {
            memset(p_res->any, 0, len+8);
            xml_write_buf(p_node->f_child, p_res->any, len);
        }
    }

    return TRUE;
}

BOOL parse_AnalyticsModuleConfigOptions(XMLN * p_node, onvif_AnalyticsModuleConfigOptions * p_res)
{
    const char * p_Type;

    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
    }

    return TRUE;
}

BOOL parse_SupportedAnalyticsModules(XMLN * p_node, onvif_SupportedAnalyticsModules * p_res)
{
    XMLN * p_AnalyticsModuleContentSchemaLocation;
    XMLN * p_AnalyticsModuleDescription;

    p_res->sizeAnalyticsModuleContentSchemaLocation = 0;
    
    p_AnalyticsModuleContentSchemaLocation = xml_node_soap_get(p_node, "AnalyticsModuleContentSchemaLocation");
    while (p_AnalyticsModuleContentSchemaLocation && 
           p_AnalyticsModuleContentSchemaLocation->data && 
           soap_strcmp(p_AnalyticsModuleContentSchemaLocation->name, "AnalyticsModuleContentSchemaLocation") == 0)
    {
        uint32 idx = p_res->sizeAnalyticsModuleContentSchemaLocation;

        strncpy(p_res->AnalyticsModuleContentSchemaLocation[idx],
            p_AnalyticsModuleContentSchemaLocation->data, 
            sizeof(p_res->AnalyticsModuleContentSchemaLocation[idx]));

        p_res->sizeAnalyticsModuleContentSchemaLocation++;
        if (p_res->sizeAnalyticsModuleContentSchemaLocation >= 
            ARRAY_SIZE(p_res->AnalyticsModuleContentSchemaLocation))
        {
            break;
        }

        p_AnalyticsModuleContentSchemaLocation = p_AnalyticsModuleContentSchemaLocation->next;
    }

    p_AnalyticsModuleDescription = xml_node_soap_get(p_node, "AnalyticsModuleDescription");
    while (p_AnalyticsModuleDescription && 
           soap_strcmp(p_AnalyticsModuleDescription->name, "AnalyticsModuleDescription") == 0)
    {
        ConfigDescriptionList * p_item = onvif_add_ConfigDescription(&p_res->AnalyticsModuleDescription);
        if (p_item)
        {
            parse_ConfigDescription(p_AnalyticsModuleDescription, &p_item->ConfigDescription);
        }

        p_AnalyticsModuleDescription = p_AnalyticsModuleDescription->next;
    }

    return TRUE;
}

BOOL parse_MetadataInfo(XMLN * p_node, onvif_MetadataInfo * p_res)
{
    const char * p_Type;
    
    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        if (p_node->l_attrib && soap_strcmp(p_node->l_attrib->name, "Type"))
        {
            strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
            
            p_res->attrFlag = 1;
            snprintf(p_res->attr, sizeof(p_res->attr)-1, "%s=\"%s\"", p_node->l_attrib->name, p_node->l_attrib->data);
        }
        else
        {
            const char * p = strchr(p_Type, ':');
            if (p)
            {
                strncpy(p_res->Type, p+1, sizeof(p_res->Type)-1);
            }
            else
            {
                strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
            }
        }
    }

    if (p_node->f_child)
    {
        int len = xml_calc_buf_len(p_node->f_child);
        
        p_res->Frame = (char *) malloc(len+8);
        if (p_res->Frame)
        {
            memset(p_res->Frame, 0, len+8);
            xml_write_buf(p_node->f_child, p_res->Frame, len);
        }
    }

    return TRUE;
}

BOOL parse_tan_GetServiceCapabilities(XMLN * p_node, tan_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_AnalyticsServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tan_GetSupportedRules(XMLN * p_node, tan_GetSupportedRules_RES * p_res)
{
    XMLN * p_SupportedRules;
    XMLN * p_RuleContentSchemaLocation;
    XMLN * p_RuleDescription;
    const char * p_Limit;

    /* 响应体结构:GetSupportedRulesResponse > SupportedRules > {Limit 属性, RuleContentSchemaLocation*, RuleDescription*}。
     * xml_node_soap_get 是浅层查找(只看直接子节点),必须先下钻到 SupportedRules 这一层。 */
    p_SupportedRules = xml_node_soap_get(p_node, "SupportedRules");
    if (p_SupportedRules == NULL)
    {
        return TRUE;   /* 无 SupportedRules:回空,不算失败(与原语义一致) */
    }

    p_Limit = xml_attr_get(p_SupportedRules, "Limit");
    if (p_Limit)
    {
        p_res->SupportedRules.LimitFlag = 1;
        p_res->SupportedRules.Limit = atoi(p_Limit);
    }

    p_res->SupportedRules.sizeRuleContentSchemaLocation = 0;

    p_RuleContentSchemaLocation = xml_node_soap_get(p_SupportedRules, "RuleContentSchemaLocation");
    while (p_RuleContentSchemaLocation && 
        p_RuleContentSchemaLocation->data && 
        soap_strcmp(p_RuleContentSchemaLocation->name, "RuleContentSchemaLocation") == 0)
    {
        uint32 idx = p_res->SupportedRules.sizeRuleContentSchemaLocation;

        strncpy(p_res->SupportedRules.RuleContentSchemaLocation[idx], p_RuleContentSchemaLocation->data, sizeof(p_res->SupportedRules.RuleContentSchemaLocation[idx])-1);

        p_res->SupportedRules.sizeRuleContentSchemaLocation++;
        if (p_res->SupportedRules.sizeRuleContentSchemaLocation >= ARRAY_SIZE(p_res->SupportedRules.RuleContentSchemaLocation))
        {
            break;
        }

        p_RuleContentSchemaLocation = p_RuleContentSchemaLocation->next;
    }

    p_RuleDescription = xml_node_soap_get(p_SupportedRules, "RuleDescription");
    while (p_RuleDescription && soap_strcmp(p_RuleDescription->name, "RuleDescription") == 0)
    {
        ConfigDescriptionList * p_cfg_desc = onvif_add_ConfigDescription(&p_res->SupportedRules.RuleDescription);
        if (p_cfg_desc)
        {    
            if (!parse_RuleDescription(p_RuleDescription, &p_cfg_desc->ConfigDescription))
            {
                onvif_free_ConfigDescriptions(&p_res->SupportedRules.RuleDescription);
                return FALSE;
            }
        }
        
        p_RuleDescription = p_RuleDescription->next;
    }
    
    return TRUE;
}

BOOL parse_tan_GetRules(XMLN * p_node, tan_GetRules_RES * p_res)
{
    XMLN * p_Rule;
    
    p_Rule = xml_node_soap_get(p_node, "Rule");
    while (p_Rule && soap_strcmp(p_Rule->name, "Rule") == 0)
    {
        ConfigList * p_config = onvif_add_Config(&p_res->Rule);
        if (p_config)
        {
            if (!parse_Config(p_Rule, &p_config->Config))
            {
                onvif_free_Configs(&p_res->Rule);
                return FALSE;
            }
        }
        
        p_Rule = p_Rule->next;
    }

    return TRUE;
}

BOOL parse_tan_GetAnalyticsModules(XMLN * p_node, tan_GetAnalyticsModules_RES * p_res)
{
    XMLN * p_Rule;
    
    p_Rule = xml_node_soap_get(p_node, "AnalyticsModule");
    while (p_Rule && soap_strcmp(p_Rule->name, "AnalyticsModule") == 0)
    {
        ConfigList * p_config = onvif_add_Config(&p_res->AnalyticsModule);
        if (p_config)
        {
            if (!parse_Config(p_Rule, &p_config->Config))
            {
                onvif_free_Configs(&p_res->AnalyticsModule);
                return FALSE;
            }
        }
        
        p_Rule = p_Rule->next;
    }

    return TRUE;
}

BOOL parse_tan_GetSupportedAnalyticsModules(XMLN * p_node, tan_GetSupportedAnalyticsModules_RES * p_res)
{
    XMLN * p_SupportedAnalyticsModules;

    p_SupportedAnalyticsModules = xml_node_soap_get(p_node, "SupportedAnalyticsModules");
    if (p_SupportedAnalyticsModules)
    {
        parse_SupportedAnalyticsModules(p_SupportedAnalyticsModules, &p_res->SupportedAnalyticsModules);
    }

    return TRUE;
}

BOOL parse_tan_GetRuleOptions(XMLN * p_node, tan_GetRuleOptions_RES * p_res)
{
    XMLN * p_RuleOptions;

    p_RuleOptions = xml_node_soap_get(p_node, "RuleOptions");
    while (p_RuleOptions && soap_strcmp(p_RuleOptions->name, "RuleOptions") == 0)
    {
        ConfigOptionsList * p_item = onvif_add_ConfigOptions(&p_res->RuleOptions);
        if (p_item)
        {
            parse_ConfigOptions(p_RuleOptions, &p_item->Options);
        }

        p_RuleOptions = p_RuleOptions->next;
    }

    return TRUE;
}

BOOL parse_tan_GetAnalyticsModuleOptions(XMLN * p_node, tan_GetAnalyticsModuleOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    while (p_Options && soap_strcmp(p_Options->name, "Options") == 0)
    {
        AnalyticsModuleConfigOptionsList * p_item = onvif_add_AnalyticsModuleConfigOptions(&p_res->Options);
        if (p_item)
        {
            parse_AnalyticsModuleConfigOptions(p_Options, &p_item->Options);
        }

        p_Options = p_Options->next;
    }

    return TRUE;
}

BOOL parse_tan_GetSupportedMetadata(XMLN * p_node, tan_GetSupportedMetadata_RES * p_res)
{
    XMLN * p_AnalyticsModule;

    p_AnalyticsModule = xml_node_soap_get(p_node, "AnalyticsModule");
    while (p_AnalyticsModule && soap_strcmp(p_AnalyticsModule->name, "AnalyticsModule") == 0)
    {
        MetadataInfoList * p_item = onvif_add_MetadataInfo(&p_res->AnalyticsModule);
        if (p_item)
        {
            parse_MetadataInfo(p_AnalyticsModule, &p_item->MetadataInfo);
        }

        p_AnalyticsModule = p_AnalyticsModule->next;
    }

    return TRUE;
}

/***************************************************************************************/

BOOL parse_VideoRateControl2(XMLN * p_node, onvif_VideoRateControl2 * p_res)
{
    XMLN * p_FrameRateLimit;
    XMLN * p_BitrateLimit;
    const char * p_ConstantBitRate;
    
    p_ConstantBitRate = xml_attr_get(p_node, "ConstantBitRate");
    if (p_ConstantBitRate)
    {
        p_res->ConstantBitRateFlag = 1;
        p_res->ConstantBitRate = parse_Bool(p_ConstantBitRate);
    }
    
    p_FrameRateLimit = xml_node_soap_get(p_node, "FrameRateLimit");
    if (p_FrameRateLimit && p_FrameRateLimit->data)
    {
        p_res->FrameRateLimit = (float)atof(p_FrameRateLimit->data);
    }

    p_BitrateLimit = xml_node_soap_get(p_node, "BitrateLimit");
    if (p_BitrateLimit && p_BitrateLimit->data)
    {
        p_res->BitrateLimit = atoi(p_BitrateLimit->data);
    }

    return TRUE;
}

BOOL parse_VideoEncoder2Configuration(XMLN * p_node, onvif_VideoEncoder2Configuration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_Encoding;
    XMLN * p_Resolution;
    XMLN * p_Quality;
    XMLN * p_RateControl;
    XMLN * p_Multicast;
    const char * p_token;
    const char * p_GovLength;
    const char * p_AnchorFrameDistance;
    const char * p_Profile;
    const char * p_GuaranteedFrameRate;
    const char * p_Signed;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_GovLength = xml_attr_get(p_node, "GovLength");
    if (p_GovLength)
    {
        p_res->GovLengthFlag = 1;
        p_res->GovLength = atoi(p_GovLength);
    }

    p_AnchorFrameDistance = xml_attr_get(p_node, "AnchorFrameDistance");
    if (p_AnchorFrameDistance)
    {
        p_res->AnchorFrameDistanceFlag = 1;
        p_res->AnchorFrameDistance = atoi(p_AnchorFrameDistance);
    }

    p_Profile = xml_attr_get(p_node, "Profile");
    if (p_Profile)
    {
        p_res->ProfileFlag = 1;
        strncpy(p_res->Profile, p_Profile, sizeof(p_res->Profile)-1);
    }

    p_GuaranteedFrameRate = xml_attr_get(p_node, "GuaranteedFrameRate");
    if (p_GuaranteedFrameRate)
    {
        p_res->GuaranteedFrameRate = parse_Bool(p_GuaranteedFrameRate);
    }

    p_Signed = xml_attr_get(p_node, "Signed");
    if (p_Signed)
    {
        p_res->Signed = parse_Bool(p_Signed);
    }
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        strncpy(p_res->Encoding, p_Encoding->data, sizeof(p_res->Encoding)-1);
    }

    p_Resolution = xml_node_soap_get(p_node, "Resolution");
    if (p_Resolution)
    {
        parse_VideoResolution(p_Resolution, &p_res->Resolution);
    }

    p_RateControl = xml_node_soap_get(p_node, "RateControl");
    if (p_RateControl)
    {
        p_res->RateControlFlag = parse_VideoRateControl2(p_RateControl, &p_res->RateControl);
    }

    p_Multicast = xml_node_soap_get(p_node, "Multicast");
    if (p_Multicast)
    {
        p_res->MulticastFlag = parse_MulticastConfiguration(p_Multicast, &p_res->Multicast);
    }
    
    p_Quality = xml_node_soap_get(p_node, "Quality");
    if (p_Quality && p_Quality->data)
    {
        p_res->Quality = (float)atof(p_Quality->data);
    }

    return TRUE;
}

BOOL parse_VideoEncoder2ConfigurationOptions(XMLN * p_node, onvif_VideoEncoder2ConfigurationOptions * p_res)
{
    uint32 i = 0;
    XMLN * p_Encoding;
    XMLN * p_QualityRange;
    XMLN * p_ResolutionsAvailable;
    XMLN * p_BitrateRange;
    const char * p_GovLengthRange;
    const char * p_MaxAnchorFrameDistance;
    const char * p_FrameRatesSupported;
    const char * p_ProfilesSupported;
    const char * p_ConstantBitRateSupported;
    const char * p_GuaranteedFrameRateSupported;

    p_GovLengthRange = xml_attr_get(p_node, "GovLengthRange");
    if (p_GovLengthRange)
    {
        p_res->GovLengthRangeFlag = 1;
        strncpy(p_res->GovLengthRange, p_GovLengthRange, sizeof(p_res->GovLengthRange)-1);
    }

    p_MaxAnchorFrameDistance = xml_attr_get(p_node, "MaxAnchorFrameDistance");
    if (p_MaxAnchorFrameDistance)
    {
        p_res->MaxAnchorFrameDistanceFlag = 1;
        p_res->MaxAnchorFrameDistance = atoi(p_MaxAnchorFrameDistance);
    }
    
    p_FrameRatesSupported = xml_attr_get(p_node, "FrameRatesSupported");
    if (p_FrameRatesSupported)
    {
        p_res->FrameRatesSupportedFlag = 1;
        strncpy(p_res->FrameRatesSupported, p_FrameRatesSupported, sizeof(p_res->FrameRatesSupported)-1);
    }

    p_ProfilesSupported = xml_attr_get(p_node, "ProfilesSupported");
    if (p_ProfilesSupported)
    {
        p_res->ProfilesSupportedFlag = 1;
        strncpy(p_res->ProfilesSupported, p_ProfilesSupported, sizeof(p_res->ProfilesSupported)-1);
    }

    p_ConstantBitRateSupported = xml_attr_get(p_node, "ConstantBitRateSupported");
    if (p_ConstantBitRateSupported)
    {
        p_res->ConstantBitRateSupported = parse_Bool(p_ConstantBitRateSupported);
    }

    p_GuaranteedFrameRateSupported = xml_attr_get(p_node, "GuaranteedFrameRateSupported");
    if (p_GuaranteedFrameRateSupported)
    {
        p_res->GuaranteedFrameRateSupported = parse_Bool(p_GuaranteedFrameRateSupported);
    }
    
    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        strncpy(p_res->Encoding, p_Encoding->data, sizeof(p_res->Encoding)-1);
    }

    p_QualityRange = xml_node_soap_get(p_node, "QualityRange");
    if (p_QualityRange)
    {
        parse_FloatRange(p_QualityRange, &p_res->QualityRange);
    }

    p_ResolutionsAvailable = xml_node_soap_get(p_node, "ResolutionsAvailable");
    while (p_ResolutionsAvailable && soap_strcmp(p_ResolutionsAvailable->name, "ResolutionsAvailable") == 0)
    {
        parse_VideoResolution(p_ResolutionsAvailable, &p_res->ResolutionsAvailable[i++]);

        if (i >= ARRAY_SIZE(p_res->ResolutionsAvailable))
        {
            break;
        }
        
        p_ResolutionsAvailable = p_ResolutionsAvailable->next;
    }

    p_BitrateRange = xml_node_soap_get(p_node, "BitrateRange");
    if (p_BitrateRange)
    {
        parse_IntRange(p_BitrateRange, &p_res->BitrateRange);
    }

    return TRUE;
}

BOOL parse_PTZFilter(XMLN * p_node, onvif_PTZFilter * p_res)
{
    XMLN * p_Status;
    XMLN * p_Position;
    
    p_Status = xml_node_soap_get(p_node, "Status");
    if (p_Status && p_Status->data)
    {
        p_res->Status = parse_Bool(p_Status->data);
    }

    p_Position = xml_node_soap_get(p_node, "Position");
    if (p_Position && p_Position->data)
    {
        p_res->Position = parse_Bool(p_Position->data);
    }

    return TRUE;
}

BOOL parse_EventSubscription(XMLN * p_node, onvif_EventSubscription * p_res)
{
    XMLN * p_Filter;
    
    p_Filter = xml_node_soap_get(p_node, "Filter");
    if (p_Filter)
    {
        XMLN * p_TopicExpression;
        
        p_TopicExpression = xml_node_soap_get(p_Filter, "TopicExpression");
        if (p_TopicExpression && p_TopicExpression->data)
        {
            const char * p_Dialect;

            p_Dialect = xml_attr_get(p_TopicExpression, "Dialect");
            if (p_Dialect)
            {
                strncpy(p_res->Dialect, p_Dialect, sizeof(p_res->Dialect)-1);
            }
            
            strncpy(p_res->TopicExpression, p_TopicExpression->data, sizeof(p_res->TopicExpression)-1);
        }
    }

    return TRUE;
}

BOOL parse_MetadataConfiguration(XMLN * p_node, onvif_MetadataConfiguration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_PTZStatus;
    XMLN * p_Events;
    XMLN * p_Analytics;
    XMLN * p_Multicast;
    XMLN * p_SessionTimeout;
    XMLN * p_AnalyticsEngineConfiguration;
    const char * p_token;
    const char * p_CompressionType;
    const char * p_GeoLocation;
    const char * p_ShapePolygon;
    
    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_CompressionType = xml_attr_get(p_node, "CompressionType");
    if (p_CompressionType)
    {
        p_res->CompressionTypeFlag = 1;
        strncpy(p_res->CompressionType, p_CompressionType, sizeof(p_res->CompressionType)-1);
    }

    p_GeoLocation = xml_attr_get(p_node, "GeoLocation");
    if (p_GeoLocation)
    {
        p_res->GeoLocation = parse_Bool(p_GeoLocation);
    }

    p_ShapePolygon = xml_attr_get(p_node, "ShapePolygon");
    if (p_ShapePolygon)
    {
        p_res->ShapePolygon = parse_Bool(p_ShapePolygon);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_PTZStatus = xml_node_soap_get(p_node, "PTZStatus");
    if (p_PTZStatus)
    {
        p_res->PTZStatusFlag = parse_PTZFilter(p_PTZStatus, &p_res->PTZStatus);
    }

    p_Events = xml_node_soap_get(p_node, "Events");
    if (p_Events)
    {
        p_res->EventsFlag = parse_EventSubscription(p_Events, &p_res->Events);
    }

    p_Analytics = xml_node_soap_get(p_node, "Analytics");
    if (p_Analytics && p_Analytics->data)
    {
        p_res->AnalyticsFlag = 1;
        p_res->Analytics = parse_Bool(p_Analytics->data);
    }

    p_Multicast = xml_node_soap_get(p_node, "Multicast");
    if (p_Multicast)
    {
        parse_MulticastConfiguration(p_Multicast, &p_res->Multicast);
    }
    
    p_SessionTimeout = xml_node_soap_get(p_node, "SessionTimeout");
    if (p_SessionTimeout && p_SessionTimeout->data)
    {
        parse_XSDDuration(p_SessionTimeout->data, &p_res->SessionTimeout);
    }

    p_AnalyticsEngineConfiguration = xml_node_soap_get(p_node, "AnalyticsEngineConfiguration");
    if (p_AnalyticsEngineConfiguration)
    {
        p_res->AnalyticsEngineConfigurationFlag = parse_AnalyticsEngineConfiguration(p_AnalyticsEngineConfiguration, &p_res->AnalyticsEngineConfiguration);
    }
    
    return TRUE;
}

BOOL parse_AudioEncoder2Configuration(XMLN * p_node, onvif_AudioEncoder2Configuration * p_res)
{
    XMLN * p_Name;
    XMLN * p_UseCount;
    XMLN * p_Encoding;
    XMLN * p_Bitrate;
    XMLN * p_SampleRate;
    XMLN * p_Multicast;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_UseCount = xml_node_soap_get(p_node, "UseCount");
    if (p_UseCount && p_UseCount->data)
    {
        p_res->UseCount = atoi(p_UseCount->data);
    }

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        strncpy(p_res->Encoding, p_Encoding->data, sizeof(p_res->Encoding)-1);
    }

    p_Multicast = xml_node_soap_get(p_node, "Multicast");
    if (p_Multicast)
    {
        p_res->MulticastFlag = parse_MulticastConfiguration(p_Multicast, &p_res->Multicast);
    }
    
    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate && p_Bitrate->data)
    {
        p_res->Bitrate = atoi(p_Bitrate->data);
    }

    p_SampleRate = xml_node_soap_get(p_node, "SampleRate");
    if (p_SampleRate && p_SampleRate->data)
    {
        p_res->SampleRate = atoi(p_SampleRate->data);
    }

    return TRUE;
}

BOOL parse_AudioEncoder2ConfigurationOptions(XMLN * p_node, onvif_AudioEncoder2ConfigurationOptions * p_res)
{
    XMLN * p_Encoding;
    XMLN * p_BitrateList;
    XMLN * p_SampleRateList;

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        strncpy(p_res->Encoding, p_Encoding->data, sizeof(p_res->Encoding)-1);
    }

    p_BitrateList = xml_node_soap_get(p_node, "BitrateList");
    if (p_BitrateList)
    {
        parse_IntList(p_BitrateList, &p_res->BitrateList);
    }

    p_SampleRateList = xml_node_soap_get(p_node, "SampleRateList");
    if (p_SampleRateList)
    {
        parse_IntList(p_SampleRateList, &p_res->SampleRateList);
    }

    return TRUE;
}

BOOL parse_ConfigurationSet(XMLN * p_node, onvif_ConfigurationSet * p_res)
{
    XMLN * p_VideoSource;
    XMLN * p_AudioSource;
    XMLN * p_VideoEncoder;
    XMLN * p_AudioEncoder;
    XMLN * p_Analytics;
    XMLN * p_PTZ;
    XMLN * p_Metadata;
    XMLN * p_AudioOutput;
    XMLN * p_AudioDecoder;

    p_VideoSource = xml_node_soap_get(p_node, "VideoSource");
    if (p_VideoSource)
    {
        p_res->VideoSourceFlag = parse_VideoSourceConfiguration(p_VideoSource, &p_res->VideoSource);
    }

    p_AudioSource = xml_node_soap_get(p_node, "AudioSource");
    if (p_AudioSource)
    {
        p_res->AudioSourceFlag = parse_AudioSourceConfiguration(p_AudioSource, &p_res->AudioSource);
    }
    
    p_VideoEncoder = xml_node_soap_get(p_node, "VideoEncoder");
    if (p_VideoEncoder)
    {
        p_res->VideoEncoderFlag = parse_VideoEncoder2Configuration(p_VideoEncoder, &p_res->VideoEncoder);
    }

    p_AudioEncoder = xml_node_soap_get(p_node, "AudioEncoder");
    if (p_AudioEncoder)
    {
        p_res->AudioEncoderFlag = parse_AudioEncoder2Configuration(p_AudioEncoder, &p_res->AudioEncoder);
    }

    p_Analytics = xml_node_soap_get(p_node, "Analytics");
    if (p_Analytics)
    {
        p_res->AnalyticsFlag = parse_VideoAnalyticsConfiguration(p_Analytics, &p_res->Analytics);
    }
    
    p_PTZ = xml_node_soap_get(p_node, "PTZ");
    if (p_PTZ)
    {
        p_res->PTZFlag = parse_PTZConfiguration(p_PTZ, &p_res->PTZ);
    }   

    p_Metadata = xml_node_soap_get(p_node, "Metadata");
    if (p_Metadata)
    {
        p_res->MetadataFlag = parse_MetadataConfiguration(p_Metadata, &p_res->Metadata);
    }

    p_AudioOutput = xml_node_soap_get(p_node, "AudioOutput");
    if (p_AudioOutput)
    {
        p_res->AudioOutputFlag = parse_AudioOutputConfiguration(p_AudioOutput, &p_res->AudioOutput);
    }

    p_AudioDecoder = xml_node_soap_get(p_node, "AudioDecoder");
    if (p_AudioDecoder)
    {
        p_res->AudioDecoderFlag = parse_AudioDecoderConfiguration(p_AudioDecoder, &p_res->AudioDecoder);
    }

    return TRUE;
}

BOOL parse_MediaProfile(XMLN * p_node, onvif_MediaProfile * p_res)
{
    XMLN * p_Name;
    XMLN * p_Configurations;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    if (p_Configurations)
    {
        parse_ConfigurationSet(p_Configurations, &p_res->Configurations);
    }
    
    return TRUE;
}

BOOL parse_Polygon(XMLN * p_node, onvif_Polygon * p_res)
{
    XMLN * p_Point;

    p_res->sizePoint = 0;
    
    p_Point = xml_node_soap_get(p_node, "Point");
    while (p_Point && soap_strcmp(p_Point->name, "Point") == 0)
    {
        uint32 idx = p_res->sizePoint;
        
        parse_Vector(p_Point, &p_res->Point[idx]);

        p_res->sizePoint++;
        if (p_res->sizePoint >= ARRAY_SIZE(p_res->Point))
        {
            break;
        }
        
        p_Point = p_Point->next;
    }

    return TRUE;
}

BOOL parse_Mask(XMLN * p_node, onvif_Mask * p_res)
{
    XMLN * p_ConfigurationToken;
    XMLN * p_Polygon;
    XMLN * p_Type;
    XMLN * p_Color;
    XMLN * p_Enabled;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_ConfigurationToken = xml_node_soap_get(p_node, "ConfigurationToken");
    if (p_ConfigurationToken && p_ConfigurationToken->data)
    {
        strncpy(p_res->ConfigurationToken, p_ConfigurationToken->data, sizeof(p_res->ConfigurationToken)-1);
    }

    p_Polygon = xml_node_soap_get(p_node, "Polygon");
    if (p_Polygon)
    {
        parse_Polygon(p_Polygon, &p_res->Polygon);
    }

    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type && p_Type->data)
    {
        strncpy(p_res->Type, p_Type->data, sizeof(p_res->Type)-1);
    }

    p_Color = xml_node_soap_get(p_node, "Color");
    if (p_Color)
    {
        p_res->ColorFlag = parse_Color(p_Color, &p_res->Color);
    }

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    return TRUE;
}

BOOL parse_MaskOptions(XMLN * p_node, onvif_MaskOptions * p_res)
{
    XMLN * p_MaxMasks;
    XMLN * p_MaxPoints;
    XMLN * p_Types;
    XMLN * p_Color;    
    const char * p_RectangleOnly;
    const char * p_SingleColorOnly;

    p_RectangleOnly = xml_attr_get(p_node, "RectangleOnly");
    if (p_RectangleOnly)
    {
        p_res->RectangleOnly = parse_Bool(p_RectangleOnly);
    }

    p_SingleColorOnly = xml_attr_get(p_node, "SingleColorOnly");
    if (p_SingleColorOnly)
    {
        p_res->SingleColorOnly = parse_Bool(p_SingleColorOnly);
    }

    p_MaxMasks = xml_node_soap_get(p_node, "MaxMasks");
    if (p_MaxMasks && p_MaxMasks->data)
    {
        p_res->MaxMasks = atoi(p_MaxMasks->data);
    }

    p_MaxPoints = xml_node_soap_get(p_node, "MaxPoints");
    if (p_MaxPoints && p_MaxPoints->data)
    {
        p_res->MaxPoints = atoi(p_MaxPoints->data);
    }

    p_res->sizeTypes = 0;
    
    p_Types = xml_node_soap_get(p_node, "Types");
    while (p_Types && p_Types->data && soap_strcmp(p_Types->name, "Types") == 0)
    {
        uint32 idx = p_res->sizeTypes;

        strncpy(p_res->Types[idx], p_Types->data, sizeof(p_res->Types[idx])-1);

        p_res->sizeTypes++;
        if (p_res->sizeTypes > ARRAY_SIZE(p_res->Types))
        {
            break;
        }
        
        p_Types = p_Types->next;
    }

    p_Color = xml_node_soap_get(p_node, "Color");
    if (p_Color)
    {
        parse_ColorOptions(p_Color, &p_res->Color);
    }

    return TRUE;
}

BOOL parse_EncoderInstance(XMLN * p_node, onvif_EncoderInstance * p_res)
{
    XMLN * p_Encoding;
    XMLN * p_Number;

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        strncpy(p_res->Encoding, p_Encoding->data, sizeof(p_res->Encoding)-1);
    }

    p_Number = xml_node_soap_get(p_node, "Number");
    if (p_Number && p_Number->data)
    {
        p_res->Number = atoi(p_Number->data);
    }

    return TRUE;
}

BOOL parse_EncoderInstanceInfo(XMLN * p_node, onvif_EncoderInstanceInfo * p_res)
{
    XMLN * p_Codec;
    XMLN * p_Total;

    p_res->sizeCodec = 0;
    
    p_Codec = xml_node_soap_get(p_node, "Codec");
    while (p_Codec && soap_strcmp(p_Codec->name, "Codec") == 0)
    {
        uint32 idx = p_res->sizeCodec;

        parse_EncoderInstance(p_Codec, &p_res->Codec[idx]);
        
        p_res->sizeCodec++;
        if (p_res->sizeCodec >= ARRAY_SIZE(p_res->Codec))
        {
            break;
        }

        p_Codec = p_Codec->next;
    }

    p_Total = xml_node_soap_get(p_node, "Total");
    if (p_Total && p_Total->data)
    {
        p_res->Total = atoi(p_Total->data);
    }

    return TRUE;
}

BOOL parse_tr2_GetServiceCapabilities(XMLN * p_node, tr2_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_Media2ServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tr2_GetVideoEncoderConfigurations(XMLN * p_node, tr2_GetVideoEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        VideoEncoder2ConfigurationList * p_v_enc = onvif_add_VideoEncoder2Configuration(&p_res->Configurations);
        if (p_v_enc)
        {
            if (!parse_VideoEncoder2Configuration(p_Configurations, &p_v_enc->Configuration))
            {
                onvif_free_VideoEncoder2Configurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetVideoEncoderConfigurationOptions(XMLN * p_node, tr2_GetVideoEncoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    while (p_Options && soap_strcmp(p_Options->name, "Options") == 0)
    {
        VideoEncoder2ConfigurationOptionsList * p_option = onvif_add_VideoEncoder2ConfigurationOptions(&p_res->Options);
        if (p_option)
        {
            if (!parse_VideoEncoder2ConfigurationOptions(p_Options, &p_option->Options))
            {
                onvif_free_VideoEncoder2ConfigurationOptions(&p_res->Options);
                return FALSE;
            }
        }
        
        p_Options = p_Options->next;
    }
    
    return TRUE;
}

BOOL parse_tr2_GetProfiles(XMLN * p_node, tr2_GetProfiles_RES * p_res)
{
    XMLN * p_Profiles;

    p_Profiles = xml_node_soap_get(p_node, "Profiles");
    while (p_Profiles)
    {
        MediaProfileList * p_profile = onvif_add_MediaProfile(&p_res->Profiles);
        if (p_profile)
        {
            const char * p_fixed;
            const char * p_token;

            p_fixed = xml_attr_get(p_Profiles, "fixed");
            if (p_fixed)
            {
                p_profile->MediaProfile.fixed = parse_Bool(p_fixed);
            }

            p_token = xml_attr_get(p_Profiles, "token");
            if (p_token)
            {
                strncpy(p_profile->MediaProfile.token, p_token, sizeof(p_profile->MediaProfile.token)-1);
            }
            
            if (!parse_MediaProfile(p_Profiles, &p_profile->MediaProfile))
            {
                onvif_free_MediaProfiles(&p_res->Profiles);
                return FALSE;
            }
        }

        p_Profiles = p_Profiles->next;
    }
    
    return TRUE;
}

BOOL parse_tr2_CreateProfile(XMLN * p_node, tr2_CreateProfile_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tr2_GetStreamUri(XMLN * p_node, tr2_GetStreamUri_RES * p_res)
{
    XMLN * p_Uri;

    p_Uri = xml_node_soap_get(p_node, "Uri");
    if (p_Uri && p_Uri->data)
    {
        onvif_parse_uri(p_Uri->data, p_res->Uri, sizeof(p_res->Uri));
    }

    return TRUE;
}

BOOL parse_tr2_GetVideoSourceConfigurations(XMLN * p_node, tr2_GetVideoSourceConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        VideoSourceConfigurationList * p_v_src_cfg = onvif_add_VideoSourceConfiguration(&p_res->Configurations);
        if (p_v_src_cfg)
        {
            if (!parse_VideoSourceConfiguration(p_Configurations, &p_v_src_cfg->Configuration))
            {
                onvif_free_VideoSourceConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetMetadataConfigurations(XMLN * p_node, tr2_GetMetadataConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        MetadataConfigurationList * p_cfg = onvif_add_MetadataConfiguration(&p_res->Configurations);
        if (p_cfg)
        {
            if (!parse_MetadataConfiguration(p_Configurations, &p_cfg->Configuration))
            {
                onvif_free_MetadataConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetMetadataConfigurationOptions(XMLN * p_node, tr2_GetMetadataConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_MetadataConfigurationOptions(p_Options, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_tr2_GetAudioEncoderConfigurations(XMLN * p_node, tr2_GetAudioEncoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioEncoder2ConfigurationList * p_cfg = onvif_add_AudioEncoder2Configuration(&p_res->Configurations);
        if (p_cfg)
        {
            if (!parse_AudioEncoder2Configuration(p_Configurations, &p_cfg->Configuration))
            {
                onvif_free_AudioEncoder2Configurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetAudioSourceConfigurations(XMLN * p_node, tr2_GetAudioSourceConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioSourceConfigurationList * p_cfg = onvif_add_AudioSourceConfiguration(&p_res->Configurations);
        if (p_cfg)
        {
            if (!parse_AudioSourceConfiguration(p_Configurations, &p_cfg->Configuration))
            {
                onvif_free_AudioSourceConfigurations(&p_res->Configurations);
                return FALSE;
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetAudioEncoderConfigurationOptions(XMLN * p_node, tr2_GetAudioEncoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    while (p_Options && soap_strcmp(p_Options->name, "Options") == 0)
    {
        AudioEncoder2ConfigurationOptionsList * p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_res->Options);
        if (p_option)
        {
            if (!parse_AudioEncoder2ConfigurationOptions(p_Options, &p_option->Options))
            {
                onvif_free_AudioEncoder2ConfigurationOptions(&p_res->Options);
                return FALSE;
            }
        }
        
        p_Options = p_Options->next;
    }
    
    return TRUE;
}

BOOL parse_tr2_GetVideoEncoderInstances(XMLN * p_node, tr2_GetVideoEncoderInstances_RES * p_res)
{
    XMLN * p_Info;

    p_Info = xml_node_soap_get(p_node, "Info");
    if (p_Info)
    {
        parse_EncoderInstanceInfo(p_Info, &p_res->Info);
    }

    return TRUE;    
}

BOOL parse_tr2_GetAudioOutputConfigurations(XMLN * p_node, tr2_GetAudioOutputConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioOutputConfigurationList * p_item = onvif_add_AudioOutputConfiguration(&p_res->Configurations);
        if (p_item)
        {
            parse_AudioOutputConfiguration(p_Configurations, &p_item->Configuration);
        }
        
        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetAudioOutputConfigurationOptions(XMLN * p_node, tr2_GetAudioOutputConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_AudioOutputConfigurationOptions(p_Options, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_tr2_GetAudioDecoderConfigurations(XMLN * p_node, tr2_GetAudioDecoderConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        AudioDecoderConfigurationList * p_item = onvif_add_AudioDecoderConfiguration(&p_res->Configurations);
        if (p_item)
        {
            parse_AudioDecoderConfiguration(p_Configurations, &p_item->Configuration);
        }
        
        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;    
}

BOOL parse_tr2_GetAudioDecoderConfigurationOptions(XMLN * p_node, tr2_GetAudioDecoderConfigurationOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    while (p_Options && soap_strcmp(p_Options->name, "Options") == 0)
    {
        AudioEncoder2ConfigurationOptionsList * p_option = onvif_add_AudioEncoder2ConfigurationOptions(&p_res->Options);
        if (p_option)
        {
            if (!parse_AudioEncoder2ConfigurationOptions(p_Options, &p_option->Options))
            {
                onvif_free_AudioEncoder2ConfigurationOptions(&p_res->Options);
                return FALSE;
            }
        }
        
        p_Options = p_Options->next;
    }
    
    return TRUE;
}

BOOL parse_tr2_GetSnapshotUri(XMLN * p_node, tr2_GetSnapshotUri_RES * p_res)
{
    XMLN * p_Uri;
    
    p_Uri = xml_node_soap_get(p_node, "Uri");
    if (p_Uri && p_Uri->data)
    {
        onvif_parse_uri(p_Uri->data, p_res->Uri, sizeof(p_res->Uri)-1);
    }

    return TRUE;
}

BOOL parse_tr2_GetVideoSourceModes(XMLN * p_node, tr2_GetVideoSourceModes_RES * p_res)
{
    XMLN * p_VideoSourceModes;

    p_VideoSourceModes = xml_node_soap_get(p_node, "VideoSourceModes");
    while (p_VideoSourceModes && soap_strcmp(p_VideoSourceModes->name, "VideoSourceModes") == 0)
    {
        VideoSourceModeList * p_item = onvif_add_VideoSourceMode(&p_res->VideoSourceModes);
        if (p_item)
        {
            parse_VideoSourceMode(p_VideoSourceModes, &p_item->VideoSourceMode);
        }
        
        p_VideoSourceModes = p_VideoSourceModes->next;
    }

    return TRUE;
}

BOOL parse_tr2_SetVideoSourceMode(XMLN * p_node, tr2_SetVideoSourceMode_RES * p_res)
{
    XMLN * p_Reboot;
    
    p_Reboot = xml_node_soap_get(p_node, "Reboot");
    if (p_Reboot && p_Reboot->data)
    {
        p_res->Reboot = parse_Bool(p_Reboot->data);
    }

    return TRUE;
}

BOOL parse_tr2_CreateOSD(XMLN * p_node, tr2_CreateOSD_RES * p_res)
{
    XMLN * p_OSDToken;
    
    p_OSDToken = xml_node_soap_get(p_node, "OSDToken");
    if (p_OSDToken && p_OSDToken->data)
    {
        strncpy(p_res->OSDToken, p_OSDToken->data, sizeof(p_res->OSDToken)-1);
    }

    return TRUE;
}

BOOL parse_tr2_GetOSDs(XMLN * p_node, tr2_GetOSDs_RES * p_res)
{
    XMLN * p_OSDs;

    p_OSDs = xml_node_soap_get(p_node, "OSDs");
    while (p_OSDs && soap_strcmp(p_OSDs->name, "OSDs") == 0)
    {
        OSDConfigurationList * p_item = onvif_add_OSDConfiguration(&p_res->OSDs);
        if (p_item)
        {
            parse_OSDConfiguration(p_OSDs, &p_item->OSD);
        }
        
        p_OSDs = p_OSDs->next;
    }

    return TRUE;
}

BOOL parse_tr2_GetOSDOptions(XMLN * p_node, tr2_GetOSDOptions_RES * p_res)
{
    XMLN * p_OSDOptions;

    p_OSDOptions = xml_node_soap_get(p_node, "OSDOptions");
    if (p_OSDOptions)
    {
        parse_OSDOptions(p_OSDOptions, &p_res->OSDOptions);
    }
    
    return TRUE;
}

BOOL parse_tr2_GetAnalyticsConfigurations(XMLN * p_node, tr2_GetAnalyticsConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        VideoAnalyticsConfigurationList * p_item = onvif_add_VideoAnalyticsConfiguration(&p_res->Configurations);
        if (p_item)
        {
            parse_VideoAnalyticsConfiguration(p_Configurations, &p_item->Configuration);
        }
        
        p_Configurations = p_Configurations->next;
    }
    
    return TRUE;
}

BOOL parse_tr2_GetMasks(XMLN * p_node, tr2_GetMasks_RES * p_res)
{
    XMLN * p_Masks;

    p_Masks = xml_node_soap_get(p_node, "Masks");
    while (p_Masks && soap_strcmp(p_Masks->name, "Masks") == 0)
    {
        MaskList * p_item = onvif_add_Mask(&p_res->Masks);
        if (p_item)
        {
            parse_Mask(p_Masks, &p_item->Mask);
        }
        
        p_Masks = p_Masks->next;
    }

    return TRUE;
}

BOOL parse_tr2_CreateMask(XMLN * p_node, tr2_CreateMask_RES * p_res)
{
    XMLN * p_Token;
    
    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tr2_GetMaskOptions(XMLN * p_node, tr2_GetMaskOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_MaskOptions(p_Options, &p_res->Options);
    }
    
    return TRUE;
}

#ifdef DEVICEIO_SUPPORT

BOOL parse_DeviceIOServiceCapabilities(XMLN * p_node, onvif_DeviceIOCapabilities * p_res)
{
    const char * p_VideoSources;
    const char * p_VideoOutputs;
    const char * p_AudioSources;
    const char * p_AudioOutputs;
    const char * p_RelayOutputs;
    const char * p_SerialPorts;
    const char * p_DigitalInputs;
    const char * p_DigitalInputOptions;

    p_VideoSources = xml_attr_get(p_node, "VideoSources");
    if (p_VideoSources)
    {
        p_res->VideoSourcesFlag = 1;
        p_res->VideoSources = atoi(p_VideoSources);
    }

    p_VideoOutputs = xml_attr_get(p_node, "VideoOutputs");
    if (p_VideoOutputs)
    {
        p_res->VideoOutputsFlag = 1;
        p_res->VideoOutputs = atoi(p_VideoOutputs);
    }

    p_AudioSources = xml_attr_get(p_node, "AudioSources");
    if (p_AudioSources)
    {
        p_res->AudioSourcesFlag = 1;
        p_res->AudioSources = atoi(p_AudioSources);
    }

    p_AudioOutputs = xml_attr_get(p_node, "AudioOutputs");
    if (p_AudioOutputs)
    {
        p_res->AudioOutputsFlag = 1;
        p_res->AudioOutputs = atoi(p_AudioOutputs);
    }

    p_RelayOutputs = xml_attr_get(p_node, "RelayOutputs");
    if (p_RelayOutputs)
    {
        p_res->RelayOutputsFlag = 1;
        p_res->RelayOutputs = atoi(p_RelayOutputs);
    }

    p_SerialPorts = xml_attr_get(p_node, "SerialPorts");
    if (p_SerialPorts)
    {
        p_res->SerialPortsFlag = 1;
        p_res->SerialPorts = atoi(p_SerialPorts);
    }

    p_DigitalInputs = xml_attr_get(p_node, "DigitalInputs");
    if (p_DigitalInputs)
    {
        p_res->DigitalInputsFlag = 1;
        p_res->DigitalInputs = atoi(p_DigitalInputs);
    }

    p_DigitalInputOptions = xml_attr_get(p_node, "DigitalInputOptions");
    if (p_DigitalInputOptions)
    {
        p_res->DigitalInputOptionsFlag = 1;
        p_res->DigitalInputOptions = parse_Bool(p_DigitalInputOptions);
    }

    return TRUE;
}

BOOL parse_DeviceIOService(XMLN * p_node, onvif_DeviceIOCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_DeviceIOServiceCapabilities(p_Capabilities, p_res);
        }
    } 

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_RelayOutput(XMLN * p_node, onvif_RelayOutput * p_res)
{
    XMLN * p_Properties;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Properties = xml_node_soap_get(p_node, "Properties");
    if (p_Properties)
    {
        parse_RelayOutputSettings(p_Properties, &p_res->Properties);
    }

    return TRUE;
}

BOOL parse_RelayOutputOptions(XMLN * p_node, onvif_RelayOutputOptions * p_res)
{
    XMLN * p_Mode;
    XMLN * p_DelayTimes;
    XMLN * p_Discrete;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Mode = xml_node_soap_get(p_node, "Mode");
    while (p_Mode && p_Mode->data && soap_strcmp(p_Mode->name, "Mode") == 0)
    {
        if (soap_strcmp(p_Mode->data, "Bistable") == 0)
        {
            p_res->RelayMode_BistableFlag = 1;
        }
        else if (soap_strcmp(p_Mode->data, "Monostable") == 0)
        {
            p_res->RelayMode_MonostableFlag = 1;
        }
        
        p_Mode = p_Mode->next;
    }

    p_DelayTimes = xml_node_soap_get(p_node, "DelayTimes");
    if (p_DelayTimes && p_DelayTimes->data)
    {
        p_res->DelayTimesFlag = 1;
        strncpy(p_res->DelayTimes, p_DelayTimes->data, sizeof(p_res->DelayTimes)-1);
    }

    p_Discrete = xml_node_soap_get(p_node, "Discrete");
    if (p_Discrete && p_Discrete->data)
    {
        p_res->DiscreteFlag = 1;
        p_res->Discrete = parse_Bool(p_Discrete->data);
    }

    return TRUE;
}

BOOL parse_DigitalInput(XMLN * p_node, onvif_DigitalInput * p_res)
{
    const char * p_token;
    const char * p_IdleState;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_IdleState = xml_attr_get(p_node, "IdleState");
    if (p_IdleState)
    {
        p_res->IdleStateFlag = 1;
        p_res->IdleState = onvif_StringToDigitalIdleState(p_IdleState);
    }

    return TRUE;
}

BOOL parse_DigitalInputConfigurationOptions(XMLN * p_node, onvif_DigitalInputConfigurationOptions * p_res)
{
    XMLN * p_IdleState;

    p_IdleState = xml_node_soap_get(p_node, "IdleState");
    while (p_IdleState && p_IdleState->data && soap_strcmp(p_IdleState->name, "IdleState") == 0)
    {
        if (soap_strcmp(p_IdleState->data, "closed") == 0)
        {
            p_res->DigitalIdleState_closedFlag = 1;
        }
        else if (soap_strcmp(p_IdleState->data, "open") == 0)
        {
            p_res->DigitalIdleState_openFlag = 1;
        }
        
        p_IdleState = p_IdleState->next;
    }

    return TRUE;
}

BOOL parse_SerialPortConfiguration(XMLN * p_node, onvif_SerialPortConfiguration * p_res)
{
    XMLN * p_BaudRate;
    XMLN * p_ParityBit;
    XMLN * p_CharacterLength;
    XMLN * p_StopBit;
    const char * p_token;
    const char * p_type;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_type = xml_attr_get(p_node, "type");
    if (p_type)
    {
        p_res->type = onvif_StringToSerialPortType(p_type);
    }

    p_BaudRate = xml_node_soap_get(p_node, "BaudRate");
    if (p_BaudRate && p_BaudRate->data)
    {
        p_res->BaudRate = atoi(p_BaudRate->data);
    }

    p_ParityBit = xml_node_soap_get(p_node, "ParityBit");
    if (p_ParityBit && p_ParityBit->data)
    {
        p_res->ParityBit = onvif_StringToParityBit(p_ParityBit->data);
    }

    p_CharacterLength = xml_node_soap_get(p_node, "CharacterLength");
    if (p_CharacterLength && p_CharacterLength->data)
    {
        p_res->CharacterLength = atoi(p_CharacterLength->data);
    }

    p_StopBit = xml_node_soap_get(p_node, "StopBit");
    if (p_StopBit && p_StopBit->data)
    {
        p_res->StopBit = (float)atof(p_StopBit->data);
    }

    return TRUE;
}

BOOL parse_ParityBitList(XMLN * p_node, onvif_ParityBitList * p_req)
{
    XMLN * p_Items;

    p_req->sizeItems = 0;
    
    p_Items = xml_node_soap_get(p_node, "Items");
    while (p_Items && p_Items->data && soap_strcmp(p_Items->name, "Items") == 0)
    {
        int idx = p_req->sizeItems;

        p_req->Items[idx] = onvif_StringToParityBit(p_Items->data);

        p_req->sizeItems++;
        if (p_req->sizeItems >= ARRAY_SIZE(p_req->Items))
        {
            break;
        }

        p_Items = p_Items->next;
    }

    return TRUE;
}

BOOL parse_SerialPortOptions(XMLN * p_node, onvif_SerialPortConfigurationOptions * p_res)
{
    XMLN * p_BaudRateList;
    XMLN * p_ParityBitList;
    XMLN * p_CharacterLengthList;
    XMLN * p_StopBitList;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_BaudRateList = xml_node_soap_get(p_node, "BaudRateList");
    if (p_BaudRateList)
    {
        parse_IntList(p_BaudRateList, &p_res->BaudRateList);
    }

    p_ParityBitList = xml_node_soap_get(p_node, "ParityBitList");
    if (p_ParityBitList)
    {
        parse_ParityBitList(p_ParityBitList, &p_res->ParityBitList);
    }

    p_CharacterLengthList = xml_node_soap_get(p_node, "CharacterLengthList");
    if (p_CharacterLengthList)
    {
        parse_IntList(p_CharacterLengthList, &p_res->CharacterLengthList);
    }

    p_StopBitList = xml_node_soap_get(p_node, "StopBitList");
    if (p_StopBitList)
    {
        parse_FloatList(p_StopBitList, &p_res->StopBitList);
    }

    return TRUE;
}

BOOL parse_tmd_GetServiceCapabilities(XMLN * p_node, tmd_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_DeviceIOServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tmd_GetRelayOutputs(XMLN * p_node, tmd_GetRelayOutputs_RES * p_res)
{
    XMLN * p_RelayOutputs;

    p_RelayOutputs = xml_node_soap_get(p_node, "RelayOutputs");
    while (p_RelayOutputs && soap_strcmp(p_RelayOutputs->name, "RelayOutputs") == 0)
    {
        RelayOutputList * p_relay = onvif_add_RelayOutput(&p_res->RelayOutputs);
        if (p_relay)
        {
            parse_RelayOutput(p_RelayOutputs, &p_relay->RelayOutput);
        }
        
        p_RelayOutputs = p_RelayOutputs->next;
    }

    return TRUE;
}

BOOL parse_tmd_GetRelayOutputOptions(XMLN * p_node, tmd_GetRelayOutputOptions_RES * p_res)
{
    XMLN * p_RelayOutputOptions;

    p_RelayOutputOptions = xml_node_soap_get(p_node, "RelayOutputOptions");
    while (p_RelayOutputOptions && soap_strcmp(p_RelayOutputOptions->name, "RelayOutputOptions") == 0)
    {
        RelayOutputOptionsList * p_option = onvif_add_RelayOutputOptions(&p_res->RelayOutputOptions);
        if (p_option)
        {
            parse_RelayOutputOptions(p_RelayOutputOptions, &p_option->Options);
        }
        
        p_RelayOutputOptions = p_RelayOutputOptions->next;
    }

    return TRUE;
}

BOOL parse_tmd_GetDigitalInputs(XMLN * p_node, tmd_GetDigitalInputs_RES * p_res)
{
    XMLN * p_DigitalInputs;

    p_DigitalInputs = xml_node_soap_get(p_node, "DigitalInputs");
    while (p_DigitalInputs && soap_strcmp(p_DigitalInputs->name, "DigitalInputs") == 0)
    {
        DigitalInputList * p_dinput = onvif_add_DigitalInput(&p_res->DigitalInputs);
        if (p_dinput)
        {
            parse_DigitalInput(p_DigitalInputs, &p_dinput->DigitalInput);
        }
        
        p_DigitalInputs = p_DigitalInputs->next;
    }

    return TRUE;
}

BOOL parse_tmd_GetDigitalInputConfigurationOptions(XMLN * p_node, tmd_GetDigitalInputConfigurationOptions_RES * p_res)
{
    XMLN * p_DigitalInputOptions;

    p_DigitalInputOptions = xml_node_soap_get(p_node, "DigitalInputOptions");
    if (p_DigitalInputOptions)
    {
        parse_DigitalInputConfigurationOptions(p_DigitalInputOptions, &p_res->DigitalInputOptions);
    }

    return TRUE;
}

BOOL parse_tmd_GetSerialPorts(XMLN * p_node, tmd_GetSerialPorts_RES * p_res)
{
    XMLN * p_SerialPort;

    p_SerialPort = xml_node_soap_get(p_node, "SerialPort");
    while (p_SerialPort && soap_strcmp(p_SerialPort->name, "SerialPort") == 0)
    {
        SerialPortList * p_node = onvif_add_SerialPort(&p_res->serial_port);
        if (p_node)
        {
            const char * p_token;

            p_token = xml_attr_get(p_SerialPort, "token");
            if (p_token)
            {
                strncpy(p_node->SerialPort.token, p_token, sizeof(p_node->SerialPort.token)-1);
            }
        }
        
        p_SerialPort = p_SerialPort->next;
    }

    return TRUE;
}

BOOL parse_tmd_GetSerialPortConfiguration(XMLN * p_node, tmd_GetSerialPortConfiguration_RES * p_res)
{
    XMLN * p_SerialPortConfiguration;

    p_SerialPortConfiguration = xml_node_soap_get(p_node, "SerialPortConfiguration");
    if (p_SerialPortConfiguration)
    {
        parse_SerialPortConfiguration(p_SerialPortConfiguration, &p_res->SerialPortConfiguration);
    }

    return TRUE;
}

BOOL parse_tmd_GetSerialPortConfigurationOptions(XMLN * p_node, tmd_GetSerialPortConfigurationOptions_RES * p_res)
{
    XMLN * p_SerialPortOptions;

    p_SerialPortOptions = xml_node_soap_get(p_node, "SerialPortOptions");
    if (p_SerialPortOptions)
    {
        parse_SerialPortOptions(p_SerialPortOptions, &p_res->SerialPortOptions);
    }

    return TRUE;
}

BOOL parse_tmd_SendReceiveSerialCommand(XMLN * p_node, tmd_SendReceiveSerialCommand_RES * p_res)
{
    XMLN * p_SerialData;
    
    p_SerialData = xml_node_soap_get(p_node, "SerialData");
    if (p_SerialData)
    {
        XMLN * p_Binary;
        XMLN * p_String;

        p_Binary = xml_node_soap_get(p_SerialData, "Binary");
        if (p_Binary && p_Binary->data)
        {
            p_res->SerialData._union_SerialData = 0;

            onvif_malloc_SerialData(&p_res->SerialData, 0, (int)strlen(p_Binary->data)+1);
            strcpy(p_res->SerialData.union_SerialData.Binary, p_Binary->data);            
        }

        p_String = xml_node_soap_get(p_SerialData, "String");
        if (p_String && p_String->data)
        {
            p_res->SerialData._union_SerialData = 1;

            onvif_malloc_SerialData(&p_res->SerialData, 1, (int)strlen(p_String->data)+1);
            strcpy(p_res->SerialData.union_SerialData.String, p_String->data);                                    
        }
        
        p_res->SerialDataFlag = 1;
    }

    return TRUE;
}

#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT

BOOL parse_RecordingServiceCapabilities(XMLN * p_node, onvif_RecordingCapabilities * p_res)
{
    const char * p_DynamicRecordings;
    const char * p_DynamicTracks;
    const char * p_Encoding;
    const char * p_MaxRate;
    const char * p_MaxTotalRate;
    const char * p_MaxRecordings;
    const char * p_MaxRecordingJobs;
    const char * p_Options;
    const char * p_MetadataRecording;
    const char * p_SupportedExportFileFormats;
    const char * p_EventRecording;
    const char * p_BeforeEventLimit;
    const char * p_AfterEventLimit;
    const char * p_SupportedTargetFormats;
    const char * p_EncryptionEntryLimit;
    const char * p_SupportedEncryptionModes;

    p_DynamicRecordings = xml_attr_get(p_node, "DynamicRecordings");
    if (p_DynamicRecordings)
    {
        p_res->DynamicRecordings = parse_Bool(p_DynamicRecordings);
    }

    p_DynamicTracks = xml_attr_get(p_node, "DynamicTracks");
    if (p_DynamicTracks)
    {
        p_res->DynamicTracks = parse_Bool(p_DynamicTracks);
    }

    p_Encoding = xml_attr_get(p_node, "Encoding");
    if (p_Encoding)
    {
        parse_EncodingList(p_Encoding, p_res);
    }

    p_MaxRate = xml_attr_get(p_node, "MaxRate");
    if (p_MaxRate)
    {
        p_res->MaxRate = (float)atof(p_MaxRate);
    }

    p_MaxTotalRate = xml_attr_get(p_node, "MaxTotalRate");
    if (p_MaxTotalRate)
    {
        p_res->MaxTotalRate = (float)atof(p_MaxTotalRate);
    }

    p_MaxRecordings = xml_attr_get(p_node, "MaxRecordings");
    if (p_MaxRecordings)
    {
        p_res->MaxRecordings = atoi(p_MaxRecordings);
    }

    p_MaxRecordingJobs = xml_attr_get(p_node, "MaxRecordingJobs");
    if (p_MaxRecordingJobs)
    {
        p_res->MaxRecordingJobs = atoi(p_MaxRecordingJobs);
    }

    p_Options = xml_attr_get(p_node, "Options");
    if (p_Options)
    {
        p_res->Options = parse_Bool(p_Options);
    }

    p_MetadataRecording = xml_attr_get(p_node, "MetadataRecording");
    if (p_MetadataRecording)
    {
        p_res->MetadataRecording = parse_Bool(p_MetadataRecording);
    }

    p_SupportedExportFileFormats = xml_attr_get(p_node, "SupportedExportFileFormats");
    if (p_SupportedExportFileFormats)
    {
        strncpy(p_res->SupportedExportFileFormats, p_SupportedExportFileFormats, sizeof(p_res->SupportedExportFileFormats)-1);
    }

    p_EventRecording = xml_attr_get(p_node, "EventRecording");
    if (p_EventRecording)
    {
        p_res->EventRecording = parse_Bool(p_EventRecording);
    }

    p_BeforeEventLimit = xml_attr_get(p_node, "BeforeEventLimit");
    if (p_BeforeEventLimit)
    {
        p_res->BeforeEventLimit = atoi(p_BeforeEventLimit);
    }

    p_AfterEventLimit = xml_attr_get(p_node, "AfterEventLimit");
    if (p_AfterEventLimit)
    {
        p_res->AfterEventLimit = atoi(p_AfterEventLimit);
    }
    
    p_SupportedTargetFormats = xml_attr_get(p_node, "SupportedTargetFormats");
    if (p_SupportedTargetFormats)
    {
        p_res->SupportedTargetFormatsFlag = 1;
        strncpy(p_res->SupportedTargetFormats, p_SupportedTargetFormats, sizeof(p_res->SupportedTargetFormats)-1);
    }

    p_EncryptionEntryLimit = xml_attr_get(p_node, "EncryptionEntryLimit");
    if (p_EncryptionEntryLimit)
    {
        p_res->EncryptionEntryLimitFlag = 1;
        p_res->EncryptionEntryLimit = atoi(p_EncryptionEntryLimit);
    }

    p_SupportedEncryptionModes = xml_attr_get(p_node, "SupportedEncryptionModes");
    if (p_SupportedEncryptionModes)
    {
        p_res->SupportedEncryptionModesFlag = 1;
        strncpy(p_res->SupportedEncryptionModes, p_SupportedEncryptionModes, sizeof(p_res->SupportedEncryptionModes)-1);
    }

    return TRUE;
}

BOOL parse_RecordingService(XMLN * p_node, onvif_RecordingCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_RecordingServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_SearchServiceCapabilities(XMLN * p_node, onvif_SearchCapabilities * p_res)
{
    const char * p_MetadataSearch;
    const char * p_GeneralStartEvents;

    p_MetadataSearch = xml_attr_get(p_node, "MetadataSearch");
    if (p_MetadataSearch)
    {
        p_res->MetadataSearch = parse_Bool(p_MetadataSearch);
    }

    p_GeneralStartEvents = xml_attr_get(p_node, "GeneralStartEvents");
    if (p_GeneralStartEvents)
    {
        p_res->GeneralStartEvents = parse_Bool(p_GeneralStartEvents);
    }

    return TRUE;
}

BOOL parse_SearchService(XMLN * p_node, onvif_SearchCapabilities * p_res)
{    
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_SearchServiceCapabilities(p_Capabilities, p_res);
        }
    }   

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_ReplayServiceCapabilities(XMLN * p_node, onvif_ReplayCapabilities * p_res)
{
    const char * p_ReversePlayback;
    const char * p_SessionTimeoutRange;
    const char * p_RTP_RTSP_TCP;
    const char * p_RTSPWebSocketUri;

    p_ReversePlayback = xml_attr_get(p_node, "ReversePlayback");
    if (p_ReversePlayback)
    {
        p_res->ReversePlayback = parse_Bool(p_ReversePlayback);
    }

    p_SessionTimeoutRange = xml_attr_get(p_node, "SessionTimeoutRange");
    if (p_SessionTimeoutRange)
    {
        parse_FloatRangeList(p_SessionTimeoutRange, &p_res->SessionTimeoutRange);
    }

    p_RTP_RTSP_TCP = xml_attr_get(p_node, "RTP_RTSP_TCP");
    if (p_RTP_RTSP_TCP)
    {
        p_res->RTP_RTSP_TCP = parse_Bool(p_RTP_RTSP_TCP);
    }

    p_RTSPWebSocketUri = xml_attr_get(p_node, "RTSPWebSocketUri");
    if (p_RTSPWebSocketUri)
    {
        strncpy(p_res->RTSPWebSocketUri, p_RTSPWebSocketUri, sizeof(p_res->RTSPWebSocketUri)-1);
    }

    return TRUE;
}

BOOL parse_ReplayService(XMLN * p_node, onvif_ReplayCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ReplayServiceCapabilities(p_Capabilities, p_res);
        }
    }   

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }
    
    return TRUE;
}

BOOL parse_RecordingSourceInformation(XMLN * p_node, onvif_RecordingSourceInformation * p_res)
{
    XMLN * p_SourceId;
    XMLN * p_Name;
    XMLN * p_Location;
    XMLN * p_Description;
    XMLN * p_Address;

    p_SourceId = xml_node_soap_get(p_node, "SourceId");
    if (p_SourceId && p_SourceId->data)
    {
        strncpy(p_res->SourceId, p_SourceId->data, sizeof(p_res->SourceId)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Location = xml_node_soap_get(p_node, "Location");
    if (p_Location && p_Location->data)
    {
        strncpy(p_res->Location, p_Location->data, sizeof(p_res->Location)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address && p_Address->data)
    {
        strncpy(p_res->Address, p_Address->data, sizeof(p_res->Address)-1);
    }
    
    return TRUE;
}

BOOL parse_RecordingEncryption(XMLN * p_node, onvif_RecordingEncryption * p_res)
{
    XMLN * p_KID;
    XMLN * p_Key;
    XMLN * p_Track;
    const char * p_Mode;

    p_Mode = xml_attr_get(p_node, "Mode");
    if (p_Mode)
    {
        strncpy(p_res->Mode, p_Mode, sizeof(p_res->Mode)-1);
    }

    p_KID = xml_node_soap_get(p_node, "KID");
    if (p_KID && p_KID->data)
    {
        strncpy(p_res->KID, p_KID->data, sizeof(p_res->KID)-1);
    }

    p_Key = xml_node_soap_get(p_node, "Key");
    if (p_Key && p_Key->data)
    {
        p_res->KeyFlag = 1;
        strncpy(p_res->Key, p_Key->data, sizeof(p_res->Key)-1);
    }

    p_Track = xml_node_soap_get(p_node, "Track");
    while (p_Track && p_Track->data && soap_strcmp(p_Track->name, "Track") == 0)
    {
        uint32 idx = p_res->sizeTrack;

        strncpy(p_res->Track[idx], p_Track->data, sizeof(p_res->Track[idx])-1);
        
        p_res->sizeTrack++;
        if (p_res->sizeTrack >= ARRAY_SIZE(p_res->Track))
        {
            break;
        }

        p_Track = p_Track->next;
    }

    return TRUE;
}

BOOL parse_RecordingTargetConfiguration(XMLN * p_node, onvif_RecordingTargetConfiguration * p_res)
{
    XMLN * p_Storage;
    XMLN * p_Format;
    XMLN * p_Prefix;
    XMLN * p_Postfix;
    XMLN * p_SpanDuration;
    XMLN * p_SegmentDuration;
    XMLN * p_Encryption;

    p_Storage = xml_node_soap_get(p_node, "Storage");
    if (p_Storage && p_Storage->data)
    {
        strncpy(p_res->Storage, p_Storage->data, sizeof(p_res->Storage)-1);
    }

    p_Format = xml_node_soap_get(p_node, "Format");
    if (p_Format && p_Format->data)
    {
        strncpy(p_res->Format, p_Format->data, sizeof(p_res->Format)-1);
    }

    p_Prefix = xml_node_soap_get(p_node, "Prefix");
    if (p_Prefix && p_Prefix->data)
    {
        p_res->PrefixFlag = 1;
        strncpy(p_res->Prefix, p_Prefix->data, sizeof(p_res->Prefix)-1);
    }

    p_Postfix = xml_node_soap_get(p_node, "Postfix");
    if (p_Postfix && p_Postfix->data)
    {
        p_res->PostfixFlag = 1;
        strncpy(p_res->Postfix, p_Postfix->data, sizeof(p_res->Postfix)-1);
    }

    p_SpanDuration = xml_node_soap_get(p_node, "SpanDuration");
    if (p_SpanDuration && p_SpanDuration->data)
    {
        p_res->SpanDurationFlag = parse_XSDDuration(p_SpanDuration->data, (int *)&p_res->SpanDuration);
    }

    p_SegmentDuration = xml_node_soap_get(p_node, "SegmentDuration");
    if (p_SegmentDuration && p_SegmentDuration->data)
    {
        p_res->SegmentDurationFlag = parse_XSDDuration(p_SegmentDuration->data, (int *)&p_res->SegmentDuration);
    }

    p_Encryption = xml_node_soap_get(p_node, "Encryption");
    while (p_Encryption && soap_strcmp(p_Encryption->name, "Encryption") == 0)
    {
        uint32 idx = p_res->sizeEncryption;

        parse_RecordingEncryption(p_Encryption, &p_res->Encryption[idx]);
        
        p_res->sizeEncryption++;
        if (p_res->sizeEncryption >= ARRAY_SIZE(p_res->Encryption))
        {
            break;
        }

        p_Encryption = p_Encryption->next;
    }
    
    return TRUE;
}

BOOL parse_RecordingConfiguration(XMLN * p_node, onvif_RecordingConfiguration * p_res)
{
    XMLN * p_Source;
    XMLN * p_Content;
    XMLN * p_MaximumRetentionTime;
    XMLN * p_Target;

    p_Source = xml_node_soap_get(p_node, "Source");
    if (p_Source)
    {
        parse_RecordingSourceInformation(p_Source, &p_res->Source);
    }

    p_Content = xml_node_soap_get(p_node, "Content");
    if (p_Content && p_Content->data)
    {
        strncpy(p_res->Content, p_Content->data, sizeof(p_res->Content)-1);
    }

    p_MaximumRetentionTime = xml_node_soap_get(p_node, "MaximumRetentionTime");
    if (p_MaximumRetentionTime && p_MaximumRetentionTime->data)
    {
        p_res->MaximumRetentionTimeFlag = parse_XSDDuration(p_MaximumRetentionTime->data, (int *)&p_res->MaximumRetentionTime);
    }

    p_Target = xml_node_soap_get(p_node, "Target");
    if (p_Target)
    {
        p_res->TargetFlag = parse_RecordingTargetConfiguration(p_Target, &p_res->Target);
    }

    return TRUE;
}

BOOL parse_TrackConfiguration(XMLN * p_node, onvif_TrackConfiguration * p_res)
{
    XMLN * p_TrackType;
    XMLN * p_Description;

    p_TrackType = xml_node_soap_get(p_node, "TrackType");
    if (p_TrackType && p_TrackType->data)
    {
        p_res->TrackType = onvif_StringToTrackType(p_TrackType->data);        
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_Track(XMLN * p_node, onvif_Track * p_res)
{
    XMLN * p_TrackToken;
    XMLN * p_Configuration;

    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_TrackConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_Recording(XMLN * p_node, onvif_Recording * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_Configuration;
    XMLN * p_Tracks;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_RecordingConfiguration(p_Configuration, &p_res->Configuration);
    }

    p_Tracks = xml_node_soap_get(p_node, "Tracks");
    if (p_Tracks)
    {
        XMLN * p_Track;

        p_Track = xml_node_soap_get(p_Tracks, "Track");
        while (p_Track && soap_strcmp(p_Track->name, "Track") == 0)
        {
            TrackList * p_item = onvif_add_Track(&p_res->Tracks);
            if (p_item)
            {
                parse_Track(p_Track, &p_item->Track);
            }

            p_Track = p_Track->next;
        }        
    }

    return TRUE;
}

BOOL parse_SourceReference(XMLN * p_node, onvif_SourceReference * p_res)
{
    XMLN * p_Token;
    const char * p_Type;
    
    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        p_res->TypeFlag = 1;
        strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
    }

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_RecordingJobTrack(XMLN * p_node, onvif_RecordingJobTrack * p_res)
{
    XMLN * p_SourceTag;
    XMLN * p_Destination;

    p_SourceTag = xml_node_soap_get(p_node, "SourceTag");
    if (p_SourceTag && p_SourceTag->data)
    {
        strncpy(p_res->SourceTag, p_SourceTag->data, sizeof(p_res->SourceTag)-1);
    }

    p_Destination = xml_node_soap_get(p_node, "Destination");
    if (p_Destination && p_Destination->data)
    {
        strncpy(p_res->Destination, p_Destination->data, sizeof(p_res->Destination)-1);
    }

    return TRUE;
}

BOOL parse_RecordingJobSource(XMLN * p_node, onvif_RecordingJobSource * p_res)
{
    XMLN * p_SourceToken;
    XMLN * p_AutoCreateReceiver;
    XMLN * p_Tracks;

    p_SourceToken = xml_node_soap_get(p_node, "SourceToken");
    if (p_SourceToken)
    {
        p_res->SourceTokenFlag = parse_SourceReference(p_SourceToken, &p_res->SourceToken);
    }

    p_AutoCreateReceiver = xml_node_soap_get(p_node, "AutoCreateReceiver");
    if (p_AutoCreateReceiver && p_AutoCreateReceiver->data)
    {
        p_res->AutoCreateReceiverFlag = 1;
        p_res->AutoCreateReceiver = parse_Bool(p_AutoCreateReceiver->data);
    }

    p_res->sizeTracks = 0;
    
    p_Tracks = xml_node_soap_get(p_node, "Tracks");
    while (p_Tracks && soap_strcmp(p_Tracks->name, "Tracks") == 0)
    {
        uint32 idx = p_res->sizeTracks;

        parse_RecordingJobTrack(p_Tracks, &p_res->Tracks[idx]);

        p_res->sizeTracks++;
        if (p_res->sizeTracks >= ARRAY_SIZE(p_res->Tracks))
        {
            break;
        }

        p_Tracks = p_Tracks->next;
    }

    return TRUE;
}

BOOL parse_RecordingJobConfiguration(XMLN * p_node, onvif_RecordingJobConfiguration * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_Mode;
    XMLN * p_Priority;
    XMLN * p_Source;
    const char * p_ScheduleToken;

    p_ScheduleToken = xml_attr_get(p_node, "ScheduleToken");
    if (p_ScheduleToken)
    {
        strncpy(p_res->ScheduleToken, p_ScheduleToken, sizeof(p_res->ScheduleToken)-1);
    }

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        strncpy(p_res->Mode, p_Mode->data, sizeof(p_res->Mode)-1);
    }

    p_Priority = xml_node_soap_get(p_node, "Priority");
    if (p_Priority && p_Priority->data)
    {
        p_res->Priority = atoi(p_Priority->data);
    }

    p_res->sizeSource = 0;
    
    p_Source = xml_node_soap_get(p_node, "Source");
    while (p_Source && soap_strcmp(p_Source->name, "Source") == 0)
    {
        int idx = p_res->sizeSource;

        parse_RecordingJobSource(p_Source, &p_res->Source[idx]);
        
        p_res->sizeSource++;
        if (p_res->sizeSource >= ARRAY_SIZE(p_res->Source))
        {
            break;
        }

        p_Source = p_Source->next;
    }

    return TRUE;
} 

BOOL parse_RecordingJobStateTrack(XMLN * p_node, onvif_RecordingJobStateTrack * p_res)
{
    XMLN * p_SourceTag;
    XMLN * p_Destination;
    XMLN * p_Error;
    XMLN * p_State;

    p_SourceTag = xml_node_soap_get(p_node, "SourceTag");
    if (p_SourceTag && p_SourceTag->data)
    {
        strncpy(p_res->SourceTag, p_SourceTag->data, sizeof(p_res->SourceTag)-1);
    }

    p_Destination = xml_node_soap_get(p_node, "Destination");
    if (p_Destination && p_Destination->data)
    {
        strncpy(p_res->Destination, p_Destination->data, sizeof(p_res->Destination)-1);
    }

    p_Error = xml_node_soap_get(p_node, "Error");
    if (p_Error && p_Error->data)
    {
        strncpy(p_res->Error, p_Error->data, sizeof(p_res->Error)-1);
    }

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        strncpy(p_res->State, p_State->data, sizeof(p_res->State)-1);
    }

    return TRUE;
}

BOOL parse_RecordingJobStateSource(XMLN * p_node, onvif_RecordingJobStateSource * p_res)
{
    XMLN * p_SourceToken;
    XMLN * p_State;
    XMLN * p_Tracks;

    p_SourceToken = xml_node_soap_get(p_node, "SourceToken");
    if (p_SourceToken)
    {
        parse_SourceReference(p_SourceToken, &p_res->SourceToken);
    }

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        strncpy(p_res->State, p_State->data, sizeof(p_res->State)-1);
    }

    p_Tracks = xml_node_soap_get(p_node, "Tracks");
    if (p_Tracks)
    {    
        XMLN * p_Track;

        p_res->sizeTrack = 0;
        
        p_Track = xml_node_soap_get(p_Tracks, "Track");        
        while (p_Track && soap_strcmp(p_Track->name, "Track") == 0)
        {
            uint32 idx = p_res->sizeTrack;

            parse_RecordingJobStateTrack(p_Track, &p_res->Track[idx]);

            p_res->sizeTrack++;
            if (p_res->sizeTrack >= ARRAY_SIZE(p_res->Track))
            {
                break;
            }
                
            p_Track = p_Track->next;
        }
    }

    return TRUE;
}

BOOL parse_RecordingJobItem(XMLN * p_node, onvif_RecordingJob * p_res)
{
    XMLN * p_JobToken;
    XMLN * p_JobConfiguration;

    p_JobToken = xml_node_soap_get(p_node, "JobToken");
    if (p_JobToken && p_JobToken->data)
    {
        strncpy(p_res->JobToken, p_JobToken->data, sizeof(p_res->JobToken)-1);
    }

    p_JobConfiguration = xml_node_soap_get(p_node, "JobConfiguration");
    if (p_JobConfiguration)
    {
        parse_RecordingJobConfiguration(p_JobConfiguration, &p_res->JobConfiguration);
    }

    return TRUE;
}

BOOL parse_TrackInformation(XMLN * p_node, onvif_TrackInformation * p_res)
{
    XMLN * p_TrackToken;
    XMLN * p_TrackType;
    XMLN * p_Description;
    XMLN * p_DataFrom;
    XMLN * p_DataTo;

    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }

    p_TrackType = xml_node_soap_get(p_node, "TrackType");
    if (p_TrackType && p_TrackType->data)
    {
        p_res->TrackType = onvif_StringToTrackType(p_TrackType->data);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_DataFrom = xml_node_soap_get(p_node, "DataFrom");
    if (p_DataFrom && p_DataFrom->data)
    {
        parse_XSDDatetime(p_DataFrom->data, &p_res->DataFrom);
    }

    p_DataTo = xml_node_soap_get(p_node, "DataTo");
    if (p_DataTo && p_DataTo->data)
    {
        parse_XSDDatetime(p_DataTo->data, &p_res->DataTo);
    }

    return TRUE;
}

BOOL parse_RecordingInformation(XMLN * p_node, onvif_RecordingInformation * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_Source;
    XMLN * p_EarliestRecording;
    XMLN * p_LatestRecording;
    XMLN * p_Content;
    XMLN * p_Track;
    XMLN * p_RecordingStatus;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_Source = xml_node_soap_get(p_node, "Source");
    if (p_Source)
    {
        parse_RecordingSourceInformation(p_Source, &p_res->Source);
    }

    p_EarliestRecording = xml_node_soap_get(p_node, "EarliestRecording");
    if (p_EarliestRecording && p_EarliestRecording->data)
    {
        p_res->EarliestRecordingFlag = 1;
        parse_XSDDatetime(p_EarliestRecording->data, &p_res->EarliestRecording);
    }

    p_LatestRecording = xml_node_soap_get(p_node, "LatestRecording");
    if (p_LatestRecording && p_LatestRecording->data)
    {
        p_res->LatestRecordingFlag = 1;
        parse_XSDDatetime(p_LatestRecording->data, &p_res->LatestRecording);
    }

    p_Content = xml_node_soap_get(p_node, "Content");
    if (p_Content && p_Content->data)
    {
        strncpy(p_res->Content, p_Content->data, sizeof(p_res->Content)-1);
    }

    p_res->sizeTrack = 0;
    
    p_Track = xml_node_soap_get(p_node, "Track");
    while (p_Track && soap_strcmp(p_Track->name, "Track") == 0)
    {
        uint32 idx = p_res->sizeTrack;        
        
        parse_TrackInformation(p_Track, &p_res->Track[idx]);

        p_res->sizeTrack++;
        if (p_res->sizeTrack >= ARRAY_SIZE(p_res->Track))
        {
            break;
        }

        p_Track = p_Track->next;
    }

    p_RecordingStatus = xml_node_soap_get(p_node, "RecordingStatus");
    if (p_RecordingStatus && p_RecordingStatus->data)
    {
        p_res->RecordingStatus = onvif_StringToRecordingStatus(p_RecordingStatus->data);
    }

    return TRUE;
}

BOOL parse_VideoAttributes(XMLN * p_node, onvif_VideoAttributes * p_res)
{
    XMLN * p_Bitrate;
    XMLN * p_Width;
    XMLN * p_Height;
    XMLN * p_Encoding;
    XMLN * p_Framerate;

    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate && p_Bitrate->data)
    {
        p_res->Bitrate = atoi(p_Bitrate->data);
    }

    p_Width = xml_node_soap_get(p_node, "Width");
    if (p_Width && p_Width->data)
    {
        p_res->Width = atoi(p_Width->data);
    }

    p_Height = xml_node_soap_get(p_node, "Height");
    if (p_Height && p_Height->data)
    {
        p_res->Height = atoi(p_Height->data);
    }

    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        p_res->Encoding = onvif_StringToVideoEncoding(p_Encoding->data);
    }

    p_Framerate = xml_node_soap_get(p_node, "Framerate");
    if (p_Framerate && p_Framerate->data)
    {
        p_res->Framerate = (float)atof(p_Framerate->data);
    }

    return TRUE;
}

BOOL parse_AudioAttributes(XMLN * p_node, onvif_AudioAttributes * p_res)
{
    XMLN * p_Bitrate;
    XMLN * p_Encoding;
    XMLN * p_Samplerate;

    p_Bitrate = xml_node_soap_get(p_node, "Bitrate");
    if (p_Bitrate && p_Bitrate->data)
    {
        p_res->Bitrate = atoi(p_Bitrate->data);
    }
    
    p_Encoding = xml_node_soap_get(p_node, "Encoding");
    if (p_Encoding && p_Encoding->data)
    {
        p_res->Encoding = onvif_StringToAudioEncoding(p_Encoding->data);
    }

    p_Samplerate = xml_node_soap_get(p_node, "Samplerate");
    if (p_Samplerate && p_Samplerate->data)
    {
        p_res->Samplerate = atoi(p_Samplerate->data);
    }

    return TRUE;
}

BOOL parse_MetadataAttributes(XMLN * p_node, onvif_MetadataAttributes * p_res)
{
    XMLN * p_CanContainPTZ;
    XMLN * p_CanContainAnalytics;
    XMLN * p_CanContainNotifications;

    p_CanContainPTZ = xml_node_soap_get(p_node, "CanContainPTZ");
    if (p_CanContainPTZ && p_CanContainPTZ->data)
    {
        p_res->CanContainPTZ = parse_Bool(p_CanContainPTZ->data);
    }
    
    p_CanContainAnalytics = xml_node_soap_get(p_node, "CanContainAnalytics");
    if (p_CanContainAnalytics && p_CanContainAnalytics->data)
    {
        p_res->CanContainAnalytics = parse_Bool(p_CanContainAnalytics->data);
    }

    p_CanContainNotifications = xml_node_soap_get(p_node, "CanContainNotifications");
    if (p_CanContainNotifications && p_CanContainNotifications->data)
    {
        p_res->CanContainNotifications = parse_Bool(p_CanContainNotifications->data);
    }

    return TRUE;
}

BOOL parse_TrackAttributes(XMLN * p_node, onvif_TrackAttributes * p_res)
{
    XMLN * p_TrackInformation;
    XMLN * p_VideoAttributes;
    XMLN * p_AudioAttributes;
    XMLN * p_MetadataAttributes;

    p_TrackInformation = xml_node_soap_get(p_node, "TrackInformation");
    if (p_TrackInformation)
    {
        parse_TrackInformation(p_TrackInformation, &p_res->TrackInformation);
    }

    p_VideoAttributes = xml_node_soap_get(p_node, "VideoAttributes");
    if (p_VideoAttributes)
    {
        p_res->VideoAttributesFlag = parse_VideoAttributes(p_VideoAttributes, &p_res->VideoAttributes);
    }

    p_AudioAttributes = xml_node_soap_get(p_node, "AudioAttributes");
    if (p_AudioAttributes)
    {
        p_res->AudioAttributesFlag = parse_AudioAttributes(p_AudioAttributes, &p_res->AudioAttributes);
    }

    p_MetadataAttributes = xml_node_soap_get(p_node, "MetadataAttributes");
    if (p_MetadataAttributes)
    {
        p_res->MetadataAttributesFlag = parse_MetadataAttributes(p_MetadataAttributes, &p_res->MetadataAttributes);
    }

    return TRUE;
}

BOOL parse_EndpointReferenceType(XMLN * p_node, onvif_EndpointReferenceType * p_res)
{
    XMLN * p_Address;

    p_Address = xml_node_soap_get(p_node, "Address");
    if (p_Address && p_Address->data)
    {
        strncpy(p_res->Address, p_Address->data, sizeof(p_res->Address)-1);
    }

    return TRUE;
}

BOOL parse_NotificationMessageHolderType(XMLN * p_node, onvif_NotificationMessageHolderType * p_res)
{
    XMLN * p_SubscriptionReference;
    XMLN * p_Topic;
    XMLN * p_ProducerReference;
    XMLN * p_Message;

    p_SubscriptionReference = xml_node_soap_get(p_node, "SubscriptionReference");
    if (p_SubscriptionReference)
    {
        p_res->SubscriptionReferenceFlag = parse_EndpointReferenceType(p_SubscriptionReference, &p_res->SubscriptionReference);
    }

    p_Topic = xml_node_soap_get(p_node, "Topic");
    if (p_Topic && p_Topic->data)
    {
        const char * p_Dialect;

        p_res->TopicFlag = 1;
        strncpy(p_res->Topic.Topic, p_Topic->data, sizeof(p_res->Topic.Topic)-1);
        
        p_Dialect = xml_attr_get(p_Topic, "Dialect");
        if (p_Dialect)
        {
            strncpy(p_res->Topic.Dialect, p_Dialect, sizeof(p_res->Topic.Dialect)-1);
        }
    }

    p_ProducerReference = xml_node_soap_get(p_node, "ProducerReference");
    if (p_ProducerReference)
    {
        p_res->ProducerReferenceFlag = parse_EndpointReferenceType(p_ProducerReference, &p_res->ProducerReference);
    }

    p_Message = xml_node_soap_get(p_node, "Message");
    if (p_Message)
    {
        parse_Message(p_Message, &p_res->Message);
    }

    return TRUE;
}

BOOL parse_FindEventResult(XMLN * p_node, onvif_FindEventResult * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_TrackToken;
    XMLN * p_Time;
    XMLN * p_Event;
    XMLN * p_StartStateEvent;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }

    p_Time = xml_node_soap_get(p_node, "Time");
    if (p_Time && p_Time->data)
    {
        parse_XSDDatetime(p_Time->data, &p_res->Time);
    }

    p_Event = xml_node_soap_get(p_node, "Event");
    if (p_Event)
    {
        parse_NotificationMessageHolderType(p_Event, &p_res->Event);
    }

    p_StartStateEvent = xml_node_soap_get(p_node, "StartStateEvent");
    if (p_StartStateEvent && p_StartStateEvent->data)
    {
        p_res->StartStateEvent = parse_Bool(p_StartStateEvent->data);
    }

    return TRUE;
}

BOOL parse_FindMetadataResult(XMLN * p_node, onvif_FindMetadataResult * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_TrackToken;
    XMLN * p_Time;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }

    p_Time = xml_node_soap_get(p_node, "Time");
    if (p_Time && p_Time->data)
    {
        parse_XSDDatetime(p_Time->data, &p_res->Time);
    }

    return TRUE;
}

BOOL parse_FindPTZPositionResult(XMLN * p_node, onvif_FindPTZPositionResult * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_TrackToken;
    XMLN * p_Time;
    XMLN * p_Position;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }

    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }

    p_Time = xml_node_soap_get(p_node, "Time");
    if (p_Time && p_Time->data)
    {
        parse_XSDDatetime(p_Time->data, &p_res->Time);
    }

    p_Position = xml_node_soap_get(p_node, "Position");
    if (p_Position)
    {
        parse_PTZVector(p_Position, &p_res->Position);
    }

    return TRUE;
}

BOOL parse_FileProgress(XMLN * p_node, onvif_FileProgress * p_res)
{
    XMLN * p_FileName;
    XMLN * p_Progress;

    p_FileName = xml_node_soap_get(p_node, "FileName");
    if (p_FileName && p_FileName->data)
    {
        strncpy(p_res->FileName, p_FileName->data, sizeof(p_res->FileName)-1);
    }

    p_Progress = xml_node_soap_get(p_node, "Progress");
    if (p_Progress && p_Progress->data)
    {
        p_res->Progress = (float)atof(p_Progress->data);
    }

    return TRUE;
}

BOOL parse_ArrayOfFileProgress(XMLN * p_node, onvif_ArrayOfFileProgress * p_res)
{
    XMLN * p_FileProgress;

    p_res->sizeFileProgress = 0;
    
    p_FileProgress = xml_node_soap_get(p_node, "FileProgress");
    while (p_FileProgress && soap_strcmp(p_FileProgress->name, "FileProgress") == 0)
    {
        uint32 idx = p_res->sizeFileProgress;
        
        parse_FileProgress(p_FileProgress, &p_res->FileProgress[idx]);

        p_res->sizeFileProgress++;
        if (p_res->sizeFileProgress >= ARRAY_SIZE(p_res->FileProgress))
        {
            break;
        }
        
        p_FileProgress = p_FileProgress->next;
    }

    return TRUE;
}

BOOL parse_JobOptions(XMLN * p_node, onvif_JobOptions * p_res)
{
    const char * p_Spare;
    const char * p_CompatibleSources;

    p_Spare = xml_attr_get(p_node, "Spare");
    if (p_Spare)
    {
        p_res->SpareFlag = 1;
        p_res->Spare = atoi(p_Spare);
    }

    p_CompatibleSources = xml_attr_get(p_node, "CompatibleSources");
    if (p_CompatibleSources)
    {
        p_res->CompatibleSourcesFlag = 1;
        strncpy(p_res->CompatibleSources, p_CompatibleSources, sizeof(p_res->CompatibleSources)-1);
    }

    return TRUE;
}

BOOL parse_TrackOptions(XMLN * p_node, onvif_TrackOptions * p_res)
{
    const char * p_SpareTotal;
    const char * p_SpareVideo;
    const char * p_SpareAudio;
    const char * p_SpareMetadata;

    p_SpareTotal = xml_attr_get(p_node, "SpareTotal");
    if (p_SpareTotal)
    {
        p_res->SpareTotalFlag = 1;
        p_res->SpareTotal = atoi(p_SpareTotal);
    }

    p_SpareVideo = xml_attr_get(p_node, "SpareVideo");
    if (p_SpareVideo)
    {
        p_res->SpareVideoFlag = 1;
        p_res->SpareVideo = atoi(p_SpareVideo);
    }

    p_SpareAudio = xml_attr_get(p_node, "SpareAudio");
    if (p_SpareAudio)
    {
        p_res->SpareAudioFlag = 1;
        p_res->SpareAudio = atoi(p_SpareAudio);
    }

    p_SpareMetadata = xml_attr_get(p_node, "SpareMetadata");
    if (p_SpareMetadata)
    {
        p_res->SpareMetadataFlag = 1;
        p_res->SpareMetadata = atoi(p_SpareMetadata);
    }

    return TRUE;
}

BOOL parse_RecordingOptions(XMLN * p_node, onvif_RecordingOptions * p_res)
{
    XMLN * p_Job;
    XMLN * p_Track;
    
    p_Job = xml_node_soap_get(p_node, "Job");
    if (p_Job)
    {
        parse_JobOptions(p_Job, &p_res->Job);
    }
    
    p_Track = xml_node_soap_get(p_node, "Track");
    if (p_Track)
    {
        parse_TrackOptions(p_Track, &p_res->Track);
    }

    return TRUE;
}

BOOL parse_RecordingJobStateInformation(XMLN * p_node, onvif_RecordingJobStateInformation * p_res)
{
    XMLN * p_RecordingToken;
    XMLN * p_State;
    XMLN * p_Sources;

    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }
    
    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        strncpy(p_res->State, p_State->data, sizeof(p_res->State)-1);
    }

    p_res->sizeSources = 0;
    
    p_Sources = xml_node_soap_get(p_node, "Sources");
    while (p_Sources && soap_strcmp(p_Sources->name, "Sources") == 0)
    {
        uint32 idx = p_res->sizeSources;

        parse_RecordingJobStateSource(p_Sources, &p_res->Sources[idx]);

        p_res->sizeSources++;
        if (p_res->sizeSources >= ARRAY_SIZE(p_res->Sources))
        {
            break;
        }
        
        p_Sources = p_Sources->next;
    }

    return TRUE;
}

BOOL parse_ReplayConfiguration(XMLN * p_node, onvif_ReplayConfiguration * p_res)
{
    XMLN * p_SessionTimeout;
        
    p_SessionTimeout = xml_node_soap_get(p_node, "SessionTimeout");
    if (p_SessionTimeout && p_SessionTimeout->data)
    {
        parse_XSDDuration(p_SessionTimeout->data, &p_res->SessionTimeout);
    }

    return TRUE;
}

BOOL parse_trc_GetServiceCapabilities(XMLN * p_node, trc_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_RecordingServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_trc_CreateRecording(XMLN * p_node, trc_CreateRecording_RES * p_res)
{
    XMLN * p_RecordingToken;
    
    p_RecordingToken = xml_node_soap_get(p_node, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_res->RecordingToken, p_RecordingToken->data, sizeof(p_res->RecordingToken)-1);
    }
    
    return TRUE;
}

BOOL parse_trc_GetRecordings(XMLN * p_node, trc_GetRecordings_RES * p_res)
{
    XMLN * p_RecordingItem;

    p_RecordingItem = xml_node_soap_get(p_node, "RecordingItem");
    while (p_RecordingItem && soap_strcmp(p_RecordingItem->name, "RecordingItem") == 0)
    {
        RecordingList * p_Recording = onvif_add_Recording(&p_res->Recordings);
        if (p_Recording)
        {
            parse_Recording(p_RecordingItem, &p_Recording->Recording);
        }
        
        p_RecordingItem = p_RecordingItem->next;
    }
    
    return TRUE;
}
   
BOOL parse_trc_GetRecordingConfiguration(XMLN * p_node, trc_GetRecordingConfiguration_RES * p_res)
{
    XMLN * p_RecordingConfiguration;
    
    p_RecordingConfiguration = xml_node_soap_get(p_node, "RecordingConfiguration");
    if (p_RecordingConfiguration)
    {
        parse_RecordingConfiguration(p_RecordingConfiguration, &p_res->RecordingConfiguration);
    }
    
    return TRUE;
}
 
BOOL parse_trc_GetRecordingOptions(XMLN * p_node, trc_GetRecordingOptions_RES * p_res)
{
    XMLN * p_Options;

    p_Options = xml_node_soap_get(p_node, "Options");
    if (p_Options)
    {
        parse_RecordingOptions(p_Options, &p_res->Options);
    }
    
    return TRUE;
}
 
BOOL parse_trc_CreateTrack(XMLN * p_node, trc_CreateTrack_RES * p_res)
{
    XMLN * p_TrackToken;
    
    p_TrackToken = xml_node_soap_get(p_node, "TrackToken");
    if (p_TrackToken && p_TrackToken->data)
    {
        strncpy(p_res->TrackToken, p_TrackToken->data, sizeof(p_res->TrackToken)-1);
    }
    
    return TRUE;
}

BOOL parse_trc_GetTrackConfiguration(XMLN * p_node, trc_GetTrackConfiguration_RES * p_res)
{
    XMLN * p_TrackConfiguration;

    p_TrackConfiguration = xml_node_soap_get(p_node, "TrackConfiguration");
    if (p_TrackConfiguration)
    {
        parse_TrackConfiguration(p_TrackConfiguration, &p_res->TrackConfiguration);
    }
    
    return TRUE;
}

BOOL parse_trc_CreateRecordingJob(XMLN * p_node, trc_CreateRecordingJob_RES * p_res)
{
    XMLN * p_JobToken;
    XMLN * p_JobConfiguration;

    p_JobToken = xml_node_soap_get(p_node, "JobToken");
    if (p_JobToken && p_JobToken->data)
    {
        strncpy(p_res->JobToken, p_JobToken->data, sizeof(p_res->JobToken)-1);
    }
    
    p_JobConfiguration = xml_node_soap_get(p_node, "JobConfiguration");
    if (p_JobConfiguration)
    {
        parse_RecordingJobConfiguration(p_JobConfiguration, &p_res->JobConfiguration);
    }
    
    return TRUE;
}

BOOL parse_trc_GetRecordingJobs(XMLN * p_node, trc_GetRecordingJobs_RES * p_res)
{
    XMLN * p_JobItem;

    p_JobItem = xml_node_soap_get(p_node, "JobItem");
    while (p_JobItem && soap_strcmp(p_JobItem->name, "JobItem") == 0)
    {
        RecordingJobList * p_RecordingJob = onvif_add_RecordingJob(&p_res->RecordingJobs);
        if (p_RecordingJob)
        {
            parse_RecordingJobItem(p_JobItem, &p_RecordingJob->RecordingJob);
        }
        
        p_JobItem = p_JobItem->next;
    }
    
    return TRUE;
}
 
BOOL parse_trc_SetRecordingJobConfiguration(XMLN * p_node, trc_SetRecordingJobConfiguration_RES * p_res)
{
    XMLN * p_JobConfiguration;
    
    p_JobConfiguration = xml_node_soap_get(p_node, "JobConfiguration");
    if (p_JobConfiguration)
    {
        parse_RecordingJobConfiguration(p_JobConfiguration, &p_res->JobConfiguration);
    }
    
    return TRUE;
}
 
BOOL parse_trc_GetRecordingJobConfiguration(XMLN * p_node, trc_GetRecordingJobConfiguration_RES * p_res)
{
    XMLN * p_JobConfiguration;
    
    p_JobConfiguration = xml_node_soap_get(p_node, "JobConfiguration");
    if (p_JobConfiguration)
    {
        parse_RecordingJobConfiguration(p_JobConfiguration, &p_res->JobConfiguration);
    }
    
    return TRUE;
}

BOOL parse_trc_GetRecordingJobState(XMLN * p_node, trc_GetRecordingJobState_RES * p_res)
{
    XMLN * p_State;

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State)
    {
        parse_RecordingJobStateInformation(p_State, &p_res->State);
    }
    
    return TRUE;
}

BOOL parse_trc_ExportRecordedData(XMLN * p_node, trc_ExportRecordedData_RES * p_res)
{
    XMLN * p_OperationToken;
    XMLN * p_FileNames;

    p_OperationToken = xml_node_soap_get(p_node, "OperationToken");
    if (p_OperationToken && p_OperationToken->data)
    {
        strncpy(p_res->OperationToken, p_OperationToken->data, sizeof(p_res->OperationToken)-1);
    }

    p_res->sizeFileNames = 0;
    
    p_FileNames = xml_node_soap_get(p_node, "FileNames");
    while (p_FileNames && soap_strcmp(p_FileNames->name, "FileNames") == 0)
    {
        uint32 idx = p_res->sizeFileNames;

        strncpy(p_res->FileNames[idx], p_FileNames->data, sizeof(p_res->FileNames[idx])-1);

        p_res->sizeFileNames++;
        if (p_res->sizeFileNames >= ARRAY_SIZE(p_res->FileNames))
        {
            break;
        }
        
        p_FileNames = p_FileNames->next;
    }

    return TRUE;
}

BOOL parse_trc_StopExportRecordedData(XMLN * p_node, trc_StopExportRecordedData_RES * p_res)
{
    XMLN * p_Progress;
    XMLN * p_FileProgressStatus;

    p_Progress = xml_node_soap_get(p_node, "Progress");
    if (p_Progress && p_Progress->data)
    {
        p_res->Progress = (float)atof(p_Progress->data);
    }

    p_FileProgressStatus = xml_node_soap_get(p_node, "FileProgressStatus");
    if (p_FileProgressStatus)
    {
        parse_ArrayOfFileProgress(p_FileProgressStatus, &p_res->FileProgressStatus);
    }

    return TRUE;
}

BOOL parse_trc_GetExportRecordedDataState(XMLN * p_node, trc_GetExportRecordedDataState_RES * p_res)
{
    XMLN * p_Progress;
    XMLN * p_FileProgressStatus;

    p_Progress = xml_node_soap_get(p_node, "Progress");
    if (p_Progress && p_Progress->data)
    {
        p_res->Progress = (float)atof(p_Progress->data);
    }

    p_FileProgressStatus = xml_node_soap_get(p_node, "FileProgressStatus");
    if (p_FileProgressStatus)
    {
        parse_ArrayOfFileProgress(p_FileProgressStatus, &p_res->FileProgressStatus);
    }

    return TRUE;
}

BOOL parse_trp_GetServiceCapabilities(XMLN * p_node, trp_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ReplayServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_trp_GetReplayUri(XMLN * p_node, trp_GetReplayUri_RES * p_res)
{
    XMLN * p_Uri = xml_node_soap_get(p_node, "Uri");
    if (p_Uri && p_Uri->data)
    {
        onvif_parse_uri(p_Uri->data, p_res->Uri, sizeof(p_res->Uri));
    }

    return TRUE;
}

BOOL parse_trp_GetReplayConfiguration(XMLN * p_node, trp_GetReplayConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_ReplayConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_tse_GetServiceCapabilities(XMLN * p_node, tse_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_SearchServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tse_GetRecordingSummary(XMLN * p_node, tse_GetRecordingSummary_RES * p_res)
{
    XMLN * p_Summary = xml_node_soap_get(p_node, "Summary");
    if (p_Summary)
    {
        XMLN * p_DataFrom;
        XMLN * p_DataUntil;
        XMLN * p_NumberRecordings;

        p_DataFrom = xml_node_soap_get(p_Summary, "DataFrom");
        if (p_DataFrom && p_DataFrom->data)
        {
            parse_XSDDatetime(p_DataFrom->data, &p_res->DataFrom);
        }

        p_DataUntil = xml_node_soap_get(p_Summary, "DataUntil");
        if (p_DataUntil && p_DataUntil->data)
        {
            parse_XSDDatetime(p_DataUntil->data, &p_res->DataUntil);
        }

        p_NumberRecordings = xml_node_soap_get(p_Summary, "NumberRecordings");
        if (p_NumberRecordings && p_NumberRecordings->data)
        {
            p_res->NumberRecordings = atoi(p_NumberRecordings->data);
        }
    }

    return TRUE;
}

BOOL parse_tse_GetRecordingInformation(XMLN * p_node, tse_GetRecordingInformation_RES * p_res)
{
    XMLN * p_RecordingInformation = xml_node_soap_get(p_node, "RecordingInformation");
    if (p_RecordingInformation)
    {
        parse_RecordingInformation(p_RecordingInformation, &p_res->RecordingInformation);
    }

    return TRUE;
}

BOOL parse_tse_GetMediaAttributes(XMLN * p_node, tse_GetMediaAttributes_RES * p_res)
{
    XMLN * p_MediaAttributes;
    XMLN * p_RecordingToken;
    XMLN * p_TrackAttributes;
    XMLN * p_From;
    XMLN * p_Until;
    onvif_MediaAttributes * p_ma = &p_res->MediaAttributes;

    p_MediaAttributes = xml_node_soap_get(p_node, "MediaAttributes");
    if (NULL == p_MediaAttributes)
    {
        return FALSE;
    }

    p_RecordingToken = xml_node_soap_get(p_MediaAttributes, "RecordingToken");
    if (p_RecordingToken && p_RecordingToken->data)
    {
        strncpy(p_ma->RecordingToken, p_RecordingToken->data, sizeof(p_ma->RecordingToken)-1);
    }

    p_ma->sizeTrackAttributes = 0;
    
    p_TrackAttributes = xml_node_soap_get(p_MediaAttributes, "TrackAttributes");
    while (p_TrackAttributes && soap_strcmp(p_TrackAttributes->name, "TrackAttributes") == 0)
    {
        uint32 idx = p_ma->sizeTrackAttributes;
        
        parse_TrackAttributes(p_TrackAttributes, &p_ma->TrackAttributes[idx]);

        p_ma->sizeTrackAttributes++;
        if (p_ma->sizeTrackAttributes >= ARRAY_SIZE(p_ma->TrackAttributes))
        {
            break;
        }

        p_TrackAttributes = p_TrackAttributes->next;
    }

    p_From = xml_node_soap_get(p_MediaAttributes, "From");
    if (p_From && p_From->data)
    {
        parse_XSDDatetime(p_From->data, &p_ma->From);
    }

    p_Until = xml_node_soap_get(p_MediaAttributes, "Until");
    if (p_Until && p_Until->data)
    {
        parse_XSDDatetime(p_Until->data, &p_ma->Until);
    }

    return TRUE;
}

BOOL parse_tse_FindRecordings(XMLN * p_node, tse_FindRecordings_RES * p_res)
{
    XMLN * p_SearchToken = xml_node_soap_get(p_node, "SearchToken");
    if (p_SearchToken && p_SearchToken->data)
    {
        strncpy(p_res->SearchToken, p_SearchToken->data, sizeof(p_res->SearchToken)-1);
    }

    return TRUE;
}

BOOL parse_tse_GetRecordingSearchResults(XMLN * p_node, tse_GetRecordingSearchResults_RES * p_res)
{
    XMLN * p_ResultList;
    XMLN * p_SearchState;
    XMLN * p_RecordingInformation;

    p_ResultList = xml_node_soap_get(p_node, "ResultList");
    if (NULL == p_ResultList)
    {
        return FALSE;
    }

    p_SearchState = xml_node_soap_get(p_ResultList, "SearchState");
    if (p_SearchState && p_SearchState->data)
    {
        p_res->ResultList.SearchState = onvif_StringToSearchState(p_SearchState->data);
    }
    
    p_RecordingInformation = xml_node_soap_get(p_ResultList, "RecordingInformation");
    while (p_RecordingInformation && soap_strcmp(p_RecordingInformation->name, "RecordingInformation") == 0)
    {
        RecordingInformationList * p_recording = onvif_add_RecordingInformation(&p_res->ResultList.RecordInformation);
        if (p_recording)
        {
            parse_RecordingInformation(p_RecordingInformation, &p_recording->RecordingInformation);
        }
        
        p_RecordingInformation = p_RecordingInformation->next;
    }

    return TRUE;
}

BOOL parse_tse_FindEvents(XMLN * p_node, tse_FindEvents_RES * p_res)
{
    XMLN * p_SearchToken = xml_node_soap_get(p_node, "SearchToken");
    if (p_SearchToken && p_SearchToken->data)
    {
        strncpy(p_res->SearchToken, p_SearchToken->data, sizeof(p_res->SearchToken)-1);
    }

    return TRUE;
}

BOOL parse_tse_GetEventSearchResults(XMLN * p_node, tse_GetEventSearchResults_RES * p_res)
{
    XMLN * p_ResultList;
    XMLN * p_SearchState;
    XMLN * p_Result;

    p_ResultList = xml_node_soap_get(p_node, "ResultList");
    if (NULL == p_ResultList)
    {
        return FALSE;
    }

    p_SearchState = xml_node_soap_get(p_ResultList, "SearchState");
    if (p_SearchState && p_SearchState->data)
    {
        p_res->ResultList.SearchState = onvif_StringToSearchState(p_SearchState->data);
    }
    
    p_Result = xml_node_soap_get(p_ResultList, "Result");
    while (p_Result && soap_strcmp(p_Result->name, "Result") == 0)
    {
        FindEventResultList * p_item = onvif_add_FindEventResult(&p_res->ResultList.Result);
        if (p_item)
        {
            parse_FindEventResult(p_Result, &p_item->Result);
        }
        
        p_Result = p_Result->next;
    }

    return TRUE;
}

BOOL parse_tse_FindMetadata(XMLN * p_node, tse_FindMetadata_RES * p_res)
{
    XMLN * p_SearchToken = xml_node_soap_get(p_node, "SearchToken");
    if (p_SearchToken && p_SearchToken->data)
    {
        strncpy(p_res->SearchToken, p_SearchToken->data, sizeof(p_res->SearchToken)-1);
    }

    return TRUE;
}

BOOL parse_tse_GetMetadataSearchResults(XMLN * p_node, tse_GetMetadataSearchResults_RES * p_res)
{
    XMLN * p_ResultList;
    XMLN * p_SearchState;
    XMLN * p_Result;

    p_ResultList = xml_node_soap_get(p_node, "ResultList");
    if (NULL == p_ResultList)
    {
        return FALSE;
    }

    p_SearchState = xml_node_soap_get(p_ResultList, "SearchState");
    if (p_SearchState && p_SearchState->data)
    {
        p_res->ResultList.SearchState = onvif_StringToSearchState(p_SearchState->data);
    }
    
    p_Result = xml_node_soap_get(p_ResultList, "Result");
    while (p_Result && soap_strcmp(p_Result->name, "Result") == 0)
    {
        FindMetadataResultList * p_item = onvif_add_FindMetadataResult(&p_res->ResultList.Result);
        if (p_item)
        {
            parse_FindMetadataResult(p_Result, &p_item->Result);
        }
        
        p_Result = p_Result->next;
    }

    return TRUE;
}

BOOL parse_tse_FindPTZPosition(XMLN * p_node, tse_FindPTZPosition_RES * p_res)
{
    XMLN * p_SearchToken = xml_node_soap_get(p_node, "SearchToken");
    if (p_SearchToken && p_SearchToken->data)
    {
        strncpy(p_res->SearchToken, p_SearchToken->data, sizeof(p_res->SearchToken)-1);
    }

    return TRUE;
}

BOOL parse_tse_GetPTZPositionSearchResults(XMLN * p_node, tse_GetPTZPositionSearchResults_RES * p_res)
{
    XMLN * p_ResultList;
    XMLN * p_SearchState;
    XMLN * p_Result;

    p_ResultList = xml_node_soap_get(p_node, "ResultList");
    if (NULL == p_ResultList)
    {
        return FALSE;
    }

    p_SearchState = xml_node_soap_get(p_ResultList, "SearchState");
    if (p_SearchState && p_SearchState->data)
    {
        p_res->ResultList.SearchState = onvif_StringToSearchState(p_SearchState->data);
    }
    
    p_Result = xml_node_soap_get(p_ResultList, "Result");
    while (p_Result && soap_strcmp(p_Result->name, "Result") == 0)
    {
        FindPTZPositionResultList * p_item = onvif_add_FindPTZPositionResult(&p_res->ResultList.Result);
        if (p_item)
        {
            parse_FindPTZPositionResult(p_Result, &p_item->Result);
        }
        
        p_Result = p_Result->next;
    }

    return TRUE;
}

BOOL parse_tse_GetSearchState(XMLN * p_node, tse_GetSearchState_RES * p_res)
{
    XMLN * p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        p_res->State = onvif_StringToSearchState(p_State->data);
    }

    return TRUE;
}

BOOL parse_tse_EndSearch(XMLN * p_node, tse_EndSearch_RES * p_res)
{
    XMLN * p_Endpoint = xml_node_soap_get(p_node, "Endpoint");
    if (p_Endpoint && p_Endpoint->data)
    {
        parse_XSDDatetime(p_Endpoint->data, &p_res->Endpoint);
    }

    return TRUE;
}

#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

BOOL parse_AccessControlServiceCapabilities(XMLN * p_node, onvif_AccessControlCapabilities * p_res)
{
    const char * p_MaxLimit;
    const char * p_MaxAccessPoints;
    const char * p_MaxAreas;
    const char * p_ClientSuppliedTokenSupported;
    const char * p_AccessPointManagementSupported;
    const char * p_AreaManagementSupported;

    p_MaxLimit = xml_attr_get(p_node, "MaxLimit");
    if (p_MaxLimit)
    {
        p_res->MaxLimit = atoi(p_MaxLimit);
    }

    p_MaxAccessPoints = xml_attr_get(p_node, "MaxAccessPoints");
    if (p_MaxAccessPoints)
    {
        p_res->MaxAccessPoints = atoi(p_MaxAccessPoints);
    }

    p_MaxAreas = xml_attr_get(p_node, "MaxAreas");
    if (p_MaxAreas)
    {
        p_res->MaxAreas = atoi(p_MaxAreas);
    }

    p_ClientSuppliedTokenSupported = xml_attr_get(p_node, "ClientSuppliedTokenSupported");
    if (p_ClientSuppliedTokenSupported)
    {
        p_res->ClientSuppliedTokenSupported = parse_Bool(p_ClientSuppliedTokenSupported);
    }

    p_AccessPointManagementSupported = xml_attr_get(p_node, "AccessPointManagementSupported");
    if (p_AccessPointManagementSupported)
    {
        p_res->AccessPointManagementSupported = parse_Bool(p_AccessPointManagementSupported);
    }

    p_AreaManagementSupported = xml_attr_get(p_node, "AreaManagementSupported");
    if (p_AreaManagementSupported)
    {
        p_res->AreaManagementSupported = parse_Bool(p_AreaManagementSupported);
    }

    return TRUE;
}

BOOL parse_AccessControlService(XMLN * p_node, onvif_AccessControlCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_AccessControlServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_DoorControlServiceCapabilities(XMLN * p_node, onvif_DoorControlCapabilities * p_res)
{
    const char * p_MaxLimit;
    const char * p_MaxDoors;
    const char * p_ClientSuppliedTokenSupported;
    const char * p_DoorManagementSupported;

    p_MaxLimit = xml_attr_get(p_node, "MaxLimit");
    if (p_MaxLimit)
    {
        p_res->MaxLimit = atoi(p_MaxLimit);
    }

    p_MaxDoors = xml_attr_get(p_node, "MaxDoors");
    if (p_MaxDoors)
    {
        p_res->MaxDoors = atoi(p_MaxDoors);
    }

    p_ClientSuppliedTokenSupported = xml_attr_get(p_node, "ClientSuppliedTokenSupported");
    if (p_ClientSuppliedTokenSupported)
    {
        p_res->ClientSuppliedTokenSupported = parse_Bool(p_ClientSuppliedTokenSupported);
    }

    p_DoorManagementSupported = xml_attr_get(p_node, "DoorManagementSupported");
    if (p_DoorManagementSupported)
    {
        p_res->DoorManagementSupported = parse_Bool(p_DoorManagementSupported);
    }

    return TRUE;
}

BOOL parse_DoorControlService(XMLN * p_node, onvif_DoorControlCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_DoorControlServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_AccessPointCapabilities(XMLN * p_node, onvif_AccessPointCapabilities * p_res)
{
    const char * p_DisableAccessPoint;
    const char * p_Duress;
    const char * p_AnonymousAccess;
    const char * p_AccessTaken;
    const char * p_ExternalAuthorization;
    const char * p_IdentifierAccess;
    const char * p_SupportedRecognitionTypes;
    const char * p_SupportedFeedbackTypes;

    p_DisableAccessPoint = xml_attr_get(p_node, "DisableAccessPoint");
    if (p_DisableAccessPoint)
    {
        p_res->DisableAccessPoint = parse_Bool(p_DisableAccessPoint);
    }

    p_Duress = xml_attr_get(p_node, "Duress");
    if (p_Duress)
    {
        p_res->Duress = parse_Bool(p_Duress);
    }

    p_AnonymousAccess = xml_attr_get(p_node, "AnonymousAccess");
    if (p_AnonymousAccess)
    {
        p_res->AnonymousAccess = parse_Bool(p_AnonymousAccess);
    }

    p_AccessTaken = xml_attr_get(p_node, "AccessTaken");
    if (p_AccessTaken)
    {
        p_res->AccessTaken = parse_Bool(p_AccessTaken);
    }

    p_ExternalAuthorization = xml_attr_get(p_node, "ExternalAuthorization");
    if (p_ExternalAuthorization)
    {
        p_res->ExternalAuthorization = parse_Bool(p_ExternalAuthorization);
    }

    p_IdentifierAccess = xml_attr_get(p_node, "IdentifierAccess");
    if (p_IdentifierAccess)
    {
        p_res->IdentifierAccess = parse_Bool(p_IdentifierAccess);
    }

    p_SupportedRecognitionTypes = xml_attr_get(p_node, "SupportedRecognitionTypes");
    if (p_SupportedRecognitionTypes)
    {
        strncpy(p_res->SupportedRecognitionTypes, p_SupportedRecognitionTypes, sizeof(p_res->SupportedRecognitionTypes)-1);
    }

    p_SupportedFeedbackTypes = xml_attr_get(p_node, "SupportedFeedbackTypes");
    if (p_SupportedFeedbackTypes)
    {
        strncpy(p_res->SupportedFeedbackTypes, p_SupportedFeedbackTypes, sizeof(p_res->SupportedFeedbackTypes)-1);
    }

    return TRUE;
}

BOOL parse_AccessPointInfo(XMLN * p_node, onvif_AccessPointInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    XMLN * p_AreaFrom;
    XMLN * p_AreaTo;
    XMLN * p_EntityType;
    XMLN * p_Entity;
    XMLN * p_Capabilities;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_AreaFrom = xml_node_soap_get(p_node, "AreaFrom");
    if (p_AreaFrom && p_AreaFrom->data)
    {
        p_res->AreaFromFlag = 1;
        strncpy(p_res->AreaFrom, p_AreaFrom->data, sizeof(p_res->AreaFrom)-1);
    }

    p_AreaTo = xml_node_soap_get(p_node, "AreaTo");
    if (p_AreaTo && p_AreaTo->data)
    {
        p_res->AreaToFlag = 1;
        strncpy(p_res->AreaTo, p_AreaTo->data, sizeof(p_res->AreaTo)-1);
    }

    p_EntityType = xml_node_soap_get(p_node, "EntityType");
    if (p_EntityType && p_EntityType->data)
    {
        p_res->EntityTypeFlag = 1;
        strncpy(p_res->EntityType, p_EntityType->data, sizeof(p_res->EntityType)-1);
    }

    p_Entity = xml_node_soap_get(p_node, "Entity");
    if (p_Entity && p_Entity->data)
    {
        strncpy(p_res->Entity, p_Entity->data, sizeof(p_res->Entity)-1);
    }

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        parse_AccessPointCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return TRUE;
}

BOOL parse_AreaInfo(XMLN * p_node, onvif_AreaInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_DoorCapabilities(XMLN * p_node, onvif_DoorCapabilities * p_res)
{
    const char * p_Access;
    const char * p_AccessTimingOverride;
    const char * p_Lock;
    const char * p_Unlock;
    const char * p_Block;
    const char * p_DoubleLock;
    const char * p_LockDown;
    const char * p_LockOpen;
    const char * p_DoorMonitor;
    const char * p_LockMonitor;
    const char * p_DoubleLockMonitor;
    const char * p_Alarm;
    const char * p_Tamper;
    const char * p_Fault;

    p_Access = xml_attr_get(p_node, "Access");
    if (p_Access)
    {
        p_res->Access = parse_Bool(p_Access);
    }

    p_AccessTimingOverride = xml_attr_get(p_node, "AccessTimingOverride");
    if (p_AccessTimingOverride)
    {
        p_res->AccessTimingOverride = parse_Bool(p_AccessTimingOverride);
    }

    p_Lock = xml_attr_get(p_node, "Lock");
    if (p_Lock)
    {
        p_res->Lock = parse_Bool(p_Lock);
    }

    p_Unlock = xml_attr_get(p_node, "Unlock");
    if (p_Unlock)
    {
        p_res->Unlock = parse_Bool(p_Unlock);
    }

    p_Block = xml_attr_get(p_node, "Block");
    if (p_Block)
    {
        p_res->Block = parse_Bool(p_Block);
    }

    p_DoubleLock = xml_attr_get(p_node, "DoubleLock");
    if (p_DoubleLock)
    {
        p_res->DoubleLock = parse_Bool(p_DoubleLock);
    }

    p_LockDown = xml_attr_get(p_node, "LockDown");
    if (p_LockDown)
    {
        p_res->LockDown = parse_Bool(p_LockDown);
    }

    p_LockOpen = xml_attr_get(p_node, "LockOpen");
    if (p_LockOpen)
    {
        p_res->LockOpen = parse_Bool(p_LockOpen);
    }

    p_DoorMonitor = xml_attr_get(p_node, "DoorMonitor");
    if (p_DoorMonitor)
    {
        p_res->DoorMonitor = parse_Bool(p_DoorMonitor);
    }

    p_LockMonitor = xml_attr_get(p_node, "LockMonitor");
    if (p_LockMonitor)
    {
        p_res->LockMonitor = parse_Bool(p_LockMonitor);
    }

    p_DoubleLockMonitor = xml_attr_get(p_node, "DoubleLockMonitor");
    if (p_DoubleLockMonitor)
    {
        p_res->DoubleLockMonitor = parse_Bool(p_DoubleLockMonitor);
    }

    p_Alarm = xml_attr_get(p_node, "Alarm");
    if (p_Alarm)
    {
        p_res->Alarm = parse_Bool(p_Alarm);
    }

    p_Tamper = xml_attr_get(p_node, "Tamper");
    if (p_Tamper)
    {
        p_res->Tamper = parse_Bool(p_Tamper);
    }

    p_Fault = xml_attr_get(p_node, "Fault");
    if (p_Fault)
    {
        p_res->Fault = parse_Bool(p_Fault);
    }
    
    return TRUE;
}

BOOL parse_DoorInfo(XMLN * p_node, onvif_DoorInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    XMLN * p_Capabilities;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token) - 1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name) - 1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description) - 1);
    }

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        parse_DoorCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return TRUE;
}

BOOL parse_Timings(XMLN * p_node, onvif_Timings * p_res)
{
    XMLN * p_ReleaseTime;
    XMLN * p_OpenTime;
    XMLN * p_ExtendedReleaseTime;
    XMLN * p_DelayTimeBeforeRelock;
    XMLN * p_ExtendedOpenTime;
    XMLN * p_PreAlarmTime;

    p_ReleaseTime = xml_node_soap_get(p_node, "ReleaseTime");
    if (p_ReleaseTime && p_ReleaseTime->data)
    {
        parse_XSDDuration(p_ReleaseTime->data, (int*)&p_res->ReleaseTime);
    }

    p_OpenTime = xml_node_soap_get(p_node, "OpenTime");
    if (p_OpenTime && p_OpenTime->data)
    {
        parse_XSDDuration(p_OpenTime->data, (int*)&p_res->OpenTime);
    }

    p_ExtendedReleaseTime = xml_node_soap_get(p_node, "ExtendedReleaseTime");
    if (p_ExtendedReleaseTime && p_ExtendedReleaseTime->data)
    {
        p_res->ExtendedReleaseTimeFlag = 1;
        parse_XSDDuration(p_ExtendedReleaseTime->data, (int*)&p_res->ExtendedReleaseTime);
    }

    p_DelayTimeBeforeRelock = xml_node_soap_get(p_node, "DelayTimeBeforeRelock");
    if (p_DelayTimeBeforeRelock && p_DelayTimeBeforeRelock->data)
    {
        p_res->DelayTimeBeforeRelockFlag = 1;
        parse_XSDDuration(p_DelayTimeBeforeRelock->data, (int*)&p_res->DelayTimeBeforeRelock);
    }

    p_ExtendedOpenTime = xml_node_soap_get(p_node, "ExtendedOpenTime");
    if (p_ExtendedOpenTime && p_ExtendedOpenTime->data)
    {
        p_res->ExtendedOpenTimeFlag = 1;
        parse_XSDDuration(p_ExtendedOpenTime->data, (int*)&p_res->ExtendedOpenTime);
    }

    p_PreAlarmTime = xml_node_soap_get(p_node, "PreAlarmTime");
    if (p_PreAlarmTime && p_PreAlarmTime->data)
    {
        p_res->PreAlarmTimeFlag = 1;
        parse_XSDDuration(p_PreAlarmTime->data, (int*)&p_res->PreAlarmTime);
    }

    return TRUE;
}

BOOL parse_Door(XMLN * p_node, onvif_Door * p_res)
{
    XMLN * p_DoorType;
    XMLN * p_Timings;
    
    parse_DoorInfo(p_node, &p_res->DoorInfo);

    p_DoorType = xml_node_soap_get(p_node, "DoorType");
    if (p_DoorType && p_DoorType->data)
    {
        strncpy(p_res->DoorType, p_DoorType->data, sizeof(p_res->DoorType) - 1);
    }

    p_Timings = xml_node_soap_get(p_node, "Timings");
    if (p_Timings)
    {
        parse_Timings(p_Timings, &p_res->Timings);
    }
    
    return TRUE;
}

BOOL parse_DoorTamper(XMLN * p_node, onvif_DoorTamper * p_res)
{
    XMLN * p_Reason;
    XMLN * p_State;

    p_Reason = xml_node_soap_get(p_node, "Reason");
    if (p_Reason && p_Reason->data)
    {
        p_res->ReasonFlag = 1;
        strncpy(p_res->Reason, p_Reason->data, sizeof(p_res->Reason)-1);
    }

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        p_res->State = onvif_StringToDoorTamperState(p_State->data);
    }

    return TRUE;
}

BOOL parse_DoorFault(XMLN * p_node, onvif_DoorFault * p_res)
{
    XMLN * p_Reason;
    XMLN * p_State;

    p_Reason = xml_node_soap_get(p_node, "Reason");
    if (p_Reason && p_Reason->data)
    {
        p_res->ReasonFlag = 1;
        strncpy(p_res->Reason, p_Reason->data, sizeof(p_res->Reason)-1);
    }

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        p_res->State = onvif_StringToDoorFaultState(p_State->data);
    }

    return TRUE;
}

BOOL parse_DoorState(XMLN * p_node, onvif_DoorState * p_res)
{
    XMLN * p_DoorPhysicalState;
    XMLN * p_LockPhysicalState;
    XMLN * p_DoubleLockPhysicalState;
    XMLN * p_Alarm;
    XMLN * p_Tamper;
    XMLN * p_Fault;
    XMLN * p_DoorMode;

    p_DoorPhysicalState = xml_node_soap_get(p_node, "DoorPhysicalState");
    if (p_DoorPhysicalState && p_DoorPhysicalState->data)
    {
        p_res->DoorPhysicalStateFlag = 1;
        p_res->DoorPhysicalState = onvif_StringToDoorPhysicalState(p_DoorPhysicalState->data);
    }

    p_LockPhysicalState = xml_node_soap_get(p_node, "LockPhysicalState");
    if (p_LockPhysicalState && p_LockPhysicalState->data)
    {
        p_res->LockPhysicalStateFlag = 1;
        p_res->LockPhysicalState = onvif_StringToLockPhysicalState(p_LockPhysicalState->data);
    }

    p_DoubleLockPhysicalState = xml_node_soap_get(p_node, "DoubleLockPhysicalState");
    if (p_DoubleLockPhysicalState && p_DoubleLockPhysicalState->data)
    {
        p_res->DoubleLockPhysicalStateFlag = 1;
        p_res->DoubleLockPhysicalState = onvif_StringToLockPhysicalState(p_DoubleLockPhysicalState->data);
    }

    p_Alarm = xml_node_soap_get(p_node, "Alarm");
    if (p_Alarm && p_Alarm->data)
    {
        p_res->AlarmFlag = 1;
        p_res->Alarm = onvif_StringToDoorAlarmState(p_Alarm->data);
    }

    p_Tamper = xml_node_soap_get(p_node, "Tamper");
    if (p_Tamper)
    {
        p_res->TamperFlag = parse_DoorTamper(p_Tamper, &p_res->Tamper);
    }

    p_Fault = xml_node_soap_get(p_node, "Fault");
    if (p_Fault)
    {
        p_res->FaultFlag = parse_DoorFault(p_Fault, &p_res->Fault);
    }

    p_DoorMode = xml_node_soap_get(p_node, "DoorMode");
    if (p_DoorMode && p_DoorMode->data)
    {
        p_res->DoorMode = onvif_StringToDoorMode(p_DoorMode->data);
    }

    return TRUE;
}

BOOL parse_tac_GetServiceCapabilities(XMLN * p_node, tac_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_AccessControlServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tac_GetAccessPointInfoList(XMLN * p_node, tac_GetAccessPointInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_AccessPointInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_AccessPointInfo = xml_node_soap_get(p_node, "AccessPointInfo");
    while (p_AccessPointInfo && soap_strcmp(p_AccessPointInfo->name, "AccessPointInfo") == 0)
    {
        AccessPointList * p_info = onvif_add_AccessPoint(&p_res->AccessPointInfo);
        if (p_info)
        {
            parse_AccessPointInfo(p_AccessPointInfo, &p_info->AccessPointInfo);
        }

        p_AccessPointInfo = p_AccessPointInfo->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAccessPointInfo(XMLN * p_node, tac_GetAccessPointInfo_RES * p_res)
{
    XMLN * p_AccessPointInfo;

    p_AccessPointInfo = xml_node_soap_get(p_node, "AccessPointInfo");
    while (p_AccessPointInfo && soap_strcmp(p_AccessPointInfo->name, "AccessPointInfo") == 0)
    {
        AccessPointList * p_info = onvif_add_AccessPoint(&p_res->AccessPointInfo);
        if (p_info)
        {
            parse_AccessPointInfo(p_AccessPointInfo, &p_info->AccessPointInfo);
        }

        p_AccessPointInfo = p_AccessPointInfo->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAccessPointList(XMLN * p_node, tac_GetAccessPointList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_AccessPoint;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_AccessPoint = xml_node_soap_get(p_node, "AccessPoint");
    while (p_AccessPoint && soap_strcmp(p_AccessPoint->name, "AccessPoint") == 0)
    {
        AccessPointList * p_info = onvif_add_AccessPoint(&p_res->AccessPoint);
        if (p_info)
        {
            XMLN * p_AuthenticationProfileToken;
            
            parse_AccessPointInfo(p_AccessPoint, &p_info->AccessPointInfo);

            p_AuthenticationProfileToken = xml_node_soap_get(p_AccessPoint, "AuthenticationProfileToken");
            if (p_AuthenticationProfileToken && p_AuthenticationProfileToken->data)
            {
                p_info->AuthenticationProfileTokenFlag = 1;
                strncpy(p_info->AuthenticationProfileToken, 
                    p_AuthenticationProfileToken->data, 
                    sizeof(p_info->AuthenticationProfileToken)-1);
            }
        }
        
        p_AccessPoint = p_AccessPoint->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAccessPoints(XMLN * p_node, tac_GetAccessPoints_RES * p_res)
{
    XMLN * p_AccessPoint;
    
    p_AccessPoint = xml_node_soap_get(p_node, "AccessPoint");
    while (p_AccessPoint && soap_strcmp(p_AccessPoint->name, "AccessPoint") == 0)
    {
        AccessPointList * p_info = onvif_add_AccessPoint(&p_res->AccessPoint);
        if (p_info)
        {
            XMLN * p_AuthenticationProfileToken;
            
            parse_AccessPointInfo(p_AccessPoint, &p_info->AccessPointInfo);

            p_AuthenticationProfileToken = xml_node_soap_get(p_AccessPoint, "AuthenticationProfileToken");
            if (p_AuthenticationProfileToken && p_AuthenticationProfileToken->data)
            {
                p_info->AuthenticationProfileTokenFlag = 1;
                strncpy(p_info->AuthenticationProfileToken, 
                    p_AuthenticationProfileToken->data, 
                    sizeof(p_info->AuthenticationProfileToken)-1);
            }
        }
        
        p_AccessPoint = p_AccessPoint->next;
    }

    return TRUE;
}

BOOL parse_tac_CreateAccessPoint(XMLN * p_node, tac_CreateAccessPoint_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tac_GetAreaInfoList(XMLN * p_node, tac_GetAreaInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_AreaInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_AreaInfo = xml_node_soap_get(p_node, "AreaInfo");
    while (p_AreaInfo && soap_strcmp(p_AreaInfo->name, "AreaInfo") == 0)
    {
        AreaList * p_info = onvif_add_Area(&p_res->AreaInfo);
        if (p_info)
        {
            parse_AreaInfo(p_AreaInfo, &p_info->AreaInfo);
        }

        p_AreaInfo = p_AreaInfo->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAreaInfo(XMLN * p_node, tac_GetAreaInfo_RES * p_res)
{
    XMLN * p_AreaInfo;

    p_AreaInfo = xml_node_soap_get(p_node, "AreaInfo");
    while (p_AreaInfo && soap_strcmp(p_AreaInfo->name, "AreaInfo") == 0)
    {
        AreaList * p_info = onvif_add_Area(&p_res->AreaInfo);
        if (p_info)
        {
            parse_AreaInfo(p_AreaInfo, &p_info->AreaInfo);
        }

        p_AreaInfo = p_AreaInfo->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAreaList(XMLN * p_node, tac_GetAreaList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_Area;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }
    
    p_Area = xml_node_soap_get(p_node, "Area");
    while (p_Area && soap_strcmp(p_Area->name, "Area") == 0)
    {
        AreaList * p_info = onvif_add_Area(&p_res->Area);
        if (p_info)
        {
            parse_AreaInfo(p_Area, &p_info->AreaInfo);
        }
        
        p_Area = p_Area->next;
    }

    return TRUE;
}

BOOL parse_tac_GetAreas(XMLN * p_node, tac_GetAreas_RES * p_res)
{
    XMLN * p_Area;
    
    p_Area = xml_node_soap_get(p_node, "Area");
    while (p_Area && soap_strcmp(p_Area->name, "Area") == 0)
    {
        AreaList * p_info = onvif_add_Area(&p_res->Area);
        if (p_info)
        {
            parse_AreaInfo(p_Area, &p_info->AreaInfo);
        }
        
        p_Area = p_Area->next;
    }

    return TRUE;
}

BOOL parse_tac_CreateArea(XMLN * p_node, tac_CreateArea_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tac_GetAccessPointState(XMLN * p_node, tac_GetAccessPointState_RES * p_res)
{
    XMLN * p_AccessPointState;

    p_AccessPointState = xml_node_soap_get(p_node, "AccessPointState");
    if (p_AccessPointState)
    {
        XMLN * p_Enabled;
        
        p_Enabled = xml_node_soap_get(p_AccessPointState, "Enabled");
        if (p_Enabled && p_Enabled->data)
        {
            p_res->Enabled = parse_Bool(p_Enabled->data);
        }        
    }

    return TRUE;
}

BOOL parse_tdc_GetServiceCapabilities(XMLN * p_node, tdc_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_DoorControlServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tdc_GetDoorInfoList(XMLN * p_node, tdc_GetDoorInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_DoorInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference) - 1);
    }

    p_DoorInfo = xml_node_soap_get(p_node, "DoorInfo");
    while (p_DoorInfo && soap_strcmp(p_DoorInfo->name, "DoorInfo") == 0)
    {
        DoorInfoList * p_info = onvif_add_DoorInfo(&p_res->DoorInfo);
        if (p_info)
        {
            parse_DoorInfo(p_DoorInfo, &p_info->DoorInfo);
        }

        p_DoorInfo = p_DoorInfo->next;
    }

    return TRUE;
}

BOOL parse_tdc_GetDoorInfo(XMLN * p_node, tdc_GetDoorInfo_RES * p_res)
{
    XMLN * p_DoorInfo;

    p_DoorInfo = xml_node_soap_get(p_node, "DoorInfo");
    while (p_DoorInfo && soap_strcmp(p_DoorInfo->name, "DoorInfo") == 0)
    {
        DoorInfoList * p_info = onvif_add_DoorInfo(&p_res->DoorInfo);
        if (p_info)
        {
            parse_DoorInfo(p_DoorInfo, &p_info->DoorInfo);
        }

        p_DoorInfo = p_DoorInfo->next;
    }

    return TRUE;
}

BOOL parse_tdc_GetDoorState(XMLN * p_node, tdc_GetDoorState_RES * p_res)
{
    XMLN * p_DoorState;

    p_DoorState = xml_node_soap_get(p_node, "DoorState");
    if (p_DoorState)
    {
        parse_DoorState(p_DoorState, &p_res->DoorState);
    }

    return TRUE;
}

BOOL parse_tdc_GetDoors(XMLN * p_node, tdc_GetDoors_RES * p_res)
{
    XMLN * p_Door;

    p_Door = xml_node_soap_get(p_node, "Door");
    while (p_Door && soap_strcmp(p_Door->name, "Door") == 0)
    {
        DoorList * p_new = onvif_add_Door(&p_res->Door);
        if (p_new)
        {
            parse_Door(p_Door, &p_new->Door);
        }

        p_Door = p_Door->next;
    }

    return TRUE;
}

BOOL parse_tdc_GetDoorList(XMLN * p_node, tdc_GetDoorList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_Door;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference) - 1);
    }

    p_Door = xml_node_soap_get(p_node, "Door");
    while (p_Door && soap_strcmp(p_Door->name, "Door") == 0)
    {
        DoorList * p_new = onvif_add_Door(&p_res->Door);
        if (p_new)
        {
            parse_Door(p_Door, &p_new->Door);
        }

        p_Door = p_Door->next;
    }

    return TRUE;
}

BOOL parse_tdc_CreateDoor(XMLN * p_node, tdc_CreateDoor_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT

BOOL parse_ThermalServiceCapabilities(XMLN * p_node, onvif_ThermalCapabilities * p_res)
{
    const char * p_Radiometry;

    p_Radiometry = xml_attr_get(p_node, "Radiometry");
    if (p_Radiometry)
    {
        p_res->Radiometry = parse_Bool(p_Radiometry);
    }

    return TRUE;
}

BOOL parse_ThermalService(XMLN * p_node, onvif_ThermalCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ThermalServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_ColorPalette(XMLN * p_node, onvif_ColorPalette * p_res)
{
    const char * p_token;
    const char * p_Type;
    XMLN * p_Name;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Type = xml_attr_get(p_node, "Type");
    if (p_Type)
    {
        strncpy(p_res->Type, p_Type, sizeof(p_res->Type)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    return TRUE;
}

BOOL parse_NUCTable(XMLN * p_node, onvif_NUCTable * p_res)
{
    const char * p_token;
    const char * p_LowTemperature;
    const char * p_HighTemperature;
    XMLN * p_Name;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_LowTemperature = xml_attr_get(p_node, "LowTemperature");
    if (p_LowTemperature)
    {
        p_res->LowTemperatureFlag = 1;
        p_res->LowTemperature = (float) atof(p_LowTemperature);
    }

    p_HighTemperature = xml_attr_get(p_node, "HighTemperature");
    if (p_HighTemperature)
    {
        p_res->HighTemperatureFlag = 1;
        p_res->HighTemperature = (float) atof(p_HighTemperature);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    return TRUE;
}

BOOL parse_Cooler(XMLN * p_node, onvif_Cooler * p_res)
{
    XMLN * p_Enabled;
    XMLN * p_RunTime;

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_RunTime = xml_node_soap_get(p_node, "RunTime");
    if (p_RunTime && p_RunTime->data)
    {
        p_res->RunTimeFlag = 1;
        p_res->RunTime = (float) atof(p_RunTime->data);
    }

    return TRUE;
}

BOOL parse_ThermalConfiguration(XMLN * p_node, onvif_ThermalConfiguration * p_res)
{
    XMLN * p_ColorPalette;
    XMLN * p_Polarity;
    XMLN * p_NUCTable;
    XMLN * p_Cooler;

    p_ColorPalette = xml_node_soap_get(p_node, "ColorPalette");
    if (p_ColorPalette)
    {
        parse_ColorPalette(p_ColorPalette, &p_res->ColorPalette);
    }

    p_Polarity = xml_node_soap_get(p_node, "Polarity");
    if (p_Polarity && p_Polarity->data)
    {
        p_res->Polarity = onvif_StringToPolarity(p_Polarity->data);
    }

    p_NUCTable = xml_node_soap_get(p_node, "NUCTable");
    if (p_NUCTable)
    {
        p_res->NUCTableFlag = parse_NUCTable(p_NUCTable, &p_res->NUCTable);
    }

    p_Cooler = xml_node_soap_get(p_node, "Cooler");
    if (p_Cooler)
    {
        p_res->CoolerFlag = parse_Cooler(p_Cooler, &p_res->Cooler);
    }

    return TRUE;
}

BOOL parse_CoolerOptions(XMLN * p_node, onvif_CoolerOptions * p_res)
{
    XMLN * p_Enabled;
    
    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }
        
    return TRUE;
}

BOOL parse_ThermalConfigurationOptions(XMLN * p_node, onvif_ThermalConfigurationOptions * p_res)
{
    XMLN * p_ColorPalette;
    XMLN * p_NUCTable;
    XMLN * p_CoolerOptions;

    p_ColorPalette = xml_node_soap_get(p_node, "ColorPalette");
    while (p_ColorPalette && soap_strcmp(p_ColorPalette->name, "ColorPalette") == 0)
    {
        ColorPaletteList * p_info = onvif_add_ColorPalette(&p_res->ColorPalette);
        if (p_info)
        {
            parse_ColorPalette(p_ColorPalette, &p_info->ColorPalette);
        }

        p_ColorPalette = p_ColorPalette->next;
    }

    p_NUCTable = xml_node_soap_get(p_node, "NUCTable");
    while (p_NUCTable && soap_strcmp(p_NUCTable->name, "NUCTable") == 0)
    {
        NUCTableList * p_info = onvif_add_NUCTable(&p_res->NUCTable);
        if (p_info)
        {
            parse_NUCTable(p_NUCTable, &p_info->NUCTable);
        }

        p_NUCTable = p_NUCTable->next;
    }

    p_CoolerOptions = xml_node_soap_get(p_node, "CoolerOptions");
    if (p_CoolerOptions)
    {
        p_res->CoolerOptionsFlag = parse_CoolerOptions(p_CoolerOptions, &p_res->CoolerOptions);
    }
    
    return TRUE;
}

BOOL parse_RadiometryGlobalParameters(XMLN * p_node, onvif_RadiometryGlobalParameters * p_res)
{
    XMLN * p_ReflectedAmbientTemperature;
    XMLN * p_Emissivity;
    XMLN * p_DistanceToObject;
    XMLN * p_RelativeHumidity;
    XMLN * p_AtmosphericTemperature;
    XMLN * p_AtmosphericTransmittance;
    XMLN * p_ExtOpticsTemperature;
    XMLN * p_ExtOpticsTransmittance;

    p_ReflectedAmbientTemperature = xml_node_soap_get(p_node, "ReflectedAmbientTemperature");
    if (p_ReflectedAmbientTemperature && p_ReflectedAmbientTemperature->data)
    {
        p_res->ReflectedAmbientTemperature = (float) atof(p_ReflectedAmbientTemperature->data);
    }

    p_Emissivity = xml_node_soap_get(p_node, "Emissivity");
    if (p_Emissivity && p_Emissivity->data)
    {
        p_res->Emissivity = (float) atof(p_Emissivity->data);
    }

    p_DistanceToObject = xml_node_soap_get(p_node, "DistanceToObject");
    if (p_DistanceToObject && p_DistanceToObject->data)
    {
        p_res->DistanceToObject = (float) atof(p_DistanceToObject->data);
    }

    p_RelativeHumidity = xml_node_soap_get(p_node, "RelativeHumidity");
    if (p_RelativeHumidity && p_RelativeHumidity->data)
    {
        p_res->RelativeHumidityFlag = 1;
        p_res->RelativeHumidity = (float) atof(p_RelativeHumidity->data);
    }

    p_AtmosphericTemperature = xml_node_soap_get(p_node, "AtmosphericTemperature");
    if (p_AtmosphericTemperature && p_AtmosphericTemperature->data)
    {
        p_res->AtmosphericTemperatureFlag = 1;
        p_res->AtmosphericTemperature = (float) atof(p_AtmosphericTemperature->data);
    }

    p_AtmosphericTransmittance = xml_node_soap_get(p_node, "AtmosphericTransmittance");
    if (p_AtmosphericTransmittance && p_AtmosphericTransmittance->data)
    {
        p_res->AtmosphericTransmittanceFlag = 1;
        p_res->AtmosphericTransmittance = (float) atof(p_AtmosphericTransmittance->data);
    }

    p_ExtOpticsTemperature = xml_node_soap_get(p_node, "ExtOpticsTemperature");
    if (p_ExtOpticsTemperature && p_ExtOpticsTemperature->data)
    {
        p_res->ExtOpticsTemperatureFlag = 1;
        p_res->ExtOpticsTemperature = (float) atof(p_ExtOpticsTemperature->data);
    }

    p_ExtOpticsTransmittance = xml_node_soap_get(p_node, "ExtOpticsTransmittance");
    if (p_ExtOpticsTransmittance && p_ExtOpticsTransmittance->data)
    {
        p_res->ExtOpticsTransmittanceFlag = 1;
        p_res->ExtOpticsTransmittance = (float) atof(p_ExtOpticsTransmittance->data);
    }

    return TRUE;
}

BOOL parse_RadiometryConfiguration(XMLN * p_node, onvif_RadiometryConfiguration * p_res)
{
    XMLN * p_RadiometryGlobalParameters;

    p_RadiometryGlobalParameters = xml_node_soap_get(p_node, "RadiometryGlobalParameters");
    if (p_RadiometryGlobalParameters)
    {
        p_res->RadiometryGlobalParametersFlag = 1;
        parse_RadiometryGlobalParameters(p_RadiometryGlobalParameters, &p_res->RadiometryGlobalParameters);
    }

    return TRUE;
}

BOOL parse_RadiometryGlobalParameterOptions(XMLN * p_node, onvif_RadiometryGlobalParameterOptions * p_res)
{
    XMLN * p_ReflectedAmbientTemperature;
    XMLN * p_Emissivity;
    XMLN * p_DistanceToObject;
    XMLN * p_RelativeHumidity;
    XMLN * p_AtmosphericTemperature;
    XMLN * p_AtmosphericTransmittance;
    XMLN * p_ExtOpticsTemperature;
    XMLN * p_ExtOpticsTransmittance;

    p_ReflectedAmbientTemperature = xml_node_soap_get(p_node, "ReflectedAmbientTemperature");
    if (p_ReflectedAmbientTemperature)
    {
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->ReflectedAmbientTemperature);
    }

    p_Emissivity = xml_node_soap_get(p_node, "Emissivity");
    if (p_Emissivity)
    {
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->Emissivity);
    }

    p_DistanceToObject = xml_node_soap_get(p_node, "DistanceToObject");
    if (p_DistanceToObject)
    {
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->DistanceToObject);
    }

    p_RelativeHumidity = xml_node_soap_get(p_node, "RelativeHumidity");
    if (p_RelativeHumidity)
    {
        p_res->RelativeHumidityFlag = 1;
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->RelativeHumidity);
    }

    p_AtmosphericTemperature = xml_node_soap_get(p_node, "AtmosphericTemperature");
    if (p_AtmosphericTemperature)
    {
        p_res->AtmosphericTemperatureFlag = 1;
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->AtmosphericTemperature);
    }

    p_AtmosphericTransmittance = xml_node_soap_get(p_node, "AtmosphericTransmittance");
    if (p_AtmosphericTransmittance)
    {
        p_res->AtmosphericTransmittanceFlag = 1;
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->AtmosphericTransmittance);
    }

    p_ExtOpticsTemperature = xml_node_soap_get(p_node, "ExtOpticsTemperature");
    if (p_ExtOpticsTemperature)
    {
        p_res->ExtOpticsTemperatureFlag = 1;
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->ExtOpticsTemperature);
    }

    p_ExtOpticsTransmittance = xml_node_soap_get(p_node, "ExtOpticsTransmittance");
    if (p_ExtOpticsTransmittance)
    {
        p_res->ExtOpticsTransmittanceFlag = 1;
        parse_FloatRange(p_ReflectedAmbientTemperature, &p_res->ExtOpticsTransmittance);
    }

    return TRUE;    
}

BOOL parse_RadiometryConfigurationOptions(XMLN * p_node, onvif_RadiometryConfigurationOptions * p_res)
{
    XMLN * p_RadiometryGlobalParameterOptions;

    p_RadiometryGlobalParameterOptions = xml_node_soap_get(p_node, "RadiometryGlobalParameterOptions");
    if (p_RadiometryGlobalParameterOptions)
    {
        p_res->RadiometryGlobalParameterOptionsFlag = 1;
        parse_RadiometryGlobalParameterOptions(p_RadiometryGlobalParameterOptions, &p_res->RadiometryGlobalParameterOptions);
    }

    return TRUE;
}

BOOL parse_tth_GetServiceCapabilities(XMLN * p_node, tth_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ThermalServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tth_GetConfigurations(XMLN * p_node, tth_GetConfigurations_RES * p_res)
{
    XMLN * p_Configurations;

    p_Configurations = xml_node_soap_get(p_node, "Configurations");
    while (p_Configurations && soap_strcmp(p_Configurations->name, "Configurations") == 0)
    {
        const char * p_token;
        XMLN * p_Configuration;

        ThermalConfigurationList * p_info = onvif_add_ThermalConfiguration(&p_res->Configurations);
        if (p_info)
        {
            p_token = xml_attr_get(p_Configurations, "token");
            if (p_token)
            {
                strncpy(p_info->token, p_token, sizeof(p_info->token)-1);
            }

            p_Configuration = xml_node_soap_get(p_Configurations, "Configuration");
            if (p_Configuration)
            {
                parse_ThermalConfiguration(p_Configuration, &p_info->Configuration);
            }
        }

        p_Configurations = p_Configurations->next;
    }

    return TRUE;
}

BOOL parse_tth_GetConfiguration(XMLN * p_node, tth_GetConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_ThermalConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_tth_GetConfigurationOptions(XMLN * p_node, tth_GetConfigurationOptions_RES * p_res)
{
    XMLN * p_ConfigurationOptions;

    p_ConfigurationOptions = xml_node_soap_get(p_node, "ConfigurationOptions");
    if (p_ConfigurationOptions)
    {
        parse_ThermalConfigurationOptions(p_ConfigurationOptions, &p_res->Options);
    }

    return TRUE;
}

BOOL parse_tth_GetRadiometryConfiguration(XMLN * p_node, tth_GetRadiometryConfiguration_RES * p_res)
{
    XMLN * p_Configuration;

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_RadiometryConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_tth_GetRadiometryConfigurationOptions(XMLN * p_node, tth_GetRadiometryConfigurationOptions_RES * p_res)
{
    XMLN * p_ConfigurationOptions;

    p_ConfigurationOptions = xml_node_soap_get(p_node, "ConfigurationOptions");
    if (p_ConfigurationOptions)
    {
        parse_RadiometryConfigurationOptions(p_ConfigurationOptions, &p_res->Options);
    }

    return TRUE;
}

#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

BOOL parse_CredentialCapabilitiesExtension(XMLN * p_node, onvif_CredentialCapabilitiesExtension * p_res)
{
    XMLN * p_SupportedExemptionType;

    p_res->sizeSupportedExemptionType = 0;
    
    p_SupportedExemptionType = xml_node_soap_get(p_node, "SupportedExemptionType");
    while (p_SupportedExemptionType && 
           p_SupportedExemptionType->data && 
           soap_strcmp(p_SupportedExemptionType->name, "SupportedExemptionType") == 0)
    {
        uint32 idx = p_res->sizeSupportedExemptionType;                

        strncpy(p_res->SupportedExemptionType[idx], 
            p_SupportedExemptionType->data, 
            sizeof(p_res->SupportedExemptionType[idx])-1);

        p_res->sizeSupportedExemptionType++;
        if (p_res->sizeSupportedExemptionType >= ARRAY_SIZE(p_res->SupportedExemptionType))
        {
            break;
        }
        
        p_SupportedExemptionType = p_SupportedExemptionType->next;
    }

    return TRUE;
}

BOOL parse_CredentialServiceCapabilities(XMLN * p_node, onvif_CredentialCapabilities * p_res)
{
    const char * p_MaxLimit;
    const char * p_CredentialValiditySupported;
    const char * p_CredentialAccessProfileValiditySupported;
    const char * p_ValiditySupportsTimeValue;
    const char * p_MaxCredentials;
    const char * p_MaxAccessProfilesPerCredential;
    const char * p_ResetAntipassbackSupported;
    const char * p_ClientSuppliedTokenSupported;
    const char * p_DefaultCredentialSuspensionDuration;
    const char * p_MaxWhitelistedItems;
    const char * p_MaxBlacklistedItems;
    XMLN * p_SupportedIdentifierType;
    XMLN * p_Extension;

    p_MaxLimit = xml_attr_get(p_node, "MaxLimit");
    if (p_MaxLimit)
    {
        p_res->MaxLimit = atoi(p_MaxLimit);
    }

    p_CredentialValiditySupported = xml_attr_get(p_node, "CredentialValiditySupported");
    if (p_CredentialValiditySupported)
    {
        p_res->CredentialValiditySupported = parse_Bool(p_CredentialValiditySupported);
    }

    p_CredentialAccessProfileValiditySupported = xml_attr_get(p_node, "CredentialAccessProfileValiditySupported");
    if (p_CredentialAccessProfileValiditySupported)
    {
        p_res->CredentialAccessProfileValiditySupported = parse_Bool(p_CredentialAccessProfileValiditySupported);
    }

    p_ValiditySupportsTimeValue = xml_attr_get(p_node, "ValiditySupportsTimeValue");
    if (p_ValiditySupportsTimeValue)
    {
        p_res->ValiditySupportsTimeValue = parse_Bool(p_ValiditySupportsTimeValue);
    }

    p_MaxCredentials = xml_attr_get(p_node, "MaxCredentials");
    if (p_MaxCredentials)
    {
        p_res->MaxCredentials = atoi(p_MaxCredentials);
    }

    p_MaxAccessProfilesPerCredential = xml_attr_get(p_node, "MaxAccessProfilesPerCredential");
    if (p_MaxAccessProfilesPerCredential)
    {
        p_res->MaxAccessProfilesPerCredential = atoi(p_MaxAccessProfilesPerCredential);
    }

    p_ResetAntipassbackSupported = xml_attr_get(p_node, "ResetAntipassbackSupported");
    if (p_ResetAntipassbackSupported)
    {
        p_res->ResetAntipassbackSupported = parse_Bool(p_ResetAntipassbackSupported);
    }

    p_ClientSuppliedTokenSupported = xml_attr_get(p_node, "ClientSuppliedTokenSupported");
    if (p_ClientSuppliedTokenSupported)
    {
        p_res->ClientSuppliedTokenSupported = parse_Bool(p_ClientSuppliedTokenSupported);
    }

    p_DefaultCredentialSuspensionDuration = xml_attr_get(p_node, "DefaultCredentialSuspensionDuration");
    if (p_DefaultCredentialSuspensionDuration)
    {
        strncpy(p_res->DefaultCredentialSuspensionDuration, 
            p_DefaultCredentialSuspensionDuration, 
            sizeof(p_res->DefaultCredentialSuspensionDuration)-1);
    }

    p_MaxWhitelistedItems = xml_attr_get(p_node, "MaxWhitelistedItems");
    if (p_MaxWhitelistedItems)
    {
        p_res->MaxWhitelistedItems = atoi(p_MaxWhitelistedItems);
    }

    p_MaxBlacklistedItems = xml_attr_get(p_node, "MaxBlacklistedItems");
    if (p_MaxBlacklistedItems)
    {
        p_res->MaxBlacklistedItems = atoi(p_MaxBlacklistedItems);
    }

    p_res->sizeSupportedIdentifierType = 0;
    
    p_SupportedIdentifierType = xml_node_soap_get(p_node, "SupportedIdentifierType");
    while (p_SupportedIdentifierType
           && p_SupportedIdentifierType->data 
           && soap_strcmp(p_SupportedIdentifierType->name, "SupportedIdentifierType") == 0)
    {
        uint32 idx = p_res->sizeSupportedIdentifierType;

        strncpy(p_res->SupportedIdentifierType[idx], 
            p_SupportedIdentifierType->data, 
            sizeof(p_res->SupportedIdentifierType[idx])-1);

        p_res->sizeSupportedIdentifierType++;
        if (p_res->sizeSupportedIdentifierType >= ARRAY_SIZE(p_res->SupportedIdentifierType))
        {
            break;
        }
        
        p_SupportedIdentifierType = p_SupportedIdentifierType->next;
    }

    p_Extension = xml_node_soap_get(p_node, "Extension");
    if (p_Extension)
    {
        p_res->ExtensionFlag = parse_CredentialCapabilitiesExtension(p_Extension, &p_res->Extension);
    }

    return TRUE;
}

BOOL parse_CredentialService(XMLN * p_node, onvif_CredentialCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_CredentialServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_CredentialInfo(XMLN * p_node, onvif_CredentialInfo * p_res)
{
    XMLN * p_Description;
    XMLN * p_CredentialHolderReference;
    XMLN * p_ValidFrom;
    XMLN * p_ValidTo;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_CredentialHolderReference = xml_node_soap_get(p_node, "CredentialHolderReference");
    if (p_CredentialHolderReference && p_CredentialHolderReference->data)
    {
        strncpy(p_res->CredentialHolderReference, 
            p_CredentialHolderReference->data, 
            sizeof(p_res->CredentialHolderReference)-1);
    }

    p_ValidFrom = xml_node_soap_get(p_node, "ValidFrom");
    if (p_ValidFrom && p_ValidFrom->data)
    {
        p_res->ValidFromFlag = 1;
        strncpy(p_res->ValidFrom, p_ValidFrom->data, sizeof(p_res->ValidFrom)-1);
    }

    p_ValidTo = xml_node_soap_get(p_node, "ValidTo");
    if (p_ValidTo && p_ValidTo->data)
    {
        p_res->ValidToFlag = 1;
        strncpy(p_res->ValidTo, p_ValidTo->data, sizeof(p_res->ValidTo)-1);
    }

    return TRUE;
}

BOOL parse_CredentialIdentifierType(XMLN * p_node, onvif_CredentialIdentifierType * p_res)
{
    XMLN * p_Name;
    XMLN * p_FormatType;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_FormatType = xml_node_soap_get(p_node, "FormatType");
    if (p_FormatType && p_FormatType->data)
    {
        strncpy(p_res->FormatType, p_FormatType->data, sizeof(p_res->FormatType)-1);
    }

    return TRUE;
}

BOOL parse_CredentialIdentifier(XMLN * p_node, onvif_CredentialIdentifier * p_res)
{
    XMLN * p_Type;
    XMLN * p_ExemptedFromAuthentication;
    XMLN * p_Value;

    p_Type = xml_node_soap_get(p_node, "Type");
    if (p_Type)
    {
        parse_CredentialIdentifierType(p_Type, &p_res->Type);
    }

    p_ExemptedFromAuthentication = xml_node_soap_get(p_node, "ExemptedFromAuthentication");
    if (p_ExemptedFromAuthentication && p_ExemptedFromAuthentication->data)
    {
        p_res->ExemptedFromAuthentication = parse_Bool(p_ExemptedFromAuthentication->data);
    }

    p_Value = xml_node_soap_get(p_node, "Value");
    if (p_Value && p_Value->data)
    {
        strncpy(p_res->Value, p_Value->data, sizeof(p_res->Value)-1);
    }

    return TRUE;
}

BOOL parse_CredentialAccessProfile(XMLN * p_node, onvif_CredentialAccessProfile * p_res)
{
    XMLN * p_AccessProfileToken;
    XMLN * p_ValidFrom;
    XMLN * p_ValidTo;

    p_AccessProfileToken = xml_node_soap_get(p_node, "AccessProfileToken");
    if (p_AccessProfileToken && p_AccessProfileToken->data)
    {
        strncpy(p_res->AccessProfileToken, 
            p_AccessProfileToken->data, 
            sizeof(p_res->AccessProfileToken)-1);
    }

    p_ValidFrom = xml_node_soap_get(p_node, "ValidFrom");
    if (p_ValidFrom && p_ValidFrom->data)
    {
        p_res->ValidFromFlag = 1;
        strncpy(p_res->ValidFrom, p_ValidFrom->data, sizeof(p_res->ValidFrom)-1);
    }

    p_ValidTo = xml_node_soap_get(p_node, "ValidTo");
    if (p_ValidTo && p_ValidTo->data)
    {
        p_res->ValidToFlag = 1;
        strncpy(p_res->ValidTo, p_ValidTo->data, sizeof(p_res->ValidFrom)-1);
    }

    return TRUE;
}

BOOL parse_Attribute(XMLN * p_node, onvif_Attribute * p_res)
{
    const char * p_Name;
    const char * p_Value;

    p_Name = xml_attr_get(p_node, "Name");
    if (p_Name)
    {
        strncpy(p_res->Name, p_Name, sizeof(p_res->Name)-1);
    }

    p_Value = xml_attr_get(p_node, "Value");
    if (p_Value)
    {
        p_res->ValueFlag = 1;
        strncpy(p_res->Value, p_Value, sizeof(p_res->Value)-1);
    }

    return TRUE;
}

BOOL parse_Credential(XMLN * p_node, onvif_Credential * p_res)
{
    XMLN * p_Description;
    XMLN * p_CredentialHolderReference;
    XMLN * p_ValidFrom;
    XMLN * p_ValidTo;
    XMLN * p_CredentialIdentifier;
    XMLN * p_CredentialAccessProfile;
    XMLN * p_ExtendedGrantTime;
    XMLN * p_Attribute;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
        
    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_CredentialHolderReference = xml_node_soap_get(p_node, "CredentialHolderReference");
    if (p_CredentialHolderReference && p_CredentialHolderReference->data)
    {
        strncpy(p_res->CredentialHolderReference, p_CredentialHolderReference->data, sizeof(p_res->CredentialHolderReference)-1);
    }

    p_ValidFrom = xml_node_soap_get(p_node, "ValidFrom");
    if (p_ValidFrom && p_ValidFrom->data)
    {
        p_res->ValidFromFlag = 1;
        strncpy(p_res->ValidFrom, p_ValidFrom->data, sizeof(p_res->ValidFrom)-1);
    }

    p_ValidTo = xml_node_soap_get(p_node, "ValidTo");
    if (p_ValidTo && p_ValidTo->data)
    {
        p_res->ValidToFlag = 1;
        strncpy(p_res->ValidTo, p_ValidTo->data, sizeof(p_res->ValidTo)-1);
    }

    p_res->sizeCredentialIdentifier = 0;
    
    p_CredentialIdentifier = xml_node_soap_get(p_node, "CredentialIdentifier");
    while (p_CredentialIdentifier && soap_strcmp(p_CredentialIdentifier->name, "CredentialIdentifier") == 0)
    {
        uint32 idx = p_res->sizeCredentialIdentifier;

        p_res->CredentialIdentifier[idx].Used = parse_CredentialIdentifier(p_CredentialIdentifier, &p_res->CredentialIdentifier[idx]);

        p_res->sizeCredentialIdentifier++;
        if (p_res->sizeCredentialIdentifier >= ARRAY_SIZE(p_res->CredentialIdentifier))
        {
            break;
        }

        p_CredentialIdentifier = p_CredentialIdentifier->next;
    }

    p_res->sizeCredentialAccessProfile = 0;
    
    p_CredentialAccessProfile = xml_node_soap_get(p_node, "CredentialAccessProfile");
    while (p_CredentialAccessProfile && soap_strcmp(p_CredentialAccessProfile->name, "CredentialAccessProfile") == 0)
    {
        uint32 idx = p_res->sizeCredentialAccessProfile;

        p_res->CredentialAccessProfile[idx].Used = parse_CredentialAccessProfile(p_CredentialAccessProfile, &p_res->CredentialAccessProfile[idx]);

        p_res->sizeCredentialAccessProfile++;
        if (p_res->sizeCredentialAccessProfile >= ARRAY_SIZE(p_res->CredentialAccessProfile))
        {
            break;
        }

        p_CredentialAccessProfile = p_CredentialAccessProfile->next;
    }

    p_ExtendedGrantTime = xml_node_soap_get(p_node, "ExtendedGrantTime");
    if (p_ExtendedGrantTime && p_ExtendedGrantTime->data)
    {
        p_res->ExtendedGrantTime = parse_Bool(p_ExtendedGrantTime->data);
    }

    p_res->sizeAttribute = 0;
    
    p_Attribute = xml_node_soap_get(p_node, "Attribute");
    while (p_Attribute && soap_strcmp(p_Attribute->name, "Attribute") == 0)
    {
        uint32 idx = p_res->sizeAttribute;

        p_res->Attribute[idx].Used = parse_Attribute(p_Attribute, &p_res->Attribute[idx]);

        p_res->sizeAttribute++;
        if (p_res->sizeAttribute >= ARRAY_SIZE(p_res->Attribute))
        {
            break;
        }

        p_Attribute = p_Attribute->next;
    }

    return TRUE;
}

BOOL parse_AntipassbackState(XMLN * p_node, onvif_AntipassbackState * p_res)
{
    XMLN * p_AntipassbackViolated;

    p_AntipassbackViolated = xml_node_soap_get(p_node, "AntipassbackViolated");
    if (p_AntipassbackViolated && p_AntipassbackViolated->data)
    {
        p_res->AntipassbackViolated = parse_Bool(p_AntipassbackViolated->data);
    }

    return TRUE;
}

BOOL parse_CredentialState(XMLN * p_node, onvif_CredentialState * p_res)
{
    XMLN * p_Enabled;
    XMLN * p_Reason;
    XMLN * p_AntipassbackState;

    p_Enabled = xml_node_soap_get(p_node, "Enabled");
    if (p_Enabled && p_Enabled->data)
    {
        p_res->Enabled = parse_Bool(p_Enabled->data);
    }

    p_Reason = xml_node_soap_get(p_node, "Reason");
    if (p_Reason && p_Reason->data)
    {
        p_res->ReasonFlag = 1;
        strncpy(p_res->Reason, p_Reason->data, sizeof(p_res->Reason)-1);
    }

    p_AntipassbackState = xml_node_soap_get(p_node, "AntipassbackState");
    if (p_AntipassbackState)
    {
        p_res->AntipassbackStateFlag = parse_AntipassbackState(p_AntipassbackState, &p_res->AntipassbackState);
    }

    return TRUE;
}

BOOL parse_CredentialIdentifierFormatTypeInfo(XMLN * p_node, onvif_CredentialIdentifierFormatTypeInfo * p_res)
{
    XMLN * p_FormatType;
    XMLN * p_Description;

    p_FormatType = xml_node_soap_get(p_node, "FormatType");
    if (p_FormatType && p_FormatType->data)
    {
        strncpy(p_res->FormatType, p_FormatType->data, sizeof(p_res->FormatType)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_tcr_GetServiceCapabilities(XMLN * p_node, tcr_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_CredentialServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tcr_GetCredentialInfo(XMLN * p_node, tcr_GetCredentialInfo_RES * p_res)
{
    XMLN * p_CredentialInfo;

    p_res->sizeCredentialInfo = 0;
    
    p_CredentialInfo = xml_node_soap_get(p_node, "CredentialInfo");
    while (p_CredentialInfo && soap_strcmp(p_CredentialInfo->name, "CredentialInfo") == 0)
    {
        uint32 idx = p_res->sizeCredentialInfo;

        parse_CredentialInfo(p_CredentialInfo, &p_res->CredentialInfo[idx]);
        
        p_res->sizeCredentialInfo++;
        if (p_res->sizeCredentialInfo >= ARRAY_SIZE(p_res->CredentialInfo))
        {
            break;
        }

        p_CredentialInfo = p_CredentialInfo->next;
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentialInfoList(XMLN * p_node, tcr_GetCredentialInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_CredentialInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeCredentialInfo = 0;
    
    p_CredentialInfo = xml_node_soap_get(p_node, "CredentialInfo");
    while (p_CredentialInfo && soap_strcmp(p_CredentialInfo->name, "CredentialInfo") == 0)
    {
        uint32 idx = p_res->sizeCredentialInfo;

        parse_CredentialInfo(p_CredentialInfo, &p_res->CredentialInfo[idx]);
        
        p_res->sizeCredentialInfo++;
        if (p_res->sizeCredentialInfo >= ARRAY_SIZE(p_res->CredentialInfo))
        {
            break;
        }

        p_CredentialInfo = p_CredentialInfo->next;
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentials(XMLN * p_node, tcr_GetCredentials_RES * p_res)
{
    XMLN * p_Credential;

    p_res->sizeCredential = 0;
    
    p_Credential = xml_node_soap_get(p_node, "Credential");
    while (p_Credential && soap_strcmp(p_Credential->name, "Credential") == 0)
    {
        uint32 idx = p_res->sizeCredential;
        
        parse_Credential(p_Credential, &p_res->Credential[idx]);
        
        p_res->sizeCredential++;
        if (p_res->sizeCredential >= ARRAY_SIZE(p_res->Credential))
        {
            break;
        }

        p_Credential = p_Credential->next;
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentialList(XMLN * p_node, tcr_GetCredentialList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_Credential;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeCredential = 0;
    
    p_Credential = xml_node_soap_get(p_node, "Credential");
    while (p_Credential && soap_strcmp(p_Credential->name, "Credential") == 0)
    {
        uint32 idx = p_res->sizeCredential;

        parse_Credential(p_Credential, &p_res->Credential[idx]);
        
        p_res->sizeCredential++;
        if (p_res->sizeCredential >= ARRAY_SIZE(p_res->Credential))
        {
            break;
        }

        p_Credential = p_Credential->next;
    }

    return TRUE;
}

BOOL parse_tcr_CreateCredential(XMLN * p_node, tcr_CreateCredential_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentialState(XMLN * p_node, tcr_GetCredentialState_RES * p_res)
{
    XMLN * p_State;

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State)
    {
        parse_CredentialState(p_State, &p_res->State);
    }

    return TRUE;
}

BOOL parse_tcr_GetSupportedFormatTypes(XMLN * p_node, tcr_GetSupportedFormatTypes_RES * p_res)
{
    XMLN * p_FormatTypeInfo;

    p_res->sizeFormatTypeInfo = 0;
    
    p_FormatTypeInfo = xml_node_soap_get(p_node, "FormatTypeInfo");
    while (p_FormatTypeInfo && soap_strcmp(p_FormatTypeInfo->name, "FormatTypeInfo") == 0)
    {
        uint32 idx = p_res->sizeFormatTypeInfo;

        parse_CredentialIdentifierFormatTypeInfo(p_FormatTypeInfo, &p_res->FormatTypeInfo[idx]);
        
        p_res->sizeFormatTypeInfo++;
        if (p_res->sizeFormatTypeInfo >= ARRAY_SIZE(p_res->FormatTypeInfo))
        {
            break;
        }

        p_FormatTypeInfo = p_FormatTypeInfo->next;
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentialIdentifiers(XMLN * p_node, tcr_GetCredentialIdentifiers_RES * p_res)
{
    XMLN * p_CredentialIdentifier;

    p_res->sizeCredentialIdentifier = 0;
    
    p_CredentialIdentifier = xml_node_soap_get(p_node, "CredentialIdentifier");
    while (p_CredentialIdentifier && soap_strcmp(p_CredentialIdentifier->name, "CredentialIdentifier") == 0)
    {
        uint32 idx = p_res->sizeCredentialIdentifier;

        parse_CredentialIdentifier(p_CredentialIdentifier, &p_res->CredentialIdentifier[idx]);
        
        p_res->sizeCredentialIdentifier++;
        if (p_res->sizeCredentialIdentifier >= ARRAY_SIZE(p_res->CredentialIdentifier))
        {
            break;
        }

        p_CredentialIdentifier = p_CredentialIdentifier->next;
    }

    return TRUE;
}

BOOL parse_tcr_GetCredentialAccessProfiles(XMLN * p_node, tcr_GetCredentialAccessProfiles_RES * p_res)
{
    XMLN * p_CredentialAccessProfile;

    p_res->sizeCredentialAccessProfile = 0;
    
    p_CredentialAccessProfile = xml_node_soap_get(p_node, "CredentialAccessProfile");
    while (p_CredentialAccessProfile && soap_strcmp(p_CredentialAccessProfile->name, "CredentialAccessProfile") == 0)
    {
        uint32 idx = p_res->sizeCredentialAccessProfile;

        parse_CredentialAccessProfile(p_CredentialAccessProfile, &p_res->CredentialAccessProfile[idx]);
        
        p_res->sizeCredentialAccessProfile++;
        if (p_res->sizeCredentialAccessProfile >= ARRAY_SIZE(p_res->CredentialAccessProfile))
        {
            break;
        }

        p_CredentialAccessProfile = p_CredentialAccessProfile->next;
    }

    return TRUE;
}

#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

BOOL parse_AccessRulesServiceCapabilities(XMLN * p_node, onvif_AccessRulesCapabilities * p_res)
{
    const char * p_MaxLimit;
    const char * p_MaxAccessProfiles;
    const char * p_MaxAccessPoliciesPerAccessProfile;
    const char * p_MultipleSchedulesPerAccessPointSupported;
    const char * p_ClientSuppliedTokenSupported;

    p_MaxLimit = xml_attr_get(p_node, "MaxLimit");
    if (p_MaxLimit)
    {
        p_res->MaxLimit = atoi(p_MaxLimit);
    }

    p_MaxAccessProfiles = xml_attr_get(p_node, "MaxAccessProfiles");
    if (p_MaxAccessProfiles)
    {
        p_res->MaxAccessProfiles = atoi(p_MaxAccessProfiles);
    }

    p_MaxAccessPoliciesPerAccessProfile = xml_attr_get(p_node, "MaxAccessPoliciesPerAccessProfile");
    if (p_MaxAccessPoliciesPerAccessProfile)
    {
        p_res->MaxAccessPoliciesPerAccessProfile = atoi(p_MaxAccessPoliciesPerAccessProfile);
    }

    p_MultipleSchedulesPerAccessPointSupported = xml_attr_get(p_node, "MultipleSchedulesPerAccessPointSupported");
    if (p_MultipleSchedulesPerAccessPointSupported)
    {
        p_res->MultipleSchedulesPerAccessPointSupported = parse_Bool(p_MultipleSchedulesPerAccessPointSupported);
    }

    p_ClientSuppliedTokenSupported = xml_attr_get(p_node, "ClientSuppliedTokenSupported");
    if (p_ClientSuppliedTokenSupported)
    {
        p_res->ClientSuppliedTokenSupported = parse_Bool(p_ClientSuppliedTokenSupported);
    }

    return TRUE;
}

BOOL parse_AccessRulesService(XMLN * p_node, onvif_AccessRulesCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_AccessRulesServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_AccessProfileInfo(XMLN * p_node, onvif_AccessProfileInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    const char * p_token;
    
    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }
    
    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_AccessPolicy(XMLN * p_node, onvif_AccessPolicy * p_res)
{
    XMLN * p_ScheduleToken;
    XMLN * p_Entity;    
    XMLN * p_EntityType;

    p_ScheduleToken = xml_node_soap_get(p_node, "ScheduleToken");
    if (p_ScheduleToken && p_ScheduleToken->data)
    {
        strncpy(p_res->ScheduleToken, p_ScheduleToken->data, sizeof(p_res->ScheduleToken)-1);
    }

    p_Entity = xml_node_soap_get(p_node, "Entity");
    if (p_Entity && p_Entity->data)
    {
        strncpy(p_res->Entity, p_Entity->data, sizeof(p_res->Entity)-1);
    }

    p_EntityType = xml_node_soap_get(p_node, "EntityType");
    if (p_EntityType && p_EntityType->data)
    {
        p_res->EntityTypeFlag = 1;
        strncpy(p_res->EntityType, p_EntityType->data, sizeof(p_res->EntityType)-1);
    }
    
    return TRUE;
}

BOOL parse_AccessProfile(XMLN * p_node, onvif_AccessProfile * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    XMLN * p_AccessPolicy;
    const char * p_token;

    p_token = xml_attr_get(p_node, "token");
    if (p_token)
    {
        strncpy(p_res->token, p_token, sizeof(p_res->token)-1);
    }
    
    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_res->sizeAccessPolicy = 0;
    
    p_AccessPolicy = xml_node_soap_get(p_node, "AccessPolicy");
    while (p_AccessPolicy && soap_strcmp(p_AccessPolicy->name, "AccessPolicy") == 0)
    {
        uint32 idx = p_res->sizeAccessPolicy;

        parse_AccessPolicy(p_AccessPolicy, &p_res->AccessPolicy[idx]);

        p_res->sizeAccessPolicy++;
        if (p_res->sizeAccessPolicy >= ARRAY_SIZE(p_res->AccessPolicy))
        {
            break;
        }

        p_AccessPolicy = p_AccessPolicy->next;
    }

    return TRUE;
}

BOOL parse_tar_GetServiceCapabilities(XMLN * p_node, tar_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_AccessRulesServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tar_GetAccessProfileInfo(XMLN * p_node, tar_GetAccessProfileInfo_RES * p_res)
{
    XMLN * p_AccessProfileInfo;

    p_res->sizeAccessProfileInfo = 0;
    
    p_AccessProfileInfo = xml_node_soap_get(p_node, "AccessProfileInfo");
    while (p_AccessProfileInfo && soap_strcmp(p_AccessProfileInfo->name, "AccessProfileInfo") == 0)
    {
        uint32 idx = p_res->sizeAccessProfileInfo;

        parse_AccessProfileInfo(p_AccessProfileInfo, &p_res->AccessProfileInfo[idx]);
        
        p_res->sizeAccessProfileInfo++;
        if (p_res->sizeAccessProfileInfo >= ARRAY_SIZE(p_res->AccessProfileInfo))
        {
            break;
        }

        p_AccessProfileInfo = p_AccessProfileInfo->next;
    }

    return TRUE;
}

BOOL parse_tar_GetAccessProfileInfoList(XMLN * p_node, tar_GetAccessProfileInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_AccessProfileInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeAccessProfileInfo = 0;
    
    p_AccessProfileInfo = xml_node_soap_get(p_node, "AccessProfileInfo");
    while (p_AccessProfileInfo && soap_strcmp(p_AccessProfileInfo->name, "AccessProfileInfo") == 0)
    {
        uint32 idx = p_res->sizeAccessProfileInfo;

        parse_AccessProfileInfo(p_AccessProfileInfo, &p_res->AccessProfileInfo[idx]);
        
        p_res->sizeAccessProfileInfo++;
        if (p_res->sizeAccessProfileInfo >= ARRAY_SIZE(p_res->AccessProfileInfo))
        {
            break;
        }

        p_AccessProfileInfo = p_AccessProfileInfo->next;
    }

    return TRUE;
}

BOOL parse_tar_GetAccessProfiles(XMLN * p_node, tar_GetAccessProfiles_RES * p_res)
{
    XMLN * p_AccessProfile;

    p_res->sizeAccessProfile = 0;
    
    p_AccessProfile = xml_node_soap_get(p_node, "AccessProfile");
    while (p_AccessProfile && soap_strcmp(p_AccessProfile->name, "AccessProfile") == 0)
    {
        uint32 idx = p_res->sizeAccessProfile;
        
        parse_AccessProfile(p_AccessProfile, &p_res->AccessProfile[idx]);
        
        p_res->sizeAccessProfile++;
        if (p_res->sizeAccessProfile >= ARRAY_SIZE(p_res->AccessProfile))
        {
            break;
        }

        p_AccessProfile = p_AccessProfile->next;
    }

    return TRUE;
}

BOOL parse_tar_GetAccessProfileList(XMLN * p_node, tar_GetAccessProfileList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_AccessProfile;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeAccessProfile = 0;
    
    p_AccessProfile = xml_node_soap_get(p_node, "AccessProfile");
    while (p_AccessProfile && soap_strcmp(p_AccessProfile->name, "AccessProfile") == 0)
    {
        uint32 idx = p_res->sizeAccessProfile;

        parse_AccessProfile(p_AccessProfile, &p_res->AccessProfile[idx]);
        
        p_res->sizeAccessProfile++;
        if (p_res->sizeAccessProfile >= ARRAY_SIZE(p_res->AccessProfile))
        {
            break;
        }

        p_AccessProfile = p_AccessProfile->next;
    }

    return TRUE;
}

BOOL parse_tar_CreateAccessProfile(XMLN * p_node, tar_CreateAccessProfile_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

BOOL parse_ScheduleServiceCapabilities(XMLN * p_node, onvif_ScheduleCapabilities * p_res)
{
    const char * p_MaxLimit;
    const char * p_MaxSchedules;
    const char * p_MaxTimePeriodsPerDay;
    const char * p_MaxSpecialDayGroups;
    const char * p_MaxDaysInSpecialDayGroup;
    const char * p_MaxSpecialDaysSchedules;        
    const char * p_ExtendedRecurrenceSupported;
    const char * p_SpecialDaysSupported;
    const char * p_StateReportingSupported;
    const char * p_ClientSuppliedTokenSupported;

    p_MaxLimit = xml_attr_get(p_node, "MaxLimit");
    if (p_MaxLimit)
    {
        p_res->MaxLimit = atoi(p_MaxLimit);
    }

    p_MaxSchedules = xml_attr_get(p_node, "MaxSchedules");
    if (p_MaxSchedules)
    {
        p_res->MaxSchedules = atoi(p_MaxSchedules);
    }

    p_MaxTimePeriodsPerDay = xml_attr_get(p_node, "MaxTimePeriodsPerDay");
    if (p_MaxTimePeriodsPerDay)
    {
        p_res->MaxTimePeriodsPerDay = atoi(p_MaxTimePeriodsPerDay);
    }

    p_MaxSpecialDayGroups = xml_attr_get(p_node, "MaxSpecialDayGroups");
    if (p_MaxSpecialDayGroups)
    {
        p_res->MaxSpecialDayGroups = atoi(p_MaxSpecialDayGroups);
    }

    p_MaxDaysInSpecialDayGroup = xml_attr_get(p_node, "MaxDaysInSpecialDayGroup");
    if (p_MaxDaysInSpecialDayGroup)
    {
        p_res->MaxDaysInSpecialDayGroup = atoi(p_MaxDaysInSpecialDayGroup);
    }

    p_MaxSpecialDaysSchedules = xml_attr_get(p_node, "MaxSpecialDaysSchedules");
    if (p_MaxSpecialDaysSchedules)
    {
        p_res->MaxSpecialDaysSchedules = atoi(p_MaxSpecialDaysSchedules);
    }

    p_ExtendedRecurrenceSupported = xml_attr_get(p_node, "ExtendedRecurrenceSupported");
    if (p_ExtendedRecurrenceSupported)
    {
        p_res->ExtendedRecurrenceSupported = parse_Bool(p_ExtendedRecurrenceSupported);
    }

    p_SpecialDaysSupported = xml_attr_get(p_node, "SpecialDaysSupported");
    if (p_SpecialDaysSupported)
    {
        p_res->SpecialDaysSupported = parse_Bool(p_SpecialDaysSupported);
    }

    p_StateReportingSupported = xml_attr_get(p_node, "StateReportingSupported");
    if (p_StateReportingSupported)
    {
        p_res->StateReportingSupported = parse_Bool(p_StateReportingSupported);
    }

    p_ClientSuppliedTokenSupported = xml_attr_get(p_node, "ClientSuppliedTokenSupported");
    if (p_ClientSuppliedTokenSupported)
    {
        p_res->ClientSuppliedTokenSupported = parse_Bool(p_ClientSuppliedTokenSupported);
    }

    return TRUE;
}

BOOL parse_ScheduleService(XMLN * p_node, onvif_ScheduleCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ScheduleServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_ScheduleInfo(XMLN * p_node, onvif_ScheduleInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_TimePeriod(XMLN * p_node, onvif_TimePeriod * p_res)
{
    XMLN * p_From;
    XMLN * p_Until;

    p_From = xml_node_soap_get(p_node, "From");
    if (p_From && p_From->data)
    {
        strncpy(p_res->From, p_From->data, sizeof(p_res->From)-1);
    }

    p_Until = xml_node_soap_get(p_node, "Until");
    if (p_Until && p_Until->data)
    {
        p_res->UntilFlag = 1;
        strncpy(p_res->Until, p_Until->data, sizeof(p_res->Until)-1);
    }

    return TRUE;
}

BOOL parse_SpecialDaysSchedule(XMLN * p_node, onvif_SpecialDaysSchedule * p_res)
{
    XMLN * p_GroupToken;
    XMLN * p_TimeRange;

    p_GroupToken = xml_node_soap_get(p_node, "GroupToken");
    if (p_GroupToken && p_GroupToken->data)
    {
        strncpy(p_res->GroupToken, p_GroupToken->data, sizeof(p_res->GroupToken)-1);
    }

    p_res->sizeTimeRange = 0;
    
    p_TimeRange = xml_node_soap_get(p_node, "TimeRange");
    while (p_TimeRange && soap_strcmp(p_TimeRange->name, "TimeRange") == 0)
    {
        uint32 idx = p_res->sizeTimeRange;

        parse_TimePeriod(p_TimeRange, &p_res->TimeRange[idx]);

        p_res->sizeTimeRange++;
        if (p_res->sizeTimeRange >= ARRAY_SIZE(p_res->TimeRange))
        {
            break;
        }
        
        p_TimeRange = p_TimeRange->next;
    }

    return TRUE;
}

BOOL parse_Schedule(XMLN * p_node, onvif_Schedule * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    XMLN * p_Standard;
    XMLN * p_SpecialDays;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_Standard = xml_node_soap_get(p_node, "Standard");
    if (p_Standard && p_Standard->data)
    {
        strncpy(p_res->Standard, p_Standard->data, sizeof(p_res->Standard)-1);
    }

    p_res->sizeSpecialDays = 0;
    
    p_SpecialDays = xml_node_soap_get(p_node, "SpecialDays");
    while (p_SpecialDays && soap_strcmp(p_SpecialDays->name, "SpecialDays") == 0)
    {
        uint32 idx = p_res->sizeSpecialDays;

        parse_SpecialDaysSchedule(p_SpecialDays, &p_res->SpecialDays[idx]);

        p_res->sizeSpecialDays++;
        if (p_res->sizeSpecialDays >= ARRAY_SIZE(p_res->SpecialDays))
        {
            break;
        }
        
        p_SpecialDays = p_SpecialDays->next;
    }

    return TRUE;
}

BOOL parse_SpecialDayGroupInfo(XMLN * p_node, onvif_SpecialDayGroupInfo * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    return TRUE;
}

BOOL parse_SpecialDayGroup(XMLN * p_node, onvif_SpecialDayGroup * p_res)
{
    XMLN * p_Name;
    XMLN * p_Description;
    XMLN * p_Days;

    p_Name = xml_node_soap_get(p_node, "Name");
    if (p_Name && p_Name->data)
    {
        strncpy(p_res->Name, p_Name->data, sizeof(p_res->Name)-1);
    }

    p_Description = xml_node_soap_get(p_node, "Description");
    if (p_Description && p_Description->data)
    {
        p_res->DescriptionFlag = 1;
        strncpy(p_res->Description, p_Description->data, sizeof(p_res->Description)-1);
    }

    p_Days = xml_node_soap_get(p_node, "Days");
    if (p_Days && p_Days->data)
    {
        p_res->DaysFlag = 1;
        strncpy(p_res->Days, p_Days->data, sizeof(p_res->Days)-1);
    }

    return TRUE;
}

BOOL parse_ScheduleState(XMLN * p_node, onvif_ScheduleState * p_res)
{
    XMLN * p_Active;
    XMLN * p_SpecialDay;

    p_Active = xml_node_soap_get(p_node, "Active");
    if (p_Active && p_Active->data)
    {
        p_res->Active = parse_Bool(p_Active->data);
    }

    p_SpecialDay = xml_node_soap_get(p_node, "SpecialDay");
    if (p_SpecialDay && p_SpecialDay->data)
    {
        p_res->SpecialDayFlag = 1;
        p_res->SpecialDay = parse_Bool(p_SpecialDay->data);
    }

    return TRUE;
}

BOOL parse_tsc_GetServiceCapabilities(XMLN * p_node, tsc_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ScheduleServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tsc_GetScheduleInfo(XMLN * p_node, tsc_GetScheduleInfo_RES * p_res)
{
    XMLN * p_ScheduleInfo;

    p_res->sizeScheduleInfo = 0;
    
    p_ScheduleInfo = xml_node_soap_get(p_node, "ScheduleInfo");
    while (p_ScheduleInfo && soap_strcmp(p_ScheduleInfo->name, "ScheduleInfo") == 0)
    {
        uint32 idx = p_res->sizeScheduleInfo;

        parse_ScheduleInfo(p_ScheduleInfo, &p_res->ScheduleInfo[idx]);
        
        p_res->sizeScheduleInfo++;
        if (p_res->sizeScheduleInfo >= ARRAY_SIZE(p_res->ScheduleInfo))
        {
            break;
        }

        p_ScheduleInfo = p_ScheduleInfo->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetScheduleInfoList(XMLN * p_node, tsc_GetScheduleInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_ScheduleInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeScheduleInfo = 0;
    
    p_ScheduleInfo = xml_node_soap_get(p_node, "ScheduleInfo");
    while (p_ScheduleInfo && soap_strcmp(p_ScheduleInfo->name, "ScheduleInfo") == 0)
    {
        uint32 idx = p_res->sizeScheduleInfo;

        parse_ScheduleInfo(p_ScheduleInfo, &p_res->ScheduleInfo[idx]);
        
        p_res->sizeScheduleInfo++;
        if (p_res->sizeScheduleInfo >= ARRAY_SIZE(p_res->ScheduleInfo))
        {
            break;
        }

        p_ScheduleInfo = p_ScheduleInfo->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetSchedules(XMLN * p_node, tsc_GetSchedules_RES * p_res)
{
    XMLN * p_Schedule;

    p_res->sizeSchedule = 0;
    
    p_Schedule = xml_node_soap_get(p_node, "Schedule");
    while (p_Schedule && soap_strcmp(p_Schedule->name, "Schedule") == 0)
    {
        uint32 idx = p_res->sizeSchedule;

        parse_Schedule(p_Schedule, &p_res->Schedule[idx]);
        
        p_res->sizeSchedule++;
        if (p_res->sizeSchedule >= ARRAY_SIZE(p_res->Schedule))
        {
            break;
        }

        p_Schedule = p_Schedule->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetScheduleList(XMLN * p_node, tsc_GetScheduleList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_Schedule;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeSchedule = 0;
    
    p_Schedule = xml_node_soap_get(p_node, "Schedule");
    while (p_Schedule && soap_strcmp(p_Schedule->name, "Schedule") == 0)
    {
        uint32 idx = p_res->sizeSchedule;

        parse_Schedule(p_Schedule, &p_res->Schedule[idx]);
        
        p_res->sizeSchedule++;
        if (p_res->sizeSchedule >= ARRAY_SIZE(p_res->Schedule))
        {
            break;
        }

        p_Schedule = p_Schedule->next;
    }

    return TRUE;
}

BOOL parse_tsc_CreateSchedule(XMLN * p_node, tsc_CreateSchedule_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tsc_GetSpecialDayGroupInfo(XMLN * p_node, tsc_GetSpecialDayGroupInfo_RES * p_res)
{
    XMLN * p_SpecialDayGroupInfo;

    p_res->sizeSpecialDayGroupInfo = 0;
    
    p_SpecialDayGroupInfo = xml_node_soap_get(p_node, "SpecialDayGroupInfo");
    while (p_SpecialDayGroupInfo && soap_strcmp(p_SpecialDayGroupInfo->name, "SpecialDayGroupInfo") == 0)
    {
        uint32 idx = p_res->sizeSpecialDayGroupInfo;

        parse_SpecialDayGroupInfo(p_SpecialDayGroupInfo, &p_res->SpecialDayGroupInfo[idx]);
        
        p_res->sizeSpecialDayGroupInfo++;
        if (p_res->sizeSpecialDayGroupInfo >= ARRAY_SIZE(p_res->SpecialDayGroupInfo))
        {
            break;
        }

        p_SpecialDayGroupInfo = p_SpecialDayGroupInfo->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetSpecialDayGroupInfoList(XMLN * p_node, tsc_GetSpecialDayGroupInfoList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_SpecialDayGroupInfo;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeSpecialDayGroupInfo = 0;
    
    p_SpecialDayGroupInfo = xml_node_soap_get(p_node, "SpecialDayGroupInfo");
    while (p_SpecialDayGroupInfo && soap_strcmp(p_SpecialDayGroupInfo->name, "SpecialDayGroupInfo") == 0)
    {
        uint32 idx = p_res->sizeSpecialDayGroupInfo;

        parse_SpecialDayGroupInfo(p_SpecialDayGroupInfo, &p_res->SpecialDayGroupInfo[idx]);
        
        p_res->sizeSpecialDayGroupInfo++;
        if (p_res->sizeSpecialDayGroupInfo >= ARRAY_SIZE(p_res->SpecialDayGroupInfo))
        {
            break;
        }

        p_SpecialDayGroupInfo = p_SpecialDayGroupInfo->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetSpecialDayGroups(XMLN * p_node, tsc_GetSpecialDayGroups_RES * p_res)
{
    XMLN * p_SpecialDayGroup;

    p_res->sizeSpecialDayGroup = 0;
    
    p_SpecialDayGroup = xml_node_soap_get(p_node, "SpecialDayGroup");
    while (p_SpecialDayGroup && soap_strcmp(p_SpecialDayGroup->name, "SpecialDayGroup") == 0)
    {
        uint32 idx = p_res->sizeSpecialDayGroup;

        parse_SpecialDayGroup(p_SpecialDayGroup, &p_res->SpecialDayGroup[idx]);
        
        p_res->sizeSpecialDayGroup++;
        if (p_res->sizeSpecialDayGroup >= ARRAY_SIZE(p_res->SpecialDayGroup))
        {
            break;
        }

        p_SpecialDayGroup = p_SpecialDayGroup->next;
    }

    return TRUE;
}

BOOL parse_tsc_GetSpecialDayGroupList(XMLN * p_node, tsc_GetSpecialDayGroupList_RES * p_res)
{
    XMLN * p_NextStartReference;
    XMLN * p_SpecialDayGroup;

    p_NextStartReference = xml_node_soap_get(p_node, "NextStartReference");
    if (p_NextStartReference && p_NextStartReference->data)
    {
        p_res->NextStartReferenceFlag = 1;
        strncpy(p_res->NextStartReference, p_NextStartReference->data, sizeof(p_res->NextStartReference)-1);
    }

    p_res->sizeSpecialDayGroup = 0;
    
    p_SpecialDayGroup = xml_node_soap_get(p_node, "SpecialDayGroup");
    while (p_SpecialDayGroup && soap_strcmp(p_SpecialDayGroup->name, "SpecialDayGroup") == 0)
    {
        uint32 idx = p_res->sizeSpecialDayGroup;

        parse_SpecialDayGroup(p_SpecialDayGroup, &p_res->SpecialDayGroup[idx]);
        
        p_res->sizeSpecialDayGroup++;
        if (p_res->sizeSpecialDayGroup >= ARRAY_SIZE(p_res->SpecialDayGroup))
        {
            break;
        }

        p_SpecialDayGroup = p_SpecialDayGroup->next;
    }

    return TRUE;
}

BOOL parse_tsc_CreateSpecialDayGroup(XMLN * p_node, tsc_CreateSpecialDayGroup_RES * p_res)
{
    XMLN * p_Token;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    return TRUE;
}

BOOL parse_tsc_GetScheduleState(XMLN * p_node, tsc_GetScheduleState_RES * p_res)
{
    XMLN * p_ScheduleState;

    p_ScheduleState = xml_node_soap_get(p_node, "ScheduleState");
    if (p_ScheduleState)
    {
        parse_ScheduleState(p_ScheduleState, &p_res->ScheduleState);
    }

    return TRUE;
}

#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

BOOL parse_ReceiverServiceCapabilities(XMLN * p_node, onvif_ReceiverCapabilities * p_res)
{
    const char * p_RTP_Multicast;
    const char * p_RTP_TCP;
    const char * p_RTP_RTSP_TCP;
    const char * p_SupportedReceivers;
    const char * p_MaximumRTSPURILength;

    p_RTP_Multicast = xml_attr_get(p_node, "RTP_Multicast");
    if (p_RTP_Multicast)
    {
        p_res->RTP_USCOREMulticast = parse_Bool(p_RTP_Multicast);
    }

    p_RTP_TCP = xml_attr_get(p_node, "RTP_TCP");
    if (p_RTP_TCP)
    {
        p_res->RTP_USCORETCP = parse_Bool(p_RTP_TCP);
    }

    p_RTP_RTSP_TCP = xml_attr_get(p_node, "RTP_RTSP_TCP");
    if (p_RTP_RTSP_TCP)
    {
        p_res->RTP_USCORERTSP_USCORETCP = parse_Bool(p_RTP_RTSP_TCP);
    }

    p_SupportedReceivers = xml_attr_get(p_node, "SupportedReceivers");
    if (p_SupportedReceivers)
    {
        p_res->SupportedReceivers = atoi(p_SupportedReceivers);
    }

    p_MaximumRTSPURILength = xml_attr_get(p_node, "MaximumRTSPURILength");
    if (p_MaximumRTSPURILength)
    {
        p_res->MaximumRTSPURILength = atoi(p_MaximumRTSPURILength);
    }

    return TRUE;
}

BOOL parse_ReceiverService(XMLN * p_node, onvif_ReceiverCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ReceiverServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_StreamSetup(XMLN * p_node, onvif_StreamSetup * p_res)
{
    XMLN * p_Stream;
    XMLN * p_Transport;

    p_Stream = xml_node_soap_get(p_node, "Stream");
    if (p_Stream && p_Stream->data)
    {
        p_res->Stream = onvif_StringToStreamType(p_Stream->data);
    }

    p_Transport = xml_node_soap_get(p_node, "Transport");
    if (p_Transport)
    {
        XMLN * p_Protocol = xml_node_soap_get(p_Transport, "Protocol");
        if (p_Protocol && p_Protocol->data)
        {
            p_res->Transport.Protocol = onvif_StringToTransportProtocol(p_Protocol->data);
        }
    }

    return TRUE;
}

BOOL parse_ReceiverConfiguration(XMLN * p_node, onvif_ReceiverConfiguration * p_res)
{
    XMLN * p_Mode;
    XMLN * p_MediaUri;
    XMLN * p_StreamSetup;

    p_Mode = xml_node_soap_get(p_node, "Mode");
    if (p_Mode && p_Mode->data)
    {
        p_res->Mode = onvif_StringToReceiverMode(p_Mode->data);
    }

    p_MediaUri = xml_node_soap_get(p_node, "MediaUri");
    if (p_MediaUri && p_MediaUri->data)
    {
        strncpy(p_res->MediaUri, p_MediaUri->data, sizeof(p_res->MediaUri)-1);
    }

    p_StreamSetup = xml_node_soap_get(p_node, "StreamSetup");
    if (p_StreamSetup)
    {
        parse_StreamSetup(p_StreamSetup, &p_res->StreamSetup);
    }

    return TRUE;
}

BOOL parse_Receiver(XMLN * p_node, onvif_Receiver * p_res)
{
    XMLN * p_Token;
    XMLN * p_Configuration;

    p_Token = xml_node_soap_get(p_node, "Token");
    if (p_Token && p_Token->data)
    {
        strncpy(p_res->Token, p_Token->data, sizeof(p_res->Token)-1);
    }

    p_Configuration = xml_node_soap_get(p_node, "Configuration");
    if (p_Configuration)
    {
        parse_ReceiverConfiguration(p_Configuration, &p_res->Configuration);
    }

    return TRUE;
}

BOOL parse_ReceiverStateInformation(XMLN * p_node, onvif_ReceiverStateInformation * p_res)
{
    XMLN * p_State;
    XMLN * p_AutoCreated;

    p_State = xml_node_soap_get(p_node, "State");
    if (p_State && p_State->data)
    {
        p_res->State = onvif_StringToReceiverState(p_State->data);
    }

    p_AutoCreated = xml_node_soap_get(p_node, "AutoCreated");
    if (p_AutoCreated && p_AutoCreated->data)
    {
        p_res->AutoCreated = parse_Bool(p_AutoCreated->data);
    }

    return TRUE;
}

BOOL parse_trv_GetServiceCapabilities(XMLN * p_node, trv_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ReceiverServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_trv_GetReceivers(XMLN * p_node, trv_GetReceivers_RES * p_res)
{
    XMLN * p_Receivers;

    p_Receivers = xml_node_soap_get(p_node, "Receivers");
    while (p_Receivers && soap_strcmp(p_Receivers->name, "Receivers") == 0)
    {
        ReceiverList * p_Receiver = onvif_add_Receiver(&p_res->Receivers);
        if (p_Receiver)
        {
            parse_Receiver(p_Receivers, &p_Receiver->Receiver);
        }

        p_Receivers = p_Receivers->next;
    }

    return TRUE;
}

BOOL parse_trv_GetReceiver(XMLN * p_node, trv_GetReceiver_RES * p_res)
{
    XMLN * p_Receiver;

    p_Receiver = xml_node_soap_get(p_node, "Receiver");
    if (p_Receiver)
    {
        parse_Receiver(p_Receiver, &p_res->Receiver);
    }

    return TRUE;
}

BOOL parse_trv_CreateReceiver(XMLN * p_node, trv_CreateReceiver_RES * p_res)
{
    XMLN * p_Receiver;

    p_Receiver = xml_node_soap_get(p_node, "Receiver");
    if (p_Receiver)
    {
        parse_Receiver(p_Receiver, &p_res->Receiver);
    }

    return TRUE;
}

BOOL parse_trv_GetReceiverState(XMLN * p_node, trv_GetReceiverState_RES * p_res)
{
    XMLN * p_ReceiverState;

    p_ReceiverState = xml_node_soap_get(p_node, "ReceiverState");
    if (p_ReceiverState)
    {
        parse_ReceiverStateInformation(p_ReceiverState, &p_res->ReceiverState);
    }
    
    return TRUE;
}

#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

BOOL parse_SourceCapabilities_xml(XMLN * p_node, onvif_SourceCapabilities * p_res)
{
    const char * p_VideoSourceToken;
    const char * p_MaximumPanMoves;
    const char * p_MaximumTiltMoves;
    const char * p_MaximumZoomMoves;
    const char * p_MaximumRollMoves;
    const char * p_AutoLevel;
    const char * p_MaximumFocusMoves;
    const char * p_AutoFocus;

    p_VideoSourceToken = xml_attr_get(p_node, "RTP_Multicast");
    if (p_VideoSourceToken)
    {
        strncpy(p_res->VideoSourceToken, p_VideoSourceToken, sizeof(p_res->VideoSourceToken)-1);
    }

    p_MaximumPanMoves = xml_attr_get(p_node, "MaximumPanMoves");
    if (p_MaximumPanMoves)
    {
        p_res->MaximumPanMovesFlag = 1;
        p_res->MaximumPanMoves = atoi(p_MaximumPanMoves);
    }

    p_MaximumTiltMoves = xml_attr_get(p_node, "MaximumTiltMoves");
    if (p_MaximumTiltMoves)
    {
        p_res->MaximumTiltMovesFlag = 1;
        p_res->MaximumTiltMoves = atoi(p_MaximumTiltMoves);
    }

    p_MaximumZoomMoves = xml_attr_get(p_node, "MaximumZoomMoves");
    if (p_MaximumZoomMoves)
    {
        p_res->MaximumZoomMovesFlag = 1;
        p_res->MaximumZoomMoves = atoi(p_MaximumZoomMoves);
    }

    p_MaximumRollMoves = xml_attr_get(p_node, "MaximumRollMoves");
    if (p_MaximumRollMoves)
    {
        p_res->MaximumRollMovesFlag = 1;
        p_res->MaximumRollMoves = atoi(p_MaximumRollMoves);
    }

    p_AutoLevel = xml_attr_get(p_node, "AutoLevel");
    if (p_AutoLevel)
    {
        p_res->AutoLevelFlag = 1;
        p_res->AutoLevel = parse_Bool(p_AutoLevel);
    }

    p_MaximumFocusMoves = xml_attr_get(p_node, "MaximumFocusMoves");
    if (p_MaximumFocusMoves)
    {
        p_res->MaximumFocusMovesFlag = 1;
        p_res->MaximumFocusMoves = atoi(p_MaximumFocusMoves);
    }

    p_AutoFocus = xml_attr_get(p_node, "AutoFocus");
    if (p_AutoFocus)
    {
        p_res->AutoFocusFlag = 1;
        p_res->AutoFocus = parse_Bool(p_AutoFocus);
    }

    return TRUE;
}

BOOL parse_ProvisioningServiceCapabilities(XMLN * p_node, onvif_ProvisioningCapabilities * p_res)
{
    XMLN * p_DefaultTimeout;
    XMLN * p_Source;

    p_DefaultTimeout = xml_node_soap_get(p_node, "DefaultTimeout");
    if (p_DefaultTimeout && p_DefaultTimeout->data)
    {
        parse_XSDDuration(p_DefaultTimeout->data, &p_res->DefaultTimeout);
    }

    p_res->sizeSource = 0;

    p_Source = xml_node_soap_get(p_node, "Source");
    while (p_Source && soap_strcmp(p_Source->name, "Source") == 0)
    {
        uint32 idx = p_res->sizeSource;
        
        parse_SourceCapabilities_xml(p_Source, &p_res->Source[idx]);

        p_res->sizeSource++;
        if (p_res->sizeSource >= ARRAY_SIZE(p_res->Source))
        {
            break;
        }
        
        p_Source = p_Source->next;
    }

    return TRUE;
}

BOOL parse_ProvisioningService(XMLN * p_node, onvif_ProvisioningCapabilities * p_res)
{
    XMLN * p_XAddr;
    XMLN * p_tds_Capabilities;
    XMLN * p_Version;
    
    p_XAddr = xml_node_soap_get(p_node, "XAddr");
    if (p_XAddr && p_XAddr->data)
    {
        parse_XAddr(p_XAddr->data, &p_res->XAddr);
    }
    else
    {
        return FALSE;
    }

    p_tds_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_tds_Capabilities)
    {
        XMLN * p_Capabilities = xml_node_soap_get(p_tds_Capabilities, "Capabilities");
        if (p_Capabilities)
        {
            parse_ProvisioningServiceCapabilities(p_Capabilities, p_res);
        }
    }

    p_Version = xml_node_soap_get(p_node, "Version");
    if (p_Version)
    {
        parse_Version(p_Version, &p_res->Version);
    }

    return TRUE;
}

BOOL parse_Usage(XMLN * p_node, onvif_Usage * p_res)
{
    XMLN * p_Pan;
    XMLN * p_Tilt;
    XMLN * p_Zoom;
    XMLN * p_Roll;
    XMLN * p_Focus;

    p_Pan = xml_node_soap_get(p_node, "Pan");
    if (p_Pan && p_Pan->data)
    {
        p_res->PanFlag = 1;
        p_res->Pan = atoi(p_Pan->data);
    }

    p_Tilt = xml_node_soap_get(p_node, "Tilt");
    if (p_Tilt && p_Tilt->data)
    {
        p_res->TiltFlag = 1;
        p_res->Tilt = atoi(p_Tilt->data);
    }

    p_Zoom = xml_node_soap_get(p_node, "Zoom");
    if (p_Zoom && p_Zoom->data)
    {
        p_res->ZoomFlag = 1;
        p_res->Zoom = atoi(p_Zoom->data);
    }

    p_Roll = xml_node_soap_get(p_node, "Roll");
    if (p_Roll && p_Roll->data)
    {
        p_res->RollFlag = 1;
        p_res->Roll = atoi(p_Roll->data);
    }

    p_Focus = xml_node_soap_get(p_node, "Focus");
    if (p_Focus && p_Focus->data)
    {
        p_res->FocusFlag = 1;
        p_res->Focus = atoi(p_Focus->data);
    }

    return TRUE;
}

BOOL parse_tpv_GetServiceCapabilities(XMLN * p_node, tpv_GetServiceCapabilities_RES * p_res)
{
    XMLN * p_Capabilities;

    p_Capabilities = xml_node_soap_get(p_node, "Capabilities");
    if (p_Capabilities)
    {
        p_res->Capabilities.support = parse_ProvisioningServiceCapabilities(p_Capabilities, &p_res->Capabilities);
    }

    return p_res->Capabilities.support;
}

BOOL parse_tpv_GetUsage(XMLN * p_node, tpv_GetUsage_RES * p_res)
{
    XMLN * p_Usage;

    p_Usage = xml_node_soap_get(p_node, "Usage");
    if (p_Usage)
    {
        parse_Usage(p_Usage, &p_res->Usage);
    }

    return TRUE;
}

#endif // end of PROVISIONING_SUPPORT




